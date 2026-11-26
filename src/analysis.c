/* tinydaw analysis: the spectrum behind the analyzer and the visual show. */
#include "daw.h"

#include <math.h>

#define PI_A 3.14159265358979323846

/* In-place radix-2 complex FFT. */
static void fft(float *re, float *im, int n)
{
    for (int i = 1, j = 0; i < n; i++) {         /* bit-reversed order */
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2 * PI_A / len;
        float wr = (float)cos(ang), wi = (float)sin(ang);
        for (int i = 0; i < n; i += len) {
            float cr = 1, ci = 0;
            for (int k = 0; k < len / 2; k++) {
                int a = i + k, b = a + len / 2;
                float xr = re[b] * cr - im[b] * ci, xi = re[b] * ci + im[b] * cr;
                re[b] = re[a] - xr;
                im[b] = im[a] - xi;
                re[a] += xr;
                im[a] += xi;
                float t = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = t;
            }
        }
    }
}

void spectrum_db(const float *mono, int n, float *db)
{
    static float re[VIS_FFT], im[VIS_FFT];
    if (n > VIS_FFT) n = VIS_FFT;
    for (int i = 0; i < n; i++) {
        float w = 0.5f - 0.5f * (float)cos(2 * PI_A * i / (n - 1));
        re[i] = mono[i] * w;
        im[i] = 0;
    }
    fft(re, im, n);
    float scale = 4.0f / n;                      /* Hann loses half the amplitude; one-sided spectrum */
    for (int k = 0; k < n / 2; k++) {
        float m = sqrtf(re[k] * re[k] + im[k] * im[k]) * scale;
        db[k] = m > 1e-7f ? 20 * log10f(m) : -140;
    }
}
