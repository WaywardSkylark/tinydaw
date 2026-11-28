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
    double comp_env, lim_gain, comp_gr, lim_gr;         /* master dynamics */
    double tcomp_env[MAX_TRACKS], tcomp_gr[MAX_TRACKS];  /* each track's compressor */
    float lim_buf[2 * 2048], lim_req[2048];             /* the limiter's look-ahead: sound, and gain it needs */
    int lim_pos;
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

const char *AUTO_NAMES[AUTO_PARAMS] = {"Volume", "Pan", "Cutoff", "Delay send", "Reverb send",
                                       "Macro 1", "Macro 2", "Macro 3", "Macro 4"};

const TrackParamInfo TRACK_PARAMS[TP_COUNT] = {
    [TP_VOLUME] = {"Volume", 0, 1.5f, 0, "%.2f"},   [TP_PAN] = {"Pan", -1, 1, 0, "%+.2f"},
    [TP_CUTOFF] = {"Cutoff", 20, 20000, 1, "%.0f Hz"}, [TP_RESO] = {"Resonance", 0, 1, 0, "%.2f"},
    [TP_EQ_LOW] = {"Low EQ", -12, 12, 0, "%+.1f dB"}, [TP_EQ_MID] = {"Mid EQ", -12, 12, 0, "%+.1f dB"},
    [TP_EQ_HIGH] = {"High EQ", -12, 12, 0, "%+.1f dB"}, [TP_DRIVE] = {"Drive", 0, 1, 0, "%.2f"},
    [TP_DELAY] = {"Delay send", 0, 1, 0, "%.2f"},    [TP_REVERB] = {"Reverb send", 0, 1, 0, "%.2f"},
    [TP_DUCK] = {"Duck", 0, 1, 0, "%.2f"},           [TP_WIDTH] = {"Width", 0, 2, 0, "%.2f"},
    [TP_COMP] = {"Compressor", -40, 0, 0, "%.0f dB"},
};

float *track_param(Track *t, int tp)
{
    switch (tp) {
    case TP_VOLUME: return &t->volume;
    case TP_PAN: return &t->pan;
    case TP_CUTOFF: return &t->cutoff;
    case TP_RESO: return &t->reso;
    case TP_EQ_LOW: return &t->eq[0];
    case TP_EQ_MID: return &t->eq[1];
    case TP_EQ_HIGH: return &t->eq[2];
    case TP_DRIVE: return &t->drive;
    case TP_DELAY: return &t->delay_send;
    case TP_REVERB: return &t->reverb_send;
    case TP_DUCK: return &t->duck;
    case TP_WIDTH: return &t->width;
    case TP_COMP: return &t->comp_thresh;
    }
    return NULL;
}

void macros_apply(Track *t, const float *values)
{
    for (int m = 0; m < TRACK_MACROS; m++) {
        const Macro *mc = &t->macro[m];
        float v = values ? values[m] : mc->value;
        v = v < 0 ? 0 : v > 1 ? 1 : v;
        for (int k = 0; k < mc->nlinks && k < MACRO_LINKS; k++) {
            const MacroLink *l = &mc->link[k];
            float *dst = l->param >= 0 && l->param < TP_COUNT ? track_param(t, l->param) : NULL;
            if (!dst) continue;
            if (TRACK_PARAMS[l->param].log && l->lo > 0 && l->hi > 0) *dst = l->lo * powf(l->hi / l->lo, v);
            else *dst = l->lo + (l->hi - l->lo) * v;
            if (l->param == TP_CUTOFF && t->filter == FILT_OFF) t->filter = FILT_LOW;   /* as automating it does */
            if (l->param == TP_COMP) t->comp_on = 1;
        }
    }
}

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
    float mv[TRACK_MACROS];
    int any = 0;
    for (int m = 0; m < TRACK_MACROS; m++) {
        int has = 0;
        float v = p->nautos ? auto_at(p, track, AUTO_MACRO1 + m, at, &has) : 0;
        mv[m] = has ? v : out->macro[m].value;
        any |= out->macro[m].nlinks > 0;
    }
    if (!p->nautos) {
        if (any) macros_apply(out, mv);
        return;
    }
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
    if (any) macros_apply(out, mv);              /* last: a macro takes over what it's linked to */
}

/* ---------------------------------------------------------------- note clips */

int sample_root(const Sample *s)
{
    if (s->synth) return s->syn.note;
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

DrumPat *drum_find(Project *p, int id)
{
    for (int i = 0; i < p->ndrums; i++)
        if (p->drums[i].id == id) return &p->drums[i];
    return NULL;
}

const DrumPat *drum_find_c(const Project *p, int id) { return drum_find((Project *)p, id); }

int drum_plays(const DrumPat *d, int r, long k, long seed)
{
    int st = (int)(k % d->steps), pr = d->prob[r][st];
    if (!d->vel[r][st]) return 0;
    if (pr <= 0 || pr >= 100) return 1;
    unsigned long h = (unsigned long)seed * 2654435761u ^ (unsigned long)k * 2246822519u ^ (unsigned long)(r + 1) * 3266489917u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return (int)(h % 100) < pr;
}

double drum_step_beat(const DrumPat *d, long k) { return k * 0.25 + ((k & 1) ? d->swing * 0.25 : 0); }

/* ---------------------------------------------------------------- sources */

#define NOTE_ATTACK 32                  /* frames: no click where a note starts mid-sound */
#define NOTE_RELEASE (SR / 33)          /* the fade after a note's end */

static void add_pattern(Mixer *m, const Project *p, const Clip *cl, long start, int n)
{
    if (!p->samples[cl->sample].data || p->samples[cl->sample].frames < 2) return;
    long ca = start > cl->start ? start : cl->start;
    long cb = start + n < cl->start + cl->length ? start + n : cl->start + cl->length;
    if (ca >= cb) return;
    long origin = cl->start - cl->offset, rel_f = NOTE_RELEASE;
    float gain = cl->gain_db == 0 ? 1.0f : powf(10.0f, cl->gain_db / 20.0f);
    float *out = m->trk[cl->track];
    const BendMap *bm = cl->bend ? bend_map(p, cl->bend) : NULL;   /* a pitch curve: notes read through its warp */
    for (int k = 0; k < p->nnotes; k++) {
        const Note *nt = &p->notes[k];
        if (nt->pattern != cl->pattern) continue;
        const Sample *s = &p->samples[sample_pick(p, cl->sample, nt->vel, (unsigned)(nt->start / 997 + nt->note * 7))];
        if (!s->data || s->frames < 2) continue;
        long ns = origin + nt->start, ne = ns + nt->length;
        long a = ca > ns ? ca : ns, b = cb < ne + rel_f ? cb : ne + rel_f;
        if (a >= b) continue;
        double step = pow(2.0, (nt->note - cl->root) / 12.0);
        long last = ns + (long)((s->frames - 1) / step);     /* where the sound runs out */
        if (b > last && !bm) b = last;
        double w0 = bm ? bend_warp(bm, (double)(ns - origin)) : 0;
        for (long f = a; f < b; f++) {
            double pos = bm ? (bend_warp(bm, (double)(f - origin)) - w0) * step : (f - ns) * step;
            long i = (long)pos;
            if (i < 0 || i >= s->frames - 1) {
                if (i < 0) continue;
                break;
            }
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

static void add_drums(Mixer *m, const Project *p, const Clip *cl, long start, int n)
{
    const DrumPat *d = drum_find_c(p, -cl->pattern);
    if (!d || d->steps < 1) return;
    long maxlen = 0;
    int per[DRUM_ROWS], before[DRUM_ROWS][DRUM_STEPS];   /* hits per pass, and before each step: round-robins */
    for (int r = 0; r < d->nrows; r++) {
        if (d->sample[r] >= 0 && d->sample[r] < p->nsamples && p->samples[d->sample[r]].frames > maxlen)
            maxlen = p->samples[d->sample[r]].frames;
        per[r] = 0;
        for (int k = 0; k < d->steps; k++) {
            before[r][k] = per[r];
            per[r] += d->vel[r][k] != 0;
        }
    }
    maxlen += maxlen / 50 + 64;                  /* a round-robin a few cents down is a touch longer */
    if (cl->bend) maxlen *= 4;                   /* bent down, a hit lasts longer (two octaves: four times) */
    long cend = cl->start + cl->length;
    if (start >= cend + maxlen || start + n <= cl->start || !maxlen) return;
    long origin = cl->start - cl->offset, from = start - maxlen;
    if (from < cl->start) from = cl->start;
    double b0 = meter_beats(p, origin);
    long k0 = (long)floor((meter_beats(p, from) - b0) * 4) - 1, k1 = (long)ceil((meter_beats(p, start + n) - b0) * 4);
    if (k0 < 0) k0 = 0;
    float gain = cl->gain_db == 0 ? 1.0f : powf(10.0f, cl->gain_db / 20.0f);
    float *out = m->trk[cl->track];
    const BendMap *bm = cl->bend ? bend_map(p, cl->bend) : NULL;   /* a pitch curve: each hit follows it */
    for (long k = k0; k <= k1; k++) {
        long hf = meter_beats_frame(p, b0 + drum_step_beat(d, k));
        if (hf < cl->start || hf >= cend) continue;
        int st = (int)(k % d->steps);
        for (int r = 0; r < d->nrows; r++) {
            int si = d->sample[r];
            if (si < 0 || si >= p->nsamples || !drum_plays(d, r, k, origin)) continue;
            si = sample_pick(p, si, d->vel[r][st] / 127.0f, (unsigned)((k / d->steps) * per[r] + before[r][st]));
            const Sample *s = &p->samples[si];
            if (!s->data) continue;
            float g = gain * d->row_vol[r] * d->vel[r][st] / 127.0f;
            if (bm) {                            /* bent: read through the warp, as long as the sound lasts */
                double w0 = bend_warp(bm, (double)(hf - origin));
                long a = start > hf ? start : hf;
                for (long f = a; f < start + n; f++) {
                    double pos = bend_warp(bm, (double)(f - origin)) - w0;
                    long i = (long)pos;
                    if (i < 0) continue;
                    if (i >= s->frames - 1) break;
                    float fr = (float)(pos - i);
                    out[2 * (f - start)] += (s->data[2 * i] + (s->data[2 * i + 2] - s->data[2 * i]) * fr) * g;
                    out[2 * (f - start) + 1] += (s->data[2 * i + 1] + (s->data[2 * i + 3] - s->data[2 * i + 1]) * fr) * g;
                }
                continue;
            }
            long a = start > hf ? start : hf, b = start + n < hf + s->frames ? start + n : hf + s->frames;
            for (long f = a; f < b; f++) {
                long i = f - hf;
                out[2 * (f - start)] += s->data[2 * i] * g;
                out[2 * (f - start) + 1] += s->data[2 * i + 1] * g;
            }
        }
    }
}

static void add_clip(Mixer *m, const Project *p, const Clip *cl, long start, int n)
{
    if (cl->pattern > 0) {
        add_pattern(m, p, cl, start, n);
        return;
    }
    if (cl->pattern < 0) {
        add_drums(m, p, cl, start, n);
        return;
    }
    long a = start > cl->start ? start : cl->start;
    long b = start + n < cl->start + cl->length ? start + n : cl->start + cl->length;
    if (a >= b) return;
    const Sample *s = &p->samples[cl->sample];
    float *out = m->trk[cl->track];
    float gain = cl->gain_db == 0 ? 1.0f : powf(10.0f, cl->gain_db / 20.0f);
    const float *data = s->data;
    const BendMap *bm = NULL;
    if (cl->bend) {                          /* a pitch curve: a render that keeps the timing, or tape */
        const Bend *bd = bend_find_c(p, cl->bend);
        if (bd && bd->keep_time) {
            const float *sh = bend_shifted(p, cl->bend, cl->sample);
            if (sh) data = sh;
        } else if (bd) {
            bm = bend_map(p, cl->bend);
        }
    }
    for (long f = a; f < b; f++) {
        long rel = f - cl->start, src = cl->offset + rel;
        float fr = 0;
        if (bm) {
            double pos = bend_warp(bm, (double)(cl->offset + rel));
            src = (long)floor(pos);
            fr = (float)(pos - src);
            if (src >= s->frames - 1) continue;
        }
        if (src < 0 || src >= s->frames) continue;
        float g = gain;
        long right = cl->length - 1 - rel;
        if (rel < CLIP_EDGE) g *= (float)rel / CLIP_EDGE;
        if (right < CLIP_EDGE) g *= (float)right / CLIP_EDGE;
        if (cl->fade_in > 0 && rel < cl->fade_in) g *= (float)rel / cl->fade_in;
        if (cl->fade_out > 0 && right < cl->fade_out) g *= (float)right / cl->fade_out;
        if (fr > 0) {
            out[2 * (f - start)] += (data[2 * src] + (data[2 * src + 2] - data[2 * src]) * fr) * g;
            out[2 * (f - start) + 1] += (data[2 * src + 1] + (data[2 * src + 3] - data[2 * src + 1]) * fr) * g;
        } else {
            out[2 * (f - start)] += data[2 * src] * g;
            out[2 * (f - start) + 1] += data[2 * src + 1] * g;
        }
    }
}

long slot_frames(const Project *p, const Slot *s, long at)
{
    if (s->kind == SLOT_RANGE) {
        long a = meter_bar_frame(p, s->bar);
        return meter_beats_frame(p, meter_beats(p, a) + s->beats) - a;
    }
    return (long)(s->beats * 60.0 / meter_bpm_at(p, at) * SR);
}

void session_reset(Project *p)
{
    for (int t = 0; t < MAX_TRACKS; t++) {
        p->sess_play[t] = SESS_TIMELINE;
        p->sess_q[t] = SESS_NONE;
        p->sess_at[t] = p->sess_q_at[t] = 0;
    }
}

/* A track's launched slot, as copies of clips placed pass after pass from when it was launched. */
static void add_session(Mixer *m, const Project *p, int t, long start, int n)
{
    int sc = p->sess_play[t];
    if (sc < 0 || sc >= p->nscenes) return;
    const Slot *sl = &p->slots[t][sc];
    long at = p->sess_at[t], L = slot_frames(p, sl, at);
    if (sl->kind == SLOT_EMPTY || L < 64) return;
    /* the passes this block touches (and the one before, ringing on); before the launch too, so moving
     * the playhead back keeps the loop going in step */
    long k0 = (long)floor((double)(start - at) / L) - 1, k1 = (long)floor((double)(start + n - at) / L);
    for (long k = k0; k <= k1; k++) {
        long pass = at + k * L;
        if (sl->kind == SLOT_CLIP) {
            Clip c = sl->clip;
            c.track = t;
            c.start = pass;
            long room = c.pattern ? L : p->samples[c.sample].frames - c.offset;
            c.length = room < L ? room : L;
            if (c.length > 0) add_clip(m, p, &c, start, n);
        } else {                                 /* the timeline's own clips between a and a + L, moved here */
            long a = meter_bar_frame(p, sl->bar), z = a + L;
            for (int ci = 0; ci < p->nclips; ci++) {
                Clip c = p->clips[ci];
                if (c.track != t || c.start >= z || c.start + c.length <= a) continue;
                if (c.start < a) {
                    long cut = a - c.start;
                    c.start = a;
                    c.offset += cut;
                    c.length -= cut;
                    c.fade_in = 0;
                }
                if (c.start + c.length > z) {
                    c.length = z - c.start;
                    c.fade_out = 0;
                }
                c.start += pass - a;
                add_clip(m, p, &c, start, n);
            }
        }
    }
}

static void add_clips(Mixer *m, const Project *p, long start, int n, int live)
{
    for (int c = 0; c < p->nclips; c++) {
        const Clip *cl = &p->clips[c];
        if (cl->track < 0 || cl->track >= p->ntracks) continue;
        if (live && p->sess_play[cl->track] != SESS_TIMELINE) continue;   /* the track is playing the session */
        add_clip(m, p, cl, start, n);
    }
    for (int t = 0; live && t < p->ntracks; t++)
        if (p->sess_play[t] >= 0) add_session(m, p, t, start, n);
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
    if (tr->comp_on) {                           /* the track's compressor: like the master's */
        double th = tr->comp_thresh, ratio = tr->comp_ratio > 1 ? tr->comp_ratio : 1;
        double att = exp(-1.0 / (fmax(0.1, tr->comp_attack) * 0.001 * m->rate));
        double rel = exp(-1.0 / (fmax(1, tr->comp_release) * 0.001 * m->rate));
        double makeup = pow(10, tr->comp_makeup / 20), gr = 0, env = m->tcomp_env[t];
        for (int i = 0; i < n; i++) {
            double x = fmax(fabs(b[2 * i]), fabs(b[2 * i + 1]));
            double db = x > 1e-9 ? 20 * log10(x) : -180, over = db - th, want = over > 0 ? over * (1 - 1 / ratio) : 0;
            env = want > env ? want + (env - want) * att : want + (env - want) * rel;
            double g = pow(10, -env / 20) * makeup;
            b[2 * i] *= (float)g;
            b[2 * i + 1] *= (float)g;
            gr = fmax(gr, env);
        }
        m->tcomp_env[t] = env;
        m->tcomp_gr[t] = gr;
    } else {
        m->tcomp_env[t] = m->tcomp_gr[t] = 0;
    }
    if (fabsf(tr->width - 1) > 0.001f) {          /* stereo width: the sides up or down against the middle */
        float w = tr->width < 0 ? 0 : tr->width > 2 ? 2 : tr->width;
        for (int i = 0; i < n; i++) {
            float mid = 0.5f * (b[2 * i] + b[2 * i + 1]), side = 0.5f * (b[2 * i] - b[2 * i + 1]) * w;
            b[2 * i] = mid + side;
            b[2 * i + 1] = mid - side;
        }
    }
}

float mixer_track_gr(const Mixer *m, int track)
{
    return m && track >= 0 && track < MAX_TRACKS ? (float)m->tcomp_gr[track] : 0;
}

/* ---------------------------------------------------------------- shared effects */

static void run_delay(Mixer *m, const Project *p, long at, const float *in, float *out, int n)
{
    const MasterFx *fx = &p->fx;
    long d = (long)(DELAY_BEATS[fx->delay_div % DELAY_DIVS] * 60.0 / meter_bpm_at(p, at) * m->rate);   /* in time where it is */
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
        if (clips && voices)                     /* live: queued launches switch on time */
            for (int t = 0; t < p->ntracks; t++)
                if (p->sess_q[t] != SESS_NONE && p->sess_q_at[t] < start + done + n) {
                    p->sess_play[t] = p->sess_q[t];
                    p->sess_at[t] = p->sess_q_at[t];
                    p->sess_q[t] = SESS_NONE;
                }
        if (clips) add_clips(m, p, start + done, n, voices);
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
            long when = start + done;
            if (voices && clips && p->sess_play[t] >= 0 && p->sess_play[t] < p->nscenes &&
                p->slots[t][p->sess_play[t]].kind == SLOT_RANGE) {   /* looping the timeline: its automation too */
                const Slot *sl = &p->slots[t][p->sess_play[t]];
                long L = slot_frames(p, sl, p->sess_at[t]), into = when - p->sess_at[t];
                if (L > 0 && into >= 0) when = meter_bar_frame(p, sl->bar) + into % L;
            }
            auto_apply(p, t, when, &eff);
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
            run_delay(m, p, start + done, sends_d, o, n);
            run_reverb(m, p, sends_r, o, n);
            m->tail_left -= n;
        }
    }
}

/* ---------------------------------------------------------------- the tempo map
 *
 * The song is cut into sections where tempo and time signature hold still: the start (bpm,
 * ts_num/ts_den) and then each change, from its bar. Within a section frames, beats and bars are
 * in proportion, so every conversion finds its section and scales. */

typedef struct { double frame, beat, bar, qf, barq; double bpm; int num, den; } Sect;

static int sections(const Project *p, Sect *out)
{
    int n = 0;
    double frame = 0, beat = 0, bar = 0, bpm = p->bpm > 0 ? p->bpm : 120;
    int num = p->ts_num > 0 ? p->ts_num : 4, den = p->ts_den > 0 ? p->ts_den : 4;
    for (int i = -1; i < p->nchanges && n < MAX_METER + 1; i++) {
        if (i >= 0) {
            const MeterChange *c = &p->changes[i];
            if (c->bar <= bar) {                 /* out of order or at the same bar: it takes over */
                if (n) n--;
                bar = n ? out[n].bar : 0;
            }
            Sect *pv = n ? &out[n - 1] : NULL;
            if (pv && c->bar > pv->bar) {
                frame = pv->frame + (c->bar - pv->bar) * pv->barq * pv->qf;
                beat = pv->beat + (c->bar - pv->bar) * pv->barq;
            }
            bar = c->bar;
            if (c->bpm > 0) bpm = c->bpm;
            if (c->num > 0) num = c->num;
            if (c->den > 0) den = c->den;
        }
        out[n++] = (Sect){frame, beat, bar, 60.0 / bpm * g_rate, num * 4.0 / den, bpm, num, den};
    }
    return n;
}

double meter_beats(const Project *p, long frame)
{
    Sect s[MAX_METER + 1];
    int n = sections(p, s), k = 0;
    while (k + 1 < n && frame >= s[k + 1].frame) k++;
    return s[k].beat + (frame - s[k].frame) / s[k].qf;
}

long meter_beats_frame(const Project *p, double beats)
{
    Sect s[MAX_METER + 1];
    int n = sections(p, s), k = 0;
    while (k + 1 < n && beats >= s[k + 1].beat) k++;
    return (long)llround(s[k].frame + (beats - s[k].beat) * s[k].qf);
}

double meter_bars(const Project *p, long frame)
{
    Sect s[MAX_METER + 1];
    int n = sections(p, s), k = 0;
    while (k + 1 < n && frame >= s[k + 1].frame) k++;
    return s[k].bar + (frame - s[k].frame) / (s[k].qf * s[k].barq);
}

long meter_bar_frame(const Project *p, double bar)
{
    Sect s[MAX_METER + 1];
    int n = sections(p, s), k = 0;
    while (k + 1 < n && bar >= s[k + 1].bar) k++;
    return (long)llround(s[k].frame + (bar - s[k].bar) * s[k].barq * s[k].qf);
}

void meter_at(const Project *p, long frame, double *bpm, int *num, int *den, int *bar, long *bar_start, long *bar_len)
{
    Sect s[MAX_METER + 1];
    int n = sections(p, s), k = 0;
    while (k + 1 < n && frame >= s[k + 1].frame) k++;
    double bl = s[k].qf * s[k].barq;
    double b = frame < s[k].frame ? 0 : floor((frame - s[k].frame) / bl + 1e-9);
    if (bpm) *bpm = s[k].bpm;
    if (num) *num = s[k].num;
    if (den) *den = s[k].den;
    if (bar) *bar = (int)(s[k].bar + b);
    if (bar_start) *bar_start = (long)llround(s[k].frame + b * bl);
    if (bar_len) *bar_len = (long)llround(bl);
}

double meter_bpm_at(const Project *p, long frame)
{
    double bpm;
    meter_at(p, frame, &bpm, NULL, NULL, NULL, NULL, NULL);
    return bpm;
}

/* ---------------------------------------------------------------- the master's dynamics
 *
 * The compressor follows the louder channel's level in dB (quick to catch, slow to let go) and
 * turns everything above the threshold down by the ratio, then makes the loss up. The limiter
 * looks 5 ms ahead: it hears a peak coming and has the gain down by the time it arrives, so
 * nothing passes the ceiling, and lets go over 80 ms. */
void master_dynamics(Mixer *m, const Project *p, float *buf, int n)
{
    const MasterFx *fx = &p->fx;
    if (!m) return;
    if (fx->comp_on) {
        double th = fx->comp_thresh, ratio = fx->comp_ratio > 1 ? fx->comp_ratio : 1;
        double att = exp(-1.0 / (fmax(0.1, fx->comp_attack) * 0.001 * m->rate));
        double rel = exp(-1.0 / (fmax(1, fx->comp_release) * 0.001 * m->rate));
        double makeup = pow(10, fx->comp_makeup / 20), gr = 0;
        for (int i = 0; i < n; i++) {
            double x = fmax(fabs(buf[2 * i]), fabs(buf[2 * i + 1]));
            double db = x > 1e-9 ? 20 * log10(x) : -180;
            double over = db - th, want = over > 0 ? over * (1 - 1 / ratio) : 0;   /* dB to take off */
            m->comp_env = want > m->comp_env ? want + (m->comp_env - want) * att : want + (m->comp_env - want) * rel;
            double g = pow(10, -m->comp_env / 20) * makeup;
            buf[2 * i] *= (float)g;
            buf[2 * i + 1] *= (float)g;
            gr = fmax(gr, m->comp_env);
        }
        m->comp_gr = gr;
    } else {
        m->comp_env = 0;
        m->comp_gr = 0;
    }
    if (fx->limit_on) {
        int look = (int)(0.005 * m->rate);
        if (look > 2047) look = 2047;
        if (look < 1) look = 1;
        double ceil_ = pow(10, fx->limit_ceiling / 20), rel = exp(-1.0 / (0.08 * m->rate)), gr = 0;
        if (m->lim_gain <= 0) m->lim_gain = 1;
        double att = 1 - exp(-5.0 / look);     /* down within the look-ahead */
        for (int i = 0; i < n; i++) {
            double x = fmax(fabs(buf[2 * i]), fabs(buf[2 * i + 1]));
            int k = m->lim_pos;                 /* out comes what went in `look` samples ago */
            float dl = m->lim_buf[2 * k], dr = m->lim_buf[2 * k + 1];
            m->lim_buf[2 * k] = buf[2 * i];
            m->lim_buf[2 * k + 1] = buf[2 * i + 1];
            m->lim_req[k] = x > ceil_ ? (float)(ceil_ / x) : 1.0f;
            m->lim_pos = (k + 1) % look;
            double target = 1;                  /* the lowest gain anything in the window needs */
            for (int j = 0; j < look; j++)
                if (m->lim_req[j] > 0 && m->lim_req[j] < target) target = m->lim_req[j];
            if (target < m->lim_gain) m->lim_gain += (target - m->lim_gain) * att;
            else m->lim_gain = target + (m->lim_gain - target) * rel;
            double g = m->lim_gain;
            float ol = (float)(dl * g), orr = (float)(dr * g);
            if (fabsf(ol) > ceil_) ol = ol > 0 ? (float)ceil_ : (float)-ceil_;   /* never over */
            if (fabsf(orr) > ceil_) orr = orr > 0 ? (float)ceil_ : (float)-ceil_;
            buf[2 * i] = ol;
            buf[2 * i + 1] = orr;
            gr = fmax(gr, -20 * log10(g));
        }
        m->lim_gr = gr;
    } else {
        m->lim_gain = 1;
        m->lim_gr = 0;
        if (m->lim_pos) {                       /* forget the look-ahead, so turning it on is clean */
            memset(m->lim_buf, 0, sizeof m->lim_buf);
            memset(m->lim_req, 0, sizeof m->lim_req);
            m->lim_pos = 0;
        }
    }
}

void mixer_dynamics_meter(const Mixer *m, float *comp_db, float *limit_db)
{
    *comp_db = m ? (float)m->comp_gr : 0;
    *limit_db = m ? (float)m->lim_gr : 0;
}

double loudness_match(float *buf, long n, double lufs, double ceiling_db)
{
    double now = loudness_of(buf, n);
    if (!isfinite(now)) return now;              /* silence: nothing to match */
    float g = (float)pow(10, (lufs - now) / 20);
    for (long i = 0; i < 2 * n; i++) buf[i] *= g;
    Mixer *m = calloc(1, sizeof *m);             /* just the limiter's state */
    if (!m) return now;
    m->rate = SR;
    Project *p = calloc(1, sizeof *p);
    if (!p) {
        free(m);
        return now;
    }
    p->fx.limit_on = 1;
    p->fx.limit_ceiling = (float)ceiling_db;
    long look = (long)(0.005 * SR);
    float *all = calloc((size_t)(n + look) * 2, sizeof(float));
    if (all) {
        memcpy(all, buf, (size_t)n * 2 * sizeof(float));
        for (long pos = 0; pos < n + look; pos += 4096)
            master_dynamics(m, p, all + 2 * pos, n + look - pos < 4096 ? (int)(n + look - pos) : 4096);
        memcpy(buf, all + 2 * look, (size_t)n * 2 * sizeof(float));
        free(all);
    }
    free(p);
    free(m);
    return now;
}
