/* tinydaw mixer: clips and live voices, through each track's effects, into the shared delay
 * and reverb. Playback and export both run through here, so an exported file sounds exactly
 * like what you heard.
 *
 * Per track, in order: drive (tanh saturation), 3-band EQ (RBJ shelf/peak biquads), filter
 * (a state-variable filter: low/high/band-pass with resonance), volume, pan (equal power) and
 * sidechain ducking. After the fader each track sends to a tempo-synced delay (optionally
 * ping-pong) and a Freeverb-style reverb, which return into the mix. */
#include "daw.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

const double DELAY_BEATS[DELAY_DIVS] = {0.25, 1.0 / 3, 0.5, 2.0 / 3, 0.75, 1, 1.5, 2};
const char *DELAY_NAMES[DELAY_DIVS] = {"1/16", "1/8T", "1/8", "1/4T", "1/8.", "1/4", "1/4.", "1/2"};

#define BLOCK 256
#define CLIP_EDGE 64            /* frames of fade at clip edges, so cuts don't click */
#define NCOMB 8
#define NAP 4
#define DELAY_SECONDS 4
#define PI_F 3.14159265358979323846

typedef struct { double b0, b1, b2, a1, a2, z1[2], z2[2]; } Biquad;
typedef struct { float *buf; int len, pos; float lp; } Comb;
typedef struct { float *buf; int len, pos; } Allpass;

struct Mixer {
    int rate;
    float trk[MAX_TRACKS][2 * BLOCK];
    float duck[BLOCK];
    double ic1[MAX_TRACKS][2], ic2[MAX_TRACKS][2];      /* filter state */
    Biquad eq[MAX_TRACKS][3];
    float eq_set[MAX_TRACKS][3];
    int eq_ready[MAX_TRACKS];
    float gl[MAX_TRACKS], gr[MAX_TRACKS];               /* last fader gains, for smooth changes */
    int g_ready[MAX_TRACKS];
    float *dl[2];
    long dlen, dpos;
    float dlp[2];
    Comb comb[2][NCOMB];
    Allpass ap[2][NAP];
    long tail_left;                                     /* frames the sends keep ringing */
    float peak[MAX_TRACKS][2];                          /* loudest sample per track since last read */
    float env;                                          /* sidechain envelope */
};

static const int COMB_TUNING[NCOMB] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
static const int AP_TUNING[NAP] = {556, 441, 341, 225};

Mixer *mixer_new(void)
{
    Mixer *m = calloc(1, sizeof *m);
    if (!m) return NULL;
    m->rate = SR;
    m->dlen = (long)DELAY_SECONDS * SR;
    int ok = 1;
    for (int c = 0; c < 2; c++) {
        m->dl[c] = calloc((size_t)m->dlen, sizeof(float));
        ok &= m->dl[c] != NULL;
        double scale = SR / 44100.0;
        for (int i = 0; i < NCOMB; i++) {
            m->comb[c][i].len = (int)((COMB_TUNING[i] + 23 * c) * scale);
            m->comb[c][i].buf = calloc((size_t)m->comb[c][i].len, sizeof(float));
            ok &= m->comb[c][i].buf != NULL;
        }
        for (int i = 0; i < NAP; i++) {
            m->ap[c][i].len = (int)((AP_TUNING[i] + 23 * c) * scale);
            m->ap[c][i].buf = calloc((size_t)m->ap[c][i].len, sizeof(float));
            ok &= m->ap[c][i].buf != NULL;
        }
    }
    if (!ok) {
        mixer_free(m);
        return NULL;
    }
    return m;
}

void mixer_free(Mixer *m)
{
    if (!m) return;
    for (int c = 0; c < 2; c++) {
        free(m->dl[c]);
        for (int i = 0; i < NCOMB; i++) free(m->comb[c][i].buf);
        for (int i = 0; i < NAP; i++) free(m->ap[c][i].buf);
    }
    free(m);
}

int mixer_rate(const Mixer *m) { return m ? m->rate : 0; }

void mixer_levels(Mixer *m, float out[][2])
{
    for (int t = 0; t < MAX_TRACKS; t++) {
        out[t][0] = m ? m->peak[t][0] : 0;
        out[t][1] = m ? m->peak[t][1] : 0;
        if (m) m->peak[t][0] = m->peak[t][1] = 0;
    }
}

int project_has_tails(const Project *p)
{
    for (int t = 0; t < p->ntracks; t++) {
        if (p->tracks[t].delay_send > 0 && p->fx.delay_level > 0) return 1;
        if (p->tracks[t].reverb_send > 0 && p->fx.reverb_level > 0) return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- automation */

const char *AUTO_NAMES[AUTO_PARAMS] = {"Volume", "Pan", "Cutoff", "Delay send", "Reverb send"};

float auto_to_value(int param, float v)
{
    v = v < 0 ? 0 : v > 1 ? 1 : v;
    switch (param) {
    case AUTO_VOLUME: return 1.5f * v;          /* the fader's range: 1.0 sits two thirds up */
    case AUTO_PAN: return 2 * v - 1;
    case AUTO_CUTOFF: return 20.0f * powf(1000.0f, v);       /* 20 Hz .. 20 kHz, evenly by ear */
    default: return v;
    }
}

float value_to_auto(int param, float value)
{
    float v = param == AUTO_VOLUME ? value / 1.5f : param == AUTO_PAN ? (value + 1) / 2 : param == AUTO_CUTOFF ? logf(fmaxf(value, 20) / 20) / logf(1000) : value;
    return v < 0 ? 0 : v > 1 ? 1 : v;
}

static int auto_cmp(const void *a, const void *b)
{
    const AutoPoint *x = a, *y = b;
    if (x->track != y->track) return x->track - y->track;
    if (x->param != y->param) return x->param - y->param;
    return x->at < y->at ? -1 : x->at > y->at;
}

void auto_sort(Project *p) { qsort(p->autos, (size_t)p->nautos, sizeof(AutoPoint), auto_cmp); }

int auto_lane(const Project *p, int track, int param, int *first)
{
    int lo = 0, hi = p->nautos;                  /* the first point at or after (track, param) */
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        const AutoPoint *q = &p->autos[mid];
        if (q->track < track || (q->track == track && q->param < param)) lo = mid + 1;
        else hi = mid;
    }
    int n = 0;
    while (lo + n < p->nautos && p->autos[lo + n].track == track && p->autos[lo + n].param == param) n++;
    if (first) *first = lo;
    return n;
}

float auto_at(const Project *p, int track, int param, long at, int *has)
{
    int first, n = auto_lane(p, track, param, &first);
    if (has) *has = n > 0;
    if (!n) return 0;
    const AutoPoint *a = &p->autos[first];
    if (at <= a[0].at) return a[0].v;
    if (at >= a[n - 1].at) return a[n - 1].v;
    int lo = 0, hi = n - 1;                      /* a[lo].at <= at < a[hi].at */
    while (hi - lo > 1) {
        int mid = (lo + hi) / 2;
        if (a[mid].at <= at) lo = mid;
        else hi = mid;
    }
    double k = (double)(at - a[lo].at) / (double)(a[hi].at - a[lo].at);
    return (float)(a[lo].v + (a[hi].v - a[lo].v) * k);
}

void auto_apply(const Project *p, int track, long at, Track *out)
{
    *out = p->tracks[track];
    if (!p->nautos) return;
    int has;
    float v = auto_at(p, track, AUTO_VOLUME, at, &has);
    if (has) out->volume = auto_to_value(AUTO_VOLUME, v);
    v = auto_at(p, track, AUTO_PAN, at, &has);
    if (has) out->pan = auto_to_value(AUTO_PAN, v);
    v = auto_at(p, track, AUTO_CUTOFF, at, &has);
    if (has) {
        out->cutoff = auto_to_value(AUTO_CUTOFF, v);
        if (out->filter == FILT_OFF) out->filter = FILT_LOW;   /* automating the cutoff turns the filter on */
    }
    v = auto_at(p, track, AUTO_DELAY, at, &has);
    if (has) out->delay_send = auto_to_value(AUTO_DELAY, v);
    v = auto_at(p, track, AUTO_REVERB, at, &has);
    if (has) out->reverb_send = auto_to_value(AUTO_REVERB, v);
}

/* ---------------------------------------------------------------- note clips */

int sample_root(const Sample *s)
{
    if (s->generated && !s->is_beat) return s->nchord ? s->chord[0] : s->gen.note;
    return 60;                                   /* drums and imported sounds: as is at middle C */
}

long pattern_end(const Project *p, int pattern)
{
    long end = 0;
    for (int i = 0; i < p->nnotes; i++)
        if (p->notes[i].pattern == pattern && p->notes[i].start + p->notes[i].length > end)
            end = p->notes[i].start + p->notes[i].length;
    return end;
}

int pattern_new_id(const Project *p)
{
    int id = 0;
    for (int i = 0; i < p->nclips; i++) id = p->clips[i].pattern > id ? p->clips[i].pattern : id;
    for (int i = 0; i < p->nnotes; i++) id = p->notes[i].pattern > id ? p->notes[i].pattern : id;
    return id + 1;
}

/* ---------------------------------------------------------------- sources */

#define NOTE_ATTACK 32                  /* frames: no click where a note starts mid-sound */
#define NOTE_RELEASE (SR / 33)          /* the fade after a note's end */

static void add_pattern(Mixer *m, const Project *p, const Clip *cl, long start, int n)
{
    const Sample *s = &p->samples[cl->sample];
    if (!s->data || s->frames < 2) return;
    long ca = start > cl->start ? start : cl->start;
    long cb = start + n < cl->start + cl->length ? start + n : cl->start + cl->length;
    if (ca >= cb) return;
    long origin = cl->start - cl->offset, rel_f = NOTE_RELEASE;
    float gain = cl->gain_db == 0 ? 1.0f : powf(10.0f, cl->gain_db / 20.0f);
    float *out = m->trk[cl->track];
    for (int k = 0; k < p->nnotes; k++) {
        const Note *nt = &p->notes[k];
        if (nt->pattern != cl->pattern) continue;
        long ns = origin + nt->start, ne = ns + nt->length;
        long a = ca > ns ? ca : ns, b = cb < ne + rel_f ? cb : ne + rel_f;
        if (a >= b) continue;
        double step = pow(2.0, (nt->note - cl->root) / 12.0);
        long last = ns + (long)((s->frames - 1) / step);     /* where the sound runs out */
        if (b > last) b = last;
        for (long f = a; f < b; f++) {
            double pos = (f - ns) * step;
            long i = (long)pos;
            float fr = (float)(pos - i), g = gain * nt->vel;
            long rel = f - cl->start, right = cl->length - 1 - rel, into = f - ns;
            if (rel < CLIP_EDGE) g *= (float)rel / CLIP_EDGE;
            if (right < CLIP_EDGE) g *= (float)right / CLIP_EDGE;
            if (cl->fade_in > 0 && rel < cl->fade_in) g *= (float)rel / cl->fade_in;
            if (cl->fade_out > 0 && right < cl->fade_out) g *= (float)right / cl->fade_out;
            if (into < NOTE_ATTACK) g *= (float)into / NOTE_ATTACK;
            if (f >= ne) g *= 1 - (float)(f - ne) / rel_f;
            const float *d = s->data + 2 * i;
            out[2 * (f - start)] += (d[0] + (d[2] - d[0]) * fr) * g;
            out[2 * (f - start) + 1] += (d[1] + (d[3] - d[1]) * fr) * g;
        }
    }
}

static void add_clips(Mixer *m, const Project *p, long start, int n)
{
    for (int c = 0; c < p->nclips; c++) {
        const Clip *cl = &p->clips[c];
        if (cl->track < 0 || cl->track >= p->ntracks) continue;
        if (cl->pattern > 0) {
            add_pattern(m, p, cl, start, n);
            continue;
        }
        long a = start > cl->start ? start : cl->start;
        long b = start + n < cl->start + cl->length ? start + n : cl->start + cl->length;
        if (a >= b) continue;
        const Sample *s = &p->samples[cl->sample];
        float *out = m->trk[cl->track];
        float gain = cl->gain_db == 0 ? 1.0f : powf(10.0f, cl->gain_db / 20.0f);
        for (long f = a; f < b; f++) {
            long rel = f - cl->start, src = cl->offset + rel;
            if (src < 0 || src >= s->frames) continue;
            float g = gain;
            long right = cl->length - 1 - rel;
            if (rel < CLIP_EDGE) g *= (float)rel / CLIP_EDGE;
            if (right < CLIP_EDGE) g *= (float)right / CLIP_EDGE;
            if (cl->fade_in > 0 && rel < cl->fade_in) g *= (float)rel / cl->fade_in;
            if (cl->fade_out > 0 && right < cl->fade_out) g *= (float)right / cl->fade_out;
            out[2 * (f - start)] += s->data[2 * src] * g;
            out[2 * (f - start) + 1] += s->data[2 * src + 1] * g;
        }
    }
}

#define VOICE_FADE (SR / 100)       /* matches voice_stop in audio.c */

static void add_voices(Mixer *m, Project *p, int frames)
{
    double bend = pow(2.0, p->bend * p->fx.bend_range / 12.0);
    for (int k = 0; k < MAX_VOICES; k++) {
        Voice *v = &p->voices[k];
        if (!v->active) continue;
        if (v->sample >= p->nsamples) {
            v->active = 0;
            continue;
        }
        const Sample *s = &p->samples[v->sample];
        int t = v->track >= 0 && v->track < p->ntracks ? v->track : 0;
        float *out = m->trk[t];
        float pl = v->pan <= 0 ? 1 : 1 - v->pan, pr = v->pan >= 0 ? 1 : 1 + v->pan;
        long n = s->frames;
        double step = v->step * bend;
        for (int i = 0; i < frames; i++) {
            if (v->pos >= n - 1) {
                if (!v->loop || n < 2) {
                    v->active = 0;
                    break;
                }
                v->pos -= n - 1;
            }
            long a = (long)v->pos;
            float tt = (float)(v->pos - a), g = v->gain;
            if (v->fade) {
                g *= (float)v->fade / VOICE_FADE;
                if (--v->fade == 0) {
                    v->active = 0;
                    break;
                }
            }
            float l = s->data[2 * a] + (s->data[2 * a + 2] - s->data[2 * a]) * tt;
            float r = s->data[2 * a + 1] + (s->data[2 * a + 3] - s->data[2 * a + 1]) * tt;
            out[2 * i] += l * g * pl;
            out[2 * i + 1] += r * g * pr;
            v->pos += step;
        }
    }
}

/* ---------------------------------------------------------------- per-track effects */

static void biquad_set(Biquad *q, int kind, double f, double gain_db, int rate)
{
    double A = pow(10.0, gain_db / 40.0), w = 2 * PI_F * f / rate, cs = cos(w), sn = sin(w);
    double b0, b1, b2, a0, a1, a2;
    if (kind == 1) {                             /* peak, Q 0.8 */
        double al = sn / (2 * 0.8);
        b0 = 1 + al * A; b1 = -2 * cs; b2 = 1 - al * A;
        a0 = 1 + al / A; a1 = -2 * cs; a2 = 1 - al / A;
    } else {                                     /* shelves, slope 1 */
        double al = sn / 2 * sqrt(2.0), sa = 2 * sqrt(A) * al;
        if (kind == 0) {
            b0 = A * ((A + 1) - (A - 1) * cs + sa); b1 = 2 * A * ((A - 1) - (A + 1) * cs);
            b2 = A * ((A + 1) - (A - 1) * cs - sa); a0 = (A + 1) + (A - 1) * cs + sa;
            a1 = -2 * ((A - 1) + (A + 1) * cs);     a2 = (A + 1) + (A - 1) * cs - sa;
        } else {
            b0 = A * ((A + 1) + (A - 1) * cs + sa); b1 = -2 * A * ((A - 1) + (A + 1) * cs);
            b2 = A * ((A + 1) + (A - 1) * cs - sa); a0 = (A + 1) - (A - 1) * cs + sa;
            a1 = 2 * ((A - 1) - (A + 1) * cs);      a2 = (A + 1) - (A - 1) * cs - sa;
        }
    }
    q->b0 = b0 / a0; q->b1 = b1 / a0; q->b2 = b2 / a0; q->a1 = a1 / a0; q->a2 = a2 / a0;
}

static void biquad_run(Biquad *q, float *buf, int n)
{
    for (int i = 0; i < n; i++)
        for (int c = 0; c < 2; c++) {
            double x = buf[2 * i + c], y = q->b0 * x + q->z1[c];
            q->z1[c] = q->b1 * x - q->a1 * y + q->z2[c];
            q->z2[c] = q->b2 * x - q->a2 * y;
            buf[2 * i + c] = (float)y;
        }
}

static void process_track(Mixer *m, const Project *p, int t, const Track *tr, float *b, int n)
{
    if (tr->drive > 0.001f) {                    /* soft saturation, level-matched at the top */
        float k = 1 + 9 * tr->drive * tr->drive, norm = 1.0f / tanhf(k), makeup = 1 / (1 + 0.6f * tr->drive);
        for (int i = 0; i < 2 * n; i++) b[i] = tanhf(k * b[i]) * norm * makeup;
    }
    if (fabsf(tr->eq[0]) > 0.05f || fabsf(tr->eq[1]) > 0.05f || fabsf(tr->eq[2]) > 0.05f) {
        static const double freq[3] = {150, 1000, 6000};
        for (int band = 0; band < 3; band++) {
            if (!m->eq_ready[t] || m->eq_set[t][band] != tr->eq[band]) {
                biquad_set(&m->eq[t][band], band, freq[band], tr->eq[band], m->rate);
                m->eq_set[t][band] = tr->eq[band];
            }
        }
        m->eq_ready[t] = 1;
        for (int band = 0; band < 3; band++)
            if (fabsf(tr->eq[band]) > 0.05f) biquad_run(&m->eq[t][band], b, n);
    }
    if (tr->filter != FILT_OFF) {
        double fc = tr->cutoff;
        if (t == p->inst_track && p->modwheel > 0) fc *= pow(2.0, 7.0 * p->modwheel);   /* mod wheel opens it */
        if (fc < 20) fc = 20;
        if (fc > 0.45 * m->rate) fc = 0.45 * m->rate;
        double g = tan(PI_F * fc / m->rate), k = 2 - 1.94 * tr->reso;
        double a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2;
        for (int c = 0; c < 2; c++) {
            double ic1 = m->ic1[t][c], ic2 = m->ic2[t][c];
            for (int i = 0; i < n; i++) {
                double v0 = b[2 * i + c], v3 = v0 - ic2;
                double v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2 * v1 - ic1;
                ic2 = 2 * v2 - ic2;
                double y = tr->filter == FILT_LOW ? v2 : tr->filter == FILT_BAND ? v1 * k : v0 - k * v1 - v2;
                b[2 * i + c] = (float)y;
            }
            m->ic1[t][c] = ic1;
            m->ic2[t][c] = ic2;
        }
    }
}

/* ---------------------------------------------------------------- shared effects */

static void run_delay(Mixer *m, const Project *p, const float *in, float *out, int n)
{
    const MasterFx *fx = &p->fx;
    long d = (long)(DELAY_BEATS[fx->delay_div % DELAY_DIVS] * 60.0 / p->bpm * m->rate);
    if (d < 1) d = 1;
    if (d > m->dlen - 1) d = m->dlen - 1;
    float fb = fx->delay_feedback > 0.95f ? 0.95f : fx->delay_feedback;
    float tone = 0.05f + 0.95f * fx->delay_tone * fx->delay_tone;
    for (int i = 0; i < n; i++) {
        long rd = (m->dpos - d + m->dlen) % m->dlen;
        float yl = m->dl[0][rd], yr = m->dl[1][rd];
        m->dlp[0] += tone * (yl - m->dlp[0]);
        m->dlp[1] += tone * (yr - m->dlp[1]);
        if (fx->pingpong) {                      /* echoes bounce left, right, left... */
            m->dl[0][m->dpos] = 0.5f * (in[2 * i] + in[2 * i + 1]) + fb * m->dlp[1];
            m->dl[1][m->dpos] = fb * m->dlp[0];
        } else {
            m->dl[0][m->dpos] = in[2 * i] + fb * m->dlp[0];
            m->dl[1][m->dpos] = in[2 * i + 1] + fb * m->dlp[1];
        }
        m->dpos = (m->dpos + 1) % m->dlen;
        out[2 * i] += fx->delay_level * m->dlp[0];
        out[2 * i + 1] += fx->delay_level * m->dlp[1];
    }
}

static void run_reverb(Mixer *m, const Project *p, const float *in, float *out, int n)
{
    const MasterFx *fx = &p->fx;
    float feedback = 0.7f + 0.28f * fx->reverb_size, damp = 0.4f * fx->reverb_damp;
    for (int i = 0; i < n; i++) {
        float x = (in[2 * i] + in[2 * i + 1]) * 0.015f;
        for (int c = 0; c < 2; c++) {
            float acc = 0;
            for (int k = 0; k < NCOMB; k++) {
                Comb *cb = &m->comb[c][k];
                float y = cb->buf[cb->pos];
                cb->lp = y * (1 - damp) + cb->lp * damp;
                cb->buf[cb->pos] = x + cb->lp * feedback;
                if (++cb->pos >= cb->len) cb->pos = 0;
                acc += y;
            }
            for (int k = 0; k < NAP; k++) {
                Allpass *a = &m->ap[c][k];
                float bo = a->buf[a->pos];
                a->buf[a->pos] = acc + bo * 0.5f;
                acc = bo - acc;
                if (++a->pos >= a->len) a->pos = 0;
            }
            out[2 * i + c] += fx->reverb_level * acc * 3.0f;
        }
    }
}

/* ---------------------------------------------------------------- the mix */

void mixer_run(Mixer *m, Project *p, long start, int clips, float *out, int frames, int only_track, int voices)
{
    if (!m || m->rate != SR) return;             /* the rate changed: wait for a new mixer */
    int any_solo = 0;
    for (int t = 0; t < p->ntracks; t++) any_solo |= p->tracks[t].solo;
    int sc = p->fx.sc_source;
    float rel = expf(-1.0f / (p->fx.sc_release * 0.001f * m->rate + 1));
    float att = expf(-1.0f / (0.002f * m->rate));

    for (int done = 0; done < frames; done += BLOCK) {
        int n = frames - done < BLOCK ? frames - done : BLOCK;
        float sends_d[2 * BLOCK] = {0}, sends_r[2 * BLOCK] = {0};
        float *o = out + 2 * done;
        for (int t = 0; t < p->ntracks; t++) memset(m->trk[t], 0, sizeof(float) * 2 * n);
        if (clips) add_clips(m, p, start + done, n);
        if (voices) add_voices(m, p, n);

        /* sidechain: follow the source track's level (before its effects) */
        int use_sc = sc >= 0 && sc < p->ntracks;
        for (int i = 0; i < n; i++) {
            if (use_sc) {
                float x = fmaxf(fabsf(m->trk[sc][2 * i]), fabsf(m->trk[sc][2 * i + 1]));
                m->env = x > m->env ? x + (m->env - x) * att : m->env * rel;
            } else {
                m->env *= rel;
            }
            m->duck[i] = m->env / 0.25f > 1 ? 1 : m->env / 0.25f;
        }

        int sending = 0;
        for (int t = 0; t < p->ntracks; t++) {
            Track eff;                           /* the track as its automation has it now */
            auto_apply(p, t, start + done, &eff);
            const Track *tr = &eff;
            if (only_track >= 0 ? t != only_track : (tr->mute || (any_solo && !tr->solo))) continue;
            float *b = m->trk[t];
            process_track(m, p, t, tr, b, n);
            double ang = (tr->pan + 1) * PI_F / 4;
            float gl = tr->volume * (float)(cos(ang) * 1.41421356), gr = tr->volume * (float)(sin(ang) * 1.41421356);
            if (!m->g_ready[t]) {
                m->gl[t] = gl;
                m->gr[t] = gr;
                m->g_ready[t] = 1;
            }
            float duck = t == sc ? 0 : tr->duck;
            for (int i = 0; i < n; i++) {        /* fader moves glide over the block: no zipper noise */
                float k = (float)(i + 1) / n;
                float l = m->gl[t] + (gl - m->gl[t]) * k, r = m->gr[t] + (gr - m->gr[t]) * k;
                float dk = 1 - duck * m->duck[i];
                float L = b[2 * i] * l * dk, R = b[2 * i + 1] * r * dk;
                o[2 * i] += L;
                o[2 * i + 1] += R;
                if (fabsf(L) > m->peak[t][0]) m->peak[t][0] = fabsf(L);
                if (fabsf(R) > m->peak[t][1]) m->peak[t][1] = fabsf(R);
                sends_d[2 * i] += L * tr->delay_send;
                sends_d[2 * i + 1] += R * tr->delay_send;
                sends_r[2 * i] += L * tr->reverb_send;
                sends_r[2 * i + 1] += R * tr->reverb_send;
            }
            m->gl[t] = gl;
            m->gr[t] = gr;
            sending |= tr->delay_send > 0 || tr->reverb_send > 0;
        }
        if (sending) m->tail_left = 8L * m->rate;   /* keep running until the echoes die away */
        if (m->tail_left > 0) {
            run_delay(m, p, sends_d, o, n);
            run_reverb(m, p, sends_r, o, n);
            m->tail_left -= n;
        }
    }
}
