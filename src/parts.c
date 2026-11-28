/* tinydaw: parts written from the chord track. No UI, no audio: given the song's chords and key,
 * these write the notes of a chord part, a bassline or a melody, and turn a bar of a drum pattern
 * into a fill. The same seed always writes the same part, so "try another" is just the next seed. */
#include "daw.h"
#include "theory.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const PART_STYLES[PART_KINDS][PART_MAX_STYLES] = {
    {"Held", "Pulse", "Stabs", "Arp up", "Arp up & down"},
    {"Roots", "Pulse", "Octaves", "Off-beat", "Walking", "Groove"},
    {"Hook", "Flowing", "Sparse"},
};
const int PART_NSTYLES[PART_KINDS] = {5, 6, 3};
const char *const FILL_STYLES[FILL_KINDS] = {"Snare roll", "Build", "Toms", "Drop-out"};

static unsigned rnd(unsigned *s)
{
    *s = *s * 1664525u + 1013904223u;
    return *s >> 8;
}
static double frand(unsigned *s) { return (rnd(s) & 0xffff) / 65536.0; }

/* ---- the song's chords over a stretch, and the bars they fall in */

typedef struct { const SongChord *c; double a, b; } Span;

static int spans(const Project *p, double from, double to, Span *out, int max)
{
    int n = 0;
    for (int i = 0; i < p->nchords && n < max; i++) {
        const SongChord *c = &p->chords[i];
        double a = c->at > from ? c->at : from, b = c->at + c->len < to ? c->at + c->len : to;
        if (b - a > 1e-6) out[n++] = (Span){c, a, b};
    }
    return n;
}

/* Beats come back from frames a hair off (19.99999 for 20): put them back on a fine grid. */
static double tidy(double beat) { return floor(beat * 960 + 0.5) / 960; }

static double bar_start_beat(const Project *p, double beat)
{
    double b = floor(meter_bars(p, meter_beats_frame(p, beat + 1e-4)) + 1e-4);
    return tidy(meter_beats(p, meter_bar_frame(p, b)));
}

static double beat_frac(double x) { return x - floor(x + 1e-6); }   /* how far past a whole beat */

static double bar_len_beats(const Project *p, double beat)
{
    int num, den;
    meter_at(p, meter_beats_frame(p, beat + 1e-6), NULL, &num, &den, NULL, NULL, NULL);
    return num * 4.0 / den;
}

static int emit(PartNote *out, int n, int max, int note, double at, double len, double vel)
{
    if (n >= max || len < 0.05 || note < 0 || note > 127) return n;
    out[n++] = (PartNote){note, at, len, (float)(vel < 0.05 ? 0.05 : vel > 1 ? 1 : vel)};
    return n;
}

/* the notes of the key at a beat, as pitch classes */
static int key_pcs(const Project *p, double beat, int *pcs)
{
    int kr, ks;
    key_at_beat(p, beat, &kr, &ks);
    const Scale *sc = &SCALES[ks];
    for (int k = 0; k < sc->n; k++) pcs[k] = (kr + sc->steps[k]) % 12;
    return sc->n;
}

static int has_pc(const int *pcs, int n, int pc)
{
    for (int i = 0; i < n; i++)
        if (pcs[i] == ((pc % 12) + 12) % 12) return 1;
    return 0;
}

static int chord_pcs(const SongChord *c, int *pcs)
{
    const ChordType *t = &CHORDS[c->type];
    int n = 0;
    for (int i = 0; i < t->n; i++) {
        int pc = (c->root + t->iv[i]) % 12;
        if (!has_pc(pcs, n, pc)) pcs[n++] = pc;
    }
    return n;
}

/* the next note of the key above (dir 1) or below (-1); a note outside the key goes to the nearest one in it */
static int scale_step(const Project *p, double beat, int note, int dir)
{
    int pcs[8], n = key_pcs(p, beat, pcs);
    int v = note + dir;
    for (int k = 0; k < 12 && !has_pc(pcs, n, v); k++) v += dir;
    return v;
}

/* the chord tone nearest a note (ties: the one in the direction `dir`) */
static int nearest_tone(const SongChord *c, int note, int dir)
{
    int pcs[8], n = chord_pcs(c, pcs), best = note, bd = 99;
    for (int d = 0; d <= 6; d++)
        for (int s = 0; s < 2; s++) {
            int v = note + (s == 0 ? d * (dir >= 0 ? 1 : -1) : -d * (dir >= 0 ? 1 : -1));
            if (has_pc(pcs, n, v) && d < bd) {
                bd = d;
                best = v;
            }
        }
    return best;
}

static int near_prev(int note, int prev, int lo, int hi)   /* the octave of `note` in range nearest prev */
{
    int best = -1;
    for (int v = note % 12; v <= 127; v += 12)
        if (v >= lo && v <= hi && (best < 0 || abs(v - prev) < abs(best - prev))) best = v;
    return best >= 0 ? best : note;
}

static int fold(int note, int lo, int hi)        /* into range by octaves */
{
    while (note < lo) note += 12;
    while (note > hi) note -= 12;
    return note < lo ? lo : note;
}

/* ---- chords */

static int write_chords(const Project *p, const PartSpec *s, PartNote *out, int max)
{
    Span sp[MAX_CHORDS];
    int ns = spans(p, s->from, s->to, sp, MAX_CHORDS), n = 0, prev[8], np = 0;
    double busy = s->density;
    for (int i = 0; i < ns; i++) {
        int v[8], nv = chord_voice(sp[i].c, prev, np, s->lo, s->hi, v);
        memcpy(prev, v, sizeof(int) * (size_t)nv);
        np = nv;
        double a = sp[i].a, b = sp[i].b;
        switch (s->style) {
        case 0:                                  /* held, a breath before the next */
            for (int k = 0; k < nv; k++) n = emit(out, n, max, v[k], a, (b - a) - fmin(0.1, (b - a) / 8), 0.7);
            break;
        case 1: {                                /* pulse: on the beat, or the 8ths when busier */
            double step = busy < 0.45 ? 1 : 0.5;
            for (double t = ceil((a - 1e-9) / step) * step; t < b - 1e-6; t += step) {
                double inbar = t - bar_start_beat(p, t), vel = inbar < 1e-6 ? 0.85 : beat_frac(inbar) < 1e-6 ? 0.65 : 0.45;
                for (int k = 0; k < nv; k++) n = emit(out, n, max, v[k], t, fmin(step * 0.8, b - t), vel);
            }
            break;
        }
        case 2: {                                /* stabs: a syncopated pattern each bar */
            static const double PAT[3][6] = {{0, 2.5, -1}, {0, 1.5, 2.5, -1}, {0, 0.75, 1.5, 2.5, 3.5, -1}};
            const double *pat = PAT[busy < 0.34 ? 0 : busy < 0.67 ? 1 : 2];
            for (double bs = bar_start_beat(p, a); bs < b - 1e-6; bs = tidy(bs + bar_len_beats(p, bs))) {
                double bl = bar_len_beats(p, bs);
                for (int k = 0; pat[k] >= 0; k++) {
                    double t = bs + pat[k];
                    if (pat[k] >= bl || t < a - 1e-6 || t >= b - 1e-6) continue;
                    for (int j = 0; j < nv; j++) n = emit(out, n, max, v[j], t, fmin(0.35, b - t), k == 0 ? 0.85 : 0.6);
                }
            }
            break;
        }
        default: {                               /* arpeggios: up, or up and down */
            int tones[10], nt = 0;
            for (int k = 0; k < nv; k++) tones[nt++] = v[k];
            tones[nt++] = v[0] + 12;
            int order[20], no = 0;
            for (int k = 0; k < nt; k++) order[no++] = tones[k];
            if (s->style == 4)
                for (int k = nt - 2; k > 0; k--) order[no++] = tones[k];
            double step = busy < 0.5 ? 0.5 : 0.25;
            int k = 0;
            for (double t = a; t < b - 1e-6; t += step, k++) {
                double inbar = t - bar_start_beat(p, t);
                n = emit(out, n, max, order[k % no], t, fmin(step * 0.9, b - t), beat_frac(inbar) < 1e-6 ? 0.8 : 0.55);
            }
        }
        }
    }
    return n;
}

/* ---- bass */

static int write_bass(const Project *p, const PartSpec *s, PartNote *out, int max)
{
    Span sp[MAX_CHORDS];
    int ns = spans(p, s->from, s->to, sp, MAX_CHORDS), n = 0, last = -1;
    unsigned seed = s->seed * 2654435761u + 17;
    static const char *const GROOVES[4] = {"x..x..x...x.x...", "x.....x.x..x..x.", "x..x.x..x..x..x.", "x...x..xx...x.x."};
    const char *groove = GROOVES[rnd(&seed) % 4];
    for (int i = 0; i < ns; i++) {
        const SongChord *c = sp[i].c;
        double a = sp[i].a, b = sp[i].b;
        int pc = c->bass >= 0 ? c->bass : c->root, base = s->lo + 5;
        int root = base + ((pc - base) % 12 + 12) % 12;    /* low in the range, with room for an octave above */
        if (last >= 0) root = near_prev(root, last, s->lo, s->hi - 12 >= s->lo ? s->hi - 12 : s->hi);   /* stay close */
        last = root;
        int fifth = root + 7 <= s->hi ? root + 7 : root - 5;
        int oct = root + 12 <= s->hi ? root + 12 : root - 12 >= s->lo ? root - 12 : root;
        int next_root = -1;
        if (i + 1 < ns) {
            const SongChord *nc = sp[i + 1].c;
            int npc = nc->bass >= 0 ? nc->bass : nc->root;
            next_root = near_prev(npc, root, s->lo, s->hi);
        }
        switch (s->style) {
        case 0:                                  /* roots: on the chord, and again each new bar */
            for (double t = a; t < b - 1e-6;) {
                double nb = tidy(bar_start_beat(p, t) + bar_len_beats(p, t)), e = nb < b ? nb : b;
                n = emit(out, n, max, root, t, (e - t) * 0.92, t == a ? 0.9 : 0.75);
                t = e;
            }
            break;
        case 1: case 2: case 3: {                /* pulse, octaves, off-beat: 8ths */
            for (double t = ceil((a - 1e-6) * 2) / 2; t < b - 1e-6; t += 0.5) {
                double inbar = t - bar_start_beat(p, t), fr = beat_frac(inbar);
                int off = fr > 0.25 && fr < 0.75;
                if (s->style == 3 && !off) continue;
                if (s->style == 1 && s->density < 0.4 && off) continue;
                int note = s->style == 2 && off ? oct : root;
                double vel = inbar < 1e-6 ? 0.95 : off ? (s->style == 3 ? 0.85 : 0.6) : 0.78;
                n = emit(out, n, max, note, t, fmin(0.42, b - t), vel);
            }
            break;
        }
        case 4: {                                /* walking: quarters, leading into the next chord */
            int pcs[8], npc = chord_pcs(c, pcs), cur = root;
            for (double t = ceil(a - 1e-9); t < b - 1e-6; t += 1) {
                int note;
                if (t - a < 1e-6) note = root;
                else if (t + 1 >= b - 1e-6 && next_root >= 0) {   /* the last beat: a step from the next root */
                    int from_above = (rnd(&seed) & 1) && next_root + 1 <= s->hi;
                    note = from_above ? next_root + 1 : next_root - 1;
                    if (note < s->lo) note = next_root + 1;
                    if (rnd(&seed) % 3 == 0) note = scale_step(p, t, next_root, from_above ? 1 : -1);
                } else {
                    int pick = pcs[rnd(&seed) % (unsigned)npc];
                    note = nearest_tone(c, cur + (rnd(&seed) % 2 ? 3 : -3), 1);
                    if (rnd(&seed) % 3 == 0) note = fold(s->lo + ((pick - s->lo) % 12 + 12) % 12, s->lo, s->hi);
                }
                note = fold(note, s->lo, s->hi);
                if (note == cur && t - a > 1e-6 && !(t + 1 >= b - 1e-6)) {   /* walking: never on the spot */
                    int alt = nearest_tone(c, cur + 4, 1);
                    note = alt <= s->hi ? alt : nearest_tone(c, cur - 4, -1);
                }
                cur = note;
                double ib = t - bar_start_beat(p, t);
                int accent = beat_frac(ib) < 1e-6 && (int)floor(ib + 1e-6) % 2 == 0;   /* beats 1 and 3 */
                n = emit(out, n, max, note, t, 0.9, accent ? 0.85 : 0.7);
            }
            break;
        }
        default: {                               /* groove: a syncopated 16th pattern, root, fifth and octave */
            int k = 0;
            for (double bs = bar_start_beat(p, a); bs < b - 1e-6; bs = tidy(bs + bar_len_beats(p, bs))) {
                double bl = bar_len_beats(p, bs);
                for (int st = 0; st < 16 && st * 0.25 < bl; st++) {
                    double t = bs + st * 0.25;
                    if (groove[st] != 'x' || t < a - 1e-6 || t >= b - 1e-6) continue;
                    if (s->density < 0.35 && st % 4) continue;
                    int note = k % 4 == 3 ? oct : k % 4 == 2 && s->density > 0.5 ? fifth : root;
                    double len = 0.22;
                    for (int u = st + 1; u < 16 && groove[u] != 'x'; u++) len += 0.25;
                    n = emit(out, n, max, note, t, fmin(len * 0.85, b - t), st == 0 ? 0.95 : st % 4 ? 0.7 : 0.82);
                    k++;
                }
            }
        }
        }
    }
    return n;
}

/* ---- melody
 *
 * Built from cells a bar (Hook) or two bars long. A phrase is four cells: A A B A, where cells with
 * the same letter share their rhythm and their melodic choices, so the shape comes back but lands on
 * whatever chord is under it. Strong beats land on chord tones, the rest move by step through the
 * key, and each phrase ends on the chord's root or third. */

typedef struct { double t, d; } Hit;

static int cell_rhythm(int style, double busy, double len, unsigned *seed, Hit *h, int max)
{
    double grid = style == 0 && busy > 0.6 ? 0.25 : 0.5;
    double pk = style == 2 ? 0.45 : 1.0;
    int n = 0;
    for (int tries = 0; tries < 8 && n < 2; tries++) {
        n = 0;
        for (double t = 0; t < len - 1e-6 && n < max; t += grid) {
            double w = fmod(t, 1) < 1e-6 ? (fmod(t, 2) < 1e-6 ? 1.0 : 0.8) : fmod(t, 0.5) < 1e-6 ? 0.55 : 0.3;
            double pr = busy * 1.25 * w * pk + (t < 1e-6 ? 0.5 : 0);
            pr = pr < 0.05 ? 0.05 : pr > 0.95 ? 0.95 : pr;
            if (frand(seed) < pr) h[n++] = (Hit){t, 0};
        }
    }
    for (int i = 0; i < n; i++) {
        double next = i + 1 < n ? h[i + 1].t : len;
        h[i].d = next - h[i].t;
        if (style == 2 && h[i].d > 1 && frand(seed) < 0.4) h[i].d *= 0.6;   /* breathe */
        else h[i].d *= 0.9;
    }
    return n;
}

static int write_melody(const Project *p, const PartSpec *s, PartNote *out, int max)
{
    int style = s->style, n = 0, cell_bars = style == 0 ? 1 : 2;
    static const int FORM[4] = {0, 0, 1, 0};
    int center = (s->lo + s->hi) / 2, prev = -1;
    double bs = bar_start_beat(p, s->from);
    if (bs < s->from - 1e-6 && s->from - bs > 0.01) bs = bar_start_beat(p, s->from);
    int cell = 0;
    while (bs < s->to - 1e-6 && n < max) {
        double ce = bs;
        for (int k = 0; k < cell_bars; k++) ce = tidy(ce + bar_len_beats(p, ce));
        double len = ce - bs;
        int letter = FORM[cell % 4], phrase_end = cell % 4 == 3;
        unsigned rs = s->seed * 7919u + (unsigned)letter * 104729u + 1, ps = s->seed * 31337u + (unsigned)letter * 15485863u + 3;
        Hit h[64];
        int nh = cell_rhythm(style, s->density, len, &rs, h, 64);
        int target = center + (int)((rnd(&ps) % 9)) - 4;    /* where this cell heads */
        for (int i = 0; i < nh; i++) {
            double t = tidy(bs + h[i].t);
            if (t < s->from - 1e-6 || t >= s->to - 1e-6) continue;
            int ci = chord_at(p, t + 1e-6);
            if (ci < 0) {
                rnd(&ps), rnd(&ps);
                continue;
            }
            const SongChord *c = &p->chords[ci];
            double inbar = t - bar_start_beat(p, t);
            int strong = (beat_frac(inbar) < 1e-6 && (int)floor(inbar + 1e-6) % 2 == 0) || h[i].d >= 1.5;
            int note;
            unsigned r1 = rnd(&ps), r2 = rnd(&ps);
            if (prev < 0) {
                note = nearest_tone(c, center, 1);
            } else {
                int dir = (r1 % 4 == 0) ? (target > prev ? -1 : 1) : (target >= prev ? 1 : -1);
                int kind = (int)(r2 % 10);       /* step, skip, repeat or leap */
                int stepw = style == 1 ? (kind < 7 ? 1 : kind < 9 ? 2 : 0) : style == 0 ? (kind < 4 ? 1 : kind < 7 ? 0 : 3)
                                                                                 : (kind < 5 ? 1 : kind < 8 ? 2 : 3);
                note = prev;
                if (stepw == 3) note = nearest_tone(c, prev + dir * 5, dir);
                else
                    for (int k = 0; k < stepw; k++) note = scale_step(p, t, note, dir);
                if (strong) note = nearest_tone(c, note, dir);
            }
            if (phrase_end && i == nh - 1) {      /* home: the root or the third */
                const ChordType *ct = &CHORDS[c->type];
                int r = nearest_tone(&(SongChord){0, 0, c->root, CH_MAJ, -1}, note, -1);
                int third = note - ((note - (c->root + ct->iv[1])) % 12 + 12) % 12;
                if (abs(third + 12 - note) < abs(third - note)) third += 12;
                note = abs(third - note) < abs(r - note) && r2 % 2 ? third : r;
                if (note % 12 != c->root % 12 && (note - c->root - ct->iv[1]) % 12 != 0) note = r;
                h[i].d = ce - t - fmin(0.25, (ce - t) * 0.1);
            }
            note = prev >= 0 ? near_prev(note, prev, s->lo, s->hi) : fold(note, s->lo, s->hi);   /* no wild leaps */
            double vel = (strong ? 0.82 : 0.62) + (frand(&ps) - 0.5) * 0.1;
            double d = h[i].d;
            if (t + d > s->to) d = s->to - t;
            n = emit(out, n, max, note, t, d, vel);
            prev = note;
        }
        bs = ce;
        cell++;
    }
    return n;
}

int part_write(const Project *p, const PartSpec *s, PartNote *out, int max)
{
    if (s->to <= s->from || !p->nchords) return 0;
    switch (s->kind) {
    case PART_CHORDS: return write_chords(p, s, out, max);
    case PART_BASS: return write_bass(p, s, out, max);
    case PART_MELODY: return write_melody(p, s, out, max);
    }
    return 0;
}

/* ---- drum fills */

static int row_like(const DrumPat *d, const Project *p, const char *const *words)
{
    for (int w = 0; words[w]; w++)
        for (int r = 0; r < d->nrows; r++) {
            int sm = d->sample[r];
            if (sm < 0 || sm >= p->nsamples) continue;
            char low[96];
            snprintf(low, sizeof low, "%s", p->samples[sm].name);
            for (char *q = low; *q; q++) *q = (char)(*q >= 'A' && *q <= 'Z' ? *q + 32 : *q);
            if (strstr(low, words[w])) return r;
        }
    return -1;
}

void drum_fill(const Project *p, const DrumPat *src, int from_step, int bar_steps, int style, double intensity,
               unsigned seed, DrumPat *out)
{
    *out = *src;
    out->steps = bar_steps > 16 ? 32 : 16;
    for (int r = 0; r < out->nrows; r++)          /* the bar as the pattern plays it there */
        for (int k = 0; k < DRUM_STEPS; k++) {
            int sk = (from_step + k) % src->steps;
            out->vel[r][k] = k < bar_steps ? src->vel[r][sk] : 0;
            out->prob[r][k] = k < bar_steps ? src->prob[r][sk] : 100;
        }
    static const char *const KICK[] = {"kick", "808", NULL}, *const SNARE[] = {"snare", "clap", "rim", "snap", NULL},
                             *const HAT[] = {"hat", "shaker", "ride", NULL}, *const TOM[] = {"tom", "conga", "perc", NULL};
    int kick = row_like(src, p, KICK), snare = row_like(src, p, SNARE), hat = row_like(src, p, HAT), tom = row_like(src, p, TOM);
    if (snare < 0) snare = tom >= 0 ? tom : out->nrows > 1 ? out->nrows - 1 : 0;
    if (snare == kick && out->nrows > 1) snare = kick == 0 ? 1 : 0;
    unsigned sd = seed * 2654435761u + 99;
    int len = intensity < 0.34 ? 4 : intensity < 0.67 ? 8 : 16;    /* a beat, half a bar or the whole bar */
    if (style == 1) len = len < 8 ? 8 : 16;
    if (len > bar_steps) len = bar_steps;
    int s0 = bar_steps - len;
    for (int r = 0; r < out->nrows; r++)          /* in the fill, the hats and percussion step aside */
        if (r != kick && r != snare && r != tom)
            for (int k = s0; k < bar_steps; k++) {
                int keep = style == 1 && r == hat && k < bar_steps - 4;
                if (!keep) out->vel[r][k] = 0;
            }
    for (int k = s0; k < bar_steps; k++) {
        double pos = len > 1 ? (double)(k - s0) / (len - 1) : 1;   /* 0..1 through the fill */
        int vel = (int)(55 + 72 * pos);
        if (kick >= 0) out->vel[kick][k] = 0;
        out->vel[snare][k] = 0;
        if (tom >= 0 && tom != snare) out->vel[tom][k] = 0;
        switch (style) {
        case 0:                                    /* snare roll: 8ths, then 16ths for the last beat */
            if ((k - s0) % 2 == 0 || k >= bar_steps - 4) out->vel[snare][k] = (unsigned char)vel;
            if (kick >= 0 && k == s0) out->vel[kick][k] = 110;
            break;
        case 1:                                    /* build: kick on the beats, snare speeding up */
            if (kick >= 0 && k % 4 == 0) out->vel[kick][k] = (unsigned char)(90 + 30 * pos);
            if (k >= bar_steps - 4 || (k >= bar_steps - 8 && k % 2 == 0) || k % 4 == 0) out->vel[snare][k] = (unsigned char)vel;
            break;
        case 2: {                                  /* toms: snare and tom answering, a flam to finish */
            int who = ((k - s0) / 2) % 2;
            int use = who && tom >= 0 && tom != snare ? tom : snare;
            if ((k - s0) % 2 == 0 || pos > 0.6) out->vel[use][k] = (unsigned char)(vel - (rnd(&sd) % 12));
            if (kick >= 0 && (k - s0) % 4 == 2 && pos < 0.8) out->vel[kick][k] = 90;
            break;
        }
        default:                                   /* drop-out: silence, then one hit to land on */
            if (k == bar_steps - 1) out->vel[snare][k] = 120;
            if (k == bar_steps - 2 && intensity > 0.5) out->vel[snare][k] = 80;
            break;
        }
        for (int r = 0; r < out->nrows; r++) out->prob[r][k] = 100;   /* a fill always plays */
    }
}
