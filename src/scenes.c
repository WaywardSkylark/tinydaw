/* tinydaw's show scenes: sacred geometry and shaders.
 *
 * The geometry scenes draw glowing lines by stamping soft points along them into an RGB
 * accumulator, then blur it twice: a little for the line cores and a lot (at quarter size) for
 * the halo. The cost follows how much line there is, not lines times pixels. The shaders are
 * per-pixel formulas, run on several threads by rows. All of it is ported from the Python
 * programs geoshade and termshade, so the scenes look the same as there, at higher resolution. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O3", "fast-math")     /* lets GCC vectorise the per-pixel maths */
#endif
#include "scenes.h"

#include <SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAU 6.28318530717958647692
#define PHI 1.61803398874989484820
#define GOLDEN_ANGLE (TAU * (1 - 1 / PHI))

typedef struct { float r, g, b; } RGB;

static const char *const NAMES[SCENE_COUNT] = {
    "Flower of Life", "Metatron's Cube", "Sri Yantra", "Platonic solids", "Phyllotaxis", "Star polygons",
    "Torus", "Spirograph", "Plasma", "Tunnel", "Fire", "Metaballs", "Raymarch", "Starfield", "Interference",
    "Voronoi"};
static const char *const CAPTIONS[SCENE_COUNT] = {
    "19 circles on a hexagonal lattice, grown from the Seed of Life",
    "the 13 circles of the Fruit of Life joined by all 78 lines",
    "nine interlocking triangles, two lotus rings and the square of four gates",
    "the five Platonic solids and the merkaba, turning in space",
    "seeds at the golden angle, as in a sunflower, under a golden spiral",
    "{5/2} {7/3} {9/4} {12/5} {16/7} stars turning against each other",
    "36 circles riding a circle",
    "a pen on a circle rolling inside a circle",
    "four sine waves added together",
    "flying down a checkered tunnel",
    "a heat map rising and cooling",
    "six blobs that melt together where their fields meet",
    "sphere tracing: a torus melting into a sphere over a floor, with soft shadows",
    "900 stars rushing past, with trails",
    "three point sources rippling through each other",
    "cells around 14 drifting seeds"};

int scene_count(void) { return SCENE_COUNT; }
const char *scene_name(int i) { return i >= 0 && i < SCENE_COUNT ? NAMES[i] : "?"; }
const char *scene_caption(int i) { return i >= 0 && i < SCENE_COUNT ? CAPTIONS[i] : ""; }

int scene_width(int i, int window_w)
{
    /* sized to stay smooth on a few cores; the raymarcher and Voronoi are the heaviest */
    int cap = i == 12 ? 320 : i == 15 ? 520 : i >= SCENE_SHADER_FIRST ? 640 : 720;
    return window_w < cap ? window_w : cap;
}

/* ---------------------------------------------------------------- small helpers */

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float ease(float v)
{
    v = clampf(v, 0, 1);
    return v * v * (3 - 2 * v);
}
static float smooth(float e0, float e1, float x) { return ease((x - e0) / (e1 - e0)); }

static RGB palette(float t, RGB a, RGB b, RGB c, RGB d)
{
    return (RGB){a.r + b.r * cosf((float)TAU * (c.r * t + d.r)), a.g + b.g * cosf((float)TAU * (c.g * t + d.g)),
                 a.b + b.b * cosf((float)TAU * (c.b * t + d.b))};
}

static RGB hue(float t)
{
    return palette(t, (RGB){0.55f, 0.55f, 0.55f}, (RGB){0.45f, 0.45f, 0.45f}, (RGB){1, 1, 1}, (RGB){0, 0.33f, 0.67f});
}

static uint32_t rng_state = 12345;
static float frand(void)                        /* 0..1 */
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return (rng_state >> 8) / 16777216.0f;
}

/* ---------------------------------------------------------------- running rows on several threads */

typedef void (*RowFn)(void *ctx, int y0, int y1);
typedef struct { RowFn fn; void *ctx; int y0, y1; } Job;

static int job_run(void *p)
{
    Job *j = p;
    j->fn(j->ctx, j->y0, j->y1);
    return 0;
}

static void par_rows(RowFn fn, void *ctx, int h)
{
    int n = SDL_GetCPUCount();
    n = n < 1 ? 1 : n > 8 ? 8 : n;
    if (n == 1 || h < 32) {
        fn(ctx, 0, h);
        return;
    }
    Job jobs[8];
    SDL_Thread *th[8];
    for (int i = 0; i < n; i++) {
        jobs[i] = (Job){fn, ctx, h * i / n, h * (i + 1) / n};
        th[i] = i ? SDL_CreateThread(job_run, "scene", &jobs[i]) : NULL;
        if (i && !th[i]) job_run(&jobs[i]);
    }
    job_run(&jobs[0]);
    for (int i = 1; i < n; i++)
        if (th[i]) SDL_WaitThread(th[i], NULL);
}

/* ---------------------------------------------------------------- the line canvas */

typedef struct {
    float *acc;             /* w * h * 3 */
    int w, h;
    float aspect, zoom;
} Canvas;

typedef struct { Canvas *c; float *out; int w, h, sw, sh; float core, halo; } CompCtx;
static void composite_rows(void *p, int y0, int y1);
static float *canvas_acc, *canvas_tmp, *canvas_small, *canvas_small2;
static size_t canvas_cap, small_cap;

static Canvas canvas_begin(int w, int h, float zoom)
{
    size_t need = (size_t)w * h * 3;
    if (need > canvas_cap) {
        free(canvas_acc);
        free(canvas_tmp);
        canvas_acc = malloc(need * sizeof(float));
        canvas_tmp = malloc(need * sizeof(float));
        canvas_cap = canvas_acc && canvas_tmp ? need : 0;
    }
    if (canvas_acc) memset(canvas_acc, 0, need * sizeof(float));
    return (Canvas){canvas_acc, w, h, (float)w / h, zoom};
}

/* scene coordinates (y up, -1..1 over the height, x scaled by the aspect) to fractional pixels */
static void to_px(const Canvas *c, float x, float y, float *px, float *py)
{
    x *= c->zoom;
    y *= c->zoom;
    *px = (x / c->aspect + 1) * 0.5f * c->w - 0.5f;
    *py = (1 - y) * 0.5f * c->h - 0.5f;
}

static void splat(Canvas *c, float px, float py, RGB col, float amp)
{
    if (!c->acc) return;
    int ix = (int)floorf(px), iy = (int)floorf(py);
    float fx = px - ix, fy = py - iy;
    const float w[4] = {(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy};
    const int ox[4] = {0, 1, 0, 1}, oy[4] = {0, 0, 1, 1};
    for (int k = 0; k < 4; k++) {
        int xx = ix + ox[k], yy = iy + oy[k];
        if (xx < 0 || yy < 0 || xx >= c->w || yy >= c->h) continue;
        float *p = c->acc + ((size_t)yy * c->w + xx) * 3, a = amp * w[k];
        p[0] += col.r * a;
        p[1] += col.g * a;
        p[2] += col.b * a;
    }
}

static void point(Canvas *c, float x, float y, RGB col, float amp)
{
    float px, py;
    to_px(c, x, y, &px, &py);
    splat(c, px, py, col, amp * 2.5f);
}

static void segment(Canvas *c, float ax, float ay, float bx, float by, RGB col, float amp)
{
    float pax, pay, pbx, pby;
    to_px(c, ax, ay, &pax, &pay);
    to_px(c, bx, by, &pbx, &pby);
    float len = hypotf(pbx - pax, pby - pay);
    int steps = (int)ceilf(len / 0.5f);
    if (steps < 1) steps = 1;
    float wgt = len / steps * amp;                 /* brightness per pixel of line, not per sample */
    for (int i = 0; i < steps; i++) {
        float f = (i + 0.5f) / steps;
        splat(c, pax + (pbx - pax) * f, pay + (pby - pay) * f, col, wgt);
    }
}

static void polyline(Canvas *c, const float *xy, int n, RGB col, float amp)
{
    for (int i = 0; i + 1 < n; i++) segment(c, xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3], col, amp);
}

static void ring(Canvas *c, float cx, float cy, float r, RGB col, float amp)
{
    int n = (int)(TAU * r * c->zoom * c->h / 2 * 2);
    if (n < 24) n = 24;
    if (n > 4000) n = 4000;
    float px0 = cx + r, py0 = cy;
    for (int i = 1; i <= n; i++) {
        float a = (float)(TAU * i / n), px1 = cx + r * cosf(a), py1 = cy + r * sinf(a);
        segment(c, px0, py0, px1, py1, col, amp);
        px0 = px1;
        py0 = py1;
    }
}

typedef struct { const float *src; float *dst; int w, h; } BlurCtx;
static const float BK[5] = {0.06f, 0.24f, 0.40f, 0.24f, 0.06f};

static void blur_h_rows(void *p, int y0, int y1)
{
    BlurCtx *c = p;
    for (int y = y0; y < y1; y++)
        for (int x = 0; x < c->w; x++)
            for (int ch = 0; ch < 3; ch++) {
                float s = 0;
                for (int i = -2; i <= 2; i++) {
                    int xx = x + i < 0 ? 0 : x + i >= c->w ? c->w - 1 : x + i;
                    s += c->src[((size_t)y * c->w + xx) * 3 + ch] * BK[i + 2];
                }
                c->dst[((size_t)y * c->w + x) * 3 + ch] = s;
            }
}

static void blur_v_rows(void *p, int y0, int y1)
{
    BlurCtx *c = p;
    for (int y = y0; y < y1; y++)
        for (int x = 0; x < c->w; x++)
            for (int ch = 0; ch < 3; ch++) {
                float s = 0;
                for (int i = -2; i <= 2; i++) {
                    int yy = y + i < 0 ? 0 : y + i >= c->h ? c->h - 1 : y + i;
                    s += c->src[((size_t)yy * c->w + x) * 3 + ch] * BK[i + 2];
                }
                c->dst[((size_t)y * c->w + x) * 3 + ch] = s;
            }
}

static void blur5(const float *src, float *dst, float *tmp, int w, int h)   /* separable 5-tap, RGB */
{
    BlurCtx a = {src, tmp, w, h}, b = {tmp, dst, w, h};
    par_rows(blur_h_rows, &a, h);
    par_rows(blur_v_rows, &b, h);
}

/* the cores (a small blur) plus a wide halo (a quarter-size copy blurred three times, scaled back
 * up smoothly), added on top of out */
static void canvas_result(Canvas *c, float *out, float core, float halo)
{
    if (!c->acc) return;
    int w = c->w, h = c->h, sw = w / 4 > 0 ? w / 4 : 1, sh = h / 4 > 0 ? h / 4 : 1;
    size_t sneed = (size_t)sw * sh * 3;
    if (sneed > small_cap) {
        free(canvas_small);
        free(canvas_small2);
        canvas_small = malloc(sneed * sizeof(float));
        canvas_small2 = malloc(sneed * sizeof(float));
        small_cap = canvas_small && canvas_small2 ? sneed : 0;
    }
    if (!small_cap) return;
    for (int y = 0; y < sh; y++)                   /* quarter size, averaged */
        for (int x = 0; x < sw; x++)
            for (int ch = 0; ch < 3; ch++) {
                float s = 0;
                for (int j = 0; j < 4; j++)
                    for (int i = 0; i < 4; i++) {
                        int xx = x * 4 + i, yy = y * 4 + j;
                        if (xx < w && yy < h) s += c->acc[((size_t)yy * w + xx) * 3 + ch];
                    }
                canvas_small[((size_t)y * sw + x) * 3 + ch] = s / 16;
            }
    for (int k = 0; k < 3; k++) {
        blur5(canvas_small, canvas_small2, canvas_tmp, sw, sh);
        memcpy(canvas_small, canvas_small2, sneed * sizeof(float));
    }
    blur5(c->acc, c->acc, canvas_tmp, w, h);         /* cores, in place */
    CompCtx cc = {c, out, w, h, sw, sh, core, halo};
    par_rows(composite_rows, &cc, h);
}

static void composite_rows(void *vp, int y0, int y1)   /* cores plus the halo scaled back up */
{
    CompCtx *p = vp;
    Canvas *c = p->c;
    float *out = p->out, core = p->core, halo = p->halo;
    int w = p->w, sw = p->sw, sh = p->sh;
    for (int y = y0; y < y1; y++) {
        float fy = (y + 0.5f) / 4 - 0.5f;
        int y0 = (int)floorf(fy);
        float ty = fy - y0;
        int ya = y0 < 0 ? 0 : y0 >= sh ? sh - 1 : y0, yb = y0 + 1 >= sh ? sh - 1 : y0 + 1 < 0 ? 0 : y0 + 1;
        for (int x = 0; x < w; x++) {
            float fx = (x + 0.5f) / 4 - 0.5f;
            int x0 = (int)floorf(fx);
            float tx = fx - x0;
            int xa = x0 < 0 ? 0 : x0 >= sw ? sw - 1 : x0, xb = x0 + 1 >= sw ? sw - 1 : x0 + 1 < 0 ? 0 : x0 + 1;
            for (int ch = 0; ch < 3; ch++) {
                float a = canvas_small[((size_t)ya * sw + xa) * 3 + ch], b = canvas_small[((size_t)ya * sw + xb) * 3 + ch];
                float cc = canvas_small[((size_t)yb * sw + xa) * 3 + ch], d = canvas_small[((size_t)yb * sw + xb) * 3 + ch];
                float wide = (a + (b - a) * tx) * (1 - ty) + (cc + (d - cc) * tx) * ty;
                out[((size_t)y * w + x) * 3 + ch] += c->acc[((size_t)y * w + x) * 3 + ch] * core + wide * halo * 4;
            }
        }
    }
}

/* a dark radial glow behind every geometry scene */
typedef struct { float *out; int w, h; float zoom, k; RGB tint; } BackCtx;
static void backdrop_rows(void *p, int y0, int y1)
{
    BackCtx *c = p;
    float *out = c->out, zoom = c->zoom, k = c->k, aspect = (float)c->w / c->h;
    int w = c->w, h = c->h;
    RGB tint = c->tint;
    for (int y = y0; y < y1; y++)
        for (int x = 0; x < w; x++) {
            float sx = ((2.0f * (x + 0.5f) / w) - 1) * aspect / zoom, sy = (1 - 2.0f * (y + 0.5f) / h) / zoom;
            float g = expf(-(sx * sx + sy * sy) * 0.9f) * k;
            float *p = out + ((size_t)y * w + x) * 3;
            p[0] = tint.r * g;
            p[1] = tint.g * g;
            p[2] = tint.b * g;
        }
}

static void backdrop(float *out, int w, int h, float zoom, double t, RGB tint)
{
    BackCtx c = {out, w, h, zoom, (float)(0.10 + 0.03 * sin(t * 0.7)), tint};
    par_rows(backdrop_rows, &c, h);
}

typedef struct { float *rgb; int w; } FinCtx;
static void finish_rows(void *p, int y0, int y1)
{
    FinCtx *c = p;
    for (size_t i = (size_t)y0 * c->w * 3; i < (size_t)y1 * c->w * 3; i++) c->rgb[i] = 1 - expf(-c->rgb[i] * 1.4f);
}

static void finish(float *rgb, int w, int h)            /* soft tone-mapping */
{
    FinCtx c = {rgb, w};
    par_rows(finish_rows, &c, h);
}

static void rot2(float *x, float *y, float a)
{
    float c = cosf(a), s = sinf(a), nx = *x * c - *y * s;
    *y = *x * s + *y * c;
    *x = nx;
}

/* ---------------------------------------------------------------- geometry scenes */

static int hex_centers(int rings, float d, float *xy)  /* the Seed (1) and Flower (2) of Life */
{
    int n = 0;
    xy[n++] = 0;
    xy[n++] = 0;
    for (int k = 1; k <= rings; k++)
        for (int side = 0; side < 6; side++) {
            float a0 = (float)(side * TAU / 6), a1 = (float)((side + 1) * TAU / 6);
            float p0x = cosf(a0) * k * d, p0y = sinf(a0) * k * d, p1x = cosf(a1) * k * d, p1y = sinf(a1) * k * d;
            for (int j = 0; j < k; j++) {
                xy[n++] = p0x + (p1x - p0x) * j / k;
                xy[n++] = p0y + (p1y - p0y) * j / k;
            }
        }
    return n / 2;
}

static void flower(double t, float *out, Canvas *cv)
{
    float r = 0.25f * (1 + 0.015f * (float)sin(t * 0.8)), xy[19 * 2];
    int n = hex_centers(2, r, xy), order[19];
    for (int i = 0; i < n; i++) {
        rot2(&xy[2 * i], &xy[2 * i + 1], (float)(t * 0.05));
        order[i] = i;
    }
    for (int i = 0; i < n; i++)                    /* inside out, as they appear */
        for (int j = i + 1; j < n; j++) {
            float di = hypotf(xy[2 * order[i]], xy[2 * order[i] + 1]) + 1e-3f * order[i];
            float dj = hypotf(xy[2 * order[j]], xy[2 * order[j] + 1]) + 1e-3f * order[j];
            if (dj < di) {
                int tmp = order[i];
                order[i] = order[j];
                order[j] = tmp;
            }
        }
    for (int rank = 0; rank < n; rank++) {
        float a = ease((float)((t - 0.45 * rank) / 0.9));
        if (a <= 0) continue;
        int rg = rank == 0 ? 0 : rank <= 6 ? 1 : 2, i = order[rank];
        ring(cv, xy[2 * i], xy[2 * i + 1], r, hue(0.62f + 0.09f * rg + 0.02f * (float)sin(t * 0.3 + rank)), a);
    }
    float outer = ease((float)((t - 0.45 * n) / 1.5));
    if (outer > 0) {
        ring(cv, 0, 0, 3 * r, (RGB){1.0f, 0.82f, 0.45f}, outer * 1.2f);
        ring(cv, 0, 0, 3.08f * r, (RGB){0.9f, 0.6f, 0.3f}, outer);
    }
    canvas_result(cv, out, 1.6f, 0.3f);
}

static void metatron(double t, float *out, Canvas *cv)
{
    float d = 0.38f * (1 + 0.02f * (float)sin(t * 0.6)), px[13], py[13];
    px[0] = py[0] = 0;
    for (int k = 1, n = 1; k <= 2; k++)                 /* 13 centres: the Fruit of Life */
        for (int i = 0; i < 6; i++, n++) {
            float a = (float)(TAU * i / 6 + TAU / 12);
            px[n] = cosf(a) * d * k;
            py[n] = sinf(a) * d * k;
        }
    for (int i = 0; i < 13; i++) rot2(&px[i], &py[i], (float)(-t * 0.06));
    for (int i = 0; i < 13; i++) {
        float a = ease((float)((t - 0.18 * i) / 1.2));
        if (a > 0) ring(cv, px[i], py[i], d / 2, (RGB){0.35f, 0.55f, 1.0f}, a * 0.8f);
    }
    float bright = 0.75f + 0.25f * (float)sin(t * 1.3);
    for (int i = 0, k = 0; i < 13; i++)                 /* all 78 lines, growing one after another */
        for (int j = i + 1; j < 13; j++, k++) {
            float g = clampf((float)((t - 3.0 - 0.06 * k) / 0.9), 0, 1);
            if (g > 0) segment(cv, px[i], py[i], px[i] + (px[j] - px[i]) * g, py[i] + (py[j] - py[i]) * g,
                               (RGB){1.0f, 0.78f, 0.35f}, bright);
        }
    point(cv, px[0], py[0], (RGB){1, 0.9f, 0.7f}, ease((float)(t / 3)));
    canvas_result(cv, out, 1.6f, 0.3f);
}

static const float SRI[9][3] = {                       /* base y, apex y, half width */
    {-0.84f, 0.97f, 0.98f}, {-0.60f, 0.70f, 0.86f}, {-0.40f, 0.50f, 0.70f}, {-0.17f, 0.30f, 0.48f},
    {0.79f, -1.0f, 0.92f}, {0.62f, -0.78f, 0.82f}, {0.40f, -0.58f, 0.66f}, {0.20f, -0.36f, 0.48f}, {0.05f, -0.16f, 0.30f}};

static void sri_yantra(double t, float *out, Canvas *cv)
{
    float s = 0.53f * (1 + 0.01f * (float)sin(t * 0.9));
    int order[9];
    for (int i = 0; i < 9; i++) order[i] = i;
    for (int i = 0; i < 9; i++)                         /* smallest triangle first: built from the centre */
        for (int j = i + 1; j < 9; j++)
            if (fabsf(SRI[order[j]][1] - SRI[order[j]][0]) < fabsf(SRI[order[i]][1] - SRI[order[i]][0])) {
                int tmp = order[i];
                order[i] = order[j];
                order[j] = tmp;
            }
    for (int rank = 0; rank < 9; rank++) {
        float a = ease((float)((t - 0.5 * rank) / 1.0));
        if (a <= 0) continue;
        int i = order[rank];
        float b = SRI[i][0] * s, ap = SRI[i][1] * s, hw = SRI[i][2] * s;
        RGB col = i < 4 ? (RGB){1.0f, 0.45f, 0.25f} : (RGB){1.0f, 0.85f, 0.35f};
        segment(cv, -hw, b, hw, b, col, a);
        segment(cv, hw, b, 0, ap, col, a);
        segment(cv, 0, ap, -hw, b, col, a);
    }
    float rings = ease((float)((t - 4.5) / 1.5));
    if (rings > 0) {                                    /* two rings of lotus petals */
        static float xy[721 * 2];
        const float spec[2][3] = {{8, 1.08f, 0.12f}, {16, 1.26f, 0.10f}};
        for (int k = 0; k < 2; k++) {
            for (int i = 0; i <= 720; i++) {
                float th = (float)(TAU * i / 720);
                float rad = (spec[k][1] + spec[k][2] * fabsf(cosf(th * spec[k][0] / 2 + (float)t * 0.05f * (k ? 1 : -1)))) * s;
                xy[2 * i] = rad * cosf(th);
                xy[2 * i + 1] = rad * sinf(th);
            }
            polyline(cv, xy, 721, (RGB){0.9f, 0.3f, 0.45f}, rings);
            ring(cv, 0, 0, spec[k][1] * s, (RGB){0.8f, 0.5f, 0.3f}, rings * 0.6f);
        }
    }
    float gate = ease((float)((t - 6) / 1.5));
    if (gate > 0) {                                     /* the square enclosure with four gates */
        float q = 1.62f * s, g = 0.26f * s;
        float side[12] = {-q, -q, -g, -q, -g, -q - 0.12f * s, g, -q - 0.12f * s, g, -q, q, -q};
        for (int k = 0; k < 4; k++) {
            float r[12];
            memcpy(r, side, sizeof r);
            for (int i = 0; i < 6; i++) rot2(&r[2 * i], &r[2 * i + 1], (float)(k * TAU / 4));
            polyline(cv, r, 6, (RGB){1.0f, 0.7f, 0.3f}, gate);
        }
    }
    point(cv, 0, -0.03f * s, (RGB){1, 0.95f, 0.8f}, 0.6f + 0.4f * (float)sin(t * 2));
    canvas_result(cv, out, 1.6f, 0.3f);
}

/* the Platonic solids: vertices on the unit sphere, edges between nearest neighbours */
static float solid_v[6][20][3];
static int solid_nv[6], solid_e[6][36][2], solid_ne[6], solids_made;
static const char *const SOLID_NAMES[6] = {"tetrahedron", "cube", "octahedron", "dodecahedron", "icosahedron", "merkaba"};

static void make_solids(void)
{
    if (solids_made) return;
    solids_made = 1;
    for (int k = 0; k < 5; k++) {
        float v[20][3];
        int n = 0;
        if (k == 0) {
            const float tv[4][3] = {{1, 1, 1}, {1, -1, -1}, {-1, 1, -1}, {-1, -1, 1}};
            memcpy(v, tv, sizeof tv);
            n = 4;
        } else if (k == 1) {
            for (int a = -1; a <= 1; a += 2)
                for (int b = -1; b <= 1; b += 2)
                    for (int c = -1; c <= 1; c += 2) v[n][0] = a, v[n][1] = b, v[n][2] = c, n++;
        } else if (k == 2) {
            const float ov[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            memcpy(v, ov, sizeof ov);
            n = 6;
        } else if (k == 3) {
            for (int a = -1; a <= 1; a += 2)
                for (int b = -1; b <= 1; b += 2)
                    for (int c = -1; c <= 1; c += 2) v[n][0] = a, v[n][1] = b, v[n][2] = c, n++;
            for (int a = -1; a <= 1; a += 2)
                for (int b = -1; b <= 1; b += 2) {
                    v[n][0] = 0, v[n][1] = a / PHI, v[n][2] = b * PHI, n++;
                    v[n][0] = a / PHI, v[n][1] = b * PHI, v[n][2] = 0, n++;
                    v[n][0] = a * PHI, v[n][1] = 0, v[n][2] = b / PHI, n++;
                }
        } else {
            for (int a = -1; a <= 1; a += 2)
                for (int b = -1; b <= 1; b += 2) {
                    v[n][0] = 0, v[n][1] = a, v[n][2] = b * PHI, n++;
                    v[n][0] = a, v[n][1] = b * PHI, v[n][2] = 0, n++;
                    v[n][0] = a * PHI, v[n][1] = 0, v[n][2] = b, n++;
                }
        }
        float maxr = 0;
        for (int i = 0; i < n; i++) maxr = fmaxf(maxr, sqrtf(v[i][0] * v[i][0] + v[i][1] * v[i][1] + v[i][2] * v[i][2]));
        float emin = 1e9f;
        for (int i = 0; i < n; i++)
            for (int j = 0; j < 3; j++) solid_v[k][i][j] = v[i][j] / maxr;
        for (int i = 0; i < n; i++)
            for (int j = i + 1; j < n; j++) {
                float dx = solid_v[k][i][0] - solid_v[k][j][0], dy = solid_v[k][i][1] - solid_v[k][j][1],
                      dz = solid_v[k][i][2] - solid_v[k][j][2], d = sqrtf(dx * dx + dy * dy + dz * dz);
                if (d > 1e-5f && d < emin) emin = d;
            }
        solid_nv[k] = n;
        for (int i = 0; i < n; i++)
            for (int j = i + 1; j < n; j++) {
                float dx = solid_v[k][i][0] - solid_v[k][j][0], dy = solid_v[k][i][1] - solid_v[k][j][1],
                      dz = solid_v[k][i][2] - solid_v[k][j][2];
                if (fabsf(sqrtf(dx * dx + dy * dy + dz * dz) - emin) < 1e-4f && solid_ne[k] < 36) {
                    solid_e[k][solid_ne[k]][0] = i;
                    solid_e[k][solid_ne[k]++][1] = j;
                }
            }
    }
    /* the merkaba: a tetrahedron and its mirror image */
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 3; j++) {
            solid_v[5][i][j] = solid_v[0][i][j];
            solid_v[5][i + 4][j] = -solid_v[0][i][j];
        }
    solid_nv[5] = 8;
    for (int i = 0; i < 4; i++)
        for (int j = i + 1; j < 4; j++) {
            solid_e[5][solid_ne[5]][0] = i, solid_e[5][solid_ne[5]++][1] = j;
            solid_e[5][solid_ne[5]][0] = i + 4, solid_e[5][solid_ne[5]++][1] = j + 4;
        }
}

static void platonic(double t, float *out, Canvas *cv, char *caption, size_t cn)
{
    make_solids();
    double each = 5.0;
    int k = (int)(t / each) % 6;
    double local = t - floor(t / each) * each;
    float fade = (float)fmin(1.0, fmin(local / 0.8, (each - local) / 0.8));
    float ay = (float)(t * 0.7), ax = (float)(t * 0.43), sc = 0.62f * (1 + 0.04f * (float)sin(t * 1.7));
    float q[20][2], z[20];
    for (int i = 0; i < solid_nv[k]; i++) {
        float x = solid_v[k][i][0], y = solid_v[k][i][1], zz = solid_v[k][i][2];
        float x1 = x * cosf(ay) + zz * sinf(ay), z1 = -x * sinf(ay) + zz * cosf(ay);   /* turn about y */
        float y2 = y * cosf(ax) - z1 * sinf(ax), z2 = y * sinf(ax) + z1 * cosf(ax);    /* then about x */
        x1 *= sc, y2 *= sc, z2 *= sc;
        float persp = 2.6f / (2.6f + z2);
        q[i][0] = x1 * persp;
        q[i][1] = y2 * persp;
        z[i] = z2;
    }
    RGB col = hue(k / 6.0f + 0.1f * (float)sin(t * 0.2));
    for (int e = 0; e < solid_ne[k]; e++) {
        int a = solid_e[k][e][0], b = solid_e[k][e][1];
        float near = clampf(0.55f + 0.45f * (-(z[a] + z[b]) / 2 / 0.6f), 0.35f, 1.0f);   /* near edges brighter */
        segment(cv, q[a][0], q[a][1], q[b][0], q[b][1], col, near * fade);
    }
    for (int i = 0; i < solid_nv[k]; i++) point(cv, q[i][0], q[i][1], (RGB){1, 1, 1}, 0.5f * fade);
    snprintf(caption, cn, "%s", SOLID_NAMES[k]);
    canvas_result(cv, out, 1.6f, 0.3f);
}

typedef struct { float *out; int w, h; float aspect, zoom, amount, t; } SpiralCtx;

static void spiral_rows(void *p, int y0, int y1)      /* the golden spiral, as a distance field */
{
    SpiralCtx *c = p;
    float px = 2.0f / c->h / c->zoom, w = px * 1.4f, b = logf((float)PHI) / (float)(TAU / 4);
    float norm = b / sqrtf(1 + b * b);
    for (int y = y0; y < y1; y++)
        for (int x = 0; x < c->w; x++) {
            float sx = ((2.0f * (x + 0.5f) / c->w) - 1) * c->aspect / c->zoom, sy = (1 - 2.0f * (y + 0.5f) / c->h) / c->zoom;
            float r = hypotf(sx, sy) + 1e-6f;
            if (r >= 1.0f) continue;
            float th = atan2f(sy, sx) - 0.15f * c->t;
            float phase = fmodf(logf(r / 0.02f) / b - th, (float)TAU);
            if (phase < 0) phase += (float)TAU;
            float d = r * fminf(phase, (float)TAU - phase) * norm;
            float g = (expf(-(d / w) * (d / w)) + 0.2f * expf(-d / (w * 7))) * c->amount;
            float *o = c->out + ((size_t)y * c->w + x) * 3;
            o[0] += g * 1.0f;
            o[1] += g * 0.85f;
            o[2] += g * 0.4f;
        }
}

static void phyllotaxis(double t, float *out, Canvas *cv, int w, int h, float zoom)
{
    int n = (int)fmin(420, 8 + t * 40);
    for (int k = 0; k < n; k++) {                       /* seeds at the golden angle */
        float rad = 0.045f * sqrtf((float)k), ang = (float)(k * GOLDEN_ANGLE + t * 0.08);
        RGB col = palette(rad + (float)t * 0.02f, (RGB){0.55f, 0.6f, 0.3f}, (RGB){0.45f, 0.35f, 0.25f}, (RGB){1, 1, 1},
                          (RGB){0.05f, 0.15f, 0.4f});
        point(cv, rad * cosf(ang), rad * sinf(ang), col, 0.9f);
    }
    canvas_result(cv, out, 2.6f, 0.5f);
    SpiralCtx sc = {out, w, h, (float)w / h, zoom, ease((float)((t - 2) / 3)) * 1.4f, (float)t};
    par_rows(spiral_rows, &sc, h);
}

static void star_polygons(double t, float *out, Canvas *cv)
{
    const float stars[5][3] = {{5, 2, 0.22f}, {7, 3, 0.38f}, {9, 4, 0.54f}, {12, 5, 0.70f}, {16, 7, 0.86f}};
    for (int i = 0; i < 5; i++) {                       /* {n/k} stars turning alternately */
        float a = ease((float)((t - 0.7 * i) / 1.2));
        if (a <= 0) continue;
        int n = (int)stars[i][0], kk = (int)stars[i][1];
        float r = stars[i][2], ang = (float)((i % 2 ? 1 : -1) * t * (0.12 + 0.03 * i) + TAU / 4);
        RGB col = hue(0.8f + i * 0.13f + 0.05f * (float)sin(t * 0.4));
        for (int j = 0; j < n; j++) {
            int m = (j + kk) % n;
            segment(cv, r * cosf(ang + (float)(TAU * j / n)), r * sinf(ang + (float)(TAU * j / n)),
                    r * cosf(ang + (float)(TAU * m / n)), r * sinf(ang + (float)(TAU * m / n)), col, a);
        }
        ring(cv, 0, 0, r, col, 0.35f * a);
    }
    point(cv, 0, 0, (RGB){1, 0.9f, 1}, 0.5f + 0.5f * (float)sin(t * 2));
    canvas_result(cv, out, 1.6f, 0.3f);
}

static void torus(double t, float *out, Canvas *cv)
{
    float rr = 0.42f * (1 + 0.04f * (float)sin(t * 0.5));
    for (int i = 0; i < 36; i++) {                      /* circles whose centres ride a circle */
        float on = ease((float)((t - 0.12 * i) / 0.8));
        if (on <= 0) continue;
        float a = (float)(TAU * i / 36 + t * 0.1);
        ring(cv, cosf(a) * rr, sinf(a) * rr, rr, hue(i / 36.0f + (float)t * 0.02f), 0.55f * on);
    }
    canvas_result(cv, out, 2.0f, 0.35f);
}

static int gcd(int a, int b) { return b ? gcd(b, a % b) : a; }

static void spirograph(double t, float *out, Canvas *cv, char *caption, size_t cn)
{
    const float sets[5][3] = {{5, 3, 2.2f}, {7, 4, 1.4f}, {11, 6, 2.6f}, {9, 2, 1.9f}, {13, 5, 3.0f}};
    double each = 9.0;
    int k = (int)(t / each) % 5;
    double local = t - floor(t / each) * each;
    int big = (int)sets[k][0], small = (int)sets[k][1];
    float pen = sets[k][2], R = 1, rr = (float)small / big, loops = (float)small / gcd(big, small);
    enum { N = 1400 };
    static float xy[N * 2];
    float maxabs = 0;
    for (int i = 0; i < N; i++) {                       /* a hypotrochoid */
        float th = (float)(TAU * loops * i / (N - 1));
        xy[2 * i] = (R - rr) * cosf(th) + pen * rr * cosf((R - rr) / rr * th);
        xy[2 * i + 1] = (R - rr) * sinf(th) - pen * rr * sinf((R - rr) / rr * th);
        maxabs = fmaxf(maxabs, fmaxf(fabsf(xy[2 * i]), fabsf(xy[2 * i + 1])));
    }
    for (int i = 0; i < N; i++) {
        xy[2 * i] = xy[2 * i] / maxabs * 0.9f;
        xy[2 * i + 1] = xy[2 * i + 1] / maxabs * 0.9f;
        rot2(&xy[2 * i], &xy[2 * i + 1], (float)(t * 0.07));
    }
    int upto = (int)(N * ease((float)(local / (each * 0.6))));
    if (upto < 2) upto = 2;
    float fade = (float)fmin(1.0, (each - local) / 0.8);
    polyline(cv, xy, upto, hue(k / 5.0f + 0.15f), fade);
    if (local < each * 0.6) point(cv, xy[2 * (upto - 1)], xy[2 * (upto - 1) + 1], (RGB){1, 1, 1}, fade * 1.5f);
    snprintf(caption, cn, "hypotrochoid %d:%d", big, small);
    canvas_result(cv, out, 1.6f, 0.3f);
}

/* ---------------------------------------------------------------- shader scenes */

typedef struct { int id; float *out; int w, h; float aspect, zoom, t; } PixCtx;

static RGB pix_plasma(float x, float y, float t)
{
    float v = sinf(x * 3.1f + t) + sinf(y * 2.3f - t * 1.3f) + sinf((x + y) * 2.2f + t * 0.7f) +
              sinf(hypotf(x + 0.7f * sinf(t * 0.4f), y + 0.6f * cosf(t * 0.3f)) * 5 - t * 1.7f);
    return palette(v * 0.25f + t * 0.05f, (RGB){0.5f, 0.5f, 0.5f}, (RGB){0.5f, 0.5f, 0.5f}, (RGB){1, 1, 1},
                    (RGB){0, 0.33f, 0.67f});
}

static RGB pix_tunnel(float x, float y, float t)
{
    x += 0.25f * sinf(t * 0.6f);
    y += 0.20f * cosf(t * 0.5f);
    float r = hypotf(x, y) + 1e-3f, depth = 0.6f / r + t * 1.5f;
    float u = atan2f(y, x) / (float)(TAU / 2) * 3 + t * 0.2f + 0.3f * sinf(depth * 0.5f);
    float check = fmodf(floorf(u * 2) + floorf(depth * 2), 2);
    if (check < 0) check += 2;
    float stripe = 0.5f + 0.5f * sinf(depth * (float)TAU);
    RGB c = palette(depth * 0.08f + check * 0.15f, (RGB){0.5f, 0.4f, 0.6f}, (RGB){0.5f, 0.4f, 0.4f}, (RGB){1, 1, 1},
                    (RGB){0, 0.15f, 0.3f});
    float shade = (0.55f + 0.45f * check) * (0.7f + 0.3f * stripe) * clampf(r * 1.6f, 0, 1);
    return (RGB){c.r * shade, c.g * shade, c.b * shade};
}

static RGB pix_metaballs(float x, float y, float t)
{
    float field = 0;
    for (int i = 0; i < 6; i++) {
        float cx = 1.1f * sinf(t * (0.5f + 0.13f * i) + i * 1.7f), cy = 0.75f * cosf(t * (0.4f + 0.11f * i) + i * 2.3f);
        field += (0.12f + 0.04f * sinf((float)i)) / ((x - cx) * (x - cx) + (y - cy) * (y - cy) + 1e-4f);
    }
    float inside = smooth(0.9f, 1.1f, field);
    RGB body = palette(fminf(field, 3) * 0.12f + t * 0.1f, (RGB){0.5f, 0.5f, 0.5f}, (RGB){0.5f, 0.5f, 0.5f},
                       (RGB){1, 1, 0.5f}, (RGB){0.8f, 0.9f, 0.3f});
    float g = powf(clampf(field, 0, 1), 3) * 0.35f;
    return (RGB){body.r * inside + 0.2f * g * (1 - inside), body.g * inside + 0.4f * g * (1 - inside),
                 body.b * inside + 1.0f * g * (1 - inside)};
}

static RGB pix_interference(float x, float y, float t)
{
    float sx[3] = {0.7f * sinf(t * 0.5f), -0.7f * cosf(t * 0.4f), 0.5f * sinf(t * 0.3f + 2)};
    float sy[3] = {0.45f * cosf(t * 0.7f), -0.45f * sinf(t * 0.6f), 0.5f * sinf(t * 0.45f + 1)};
    float v = 0;
    for (int i = 0; i < 3; i++) v += sinf(hypotf(x - sx[i], y - sy[i]) * 18 - t * 4);
    RGB c = palette(v / 6 + 0.5f + t * 0.03f, (RGB){0.5f, 0.5f, 0.5f}, (RGB){0.5f, 0.5f, 0.5f}, (RGB){1, 0.7f, 0.4f},
                    (RGB){0, 0.15f, 0.2f});
    float k = clampf(v / 3 * 0.5f + 0.6f, 0.1f, 1);
    return (RGB){c.r * k, c.g * k, c.b * k};
}

static float vor_phase[14][2], vor_speed[14][2];
static int vor_made;

static RGB pix_voronoi(float x, float y, float t)
{
    float d1 = 1e9f, d2 = 1e9f;
    int near = 0;
    for (int i = 0; i < 14; i++) {
        float cx = 1.4f * sinf(t * vor_speed[i][0] + vor_phase[i][0]), cy = 0.95f * cosf(t * vor_speed[i][1] + vor_phase[i][1]);
        float d = (x - cx) * (x - cx) + (y - cy) * (y - cy);
        if (d < d1) {
            d2 = d1;
            d1 = d;
            near = i;
        } else if (d < d2) {
            d2 = d;
        }
    }
    float edge = smooth(0, 0.035f, sqrtf(d2) - sqrtf(d1));
    RGB c = palette(near / 14.0f + t * 0.02f, (RGB){0.55f, 0.5f, 0.5f}, (RGB){0.45f, 0.4f, 0.45f}, (RGB){1, 1, 1},
                    (RGB){0, 0.1f, 0.25f});
    float k = (1 - 0.7f * clampf(sqrtf(d1), 0, 1)) * edge;
    return (RGB){c.r * k, c.g * k, c.b * k};
}

/* the raymarcher: a turning torus melted into a bobbing sphere, over a checker floor */
typedef struct { float c1, s1, c2, s2, bob; } RayScene;

static float ray_scene(const RayScene *s, float px, float py, float pz, float *obj_out, float *floor_out)
{
    float qy = py * s->c1 - pz * s->s1, qz = py * s->s1 + pz * s->c1;
    float qx = px * s->c2 - qy * s->s2;
    qy = px * s->s2 + qy * s->c2;
    float torus = hypotf(hypotf(qx, qz) - 0.9f, qy) - 0.28f;
    float sphere = sqrtf(px * px + (py - s->bob) * (py - s->bob) + pz * pz) - 0.45f;
    float k = 0.35f, hm = clampf(0.5f + 0.5f * (sphere - torus) / k, 0, 1);
    float obj = sphere * (1 - hm) + torus * hm - k * hm * (1 - hm);
    float fl = py + 1.1f;
    if (obj_out) *obj_out = obj;
    if (floor_out) *floor_out = fl;
    return fminf(obj, fl);
}

static RGB pix_raymarch(float x, float y, float t)
{
    RayScene s = {cosf(t), sinf(t), cosf(t * 0.7f), sinf(t * 0.7f), 0.35f * sinf(t * 1.3f)};
    float ang = t * 0.3f, ro[3] = {3.4f * sinf(ang), 0.7f, -3.4f * cosf(ang)};
    float fw[3] = {-ro[0], -ro[1], -ro[2]}, fl = sqrtf(fw[0] * fw[0] + fw[1] * fw[1] + fw[2] * fw[2]);
    for (int i = 0; i < 3; i++) fw[i] /= fl;
    float right[3] = {fw[2], 0, -fw[0]}, rl = sqrtf(right[0] * right[0] + right[2] * right[2]);   /* up x fw */
    for (int i = 0; i < 3; i++) right[i] /= rl;
    float up[3] = {fw[1] * right[2] - fw[2] * right[1], fw[2] * right[0] - fw[0] * right[2], fw[0] * right[1] - fw[1] * right[0]};
    float rd[3];
    for (int i = 0; i < 3; i++) rd[i] = fw[i] + 0.75f * (x * right[i] + y * up[i]);
    float dl = sqrtf(rd[0] * rd[0] + rd[1] * rd[1] + rd[2] * rd[2]);
    for (int i = 0; i < 3; i++) rd[i] /= dl;
    float dist = 0;
    for (int i = 0; i < 40; i++) {
        float d = ray_scene(&s, ro[0] + rd[0] * dist, ro[1] + rd[1] * dist, ro[2] + rd[2] * dist, NULL, NULL);
        dist = fminf(dist + d, 20.0f);
        if (d < 0.002f || dist >= 20.0f) break;
    }
    float p[3] = {ro[0] + rd[0] * dist, ro[1] + rd[1] * dist, ro[2] + rd[2] * dist}, obj, flr;
    float d = ray_scene(&s, p[0], p[1], p[2], &obj, &flr);
    RGB sky = {0.08f + fmaxf(rd[1], 0) * 0.2f, 0.1f + fmaxf(rd[1], 0) * 0.3f, 0.2f + fmaxf(rd[1], 0) * 0.5f};
    if (d >= 0.02f) return (RGB){powf(sky.r, 1 / 2.2f), powf(sky.g, 1 / 2.2f), powf(sky.b, 1 / 2.2f)};
    float e = 0.002f, n[3];
    for (int a = 0; a < 3; a++) {
        float o[3] = {0, 0, 0};
        o[a] = e;
        n[a] = ray_scene(&s, p[0] + o[0], p[1] + o[1], p[2] + o[2], NULL, NULL) -
               ray_scene(&s, p[0] - o[0], p[1] - o[1], p[2] - o[2], NULL, NULL);
    }
    float nl = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) + 1e-9f;
    for (int a = 0; a < 3; a++) n[a] /= nl;
    float light[3] = {0.6f, 0.8f, -0.4f}, ll = sqrtf(0.36f + 0.64f + 0.16f);
    for (int a = 0; a < 3; a++) light[a] /= ll;
    float shadow = 1, sd = 0.05f;
    for (int i = 0; i < 12; i++) {                      /* soft shadows */
        float hh = ray_scene(&s, p[0] + n[0] * 0.02f + light[0] * sd, p[1] + n[1] * 0.02f + light[1] * sd,
                             p[2] + n[2] * 0.02f + light[2] * sd, NULL, NULL);
        shadow = fminf(shadow, clampf(8 * hh / sd, 0, 1));
        sd += clampf(hh, 0.02f, 0.5f);
    }
    float diff = clampf(n[0] * light[0] + n[1] * light[1] + n[2] * light[2], 0, 1) * shadow;
    float hv[3] = {light[0] - rd[0], light[1] - rd[1], light[2] - rd[2]}, hl = sqrtf(hv[0] * hv[0] + hv[1] * hv[1] + hv[2] * hv[2]);
    float spec = powf(clampf((n[0] * hv[0] + n[1] * hv[1] + n[2] * hv[2]) / hl, 0, 1), 32) * shadow;
    float rim = powf(1 - clampf(-(n[0] * rd[0] + n[1] * rd[1] + n[2] * rd[2]), 0, 1), 3);
    int is_floor = flr < obj;
    RGB base;
    if (is_floor) {
        float chk = fmodf(floorf(p[0]) + floorf(p[2]), 2);
        if (chk < 0) chk += 2;
        base.r = base.g = base.b = 0.18f + 0.4f * chk;
    } else {
        base = (RGB){1.0f, 0.55f, 0.25f};
    }
    float lit = 0.12f + 0.88f * diff;
    RGB col = {base.r * lit + spec * 0.8f, base.g * lit + spec * 0.8f, base.b * lit + spec * 0.8f};
    if (!is_floor) col.r += rim * 0.2f, col.g += rim * 0.4f, col.b += rim * 0.8f;
    float fog = 1 - expf(-0.015f * dist * dist);
    col = (RGB){col.r * (1 - fog) + sky.r * fog, col.g * (1 - fog) + sky.g * fog, col.b * (1 - fog) + sky.b * fog};
    return (RGB){powf(clampf(col.r, 0, 1), 1 / 2.2f), powf(clampf(col.g, 0, 1), 1 / 2.2f), powf(clampf(col.b, 0, 1), 1 / 2.2f)};
}

static void pix_rows(void *p, int y0, int y1)
{
    PixCtx *c = p;
    for (int y = y0; y < y1; y++)
        for (int x = 0; x < c->w; x++) {
            float sx = ((2.0f * (x + 0.5f) / c->w) - 1) * c->aspect / c->zoom, sy = (1 - 2.0f * (y + 0.5f) / c->h) / c->zoom;
            RGB v;
            switch (c->id) {
            case 8: v = pix_plasma(sx, sy, c->t); break;
            case 9: v = pix_tunnel(sx, sy, c->t); break;
            case 11: v = pix_metaballs(sx, sy, c->t); break;
            case 12: v = pix_raymarch(sx, sy, c->t); break;
            case 14: v = pix_interference(sx, sy, c->t); break;
            default: v = pix_voronoi(sx, sy, c->t); break;
            }
            float *o = c->out + ((size_t)y * c->w + x) * 3;
            o[0] = v.r;
            o[1] = v.g;
            o[2] = v.b;
        }
}

/* fire: a heat field fed with random sparks along the bottom, rising and cooling */
static float *heat;
static int heat_w, heat_h;

static void fire(float *out, int w, int h)
{
    if (!heat || heat_w != w || heat_h != h) {
        free(heat);
        heat = calloc((size_t)w * (h + 2), sizeof(float));
        heat_w = w;
        heat_h = h;
        if (!heat) return;
    }
    float cool = 1.25f / h;
    for (int pass = 0; pass < 2; pass++) {
        for (int y = h; y < h + 2; y++)
            for (int x = 0; x < w; x++) {
                float r = frand();
                heat[(size_t)y * w + x] = r * r * 1.7f;
            }
        for (int y = 0; y < h; y++)                    /* each row from the two below it */
            for (int x = 0; x < w; x++) {
                const float *b = heat + (size_t)(y + 1) * w, *b2 = heat + (size_t)(y + 2) * w;
                float avg = (b[(x + w - 1) % w] + b[x] + b[(x + 1) % w] + b2[x]) / 4;
                heat[(size_t)y * w + x] = fmaxf(0, avg - cool * (0.6f + 0.8f * frand()));
            }
    }
    for (int i = 0; i < w * h; i++) {
        float v = clampf(heat[i], 0, 1);
        out[3 * i] = clampf(v * 3, 0, 1);
        out[3 * i + 1] = clampf(v * 3 - 1, 0, 1);
        out[3 * i + 2] = clampf(v * 3 - 2, 0, 1);
    }
}

/* starfield: 900 stars flying at the viewer, with trails */
static float star_pos[900][3];
static RGB star_tint[900];
static float *star_img;
static int star_w, star_h, stars_made;

static void starfield(float *out, int w, int h, double dt)
{
    if (!stars_made) {
        for (int i = 0; i < 900; i++) {
            star_pos[i][0] = frand() * 6 - 3;
            star_pos[i][1] = frand() * 6 - 3;
            star_pos[i][2] = 0.1f + frand() * 3.9f;
            star_tint[i] = palette(frand(), (RGB){0.8f, 0.8f, 0.85f}, (RGB){0.2f, 0.2f, 0.2f}, (RGB){1, 1, 1},
                                   (RGB){0, 0.2f, 0.5f});
        }
        stars_made = 1;
    }
    if (!star_img || star_w != w || star_h != h) {
        free(star_img);
        star_img = calloc((size_t)w * h * 3, sizeof(float));
        star_w = w;
        star_h = h;
        if (!star_img) return;
    }
    if (!(dt >= 0 && dt <= 0.25)) dt = 0.03;
    for (size_t i = 0; i < (size_t)w * h * 3; i++) star_img[i] *= dt > 0 ? 0.55f : 1;   /* trails */
    float aspect = (float)w / h;
    for (int i = 0; i < 900; i++) {
        float *p = star_pos[i];
        p[2] -= (float)dt * 1.6f;
        if (p[2] < 0.05f) {
            p[0] = frand() * 6 - 3;
            p[1] = frand() * 6 - 3;
            p[2] = 4.0f;
        }
        int px = (int)((p[0] / p[2] / aspect + 1) * 0.5f * w), py = (int)((1 - p[1] / p[2]) * 0.5f * h);
        if (px < 0 || py < 0 || px >= w || py >= h) continue;
        float b = powf(clampf(1.15f - p[2] / 4, 0, 1), 1.5f);
        for (int dy = 0; dy < 2 && py + dy < h; dy++)    /* 2x2: visible at this resolution */
            for (int dx = 0; dx < 2 && px + dx < w; dx++) {
                float *o = star_img + ((size_t)(py + dy) * w + px + dx) * 3;
                o[0] = fmaxf(o[0], b * star_tint[i].r);
                o[1] = fmaxf(o[1], b * star_tint[i].g);
                o[2] = fmaxf(o[2], b * star_tint[i].b);
            }
    }
    memcpy(out, star_img, (size_t)w * h * 3 * sizeof(float));
}

/* ---------------------------------------------------------------- the entry points */

void scene_reset(int i)
{
    if (i == 10 && heat) memset(heat, 0, (size_t)heat_w * (heat_h + 2) * sizeof(float));
    if (i == 13) {
        stars_made = 0;
        if (star_img) memset(star_img, 0, (size_t)star_w * star_h * 3 * sizeof(float));
    }
}

void scene_render(int i, double t, double dt, float zoom, float *rgb, int w, int h, char *caption, size_t n)
{
    if (caption && n) caption[0] = 0;
    char dummy[64];
    if (!caption) caption = dummy, n = sizeof dummy;
    if (!vor_made) {                                    /* the Voronoi seeds, fixed like termshade's */
        uint32_t keep = rng_state;
        rng_state = 5;
        for (int k = 0; k < 14; k++) {
            vor_phase[k][0] = frand() * (float)TAU;
            vor_phase[k][1] = frand() * (float)TAU;
            vor_speed[k][0] = 0.15f + frand() * 0.35f;
            vor_speed[k][1] = 0.15f + frand() * 0.35f;
        }
        rng_state = keep;
        vor_made = 1;
    }
    if (i < SCENE_SHADER_FIRST) {
        static const RGB tints[8] = {{0.25f, 0.2f, 0.6f}, {0.1f, 0.25f, 0.5f}, {0.5f, 0.15f, 0.1f}, {0.15f, 0.1f, 0.4f},
                                     {0.1f, 0.35f, 0.2f}, {0.35f, 0.1f, 0.4f}, {0.1f, 0.2f, 0.45f}, {0.2f, 0.1f, 0.35f}};
        backdrop(rgb, w, h, zoom, t, tints[i]);
        Canvas cv = canvas_begin(w, h, zoom);
        switch (i) {
        case 0: flower(t, rgb, &cv); break;
        case 1: metatron(t, rgb, &cv); break;
        case 2: sri_yantra(t, rgb, &cv); break;
        case 3: platonic(t, rgb, &cv, caption, n); break;
        case 4: phyllotaxis(t, rgb, &cv, w, h, zoom); break;
        case 5: star_polygons(t, rgb, &cv); break;
        case 6: torus(t, rgb, &cv); break;
        default: spirograph(t, rgb, &cv, caption, n); break;
        }
        finish(rgb, w, h);
        return;
    }
    if (i == 10) {
        fire(rgb, w, h);
        return;
    }
    if (i == 13) {
        starfield(rgb, w, h, dt);
        return;
    }
    PixCtx c = {i, rgb, w, h, (float)w / h, zoom, (float)t};
    par_rows(pix_rows, &c, h);
}
