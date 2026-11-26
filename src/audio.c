/* tinydaw audio engine: sample storage, tone generation, file import, sample-rate changes,
 * mixing and export. libsndfile reads and writes files; libsamplerate converts rates. */
#include "daw.h"
#include "compat.h"

#include <math.h>
#include <samplerate.h>
#include <sndfile.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846
#define CLIP_FADE 64        /* frames of fade at clip edges, so cuts don't click */

int g_rate = 48000;

const char *WAVE_NAMES[WAVE_COUNT] = {"sine", "square", "saw", "tri", "noise", "kick", "snare", "hat"};
const int PROJECT_RATES[6] = {44100, 48000, 88200, 96000, 176400, 192000};

const ExportFormat EXPORT_FORMATS[EXP_COUNT] = {
    {"WAV",  "wav",  SF_FORMAT_WAV,                              0, 0,     0,     32},
    {"AIFF", "aiff", SF_FORMAT_AIFF,                             0, 0,     0,     32},
    {"FLAC", "flac", SF_FORMAT_FLAC,                             0, 0,     0,     24},
    {"OGG",  "ogg",  SF_FORMAT_OGG | SF_FORMAT_VORBIS,           1, 0,     0,     0},
    {"Opus", "opus", SF_FORMAT_OGG | SF_FORMAT_OPUS,             1, 48000, 48000, 0},
    {"MP3",  "mp3",  SF_FORMAT_MPEG | SF_FORMAT_MPEG_LAYER_III,  1, 48000, 0,     0},
};

void project_init(Project *p)
{
    memset(p, 0, sizeof *p);
    p->bpm = 120;
    p->a4 = 440;
    p->master = 0.8f;
    p->preview = -1;
    p->ntracks = DEFAULT_TRACKS;
    p->inst_sample = -1;
    p->inst_root = 60;
    for (int i = 0; i < NPADS; i++) p->pads[i] = (Pad){-1, PAD_ONESHOT, 0.8f, 0, 0};
    for (int i = 0; i < MAX_TRACKS; i++) {
        p->tracks[i].volume = 0.8f;
        snprintf(p->tracks[i].name, sizeof p->tracks[i].name, "Track %d", i + 1);
    }
}

int project_add_track(Project *p, const char *name)
{
    if (p->ntracks >= MAX_TRACKS) return -1;
    Track *t = &p->tracks[p->ntracks];
    t->volume = 0.8f;
    t->mute = t->solo = 0;
    if (name) snprintf(t->name, sizeof t->name, "%s", name);
    else snprintf(t->name, sizeof t->name, "Track %d", p->ntracks + 1);
    return p->ntracks++;
}

long project_end(const Project *p)
{
    long end = 0;
    for (int i = 0; i < p->nclips; i++)
        if (p->clips[i].start + p->clips[i].length > end)
            end = p->clips[i].start + p->clips[i].length;
    return end;
}

void note_name(int note, char *buf, size_t n)
{
    static const char *names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    if (note < 0) note = 0;
    snprintf(buf, n, "%s%d", names[note % 12], note / 12 - 1);
}

/* ---------------------------------------------------------------- resampling */

/* Converts interleaved audio by `ratio` (out rate / in rate). Returns malloc'd data. */
static float *resample(const float *in, long frames, int channels, double ratio, long *out_frames,
                       int quality)
{
    long cap = (long)ceil(frames * ratio) + 64;
    float *out = malloc((size_t)cap * channels * sizeof(float));
    if (!out) return NULL;
    SRC_DATA d;
    memset(&d, 0, sizeof d);
    d.data_in = in;
    d.input_frames = frames;
    d.data_out = out;
    d.output_frames = cap;
    d.src_ratio = ratio;
    d.end_of_input = 1;
    if (src_simple(&d, quality, channels) != 0) {
        free(out);
        return NULL;
    }
    *out_frames = d.output_frames_gen;
    return out;
}

/* ---------------------------------------------------------------- samples */

static int build_peaks(Sample *s)
{
    free(s->peak_min);
    free(s->peak_max);
    s->npeaks = (s->frames + PEAK_BLOCK - 1) / PEAK_BLOCK;
    s->peak_min = malloc(s->npeaks * sizeof(float));
    s->peak_max = malloc(s->npeaks * sizeof(float));
    if (!s->peak_min || !s->peak_max) return -1;
    for (long b = 0; b < s->npeaks; b++) {
        float lo = 1, hi = -1;
        long end = (b + 1) * PEAK_BLOCK < s->frames ? (b + 1) * PEAK_BLOCK : s->frames;
        for (long i = b * PEAK_BLOCK; i < end; i++) {
            float v = 0.5f * (s->data[2 * i] + s->data[2 * i + 1]);
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        s->peak_min[b] = lo;
        s->peak_max[b] = hi;
    }
    return 0;
}

int sample_add(Project *p, const char *name, float *data, long frames)
{
    if (p->nsamples >= MAX_SAMPLES || frames <= 0) {
        free(data);
        return -1;
    }
    Sample *s = &p->samples[p->nsamples];
    memset(s, 0, sizeof *s);
    snprintf(s->name, sizeof s->name, "%s", name);
    s->data = data;
    s->frames = frames;
    if (build_peaks(s) != 0) {
        free(s->peak_min);
        free(s->peak_max);
        free(data);
        memset(s, 0, sizeof *s);
        return -1;
    }
    return p->nsamples++;
}

void sample_minmax(const Sample *s, long f0, long f1, float *lo, float *hi)
{
    if (f0 < 0) f0 = 0;
    if (f1 > s->frames) f1 = s->frames;
    *lo = 0;
    *hi = 0;
    if (f1 <= f0) return;
    float a = 1, b = -1;
    if (f1 - f0 >= 2 * PEAK_BLOCK) {             /* wide range: use the overview */
        for (long i = f0 / PEAK_BLOCK; i <= (f1 - 1) / PEAK_BLOCK; i++) {
            if (s->peak_min[i] < a) a = s->peak_min[i];
            if (s->peak_max[i] > b) b = s->peak_max[i];
        }
    } else {                                     /* zoomed in: read the frames */
        for (long i = f0; i < f1; i++) {
            float v = 0.5f * (s->data[2 * i] + s->data[2 * i + 1]);
            if (v < a) a = v;
            if (v > b) b = v;
        }
    }
    *lo = a;
    *hi = b;
}

/* ---------------------------------------------------------------- generation */

static uint32_t rng_state = 0x9E3779B9u;

static float noise(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return (float)(rng_state / 2147483647.5 - 1.0);
}

/* Band-limited step correction, so square and saw don't alias badly. */
static double polyblep(double t, double dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt) {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

/* Synthesizes a sound at the current project rate. */
static float *render_gen(const GenParams *g, double bpm, long *frames)
{
    long n = (long)(g->beats * 60.0 / bpm * SR);
    if (n < 16) n = 16;
    float *d = malloc((size_t)n * 2 * sizeof(float));
    if (!d) return NULL;

    int drum = g->wave >= WAVE_KICK;
    double freq = (g->a4 > 0 ? g->a4 : 440.0) * pow(2.0, (g->note - 69) / 12.0);
    double dt = freq / SR, phase = 0, prev_noise = 0;
    long attack = drum ? 0 : (long)(g->attack_ms * SR / 1000);
    long release = drum ? SR / 200 : (long)(g->release_ms * SR / 1000);
    if (attack + release > n) {                  /* keep both ramps inside the sound */
        double k = (double)n / (attack + release);
        attack = (long)(attack * k);
        release = (long)(release * k);
    }
    /* the hat's brightness comes from differencing noise, which depends on the rate;
     * scale it so the hat sounds the same at 44.1 kHz and 192 kHz */
    double hat_gain = 0.5 * sqrt(48000.0 / SR);

    for (long i = 0; i < n; i++) {
        double t = (double)i / SR, s = 0;
        switch (g->wave) {
        case WAVE_SINE:
            s = sin(2 * PI * phase);
            break;
        case WAVE_SQUARE:
            s = (phase < 0.5 ? 1.0 : -1.0) + polyblep(phase, dt) - polyblep(fmod(phase + 0.5, 1.0), dt);
            s *= 0.7;
            break;
        case WAVE_SAW:
            s = (2 * phase - 1) - polyblep(phase, dt);
            s *= 0.7;
            break;
        case WAVE_TRIANGLE:
            s = 2 * fabs(2 * phase - 1) - 1;
            break;
        case WAVE_NOISE:
            s = noise() * 0.6;
            break;
        case WAVE_KICK: {                        /* sine with a falling pitch */
            double f = 48 + 120 * exp(-t * 30);
            phase += f / SR;
            s = sin(2 * PI * phase) * exp(-t * 7);
            break;
        }
        case WAVE_SNARE:                         /* noise burst over a short tone */
            s = noise() * exp(-t * 20) * 0.7 + sin(2 * PI * 185 * t) * exp(-t * 28) * 0.5;
            break;
        case WAVE_HAT: {                         /* differentiated noise = bright hiss */
            double w = noise();
            s = (w - prev_noise) * hat_gain * exp(-t * 45);
            prev_noise = w;
            break;
        }
        }
        if (!drum) {
            phase += dt;
            if (phase >= 1.0) phase -= 1.0;
        }
        double env = 1.0;
        if (attack > 0 && i < attack) env = (double)i / attack;
        if (release > 0 && i >= n - release) env = fmin(env, (double)(n - 1 - i) / release);
        float v = (float)(s * env * g->volume);
        d[2 * i] = v;
        d[2 * i + 1] = v;
    }
    *frames = n;
    return d;
}

int sample_generate(Project *p, const GenParams *g)
{
    long n;
    float *d = render_gen(g, p->bpm, &n);
    if (!d) return -1;
    char name[96], nn[16];
    if (g->wave >= WAVE_KICK) {
        snprintf(name, sizeof name, "%s %gb", WAVE_NAMES[g->wave], g->beats);
    } else {
        note_name(g->note, nn, sizeof nn);
        snprintf(name, sizeof name, "%s %s %gb", WAVE_NAMES[g->wave], nn, g->beats);
    }
    int idx = sample_add(p, name, d, n);
    if (idx >= 0) {
        p->samples[idx].generated = 1;
        p->samples[idx].gen = *g;
        p->samples[idx].gen_bpm = p->bpm;
    }
    return idx;
}

float *render_notes(const GenParams *g, double bpm, const int *notes, int n, long *frames)
{
    float *acc = NULL;
    long len = 0;
    float gain = 0.8f / sqrtf((float)(n > 0 ? n : 1));   /* keeps 3-5 note chords near unity */
    for (int k = 0; k < n; k++) {
        GenParams one = *g;
        one.note = notes[k];
        long l;
        float *d = render_gen(&one, bpm, &l);
        if (!d) {
            free(acc);
            return NULL;
        }
        if (!acc) {
            acc = d;
            len = l;
            for (long i = 0; i < 2 * len; i++) acc[i] *= gain;
        } else {
            for (long i = 0; i < 2 * len && i < 2 * l; i++) acc[i] += d[i] * gain;
            free(d);
        }
    }
    *frames = len;
    return acc;
}

int sample_generate_notes(Project *p, const GenParams *g, const int *notes, int n, const char *name)
{
    if (n < 1 || n > 6) return -1;
    long len;
    float *d = render_notes(g, p->bpm, notes, n, &len);
    if (!d) return -1;
    int idx = sample_add(p, name, d, len);
    if (idx >= 0) {
        Sample *s = &p->samples[idx];
        s->generated = 1;
        s->gen = *g;
        s->gen_bpm = p->bpm;
        s->nchord = n;
        memcpy(s->chord, notes, (size_t)n * sizeof(int));
    }
    return idx;
}

/* ---------------------------------------------------------------- drum loops */

/* One drum hit (mono) for a lane, synthesized at the project rate. */
static float *drum_hit(int lane, const BeatSpec *b, long *len)
{
    static const double seconds[DRUM_LANES] = {0, 0.35, 0.45, 0.2, 0.9, 0.15};
    double secs = lane == 0 ? fmin(2.2, 7.0 / b->kick_decay) : seconds[lane];
    long n = (long)(secs * SR);
    float *d = calloc((size_t)n, sizeof(float));
    if (!d) return NULL;
    double phase = 0, prev = 0, prev2 = 0, lp = 0, bright = sqrt(48000.0 / SR);
    for (long i = 0; i < n; i++) {
        double t = (double)i / SR, s = 0, w = noise();
        double hp = (w - prev) * bright, hp2 = (hp - prev2);   /* brighter and brighter noise */
        prev = w;
        prev2 = hp;
        switch (lane) {
        case 0: {                                /* kick: pitch drop onto a tuned body, short click */
            double f = b->kick_hz * (1 + 2.5 * exp(-t * 65));   /* settles on pitch within ~50 ms */
            phase += f / SR;
            s = sin(2 * PI * phase) * exp(-t * b->kick_decay) + w * exp(-t * 400) * 0.25;
            s = tanh(1.6 * s) / tanh(1.6);
            break;
        }
        case 1:                                  /* snare: two tuned modes and a noise burst */
            s = sin(2 * PI * b->snare_hz * t) * exp(-t * 30) * 0.45 +
                sin(2 * PI * b->snare_hz * 1.6 * t) * exp(-t * 38) * 0.2 + w * exp(-t * 21) * 0.6;
            break;
        case 2: {                                /* clap: three quick bursts and a tail */
            double env = 0;
            for (int k = 0; k < 3; k++) {
                double tk = t - k * 0.011;
                if (tk >= 0) env += exp(-tk * 320);
            }
            if (t > 0.03) env += 0.55 * exp(-(t - 0.03) * 16);
            lp += 0.35 * (hp - lp);                /* band-limit the hiss to mid-highs */
            s = lp * env * 1.6;
            break;
        }
        case 3:                                  /* closed hat */
            s = hp2 * 0.5 * exp(-t * b->hat_decay);
            break;
        case 4:                                  /* open hat */
            s = hp2 * 0.45 * exp(-t * b->open_decay);
            break;
        case 5:                                  /* percussion: rim/woodblock */
            s = sin(2 * PI * 1700 * t) * exp(-t * 60) * 0.5 + sin(2 * PI * 820 * t) * exp(-t * 80) * 0.3 +
                w * exp(-t * 300) * 0.3;
            break;
        }
        d[i] = (float)s;
    }
    *len = n;
    return d;
}

float *render_beat(const BeatSpec *b, int mask, double bpm, long *frames)
{
    static const float lane_gain[DRUM_LANES] = {0.9f, 0.7f, 0.65f, 0.38f, 0.32f, 0.45f};
    int steps = b->steps == 32 ? 32 : 16;
    double sd = 60.0 / bpm / 4 * SR;             /* frames per 16th */
    long n = (long)llround(steps * sd);
    float *out = calloc((size_t)n * 2, sizeof(float));
    if (!out) return NULL;
    /* where every hit lands, for all lanes (chokes need them even for lanes not rendered) */
    long at[DRUM_LANES][96];
    float vel[DRUM_LANES][96];
    int count[DRUM_LANES] = {0};
    for (int lane = 0; lane < DRUM_LANES; lane++) {
        for (int st = 0; st < steps && b->lanes[lane][st]; st++) {
            char c = b->lanes[lane][st];
            float v = c == 'X' ? 1.2f : c == 'x' ? 1.0f : c == 'o' ? 0.42f : c == 'r' ? 0.65f : 0;
            if (v == 0) continue;
            double pos = st * sd;
            if (b->swing8 && st % 4 == 2) pos += b->swing * 2 * sd;      /* late off-beat 8ths */
            else if (!b->swing8 && st % 2 == 1) pos += b->swing * sd;    /* late off-beat 16ths */
            for (int h = 0; h < (c == 'r' ? 3 : 1); h++) {
                at[lane][count[lane]] = (long)llround(pos + h * sd / 3) % n;
                vel[lane][count[lane]++] = v * (h ? 0.8f : 1.0f);
            }
        }
    }
    for (int lane = 0; lane < DRUM_LANES; lane++) {
        if (!(mask & (1 << lane)) || !count[lane]) continue;
        long hl;
        float *hit = drum_hit(lane, b, &hl);
        if (!hit) continue;
        for (int k = 0; k < count[lane]; k++) {
            /* chokes, as on a drum machine: a new kick cuts off the last one, and a closed
             * or open hat cuts off a ringing open hat */
            long limit = hl;
            int chokers[2] = {lane == 0 ? 0 : lane == 4 ? 3 : -1, lane == 4 ? 4 : -1};
            for (int c = 0; c < 2; c++) {
                if (chokers[c] < 0) continue;
                for (int j = 0; j < count[chokers[c]]; j++) {
                    long d = ((at[chokers[c]][j] - at[lane][k]) % n + n) % n;
                    if (d > 0 && d < limit) limit = d;
                }
            }
            float g = vel[lane][k] * lane_gain[lane] * (float)b->volume;
            long fade = limit < hl ? (limit < 96 ? limit : 96) : 0;
            for (long i = 0; i < limit; i++) {   /* tails wrap round, so the loop repeats seamlessly */
                float e = fade && i >= limit - fade ? (float)(limit - i) / fade : 1.0f;
                long idx = (at[lane][k] + i) % n;
                out[2 * idx] += hit[i] * g * e;
                out[2 * idx + 1] += hit[i] * g * e;
            }
        }
        free(hit);
    }
    *frames = n;
    return out;
}

int sample_generate_beat(Project *p, const BeatSpec *b, int mask, const char *name)
{
    long len;
    float *d = render_beat(b, mask, p->bpm, &len);
    if (!d) return -1;
    int idx = sample_add(p, name, d, len);
    if (idx >= 0) {
        Sample *s = &p->samples[idx];
        s->generated = 1;
        s->is_beat = 1;
        s->beat = *b;
        s->beat_mask = mask;
        s->gen_bpm = p->bpm;
    }
    return idx;
}

/* ---------------------------------------------------------------- import */

/* Reads a file as stereo float at the project rate. */
static float *load_file(const char *path, long *frames, char *err, size_t errlen)
{
    SF_INFO info;
    memset(&info, 0, sizeof info);
    SNDFILE *sf = sf_open(path, SFM_READ, &info);
    if (!sf) {
        snprintf(err, errlen, "%s", sf_strerror(NULL));
        return NULL;
    }
    if (info.frames <= 0 || info.channels <= 0 || info.samplerate <= 0) {
        sf_close(sf);
        snprintf(err, errlen, "file has no audio");
        return NULL;
    }
    float *raw = malloc((size_t)info.frames * info.channels * sizeof(float));
    if (!raw) {
        sf_close(sf);
        snprintf(err, errlen, "out of memory");
        return NULL;
    }
    long got = (long)sf_readf_float(sf, raw, info.frames);
    sf_close(sf);
    if (got <= 0) {
        free(raw);
        snprintf(err, errlen, "could not read audio");
        return NULL;
    }

    float *st = malloc((size_t)got * 2 * sizeof(float));   /* mono -> both sides, >2 -> first two */
    if (!st) {
        free(raw);
        snprintf(err, errlen, "out of memory");
        return NULL;
    }
    int ch = info.channels;
    for (long i = 0; i < got; i++) {
        st[2 * i] = raw[i * ch];
        st[2 * i + 1] = raw[i * ch + (ch > 1)];
    }
    free(raw);

    if (info.samplerate == SR) {
        *frames = got;
        return st;
    }
    float *out = resample(st, got, 2, (double)SR / info.samplerate, frames, SRC_SINC_MEDIUM_QUALITY);
    free(st);
    if (!out) snprintf(err, errlen, "could not convert the sample rate");
    return out;
}

int sample_import(Project *p, const char *path, char *err, size_t errlen)
{
    long n;
    float *d = load_file(path, &n, err, errlen);
    if (!d) return -1;
    int idx = sample_add(p, path_base(path), d, n);
    if (idx < 0) {
        snprintf(err, errlen, "library is full");
        return -1;
    }
    char *full = full_path(path);
    snprintf(p->samples[idx].path, sizeof p->samples[idx].path, "%s", full ? full : path);
    free(full);
    return idx;
}

/* ---------------------------------------------------------------- pitched copies */

/* Resamples so the sound plays `semis` semitones higher at the same rate. */
static float *pitch_copy(const Sample *s, int semis, long *frames)
{
    return resample(s->data, s->frames, 2, pow(2.0, -semis / 12.0), frames, SRC_SINC_MEDIUM_QUALITY);
}

int sample_pitched(Project *p, int src, int semis)
{
    if (src < 0 || src >= p->nsamples) return -1;
    if (semis == 0) return src;
    const Sample *s = &p->samples[src];
    char name[96];
    snprintf(name, sizeof name, "%.80s %+d", s->name, semis);
    for (int i = 0; i < p->nsamples; i++) {
        const Sample *o = &p->samples[i];
        if (!strcmp(o->name, name) && ((o->pitched && o->src == src && o->semis == semis) || o->generated))
            return i;
    }
    /* synthesized sounds are simply made again at the new pitch; anything else is resampled */
    if (s->generated && !s->is_beat && s->gen.wave < WAVE_KICK) {
        GenParams g = s->gen;
        long n;
        float *d;
        int notes[6];
        if (s->nchord) {
            for (int k = 0; k < s->nchord; k++) notes[k] = s->chord[k] + semis;
            d = render_notes(&g, s->gen_bpm, notes, s->nchord, &n);
        } else {
            g.note += semis;
            d = render_gen(&g, s->gen_bpm, &n);
        }
        int idx = d ? sample_add(p, name, d, n) : -1;
        if (idx >= 0) {
            Sample *t = &p->samples[idx];
            t->generated = 1;
            t->gen = g;
            t->gen_bpm = p->samples[src].gen_bpm;
            t->nchord = p->samples[src].nchord;
            memcpy(t->chord, notes, sizeof notes);
        }
        return idx;
    }
    long n;
    float *d = pitch_copy(s, semis, &n);
    if (!d) return -1;
    int idx = sample_add(p, name, d, n);
    if (idx >= 0) {
        p->samples[idx].pitched = 1;
        p->samples[idx].src = src;
        p->samples[idx].semis = semis;
    }
    return idx;
}

/* ---------------------------------------------------------------- rate change */

int project_set_rate(Project *p, int rate, char *msg, size_t msglen)
{
    if (rate == g_rate) {
        snprintf(msg, msglen, "Project rate is already %d Hz", rate);
        return 0;
    }
    double ratio = (double)rate / g_rate;
    int old_rate = g_rate, rebuilt = 0, resampled = 0, failed = 0;
    g_rate = rate;                               /* render_gen and load_file use it */

    for (int i = 0; i < p->nsamples; i++) {
        Sample *s = &p->samples[i];
        float *nd = NULL;
        long nf = 0;
        char err[128];
        if (s->is_beat) nd = render_beat(&s->beat, s->beat_mask, s->gen_bpm, &nf);
        else if (s->generated && s->nchord) nd = render_notes(&s->gen, s->gen_bpm, s->chord, s->nchord, &nf);
        else if (s->generated) nd = render_gen(&s->gen, s->gen_bpm, &nf);
        else if (s->path[0]) nd = load_file(s->path, &nf, err, sizeof err);
        else if (s->pitched && s->src >= 0 && s->src < i)
            nd = pitch_copy(&p->samples[s->src], s->semis, &nf);
        if (nd) {
            rebuilt++;
        } else {
            nd = resample(s->data, s->frames, 2, ratio, &nf, SRC_SINC_BEST_QUALITY);
            if (!nd) {
                failed++;
                continue;
            }
            resampled++;
        }
        free(s->data);
        s->data = nd;
        s->frames = nf;
        build_peaks(s);
    }
    for (int c = 0; c < p->nclips; c++) {
        Clip *cl = &p->clips[c];
        long frames = p->samples[cl->sample].frames;
        cl->start = llround(cl->start * ratio);
        cl->offset = llround(cl->offset * ratio);
        cl->length = llround(cl->length * ratio);
        if (cl->offset > frames - 1) cl->offset = frames - 1;
        if (cl->offset + cl->length > frames) cl->length = frames - cl->offset;
        if (cl->length < 1) cl->length = 1;
    }
    p->playhead = llround(p->playhead * ratio);
    p->loop_start = llround(p->loop_start * ratio);
    p->loop_end = llround(p->loop_end * ratio);
    p->preview = -1;
    voices_clear(p);

    int n = snprintf(msg, msglen, "Project rate %g kHz (was %g): %d sounds rebuilt at full quality",
                     rate / 1000.0, old_rate / 1000.0, rebuilt);
    if (resampled && n > 0 && (size_t)n < msglen)
        n += snprintf(msg + n, msglen - n, ", %d resampled (source file missing)", resampled);
    if (failed && n > 0 && (size_t)n < msglen)
        snprintf(msg + n, msglen - n, ", %d FAILED (out of memory)", failed);
    return failed ? -1 : 0;
}

/* ---------------------------------------------------------------- mixing */

/* Linear below 0.8, then a smooth knee that never quite reaches full scale, so
 * stacked sounds get louder gently instead of hard-clipping. */
float soft_clip(float v)
{
    const float knee = 0.8f;
    float a = fabsf(v);
    if (a <= knee) return v;
    float shaped = knee + (1.0f - knee) * tanhf((a - knee) / (1.0f - knee));
    return v < 0 ? -shaped : shaped;
}

void mix(const Project *p, long start, float *out, int frames, int only_track)
{
    int any_solo = 0;
    for (int t = 0; t < p->ntracks; t++)
        any_solo |= p->tracks[t].solo;

    for (int c = 0; c < p->nclips; c++) {
        const Clip *cl = &p->clips[c];
        const Track *tr = &p->tracks[cl->track];
        if (only_track >= 0) {
            if (cl->track != only_track) continue;
        } else if (tr->mute || (any_solo && !tr->solo)) {
            continue;
        }
        long a = start > cl->start ? start : cl->start;
        long b = start + frames < cl->start + cl->length ? start + frames : cl->start + cl->length;
        if (a >= b) continue;
        const Sample *s = &p->samples[cl->sample];
        for (long f = a; f < b; f++) {
            long rel = f - cl->start, src = cl->offset + rel;
            if (src < 0 || src >= s->frames) continue;
            float g = tr->volume;
            long right = cl->length - 1 - rel;
            if (rel < CLIP_FADE) g *= (float)rel / CLIP_FADE;
            if (right < CLIP_FADE) g *= (float)right / CLIP_FADE;
            out[2 * (f - start)] += s->data[2 * src] * g;
            out[2 * (f - start) + 1] += s->data[2 * src + 1] * g;
        }
    }
}

/* ---------------------------------------------------------------- live voices */

#define VOICE_FADE (SR / 100)       /* 10 ms fade when a voice is stopped early */

void voices_clear(Project *p)
{
    for (int i = 0; i < MAX_VOICES; i++) p->voices[i].active = 0;
}

int voice_start(Project *p, int sample, double step, float gain, int loop, int pad, int note)
{
    if (sample < 0 || sample >= p->nsamples || p->samples[sample].frames <= 0) return -1;
    int best = -1;
    for (int i = 0; i < MAX_VOICES && best < 0; i++)
        if (!p->voices[i].active) best = i;
    if (best < 0) {                              /* all busy: take the one furthest through */
        double most = -1;
        for (int i = 0; i < MAX_VOICES; i++) {
            const Voice *v = &p->voices[i];
            double done = v->pos / p->samples[v->sample].frames;
            if (done > most) {
                most = done;
                best = i;
            }
        }
    }
    p->voices[best] = (Voice){1, sample, pad, note, 0, step > 0 ? step : 1, gain, loop, 0};
    return best;
}

void voice_stop(Project *p, int pad, int note)
{
    for (int i = 0; i < MAX_VOICES; i++) {
        Voice *v = &p->voices[i];
        if (v->active && !v->fade && ((pad >= 0 && v->pad == pad) || (note >= 0 && v->pad < 0 && v->note == note)))
            v->fade = VOICE_FADE > 0 ? VOICE_FADE : 1;
    }
}

int voice_playing(const Project *p, int pad)
{
    for (int i = 0; i < MAX_VOICES; i++)
        if (p->voices[i].active && p->voices[i].pad == pad && !p->voices[i].fade) return 1;
    return 0;
}

void voices_mix(Project *p, float *out, int frames)
{
    for (int k = 0; k < MAX_VOICES; k++) {
        Voice *v = &p->voices[k];
        if (!v->active) continue;
        if (v->sample >= p->nsamples) {
            v->active = 0;
            continue;
        }
        const Sample *s = &p->samples[v->sample];
        long n = s->frames;
        for (int i = 0; i < frames; i++) {
            if (v->pos >= n - 1) {
                if (!v->loop || n < 2) {
                    v->active = 0;
                    break;
                }
                v->pos -= n - 1;
            }
            long a = (long)v->pos;
            float t = (float)(v->pos - a), g = v->gain;
            if (v->fade) {
                g *= (float)v->fade / VOICE_FADE;
                if (--v->fade == 0) {
                    v->active = 0;
                    break;
                }
            }
            float l = s->data[2 * a] + (s->data[2 * a + 2] - s->data[2 * a]) * t;
            float r = s->data[2 * a + 1] + (s->data[2 * a + 3] - s->data[2 * a + 1]) * t;
            out[2 * i] += l * g;
            out[2 * i + 1] += r * g;
            v->pos += v->step;
        }
    }
}

/* ---------------------------------------------------------------- export */

void export_defaults(ExportOptions *o)
{
    memset(o, 0, sizeof *o);
    o->format = EXP_WAV;
    o->bits = 24;
    o->quality = 0.7;
    o->dither = 1;
}

void export_effective(const ExportOptions *o, int *rate, int *bits)
{
    const ExportFormat *f = &EXPORT_FORMATS[o->format];
    int r = o->rate ? o->rate : SR;
    if (f->fixed_rate) r = f->fixed_rate;
    if (f->max_rate && r > f->max_rate) r = r % 44100 == 0 ? 44100 : 48000;
    *rate = r;
    int b = o->bits;
    if (f->lossy) b = 0;
    else if (b > f->max_bits) b = f->max_bits;
    *bits = b;
}

static float *render_range(const Project *p, long a, long b, int only_track)
{
    long n = b - a;
    float *buf = calloc((size_t)n * 2, sizeof(float));
    if (!buf) return NULL;
    for (long pos = 0; pos < n; pos += 4096) {
        int k = n - pos < 4096 ? (int)(n - pos) : 4096;
        mix(p, a + pos, buf + 2 * pos, k, only_track);
    }
    for (long i = 0; i < 2 * n; i++)
        buf[i] = soft_clip(buf[i] * p->master);
    return buf;
}

static float tpdf(void)                           /* triangular noise, +-1 LSB at 16-bit */
{
    return (noise() + noise()) * 0.5f / 32768.0f;
}

/* Converts a rendered stereo buffer (project rate) according to the options and writes it. */
static int write_audio(const char *path, const ExportOptions *o, const float *buf, long n, int apply_norm,
                       char *err, size_t errlen)
{
    const ExportFormat *f = &EXPORT_FORMATS[o->format];
    int rate, bits;
    export_effective(o, &rate, &bits);

    long frames = n;
    float *data;
    if (rate != SR) {
        data = resample(buf, n, 2, (double)rate / SR, &frames, SRC_SINC_BEST_QUALITY);
    } else {
        data = malloc((size_t)n * 2 * sizeof(float));
        if (data) memcpy(data, buf, (size_t)n * 2 * sizeof(float));
    }
    if (!data) {
        snprintf(err, errlen, "out of memory");
        return -1;
    }
    int ch = 2;
    if (o->mono) {
        for (long i = 0; i < frames; i++) data[i] = 0.5f * (data[2 * i] + data[2 * i + 1]);
        ch = 1;
    }
    long count = frames * ch;
    if (apply_norm && o->normalize) {
        float peak = 0;
        for (long i = 0; i < count; i++) peak = fabsf(data[i]) > peak ? fabsf(data[i]) : peak;
        if (peak > 0) {
            float g = (float)pow(10.0, (o->normalize == 1 ? -1.0 : -0.1) / 20.0) / peak;
            for (long i = 0; i < count; i++) data[i] *= g;
        }
    }
    if (bits == 16 && o->dither)
        for (long i = 0; i < count; i++) data[i] += tpdf();

    SF_INFO info;
    memset(&info, 0, sizeof info);
    info.samplerate = rate;
    info.channels = ch;
    info.format = f->sf_format;
    if (!f->lossy)
        info.format |= bits == 16 ? SF_FORMAT_PCM_16 : bits == 24 ? SF_FORMAT_PCM_24 : SF_FORMAT_FLOAT;
    SNDFILE *sf = sf_open(path, SFM_WRITE, &info);
    if (!sf) {
        snprintf(err, errlen, "%s", sf_strerror(NULL));
        free(data);
        return -1;
    }
    sf_command(sf, SFC_SET_CLIPPING, NULL, SF_TRUE);
    if (f->lossy) {
        if (o->format == EXP_MP3) {
            int mode = SF_BITRATE_MODE_VARIABLE;
            sf_command(sf, SFC_SET_BITRATE_MODE, &mode, sizeof mode);
        }
        double q = o->quality;
        sf_command(sf, SFC_SET_VBR_ENCODING_QUALITY, &q, sizeof q);
    }
    int rc = 0;
    for (long pos = 0; pos < frames; pos += 8192) {
        long k = frames - pos < 8192 ? frames - pos : 8192;
        if (sf_writef_float(sf, data + pos * ch, k) != k) {
            snprintf(err, errlen, "write failed: %s", sf_strerror(sf));
            rc = -1;
            break;
        }
    }
    if (sf_close(sf) != 0 && rc == 0) {
        snprintf(err, errlen, "could not finish the file");
        rc = -1;
    }
    free(data);
    if (rc != 0) remove(path);
    return rc;
}

int project_export(const Project *p, const ExportOptions *o, const char *base, char *msg, size_t msglen)
{
    long a = 0, b = project_end(p);
    if (o->loop_only) {
        if (p->loop_end <= p->loop_start) {
            snprintf(msg, msglen, "no loop region set");
            return -1;
        }
        a = p->loop_start;
        b = p->loop_end;
    }
    if (b <= a) {
        snprintf(msg, msglen, "nothing to export: the timeline is empty");
        return -1;
    }
    const ExportFormat *f = &EXPORT_FORMATS[o->format];
    char path[1200], first[1200] = "", err[256];
    int files = 0;

    for (int t = o->stems ? 0 : -1; t < (o->stems ? p->ntracks : 0); t++) {
        if (t >= 0) {                            /* skip tracks with nothing in the range */
            int used = 0;
            for (int c = 0; c < p->nclips && !used; c++)
                used = p->clips[c].track == t && p->clips[c].start < b &&
                       p->clips[c].start + p->clips[c].length > a;
            if (!used) continue;
            snprintf(path, sizeof path, "%s-track%d.%s", base, t + 1, f->ext);
        } else {
            snprintf(path, sizeof path, "%s.%s", base, f->ext);
        }
        float *buf = render_range(p, a, b, t);
        if (!buf) {
            snprintf(msg, msglen, "out of memory");
            return -1;
        }
        int rc = write_audio(path, o, buf, b - a, t < 0, err, sizeof err);
        free(buf);
        if (rc != 0) {
            snprintf(msg, msglen, "%s: %s", path, err);
            return -1;
        }
        if (!files++) snprintf(first, sizeof first, "%s", path);
    }
    if (!files) {
        snprintf(msg, msglen, "nothing to export in that range");
        return -1;
    }
    int rate, bits;
    export_effective(o, &rate, &bits);
    char depth[24];
    if (f->lossy) snprintf(depth, sizeof depth, "q%.0f%%", o->quality * 100);
    else snprintf(depth, sizeof depth, bits == 32 ? "32-bit float" : "%d-bit", bits);
    if (files == 1)
        snprintf(msg, msglen, "Exported %s (%s, %g kHz, %s, %s)", first, f->name, rate / 1000.0, depth,
                 o->mono ? "mono" : "stereo");
    else
        snprintf(msg, msglen, "Exported %d stems like %s (%s, %g kHz, %s)", files, first, f->name,
                 rate / 1000.0, depth);
    return 0;
}
