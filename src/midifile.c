/* tinydaw: Standard MIDI Files (.mid). Reading turns each track (or, in a single-track file, each
 * channel) into a list of notes in beats; writing makes a type-1 file with a tempo track and one
 * track per list of notes. Positions are in beats (quarter notes) on both sides: the caller turns
 * them into frames with the project's own tempo, so imported parts line up with its bars. */
#include "midifile.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PPQ 480

/* ---------------------------------------------------------------- reading */

typedef struct {
    const unsigned char *p, *end;
} Rd;

static int rd_u8(Rd *r) { return r->p < r->end ? *r->p++ : -1; }

static long rd_var(Rd *r)                       /* a variable-length quantity */
{
    long v = 0;
    for (int i = 0; i < 4; i++) {
        int c = rd_u8(r);
        if (c < 0) return -1;
        v = (v << 7) | (c & 0x7f);
        if (!(c & 0x80)) return v;
    }
    return -1;
}

static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

static MfTrack *add_track(MfFile *f, const char *name)
{
    if (f->ntracks >= MF_MAX_TRACKS) return NULL;
    MfTrack *t = &f->tracks[f->ntracks++];
    memset(t, 0, sizeof *t);
    snprintf(t->name, sizeof t->name, "%s", name);
    return t;
}

static int add_note(MfTrack *t, int note, double start, double len, int vel)
{
    if (t->n == t->cap) {
        int cap = t->cap ? t->cap * 2 : 256;
        MfNote *nn = realloc(t->notes, (size_t)cap * sizeof *nn);
        if (!nn) return -1;
        t->notes = nn;
        t->cap = cap;
    }
    t->notes[t->n++] = (MfNote){note, start, len, vel};
    return 0;
}

/* a tempo (bpm > 0) or time signature (num > 0) at `beat`, merged with one already there */
static void meter_note(MfFile *f, double beat, double bpm, int num, int den)
{
    for (int i = 0; i < f->nmeter; i++)
        if (f->meter[i].beat == beat) {
            if (bpm > 0) f->meter[i].bpm = bpm;
            if (num > 0) f->meter[i].num = num, f->meter[i].den = den;
            return;
        }
    if (f->nmeter >= MF_MAX_METER) return;
    int k = f->nmeter++;                        /* kept in order */
    while (k > 0 && f->meter[k - 1].beat > beat) {
        f->meter[k] = f->meter[k - 1];
        k--;
    }
    f->meter[k] = (MfMeter){beat, bpm, num, den};
}

void mf_free(MfFile *f)
{
    for (int i = 0; i < f->ntracks; i++) free(f->tracks[i].notes);
    memset(f, 0, sizeof *f);
}

int mf_read(const char *path, MfFile *f, char *err, size_t errlen)
{
    memset(f, 0, sizeof *f);
    f->bpm = 0;
    f->num = 4;
    f->den = 4;
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        snprintf(err, errlen, "can't open %s", path);
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size < 14 || size > 64L * 1024 * 1024) {
        fclose(fp);
        snprintf(err, errlen, "not a MIDI file");
        return -1;
    }
    unsigned char *buf = malloc((size_t)size);
    if (!buf || fread(buf, 1, (size_t)size, fp) != (size_t)size) {
        free(buf);
        fclose(fp);
        snprintf(err, errlen, "can't read %s", path);
        return -1;
    }
    fclose(fp);
    const unsigned char *p = buf, *end = buf + size;
    if (memcmp(p, "MThd", 4) || be32(p + 4) < 6) {
        free(buf);
        snprintf(err, errlen, "not a MIDI file");
        return -1;
    }
    int format = p[8] << 8 | p[9], ntrk = p[10] << 8 | p[11], div = p[12] << 8 | p[13];
    if (div & 0x8000 || div == 0) {
        free(buf);
        snprintf(err, errlen, "MIDI files timed in SMPTE frames aren't supported");
        return -1;
    }
    p += 8 + be32(p + 4);
    for (int tn = 0; tn < ntrk && p + 8 <= end; tn++) {
        uint32_t len = be32(p + 4);
        if (memcmp(p, "MTrk", 4)) {              /* an unknown chunk: skip it */
            p += 8 + len;
            tn--;
            continue;
        }
        const unsigned char *tp = p + 8, *te = tp + len > end ? end : tp + len;
        p = te;
        Rd r = {tp, te};
        char name[64] = "";
        long tick = 0;
        int status = 0;
        /* a track's notes, by channel: on a single-track file each channel becomes its own part */
        MfTrack *by_ch[16] = {0};
        long on_tick[16][128];
        int on_vel[16][128];
        for (int c = 0; c < 16; c++)
            for (int k = 0; k < 128; k++) on_tick[c][k] = -1;
        while (r.p < r.end) {
            long d = rd_var(&r);
            if (d < 0) break;
            tick += d;
            int c = rd_u8(&r);
            if (c < 0) break;
            if (c == 0xFF) {                     /* meta */
                int type = rd_u8(&r);
                long l = rd_var(&r);
                if (type < 0 || l < 0 || r.p + l > r.end) break;
                if (type == 0x51 && l == 3) {
                    long us = r.p[0] << 16 | r.p[1] << 8 | r.p[2];
                    if (us > 0) {
                        if (f->bpm == 0 || tick == 0) f->bpm = 60000000.0 / us;
                        meter_note(f, (double)tick / div, 60000000.0 / us, 0, 0);
                    }
                } else if (type == 0x58 && l >= 2) {
                    if (!f->has_meter || tick == 0) {
                        f->num = r.p[0];
                        f->den = 1 << r.p[1];
                        f->has_meter = 1;
                    }
                    meter_note(f, (double)tick / div, 0, r.p[0], 1 << r.p[1]);
                } else if (type == 0x03 && l > 0) {
                    snprintf(name, sizeof name, "%.*s", (int)(l < 63 ? l : 63), (const char *)r.p);
                }
                r.p += l;
                if (type == 0x2F) break;
                continue;
            }
            if (c == 0xF0 || c == 0xF7) {            /* sysex */
                long l = rd_var(&r);
                if (l < 0 || r.p + l > r.end) break;
                r.p += l;
                continue;
            }
            int d1;
            if (c & 0x80) {
                status = c;
                d1 = rd_u8(&r);
            } else {                                 /* running status */
                d1 = c;
            }
            if (!status) break;
            int hi = status & 0xF0, ch = status & 0x0F, d2 = 0;
            if (hi != 0xC0 && hi != 0xD0) d2 = rd_u8(&r);
            if (d1 < 0 || d2 < 0) break;
            if (hi == 0x90 && d2 > 0) {
                if (on_tick[ch][d1 & 127] >= 0) {   /* struck again while held: end the last one */
                    if (!by_ch[ch]) by_ch[ch] = add_track(f, name);
                    if (by_ch[ch]) add_note(by_ch[ch], d1 & 127, (double)on_tick[ch][d1 & 127] / div,
                                            (double)(tick - on_tick[ch][d1 & 127]) / div, on_vel[ch][d1 & 127]);
                }
                on_tick[ch][d1 & 127] = tick;
                on_vel[ch][d1 & 127] = d2;
            } else if (hi == 0x80 || (hi == 0x90 && d2 == 0)) {
                long st = on_tick[ch][d1 & 127];
                if (st >= 0) {
                    if (!by_ch[ch]) by_ch[ch] = add_track(f, name);
                    if (by_ch[ch]) add_note(by_ch[ch], d1 & 127, (double)st / div, (double)(tick - st) / div,
                                            on_vel[ch][d1 & 127]);
                    on_tick[ch][d1 & 127] = -1;
                }
            }
        }
        for (int ch = 0; ch < 16; ch++) {            /* names: the track's, with the channel when split */
            if (!by_ch[ch]) continue;
            by_ch[ch]->channel = ch;
            if (!by_ch[ch]->name[0] || name[0]) snprintf(by_ch[ch]->name, sizeof by_ch[ch]->name, "%s", name);
            int parts = 0;
            for (int k = 0; k < 16; k++) parts += by_ch[k] != NULL;
            if (!by_ch[ch]->name[0]) snprintf(by_ch[ch]->name, sizeof by_ch[ch]->name, ch == 9 ? "Drums" : "Part %d", f->ntracks);
            else if (parts > 1) {
                char nm[64];
                snprintf(nm, sizeof nm, "%.50s ch%d", by_ch[ch]->name, ch + 1);
                snprintf(by_ch[ch]->name, sizeof by_ch[ch]->name, "%s", nm);
            }
        }
    }
    (void)format;
    free(buf);
    if (!f->ntracks) {
        snprintf(err, errlen, "no notes in it");
        return -1;
    }
    return 0;
}

/* ---------------------------------------------------------------- writing */

typedef struct {
    unsigned char *d;
    size_t n, cap;
} Buf;

static void put(Buf *b, const void *s, size_t n)
{
    if (b->n + n > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 4096;
        while (cap < b->n + n) cap *= 2;
        unsigned char *nd = realloc(b->d, cap);
        if (!nd) return;
        b->d = nd;
        b->cap = cap;
    }
    memcpy(b->d + b->n, s, n);
    b->n += n;
}

static void put_u8(Buf *b, int v)
{
    unsigned char c = (unsigned char)v;
    put(b, &c, 1);
}

static void put_var(Buf *b, unsigned long v)
{
    unsigned char t[5];
    int n = 0;
    t[n++] = v & 0x7f;
    while ((v >>= 7)) t[n++] = 0x80 | (v & 0x7f);
    while (n) put_u8(b, t[--n]);
}

static void put_be32(Buf *b, uint32_t v)
{
    unsigned char t[4] = {v >> 24, v >> 16 & 255, v >> 8 & 255, v & 255};
    put(b, t, 4);
}

typedef struct { long tick; int on, note, vel; } Ev;

static int ev_cmp(const void *a, const void *b)
{
    const Ev *x = a, *y = b;
    if (x->tick != y->tick) return x->tick < y->tick ? -1 : 1;
    return x->on - y->on;                        /* offs before ons at the same moment */
}

static void put_chunk(Buf *file, const Buf *trk)
{
    put(file, "MTrk", 4);
    put_be32(file, (uint32_t)trk->n);
    put(file, trk->d, trk->n);
}

int mf_write(const char *path, const MfFile *f, char *err, size_t errlen)
{
    Buf file = {0}, t = {0};
    put(&file, "MThd", 4);
    put_be32(&file, 6);
    put_u8(&file, 0);
    put_u8(&file, 1);                            /* type 1 */
    put_u8(&file, 0);
    put_u8(&file, f->ntracks + 1);
    put_u8(&file, PPQ >> 8);
    put_u8(&file, PPQ & 255);

    /* the tempo track: tempo changes and time signatures, at their beats */
    long last = 0;
    put_var(&t, 0);
    put_u8(&t, 0xFF);
    put_u8(&t, 0x03);
    put_var(&t, 7);
    put(&t, "tinydaw", 7);
    int nm = f->nmeter > 0 ? f->nmeter : 1;
    for (int i = 0; i < nm; i++) {
        double at = f->nmeter > 0 ? f->meter[i].beat : 0, bpm = f->nmeter > 0 ? f->meter[i].bpm : f->bpm;
        int num = f->nmeter > 0 ? f->meter[i].num : f->num, den = f->nmeter > 0 ? f->meter[i].den : f->den;
        long tick = (long)(at * PPQ + 0.5);
        long us = (long)(60000000.0 / (bpm > 0 ? bpm : 120) + 0.5);
        put_var(&t, (unsigned long)(tick - last));
        last = tick;
        put_u8(&t, 0xFF);
        put_u8(&t, 0x51);
        put_u8(&t, 3);
        put_u8(&t, us >> 16 & 255);
        put_u8(&t, us >> 8 & 255);
        put_u8(&t, us & 255);
        int dpow = 0;
        while ((1 << dpow) < den && dpow < 6) dpow++;
        put_var(&t, 0);
        put_u8(&t, 0xFF);
        put_u8(&t, 0x58);
        put_u8(&t, 4);
        put_u8(&t, num);
        put_u8(&t, dpow);
        put_u8(&t, 24);
        put_u8(&t, 8);
    }
    put_var(&t, 0);
    put_u8(&t, 0xFF);
    put_u8(&t, 0x2F);
    put_u8(&t, 0);
    put_chunk(&file, &t);

    for (int k = 0; k < f->ntracks; k++) {
        const MfTrack *tr = &f->tracks[k];
        t.n = 0;
        size_t nl = strlen(tr->name);
        put_var(&t, 0);
        put_u8(&t, 0xFF);
        put_u8(&t, 0x03);
        put_var(&t, (unsigned long)nl);
        put(&t, tr->name, nl);
        Ev *ev = malloc((size_t)(tr->n * 2 + 1) * sizeof *ev);
        if (!ev) {
            free(t.d);
            free(file.d);
            snprintf(err, errlen, "out of memory");
            return -1;
        }
        int ne = 0;
        for (int i = 0; i < tr->n; i++) {
            const MfNote *n = &tr->notes[i];
            long a = (long)(n->start * PPQ + 0.5), b = (long)((n->start + n->len) * PPQ + 0.5);
            if (b <= a) b = a + 1;
            int vel = n->vel < 1 ? 1 : n->vel > 127 ? 127 : n->vel;
            ev[ne++] = (Ev){a, 1, n->note & 127, vel};
            ev[ne++] = (Ev){b, 0, n->note & 127, 0};
        }
        qsort(ev, (size_t)ne, sizeof *ev, ev_cmp);
        int ch = tr->channel & 15;
        last = 0;
        for (int i = 0; i < ne; i++) {
            put_var(&t, (unsigned long)(ev[i].tick - last));
            last = ev[i].tick;
            put_u8(&t, (ev[i].on ? 0x90 : 0x80) | ch);
            put_u8(&t, ev[i].note);
            put_u8(&t, ev[i].on ? ev[i].vel : 64);
        }
        free(ev);
        put_var(&t, 0);
        put_u8(&t, 0xFF);
        put_u8(&t, 0x2F);
        put_u8(&t, 0);
        put_chunk(&file, &t);
    }
    free(t.d);
    FILE *fp = fopen(path, "wb");
    if (!fp || fwrite(file.d, 1, file.n, fp) != file.n) {
        if (fp) fclose(fp);
        free(file.d);
        snprintf(err, errlen, "can't write %s", path);
        return -1;
    }
    fclose(fp);
    free(file.d);
    return 0;
}
