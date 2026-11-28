/* tinydaw: pitch bends drawn over clips. A clip's curve is in seconds from its origin (where its
 * sound or pattern starts on the timeline), so it stays put when the clip is moved, trimmed or split.
 *
 * Playing a bent clip needs to know how far into its sound it is after a stretch of timeline: the
 * "warp", the running integral of the playback-speed ratio 2^(semitones/12). Notes, drum hits and
 * tape-style audio read their sound through it. Audio that keeps its timing is pitch-shifted once,
 * ahead of time, by overlapping grains (WSOLA), and plays that instead.
 *
 * The tables are built on the UI thread (bends_build, outside the audio lock) and swapped in under
 * the lock (bends_install); the audio thread only reads them, after checking they match the curve. */
#include "daw.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define WARP_STEP 32                     /* frames between warp table entries */

struct BendMap {
    int id;
    unsigned hash;
    long n;
    double *w;                           /* w[i]: sound frames after i * WARP_STEP timeline frames */
    double tail;                         /* the ratio after the last point (the bend holds) */
};

typedef struct { int id, sample; unsigned hash; long frames; float *data; } Shifted;

static BendMap maps[MAX_BENDS];
static int nmaps;
static Shifted shifted[64];
static int nshifted;

float bend_at(const Bend *b, double sec)
{
    if (!b || b->n <= 0) return 0;
    if (sec <= b->at[0]) return b->semis[0];
    for (int i = 1; i < b->n; i++)
        if (sec < b->at[i]) {
            double span = b->at[i] - b->at[i - 1];
            double k = span > 1e-9 ? (sec - b->at[i - 1]) / span : 1;
            return (float)(b->semis[i - 1] + (b->semis[i] - b->semis[i - 1]) * k);
        }
    return b->semis[b->n - 1];
}

const Bend *bend_find_c(const Project *p, int id)
{
    if (id <= 0) return NULL;
    for (int i = 0; i < p->nbends; i++)
        if (p->bends[i].id == id) return &p->bends[i];
    return NULL;
}

Bend *bend_find(Project *p, int id) { return (Bend *)bend_find_c(p, id); }

int bend_new(Project *p)
{
    if (p->nbends >= MAX_BENDS) {                /* make room: curves no clip uses any more */
        int w = 0;
        for (int i = 0; i < p->nbends; i++) {
            int used = 0;
            for (int c = 0; c < p->nclips && !used; c++) used = p->clips[c].bend == p->bends[i].id;
            if (used) p->bends[w++] = p->bends[i];
        }
        p->nbends = w;
        if (p->nbends >= MAX_BENDS) return 0;
    }
    int id = 0;
    for (int i = 0; i < p->nbends; i++) id = p->bends[i].id > id ? p->bends[i].id : id;
    Bend *b = &p->bends[p->nbends++];
    memset(b, 0, sizeof *b);
    b->id = id + 1;
    b->keep_time = 1;
    return b->id;
}

static unsigned bend_hash(const Bend *b)
{
    unsigned h = 2166136261u;
    const unsigned char *q = (const unsigned char *)&b->n;
    for (size_t i = 0; i < sizeof b->n; i++) h = (h ^ q[i]) * 16777619u;
    q = (const unsigned char *)b->at;
    for (size_t i = 0; i < sizeof(float) * (size_t)b->n; i++) h = (h ^ q[i]) * 16777619u;
    q = (const unsigned char *)b->semis;
    for (size_t i = 0; i < sizeof(float) * (size_t)b->n; i++) h = (h ^ q[i]) * 16777619u;
    h = (h ^ (unsigned)g_rate) * 16777619u;
    return h ? h : 1;
}

const BendMap *bend_map(const Project *p, int id)
{
    const Bend *b = bend_find_c(p, id);
    if (!b || b->n <= 0) return NULL;
    unsigned h = bend_hash(b);
    for (int i = 0; i < nmaps; i++)
        if (maps[i].id == id) return maps[i].hash == h ? &maps[i] : NULL;
    return NULL;
}

double bend_warp(const BendMap *m, double x)
{
    if (x <= 0) return x * (m->n ? (m->w[1] - m->w[0]) / WARP_STEP : 1);
    double k = x / WARP_STEP;
    long i = (long)k;
    if (i >= m->n - 1) return m->w[m->n - 1] + (x - (double)(m->n - 1) * WARP_STEP) * m->tail;
    return m->w[i] + (m->w[i + 1] - m->w[i]) * (k - i);
}

const float *bend_shifted(const Project *p, int id, int sample)
{
    const Bend *b = bend_find_c(p, id);
    if (!b || b->n <= 0 || sample < 0 || sample >= p->nsamples) return NULL;
    unsigned h = bend_hash(b) ^ (unsigned)p->samples[sample].frames * 2654435761u;
    for (int i = 0; i < nshifted; i++)
        if (shifted[i].id == id && shifted[i].sample == sample)
            return shifted[i].hash == h && shifted[i].frames == p->samples[sample].frames ? shifted[i].data : NULL;
    return NULL;
}

/* ---- building */

static BendMap build_map(const Bend *b)
{
    BendMap m = {b->id, bend_hash(b), 0, NULL, 1};
    double last = b->at[b->n - 1] + 0.05;
    m.n = (long)(last * g_rate / WARP_STEP) + 2;
    if (m.n < 2) m.n = 2;
    m.w = malloc(sizeof(double) * (size_t)m.n);
    if (!m.w) {
        m.n = 0;
        return m;
    }
    m.w[0] = 0;
    double prev = pow(2.0, bend_at(b, 0) / 12.0);
    for (long i = 1; i < m.n; i++) {
        double r = pow(2.0, bend_at(b, (double)i * WARP_STEP / g_rate) / 12.0);
        m.w[i] = m.w[i - 1] + (prev + r) * 0.5 * WARP_STEP;   /* trapezoids */
        prev = r;
    }
    m.tail = pow(2.0, b->semis[b->n - 1] / 12.0);
    return m;
}

static inline double mono_at(const float *m, long n, double pos)
{
    if (pos < 0 || pos >= n - 1) return 0;
    long i = (long)pos;
    return m[i] + (m[i + 1] - m[i]) * (pos - i);
}

/* how well a grain read from sc at ratio r continues what the last grain would have played next */
static double match(const float *m, long n, double sc, double r, double want, double pr, int half)
{
    double v = 0;
    for (int j = -half; j < 0; j += 8) v += mono_at(m, n, sc + j * r) * mono_at(m, n, want + j * pr);
    return v;
}

/* The sound pitch-shifted along the curve, the same length: grains a hop apart, each read at the
 * bend's speed ratio, from where its start best continues the grain before (WSOLA). */
static float *shift_sound(const Sample *s, const Bend *b, int sample_rate)
{
    long n = s->frames;
    float *out = calloc((size_t)n * 2 + 2, sizeof(float));
    if (!out || !s->data || n < 4) return out;
    const float *x = s->data;
    int hop = sample_rate / 94;                  /* about 10 ms */
    hop = hop < 64 ? 64 : hop;
    int tol = hop / 2, half = hop;
    double *win = malloc(sizeof(double) * (size_t)(2 * half));
    float *mono = malloc(sizeof(float) * (size_t)n);
    if (!win || !mono) {
        free(win);
        free(mono);
        return out;
    }
    for (long i = 0; i < n; i++) mono[i] = 0.5f * (x[2 * i] + x[2 * i + 1]);
    for (int j = 0; j < 2 * half; j++) win[j] = 0.5 - 0.5 * cos(2 * M_PI * (j + 0.5) / (2 * half));
    double prev_s = 0, prev_r = 1;
    int first = 1;
    for (long c = 0; c < n + half; c += hop) {
        double r = pow(2.0, bend_at(b, (double)c / sample_rate) / 12.0);
        double best_s = (double)c, want = prev_s + hop * prev_r;
        if (!first && r == 1 && prev_r == 1 && fabs(want - c) < 0.5) {
            best_s = want;                       /* unbent and in step: nothing to search for */
        } else if (!first) {                     /* where the previous grain would have gone on to: */
            double bestv = -1e30;                /* coarsely, then closely around the best */
            int bd = 0;
            for (int d = -tol; d <= tol; d += 6) {
                double v = match(mono, n, (double)c + d, r, want, prev_r, half) - 1e-4 * fabs((double)d);
                if (v > bestv) bestv = v, bd = d;
            }
            int lo = bd - 5, hi = bd + 5;
            for (int d = lo; d <= hi; d++) {
                if (d < -tol || d > tol) continue;
                double v = match(mono, n, (double)c + d, r, want, prev_r, half) - 1e-4 * fabs((double)d);
                if (v > bestv) bestv = v, bd = d;
            }
            best_s = (double)c + bd;
        }
        for (int j = -half; j < half; j++) {
            long o = c + j;
            if (o < 0 || o >= n) continue;
            double pos = best_s + j * r;
            if (pos < 0 || pos >= n - 1) continue;
            long i = (long)pos;
            double f = pos - i, w = win[j + half];
            out[2 * o] += (float)((x[2 * i] * (1 - f) + x[2 * i + 2] * f) * w);
            out[2 * o + 1] += (float)((x[2 * i + 1] * (1 - f) + x[2 * i + 3] * f) * w);
        }
        prev_s = best_s;
        prev_r = r;
        first = 0;
    }
    free(win);
    free(mono);
    return out;
}

struct BendWork {
    BendMap maps[MAX_BENDS];
    int nmaps;
    Shifted shifted[64];
    int nshifted;
};

/* What's out of date, built afresh (NULL when everything is current). Doesn't touch what the audio
 * thread reads, so it runs without the lock. */
BendWork *bends_build(const Project *p)
{
    int stale = 0;
    for (int i = 0; i < p->nbends && !stale; i++) {
        int used = 0;
        for (int c = 0; c < p->nclips && !used; c++) used = p->clips[c].bend == p->bends[i].id;
        if (used && p->bends[i].n > 0 && !bend_map(p, p->bends[i].id)) stale = 1;
    }
    for (int c = 0; c < p->nclips && !stale; c++) {
        const Clip *cl = &p->clips[c];
        const Bend *b = bend_find_c(p, cl->bend);
        if (b && b->n > 0 && b->keep_time && !cl->pattern && !bend_shifted(p, cl->bend, cl->sample)) stale = 1;
    }
    int used_maps = 0;                           /* or tables nothing needs any more */
    for (int i = 0; i < nmaps; i++) {
        const Bend *b = bend_find_c(p, maps[i].id);
        used_maps += b != NULL;
    }
    if (!stale && used_maps == nmaps) return NULL;
    BendWork *w = calloc(1, sizeof *w);
    if (!w) return NULL;
    for (int i = 0; i < p->nbends && w->nmaps < MAX_BENDS; i++) {
        const Bend *b = &p->bends[i];
        int used = 0;
        for (int c = 0; c < p->nclips && !used; c++) used = p->clips[c].bend == b->id;
        if (!used || b->n <= 0) continue;
        const BendMap *have = bend_map(p, b->id);
        if (have) {                               /* current: keep it (copied, freed with the old set) */
            BendMap m = *have;
            m.w = malloc(sizeof(double) * (size_t)m.n);
            if (m.w) memcpy(m.w, have->w, sizeof(double) * (size_t)m.n);
            else m.n = 0;
            w->maps[w->nmaps++] = m;
        } else {
            w->maps[w->nmaps++] = build_map(b);
        }
    }
    for (int c = 0; c < p->nclips && w->nshifted < 64; c++) {
        const Clip *cl = &p->clips[c];
        const Bend *b = bend_find_c(p, cl->bend);
        if (!b || b->n <= 0 || !b->keep_time || cl->pattern || cl->sample < 0 || cl->sample >= p->nsamples) continue;
        int dup = 0;
        for (int k = 0; k < w->nshifted; k++) dup |= w->shifted[k].id == b->id && w->shifted[k].sample == cl->sample;
        if (dup) continue;
        const Sample *s = &p->samples[cl->sample];
        Shifted sh = {b->id, cl->sample, bend_hash(b) ^ (unsigned)s->frames * 2654435761u, s->frames, NULL};
        const float *have = bend_shifted(p, b->id, cl->sample);
        sh.data = malloc(sizeof(float) * 2 * (size_t)(s->frames + 1));
        if (!sh.data) continue;
        if (have) memcpy(sh.data, have, sizeof(float) * 2 * (size_t)s->frames);
        else {
            free(sh.data);
            sh.data = shift_sound(s, b, g_rate);
            if (!sh.data) continue;
        }
        w->shifted[w->nshifted++] = sh;
    }
    return w;
}

void bends_install(BendWork *w)
{
    if (!w) return;
    for (int i = 0; i < nmaps; i++) free(maps[i].w);
    for (int i = 0; i < nshifted; i++) free(shifted[i].data);
    memcpy(maps, w->maps, sizeof(BendMap) * (size_t)w->nmaps);
    nmaps = w->nmaps;
    memcpy(shifted, w->shifted, sizeof(Shifted) * (size_t)w->nshifted);
    nshifted = w->nshifted;
    free(w);
}
