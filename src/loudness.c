/* tinydaw: loudness the way streaming services measure it (ITU-R BS.1770 / EBU R128).
 *
 * The sound is K-weighted (a gentle high shelf, and a high-pass that ignores rumble), squared and
 * averaged in 100 ms pieces. Momentary loudness is the last 400 ms, short-term the last 3 s, and
 * integrated is the whole run, leaving out silence (below -70 LUFS) and the quiet parts (more than
 * 10 LU below the rest), so a song's breaks don't drag its number down. */
#include "daw.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI_L 3.14159265358979323846

static void shelf(Loud *l, int rate)            /* the K-weighting filters, for any rate */
{
    /* stage 1: high shelf, +4 dB above about 1.7 kHz (RBJ, matched to BS.1770's 48 kHz values) */
    double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
    double K = tan(PI_L * f0 / rate), Vh = pow(10, G / 20), Vb = pow(Vh, 0.4996667741545416);
    double a0 = 1 + K / Q + K * K;
    l->b1[0] = (Vh + Vb * K / Q + K * K) / a0;
    l->b1[1] = 2 * (K * K - Vh) / a0;
    l->b1[2] = (Vh - Vb * K / Q + K * K) / a0;
    l->a1[0] = 2 * (K * K - 1) / a0;
    l->a1[1] = (1 - K / Q + K * K) / a0;
    /* stage 2: high-pass around 38 Hz */
    f0 = 38.13547087602444;
    Q = 0.5003270373238773;
    K = tan(PI_L * f0 / rate);
    double d = 1 + K / Q + K * K;
    l->b2[0] = 1;
    l->b2[1] = -2;
    l->b2[2] = 1;
    l->a2[0] = 2 * (K * K - 1) / d;
    l->a2[1] = (1 - K / Q + K * K) / d;
}

void loud_init(Loud *l, int rate)
{
    free(l->blocks);
    memset(l, 0, sizeof *l);
    l->rate = rate;
    l->piece = rate / 10;
    shelf(l, rate);
}

void loud_reset(Loud *l)
{
    int rate = l->rate;
    loud_init(l, rate > 0 ? rate : 48000);
}

void loud_free(Loud *l)
{
    free(l->blocks);
    l->blocks = NULL;
}

void loud_feed(Loud *l, const float *stereo, long n)
{
    if (l->rate != SR || !l->piece) loud_init(l, SR);
    for (long i = 0; i < n; i++) {
        for (int c = 0; c < 2; c++) {
            double x = stereo[2 * i + c];
            double y = l->b1[0] * x + l->s1[c][0];      /* transposed direct form II, twice */
            l->s1[c][0] = l->b1[1] * x - l->a1[0] * y + l->s1[c][1];
            l->s1[c][1] = l->b1[2] * x - l->a1[1] * y;
            double z = y + l->s2[c][0];
            l->s2[c][0] = -2 * y - l->a2[0] * z + l->s2[c][1];
            l->s2[c][1] = y - l->a2[1] * z;
            l->acc += z * z;
        }
        if (++l->count == l->piece) {           /* a 100 ms piece: its mean square (L + R) */
            double ms = l->acc / l->piece;
            l->acc = 0;
            l->count = 0;
            l->recent[l->rpos] = ms;
            l->rpos = (l->rpos + 1) % 30;
            if (l->nrecent < 30) l->nrecent++;
            if (l->nblocks == l->cap) {
                int cap = l->cap ? l->cap * 2 : 1024;
                double *nb = realloc(l->blocks, (size_t)cap * sizeof *nb);
                if (!nb) continue;
                l->blocks = nb;
                l->cap = cap;
            }
            l->blocks[l->nblocks++] = ms;
        }
    }
}

static double lufs(double ms) { return ms > 0 ? -0.691 + 10 * log10(ms) : -INFINITY; }

static double recent_mean(const Loud *l, int pieces)
{
    if (l->nrecent < pieces) return -INFINITY;
    double s = 0;
    for (int i = 1; i <= pieces; i++) s += l->recent[(l->rpos - i + 30) % 30];
    return lufs(s / pieces);
}

double loud_momentary(const Loud *l) { return recent_mean(l, 4); }
double loud_short(const Loud *l) { return recent_mean(l, 30); }

double loud_integrated(const Loud *l)
{
    int nb = l->nblocks - 3;                    /* 400 ms blocks, a new one every 100 ms */
    if (nb < 1) return -INFINITY;
    double sum = 0;
    int n = 0;
    for (int j = 0; j < nb; j++) {              /* absolute gate: leave out silence */
        double ms = (l->blocks[j] + l->blocks[j + 1] + l->blocks[j + 2] + l->blocks[j + 3]) / 4;
        if (lufs(ms) > -70) {
            sum += ms;
            n++;
        }
    }
    if (!n) return -INFINITY;
    double gate = lufs(sum / n) - 10, s2 = 0;
    int n2 = 0;
    for (int j = 0; j < nb; j++) {              /* relative gate: leave out the quiet parts */
        double ms = (l->blocks[j] + l->blocks[j + 1] + l->blocks[j + 2] + l->blocks[j + 3]) / 4;
        if (lufs(ms) > -70 && lufs(ms) > gate) {
            s2 += ms;
            n2++;
        }
    }
    return n2 ? lufs(s2 / n2) : -INFINITY;
}

double loudness_of(const float *stereo, long n)
{
    Loud l;
    memset(&l, 0, sizeof l);
    loud_init(&l, SR);
    loud_feed(&l, stereo, n);
    double v = loud_integrated(&l);
    loud_free(&l);
    return v;
}
