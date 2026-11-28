/* tinydaw: the chord track and the song's key. No UI, no audio: the timeline draws and edits
 * these, and whatever writes a part from the chords (a pad, a bassline, a melody) reads them. */
#include "daw.h"
#include "theory.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void key_at_bar(const Project *p, int bar, int *root, int *scale)
{
    int r = p->key_root, s = p->key_scale;
    for (int i = 0; i < p->nkeys && p->keys[i].bar <= bar; i++) {
        r = p->keys[i].root;
        s = p->keys[i].scale;
    }
    *root = r;
    *scale = s;
}

void key_at_beat(const Project *p, double beat, int *root, int *scale)
{
    int bar = (int)floor(meter_bars(p, meter_beats_frame(p, beat)) + 1e-4);   /* frames round: a little slack */
    key_at_bar(p, bar < 0 ? 0 : bar, root, scale);
}

void keys_tidy(Project *p)
{
    for (int i = 1; i < p->nkeys; i++)
        for (int j = i; j > 0 && p->keys[j - 1].bar > p->keys[j].bar; j--) {
            KeyChange t = p->keys[j];
            p->keys[j] = p->keys[j - 1];
            p->keys[j - 1] = t;
        }
    int w = 0;
    for (int i = 0; i < p->nkeys; i++) {
        if (p->keys[i].bar < 1) continue;
        if (w && p->keys[w - 1].bar == p->keys[i].bar) w--;   /* one a bar: the later one wins */
        p->keys[w++] = p->keys[i];
    }
    p->nkeys = w;
}

void key_set(Project *p, int bar, int root, int scale)
{
    int at = -1;
    for (int i = 0; i < p->nkeys && p->keys[i].bar <= bar; i++) at = i;
    root = ((root % 12) + 12) % 12;
    if (at < 0) {
        p->key_root = root;
        p->key_scale = scale;
    } else {
        p->keys[at].root = root;
        p->keys[at].scale = scale;
    }
}

int key_change_add(Project *p, int bar)
{
    if (bar < 1) return -1;
    for (int i = 0; i < p->nkeys; i++)
        if (p->keys[i].bar == bar) return i;
    if (p->nkeys >= MAX_KEYS) return -1;
    KeyChange k = {bar, 0, 0};
    key_at_bar(p, bar, &k.root, &k.scale);
    p->keys[p->nkeys++] = k;
    keys_tidy(p);
    for (int i = 0; i < p->nkeys; i++)
        if (p->keys[i].bar == bar) return i;
    return -1;
}

void key_change_remove(Project *p, int i)
{
    if (i < 0 || i >= p->nkeys) return;
    memmove(&p->keys[i], &p->keys[i + 1], (size_t)(p->nkeys - i - 1) * sizeof(KeyChange));
    p->nkeys--;
}

int chord_at(const Project *p, double beat)
{
    for (int i = 0; i < p->nchords; i++) {
        const SongChord *c = &p->chords[i];
        if (beat >= c->at - 1e-9 && beat < c->at + c->len - 1e-9) return i;
        if (c->at > beat) break;
    }
    return -1;
}

void chords_sort(Project *p)
{
    for (int i = 1; i < p->nchords; i++)
        for (int j = i; j > 0 && p->chords[j - 1].at > p->chords[j].at; j--) {
            SongChord t = p->chords[j];
            p->chords[j] = p->chords[j - 1];
            p->chords[j - 1] = t;
        }
}

void chord_remove(Project *p, int i)
{
    if (i < 0 || i >= p->nchords) return;
    memmove(&p->chords[i], &p->chords[i + 1], (size_t)(p->nchords - i - 1) * sizeof(SongChord));
    p->nchords--;
}

int chord_put(Project *p, double at, double len, int root, int type, int bass)
{
    if (len < 1e-6 || at < 0 || type < 0 || type >= CH_COUNT) return -1;
    double end = at + len;
    for (int i = p->nchords - 1; i >= 0; i--) {   /* make room */
        SongChord *c = &p->chords[i];
        double ce = c->at + c->len;
        if (ce <= at + 1e-9 || c->at >= end - 1e-9) continue;
        if (c->at >= at - 1e-9 && ce <= end + 1e-9) {          /* covered: gone */
            chord_remove(p, i);
        } else if (c->at < at && ce > end) {                    /* split around the new one */
            SongChord tail = *c;
            c->len = at - c->at;
            tail.at = end;
            tail.len = ce - end;
            if (p->nchords < MAX_CHORDS) p->chords[p->nchords++] = tail;
        } else if (c->at < at) {
            c->len = at - c->at;
        } else {
            c->len = ce - end;
            c->at = end;
        }
    }
    if (p->nchords >= MAX_CHORDS) return -1;
    root = ((root % 12) + 12) % 12;
    if (bass == root) bass = -1;
    p->chords[p->nchords++] = (SongChord){at, len, root, type, bass < 0 ? -1 : bass % 12};
    chords_sort(p);
    for (int i = 0; i < p->nchords; i++)
        if (fabs(p->chords[i].at - at) < 1e-9) return i;
    return -1;
}

void chord_label(const Project *p, const SongChord *c, char *buf, size_t n)
{
    int kr, ks;
    key_at_beat(p, c->at, &kr, &ks);
    int flats = key_uses_flats(kr, ks);
    char name[32];
    chord_name(c->root, c->type, flats, name, sizeof name);
    if (c->bass >= 0 && c->bass != c->root) snprintf(buf, n, "%s/%s", name, pitch_name(c->bass, flats));
    else snprintf(buf, n, "%s", name);
}

void chord_numeral(const Project *p, const SongChord *c, char *buf, size_t n)
{
    static const char *const UP[12] = {"I", "bII", "II", "bIII", "III", "IV", "bV", "V", "bVI", "VI", "bVII", "VII"};
    int kr, ks;
    key_at_beat(p, c->at, &kr, &ks);
    int iv = ((c->root - kr) % 12 + 12) % 12;
    /* inside the key, sharpened degrees read better than flattened ones in a few places (#iv°) */
    const Scale *sc = &SCALES[ks];
    int in = 0;
    for (int k = 0; k < sc->n; k++) in |= sc->steps[k] == iv;
    const char *roman = in && iv == 6 ? "#IV" : UP[iv];
    int minor = 0;
    switch (c->type) {
    case CH_MIN: case CH_DIM: case CH_MIN7: case CH_M7B5: case CH_DIM7: case CH_MMAJ7: case CH_M6: case CH_MIN9: minor = 1; break;
    }
    char r[8];
    snprintf(r, sizeof r, "%s", roman);
    if (minor)
        for (char *q = r; *q; q++)
            if (*q == 'I' || *q == 'V') *q = (char)(*q + ('a' - 'A'));
    const char *suf = "";
    switch (c->type) {
    case CH_DIM: suf = "°"; break;
    case CH_AUG: suf = "+"; break;
    case CH_SUS2: suf = "sus2"; break;
    case CH_SUS4: suf = "sus4"; break;
    case CH_MAJ7: suf = "maj7"; break;
    case CH_DOM7: case CH_MIN7: suf = "7"; break;
    case CH_M7B5: suf = "ø7"; break;
    case CH_DIM7: suf = "°7"; break;
    case CH_MMAJ7: suf = "(maj7)"; break;
    case CH_AUGMAJ7: suf = "+maj7"; break;
    case CH_6: case CH_M6: suf = "6"; break;
    case CH_ADD9: suf = "add9"; break;
    case CH_9: case CH_MIN9: suf = "9"; break;
    case CH_MAJ9: suf = "maj9"; break;
    }
    snprintf(buf, n, "%s%s", r, suf);
}

int chord_voice(const SongChord *c, const int *prev, int nprev, int lo, int hi, int *out)
{
    const ChordType *t = &CHORDS[c->type];
    int pcs[6], np = 0;
    for (int i = 0; i < t->n && np < 6; i++) {
        int pc = (c->root + t->iv[i]) % 12, dup = 0;
        for (int k = 0; k < np; k++) dup |= pcs[k] == pc;
        if (!dup) pcs[np++] = pc;
    }
    double best = 1e18;
    int nbest = 0, cand[6];
    for (int inv = 0; inv < np; inv++)              /* each inversion, at each octave that fits */
        for (int base = lo; base < lo + 12; base++) {
            if (base % 12 != pcs[inv]) continue;
            for (int oct = 0; oct < 3; oct++) {
                int n0 = base + 12 * oct, top = n0;
                cand[0] = n0;
                for (int k = 1; k < np; k++) {
                    int pc = pcs[(inv + k) % np], v = top + 1;
                    while (v % 12 != pc) v++;
                    cand[k] = top = v;
                }
                if (top > hi) continue;
                double cost = 0;
                if (nprev > 0) {                    /* how far each voice moves, both ways round */
                    for (int k = 0; k < np; k++) {
                        int d = 999;
                        for (int j = 0; j < nprev; j++) d = abs(cand[k] - prev[j]) < d ? abs(cand[k] - prev[j]) : d;
                        cost += d;
                    }
                    for (int j = 0; j < nprev; j++) {
                        int d = 999;
                        for (int k = 0; k < np; k++) d = abs(cand[k] - prev[j]) < d ? abs(cand[k] - prev[j]) : d;
                        cost += d;
                    }
                } else {
                    double mean = 0;
                    for (int k = 0; k < np; k++) mean += cand[k];
                    cost = fabs(mean / np - (lo + hi) / 2.0) + 0.5 * inv;   /* root position, all else equal */
                }
                if (cost < best - 1e-9) {
                    best = cost;
                    nbest = np;
                    memcpy(out, cand, sizeof(int) * (size_t)np);
                }
            }
        }
    return nbest;
}

void chord_split(Project *p, double beat)
{
    int n = p->nchords;
    for (int i = 0; i < n && p->nchords < MAX_CHORDS; i++) {
        SongChord *c = &p->chords[i];
        if (c->at >= beat - 1e-9 || c->at + c->len <= beat + 1e-9) continue;
        SongChord tail = *c;
        tail.at = beat;
        tail.len = c->at + c->len - beat;
        c->len = beat - c->at;
        p->chords[p->nchords++] = tail;
    }
    chords_sort(p);
}
