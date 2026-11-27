/* tinydaw: a small timeline sequencer. SDL2 for window, input, drawing and audio output,
 * SDL2_ttf for text, libsndfile for reading and writing audio files. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "daw.h"
#include "theory.h"
#include "midi.h"
#include "scenes.h"
#include "compat.h"

#include <ctype.h>
#include <SDL.h>
#include <SDL_ttf.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static Project P;
static SDL_AudioDeviceID dev;
static SDL_Window *win;
static SDL_Renderer *ren;
static TTF_Font *font;
static int font_h;
static int W = 1280, H = 760;
static SDL_Cursor *cur_arrow, *cur_resize, *cur_hand;

/* The audio callback runs on its own thread; every change to the project from the UI
 * happens inside LOCK/UNLOCK so the callback never sees a half-edited clip list. */
#define LOCK()   do { if (dev) SDL_LockAudioDevice(dev); } while (0)
#define UNLOCK() do { if (dev) SDL_UnlockAudioDevice(dev); } while (0)

/* ---------------------------------------------------------------- colours */

typedef struct { Uint8 r, g, b, a; } Col;
/* The theme: set by theme_apply. Dark is the original look; light keeps the same structure
 * with the greys inverted and the accents deepened so they read on white. */
static Col BG, PANEL, PANEL2, PANEL3, LINE, TEXT, DIM, ACCENT, DARK, HEAD;
static int theme_light;
enum { THEME_DARK, THEME_LIGHT, THEME_SYSTEM, THEME_XP, THEME_AERO, THEME_BLENDER, THEME_COUNT };

/* How controls are drawn. Dark and Light are flat; the others shape buttons, fields and the bars
 * with gradients, gloss and rounded corners, in the spirit of their namesakes. */
static struct {
    int radius, gloss;                  /* corner radius; 0 flat, 1 gradient, 2 glossy */
    Col btn_top, btn_bot, btn_border, hover_top, hover_bot, hover_border;
    Col on_top, on_bot, on_border, on_text, btn_text;
    Col field_bg, field_border;
    int field_radius;
    Col bar_top, bar_bot, bar_text, bar_dim, bar_hi;
    Col rail_top, rail_bot, rail_text, rail_dim, rail_on;
    int bar_gloss, bubbles, shaped;
} ST;
static int theme_kind;                  /* the look in use: THEME_DARK .. THEME_BLENDER (never SYSTEM) */
static Col rgb(int r, int g, int b) { return (Col){(Uint8)r, (Uint8)g, (Uint8)b, 255}; }
static int theme_choice = THEME_SYSTEM;

static void theme_apply(int kind)       /* THEME_DARK, THEME_LIGHT (1 also means light) or a shaped look */
{
    if (kind == 1) kind = THEME_LIGHT;
    theme_kind = kind;
    theme_light = kind == THEME_LIGHT || kind == THEME_XP || kind == THEME_AERO;
    DARK = rgb(20, 20, 22);
    memset(&ST, 0, sizeof ST);
    switch (kind) {
    case THEME_LIGHT:
        BG = rgb(236, 237, 241); PANEL = rgb(248, 248, 250); PANEL2 = rgb(232, 234, 239); PANEL3 = rgb(216, 220, 228);
        LINE = rgb(196, 200, 210); TEXT = rgb(28, 30, 36); DIM = rgb(96, 102, 116);
        ACCENT = rgb(226, 112, 34); HEAD = rgb(196, 128, 0);
        break;
    case THEME_XP:                      /* Luna: beige windows, blue title bar, green Start button */
        BG = rgb(255, 255, 255); PANEL = rgb(236, 233, 216); PANEL2 = rgb(245, 243, 233); PANEL3 = rgb(225, 222, 206);
        LINE = rgb(172, 168, 153); TEXT = rgb(0, 0, 0); DIM = rgb(95, 95, 95);
        ACCENT = rgb(49, 106, 197); HEAD = rgb(229, 128, 20);
        ST.shaped = 1, ST.radius = 3, ST.gloss = 1;
        ST.btn_top = rgb(255, 255, 255), ST.btn_bot = rgb(214, 208, 197), ST.btn_border = rgb(0, 60, 116);
        ST.hover_top = rgb(255, 255, 255), ST.hover_bot = rgb(236, 225, 196), ST.hover_border = rgb(248, 179, 48);
        ST.on_top = rgb(90, 180, 80), ST.on_bot = rgb(40, 130, 40), ST.on_border = rgb(25, 90, 25);
        ST.on_text = rgb(255, 255, 255), ST.btn_text = rgb(0, 0, 0);
        ST.field_bg = rgb(255, 255, 255), ST.field_border = rgb(127, 157, 185), ST.field_radius = 0;
        ST.bar_top = rgb(64, 138, 245), ST.bar_bot = rgb(20, 78, 210), ST.bar_hi = rgb(140, 190, 255);
        ST.bar_text = rgb(255, 255, 255), ST.bar_dim = rgb(205, 222, 255);
        ST.rail_top = rgb(123, 162, 231), ST.rail_bot = rgb(99, 117, 214);
        ST.rail_text = rgb(255, 255, 255), ST.rail_dim = rgb(225, 234, 255), ST.rail_on = rgb(198, 211, 247);
        break;
    case THEME_AERO:                    /* glossy glass, sky and aqua, fresh green */
        BG = rgb(234, 246, 255); PANEL = rgb(244, 251, 255); PANEL2 = rgb(226, 242, 252); PANEL3 = rgb(204, 232, 250);
        LINE = rgb(160, 205, 232); TEXT = rgb(18, 42, 72); DIM = rgb(70, 110, 145);
        ACCENT = rgb(30, 155, 230); HEAD = rgb(80, 180, 30);
        ST.shaped = 1, ST.radius = 8, ST.gloss = 2, ST.bar_gloss = 1, ST.bubbles = 1;
        ST.btn_top = rgb(240, 250, 255), ST.btn_bot = rgb(170, 218, 246), ST.btn_border = rgb(95, 165, 212);
        ST.hover_top = rgb(250, 255, 255), ST.hover_bot = rgb(190, 235, 255), ST.hover_border = rgb(40, 150, 225);
        ST.on_top = rgb(170, 236, 120), ST.on_bot = rgb(60, 170, 40), ST.on_border = rgb(45, 130, 30);
        ST.on_text = rgb(255, 255, 255), ST.btn_text = rgb(18, 42, 72);
        ST.field_bg = rgb(255, 255, 255), ST.field_border = rgb(130, 190, 230), ST.field_radius = 6;
        ST.bar_top = rgb(120, 205, 250), ST.bar_bot = rgb(30, 135, 215), ST.bar_hi = rgb(255, 255, 255);
        ST.bar_text = rgb(255, 255, 255), ST.bar_dim = rgb(225, 245, 255);
        ST.rail_top = rgb(215, 240, 255), ST.rail_bot = rgb(150, 205, 240);
        ST.rail_text = rgb(18, 42, 72), ST.rail_dim = rgb(50, 95, 135), ST.rail_on = rgb(255, 255, 255);
        break;
    case THEME_BLENDER:                 /* neutral greys, rounded flat widgets, blue selection, orange playhead */
        BG = rgb(40, 40, 40); PANEL = rgb(48, 48, 48); PANEL2 = rgb(84, 84, 84); PANEL3 = rgb(100, 100, 100);
        LINE = rgb(62, 62, 62); TEXT = rgb(230, 230, 230); DIM = rgb(160, 160, 160);
        ACCENT = rgb(71, 114, 179); HEAD = rgb(255, 140, 0);
        ST.shaped = 1, ST.radius = 4, ST.gloss = 1;
        ST.btn_top = rgb(88, 88, 88), ST.btn_bot = rgb(80, 80, 80), ST.btn_border = rgb(36, 36, 36);
        ST.hover_top = rgb(102, 102, 102), ST.hover_bot = rgb(94, 94, 94), ST.hover_border = rgb(36, 36, 36);
        ST.on_top = rgb(84, 128, 194), ST.on_bot = rgb(71, 114, 179), ST.on_border = rgb(36, 36, 36);
        ST.on_text = rgb(255, 255, 255), ST.btn_text = rgb(230, 230, 230);
        ST.field_bg = rgb(84, 84, 84), ST.field_border = rgb(36, 36, 36), ST.field_radius = 4;
        ST.bar_top = rgb(48, 48, 48), ST.bar_bot = rgb(48, 48, 48), ST.bar_hi = rgb(48, 48, 48);
        ST.bar_text = rgb(230, 230, 230), ST.bar_dim = rgb(160, 160, 160);
        ST.rail_top = rgb(40, 40, 40), ST.rail_bot = rgb(40, 40, 40);
        ST.rail_text = rgb(230, 230, 230), ST.rail_dim = rgb(160, 160, 160), ST.rail_on = rgb(71, 114, 179);
        break;
    default:
        BG = rgb(22, 23, 27); PANEL = rgb(31, 33, 39); PANEL2 = rgb(41, 44, 52); PANEL3 = rgb(54, 58, 68);
        LINE = rgb(58, 62, 72); TEXT = rgb(222, 224, 230); DIM = rgb(138, 144, 156);
        ACCENT = rgb(240, 138, 64); HEAD = rgb(255, 214, 90);
        break;
    }
}
static const Col TRACK_COL[MAX_TRACKS] = {
    {226, 104, 92, 255}, {232, 158, 70, 255}, {212, 198, 82, 255}, {118, 198, 110, 255},
    {80, 188, 190, 255}, {92, 142, 230, 255}, {160, 112, 220, 255}, {220, 100, 168, 255},
    {236, 128, 112, 255}, {240, 186, 96, 255}, {170, 210, 90, 255}, {90, 200, 150, 255},
    {110, 170, 240, 255}, {140, 130, 240, 255}, {200, 130, 230, 255}, {240, 130, 160, 255},
};

static Col shade(Col c, float k)
{
    float r = c.r * k, g = c.g * k, b = c.b * k;
    return (Col){r > 255 ? 255 : (Uint8)r, g > 255 ? 255 : (Uint8)g, b > 255 ? 255 : (Uint8)b, c.a};
}
static Col alpha(Col c, Uint8 a) { c.a = a; return c; }

/* shade() for interface greys that follows the theme: "dimmer" means closer to the background,
 * so on a light theme it lightens instead of darkening, and "brighter" darkens */
static Col tone(Col c, float k)
{
    if (!theme_light) return shade(c, k);
    if (k >= 1) return shade(c, 1 - (k - 1) * 0.35f);   /* gentler: light greys darken fast */
    float t = 1 - k;
    return (Col){(Uint8)(c.r + (255 - c.r) * t), (Uint8)(c.g + (255 - c.g) * t), (Uint8)(c.b + (255 - c.b) * t), c.a};
}

/* A track colour at strength k: darkened on the dark theme, washed towards white on the light */
static Col wash(Col c, float k)
{
    if (!theme_light) return shade(c, k);
    float t = (1 - k) * 0.85f;
    return (Col){(Uint8)(c.r + (255 - c.r) * t), (Uint8)(c.g + (255 - c.g) * t), (Uint8)(c.b + (255 - c.b) * t), c.a};
}

static void fill(SDL_Rect r, Col c)
{
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(ren, &r);
}
static void outline(SDL_Rect r, Col c)
{
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    SDL_RenderDrawRect(ren, &r);
}
static void line(int x0, int y0, int x1, int y1, Col c)
{
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    SDL_RenderDrawLine(ren, x0, y0, x1, y1);
}

static Col mix_col(Col a, Col b, float t)
{
    return (Col){(Uint8)(a.r + (b.r - a.r) * t), (Uint8)(a.g + (b.g - a.g) * t), (Uint8)(a.b + (b.b - a.b) * t),
                 (Uint8)(a.a + (b.a - a.a) * t)};
}

static int corner_inset(int radius, int yy, int h)   /* how far a rounded corner pulls row yy in */
{
    if (radius <= 0) return 0;
    if (radius * 2 > h) radius = h / 2;
    double dy = yy < radius ? radius - yy - 0.5 : yy >= h - radius ? yy - (h - radius) + 0.5 : 0;
    if (dy <= 0) return 0;
    return radius - (int)floor(sqrt((double)radius * radius - dy * dy) + 0.5);
}

/* A box with rounded corners, shaded from `top` to `bot`; gloss adds a glassy highlight on top */
static void shape_fill(SDL_Rect r, Col top, Col bot, int radius, int gloss)
{
    Col white = {255, 255, 255, 255};
    for (int yy = 0; yy < r.h; yy++) {
        float t = r.h > 1 ? (float)yy / (r.h - 1) : 0;
        Col c = mix_col(top, bot, t);
        if (gloss && yy < r.h / 2) c = mix_col(c, white, 0.5f * (1 - (float)yy / (r.h / 2)));
        if (gloss && yy == r.h / 2) c = mix_col(c, bot, 0.25f);      /* the glass edge */
        int in = corner_inset(radius, yy, r.h);
        line(r.x + in, r.y + yy, r.x + r.w - 1 - in, r.y + yy, c);
    }
}

static void shape_outline(SDL_Rect r, Col c, int radius)
{
    if (radius <= 0) {
        outline(r, c);
        return;
    }
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    int prev = corner_inset(radius, 0, r.h);
    line(r.x + prev, r.y, r.x + r.w - 1 - prev, r.y, c);
    line(r.x + prev, r.y + r.h - 1, r.x + r.w - 1 - prev, r.y + r.h - 1, c);
    for (int yy = 1; yy < r.h - 1; yy++) {       /* the sides, joining each row's edge to the last */
        int in = corner_inset(radius, yy, r.h), pin = corner_inset(radius, yy - (yy < r.h / 2 ? 1 : -1), r.h);
        int a = in < pin ? in : pin, b = in > pin ? in : pin;
        line(r.x + a, r.y + yy, r.x + b, r.y + yy, c);
        line(r.x + r.w - 1 - b, r.y + yy, r.x + r.w - 1 - a, r.y + yy, c);
    }
}

/* the theme's button: 0 normal, 1 hover, 2 on, 3 disabled */
static void style_box(SDL_Rect r, int state)
{
    if (!ST.shaped) {
        if (state == 3) {
            fill(r, tone(PANEL2, 0.8f));
            outline(r, tone(LINE, 0.8f));
        } else {
            fill(r, state == 2 ? ACCENT : state == 1 ? PANEL3 : PANEL2);
            outline(r, state == 2 ? tone(ACCENT, 1.2f) : LINE);
        }
        return;
    }
    Col top = state == 2 ? ST.on_top : state == 1 ? ST.hover_top : ST.btn_top;
    Col bot = state == 2 ? ST.on_bot : state == 1 ? ST.hover_bot : ST.btn_bot;
    Col bd = state == 2 ? ST.on_border : state == 1 ? ST.hover_border : ST.btn_border;
    if (state == 3) {
        top = mix_col(ST.btn_top, PANEL, 0.6f);
        bot = mix_col(ST.btn_bot, PANEL, 0.6f);
        bd = mix_col(ST.btn_border, PANEL, 0.6f);
    }
    shape_fill(r, top, bot, ST.radius, ST.gloss == 2);
    shape_outline(r, bd, ST.radius);
    if (state == 1 && theme_kind == THEME_XP)    /* Luna's orange hover glow, inside the border */
        shape_outline((SDL_Rect){r.x + 1, r.y + 1, r.w - 2, r.h - 2}, rgb(253, 216, 137), ST.radius - 1);
}

static Col btn_fg(int on) { return on ? (ST.shaped ? ST.on_text : DARK) : (ST.shaped ? ST.btn_text : TEXT); }

/* a text field's box */
static void field_box(SDL_Rect r, int hov, int active)
{
    if (!ST.shaped) {
        fill(r, active || hov ? PANEL3 : PANEL2);
        outline(r, active ? ACCENT : LINE);
        return;
    }
    shape_fill(r, ST.field_bg, ST.field_bg, ST.field_radius, 0);
    shape_outline(r, active ? ACCENT : hov ? ST.hover_border : ST.field_border, ST.field_radius);
}

/* ---------------------------------------------------------------- text */

typedef struct { char s[128]; Uint32 key; SDL_Texture *t; SDL_Renderer *r; int w, h; unsigned used; } TextEntry;
static TextEntry tcache[512];
static unsigned tclock;

static TextEntry *text_get(const char *s, Col c)
{
    Uint32 key = (Uint32)c.r << 16 | (Uint32)c.g << 8 | c.b;
    TextEntry *victim = NULL;
    for (size_t i = 0; i < sizeof tcache / sizeof *tcache; i++) {
        TextEntry *e = &tcache[i];
        if (e->t) {
            if (e->key == key && e->r == ren && strncmp(e->s, s, sizeof e->s - 1) == 0) {
                e->used = ++tclock;
                return e;
            }
            if (!victim || (victim->t && e->used < victim->used)) victim = e;
        } else if (!victim || victim->t) {
            victim = e;
        }
    }
    if (victim->t) SDL_DestroyTexture(victim->t);
    victim->t = NULL;
    snprintf(victim->s, sizeof victim->s, "%s", s);
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, victim->s[0] ? victim->s : " ",
                                               (SDL_Color){c.r, c.g, c.b, 255});
    if (!surf) return NULL;
    victim->t = SDL_CreateTextureFromSurface(ren, surf);
    victim->r = ren;                     /* textures belong to one window's renderer */
    victim->w = surf->w;
    victim->h = surf->h;
    SDL_FreeSurface(surf);
    victim->key = key;
    victim->used = ++tclock;
    return victim->t ? victim : NULL;
}

static void text_forget(SDL_Renderer *r)   /* a window is closing: its text goes with it */
{
    for (size_t i = 0; i < sizeof tcache / sizeof *tcache; i++)
        if (tcache[i].t && tcache[i].r == r) {
            SDL_DestroyTexture(tcache[i].t);
            tcache[i].t = NULL;
        }
}

static int text(int x, int y, const char *s, Col c)
{
    TextEntry *e = text_get(s, c);
    if (!e) return 0;
    SDL_Rect d = {x, y, e->w, e->h};
    SDL_RenderCopy(ren, e->t, NULL, &d);
    return e->w;
}

static int text_w(const char *s)
{
    int w = 0, h = 0;
    TTF_SizeUTF8(font, s, &w, &h);
    return w;
}

static void text_clip(SDL_Rect r, int x, int y, const char *s, Col c)
{
    SDL_Rect old;
    SDL_bool had = SDL_RenderIsClipEnabled(ren);
    SDL_RenderGetClipRect(ren, &old);
    SDL_Rect clip = r;
    if (had && !SDL_IntersectRect(&old, &r, &clip)) return;
    SDL_RenderSetClipRect(ren, &clip);
    text(x, y, s, c);
    SDL_RenderSetClipRect(ren, had ? &old : NULL);
}

static void text_center(SDL_Rect r, const char *s, Col c)
{
    text(r.x + (r.w - text_w(s)) / 2, r.y + (r.h - font_h) / 2, s, c);
}

static char status_msg[300];

static void status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(status_msg, sizeof status_msg, fmt, ap);
    va_end(ap);
    if (getenv("TINYDAW_DEBUG_INPUT")) fprintf(stderr, "status: %s\n", status_msg);
}

/* ---------------------------------------------------------------- input */

static int mx, my, ldown, lpress, lrel, rpress, wheel_x, wheel_y;
static SDL_Keymod mods;

static int hit(SDL_Rect r) { return mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h; }

enum { D_NONE, D_LIBPEND, D_LIB, D_MOVE, D_TRIML, D_TRIMR, D_PLAYHEAD, D_LOOP, D_NUM, D_NOTE, D_TVOL, D_BEAT,
       D_FADEIN, D_FADEOUT, D_BAND, D_SCROLL, D_AUTO, D_NMOVE, D_NLEN, D_NBAND, D_VEL, D_SLIDE, D_ED };
static int beat_layout(int genre, long *pattern_frames, int *reps);
/* the clip editor window (further down) */
static SDL_Window *ewin;
static SDL_Renderer *eren;
static Uint32 ewin_id;
static int ed_clip = -1, ed_handle, drag_in_editor;
static Clip ed_last, ed_orig;            /* the clip as last seen (to find it again), and at a drag's start */
static int ed_nclips;
static long ed_v0, ed_v1;                /* the part of the sound shown, in frames */
static SDL_Rect ed_wave, ed_btn[8];
static struct { int mx, my, ldown, lpress, lrel, rpress, wheel_x, wheel_y, ldouble; } ein;
static void editor_drag(void);
static void editor_open(int clip);
static void editor_close(void);
static void editor_frame(void);
static int ed_frame_to_x(long f);
static void place_beat(int genre, long start, int first_track);

static struct {
    int kind, sample, clip, track, sx, sy;
    long a, b, c, grab;
    double *num, nmin, nmax, nstep, norig;
    int *inum, iorig, imin, imax;
} drag;

/* ---------------------------------------------------------------- layout and view */

static struct { int top, left, head, ruler, status, lane_h, auto_h, tl_x, tl_w, lanes_y, pane_x, pane_w; } L;

/* automation lanes (one shown under a track when its A is on) and the piano roll */
static int auto_open[MAX_TRACKS], auto_param[MAX_TRACKS];
static int roll_clip = -1, roll_pattern, roll_top = -1, roll_kh = 14;
static double roll_saved_view, roll_saved_pps;
static long roll_len;                   /* the length of the last note drawn or stretched */
static unsigned char nsel[MAX_NOTES];   /* selected notes in the piano roll */
static int ldouble;                     /* this left press is a double-click */
static int chord_sample(const int *notes, int n, const char *label, double beats);

/* Theory pane state */
#define PANE_W 480
static int pads_sub, mixer_sub, vis_sub, settings_sub, sounds_sub;   /* each panel's open sub-tab */
static int pads_open;                   /* the Pads pane (shares the right-hand slot) */
static int mixer_open;                  /* the Mixer pane: clip, track and master effects */
static int vis_open;                    /* the Visualizer pane: spectrum, scope, meters */
static int show_open;                   /* the full-screen show */
static int show_scene, show_cycle = 1;  /* 0 = blobs, 1.. = scenes.c's scene show_scene - 1 */
static double show_each = 24, show_clock, show_started[SCENE_COUNT + 1];
static const char *show_name(int k);
static int sounds_open = 1;             /* the Sounds panel: library and new sounds */
static int lib_badge;                   /* sounds added since the Sounds panel was last opened */
static Uint32 lib_badge_at;
static int th_len = 2;
static int add_to_library(const int *notes, int n, const char *label);
static void add_beat_to_library(int g);
static int settings_open;
static void disc(int cx, int cy, int r, Col c);
enum { RI_SOUNDS, RI_THEORY, RI_PADS, RI_MIXER, RI_VISUAL, RI_SETTINGS, RI_COUNT };
static void open_panel(int which);
static void show_toggle(void);
static void settings_save(void);
static Mixer *MX;                       /* the live mixer (see audio_cb) */
static int sel_track;                   /* the track the Mixer pane shows */
static int headless;                    /* self-test or screenshot: touch no files */
static int pads_drop(int sample);
static int theory_open, theory_tab = 6, th_root, th_scale, th_sevenths, th_track = 4, th_scroll, th_content_h;

/* one-off preview buffer (chords, intervals, notes) played on top of everything */
static float *scratch;
static long scratch_len, scratch_pos;
static double view_start = 0;           /* seconds at the left edge of the timeline */
static double pps = 110;                /* pixels per second */
static int selected = -1;              /* the clip clicked last (the Mixer pane edits it) */
static unsigned char csel[MAX_CLIPS];   /* every selected clip; selected is always one of them */
static int lib_scroll = 0;
/* A tempo edit by hand (BPM box, its menu, tap tempo): the tempo it started from, and when it
 * last moved. Once it settles, the selected clips are matched to the new tempo (tempo_apply). */
static double tempo_from;
static Uint32 tempo_at;
static void tempo_touch(double before)
{
    if (P.bpm == before) return;
    if (tempo_from == 0) tempo_from = before;
    tempo_at = SDL_GetTicks();
}
static SDL_Rect lib_rects[MAX_SAMPLES];  /* where each library row was drawn (used by --selftest) */
static GenParams G = {WAVE_SAW, 45, 1.0, 0.6, 5, 60, 440};

static const char *SNAP_NAMES[] = {"off", "bar", "1/2", "1/4", "1/8", "1/16"};
static const double SNAP_BEATS[] = {0, 4, 2, 1, 0.5, 0.25};
static int snap_i = 4;

static void layout(void)
{
    SDL_GetWindowSize(win, &W, &H);
    L.top = 48;
    L.head = 170;
    L.ruler = 44;                       /* bar numbers above, minutes:seconds below */
    L.status = 26;
    L.pane_x = 70;                      /* RAIL_W: the category rail is on the far left */
    L.pane_w = theory_open || pads_open || mixer_open || vis_open || sounds_open ? PANE_W : settings_open ? 440 : 0;
    L.left = L.pane_x + L.pane_w;
    L.tl_x = L.left + L.head;
    L.tl_w = W - L.tl_x;
    L.lanes_y = L.top + L.ruler;
    int avail = H - L.lanes_y - L.status, nopen = 0;
    for (int t = 0; t < P.ntracks; t++) nopen += auto_open[t] != 0;
    L.auto_h = nopen ? avail / (P.ntracks + nopen) : 0;   /* a lane gets a row's share, within reason */
    if (nopen && L.auto_h < 58) L.auto_h = 58;
    if (L.auto_h > 110) L.auto_h = 110;
    L.lane_h = (avail - nopen * L.auto_h) / P.ntracks;
    if (L.lane_h < 22) L.lane_h = 22;
    if (L.lane_h > 96) L.lane_h = 96;
}

/* where track t's row starts: rows below an open automation lane move down by its height */
static int track_y(int t)
{
    int y = L.lanes_y;
    for (int k = 0; k < t && k < P.ntracks; k++) y += L.lane_h + (auto_open[k] ? L.auto_h : 0);
    return y;
}
static int lanes_h(void) { return track_y(P.ntracks) - L.lanes_y; }
static SDL_Rect auto_rect(int t) { return (SDL_Rect){L.tl_x, track_y(t) + L.lane_h, L.tl_w, auto_open[t] ? L.auto_h : 0}; }
static int auto_lane_at(int y)          /* the track whose automation lane is at y, or -1 */
{
    for (int t = 0; t < P.ntracks; t++) {
        int y0 = track_y(t) + L.lane_h;
        if (auto_open[t] && y >= y0 && y < y0 + L.auto_h) return t;
    }
    return -1;
}

static double frame_to_x(long f) { return L.tl_x + ((double)f / SR - view_start) * pps; }
static double x_to_frame_raw(int x) { return (view_start + (x - L.tl_x) / pps) * SR; }
static long x_to_frame(int x)
{
    double f = x_to_frame_raw(x);
    return f < 0 ? 0 : (long)f;
}
static int lane_at(int y)
{
    for (int t = 0; t < P.ntracks; t++)
        if (y < track_y(t + 1)) return t;
    return P.ntracks - 1;
}
static long beat_frames(void) { return (long)llround(60.0 / P.bpm * SR); }
static long snap_frames(void)
{
    return SNAP_BEATS[snap_i] > 0 ? (long)llround(SNAP_BEATS[snap_i] * 60.0 / P.bpm * SR) : 0;
}
static long snap(long f)                /* Shift held = no snapping */
{
    long s = snap_frames();
    if (!s || (mods & KMOD_SHIFT)) return f;
    return (long)llround((double)f / s) * s;
}
static SDL_Rect clip_rect(const Clip *c)
{
    int x0 = (int)floor(frame_to_x(c->start)), x1 = (int)ceil(frame_to_x(c->start + c->length));
    return (SDL_Rect){x0, track_y(c->track) + 2, x1 - x0 < 2 ? 2 : x1 - x0, L.lane_h - 4};
}
/* The little squares on a clip's top corners that drag its fades (none on very narrow clips). */
static int fade_handles(const Clip *c, SDL_Rect *in, SDL_Rect *out)
{
    SDL_Rect r = clip_rect(c);
    if (r.w < 40 || r.h < 20) return 0;
    int fi = (int)(c->fade_in / (double)SR * pps), fo = (int)(c->fade_out / (double)SR * pps);
    int xi = r.x + (fi > 8 ? fi : 8), xo = r.x + r.w - (fo > 8 ? fo : 8);
    *in = (SDL_Rect){xi - 4, r.y + 1, 9, 9};
    *out = (SDL_Rect){xo - 5, r.y + 1, 9, 9};
    return 1;
}

static int fade_hit(int i)
{
    SDL_Rect a, b;
    if (!fade_handles(&P.clips[i], &a, &b)) return D_NONE;
    a = (SDL_Rect){a.x - 2, a.y - 2, a.w + 4, a.h + 4};
    b = (SDL_Rect){b.x - 2, b.y - 2, b.w + 4, b.h + 4};
    return hit(a) ? D_FADEIN : hit(b) ? D_FADEOUT : D_NONE;
}

static SDL_Rect volume_bar(int t)
{
    if (L.lane_h < 40)                  /* compact rows: a thin bar along the bottom */
        return (SDL_Rect){L.left + 12, track_y(t) + L.lane_h - 6, L.head - 24, 3};
    return (SDL_Rect){L.left + 12, track_y(t) + L.lane_h - 14, L.head - 24, 6};
}

/* ---------------------------------------------------------------- project edits */

static int clip_add(int sample, int track, long start, long offset, long length)
{
    if (P.nclips >= MAX_CLIPS) {
        status("Clip limit reached (%d)", MAX_CLIPS);
        return -1;
    }
    LOCK();
    P.clips[P.nclips] = (Clip){sample, track, start, offset, length, 0, 0, 0, 0, 0};
    P.nclips++;
    UNLOCK();
    csel[P.nclips - 1] = 0;
    return P.nclips - 1;
}

/* ---- the selection */

static void sel_clear(void)
{
    memset(csel, 0, sizeof csel);
    selected = -1;
}

static void sel_only(int i)
{
    sel_clear();
    if (i >= 0 && i < P.nclips) {
        csel[i] = 1;
        selected = i;
    }
}

static int sel_count(void)
{
    int n = 0;
    for (int i = 0; i < P.nclips; i++) n += csel[i];
    return n;
}

static void sel_fix_primary(void)       /* keep `selected` pointing at a selected clip */
{
    if (selected >= 0 && selected < P.nclips && csel[selected]) return;
    selected = -1;
    for (int i = P.nclips - 1; i >= 0 && selected < 0; i--)
        if (csel[i]) selected = i;
}

static void clip_delete(int i)
{
    LOCK();
    memmove(&P.clips[i], &P.clips[i + 1], (size_t)(P.nclips - i - 1) * sizeof(Clip));
    P.nclips--;
    UNLOCK();
    memmove(&csel[i], &csel[i + 1], (size_t)(P.nclips - i));
    csel[P.nclips] = 0;
    if (selected == i) selected = -1;
    else if (selected > i) selected--;
    sel_fix_primary();
}

static int delete_selection(void)       /* returns how many went */
{
    int n = 0;
    for (int i = P.nclips - 1; i >= 0; i--)
        if (csel[i]) {
            clip_delete(i);
            n++;
        }
    sel_clear();
    return n;
}

/* Copies of the selected clips, `shift` frames later; the copies become the selection. */
static int duplicate_selection(long shift)
{
    int idx[MAX_CLIPS], n = 0, made = 0, count = P.nclips;
    for (int i = 0; i < count; i++)
        if (csel[i]) idx[n++] = i;
    sel_clear();
    for (int k = 0; k < n; k++) {
        Clip c = P.clips[idx[k]];
        int m = clip_add(c.sample, c.track, c.start + shift, c.offset, c.length);
        if (m < 0) break;
        LOCK();
        P.clips[m] = c;
        P.clips[m].start = c.start + shift;
        UNLOCK();
        csel[m] = 1;
        selected = m;
        made++;
    }
    return made;
}

static void selection_span(long *a, long *b)
{
    *a = -1;
    *b = 0;
    for (int i = 0; i < P.nclips; i++)
        if (csel[i]) {
            if (*a < 0 || P.clips[i].start < *a) *a = P.clips[i].start;
            if (P.clips[i].start + P.clips[i].length > *b) *b = P.clips[i].start + P.clips[i].length;
        }
    if (*a < 0) *a = 0;
}

/* the clipboard: clips relative to the earliest one */
static Clip clipboard[MAX_CLIPS];
static int nclipboard;

static void copy_selection(void)
{
    long a, b;
    selection_span(&a, &b);
    nclipboard = 0;
    for (int i = 0; i < P.nclips; i++)
        if (csel[i]) {
            clipboard[nclipboard] = P.clips[i];
            clipboard[nclipboard++].start -= a;
        }
}

static int paste_at(long at)
{
    sel_clear();
    int made = 0;
    for (int k = 0; k < nclipboard; k++) {
        Clip c = clipboard[k];
        if (c.track >= P.ntracks || c.sample >= P.nsamples) continue;
        int m = clip_add(c.sample, c.track, at + c.start, c.offset, c.length);
        if (m < 0) break;
        LOCK();
        P.clips[m] = c;
        P.clips[m].start = at + c.start;
        UNLOCK();
        csel[m] = 1;
        selected = m;
        made++;
    }
    return made;
}

/* Moves every selected clip by df frames and dt tracks, as far as they can all go. */
static long g_start[MAX_CLIPS];
static unsigned char band_base[MAX_CLIPS];  /* the selection when a box drag began (Shift adds) */
static int g_track[MAX_CLIPS];

static void group_remember(void)
{
    for (int i = 0; i < P.nclips; i++) {
        g_start[i] = P.clips[i].start;
        g_track[i] = P.clips[i].track;
    }
}

/* the library list is showing and the pointer is over it (a drop target for clips) */
static int over_library(void)
{
    return sounds_open && sounds_sub == 0 && L.pane_w && mx >= L.pane_x && mx < L.pane_x + L.pane_w && my >= L.top &&
           my < H - L.status;
}

static void group_place(long df, int dt)
{
    long min_start = -1;
    int min_t = MAX_TRACKS, max_t = -1;
    for (int i = 0; i < P.nclips; i++)
        if (csel[i]) {
            if (min_start < 0 || g_start[i] < min_start) min_start = g_start[i];
            if (g_track[i] < min_t) min_t = g_track[i];
            if (g_track[i] > max_t) max_t = g_track[i];
        }
    if (min_start < 0) return;
    if (min_start + df < 0) df = -min_start;
    if (min_t + dt < 0) dt = -min_t;
    if (max_t + dt > P.ntracks - 1) dt = P.ntracks - 1 - max_t;
    LOCK();
    for (int i = 0; i < P.nclips; i++)
        if (csel[i]) {
            P.clips[i].start = g_start[i] + df;
            P.clips[i].track = g_track[i] + dt;
        }
    UNLOCK();
}

static void preview(int sample)
{
    LOCK();
    if (P.preview == sample) {
        P.preview = -1;
    } else {
        P.preview = sample;
        P.preview_pos = 0;
    }
    UNLOCK();
}

/* ---------------------------------------------------------------- metronome */

/* A click on every beat (or 8th/16th), louder on the first beat of each bar, and an optional
 * count-in before playback starts. It plays in the audio callback on top of everything and is
 * never exported. Its settings belong to this computer (settings.conf), not to a project. */
enum { CLICK_CLICK, CLICK_BEEP, CLICK_WOOD, CLICK_SOUNDS };
static struct {
    int on, sound, sub, accent, countin, countin_rec_only;
    float volume;
} MET = {0, CLICK_CLICK, 1, 1, 1, 1, 0.6f};
static const char *const CLICK_NAMES[CLICK_SOUNDS] = {"Click", "Beep", "Wood"};
static float *click_buf[3];             /* bar, beat, subdivision */
static long click_len, click_pos = -1;
static int click_kind, click_rate, click_sound = -1;
static long countin_left, countin_total; /* frames of count-in still to play (audio thread) */
static int pads_rec;
#define MET_PI 3.14159265358979323846

static void metronome_prepare(void)     /* (re)make the click sounds at the project rate */
{
    if (click_rate == SR && click_sound == MET.sound) return;
    long n = SR / 16;                   /* 62 ms */
    float *nb[3];
    static const double freq[CLICK_SOUNDS][3] = {{2200, 1600, 1600}, {1320, 880, 880}, {1900, 1500, 1500}};
    static const float level[3] = {1.0f, 0.7f, 0.38f};
    for (int k = 0; k < 3; k++) {
        nb[k] = calloc((size_t)n * 2, sizeof(float));
        if (!nb[k]) {
            while (k--) free(nb[k]);
            return;
        }
        Uint32 seed = 12345;
        for (long i = 0; i < n; i++) {
            double t = (double)i / SR, f = freq[MET.sound][k], v;
            seed = seed * 1664525u + 1013904223u;
            double noise = (seed >> 9) / 4194304.0 - 1;
            if (MET.sound == CLICK_BEEP) v = sin(2 * MET_PI * f * t) * exp(-t * 45) * (t < 0.002 ? t / 0.002 : 1);
            else if (MET.sound == CLICK_WOOD) v = (sin(2 * MET_PI * f * t) * 0.7 + noise * 0.3) * exp(-t * 110);
            else v = sin(2 * MET_PI * f * t) * exp(-t * 160) + noise * exp(-t * 900) * 0.4;
            nb[k][2 * i] = nb[k][2 * i + 1] = (float)(v * level[k] * 0.6);
        }
    }
    float *old[3];
    LOCK();
    for (int k = 0; k < 3; k++) {
        old[k] = click_buf[k];
        click_buf[k] = nb[k];
    }
    click_len = n;
    click_pos = -1;
    click_rate = SR;
    click_sound = MET.sound;
    UNLOCK();
    for (int k = 0; k < 3; k++) free(old[k]);
}

/* Adds the clicks for song frames [start, start + n) to out (audio thread). force: count-in */
static void metronome_mix(float *out, long start, int n, int force)
{
    if ((!MET.on && !force) || !click_buf[0] || click_rate != SR) return;
    int sub = MET.sub < 1 ? 1 : MET.sub;
    double tick = 60.0 / P.bpm * SR / sub;
    long k = (long)ceil(start / tick - 1e-9);
    long next = (long)llround(k * tick);
    for (int i = 0; i < n; i++) {
        if (start + i == next) {
            click_pos = 0;
            click_kind = k % (4 * sub) == 0 && MET.accent ? 0 : k % sub == 0 ? 1 : 2;
            k++;
            next = (long)llround(k * tick);
        }
        if (click_pos >= 0) {
            out[2 * i] += click_buf[click_kind][2 * click_pos] * MET.volume;
            out[2 * i + 1] += click_buf[click_kind][2 * click_pos + 1] * MET.volume;
            if (++click_pos >= click_len) click_pos = -1;
        }
    }
}

static long play_from;                  /* where playback last started: Stop goes back there */

static void toggle_play(void)
{
    LOCK();
    P.playing = !P.playing;
    if (P.playing) play_from = P.playhead;
    countin_left = 0;
    if (P.playing && MET.countin > 0 && (!MET.countin_rec_only || pads_rec)) {
        countin_total = countin_left = MET.countin * 4 * beat_frames();   /* a bar or two of clicks first */
        click_pos = -1;
    }
    UNLOCK();
}

static Uint32 taps[8];
static int ntaps;

static void tap_tempo(void)             /* tap along: the average gap sets the tempo */
{
    Uint32 now = SDL_GetTicks();
    if (ntaps && now - taps[ntaps - 1] > 2000) ntaps = 0;
    if (ntaps == 8) {
        memmove(taps, taps + 1, 7 * sizeof *taps);
        ntaps = 7;
    }
    taps[ntaps++] = now;
    if (ntaps >= 2) {
        double bpm = 60000.0 * (ntaps - 1) / (taps[ntaps - 1] - taps[0]);
        if (bpm >= 40 && bpm <= 300) {
            double before = P.bpm;
            P.bpm = round(bpm);
            tempo_touch(before);
            status("Tempo %.0f BPM from %d taps (%s)", P.bpm, ntaps,
                   sel_count() ? "the selected clips follow it" : "clips keep their times; select some to have them follow");
        }
    } else {
        status("Tap: keep tapping on the beat");
    }
}

/* ---- moving around the timeline */

static void view_show(long f)           /* scroll so frame f is in view (centred if it was off) */
{
    double x = frame_to_x(f);
    if (x < L.tl_x + 20 || x > L.tl_x + L.tl_w - 20) {
        view_start = (double)f / SR - L.tl_w / 2.0 / pps;
        if (view_start < 0) view_start = 0;
    }
}

static void playhead_to(long f)
{
    LOCK();
    P.playhead = f < 0 ? 0 : f;
    UNLOCK();
    view_show(P.playhead);
}

static void playhead_step(long unit, int dir)  /* to the previous/next multiple of unit */
{
    long ph = P.playhead, k = ph / unit;
    if (dir > 0) playhead_to((k + 1) * unit);
    else playhead_to(ph % unit ? k * unit : (k - 1) * unit);
}

static void playhead_to_edge(int dir)   /* the next/previous clip start or end, on any track */
{
    long ph = P.playhead, best = dir > 0 ? -1 : -1;
    for (int i = 0; i < P.nclips; i++) {
        long e[2] = {P.clips[i].start, P.clips[i].start + P.clips[i].length};
        for (int j = 0; j < 2; j++) {
            if (dir > 0 && e[j] > ph && (best < 0 || e[j] < best)) best = e[j];
            if (dir < 0 && e[j] < ph && (best < 0 || e[j] > best)) best = e[j];
        }
    }
    if (best >= 0) playhead_to(best);
    else if (dir < 0) playhead_to(0);
}

static void zoom_to_fit(void)
{
    long end = project_end(&P);
    if (end <= 0) end = 8 * beat_frames();
    pps = (L.tl_w - 40) / ((double)end / SR);
    if (pps < 8) pps = 8;
    if (pps > 4000) pps = 4000;
    view_start = 0;
}


static void playhead_to(long f);

static void transport_stop(void)         /* stop and go back to where play began; again: to the start */
{
    LOCK();
    int was = P.playing;
    P.playing = 0;
    countin_left = 0;
    P.playhead = was ? play_from : 0;
    UNLOCK();
}

static void toggle_loop(void)
{
    LOCK();
    if (P.loop_end <= P.loop_start) {   /* no region yet: loop the first two bars */
        P.loop_start = 0;
        P.loop_end = 8 * beat_frames();
    }
    P.loop = !P.loop;
    UNLOCK();
}

static void import_file(const char *path)
{
    char err[200];
    int idx = sample_import(&P, path, err, sizeof err);
    if (idx < 0) {
        status("Could not import %s: %s", path, err);
    } else {
        status("Imported %s (%.2f s)", P.samples[idx].name, (double)P.samples[idx].frames / SR);
        lib_scroll = P.nsamples > 6 ? P.nsamples - 6 : 0;
    }
}

/* export dialog state: options persist for the session */
static ExportOptions EXO;
static int export_open, export_pending;
static char export_dir[900];            /* where it goes: kept for the session once chosen */
static char export_name[256];           /* the file name, without the extension */
static char export_base_path[1200];     /* export_dir/export_name, made when exporting */
static SDL_Rect export_dir_r, export_name_r, export_go_r;   /* where they were drawn, for the self-test */
static char proj_path[1024];            /* "" = never saved */

/* A name typed with a format's extension picks that format, and loses the extension. */
static void export_take_ext(char *name)
{
    size_t n = strlen(name);
    for (int f = 0; f < EXP_COUNT; f++) {
        size_t e = strlen(EXPORT_FORMATS[f].ext);
        if (n > e + 1 && name[n - e - 1] == '.' && !strcasecmp(name + n - e, EXPORT_FORMATS[f].ext)) {
            name[n - e - 1] = 0;
            EXO.format = f;
            return;
        }
    }
}

static void export_make_base(void)
{
    size_t n = strlen(export_dir);
    int slash = n && (export_dir[n - 1] == '/' || export_dir[n - 1] == '\\');
    snprintf(export_base_path, sizeof export_base_path, "%s%s%s", export_dir, slash ? "" : "/",
             export_name[0] ? export_name : "untitled");
}

static void open_export(void)
{
    if (!export_dir[0]) {                   /* the first time: the Music folder */
        user_dir("music", export_dir, sizeof export_dir);
        if (!dir_writable(export_dir) && !getcwd(export_dir, sizeof export_dir))
            snprintf(export_dir, sizeof export_dir, ".");
    }
    if (proj_path[0]) {                     /* named after the project... */
        snprintf(export_name, sizeof export_name, "%.255s", path_base(proj_path));
        size_t n = strlen(export_name);
        if (n > 5 && !strcmp(export_name + n - 5, ".tdaw")) export_name[n - 5] = 0;
    } else {                                /* ...or the time, so nothing is overwritten */
        char stamp[32];
        time_t now = time(NULL);
        strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&now));
        snprintf(export_name, sizeof export_name, "tinydaw-%s", stamp);
    }
    if (P.loop_end <= P.loop_start) EXO.loop_only = 0;
    export_open = 1;
}

static void do_export_now(void)
{
    char msg[1500];
    export_make_base();
    if (project_export(&P, &EXO, export_base_path, msg, sizeof msg) == 0) status("%s", msg);
    else status("Export failed: %s", msg);
}

/* ---- a one-line text field (the export folder and file name). Click to type; Enter, Tab or a
 * click elsewhere keeps it, Esc puts it back. */
static struct {
    char *buf, orig[1024];
    size_t cap;
    int path;                           /* folders may have slashes; file names may not */
    SDL_Rect r;
} tf;

static int tf_drawn;                    /* the field being typed in was drawn last frame */

static void tf_end(int keep)
{
    if (!tf.buf) return;
    if (!keep) snprintf(tf.buf, tf.cap, "%s", tf.orig);
    tf.buf = NULL;
}

static void tf_insert(const char *t)
{
    if (!tf.buf) return;
    for (; *t; t++) {
        size_t n = strlen(tf.buf);
        unsigned char c = (unsigned char)*t;
        if (c < 32 || (!tf.path && strchr("/\\:*?\"<>|", c))) continue;
        if (n + 1 < tf.cap) {
            tf.buf[n] = (char)c;
            tf.buf[n + 1] = 0;
        }
    }
}

static int tf_key(SDL_Keysym k)         /* 1 when the field took the key */
{
    if (!tf.buf) return 0;
    size_t n = strlen(tf.buf);
    if (k.sym == SDLK_RETURN || k.sym == SDLK_KP_ENTER || k.sym == SDLK_TAB) tf_end(1);
    else if (k.sym == SDLK_ESCAPE) tf_end(0);
    else if (k.sym == SDLK_BACKSPACE && n) {
        while (n > 0 && ((unsigned char)tf.buf[n - 1] & 0xC0) == 0x80) n--;   /* a whole UTF-8 character */
        tf.buf[n ? n - 1 : 0] = 0;
    } else if ((k.mod & KMOD_CTRL) && k.sym == SDLK_v) {
        char *clip = SDL_GetClipboardText();
        if (clip) tf_insert(clip);
        SDL_free(clip);
    } else if ((k.mod & KMOD_CTRL) && (k.sym == SDLK_a || k.sym == SDLK_u)) tf.buf[0] = 0;
    return 1;
}

static void textfield(SDL_Rect r, char *buf, size_t cap, int path, const char *placeholder)
{
    int active = tf.buf == buf, hov = hit(r);
    if (active) {
        tf_drawn = 1;
        tf.r = r;
    }
    field_box(r, hov, active);
    if (active) {
        outline(r, ACCENT);
        outline((SDL_Rect){r.x + 1, r.y + 1, r.w - 2, r.h - 2}, ACCENT);
    }
    int tw = text_w(buf), room = r.w - 20;
    int tx = tw > room && active ? r.x + 10 + room - tw : r.x + 10;   /* typing at the end keeps the end in view */
    if (buf[0]) text_clip((SDL_Rect){r.x + 6, r.y, r.w - 12, r.h}, tx, r.y + (r.h - font_h) / 2, buf, TEXT);
    else if (!active) text(r.x + 10, r.y + (r.h - font_h) / 2, placeholder, DIM);
    if (active && (SDL_GetTicks() / 500) % 2 == 0) {
        int cx = tx + tw + 1;
        if (cx > r.x + r.w - 6) cx = r.x + r.w - 6;
        fill((SDL_Rect){cx, r.y + 6, 2, r.h - 12}, ACCENT);
    }
    if (hov && lpress && !active) {
        lpress = 0;
        tf_end(1);
        tf.buf = buf;
        tf.cap = cap;
        tf.path = path;
        tf.r = r;
        tf_drawn = 1;
        snprintf(tf.orig, sizeof tf.orig, "%s", buf);
        SDL_StartTextInput();
    }
}

static void set_rate(int rate);

static void zoom(double factor, int anchor_x)
{
    double t = view_start + (anchor_x - L.tl_x) / pps;
    pps *= factor;
    if (pps < 8) pps = 8;
    if (pps > 4000) pps = 4000;
    view_start = t - (anchor_x - L.tl_x) / pps;
    if (view_start < 0) view_start = 0;
}

/* ---------------------------------------------------------------- widgets */

static int button2(SDL_Rect r, const char *label, int on, int enabled)
{
    if (!enabled) {
        style_box(r, 3);
        text_center(r, label, tone(DIM, 0.6f));
        return 0;
    }
    int hov = hit(r) && drag.kind == D_NONE;
    style_box(r, on ? 2 : hov ? 1 : 0);
    text_center(r, label, btn_fg(on));
    if (hov && lpress) {
        lpress = 0;
        return 1;
    }
    return 0;
}

static int button(SDL_Rect r, const char *label, int on) { return button2(r, label, on, 1); }

/* ---- drop-down lists */

/* A field showing the current choice with an arrow. The arrow opens a list to pick from; with
 * `cycle`, clicking the rest of the field still steps to the next choice as before. The list is
 * drawn on top of everything at the end of the frame (dd_draw) and takes the next click
 * (dd_input). Long lists split into columns so they never need scrolling. */
#define DD_MAX 160
static struct {
    int open, *sel, n, cols, changed_for_set;
    int *changed_for;
    char items[DD_MAX][48];
    SDL_Rect anchor, box;
} dd;

static int dropdown(SDL_Rect r, const char *prefix, const char *const *items, int n, int *sel, int cycle)
{
    if (*sel < 0 || *sel >= n) *sel = 0;
    char label[120];
    snprintf(label, sizeof label, "%s%s", prefix, items[*sel]);
    SDL_Rect arrow = {r.x + r.w - 22, r.y, 22, r.h}, body = {r.x, r.y, r.w - 22, r.h};
    int is_open = dd.open && dd.sel == sel, hov = hit(r) && drag.kind == D_NONE;
    style_box(r, hov && !is_open ? 1 : 0);
    if (is_open) shape_outline(r, ACCENT, ST.shaped ? ST.radius : 0);
    if (cycle) line(arrow.x, r.y + 5, arrow.x, r.y + r.h - 5, LINE);
    text_clip(body, r.x + 8, r.y + (r.h - font_h) / 2, label, TEXT);
    for (int i = 0; i < 4; i++)
        line(arrow.x + 7 + i, r.y + r.h / 2 - 2 + i, arrow.x + 15 - i, r.y + r.h / 2 - 2 + i, is_open ? ACCENT : TEXT);
    int changed = 0;
    if (dd.changed_for == sel) {        /* picked from the list last frame */
        dd.changed_for = NULL;
        changed = 1;
    }
    if (hov && lpress) {
        lpress = 0;
        if (cycle && !hit(arrow)) {
            *sel = (*sel + 1) % n;
            changed = 1;
        } else if (is_open) {
            dd.open = 0;
        } else {
            dd.open = 1;
            dd.sel = sel;
            dd.n = n < DD_MAX ? n : DD_MAX;
            for (int i = 0; i < dd.n; i++) snprintf(dd.items[i], sizeof dd.items[i], "%s", items[i]);
            dd.anchor = r;
        }
    }
    return changed;
}

static void dd_layout(void)
{
    int iw = 0, ih = 26;
    for (int i = 0; i < dd.n; i++) {
        int tw = text_w(dd.items[i]) + 24;
        if (tw > iw) iw = tw;
    }
    int max_rows = (H - L.top - 40) / ih;
    dd.cols = (dd.n + max_rows - 1) / max_rows;
    if (dd.n > 15 && dd.cols < 2) dd.cols = 2;           /* long lists: two short columns */
    if (dd.cols < 1) dd.cols = 1;
    if (iw * dd.cols < dd.anchor.w) iw = dd.anchor.w / dd.cols;   /* at least as wide as the field */
    int rows = (dd.n + dd.cols - 1) / dd.cols;
    dd.box = (SDL_Rect){dd.anchor.x, dd.anchor.y + dd.anchor.h + 2, iw * dd.cols + 8, rows * ih + 8};
    if (dd.box.y + dd.box.h > H - 4) dd.box.y = dd.anchor.y - dd.box.h - 2;   /* no room below: above */
    if (dd.box.y < 4) dd.box.y = 4;
    if (dd.box.x + dd.box.w > W - 4) dd.box.x = W - 4 - dd.box.w;
    if (dd.box.x < 4) dd.box.x = 4;
}

static SDL_Rect dd_item(int i)
{
    int rows = (dd.n + dd.cols - 1) / dd.cols, iw = (dd.box.w - 8) / dd.cols;
    return (SDL_Rect){dd.box.x + 4 + (i / rows) * iw, dd.box.y + 4 + (i % rows) * 26, iw, 26};
}

/* before anything else sees the click: a click on the open list picks, anywhere else closes it */
static void dd_input(void)
{
    if (!dd.open) return;
    dd_layout();
    if (lpress || rpress) {
        if (lpress && hit(dd.box)) {
            for (int i = 0; i < dd.n; i++)
                if (hit(dd_item(i))) {
                    *dd.sel = i;
                    dd.changed_for = dd.sel;
                }
            dd.open = 0;
            lpress = rpress = 0;
        } else if (!hit(dd.anchor)) {
            dd.open = 0;
            lpress = rpress = 0;
        }
    }
    if (hit(dd.box)) wheel_y = 0;
}

static void dd_draw(void)
{
    if (!dd.open) return;
    dd_layout();
    fill((SDL_Rect){dd.box.x + 3, dd.box.y + 4, dd.box.w, dd.box.h}, alpha(DARK, 90));   /* shadow */
    fill(dd.box, PANEL);
    outline(dd.box, ACCENT);
    for (int i = 0; i < dd.n; i++) {
        SDL_Rect r = dd_item(i);
        int cur = *dd.sel == i;
        if (cur) fill(r, tone(ACCENT, 0.45f));
        else if (hit(r)) fill(r, PANEL3);
        text_clip(r, r.x + 8, r.y + (r.h - font_h) / 2, dd.items[i], TEXT);
    }
}

/* a dropdown of the tracks, by number and name */
static int track_dropdown(SDL_Rect r, const char *prefix, int *track)
{
    static char names[MAX_TRACKS][48];
    static const char *ptrs[MAX_TRACKS];
    for (int t = 0; t < P.ntracks; t++) {
        snprintf(names[t], sizeof names[t], "%d  %s", t + 1, P.tracks[t].name);
        ptrs[t] = names[t];
    }
    if (*track >= P.ntracks) *track = P.ntracks - 1;
    return dropdown(r, prefix, ptrs, P.ntracks, track, 0);
}

/* A row of sub-tabs: one category's pages. Returns the y below it. */
static int subtabs(int x, int y, int w, const char *const *names, int n, int *sel)
{
    int gap = 4, bw = (w - gap * (n - 1)) / n;
    for (int i = 0; i < n; i++)
        if (button((SDL_Rect){x + i * (bw + gap), y, i == n - 1 ? w - i * (bw + gap) : bw, 28}, names[i], *sel == i))
            *sel = i;
    return y + 40;
}

/* A scrollbar down the right of `area` for content_h pixels of content: a handle to drag, a
 * track to click for a page at a time, and shading at the edges whenever there's more above or
 * below, with a "more" marker at the bottom. */
static void scrollbar(SDL_Rect area, int *scroll, int content_h)
{
    int max = content_h - area.h;
    if (max <= 0) {
        *scroll = 0;
        return;
    }
    if (*scroll > max) *scroll = max;
    int sw = 8, shade_h = 22;
    SDL_Rect trk = {area.x + area.w - sw - 3, area.y + 3, sw, area.h - 6};
    int th = trk.h * area.h / content_h;
    if (th < 30) th = 30;
    int ty = trk.y + (int)((long)(trk.h - th) * *scroll / max);
    SDL_Rect thumb = {trk.x, ty, sw, th};
    for (int i = 0; i < shade_h; i++) {          /* soft edges: there's more this way */
        Uint8 a = (Uint8)(150 * (shade_h - i) / shade_h);
        if (*scroll > 0) line(area.x, area.y + i, trk.x - 3, area.y + i, alpha(DARK, a));
        if (*scroll < max) line(area.x, area.y + area.h - 1 - i, trk.x - 3, area.y + area.h - 1 - i, alpha(DARK, a));
    }
    if (*scroll == 0 && !hit(area)) {            /* there's more: shown until you reach in or scroll */
        const char *more = "more ▾";
        int mw = text_w(more) + 12;
        SDL_Rect pill = {trk.x - mw - 6, area.y + area.h - font_h - 8, mw, font_h + 4};
        fill(pill, alpha(ACCENT, 200));
        text_center(pill, more, DARK);
    }
    int dragging = drag.kind == D_SCROLL && drag.inum == scroll;
    int hov = drag.kind == D_NONE && hit((SDL_Rect){trk.x - 3, trk.y, trk.w + 6, trk.h});
    fill(trk, alpha(PANEL3, 140));
    fill(thumb, dragging ? ACCENT : hov && hit(thumb) ? tone(DIM, 1.1f) : tone(DIM, 0.75f));
    if (hov && lpress) {
        if (hit((SDL_Rect){thumb.x - 3, thumb.y, thumb.w + 6, thumb.h})) {
            drag = (typeof(drag)){.kind = D_SCROLL, .inum = scroll, .iorig = *scroll, .imax = max, .sy = my,
                                  .nstep = (double)max / (trk.h - th > 1 ? trk.h - th : 1)};
        } else {                                 /* click above or below the handle: a page */
            *scroll += (my < ty ? -1 : 1) * area.h * 9 / 10;
            *scroll = *scroll < 0 ? 0 : *scroll > max ? max : *scroll;
        }
        lpress = 0;
    }
}

static void number_box(SDL_Rect r, const char *label, const char *value, int active)
{
    int hov = hit(r) && (drag.kind == D_NONE || active);
    field_box(r, hov, active);
    text(r.x + 8, r.y + (r.h - font_h) / 2, label, DIM);
    text(r.x + r.w - 8 - text_w(value), r.y + (r.h - font_h) / 2, value, TEXT);
}

/* Typing a value: a click on a number box (one that doesn't drag) opens it for typing.
 * Enter or a click elsewhere sets it, Esc leaves it as it was. The value is handed back to the
 * widget the next time it's drawn, so float settings go through their usual proxy. */
static struct {
    int active, pending;
    double *v, lo, hi, value;
    char buf[24];
    SDL_Rect r;
    int *iv;                            /* typing a note (note_number) instead of a number */
} numedit;

static void numedit_begin(double *v, double lo, double hi)
{
    numedit.active = 1;
    numedit.pending = 0;
    numedit.v = v;
    numedit.iv = NULL;
    numedit.lo = lo;
    numedit.hi = hi;
    numedit.buf[0] = 0;                  /* start empty: just type the new value */
    SDL_StartTextInput();
    status("Type a value (%g to %g), Enter to set it, Esc to cancel", lo, hi);
}

/* A typed note: a name like C4, F#3, Bb2 or Eb (octave 4 when left out), or a MIDI number.
 * Returns -1 when it isn't one. */
static int parse_note(const char *s)
{
    while (*s == ' ') s++;
    char *end;
    if (isdigit((unsigned char)*s)) {
        long v = strtol(s, &end, 10);
        return *end ? -1 : (int)v;
    }
    static const int PC[7] = {9, 11, 0, 2, 4, 5, 7};    /* A B C D E F G */
    char c = (char)toupper((unsigned char)*s);
    if (c < 'A' || c > 'G') return -1;
    int pc = PC[c - 'A'];
    s++;
    while (*s == '#' || *s == 'b' || *s == 's') pc += *s++ == 'b' ? -1 : 1;
    int oct = 4;
    if (*s) {
        long o = strtol(s, &end, 10);
        if (*end || end == s) return -1;
        oct = (int)o;
    }
    return (oct + 1) * 12 + pc;
}

static void numedit_end(int commit)
{
    if (!numedit.active) return;
    numedit.active = 0;
    if (numedit.iv) {                   /* a note */
        int v = parse_note(numedit.buf);
        if (commit && numedit.buf[0] && v >= 0) {
            numedit.value = v < numedit.lo ? numedit.lo : v > numedit.hi ? numedit.hi : v;
            numedit.pending = 1;
        } else if (commit && numedit.buf[0]) {
            status("\"%s\" isn't a note: try C4, F#3 or Bb2", numedit.buf);
        }
        return;
    }
    char *end;
    double v = strtod(numedit.buf, &end);
    if (commit && numedit.buf[0] && end != numedit.buf) {
        numedit.value = v < numedit.lo ? numedit.lo : v > numedit.hi ? numedit.hi : v;
        numedit.pending = 1;
    }
}

static void numedit_text(const char *t)
{
    if (!numedit.active) return;
    for (; *t; t++) {
        size_t n = strlen(numedit.buf);
        const char *ok = numedit.iv ? "0123456789-ABCDEFGabcdefg#" : "0123456789.-";
        if (*t && strchr(ok, *t) && n + 1 < sizeof numedit.buf) {
            numedit.buf[n] = *t;
            numedit.buf[n + 1] = 0;
        }
    }
}

static int numedit_key(SDL_Keysym k)     /* returns 1 when typing took the key */
{
    if (!numedit.active) return 0;
    size_t n = strlen(numedit.buf);
    if (k.sym == SDLK_RETURN || k.sym == SDLK_KP_ENTER || k.sym == SDLK_TAB) numedit_end(1);
    else if (k.sym == SDLK_ESCAPE) numedit_end(0);
    else if (k.sym == SDLK_BACKSPACE && n) numedit.buf[n - 1] = 0;
    return 1;
}

/* while typing into v: the text and a caret instead of the value; afterwards, the typed value */
static int numedit_widget(SDL_Rect r, const char *label, double *v)
{
    if (numedit.pending && numedit.v == v) {
        *v = numedit.value;
        numedit.pending = 0;
    }
    if (!numedit.active || numedit.v != v) return 0;
    numedit.r = r;
    fill(r, PANEL);
    outline(r, ACCENT);
    outline((SDL_Rect){r.x + 1, r.y + 1, r.w - 2, r.h - 2}, ACCENT);
    text(r.x + 8, r.y + (r.h - font_h) / 2, label, DIM);
    int tw = text_w(numedit.buf), tx = r.x + r.w - 10 - tw;
    text(tx, r.y + (r.h - font_h) / 2, numedit.buf, TEXT);
    if ((SDL_GetTicks() / 500) % 2 == 0) fill((SDL_Rect){tx + tw + 1, r.y + 6, 2, r.h - 12}, ACCENT);
    return 1;
}

/* drag sideways or up/down to change; mouse wheel steps; Shift = fine; click to type */
/* like number(), but shows `shown` instead of formatting the value (log scales, L/R pan...) */
static void number_text(SDL_Rect r, const char *label, double *v, double lo, double hi, double step,
                        const char *shown)
{
    if (numedit_widget(r, label, v)) return;
    number_box(r, label, shown, drag.kind == D_NUM && drag.num == v);
    if (drag.kind == D_NONE && hit(r)) {
        if (lpress) {
            drag = (typeof(drag)){.kind = D_NUM, .num = v, .nmin = lo, .nmax = hi, .nstep = step,
                                  .norig = *v, .sx = mx, .sy = my};
            lpress = 0;
        }
        if (wheel_y) {
            *v += wheel_y * step * 5;
            *v = *v < lo ? lo : *v > hi ? hi : *v;
            wheel_y = 0;
        }
    }
}

/* Project settings are floats; the number widgets edit doubles. A proxy double per float, kept
 * in step with it except while it's being dragged; fcommit writes changes back under the lock. */
static void pan_text(float pan, char *buf, size_t n)
{
    int k = (int)lroundf(pan * 100);
    if (k == 0) snprintf(buf, n, "centre");
    else snprintf(buf, n, "%s %d", k < 0 ? "L" : "R", abs(k));
}

static double *fproxy(float *f)
{
    static struct { float *f; double d; } pool[128];
    static int next;
    int k = -1;
    for (int i = 0; i < 128 && k < 0; i++)
        if (pool[i].f == f) k = i;
    if (k < 0) {
        k = next;
        next = (next + 1) % 128;
        pool[k].f = f;
        pool[k].d = *f;
    }
    if (!(drag.kind == D_NUM && drag.num == &pool[k].d)) pool[k].d = *f;
    return &pool[k].d;
}

static void fcommit(float *f, const double *d)
{
    if ((float)*d != *f) {
        LOCK();
        *f = (float)*d;
        UNLOCK();
    }
}

static void fnumber(SDL_Rect r, const char *label, float *f, double lo, double hi, double step, const char *fmt)
{
    double *d = fproxy(f);
    char buf[48];
    snprintf(buf, sizeof buf, fmt, *d);
    number_text(r, label, d, lo, hi, step, buf);
    fcommit(f, d);
}

static void number(SDL_Rect r, const char *label, double *v, double lo, double hi, double step,
                   const char *fmt)
{
    char buf[48];
    if (numedit_widget(r, label, v)) return;
    snprintf(buf, sizeof buf, fmt, *v);
    number_box(r, label, buf, drag.kind == D_NUM && drag.num == v);
    if (drag.kind == D_NONE && hit(r)) {
        if (lpress) {
            drag = (typeof(drag)){.kind = D_NUM, .num = v, .nmin = lo, .nmax = hi, .nstep = step,
                                  .norig = *v, .sx = mx, .sy = my};
            lpress = 0;
        }
        if (wheel_y) {
            *v += wheel_y * step * 5;
            *v = *v < lo ? lo : *v > hi ? hi : *v;
            wheel_y = 0;
        }
    }
}

static void note_number(SDL_Rect r, const char *label, int *v)
{
    char buf[16];
    if (numedit.pending && numedit.iv == v) {
        *v = (int)numedit.value;
        numedit.pending = 0;
        numedit.iv = NULL;
    }
    if (numedit.active && numedit.iv == v) {     /* being typed into */
        numedit.r = r;
        fill(r, PANEL);
        outline(r, ACCENT);
        outline((SDL_Rect){r.x + 1, r.y + 1, r.w - 2, r.h - 2}, ACCENT);
        text(r.x + 8, r.y + (r.h - font_h) / 2, label, DIM);
        int tw = text_w(numedit.buf), tx = r.x + r.w - 10 - tw;
        text(tx, r.y + (r.h - font_h) / 2, numedit.buf, TEXT);
        if ((SDL_GetTicks() / 500) % 2 == 0) fill((SDL_Rect){tx + tw + 1, r.y + 6, 2, r.h - 12}, ACCENT);
        return;
    }
    note_name(*v, buf, sizeof buf);
    number_box(r, label, buf, drag.kind == D_NOTE);
    if (drag.kind == D_NONE && hit(r)) {
        if (lpress) {
            drag = (typeof(drag)){.kind = D_NOTE, .inum = v, .iorig = *v, .imin = 12, .imax = 108,
                                  .sx = mx, .sy = my};
            lpress = 0;
        }
        if (wheel_y) {
            *v += wheel_y;
            *v = *v < 12 ? 12 : *v > 108 ? 108 : *v;
            wheel_y = 0;
        }
    }
}

/* ---------------------------------------------------------------- undo and redo */

/* Undo works on snapshots of everything an edit can change. Instead of every edit having to
 * remember to record itself, the state is compared with the last snapshot after each frame
 * (once no drag is in progress): any difference becomes one undo step. A whole drag, a placed
 * beat or a recorded take is therefore a single step. Library sounds are never removed, so
 * sample indices in old snapshots stay valid. */
typedef struct {
    Clip *clips;
    int nclips;
    Note *notes;
    int nnotes;
    AutoPoint *autos;
    int nautos;
    Track tracks[MAX_TRACKS];
    int ntracks;
    double bpm, a4;
    long loop_start, loop_end;
    Pad pads[NPADS];
    int inst_sample, inst_root, inst_track;
    MasterFx fx;
    unsigned char removed[MAX_SAMPLES];  /* sounds taken out of the library */
    char label[48];
} State;

#define UNDO_MAX 300
static State undo_stack[UNDO_MAX], redo_stack[UNDO_MAX];
static int nundo, nredo;
static State committed;                 /* the state as of the last check */
static State saved;                     /* the state when last saved or opened */
static int saved_nsamples, edit_serial;
static int pads_rec;                    /* recording pad hits: a take is one undo step */

static void state_free(State *s)
{
    free(s->clips);
    free(s->notes);
    free(s->autos);
    s->clips = NULL;
    s->notes = NULL;
    s->autos = NULL;
    s->nclips = s->nnotes = s->nautos = 0;
}

static void *dup_mem(const void *src, size_t n)   /* malloc'd copy; never NULL for n = 0 */
{
    void *d = malloc(n ? n : 1);
    if (d && n) memcpy(d, src, n);
    return d;
}

static void state_capture(State *s)
{
    free(s->clips);
    s->clips = malloc((size_t)(P.nclips > 0 ? P.nclips : 1) * sizeof(Clip));
    if (s->clips) memcpy(s->clips, P.clips, (size_t)P.nclips * sizeof(Clip));
    s->nclips = s->clips ? P.nclips : 0;
    free(s->notes);
    free(s->autos);
    s->notes = dup_mem(P.notes, (size_t)P.nnotes * sizeof(Note));
    s->nnotes = s->notes ? P.nnotes : 0;
    s->autos = dup_mem(P.autos, (size_t)P.nautos * sizeof(AutoPoint));
    s->nautos = s->autos ? P.nautos : 0;
    memcpy(s->tracks, P.tracks, sizeof s->tracks);
    s->ntracks = P.ntracks;
    s->bpm = P.bpm;
    s->a4 = P.a4;
    s->loop_start = P.loop_start;
    s->loop_end = P.loop_end;
    memcpy(s->pads, P.pads, sizeof s->pads);
    s->inst_sample = P.inst_sample;
    s->inst_root = P.inst_root;
    s->inst_track = P.inst_track;
    s->fx = P.fx;
    for (int i = 0; i < MAX_SAMPLES; i++)   /* bit 1: removed, bit 2: listed */
        s->removed[i] = i < P.nsamples ? (P.samples[i].removed != 0) | (P.samples[i].listed != 0) << 1 : 0;
    s->label[0] = 0;
}

static int removed_same(const State *s)
{
    for (int i = 0; i < P.nsamples; i++)
        if (s->removed[i] != ((P.samples[i].removed != 0) | (P.samples[i].listed != 0) << 1)) return 0;
    return 1;
}

static int state_same(const State *s)
{
    return s->nclips == P.nclips && !memcmp(s->clips, P.clips, (size_t)P.nclips * sizeof(Clip)) &&
           s->nnotes == P.nnotes && !memcmp(s->notes, P.notes, (size_t)P.nnotes * sizeof(Note)) &&
           s->nautos == P.nautos && !memcmp(s->autos, P.autos, (size_t)P.nautos * sizeof(AutoPoint)) &&
           s->ntracks == P.ntracks && !memcmp(s->tracks, P.tracks, sizeof s->tracks) && s->bpm == P.bpm &&
           s->a4 == P.a4 && s->loop_start == P.loop_start && s->loop_end == P.loop_end &&
           !memcmp(s->pads, P.pads, sizeof s->pads) && s->inst_sample == P.inst_sample &&
           s->inst_root == P.inst_root && s->inst_track == P.inst_track && !memcmp(&s->fx, &P.fx, sizeof s->fx) &&
           removed_same(s);
}

static void state_restore(const State *s)
{
    LOCK();
    memcpy(P.clips, s->clips, (size_t)s->nclips * sizeof(Clip));
    P.nclips = s->nclips;
    memcpy(P.notes, s->notes, (size_t)s->nnotes * sizeof(Note));
    P.nnotes = s->nnotes;
    memcpy(P.autos, s->autos, (size_t)s->nautos * sizeof(AutoPoint));
    P.nautos = s->nautos;
    memcpy(P.tracks, s->tracks, sizeof s->tracks);
    P.ntracks = s->ntracks;
    P.bpm = s->bpm;
    P.a4 = s->a4;
    P.loop_start = s->loop_start;
    P.loop_end = s->loop_end;
    memcpy(P.pads, s->pads, sizeof s->pads);
    P.inst_sample = s->inst_sample;
    P.inst_root = s->inst_root;
    P.inst_track = s->inst_track;
    P.fx = s->fx;
    for (int i = 0; i < P.nsamples; i++) {
        P.samples[i].removed = s->removed[i] & 1;
        P.samples[i].listed = s->removed[i] >> 1 & 1;
    }
    UNLOCK();
}

/* What changed between a snapshot and the project now, in a few words. */
static void describe(const State *s, char *buf, size_t n)
{
    if (!removed_same(s)) {
        int back = 0;                       /* a sound came (back) into the library, or went */
        for (int i = 0; i < P.nsamples; i++)
            back |= ((s->removed[i] & 1) && !P.samples[i].removed) || (!(s->removed[i] & 2) && P.samples[i].listed);
        snprintf(buf, n, back ? "paste a sound" : "remove a sound");
    }
    else if (s->nnotes != P.nnotes || memcmp(s->notes, P.notes, (size_t)P.nnotes * sizeof(Note)))
        snprintf(buf, n, "%s", P.nnotes > s->nnotes ? "add notes" : P.nnotes < s->nnotes ? "delete notes" : "note edit");
    else if (s->nautos != P.nautos || memcmp(s->autos, P.autos, (size_t)P.nautos * sizeof(AutoPoint)))
        snprintf(buf, n, "automation");
    else if (P.nclips > s->nclips) snprintf(buf, n, "add %d clip%s", P.nclips - s->nclips, P.nclips - s->nclips == 1 ? "" : "s");
    else if (P.nclips < s->nclips) snprintf(buf, n, "delete %d clip%s", s->nclips - P.nclips, s->nclips - P.nclips == 1 ? "" : "s");
    else if (memcmp(s->clips, P.clips, (size_t)P.nclips * sizeof(Clip))) snprintf(buf, n, "clip edit");
    else if (s->ntracks != P.ntracks) snprintf(buf, n, "add track");
    else if (memcmp(s->tracks, P.tracks, sizeof s->tracks)) snprintf(buf, n, "track settings");
    else if (memcmp(&s->fx, &P.fx, sizeof s->fx)) snprintf(buf, n, "effects");
    else if (s->bpm != P.bpm) snprintf(buf, n, "tempo %.0f BPM", s->bpm);
    else if (s->loop_start != P.loop_start || s->loop_end != P.loop_end) snprintf(buf, n, "loop region");
    else if (memcmp(s->pads, P.pads, sizeof s->pads) || s->inst_sample != P.inst_sample || s->inst_root != P.inst_root)
        snprintf(buf, n, "pad settings");
    else snprintf(buf, n, "tuning");
}

static void stack_push(State *stack, int *n, State s)
{
    if (*n == UNDO_MAX) {                   /* forget the oldest step */
        state_free(&stack[0]);
        memmove(stack, stack + 1, (UNDO_MAX - 1) * sizeof *stack);
        (*n)--;
    }
    stack[(*n)++] = s;
}

static void history_check(void)
{
    if (state_same(&committed)) return;
    State before = committed;
    describe(&before, before.label, sizeof before.label);
    stack_push(undo_stack, &nundo, before);
    while (nredo > 0) state_free(&redo_stack[--nredo]);
    memset(&committed, 0, sizeof committed);
    state_capture(&committed);
    edit_serial++;
}

static void history_reset(void)
{
    while (nundo > 0) state_free(&undo_stack[--nundo]);
    while (nredo > 0) state_free(&redo_stack[--nredo]);
    state_capture(&committed);
}

static void undo_redo(int redo)
{
    tempo_from = 0;                         /* a tempo edit in progress is overtaken */
    State *from = redo ? redo_stack : undo_stack, *to = redo ? undo_stack : redo_stack;
    int *nfrom = redo ? &nredo : &nundo, *nto = redo ? &nundo : &nredo;
    if (drag.kind != D_NONE) return;
    history_check();                        /* an edit made this frame becomes its own step first */
    if (*nfrom == 0) {
        status(redo ? "Nothing to redo" : "Nothing to undo");
        return;
    }
    State target = from[--*nfrom];
    State current = committed;              /* committed == the project now */
    snprintf(current.label, sizeof current.label, "%s", target.label);
    stack_push(to, nto, current);
    state_restore(&target);
    committed = target;
    committed.label[0] = 0;
    sel_clear();
    memset(nsel, 0, sizeof nsel);
    edit_serial++;
    status("%s: %s   (%d more to undo, %d to redo)", redo ? "Redo" : "Undo", target.label, nundo, nredo);
}

/* After a project rate change every snapshot's positions are rescaled like the project's. */
static void state_scale(State *s, double ratio)
{
    for (int c = 0; c < s->nclips; c++) {
        Clip *cl = &s->clips[c];
        long frames = P.samples[cl->sample].frames;
        cl->start = llround(cl->start * ratio);
        cl->offset = llround(cl->offset * ratio);
        cl->length = llround(cl->length * ratio);
        if (cl->pattern > 0) continue;
        if (cl->offset > frames - 1) cl->offset = frames - 1;
        if (cl->offset + cl->length > frames) cl->length = frames - cl->offset;
        if (cl->length < 1) cl->length = 1;
    }
    for (int i = 0; i < s->nnotes; i++) {
        s->notes[i].start = llround(s->notes[i].start * ratio);
        s->notes[i].length = llround(s->notes[i].length * ratio);
        if (s->notes[i].length < 1) s->notes[i].length = 1;
    }
    for (int i = 0; i < s->nautos; i++) s->autos[i].at = llround(s->autos[i].at * ratio);
    s->loop_start = llround(s->loop_start * ratio);
    s->loop_end = llround(s->loop_end * ratio);
}

static void history_scale(double ratio)
{
    for (int i = 0; i < nundo; i++) state_scale(&undo_stack[i], ratio);
    for (int i = 0; i < nredo; i++) state_scale(&redo_stack[i], ratio);
    state_scale(&committed, ratio);
    state_scale(&saved, ratio);
}

/* ---------------------------------------------------------------- project files */

static int recovered;                   /* opened from the autosave: unsaved by definition */
static int autosaved_serial = -1;
static Uint32 last_autosave;

enum { ACT_NONE, ACT_NEW, ACT_OPEN, ACT_OPEN_PATH, ACT_QUIT, ACT_RECOVER, ACT_OPEN_EXAMPLE };
static int confirm_act;                 /* an action waiting on "save changes?" */
static int after_save_act;              /* an action to run once a Save As completes */
static char act_path[1024];
static int menu_open, quit_now;

static void mark_saved(void)
{
    state_capture(&saved);
    saved_nsamples = P.nsamples;
    recovered = 0;
}

static int is_dirty(void)
{
    return recovered || P.nsamples != saved_nsamples || !state_same(&saved);
}

static const char *base_name(const char *path) { return path_base(path); }

static const char *dirname_of(const char *path)
{
    static char buf[1024];
    snprintf(buf, sizeof buf, "%s", path);
    char *base = (char *)path_base(buf);
    if (base == buf) snprintf(buf, sizeof buf, ".");
    else if (base == buf + 1) buf[1] = 0;      /* "/file" -> "/" */
    else base[-1] = 0;
    return buf;
}

static void ensure_dir(const char *path) { make_dirs(path); }

static void projects_dir(char *buf, size_t n)
{
    char music[900];
    user_dir("music", music, sizeof music);
    snprintf(buf, n, "%.880s/tinydaw", music);
    ensure_dir(buf);
}

static const char *autosave_file(void)
{
    static char path[1024];
    if (!path[0]) {
        char dir[900];
        user_dir("data", dir, sizeof dir);
        ensure_dir(dir);
        snprintf(path, sizeof path, "%s/autosave.tdaw", dir);
    }
    return path;
}

static int autosave_exists(void) { return file_exists(autosave_file()); }

static void update_title(void)
{
    static char last[1100];
    char t[1100];
    snprintf(t, sizeof t, "%s%s - tinydaw " TINYDAW_VERSION, is_dirty() ? "* " : "",
             proj_path[0] ? base_name(proj_path) : recovered ? "recovered work" : "untitled");
    if (strcmp(t, last)) {
        snprintf(last, sizeof last, "%s", t);
        SDL_SetWindowTitle(win, t);
    }
}

static int save_to(const char *path)
{
    char msg[1200];
    if (project_save(&P, path, msg, sizeof msg) != 0) {
        status("Save failed: %s", msg);
        return -1;
    }
    char *full = full_path(path);
    snprintf(proj_path, sizeof proj_path, "%s", full ? full : path);
    free(full);
    mark_saved();
    remove(autosave_file());            /* everything is in the real file now */
    status("%s", msg);
    return 0;
}

static void open_audio(void);
static void play_scratch(float *buf, long frames);

/* Replaces the project with `np` (which it takes), freeing the old one. */
static void replace_project(Project *np)
{
    play_scratch(NULL, 0);
    Project *old = malloc(sizeof *old);
    if (!old) return;
    LOCK();
    memcpy(old, &P, sizeof P);
    memcpy(&P, np, sizeof P);
    UNLOCK();
    project_free(old);
    free(old);
    free(np);
    sel_clear();
    tempo_from = 0;
    view_start = 0;
    lib_scroll = 0;
    drag.kind = D_NONE;
    history_reset();
}

static int open_project(const char *path, int as_recovery)
{
    Project *np = malloc(sizeof *np);
    char msg[1200];
    int old_rate = g_rate;
    if (!np) return -1;
    if (project_load(np, path, msg, sizeof msg) != 0) {
        free(np);
        status("Could not open: %s", msg);
        return -1;
    }
    replace_project(np);
    if (g_rate != old_rate) open_audio();
    mark_saved();
    if (as_recovery) {
        proj_path[0] = 0;
        recovered = 1;
        status("Recovered your unsaved work. Save it with Ctrl+S.");
    } else {
        char *full = full_path(path);
        snprintf(proj_path, sizeof proj_path, "%s", full ? full : path);
        free(full);
        status("%s", msg);
    }
    return 0;
}

static void new_project(void)
{
    Project *np = malloc(sizeof *np);
    if (!np) return;
    project_init(np);
    replace_project(np);
    proj_path[0] = 0;
    mark_saved();
    remove(autosave_file());
    status("New project");
}

static void autosave_tick(void)
{
    Uint32 now = SDL_GetTicks();
    int serial = edit_serial + P.nsamples * 100000;
    if (now - last_autosave < 30000 || serial == autosaved_serial || !is_dirty()) return;
    char msg[1200];
    last_autosave = now;
    if (project_save(&P, autosave_file(), msg, sizeof msg) == 0) autosaved_serial = serial;
}

/* ---- the system file dialog (zenity or kdialog), run on a thread so the window stays live */

static SDL_atomic_t fd_state;           /* 0 idle, 1 open, 2 finished */
static int fd_saving;
static int fd_export;                   /* choosing where an export goes (else a project) */
static char fd_result[1024];
#ifndef _WIN32
static char fd_cmd[4000];
#endif

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <SDL_syswm.h>

static char fd_start[2100];
static HWND fd_owner;

/* The standard Windows Open/Save dialog. Paths go in and out as UTF-8. */
static void win_file_dialog(void)
{
    wchar_t file[1024] = L"", dir[1024] = L"";
    char d[2100], name[512];
    snprintf(d, sizeof d, "%s", fd_start);
    for (char *c = d; *c; c++)
        if (*c == '/') *c = '\\';
    char *b = (char *)path_base(d);
    snprintf(name, sizeof name, "%s", b);
    if (b > d) b[-1] = 0;
    else d[0] = 0;
    MultiByteToWideChar(CP_UTF8, 0, name, -1, file, 1024);
    MultiByteToWideChar(CP_UTF8, 0, d, -1, dir, 1024);
    OPENFILENAMEW o;
    memset(&o, 0, sizeof o);
    o.lStructSize = sizeof o;
    o.hwndOwner = fd_owner;
    wchar_t filter[256], ext[16];
    char f8[256];
    const char *e8 = fd_export ? EXPORT_FORMATS[EXO.format].ext : "tdaw";
    if (fd_export) snprintf(f8, sizeof f8, "%s audio (*.%s)|*.%s|All files|*.*|", EXPORT_FORMATS[EXO.format].name, e8, e8);
    else snprintf(f8, sizeof f8, "tinydaw projects (*.tdaw)|*.tdaw|All files|*.*|");
    int fl = MultiByteToWideChar(CP_UTF8, 0, f8, -1, filter, 255);
    for (int i = 0; i < fl; i++)
        if (filter[i] == L'|') filter[i] = 0;     /* the list is NUL-separated, ending in two */
    MultiByteToWideChar(CP_UTF8, 0, e8, -1, ext, 16);
    o.lpstrFilter = filter;
    o.lpstrFile = file;
    o.nMaxFile = 1024;
    o.lpstrInitialDir = dir[0] ? dir : NULL;
    o.lpstrDefExt = ext;
    o.lpstrTitle = fd_export ? L"Export to" : fd_saving ? L"Save project" : L"Open project";
    o.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST |
              (fd_saving ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    BOOL ok = fd_saving ? GetSaveFileNameW(&o) : GetOpenFileNameW(&o);
    fd_result[0] = 0;
    if (ok) WideCharToMultiByte(CP_UTF8, 0, file, -1, fd_result, sizeof fd_result, NULL, NULL);
}
#else
static int have_program(const char *name)
{
    const char *path = getenv("PATH");
    char buf[1200];
    if (!path) path = "/usr/bin:/bin";
    while (*path) {
        size_t n = strcspn(path, ":");
        snprintf(buf, sizeof buf, "%.*s/%s", (int)n, path, name);
        if (access(buf, X_OK) == 0) return 1;
        path += n + (path[n] == ':');
    }
    return 0;
}

static void shell_quote(char *out, size_t n, const char *s)
{
    size_t k = 0;
    if (k < n - 1) out[k++] = '\'';
    for (; *s && k < n - 5; s++) {
        if (*s == '\'') {
            memcpy(out + k, "'\\''", 4);
            k += 4;
        } else {
            out[k++] = *s;
        }
    }
    out[k++] = '\'';
    out[k] = 0;
}

#endif

static void run_action(int act);

static int fd_thread(void *arg)
{
    (void)arg;
    fd_result[0] = 0;
#ifdef _WIN32
    win_file_dialog();
#else
    FILE *pp = popen(fd_cmd, "r");
    if (pp) {
        if (!fgets(fd_result, sizeof fd_result, pp)) fd_result[0] = 0;
        pclose(pp);
    }
#endif
    fd_result[strcspn(fd_result, "\r\n")] = 0;
    SDL_AtomicSet(&fd_state, 2);
    return 0;
}

static void file_dialog(int saving)
{
    if (SDL_AtomicGet(&fd_state) != 0) return;
    char dir[900], start[2100], q[2400];
    projects_dir(dir, sizeof dir);
    if (saving)
        snprintf(start, sizeof start, "%s/%s", proj_path[0] ? dirname_of(proj_path) : dir,
                 proj_path[0] ? base_name(proj_path) : "untitled.tdaw");
    else
        snprintf(start, sizeof start, "%s/", proj_path[0] ? dirname_of(proj_path) : dir);
    fd_saving = saving;
    fd_export = 0;
#ifdef _WIN32
    (void)q;
    snprintf(fd_start, sizeof fd_start, "%s", start);
    SDL_SysWMinfo wm;
    SDL_VERSION(&wm.version);
    fd_owner = SDL_GetWindowWMInfo(win, &wm) ? wm.info.win.window : NULL;
    if (0) {
#else
    shell_quote(q, sizeof q, start);
    if (have_program("zenity")) {
        snprintf(fd_cmd, sizeof fd_cmd,
                 "zenity --file-selection %s --title=%s --filename=%s "
                 "--file-filter='tinydaw projects (*.tdaw) | *.tdaw' --file-filter='All files | *' 2>/dev/null",
                 saving ? "--save" : "", saving ? "'Save project'" : "'Open project'", q);
    } else if (have_program("kdialog")) {
        snprintf(fd_cmd, sizeof fd_cmd, "kdialog %s %s 'tinydaw projects (*.tdaw)' 2>/dev/null",
                 saving ? "--getsavefilename" : "--getopenfilename", q);
    } else if (saving) {                     /* no dialog program: save with a dated name */
        char stamp[32], path[1100];
        time_t now = time(NULL);
        strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&now));
        snprintf(path, sizeof path, "%s/project-%s.tdaw", dir, stamp);
        int act = after_save_act;
        after_save_act = ACT_NONE;
        if (save_to(path) == 0 && act) run_action(act);   /* carry on with what the save was for */
        return;
    } else {
        status("No file dialog available (install zenity): drop a .tdaw file on the window, "
               "or run tinydaw file.tdaw");
        return;
#endif
    }
    SDL_AtomicSet(&fd_state, 1);
    SDL_Thread *t = SDL_CreateThread(fd_thread, "filedialog", NULL);
    if (t) SDL_DetachThread(t);
    else SDL_AtomicSet(&fd_state, 0);
}

/* The system Save dialog for an export: starts at the folder and name in the dialog, and
 * hands back both (file_dialog_poll). Without a dialog program the fields can still be typed in. */
static void export_file_dialog(void)
{
    if (SDL_AtomicGet(&fd_state) != 0) return;
    const ExportFormat *f = &EXPORT_FORMATS[EXO.format];
    char start[1300];
    export_make_base();
    snprintf(start, sizeof start, "%s.%s", export_base_path, f->ext);
    fd_saving = 1;
#ifdef _WIN32
    snprintf(fd_start, sizeof fd_start, "%s", start);
    SDL_SysWMinfo wm;
    SDL_VERSION(&wm.version);
    fd_owner = SDL_GetWindowWMInfo(win, &wm) ? wm.info.win.window : NULL;
#else
    char q[2700];
    shell_quote(q, sizeof q, start);
    if (have_program("zenity")) {
        snprintf(fd_cmd, sizeof fd_cmd,
                 "zenity --file-selection --save --confirm-overwrite --title='Export to' --filename=%s "
                 "--file-filter='%s audio (*.%s) | *.%s' --file-filter='All files | *' 2>/dev/null",
                 q, f->name, f->ext, f->ext);
    } else if (have_program("kdialog")) {
        snprintf(fd_cmd, sizeof fd_cmd, "kdialog --getsavefilename %s '*.%s' 2>/dev/null", q, f->ext);
    } else {
        status("No file dialog available (install zenity): type the folder and name instead");
        return;
    }
#endif
    fd_export = 1;
    SDL_AtomicSet(&fd_state, 1);
    SDL_Thread *t = SDL_CreateThread(fd_thread, "filedialog", NULL);
    if (t) SDL_DetachThread(t);
    else {
        SDL_AtomicSet(&fd_state, 0);
        fd_export = 0;
    }
}

/* a chosen export file: its folder and name go into the dialog (its extension picks the format) */
static void export_take_path(const char *path)
{
    char name[512];
    snprintf(name, sizeof name, "%s", path_base(path));
    export_take_ext(name);
    snprintf(export_name, sizeof export_name, "%.255s", name);
    snprintf(export_dir, sizeof export_dir, "%.899s", dirname_of(path));
}

static void file_dialog_poll(void)
{
    if (SDL_AtomicGet(&fd_state) != 2) return;
    SDL_AtomicSet(&fd_state, 0);
    if (fd_export) {                        /* Export > Choose: back to the export dialog */
        fd_export = 0;
        if (fd_result[0]) export_take_path(fd_result);
        return;
    }
    if (!fd_result[0]) {                    /* cancelled */
        after_save_act = ACT_NONE;
        return;
    }
    if (fd_saving) {
        char path[1100];
        size_t n = strlen(fd_result);
        snprintf(path, sizeof path, "%s%s", fd_result, n > 5 && !strcmp(fd_result + n - 5, ".tdaw") ? "" : ".tdaw");
        int act = after_save_act;
        after_save_act = ACT_NONE;
        if (save_to(path) == 0 && act) run_action(act);
    } else {
        open_project(fd_result, 0);
    }
}

static void save_project(int as)
{
    if (!as && proj_path[0]) save_to(proj_path);
    else file_dialog(1);
}

/* Runs an action that would lose unsaved changes, asking first when there are some. */
static void request(int act, const char *path)
{
    if (path) snprintf(act_path, sizeof act_path, "%s", path);
    if (is_dirty()) confirm_act = act;
    else run_action(act);
}

static void run_action(int act)
{
    confirm_act = ACT_NONE;
    switch (act) {
    case ACT_NEW: new_project(); break;
    case ACT_OPEN: file_dialog(0); break;
    case ACT_OPEN_PATH: open_project(act_path, 0); break;
    case ACT_RECOVER: open_project(autosave_file(), 1); break;
    case ACT_OPEN_EXAMPLE:              /* an untitled copy: Save asks where, so the example stays as it is */
        if (open_project(act_path, 0) == 0) {
            char title[200];
            snprintf(title, sizeof title, "%.190s", base_name(act_path));
            proj_path[0] = 0;
            mark_saved();
            status("Opened the example %s. Press Space to play; Save keeps your own copy.", title);
        }
        break;
    case ACT_QUIT: quit_now = 1; break;
    }
}

/* ---------------------------------------------------------------- pads and live input */

/* Sixteen pads play library sounds from the computer keyboard (while the Pads pane is open),
 * game controllers and other USB HID controllers (SDL joysticks), and MIDI devices. Which key,
 * button or note triggers which pad is a property of your hardware, not of a song, so the
 * bindings live in ~/.config/tinydaw/pads.conf; the sounds on the pads are saved in the
 * project. MIDI notes that are not bound to a pad play the "instrument" sound, pitched. */

enum { SRC_KEY, SRC_JOY, SRC_MIDI, SRC_MOUSE };
typedef struct { int key, joy, midi; } Bind;     /* -1 = none */

static Bind binds[NPADS];
static int learn_pad = -1, learned = -1, binds_changed;
static int inst_midi = 1;               /* MIDI notes not on a pad play the instrument */
static Uint32 pad_flash[NPADS], key_flash, joy_flash[8], midi_flash[MIDI_MAX_DEVICES];
static int midi_ok;
static int mouse_pad = -1;

/* joysticks: SDL instance ids, plus per-axis and per-hat state to turn them into buttons */
#define MAX_JOYS 8
static SDL_Joystick *joys[MAX_JOYS];
static int njoys, joy_gc[MAX_JOYS];
static Sint16 axis_rest[MAX_JOYS][16];
static Uint8 axis_on[MAX_JOYS][16], hat_bits[MAX_JOYS][4];
/* A stick can bend pitch like a MIDI pitch wheel: which one (JB_*), and each controller's raw
 * axis numbers for left x, left y, right x, right y (from SDL's controller mapping). */
enum { JB_OFF, JB_LEFT_Y, JB_RIGHT_Y, JB_LEFT_X, JB_RIGHT_X, JB_COUNT };
static const char *const JB_NAMES[JB_COUNT] = {"Off", "Left stick up/down", "Right stick up/down",
                                               "Left stick left/right", "Right stick left/right"};
static int joy_bend = JB_RIGHT_Y;
static int stick_axis[MAX_JOYS][4];
static SDL_GameController *joy_ctl[MAX_JOYS];

/* hits waiting to be written onto the timeline (filled under the audio lock, from any thread) */
typedef struct { int pad, note, down; long frame; } RecEv;
static RecEv recq[512];
static int nrecq;

static void binds_default(void)
{
    static const SDL_Scancode keys[NPADS] = {
        SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4,
        SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_E, SDL_SCANCODE_R,
        SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_F,
        SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_C, SDL_SCANCODE_V};
    for (int i = 0; i < NPADS; i++)
        binds[i] = (Bind){keys[i], i < 12 ? i : 100 + (i - 12), 36 + i};   /* buttons, then hat 1 directions */
}

static const char *binds_file(void)
{
    static char path[1024];
    if (!path[0]) {
        char dir[900];
        user_dir("config", dir, sizeof dir);
        ensure_dir(dir);
        snprintf(path, sizeof path, "%s/pads.conf", dir);
    }
    return path;
}

static void binds_load(void)
{
    binds_default();
    FILE *f = fopen(binds_file(), "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        int i, k, j, m, v;
        if (sscanf(line, "pad %d key %d joy %d midi %d", &i, &k, &j, &m) == 4 && i >= 0 && i < NPADS)
            binds[i] = (Bind){k, j, m};
        else if (sscanf(line, "joy_bend %d", &v) == 1 && v >= 0 && v < JB_COUNT)
            joy_bend = v;
        else if (sscanf(line, "instrument_midi %d", &v) == 1)
            inst_midi = v != 0;
    }
    fclose(f);
}

static void binds_save(void)
{
    FILE *f = fopen(binds_file(), "w");
    if (!f) return;
    fprintf(f, "# tinydaw pad inputs: SDL scancode, controller input, MIDI note (-1 = none)\n");
    for (int i = 0; i < NPADS; i++)
        fprintf(f, "pad %d key %d joy %d midi %d\n", i, binds[i].key, binds[i].joy, binds[i].midi);
    fprintf(f, "instrument_midi %d\n", inst_midi);
    fprintf(f, "joy_bend %d\n", joy_bend);
    fclose(f);
}

static void joy_label(int code, char *buf, size_t n)
{
    static const char *dirs[4] = {"up", "right", "down", "left"};
    if (code < 0) snprintf(buf, n, "-");
    else if (code < 100) snprintf(buf, n, "button %d", code);
    else if (code < 200) snprintf(buf, n, "hat%s %s", (code - 100) / 4 ? "2" : "", dirs[(code - 100) % 4]);
    else snprintf(buf, n, "axis %d%c", (code - 200) / 2, (code - 200) % 2 ? '-' : '+');
}

static void midi_label(int code, char *buf, size_t n)
{
    if (code < 0) snprintf(buf, n, "-");
    else if (code >= MIDI_CC) snprintf(buf, n, "CC %d", code - MIDI_CC);
    else note_name(code, buf, n);
}

static void bind_label(int pad, char *buf, size_t n)   /* short, for the pad itself */
{
    static const char *arrows[4] = {"up", "rt", "dn", "lt"};
    char a[32], b[32];
    int j = binds[pad].joy;
    if (j < 100) snprintf(a, sizeof a, "btn %d", j);
    else if (j < 200) snprintf(a, sizeof a, "hat %s", arrows[(j - 100) % 4]);
    else snprintf(a, sizeof a, "ax%d%c", (j - 200) / 2, (j - 200) % 2 ? '-' : '+');
    midi_label(binds[pad].midi, b, sizeof b);
    snprintf(buf, n, "%s  %s", binds[pad].joy >= 0 ? a : "", binds[pad].midi >= 0 ? b : "");
}

static void rec_push(int pad, int note, int down)
{
    if (!pads_rec || !P.playing || countin_left > 0 || nrecq >= (int)(sizeof recq / sizeof *recq)) return;
    recq[nrecq++] = (RecEv){pad, note, down, P.playhead};
}

static void pad_hit(int pad, int vel, int down)
{
    Pad *pd = &P.pads[pad];
    if (down) pad_flash[pad] = SDL_GetTicks();
    if (pd->sample < 0 || pd->sample >= P.nsamples) return;
    float gain = pd->volume * (float)vel / 127.0f;
    double step = pow(2.0, pd->pitch / 12.0);
    switch (pd->mode) {
    case PAD_ONESHOT:
        if (!down) return;
        voice_stop(&P, pad, -1);            /* choke: a new hit cuts the last one off */
        voice_start(&P, pd->sample, step, gain, 0, pad, -1);
        rec_push(pad, -1, 1);
        break;
    case PAD_HOLD:
        voice_stop(&P, pad, -1);
        if (down) voice_start(&P, pd->sample, step, gain, 0, pad, -1);
        rec_push(pad, -1, down);
        break;
    case PAD_LOOP:                          /* press to start, press again to stop */
        if (!down) return;
        if (voice_playing(&P, pad)) {
            voice_stop(&P, pad, -1);
            rec_push(pad, -1, 0);
        } else {
            voice_start(&P, pd->sample, step, gain, 1, pad, -1);
            rec_push(pad, -1, 1);
        }
        break;
    }
}

static void inst_note(int note, int vel, int down)
{
    voice_stop(&P, -1, note);
    if (!down) {
        rec_push(-1, note, 0);
        return;
    }
    voice_start(&P, P.inst_sample, pow(2.0, (note - P.inst_root) / 12.0), 0.9f * (float)vel / 127.0f, 0, -1, note);
    rec_push(-1, note, 1);
}

/* Every live input ends up here, from the UI thread or the MIDI thread. */
static void input_event(int src, int code, int vel, int down, int device)
{
    static int debug = -1;
    if (debug < 0) debug = getenv("TINYDAW_DEBUG_INPUT") != NULL;
    if (debug) fprintf(stderr, "input %s code %d vel %d %s device %d\n",
                       (const char *[]){"key", "controller", "midi", "mouse"}[src], code, vel, down ? "down" : "up", device);
    LOCK();
    Uint32 now = SDL_GetTicks();
    if (src == SRC_KEY) key_flash = now;
    else if (src == SRC_JOY && device >= 0 && device < MAX_JOYS) joy_flash[device] = now;
    else if (src == SRC_MIDI && device >= 0 && device < MIDI_MAX_DEVICES) midi_flash[device] = now;

    if (learn_pad >= 0 && down && src != SRC_MOUSE) {    /* bind this input to the pad */
        for (int i = 0; i < NPADS; i++) {               /* one input triggers one pad */
            if (src == SRC_KEY && binds[i].key == code) binds[i].key = -1;
            if (src == SRC_JOY && binds[i].joy == code) binds[i].joy = -1;
            if (src == SRC_MIDI && binds[i].midi == code) binds[i].midi = -1;
        }
        if (src == SRC_KEY) binds[learn_pad].key = code;
        if (src == SRC_JOY) binds[learn_pad].joy = code;
        if (src == SRC_MIDI) binds[learn_pad].midi = code;
        learned = learn_pad;
        learn_pad = -1;
        binds_changed = 1;
        UNLOCK();
        return;
    }
    int pad = -1;
    for (int i = 0; i < NPADS && pad < 0; i++) {
        if ((src == SRC_KEY && binds[i].key == code) || (src == SRC_JOY && binds[i].joy == code) ||
            (src == SRC_MIDI && binds[i].midi == code) || (src == SRC_MOUSE && i == code))
            pad = i;
    }
    if (pad >= 0) pad_hit(pad, vel, down);
    else if (src == SRC_MIDI && code < 128 && inst_midi && P.inst_sample >= 0) inst_note(code, vel, down);
    UNLOCK();
}


static void midi_in(int code, int velocity, int device, void *ud)
{
    (void)ud;
    if (code == MIDI_BEND || code >= MIDI_CCV) {    /* continuous controls: not pads */
        LOCK();
        if (code == MIDI_BEND) {
            float b = (velocity - 8192) / 8192.0f;
            P.bend = b < -1 ? -1 : b > 1 ? 1 : b;
        } else if (code == MIDI_CCV + 1) {
            P.modwheel = velocity / 127.0f;
        }
        if (device >= 0 && device < MIDI_MAX_DEVICES) midi_flash[device] = SDL_GetTicks();
        UNLOCK();
        return;
    }
    input_event(SRC_MIDI, code, velocity ? velocity : 0, velocity > 0, device);
}

/* ---- controllers */

static int joy_index(SDL_JoystickID id)
{
    for (int i = 0; i < njoys; i++)
        if (SDL_JoystickInstanceID(joys[i]) == id) return i;
    return -1;
}

static void joy_added(int device_index)
{
    if (njoys >= MAX_JOYS) return;
    SDL_Joystick *j = SDL_JoystickOpen(device_index);
    if (!j) return;
    if (joy_index(SDL_JoystickInstanceID(j)) >= 0) return;   /* already open */
    int k = njoys++;
    joys[k] = j;
    joy_gc[k] = SDL_IsGameController(device_index);
    memset(axis_on[k], 0, sizeof axis_on[k]);
    memset(hat_bits[k], 0, sizeof hat_bits[k]);
    static const int GUESS[4] = {0, 1, 2, 3};   /* the usual layout when SDL has no mapping */
    static const SDL_GameControllerAxis GC[4] = {SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY,
                                                 SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY};
    joy_ctl[k] = joy_gc[k] ? SDL_GameControllerOpen(device_index) : NULL;
    for (int i = 0; i < 4; i++) {
        stick_axis[k][i] = GUESS[i];
        if (joy_ctl[k]) {
            SDL_GameControllerButtonBind b = SDL_GameControllerGetBindForAxis(joy_ctl[k], GC[i]);
            if (b.bindType == SDL_CONTROLLER_BINDTYPE_AXIS) stick_axis[k][i] = b.value.axis;
        }
    }
    for (int a = 0; a < 16 && a < SDL_JoystickNumAxes(j); a++) {
        Sint16 rest = 0;
        if (!SDL_JoystickGetAxisInitialState(j, a, &rest)) rest = 0;
        axis_rest[k][a] = rest;
    }
    status("Controller connected: %s", SDL_JoystickName(j));
}

static void joy_removed(SDL_JoystickID id)
{
    int k = joy_index(id);
    if (k < 0) return;
    status("Controller disconnected: %s", SDL_JoystickName(joys[k]));
    if (joy_ctl[k]) SDL_GameControllerClose(joy_ctl[k]);
    SDL_JoystickClose(joys[k]);
    for (int i = k; i < njoys - 1; i++) {
        joys[i] = joys[i + 1];
        joy_ctl[i] = joy_ctl[i + 1];
        memcpy(stick_axis[i], stick_axis[i + 1], sizeof stick_axis[i]);
        joy_gc[i] = joy_gc[i + 1];
        memcpy(axis_rest[i], axis_rest[i + 1], sizeof axis_rest[i]);
        memcpy(axis_on[i], axis_on[i + 1], sizeof axis_on[i]);
        memcpy(hat_bits[i], hat_bits[i + 1], sizeof hat_bits[i]);
    }
    njoys--;
}

/* Controller events as numbered inputs: buttons 0-99, hat directions 100+, axis pushes 200+. */
static void joy_event(const SDL_Event *e)
{
    switch (e->type) {
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP: {
        int k = joy_index(e->jbutton.which);
        if (k >= 0 && e->jbutton.button < 100)
            input_event(SRC_JOY, e->jbutton.button, 127, e->type == SDL_JOYBUTTONDOWN, k);
        break;
    }
    case SDL_JOYHATMOTION: {
        int k = joy_index(e->jhat.which), h = e->jhat.hat;
        if (k < 0 || h >= 2) break;
        static const Uint8 bit[4] = {SDL_HAT_UP, SDL_HAT_RIGHT, SDL_HAT_DOWN, SDL_HAT_LEFT};
        for (int d = 0; d < 4; d++) {
            int was = hat_bits[k][h] & bit[d], is = e->jhat.value & bit[d];
            if (!was != !is) input_event(SRC_JOY, 100 + h * 4 + d, 127, is != 0, k);
        }
        hat_bits[k][h] = e->jhat.value;
        break;
    }
    case SDL_JOYAXISMOTION: {                /* sticks and triggers act as buttons past 60% */
        int k = joy_index(e->jaxis.which), a = e->jaxis.axis;
        if (k < 0 || a >= 16) break;
        if (joy_bend != JB_OFF && a == stick_axis[k][(joy_bend == JB_LEFT_Y ? 1 : joy_bend == JB_RIGHT_Y ? 3
                                                     : joy_bend == JB_LEFT_X ? 0 : 2)]) {
            /* the bend stick: up (or right) bends up, with a small dead zone, back to centre on release */
            double v = e->jaxis.value / 32767.0;
            if (joy_bend == JB_LEFT_Y || joy_bend == JB_RIGHT_Y) v = -v;
            double m = fabs(v) < 0.12 ? 0 : (fabs(v) - 0.12) / 0.88;
            float b = (float)(v < 0 ? -m : m);
            LOCK();
            P.bend = b < -1 ? -1 : b > 1 ? 1 : b;
            UNLOCK();
            break;
        }
        int d = e->jaxis.value - axis_rest[k][a];
        int range = axis_rest[k][a] < -16000 || axis_rest[k][a] > 16000 ? 65535 : 32767;
        int on_pos = d > range * 0.6, on_neg = d < -range * 0.6;
        int off_pos = d < range * 0.4, off_neg = d > -range * 0.4;
        if (!(axis_on[k][a] & 1) && on_pos) { axis_on[k][a] |= 1; input_event(SRC_JOY, 200 + a * 2, 127, 1, k); }
        else if ((axis_on[k][a] & 1) && off_pos) { axis_on[k][a] &= ~1; input_event(SRC_JOY, 200 + a * 2, 127, 0, k); }
        if (!(axis_on[k][a] & 2) && on_neg) { axis_on[k][a] |= 2; input_event(SRC_JOY, 201 + a * 2, 127, 1, k); }
        else if ((axis_on[k][a] & 2) && off_neg) { axis_on[k][a] &= ~2; input_event(SRC_JOY, 201 + a * 2, 127, 0, k); }
        break;
    }
    }
}

/* ---- recording hits onto the timeline */

typedef struct { int on; long start; int sample; int track; int loop; } Held;
static Held held_pad[NPADS], held_note[128];

static void rec_close(Held *h, long end)
{
    if (!h->on) return;
    h->on = 0;
    long len = end - h->start;
    if (len <= 0) len = P.loop_end > h->start ? P.loop_end - h->start : SR / 10;   /* wrapped round a loop */
    long frames = P.samples[h->sample].frames;
    if (h->loop) {                          /* a looping pad: repeat the sound to fill the time */
        for (long at = 0; at < len; at += frames)
            clip_add(h->sample, h->track, h->start + at, 0, len - at < frames ? len - at : frames);
    } else {
        long l = len < frames ? len : frames;
        clip_add(h->sample, h->track, h->start, 0, l < SR / 100 ? SR / 100 : l);
    }
}


/* ---- takes: a recording from when it starts to when it stops lands on an open track (one with
 * nothing on it from the take's start on), or on a new track when there's none */
static struct {
    int on, track, hits, number;
    long start;                         /* where in the song it began */
    Uint32 since;                       /* when (for the timer) */
} take;

static void disc_small(int cx, int cy, int r, Col c)
{
    for (int dy = -r; dy <= r; dy++) {
        int dx = (int)sqrt((double)(r * r - dy * dy));
        line(cx - dx, cy + dy, cx + dx, cy + dy, c);
    }
}

static int take_track_for(int from_track)
{
    if (take.track >= 0) return take.track;
    for (int t = 0; t < P.ntracks; t++) {
        int busy = 0;
        for (int c = 0; c < P.nclips && !busy; c++)
            busy = P.clips[c].track == t && P.clips[c].start + P.clips[c].length > take.start;
        if (!busy) {
            int empty = 1;                      /* an unused track takes on the sound being played */
            for (int c = 0; c < P.nclips && empty; c++) empty = P.clips[c].track != t;
            if (empty && from_track >= 0 && from_track < P.ntracks && from_track != t) {
                LOCK();
                char name[32];
                snprintf(name, sizeof name, "%s", P.tracks[t].name);
                P.tracks[t] = P.tracks[from_track];
                snprintf(P.tracks[t].name, sizeof P.tracks[t].name, "%s", name);
                P.tracks[t].mute = P.tracks[t].solo = 0;
                UNLOCK();
            }
            return take.track = t;
        }
    }
    if (P.ntracks < MAX_TRACKS) {               /* none free: a new one, sounding the same */
        LOCK();
        int t = project_add_track(&P, NULL);
        if (t >= 0 && from_track >= 0 && from_track < P.ntracks) {
            P.tracks[t] = P.tracks[from_track];
            P.tracks[t].mute = P.tracks[t].solo = 0;
        }
        if (t >= 0) snprintf(P.tracks[t].name, sizeof P.tracks[t].name, "Take %d", take.number);
        UNLOCK();
        if (t >= 0) return take.track = t;
    }
    return from_track >= 0 && from_track < P.ntracks ? from_track : 0;   /* full: where it was playing */
}

/* each frame: a take starts once play and Rec are both on (after any count-in), and ends when
 * either stops */
static void take_check(void)
{
    int now = pads_rec && P.playing && countin_left == 0;
    if (now && !take.on) {
        take.on = 1;
        take.track = -1;
        take.hits = 0;
        take.number++;
        take.start = P.playhead;
        take.since = SDL_GetTicks();
    } else if (!now && take.on) {
        take.on = 0;
        double secs = (SDL_GetTicks() - take.since) / 1000.0;
        if (take.hits)
            status("Recorded %d hit%s in %.1f s onto track %d (%s)", take.hits, take.hits == 1 ? "" : "s", secs,
                   take.track + 1, P.tracks[take.track].name);
        else
            status("Recording stopped: nothing was played (%.1f s)", secs);
    }
}

/* Rec: arms recording, and starts playing (after the count-in, if there is one) when stopped. */
static void rec_toggle(void)
{
    pads_rec = !pads_rec;
    if (pads_rec && !P.playing) toggle_play();
    if (pads_rec)
        status(countin_left > 0 ? "Recording after the count-in: play the pads or a MIDI keyboard"
                                : "Recording: play the pads or a MIDI keyboard; Stop or Rec ends the take");
    else if (!take.on)
        status("Recording off");
}

/* the take's own clock beside the transport: red while recording */
static void rec_timer(SDL_Rect r)
{
    char buf[32];
    if (take.on) {
        double t = (SDL_GetTicks() - take.since) / 1000.0;
        snprintf(buf, sizeof buf, "REC %d:%04.1f", (int)(t / 60), fmod(t, 60));
        fill(r, rgb(190, 45, 40));
        if ((SDL_GetTicks() / 500) % 2 == 0) disc_small(r.x + 10, r.y + r.h / 2, 4, rgb(255, 255, 255));
        text_center((SDL_Rect){r.x + 14, r.y, r.w - 14, r.h}, buf, rgb(255, 255, 255));
    } else if (pads_rec) {
        outline(r, rgb(200, 60, 55));
        text_center(r, P.playing && countin_left > 0 ? "count-in..." : "REC armed", rgb(220, 70, 60));
    }
}

static void rec_drain(void)
{
    RecEv evs[512];
    int n;
    LOCK();
    n = nrecq;
    memcpy(evs, recq, (size_t)n * sizeof *evs);
    nrecq = 0;
    UNLOCK();
    for (int i = 0; i < n; i++) {
        RecEv *e = &evs[i];
        if (e->pad >= 0) {
            Pad *pd = &P.pads[e->pad];
            Held *h = &held_pad[e->pad];
            if (!e->down) {
                rec_close(h, e->frame);
                continue;
            }
            if (pd->sample < 0) continue;
            LOCK();
            int s = sample_pitched(&P, pd->sample, pd->pitch);
            UNLOCK();
            if (s < 0) continue;
            int track = take_track_for(pd->track < P.ntracks ? pd->track : P.ntracks - 1);
            take.hits++;
            rec_close(h, e->frame);
            if (pd->mode == PAD_ONESHOT) clip_add(s, track, snap(e->frame), 0, P.samples[s].frames);
            else *h = (Held){1, snap(e->frame), s, track, pd->mode == PAD_LOOP};
        } else if (e->note >= 0 && e->note < 128) {
            Held *h = &held_note[e->note];
            if (!e->down) {
                rec_close(h, e->frame);
                continue;
            }
            if (P.inst_sample < 0) continue;
            LOCK();
            int s = sample_pitched(&P, P.inst_sample, e->note - P.inst_root);
            UNLOCK();
            if (s < 0) continue;
            rec_close(h, e->frame);
            take.hits++;
            *h = (Held){1, snap(e->frame), s, take_track_for(P.inst_track < P.ntracks ? P.inst_track : 0), 0};
        }
    }
    if (!P.playing || !pads_rec) {          /* stopped: finish anything still held */
        for (int i = 0; i < NPADS; i++) rec_close(&held_pad[i], P.playhead);
        for (int i = 0; i < 128; i++) rec_close(&held_note[i], P.playhead);
    }
}

/* ---- the Pads pane */

static int pad_sel, pads_scroll, pads_content_h;
static SDL_Rect pad_rects[NPADS], inst_drop;
enum { CM_LIB, CM_PAD, CM_INST };       /* what a right-click menu is for */
static void cm_open(int kind, int idx);

static const char *MODE_NAMES[PAD_MODES] = {"One-shot", "Hold", "Loop"};

static void pad_assign(int pad, int sample)
{
    LOCK();
    P.pads[pad].sample = sample;
    UNLOCK();
    pad_sel = pad;
    status("Pad %d plays \"%s\"", pad + 1, P.samples[sample].name);
}

/* Library drops onto a pad or the instrument slot. Returns 1 when it was one. */
static int pads_drop(int sample)
{
    if (!pads_open) return 0;
    for (int i = 0; i < NPADS; i++) {
        if (hit(pad_rects[i])) {
            pad_assign(i, sample);
            return 1;
        }
    }
    if (hit(inst_drop)) {
        LOCK();
        P.inst_sample = sample;
        UNLOCK();
        status("MIDI keys play \"%s\" (unpitched at %s)", P.samples[sample].name, "the root note");
        return 1;
    }
    return 0;
}

static void track_picker(int x, int y, const char *label, int *track)
{
    text(x, y + 6, label, DIM);
    int lx = x + text_w(label) + 8;
    track_dropdown((SDL_Rect){lx, y, 160, 26}, "", track);
}

static void device_row(int x, int y, int w, const char *name, Uint32 flash, Col c)
{
    Uint32 age = SDL_GetTicks() - flash;
    Col dot = flash && age < 200 ? HEAD : shade(c, 0.9f);
    fill((SDL_Rect){x, y + 4, 9, 9}, dot);
    text_clip((SDL_Rect){x + 16, y, w - 16, font_h + 2}, x + 16, y, name, TEXT);
}

static int devices_section(int x, int y, int w)
{
    char buf[160];
    text(x, y, "INPUT DEVICES", DIM);
    y += font_h + 8;
    device_row(x, y, w, "Computer keyboard (plays pads while the Pads panel is open)", key_flash, ACCENT);
    y += font_h + 6;
    for (int i = 0; i < njoys; i++) {
        snprintf(buf, sizeof buf, "%s%s", SDL_JoystickName(joys[i]),
                 joy_gc[i] ? " (gamepad)" : " (controller)");
        device_row(x, y, w, buf, joy_flash[i], TRACK_COL[5]);
        y += font_h + 6;
    }
    char names[MIDI_MAX_DEVICES][64];
    int nm = midi_ok ? midi_devices(names, MIDI_MAX_DEVICES) : 0;
    for (int i = 0; i < nm; i++) {
        snprintf(buf, sizeof buf, "MIDI: %.63s", names[i]);
        device_row(x, y, w, buf, midi_flash[i], TRACK_COL[3]);
        y += font_h + 6;
    }
    if (!njoys) {
        text(x + 16, y, "No controllers: plug in a gamepad or USB controller", DIM);
        y += font_h + 6;
    }
    if (!nm) {
        snprintf(buf, sizeof buf, midi_ok ? "No MIDI devices: plug in a keyboard or pad controller" : "MIDI off: %s",
                 midi_error());
        text_clip((SDL_Rect){x + 16, y, w - 16, font_h + 2}, x + 16, y, buf, DIM);
        y += font_h + 6;
    }
    return y;
}


enum { PAD_PRESET_COUNT = 6 };
static const char *const PAD_PRESETS[PAD_PRESET_COUNT] = {"Drum kit", "Drums + chords", "Chords of the key",
                                                          "Scale notes", "Bass notes", "Beat loops"};
static void load_pad_preset(int k);
static int gn_sel;

static int pads_content(int x, int y, int w)
{
    char buf[160], b2[64];
    Uint32 now = SDL_GetTicks();

    static const char *const SUBS[] = {"Pads", "Instrument"};
    y = subtabs(x, y, w, SUBS, 2, &pads_sub);
    if (pads_sub == 0) {
    char names[MIDI_MAX_DEVICES][64];
    int nm = midi_ok ? midi_devices(names, MIDI_MAX_DEVICES) : 0;
    snprintf(buf, sizeof buf, "Playing from: keyboard%s%s%s%s   (Settings > Input)", njoys ? ", " : "",
             njoys ? (njoys == 1 ? "1 controller" : "controllers") : "", nm ? ", " : "", nm ? "MIDI" : "");
    text_clip((SDL_Rect){x, y, w, font_h + 2}, x, y, buf, DIM);
    y += font_h + 10;
    y += 6;
    if (button((SDL_Rect){x, y, 76, 28}, pads_rec ? "● Rec" : "Rec", pads_rec)) rec_toggle();
    snprintf(buf, sizeof buf, "record hits while playing (snap %s)", SNAP_NAMES[snap_i]);
    text_clip((SDL_Rect){x + 86, y, w - 86, 28}, x + 86, y + (28 - font_h) / 2, buf, pads_rec ? HEAD : DIM);
    y += 40;

    {                                           /* preset layouts: fill all 16 pads at once */
        static int preset;
        if (dropdown((SDL_Rect){x, y, w - 76, 30}, "Preset: ", PAD_PRESETS, PAD_PRESET_COUNT, &preset, 0))
            load_pad_preset(preset);
        if (button((SDL_Rect){x + w - 70, y, 70, 30}, "Load", 0)) load_pad_preset(preset);   /* again, e.g. after a key change */
        y += 34;
        snprintf(buf, sizeof buf, "Uses the key in Theory (%s %s), the Beats style (%s)", pitch_name(th_root, key_uses_flats(th_root, th_scale)),
                 SCALES[th_scale].name, GENRES[gn_sel].name);
        text_clip((SDL_Rect){x, y, w, font_h + 2}, x, y, buf, DIM);
        text_clip((SDL_Rect){x, y + font_h + 2, w, font_h + 2}, x, y + font_h + 2,
                  "and the New sound wave. Ctrl+Z puts the old pads back.", DIM);
        y += 2 * font_h + 12;
    }
    text(x, y + 6, "PADS", DIM);
    if (button((SDL_Rect){x + w - 246, y, 150, 26}, "Fill from library", 0)) {
        int s = 0, filled = 0;
        LOCK();
        for (int i = 0; i < NPADS && s < P.nsamples; i++)
            if (P.pads[i].sample < 0) {
                while (s < P.nsamples && (P.samples[s].pitched || P.samples[s].removed || P.samples[s].hidden)) s++;   /* skip clip variants */
                if (s >= P.nsamples) break;
                P.pads[i].sample = s++;
                filled++;
            }
        UNLOCK();
        status(filled ? "Put %d library sounds on the empty pads" : "No empty pads, or the library is empty", filled);
    }
    if (button((SDL_Rect){x + w - 90, y, 90, 26}, "Clear all", 0)) {
        LOCK();
        voices_clear(&P);
        for (int i = 0; i < NPADS; i++) P.pads[i].sample = -1;
        UNLOCK();
    }
    y += 34;

    int gap = 6, pw = (w - 3 * gap) / 4, ph = 2 * font_h + 34;
    for (int i = 0; i < NPADS; i++) {
        SDL_Rect r = {x + (i % 4) * (pw + gap), y + (i / 4) * (ph + gap), pw, ph};
        pad_rects[i] = r;
        Pad *pd = &P.pads[i];
        int has = pd->sample >= 0 && pd->sample < P.nsamples;
        Uint32 age = now - pad_flash[i];
        float glow = pad_flash[i] && age < 250 ? 1.0f - age / 250.0f : 0;
        Col base = has ? wash(TRACK_COL[pd->track % MAX_TRACKS], 0.34f + 0.5f * glow) : tone(PANEL2, 1 + glow);
        if (has && voice_playing(&P, i) && pd->mode == PAD_LOOP) base = wash(TRACK_COL[pd->track % MAX_TRACKS], 0.6f);
        int hov = hit(r) && drag.kind == D_NONE;
        int drop = drag.kind == D_LIB && hit(r);
        fill(r, hov ? tone(base, 1.2f) : base);
        outline(r, drop ? HEAD : i == pad_sel ? ACCENT : LINE);
        if (i == pad_sel) outline((SDL_Rect){r.x + 1, r.y + 1, r.w - 2, r.h - 2}, ACCENT);
        if (learn_pad == i && (now / 300) % 2) outline((SDL_Rect){r.x + 2, r.y + 2, r.w - 4, r.h - 4}, HEAD);
        const char *kname = binds[i].key >= 0 ? SDL_GetScancodeName(binds[i].key) : "";
        text(r.x + 6, r.y + 4, kname, HEAD);
        snprintf(b2, sizeof b2, "%d", i + 1);
        text(r.x + r.w - 6 - text_w(b2), r.y + 4, b2, DIM);
        const char *nm2 = has ? P.samples[pd->sample].name : "empty";
        text_clip((SDL_Rect){r.x + 4, r.y, r.w - 8, r.h}, r.x + 6, r.y + 8 + font_h, nm2, has ? TEXT : DIM);
        bind_label(i, buf, sizeof buf);
        text_clip((SDL_Rect){r.x + 4, r.y, r.w - 8, r.h}, r.x + 6, r.y + r.h - font_h - 5, buf, DIM);
        if (hov && lpress) {                /* click: select and play */
            pad_sel = i;
            mouse_pad = i;
            input_event(SRC_MOUSE, i, 110, 1, -1);
            lpress = 0;
        }
        if (hov && rpress) {                /* right-click: copy, paste, clear */
            rpress = 0;
            pad_sel = i;
            cm_open(CM_PAD, i);
        }
    }
    y += 4 * (ph + gap) + 8;

    /* the selected pad */
    Pad *pd = &P.pads[pad_sel];
    snprintf(buf, sizeof buf, "PAD %d", pad_sel + 1);
    text(x, y, buf, DIM);
    const char *snd = pd->sample >= 0 ? P.samples[pd->sample].name : "(empty: drag a library sound onto a pad)";
    text_clip((SDL_Rect){x + 60, y, w - 60, font_h + 2}, x + 60, y, snd, pd->sample >= 0 ? TEXT : DIM);
    y += font_h + 10;
    for (int m = 0; m < PAD_MODES; m++)
        if (button((SDL_Rect){x + m * 96, y, 90, 26}, MODE_NAMES[m], pd->mode == m)) {
            LOCK();
            voice_stop(&P, pad_sel, -1);
            pd->mode = m;
            UNLOCK();
        }
    text(x + 3 * 96 + 6, y + 6, pd->mode == PAD_ONESHOT ? "plays to the end" : pd->mode == PAD_HOLD ?
         "plays while held" : "press to start/stop", DIM);
    y += 34;
    static double vol, pitch;
    if (!(drag.kind == D_NUM && drag.num == &vol)) vol = pd->volume;
    if (!(drag.kind == D_NUM && drag.num == &pitch)) pitch = pd->pitch;
    int hw = (w - 8) / 2;
    number((SDL_Rect){x, y, hw, 26}, "Volume", &vol, 0, 1.5, 0.005, "%.2f");
    number((SDL_Rect){x + hw + 8, y, hw, 26}, "Pitch", &pitch, -24, 24, 0.1, "%+.0f st");
    if ((float)vol != pd->volume || (int)lround(pitch) != pd->pitch) {
        LOCK();
        pd->volume = (float)vol;
        pd->pitch = (int)lround(pitch);
        UNLOCK();
    }
    y += 34;
    double *pp = fproxy(&pd->pan);
    char pt[24];
    pan_text((float)*pp, pt, sizeof pt);
    number_text((SDL_Rect){x, y, hw, 26}, "Pan", pp, -1, 1, 0.005, pt);
    fcommit(&pd->pan, pp);
    text(x + hw + 12, y + 6, "uses its track's effects", DIM);
    y += 34;
    track_picker(x, y, "Track", &pd->track);
    if (button((SDL_Rect){x + w - 220, y, 110, 26}, learn_pad == pad_sel ? "Press input..." : "Learn input",
               learn_pad == pad_sel))
        learn_pad = learn_pad == pad_sel ? -1 : pad_sel;
    if (button((SDL_Rect){x + w - 104, y, 104, 26}, "Clear inputs", 0)) {
        LOCK();
        binds[pad_sel] = (Bind){-1, -1, -1};
        UNLOCK();
        binds_changed = 1;
    }
    y += 34;
    char jb[32], mb[32];
    joy_label(binds[pad_sel].joy, jb, sizeof jb);
    midi_label(binds[pad_sel].midi, mb, sizeof mb);
    snprintf(buf, sizeof buf, "Inputs:  key %s   controller %s   MIDI %s",
             binds[pad_sel].key >= 0 ? SDL_GetScancodeName(binds[pad_sel].key) : "-", jb, mb);
    text_clip((SDL_Rect){x, y, w, font_h + 2}, x, y, buf, learn_pad >= 0 ? HEAD : TEXT);
    y += font_h + 4;
    text(x, y, learn_pad >= 0 ? "Press a key, button or MIDI note (Esc cancels)"
                              : "Learn input, then press a key, button or MIDI note", DIM);
    y += font_h + 18;

    } else {
    /* the instrument */
    int hw = (w - 8) / 2;
    text(x, y, "INSTRUMENT", DIM);
    y += font_h + 4;
    text(x, y, "MIDI notes that aren't on a pad play this sound, pitched", DIM);
    y += font_h + 8;
    inst_drop = (SDL_Rect){x, y, w, 30};
    if (hit(inst_drop) && rpress && drag.kind == D_NONE) {
        rpress = 0;
        cm_open(CM_INST, 0);
    }
    int idrop = drag.kind == D_LIB && hit(inst_drop);
    fill(inst_drop, idrop ? PANEL3 : PANEL2);
    outline(inst_drop, idrop ? HEAD : LINE);
    snprintf(buf, sizeof buf, "%s", P.inst_sample >= 0 ? P.samples[P.inst_sample].name : "drop a library sound here");
    text_clip((SDL_Rect){x + 4, y, w - 150, 30}, x + 10, y + (30 - font_h) / 2, buf, P.inst_sample >= 0 ? TEXT : DIM);
    if (button((SDL_Rect){x + w - 140, y + 2, 136, 26}, "Generator sound", 0)) {
        GenParams g = G;
        g.a4 = P.a4;
        if (g.wave >= WAVE_NOISE) g.wave = WAVE_TRIANGLE;
        g.note = P.inst_root;
        if (g.release_ms < 80) g.release_ms = 80;
        int s = sample_generate(&P, &g);
        if (s >= 0) {
            LOCK();
            P.inst_sample = s;
            UNLOCK();
            status("MIDI keys play the generator's %s sound", WAVE_NAMES[g.wave]);
        }
    }
    y += 38;
    note_number((SDL_Rect){x, y, hw, 26}, "Root note", &P.inst_root);
    if (button((SDL_Rect){x + hw + 8, y, hw, 26}, inst_midi ? "MIDI keys: on" : "MIDI keys: off", inst_midi)) {
        inst_midi = !inst_midi;
        binds_changed = 1;
    }
    y += 34;
    track_picker(x, y, "Track", &P.inst_track);
    if (button((SDL_Rect){x + w - 150, y, 150, 26}, "No MIDI on pads", 0)) {
        LOCK();
        for (int i = 0; i < NPADS; i++) binds[i].midi = -1;
        UNLOCK();
        binds_changed = 1;
        status("MIDI notes no longer trigger pads: every key plays the instrument");
    }
    y += 34;
    text(x, y, "Pads use MIDI notes 36-51 (C2-D#3) by default, like", DIM);
    text(x, y + font_h + 2, "most pad controllers. Playing a piano keyboard? Press", DIM);
    text(x, y + 2 * font_h + 4, "No MIDI on pads so every key plays the instrument.", DIM);
    y += 3 * font_h + 12;
    }
    return y;
}

static void pads_pane(void)
{
    SDL_Rect pane = {L.pane_x, L.top, L.pane_w, H - L.top - L.status};
    fill(pane, PANEL);
    line(pane.x + pane.w - 1, pane.y, pane.x + pane.w - 1, pane.y + pane.h, LINE);
    int inside = hit(pane), saved_press = lpress;
    if (!inside) lpress = 0;
    int max_scroll = pads_content_h - pane.h > 0 ? pads_content_h - pane.h : 0;
    pads_scroll = pads_scroll < 0 ? 0 : pads_scroll > max_scroll ? max_scroll : pads_scroll;
    SDL_RenderSetClipRect(ren, &pane);
    int y0 = pane.y + 12 - pads_scroll;
    int y = pads_content(pane.x + 14, y0, pane.w - 28);
    pads_content_h = y - y0 + 12;
    (void)max_scroll;
    scrollbar(pane, &pads_scroll, pads_content_h);
    SDL_RenderSetClipRect(ren, NULL);
    if (!inside) lpress = saved_press;
}

/* Keys go to the pads while the pane is open, unless Ctrl/Alt is held or a dialog is up. */
static int pads_take_key(SDL_Keysym k)
{
    return (pads_open || show_open) && !(k.mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI)) && !menu_open && !confirm_act &&
           !export_open;
}

/* ---- File menu and the "save changes?" question */

static SDL_Rect file_btn, met_btn, snap_rect, bpm_rect, a4_rect;
static int met_open;

static int menu_item(SDL_Rect r, const char *label, const char *shortcut, int enabled)
{
    int hov = enabled && hit(r);
    if (hov) fill(r, PANEL3);
    text(r.x + 12, r.y + (r.h - font_h) / 2, label, enabled ? TEXT : tone(DIM, 0.7f));
    if (shortcut) text(r.x + r.w - 12 - text_w(shortcut), r.y + (r.h - font_h) / 2, shortcut, DIM);
    if (hov && lpress) {
        lpress = 0;
        return 1;
    }
    return 0;
}

/* ---- the example songs, from the examples folder next to the program */

#include <dirent.h>
static char example_path[8][1024], example_title[8][96];
static int nexamples, example_pick;

static void examples_scan(void)
{
    nexamples = 0;
    char dir[1100] = "examples";
    char *base = SDL_GetBasePath();
    if (base) {
        snprintf(dir, sizeof dir, "%sexamples", base);
        SDL_free(base);
    }
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && nexamples < 8) {
        size_t n = strlen(e->d_name);
        if (n < 6 || strcmp(e->d_name + n - 5, ".tdaw")) continue;
        snprintf(example_path[nexamples], sizeof example_path[0], "%.900s/%.100s", dir, e->d_name);
        snprintf(example_title[nexamples], sizeof example_title[0], "%.*s", (int)(n - 5), e->d_name);
        FILE *f = fopen(example_path[nexamples], "r");      /* its title line, if it has one */
        char line_[200];
        for (int k = 0; f && k < 4 && fgets(line_, sizeof line_, f); k++)
            if (!strncmp(line_, "title ", 6)) {
                line_[strcspn(line_, "\r\n")] = 0;
                snprintf(example_title[nexamples], sizeof example_title[0], "%.90s", line_ + 6);
            }
        if (f) fclose(f);
        nexamples++;
    }
    closedir(d);
    for (int i = 0; i < nexamples; i++)                        /* in title order */
        for (int j = i + 1; j < nexamples; j++)
            if (strcmp(example_title[j], example_title[i]) < 0) {
                char t[1024];
                memcpy(t, example_path[i], sizeof t);
                memcpy(example_path[i], example_path[j], sizeof t);
                memcpy(example_path[j], t, sizeof t);
                char u[96];
                memcpy(u, example_title[i], sizeof u);
                memcpy(example_title[i], example_title[j], sizeof u);
                memcpy(example_title[j], u, sizeof u);
            }
}

static void examples_menu(SDL_Rect anchor)   /* the list of examples, as a drop-down */
{
    examples_scan();
    if (!nexamples) {
        status("No examples found: they belong in an examples folder next to the program");
        return;
    }
    dd.open = 1;
    dd.sel = &example_pick;
    example_pick = -1;
    dd.n = nexamples;
    for (int i = 0; i < nexamples; i++) snprintf(dd.items[i], sizeof dd.items[i], "%.46s", example_title[i]);
    dd.anchor = anchor;
}

static void file_menu(void)
{
    int w = 270, ih = 30;
    static const struct { const char *label, *keys; } ITEMS[] = {
        {"New", "Ctrl+N"}, {"Open...", "Ctrl+O"}, {"Save", "Ctrl+S"}, {"Save As...", "Ctrl+Shift+S"},
        {"Recover unsaved work", NULL}, {NULL, NULL}, {"Export audio...", "Ctrl+E"}, {NULL, NULL},
        {"Settings", "Ctrl+,"}, {"Full-screen show", "F11"}, {NULL, NULL}, {"Quit", "Ctrl+Q"},
        {NULL, NULL}, {"Open an example song", NULL}};
    int n = sizeof ITEMS / sizeof *ITEMS, hgt = 8;
    for (int i = 0; i < n; i++) hgt += ITEMS[i].label ? ih : 9;
    SDL_Rect m = {file_btn.x, file_btn.y + file_btn.h + 2, w, hgt + font_h + 12};
    fill(m, PANEL);
    outline(m, LINE);
    int y = m.y + 4, act = -1;
    for (int i = 0; i < n; i++) {
        if (!ITEMS[i].label) {
            line(m.x + 8, y + 4, m.x + w - 8, y + 4, LINE);
            y += 9;
            continue;
        }
        int enabled = i != 4 || autosave_exists();
        if (menu_item((SDL_Rect){m.x + 1, y, w - 2, ih}, ITEMS[i].label, ITEMS[i].keys, enabled)) act = i;
        y += ih;
    }
    line(m.x + 8, y + 4, m.x + w - 8, y + 4, LINE);
    text(m.x + 12, y + 9, "tinydaw " TINYDAW_VERSION, DIM);
    if (act >= 0 || lpress || rpress) menu_open = 0;   /* any click closes it */
    lpress = rpress = 0;
    switch (act) {
    case 0: request(ACT_NEW, NULL); break;
    case 1: request(ACT_OPEN, NULL); break;
    case 2: save_project(0); break;
    case 3: save_project(1); break;
    case 4: request(ACT_RECOVER, NULL); break;
    case 6: open_export(); break;
    case 8: if (!settings_open) open_panel(RI_SETTINGS); break;
    case 9: show_toggle(); break;
    case 11: request(ACT_QUIT, NULL); break;
    case 13: examples_menu((SDL_Rect){m.x + m.w - 4, y - ih - font_h, 4, 4}); break;
    }
}

/* The metronome's options and the tempo, under the arrow next to Click. */
static void metronome_menu(void)
{
    int w = 380, h = 404;
    SDL_Rect m = {met_btn.x + met_btn.w - w, met_btn.y + met_btn.h + 2, w, h};
    if (m.x < 4) m.x = 4;
    fill(m, PANEL);
    outline(m, LINE);
    int x = m.x + 14, y = m.y + 12, iw = w - 28, changed = 0;
    char buf[64];
    text(x, y, "METRONOME", DIM);
    y += font_h + 8;
    if (button((SDL_Rect){x, y, 110, 28}, MET.on ? "Click: on" : "Click: off", MET.on)) MET.on = !MET.on, changed = 1;
    double vol = MET.volume;
    number((SDL_Rect){x + 118, y, iw - 118, 28}, "Volume", &vol, 0, 1.5, 0.005, "%.2f");
    if ((float)vol != MET.volume) MET.volume = (float)vol, changed = 1;
    y += 36;
    text(x, y + 7, "Sound", DIM);
    int bw = (iw - 80 - 8) / 3;
    for (int i = 0; i < CLICK_SOUNDS; i++)
        if (button((SDL_Rect){x + 80 + i * (bw + 4), y, bw, 28}, CLICK_NAMES[i], MET.sound == i)) {
            MET.sound = i;
            metronome_prepare();
            changed = 1;
        }
    y += 34;
    text(x, y + 7, "Ticks", DIM);
    static const char *const SUBN[] = {"Beats", "8ths", "16ths"};
    static const int SUBV[] = {1, 2, 4};
    for (int i = 0; i < 3; i++)
        if (button((SDL_Rect){x + 80 + i * (bw + 4), y, bw, 28}, SUBN[i], MET.sub == SUBV[i])) MET.sub = SUBV[i], changed = 1;
    y += 34;
    if (button((SDL_Rect){x, y, iw, 28}, MET.accent ? "Louder first beat of each bar: on" : "Louder first beat of each bar: off",
               MET.accent))
        MET.accent = !MET.accent, changed = 1;
    y += 34;
    text(x, y + 7, "Count-in", DIM);
    static const char *const CIN[] = {"Off", "1 bar", "2 bars"};
    for (int i = 0; i < 3; i++)
        if (button((SDL_Rect){x + 80 + i * (bw + 4), y, bw, 28}, CIN[i], MET.countin == i)) MET.countin = i, changed = 1;
    y += 34;
    if (button((SDL_Rect){x + 80, y, iw - 80, 28}, MET.countin_rec_only ? "Only when recording (Rec)" : "Every time play starts",
               0))
        MET.countin_rec_only = !MET.countin_rec_only, changed = 1;
    y += 42;
    text(x, y, "TEMPO", DIM);
    y += font_h + 8;
    double tempo_before = P.bpm;
    number((SDL_Rect){x, y, 120, 28}, "BPM", &P.bpm, 40, 300, 0.25, "%.1f");
    static const char *const TB[] = {"-1", "+1", "/2", "x2"};
    for (int i = 0; i < 4; i++)
        if (button((SDL_Rect){x + 128 + i * 46, y, 42, 28}, TB[i], 0)) {
            double v = i == 0 ? P.bpm - 1 : i == 1 ? P.bpm + 1 : i == 2 ? P.bpm / 2 : P.bpm * 2;
            P.bpm = v < 40 ? 40 : v > 300 ? 300 : round(v * 4) / 4;
        }
    tempo_touch(tempo_before);
    y += 36;
    if (button((SDL_Rect){x, y, 150, 28}, "Tap tempo (T)", 0)) tap_tempo();
    snprintf(buf, sizeof buf, "beat %.0f ms, bar %.2f s", 60000.0 / P.bpm, 240.0 / P.bpm);
    text_clip((SDL_Rect){x + 158, y, iw - 158, 28}, x + 158, y + 7, buf, DIM);
    y += 34;
    text(x, y + 2, "M toggles the click. Clips keep their times", DIM);
    text(x, y + font_h + 4, "when the tempo changes.", DIM);
    if (changed) settings_save();
    if ((lpress || rpress) && !hit(m) && !hit(met_btn)) met_open = 0;   /* a click outside closes it */
    if (hit(m)) lpress = rpress = 0;
}

static void confirm_dialog(void)
{
    fill((SDL_Rect){0, 0, W, H}, alpha(DARK, 170));
    int w = 520, h = 150;
    SDL_Rect d = {(W - w) / 2, (H - h) / 2, w, h};
    fill(d, PANEL);
    outline(d, LINE);
    char buf[1200];
    snprintf(buf, sizeof buf, "Save changes to %s?", proj_path[0] ? base_name(proj_path) : "this project");
    text(d.x + 20, d.y + 20, buf, TEXT);
    const char *what = confirm_act == ACT_QUIT ? "quit" : confirm_act == ACT_NEW ? "start a new project"
                     : "open another project";
    snprintf(buf, sizeof buf, "Your changes will be lost if you %s without saving.", what);
    text(d.x + 20, d.y + 24 + font_h + 6, buf, DIM);
    int by = d.y + h - 50;
    int act = confirm_act;
    if (button((SDL_Rect){d.x + 20, by, 120, 32}, "Don't save", 0)) {
        if (act == ACT_QUIT) remove(autosave_file());   /* you chose to throw it away */
        run_action(act);
    }
    if (button((SDL_Rect){d.x + w - 250, by, 110, 32}, "Cancel", 0)) confirm_act = ACT_NONE;
    if (button((SDL_Rect){d.x + w - 130, by, 110, 32}, "Save", 1)) {
        confirm_act = ACT_NONE;
        if (proj_path[0]) {
            if (save_to(proj_path) == 0) run_action(act);
        } else {
            after_save_act = act;
            file_dialog(1);
        }
    }
}

static void waiting_overlay(const char *msg)
{
    fill((SDL_Rect){0, 0, W, H}, alpha(DARK, 150));
    SDL_Rect r = {(W - 360) / 2, (H - 60) / 2, 360, 60};
    fill(r, PANEL);
    outline(r, ACCENT);
    text_center(r, msg, TEXT);
}

/* ---- clip edits: pitch and reverse swap in a variant of the sound; gain and fades are numbers */

static void clip_variant(int ci, int semis, int reverse)
{
    if (ci < 0 || ci >= P.nclips) return;
    Clip *c = &P.clips[ci];
    const Sample *old = &P.samples[c->sample];
    int old_rev = old->pitched ? old->reversed : 0;
    int base = sample_base(&P, c->sample);
    int ns = sample_variant(&P, base, semis, reverse);    /* made outside the lock: can take a moment */
    if (ns < 0) {
        status("Library is full");
        return;
    }
    if (ns == c->sample) return;
    if (c->pattern > 0) {                       /* a note clip: its notes play the shifted sound */
        LOCK();
        c->sample = ns;
        UNLOCK();
        return;
    }
    double ratio = (double)P.samples[ns].frames / old->frames;
    long nf = P.samples[ns].frames;
    long off = llround(c->offset * ratio), len = llround(c->length * ratio);
    if (!old_rev != !reverse) off = nf - off - len;         /* the same stretch of sound, mirrored */
    if (off < 0) off = 0;
    if (off > nf - 1) off = nf - 1;
    if (off + len > nf) len = nf - off;
    if (len < 1) len = 1;
    LOCK();
    c->sample = ns;
    c->offset = off;
    c->length = len;
    c->fade_in = llround(c->fade_in * ratio);
    c->fade_out = llround(c->fade_out * ratio);
    if (c->fade_in + c->fade_out > len) c->fade_in = c->fade_out = 0;
    UNLOCK();
}

static int clip_semis(const Clip *c) { return P.samples[c->sample].pitched ? P.samples[c->sample].semis : 0; }
static int clip_rev(const Clip *c) { return P.samples[c->sample].pitched ? P.samples[c->sample].reversed : 0; }

static int track_has_fx(const Track *t)
{
    return t->filter || t->drive > 0 || t->eq[0] || t->eq[1] || t->eq[2] || t->delay_send > 0 ||
           t->reverb_send > 0 || t->duck > 0 || t->pan != 0;
}

/* ---- the Mixer pane */

static int mixer_scroll, mixer_content_h;

static int section(int x, int y, int w, const char *title)
{
    line(x, y + font_h / 2, x + w, y + font_h / 2, LINE);
    int tw = text_w(title) + 12;
    fill((SDL_Rect){x, y, tw, font_h + 2}, PANEL);
    text(x, y, title, DIM);
    return y + font_h + 10;
}

static int mixer_content(int x, int y, int w)
{
    char buf[160], b2[48];
    int hw = (w - 8) / 2, tw = (w - 16) / 3, rh = 34;

    static const char *const SUBS[] = {"Clip", "Track", "Effects", "Live"};
    y = subtabs(x, y, w, SUBS, 4, &mixer_sub);
    if (mixer_sub == 0) {
    /* the selected clips: the one clicked last shows its values; a change applies to all of them */
    y = section(x, y, w, "CLIP");
    if (selected >= 0 && selected < P.nclips) {
        Clip *c = &P.clips[selected];
        int many = sel_count();
        if (many > 1) snprintf(buf, sizeof buf, "%d clips selected: changes apply to all", many);
        else snprintf(buf, sizeof buf, "%s", P.samples[sample_base(&P, c->sample)].name);
        text_clip((SDL_Rect){x, y, w, font_h + 2}, x, y, buf, many > 1 ? HEAD : TEXT);
        y += font_h + 8;
        float g0 = c->gain_db;
        fnumber((SDL_Rect){x, y, hw, 26}, "Gain", &c->gain_db, -36, 12, 0.05, "%+.1f dB");
        if (c->gain_db != g0 && many > 1) {     /* move every selected clip's gain by the same amount */
            float d = c->gain_db - g0;
            LOCK();
            for (int i = 0; i < P.nclips; i++)
                if (csel[i] && i != selected) {
                    float v = P.clips[i].gain_db + d;
                    P.clips[i].gain_db = v < -36 ? -36 : v > 12 ? 12 : v;
                }
            UNLOCK();
        }
        static double semis;
        if (!(drag.kind == D_NUM && drag.num == &semis)) semis = clip_semis(c);
        number((SDL_Rect){x + hw + 8, y, hw, 26}, "Pitch", &semis, -24, 24, 0.08, "%+.0f st");
        if (drag.kind != D_NUM && (int)lround(semis) != clip_semis(c))
            for (int i = 0; i < P.nclips; i++)
                if (csel[i]) clip_variant(i, (int)lround(semis), clip_rev(&P.clips[i]));
        y += rh;
        c = &P.clips[selected];
        double fin = (double)c->fade_in / SR * 1000, fout = (double)c->fade_out / SR * 1000;
        double maxms = (double)c->length / SR * 1000;
        static double din, dout;
        if (!(drag.kind == D_NUM && drag.num == &din)) din = fin;
        if (!(drag.kind == D_NUM && drag.num == &dout)) dout = fout;
        number((SDL_Rect){x, y, tw, 26}, "In", &din, 0, maxms, 2, "%.0f ms");
        number((SDL_Rect){x + tw + 8, y, tw, 26}, "Out", &dout, 0, maxms, 2, "%.0f ms");
        if (din != fin || dout != fout) {
            LOCK();
            for (int i = 0; i < P.nclips; i++) {
                if (!csel[i]) continue;
                Clip *ci = &P.clips[i];
                long a = (long)llround(din / 1000 * SR), b = (long)llround(dout / 1000 * SR);
                if (a > ci->length) a = ci->length;
                if (a + b > ci->length) b = ci->length - a;
                if (din != fin) ci->fade_in = a;
                if (dout != fout) ci->fade_out = b < 0 ? 0 : b;
            }
            UNLOCK();
        }
        if (button((SDL_Rect){x + 2 * (tw + 8), y, tw, 26}, "Reverse", clip_rev(c))) {
            int rev = !clip_rev(c);
            for (int i = 0; i < P.nclips; i++)
                if (csel[i]) clip_variant(i, clip_semis(&P.clips[i]), rev);
        }
        y += rh;
        text(x, y, "Drag the squares on a clip's top corners to fade it", DIM);
        y += font_h + 14;
    } else {
        text(x, y, "Click a clip, or drag a box around several", DIM);
        y += font_h + 14;
    }

    }
    if (mixer_sub == 1) {
    /* the selected track */
    if (sel_track >= P.ntracks) sel_track = P.ntracks - 1;
    Track *t = &P.tracks[sel_track];
    y = section(x, y, w, "TRACK");
    fill((SDL_Rect){x, y + 4, 4, 18}, TRACK_COL[sel_track]);
    track_dropdown((SDL_Rect){x + 10, y, w - 10, 26}, "Track ", &sel_track);
    t = &P.tracks[sel_track];
    y += rh;
    fnumber((SDL_Rect){x, y, hw, 26}, "Volume", &t->volume, 0, 1.5, 0.004, "%.2f");
    double *pd = fproxy(&t->pan);
    pan_text((float)*pd, b2, sizeof b2);
    number_text((SDL_Rect){x + hw + 8, y, hw, 26}, "Pan", pd, -1, 1, 0.005, b2);
    fcommit(&t->pan, pd);
    y += rh;
    {                                           /* automation overrides these while it has points */
        char au[120] = "";
        for (int p = 0; p < AUTO_PARAMS; p++)
            if (auto_lane(&P, sel_track, p, NULL)) {
                if (au[0]) strncat(au, ", ", sizeof au - strlen(au) - 1);
                strncat(au, AUTO_NAMES[p], sizeof au - strlen(au) - 1);
            }
        if (au[0]) {
            char line_[200];
            snprintf(line_, sizeof line_, "Automated: %s (the track's A lane sets them)", au);
            text_clip((SDL_Rect){x, y, w, font_h + 4}, x, y, line_, ACCENT);
            y += font_h + 10;
        }
    }
    static const char *FNAMES[FILT_TYPES] = {"Off", "Low-pass", "High-pass", "Band-pass"};
    text(x, y + 6, "Filter", DIM);
    int fw = (w - 60 - 12) / 4;
    for (int k = 0; k < FILT_TYPES; k++)
        if (button((SDL_Rect){x + 60 + k * (fw + 4), y, fw, 26}, FNAMES[k], t->filter == k)) {
            LOCK();
            t->filter = k;
            UNLOCK();
        }
    y += rh;
    double *cp = fproxy(&t->cutoff);
    static double cpos;                         /* cutoff is dragged on a log scale */
    if (!(drag.kind == D_NUM && drag.num == &cpos)) cpos = log(*cp / 20.0) / log(1000.0);
    double hz = 20 * pow(1000.0, cpos);
    if (hz >= 1000) snprintf(b2, sizeof b2, "%.2f kHz", hz / 1000);
    else snprintf(b2, sizeof b2, "%.0f Hz", hz);
    number_text((SDL_Rect){x, y, hw, 26}, "Cutoff", &cpos, 0, 1, 0.002, b2);
    *cp = 20 * pow(1000.0, cpos);
    fcommit(&t->cutoff, cp);
    fnumber((SDL_Rect){x + hw + 8, y, hw, 26}, "Resonance", &t->reso, 0, 1, 0.004, "%.2f");
    y += rh;
    static const char *EQN[3] = {"Low", "Mid", "High"};
    for (int k = 0; k < 3; k++)
        fnumber((SDL_Rect){x + k * (tw + 8), y, tw, 26}, EQN[k], &t->eq[k], -15, 15, 0.05, "%+.1f dB");
    y += rh;
    fnumber((SDL_Rect){x, y, hw, 26}, "Drive", &t->drive, 0, 1, 0.004, "%.2f");
    snprintf(buf, sizeof buf, P.fx.sc_source >= 0 ? "Duck (by %d)" : "Duck", P.fx.sc_source + 1);
    double before = t->duck;
    fnumber((SDL_Rect){x + hw + 8, y, hw, 26}, buf, &t->duck, 0, 1, 0.004, "%.2f");
    if (before == 0 && t->duck > 0 && P.fx.sc_source < 0) {     /* pick a sidechain source: the kick */
        int src = sel_track == 0 ? 1 : 0;
        for (int k = 0; k < P.ntracks; k++)
            if (k != sel_track && (strstr(P.tracks[k].name, "Kick") || strstr(P.tracks[k].name, "kick"))) src = k;
        LOCK();
        P.fx.sc_source = src < P.ntracks ? src : -1;
        UNLOCK();
        status("Track %d now ducks this one: change it under Master effects", P.fx.sc_source + 1);
    }
    y += rh;
    fnumber((SDL_Rect){x, y, hw, 26}, "Delay send", &t->delay_send, 0, 1, 0.004, "%.2f");
    fnumber((SDL_Rect){x + hw + 8, y, hw, 26}, "Reverb send", &t->reverb_send, 0, 1, 0.004, "%.2f");
    y += rh + 8;

    }
    MasterFx *fx = &P.fx;
    if (mixer_sub == 2) {
    /* shared effects */
    y = section(x, y, w, "DELAY");
    int dw = (w - 7 * 4) / 8;
    for (int k = 0; k < DELAY_DIVS; k++)
        if (button((SDL_Rect){x + k * (dw + 4), y, dw, 26}, DELAY_NAMES[k], fx->delay_div == k)) {
            LOCK();
            fx->delay_div = k;
            UNLOCK();
        }
    y += rh;
    fnumber((SDL_Rect){x, y, tw, 26}, "Feedback", &fx->delay_feedback, 0, 0.95, 0.004, "%.2f");
    fnumber((SDL_Rect){x + tw + 8, y, tw, 26}, "Tone", &fx->delay_tone, 0, 1, 0.004, "%.2f");
    fnumber((SDL_Rect){x + 2 * (tw + 8), y, tw, 26}, "Level", &fx->delay_level, 0, 1.5, 0.004, "%.2f");
    y += rh;
    if (button((SDL_Rect){x, y, 120, 26}, "Ping-pong", fx->pingpong)) {
        LOCK();
        fx->pingpong = !fx->pingpong;
        UNLOCK();
    }
    snprintf(buf, sizeof buf, "%.0f ms at %.0f BPM", DELAY_BEATS[fx->delay_div] * 60000.0 / P.bpm, P.bpm);
    text(x + 132, y + 6, buf, DIM);
    y += rh + 8;
    y = section(x, y, w, "REVERB");
    fnumber((SDL_Rect){x, y, tw, 26}, "Size", &fx->reverb_size, 0, 1, 0.004, "%.2f");
    fnumber((SDL_Rect){x + tw + 8, y, tw, 26}, "Damping", &fx->reverb_damp, 0, 1, 0.004, "%.2f");
    fnumber((SDL_Rect){x + 2 * (tw + 8), y, tw, 26}, "Level", &fx->reverb_level, 0, 1.5, 0.004, "%.2f");
    y += rh + 8;
    y = section(x, y, w, "SIDECHAIN");
    {                                           /* off, or which track ducks the others */
        static char names[MAX_TRACKS + 1][48];
        static const char *ptrs[MAX_TRACKS + 1];
        static int pick;
        snprintf(names[0], sizeof names[0], "off");
        ptrs[0] = names[0];
        for (int k = 0; k < P.ntracks; k++) {
            snprintf(names[k + 1], sizeof names[k + 1], "%d  %s", k + 1, P.tracks[k].name);
            ptrs[k + 1] = names[k + 1];
        }
        if (!(dd.open && dd.sel == &pick)) pick = fx->sc_source + 1;
        if (dropdown((SDL_Rect){x, y, hw, 26}, "Source: ", ptrs, P.ntracks + 1, &pick, 0)) {
            LOCK();
            fx->sc_source = pick - 1;
            UNLOCK();
        }
    }
    fnumber((SDL_Rect){x + hw + 8, y, hw, 26}, "Release", &fx->sc_release, 20, 1000, 1, "%.0f ms");
    y += rh;
    text(x, y, "Tracks with Duck above 0 dip whenever the source hits", DIM);
    y += font_h + 16;

    }
    if (mixer_sub == 3) {
    y = section(x, y, w, "LIVE CONTROLS");
    fnumber((SDL_Rect){x, y, hw, 26}, "Bend range", &fx->bend_range, 1, 24, 0.05, "%.0f st");
    SDL_Rect meter = {x + hw + 8, y + 4, hw, 18};
    fill(meter, PANEL2);
    int mid = meter.x + meter.w / 2;
    int bx = mid + (int)(P.bend * meter.w / 2);
    fill((SDL_Rect){bx < mid ? bx : mid, meter.y, abs(bx - mid) + 1, meter.h}, ACCENT);
    snprintf(buf, sizeof buf, "bend %+.1f st", P.bend * fx->bend_range);
    text_center(meter, buf, TEXT);
    line(mid, meter.y - 3, mid, meter.y, DIM);
    line(mid, meter.y + meter.h, mid, meter.y + meter.h + 3, DIM);
    y += rh;
    SDL_Rect mw = {x + hw + 8, y + 4, hw, 18};
    fill(mw, PANEL2);
    fill((SDL_Rect){mw.x, mw.y, (int)(mw.w * P.modwheel), mw.h}, TRACK_COL[3]);
    text_center(mw, "mod wheel", TEXT);
    snprintf(buf, sizeof buf, "opens track %d's filter", P.inst_track + 1);
    text(x, y + 6, buf, DIM);
    y += rh;
    text(x, y, "The pitch wheel (or a controller stick: Settings > Input)", DIM);
    text(x, y + font_h + 2, "bends pads and the instrument live.", DIM);
    y += font_h + 2;
    text(x, y + font_h + 2, "Set the instrument's track (Pads pane) to a filter,", DIM);
    text(x, y + 2 * font_h + 4, "then push the mod wheel up to open it.", DIM);
    y += 3 * font_h + 12;
    }
    return y;
}

static void mixer_pane(void)
{
    SDL_Rect pane = {L.pane_x, L.top, L.pane_w, H - L.top - L.status};
    fill(pane, PANEL);
    line(pane.x + pane.w - 1, pane.y, pane.x + pane.w - 1, pane.y + pane.h, LINE);
    int inside = hit(pane), saved_press = lpress;
    if (!inside) lpress = 0;
    int max_scroll = mixer_content_h - pane.h > 0 ? mixer_content_h - pane.h : 0;
    mixer_scroll = mixer_scroll < 0 ? 0 : mixer_scroll > max_scroll ? max_scroll : mixer_scroll;
    SDL_RenderSetClipRect(ren, &pane);
    int y0 = pane.y + 12 - mixer_scroll;
    int y = mixer_content(pane.x + 14, y0, pane.w - 28);
    mixer_content_h = y - y0 + 12;
    (void)max_scroll;
    scrollbar(pane, &mixer_scroll, mixer_content_h);
    SDL_RenderSetClipRect(ren, NULL);
    if (!inside) lpress = saved_press;
}

/* ---------------------------------------------------------------- visualizer */

/* The audio callback copies everything it plays into a ring; each frame the UI takes the newest
 * VIS_FFT frames from it, plus each track's peak level from the mixer. From those come the
 * meters, the analyzer pane and the full-screen show. */
#define VIS_RING 16384
static float vis_ring[2 * VIS_RING];
static int vis_pos;                     /* written by the audio thread under its lock */
static float vis_buf[2 * VIS_FFT], vis_mono[VIS_FFT], vis_db[VIS_FFT / 2], vis_smooth[VIS_FFT / 2];

typedef struct { float level, peak; Uint32 peak_at; int clipped; } Meter;
static Meter track_meter[MAX_TRACKS], master_meter[2];
#define VIS_PI 3.14159265358979323846
static void show_toggle(void);
static float low_energy, low_avg, high_energy, beat_pulse;   /* for the show */
static Uint32 vis_last;

static float to_db(float v) { return v > 1e-6f ? 20 * log10f(v) : -120; }

/* fast rise, ~24 dB/s fall, peak held 1.5 s, sticky clip light (click to clear) */
static void meter_feed(Meter *m, float v, float dt)
{
    float fall = powf(10, -24 * dt / 20);
    m->level = v > m->level * fall ? v : m->level * fall;
    Uint32 now = SDL_GetTicks();
    if (v >= m->peak || now - m->peak_at > 1500) {
        m->peak = v;
        m->peak_at = now;
    }
    if (v > 1.0f) m->clipped = 1;
}

static void vis_update(void)
{
    Uint32 now = SDL_GetTicks();
    float dt = vis_last ? (now - vis_last) / 1000.0f : 0.016f;
    vis_last = now;
    float levels[MAX_TRACKS][2];
    LOCK();
    int pos = vis_pos;
    for (int i = 0; i < VIS_FFT; i++) {
        int k = (pos - VIS_FFT + i + VIS_RING) % VIS_RING;
        vis_buf[2 * i] = vis_ring[2 * k];
        vis_buf[2 * i + 1] = vis_ring[2 * k + 1];
    }
    mixer_levels(MX, levels);
    UNLOCK();
    float pl = 0, pr = 0;
    for (int i = VIS_FFT - 1024; i < VIS_FFT; i++) {      /* the latest ~20 ms for the master meter */
        pl = fmaxf(pl, fabsf(vis_buf[2 * i]));
        pr = fmaxf(pr, fabsf(vis_buf[2 * i + 1]));
    }
    meter_feed(&master_meter[0], pl, dt);
    meter_feed(&master_meter[1], pr, dt);
    for (int t = 0; t < MAX_TRACKS; t++) meter_feed(&track_meter[t], fmaxf(levels[t][0], levels[t][1]), dt);
    if (!vis_open && !show_open) return;
    static int smooth_ready;
    if (!smooth_ready) {                                  /* start from silence, not full scale */
        for (int k = 0; k < VIS_FFT / 2; k++) vis_smooth[k] = -140;
        smooth_ready = 1;
    }
    for (int i = 0; i < VIS_FFT; i++) vis_mono[i] = 0.5f * (vis_buf[2 * i] + vis_buf[2 * i + 1]);
    spectrum_db(vis_mono, VIS_FFT, vis_db);
    float fall = 60 * dt;                                 /* bars fall 60 dB/s */
    double bin_hz = (double)SR / VIS_FFT, lo = 0, hi = 0;
    int nlo = 0, nhi = 0;
    for (int k = 1; k < VIS_FFT / 2; k++) {
        vis_smooth[k] = vis_db[k] > vis_smooth[k] - fall ? vis_db[k] : vis_smooth[k] - fall;
        double f = k * bin_hz;
        float lin = powf(10, vis_db[k] / 20);
        if (f >= 30 && f < 150) { lo += lin * lin; nlo++; }
        if (f >= 4000 && f < 16000) { hi += lin * lin; nhi++; }
    }
    low_energy = nlo ? sqrtf((float)(lo / nlo)) * 8 : 0;
    high_energy = nhi ? sqrtf((float)(hi / nhi)) * 30 : 0;
    low_avg += (low_energy - low_avg) * fminf(1, dt * 2);
    if (low_energy > low_avg * 1.35f && low_energy > 0.05f && beat_pulse < 0.3f) beat_pulse = 1;   /* a kick */
    beat_pulse *= expf(-dt * 6);
}

static Col meter_col(float db)
{
    return db > -0.5f ? (Col){235, 70, 60, 255} : db > -6 ? (Col){240, 190, 70, 255} : (Col){110, 200, 120, 255};
}

/* A level meter, -60..0 dB, in r: vertical or horizontal. */
static void meter_draw(SDL_Rect r, const Meter *m, int vertical)
{
    fill(r, ST.shaped && theme_light ? rgb(28, 44, 78) : tone(PANEL, 0.7f));   /* dark well on bright themes */
    float db = to_db(m->level), pk = to_db(m->peak);
    float k = (db + 60) / 60, kp = (pk + 60) / 60;
    k = k < 0 ? 0 : k > 1 ? 1 : k;
    kp = kp < 0 ? 0 : kp > 1 ? 1 : kp;
    if (vertical) {
        int h = (int)(r.h * k), hp = (int)(r.h * kp);
        fill((SDL_Rect){r.x, r.y + r.h - h, r.w, h}, meter_col(db));
        if (hp > 1) fill((SDL_Rect){r.x, r.y + r.h - hp, r.w, 2}, meter_col(pk));
        if (m->clipped) fill((SDL_Rect){r.x, r.y, r.w, 3}, (Col){235, 70, 60, 255});
    } else {
        int w = (int)(r.w * k), wp = (int)(r.w * kp);
        fill((SDL_Rect){r.x, r.y, w, r.h}, meter_col(db));
        if (wp > 1) fill((SDL_Rect){r.x + wp - 2, r.y, 2, r.h}, meter_col(pk));
        if (m->clipped) fill((SDL_Rect){r.x + r.w - 4, r.y, 4, r.h}, (Col){235, 70, 60, 255});
    }
}

static double freq_x(double f, SDL_Rect r)   /* 20 Hz .. 20 kHz, logarithmic */
{
    return r.x + r.w * log(f / 20.0) / log(1000.0);
}

static void draw_spectrum(SDL_Rect r)
{
    fill(r, tone(BG, 0.9f));
    static const int grid_hz[] = {50, 100, 200, 500, 1000, 2000, 5000, 10000};
    static const char *grid_lbl[] = {"50", "100", "200", "500", "1k", "2k", "5k", "10k"};
    for (int i = 0; i < 8; i++) {
        int x = (int)freq_x(grid_hz[i], r);
        line(x, r.y, x, r.y + r.h, tone(LINE, 0.7f));
        text(x + 3, r.y + r.h - font_h - 2, grid_lbl[i], tone(DIM, 0.8f));
    }
    for (int db = -12; db >= -84; db -= 24) {
        int y = r.y + (int)(r.h * (-db) / 96.0);
        line(r.x, y, r.x + r.w, y, tone(LINE, 0.7f));
        char b[16];
        snprintf(b, sizeof b, "%d", db);
        text(r.x + 3, y + 1, b, tone(DIM, 0.8f));
    }
    double bin_hz = (double)SR / VIS_FFT;
    int prev_y = -1;
    for (int x = r.x; x < r.x + r.w; x++) {      /* each pixel column: the loudest bin under it */
        double f0 = 20 * pow(1000.0, (double)(x - r.x) / r.w), f1 = 20 * pow(1000.0, (double)(x + 1 - r.x) / r.w);
        int k0 = (int)(f0 / bin_hz), k1 = (int)(f1 / bin_hz);
        if (k1 <= k0) k1 = k0 + 1;
        float best = -140;
        for (int k = k0; k < k1 && k < VIS_FFT / 2; k++) best = fmaxf(best, vis_smooth[k]);
        if (k0 < 1) best = -140;
        float t = (best + 96) / 96;
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        int y = r.y + r.h - (int)(r.h * t);
        Col c = tone(ACCENT, 0.45f + 0.55f * t);
        line(x, y, x, r.y + r.h, alpha(c, 150));
        if (prev_y >= 0) line(x - 1, prev_y, x, y, HEAD);
        prev_y = y;
    }
    outline(r, LINE);
}

static void draw_scope(SDL_Rect r)
{
    fill(r, tone(BG, 0.9f));
    line(r.x, r.y + r.h / 2, r.x + r.w, r.y + r.h / 2, tone(LINE, 0.7f));
    int span = 2048, start = VIS_FFT - span - 1;
    for (int i = VIS_FFT - span - 1; i > VIS_FFT / 2; i--)   /* trigger on a rising zero crossing */
        if (vis_mono[i - 1] < 0 && vis_mono[i] >= 0) {
            start = i;
            break;
        }
    if (start + span > VIS_FFT) start = VIS_FFT - span;
    int py = -1;
    for (int x = 0; x < r.w; x++) {
        float v = vis_mono[start + x * span / r.w];
        v = v > 1 ? 1 : v < -1 ? -1 : v;
        int y = r.y + r.h / 2 - (int)(v * (r.h / 2 - 2));
        if (py >= 0) line(r.x + x - 1, py, r.x + x, y, TRACK_COL[4]);
        py = y;
    }
    outline(r, LINE);
}

static void draw_stereo(SDL_Rect r)              /* goniometer: mono is vertical, wide is spread out */
{
    fill(r, tone(BG, 0.9f));
    int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    line(cx, r.y, cx, r.y + r.h, tone(LINE, 0.7f));
    line(r.x, cy, r.x + r.w, cy, tone(LINE, 0.7f));
    SDL_SetRenderDrawColor(ren, TRACK_COL[6].r, TRACK_COL[6].g, TRACK_COL[6].b, 160);
    double corr_num = 0, el = 0, er = 0;
    for (int i = VIS_FFT - 1536; i < VIS_FFT; i++) {
        float L = vis_buf[2 * i], R = vis_buf[2 * i + 1];
        corr_num += L * R;
        el += L * L;
        er += R * R;
        float sx = (R - L) * 0.7071f, sy = (L + R) * 0.7071f;
        int x = cx + (int)(sx * r.w * 0.45f), y = cy - (int)(sy * r.h * 0.45f);
        if (x > r.x && x < r.x + r.w && y > r.y && y < r.y + r.h) SDL_RenderDrawPoint(ren, x, y);
    }
    double corr = el > 1e-9 && er > 1e-9 ? corr_num / sqrt(el * er) : 1;
    char b[32];
    snprintf(b, sizeof b, "corr %+.2f", corr);
    text(r.x + 4, r.y + 2, b, corr < 0 ? (Col){235, 70, 60, 255} : DIM);
    outline(r, LINE);
}

static int vis_content(int x, int y, int w)
{
    char buf[64];
    static const char *const SUBS[] = {"Analyzer", "Meters"};
    y = subtabs(x, y, w, SUBS, 2, &vis_sub);
    {                                           /* the show: which scene, cycling, how long each */
        static const char *names[SCENE_COUNT + 1];
        for (int k = 0; k <= SCENE_COUNT; k++) names[k] = show_name(k);
        if (dropdown((SDL_Rect){x, y, w - 140, 30}, "Show: ", names, SCENE_COUNT + 1, &show_scene, 0)) {
            show_started[show_scene] = show_clock;
            settings_save();
        }
        if (button((SDL_Rect){x + w - 132, y, 132, 30}, show_cycle ? "Cycle: on" : "Cycle: off", show_cycle)) {
            show_cycle = !show_cycle;
            settings_save();
        }
        y += 36;
        double each = show_each;
        number((SDL_Rect){x, y, w / 2 - 4, 28}, "Each scene", &show_each, 5, 600, 0.25, "%.0f s");
        if (show_each != each) settings_save();
        if (button((SDL_Rect){x + w / 2 + 4, y, w / 2 - 4, 28}, "Full-screen show  (F11)", 0)) show_toggle();
        y += 40;
    }
    if (vis_sub == 0) {
    text(x, y, "SPECTRUM", DIM);
    y += font_h + 6;
    draw_spectrum((SDL_Rect){x, y, w, 200});
    y += 212;
    text(x, y, "WAVEFORM", DIM);
    text(x + w / 2 + 6, y, "STEREO", DIM);
    y += font_h + 6;
    draw_scope((SDL_Rect){x, y, w / 2 - 6, 150});
    draw_stereo((SDL_Rect){x + w / 2 + 6, y, w / 2 - 6, 150});
    y += 162;
    text(x, y, "Stereo: a vertical line is mono, a wide cloud is", DIM);
    text(x, y + font_h + 2, "wide. Below zero, left and right cancel in mono.", DIM);
    y += 2 * font_h + 12;
    } else {
    text(x, y, "MASTER", DIM);
    y += font_h + 6;
    for (int c = 0; c < 2; c++) {
        SDL_Rect mr = {x + 20, y + c * 20, w - 110, 14};
        text(x, y + c * 20 - 1, c ? "R" : "L", DIM);
        meter_draw(mr, &master_meter[c], 0);
        snprintf(buf, sizeof buf, "%5.1f dB", fmaxf(-99, to_db(master_meter[c].peak)));
        text(x + w - 82, y + c * 20 - 1, buf, master_meter[c].clipped ? (Col){235, 70, 60, 255} : TEXT);
        if (hit(mr) && lpress) {
            master_meter[0].clipped = master_meter[1].clipped = 0;
            for (int t = 0; t < MAX_TRACKS; t++) track_meter[t].clipped = 0;
            lpress = 0;
        }
    }
    y += 46;
    text(x, y, "TRACKS", DIM);
    y += font_h + 6;
    for (int t = 0; t < P.ntracks; t++) {
        fill((SDL_Rect){x, y + 3, 4, 10}, TRACK_COL[t]);
        text_clip((SDL_Rect){x + 8, y, 110, font_h + 2}, x + 10, y, P.tracks[t].name, TEXT);
        meter_draw((SDL_Rect){x + 124, y + 2, w - 124, 12}, &track_meter[t], 0);
        y += 18;
    }
    y += 10;
    text(x, y, "Red at the right of a meter means it went over 0 dB.", DIM);
    text(x, y + font_h + 2, "Click a master meter to clear the clip lights.", DIM);
    y += 2 * font_h + 14;
    }
    return y;
}

static int vis_scroll, vis_content_h;

static void vis_pane(void)
{
    SDL_Rect pane = {L.pane_x, L.top, L.pane_w, H - L.top - L.status};
    fill(pane, PANEL);
    line(pane.x + pane.w - 1, pane.y, pane.x + pane.w - 1, pane.y + pane.h, LINE);
    int inside = hit(pane), saved_press = lpress;
    if (!inside) lpress = 0;
    int max_scroll = vis_content_h - pane.h > 0 ? vis_content_h - pane.h : 0;
    vis_scroll = vis_scroll < 0 ? 0 : vis_scroll > max_scroll ? max_scroll : vis_scroll;
    SDL_RenderSetClipRect(ren, &pane);
    int y0 = pane.y + 12 - vis_scroll;
    int y = vis_content(pane.x + 14, y0, pane.w - 28);
    vis_content_h = y - y0 + 12;
    scrollbar(pane, &vis_scroll, vis_content_h);
    SDL_RenderSetClipRect(ren, NULL);
    if (!inside) lpress = saved_press;
}

/* ---- the full-screen show: metaballs, one per sounding track, in its colour, orbiting in time
 * with the song; they swell on the kick, a spectrum ring pulses around them, beats send out
 * ripples. Rendered at low resolution into a texture and smoothly scaled up. */

static SDL_Texture *show_tex;
static int show_w, show_h;
static Uint32 *show_px;
static float blob_size[MAX_TRACKS], show_phase, ripple_t[6];
static int ripple_n;
static double last_beat = -1;

static void ring(int cx, int cy, int r, Col c);

static void show_overlay(const char *name, const char *caption);

static void show_blobs(void)
{
    int tw = 640, th = 640 * H / (W > 0 ? W : 1);    /* twice the old resolution */
    if (th < 180) th = 180;
    if (!show_tex || tw != show_w || th != show_h) {
        if (show_tex) SDL_DestroyTexture(show_tex);
        free(show_px);
        show_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, tw, th);
        SDL_SetTextureScaleMode(show_tex, SDL_ScaleModeLinear);
        show_px = malloc((size_t)tw * th * sizeof(Uint32));
        show_w = tw;
        show_h = th;
        if (!show_tex || !show_px) return;
    }
    float dt = 1 / 60.0f;
    /* the song's position: blobs go round once every two bars while it plays */
    double beats = (double)P.playhead / SR * P.bpm / 60.0;
    if (P.playing) {
        show_phase = (float)(beats / 8.0 * 2 * VIS_PI);
        if (floor(beats) != floor(last_beat) && ripple_n < 6) ripple_t[ripple_n++] = 0;   /* each beat */
        last_beat = beats;
    } else {
        show_phase += dt * 0.15f;
    }
    float bf = P.playing ? (float)(beats - floor(beats)) : 1;
    float pulse = fmaxf(beat_pulse, P.playing ? expf(-bf * 7) * fminf(1, low_energy * 3) : 0);

    /* blobs: active tracks in their colours */
    float bx[MAX_TRACKS + 1], by[MAX_TRACKS + 1], bs[MAX_TRACKS + 1], br[MAX_TRACKS + 1], bg[MAX_TRACKS + 1], bb[MAX_TRACKS + 1];
    int n = 0;
    float aspect = (float)tw / th;
    for (int t = 0; t < P.ntracks; t++) {
        float lv = track_meter[t].level;
        float target = lv > 0.002f ? 0.06f + 0.16f * sqrtf(fminf(lv, 1.5f)) : 0;
        blob_size[t] += (target - blob_size[t]) * (target > blob_size[t] ? 0.5f : 0.06f);
        if (blob_size[t] < 0.004f) continue;
        float a = show_phase + t * 2 * (float)VIS_PI / P.ntracks;
        float rad = 0.38f + 0.12f * sinf(show_phase * 2 + t);
        bx[n] = cosf(a) * rad * fminf(aspect, 1.9f);
        by[n] = sinf(a) * rad;
        bs[n] = blob_size[t] * (1 + 0.6f * pulse);
        br[n] = TRACK_COL[t].r / 255.0f;
        bg[n] = TRACK_COL[t].g / 255.0f;
        bb[n] = TRACK_COL[t].b / 255.0f;
        n++;
    }
    /* a core in the middle that breathes with the bass */
    bx[n] = 0;
    by[n] = 0;
    bs[n] = 0.05f + 0.12f * fminf(1, low_energy) + 0.06f * pulse;
    br[n] = 1.0f;
    bg[n] = 0.82f;
    bb[n] = 0.45f;
    n++;

    float sparkle = fminf(1, high_energy);
    for (int py = 0; py < th; py++) {
        float y = 1 - 2.0f * py / th;
        for (int px = 0; px < tw; px++) {
            float x = (2.0f * px / tw - 1) * aspect;
            float field = 0, cr = 0, cg = 0, cb = 0;
            for (int i = 0; i < n; i++) {
                float dx = x - bx[i], dy = y - by[i];
                float f = bs[i] * bs[i] / (dx * dx + dy * dy + 1e-4f);
                field += f;
                cr += f * br[i];
                cg += f * bg[i];
                cb += f * bb[i];
            }
            float inv = field > 0 ? 1 / field : 0;
            float inside = field < 0.9f ? 0 : field > 1.1f ? 1 : (field - 0.9f) * 5;
            inside = inside * inside * (3 - 2 * inside);
            float core = fminf(1, (field - 1) * 0.15f);
            core = core < 0 ? 0 : core;
            float glow = fminf(field, 1) * fminf(field, 1) * fminf(field, 1) * (0.35f + 0.4f * pulse);
            float vign = 1 - 0.55f * (x * x / (aspect * aspect) + y * y);
            float r = (cr * inv) * (inside * (0.75f + 0.5f * core) + glow * (1 - inside)) + 0.03f * vign;
            float g = (cg * inv) * (inside * (0.75f + 0.5f * core) + glow * (1 - inside)) + 0.035f * vign;
            float b = (cb * inv) * (inside * (0.75f + 0.5f * core) + glow * (1 - inside)) + 0.06f * vign;
            if (sparkle > 0.05f && ((px * 7919 + py * 104729 + (int)(SDL_GetTicks() / 70) * 31) % 997) < sparkle * 3) {
                r += 0.6f * sparkle;                 /* hi-hats glitter */
                g += 0.6f * sparkle;
                b += 0.7f * sparkle;
            }
            Uint32 R = r > 1 ? 255 : r < 0 ? 0 : (Uint32)(r * 255);
            Uint32 G = g > 1 ? 255 : g < 0 ? 0 : (Uint32)(g * 255);
            Uint32 B = b > 1 ? 255 : b < 0 ? 0 : (Uint32)(b * 255);
            show_px[py * tw + px] = 0xFF000000u | R << 16 | G << 8 | B;
        }
    }
    SDL_UpdateTexture(show_tex, NULL, show_px, tw * (int)sizeof(Uint32));
    SDL_RenderCopy(ren, show_tex, NULL, NULL);

    /* the spectrum as a ring of bars around the middle */
    int cx = W / 2, cy = H / 2, r0 = (int)(H * (0.30f + 0.04f * pulse));
    double bin_hz = (double)SR / VIS_FFT;
    for (int i = 0; i < 96; i++) {
        double f0 = 40 * pow(400.0, i / 96.0), f1 = 40 * pow(400.0, (i + 1) / 96.0);
        float best = -140;
        for (int k = (int)(f0 / bin_hz); k <= (int)(f1 / bin_hz) && k < VIS_FFT / 2; k++) best = fmaxf(best, vis_smooth[k]);
        float t = (best + 80) / 80;
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        float a = (float)(i / 96.0 * 2 * VIS_PI) - (float)VIS_PI / 2 + show_phase * 0.25f;
        Col c = TRACK_COL[i * MAX_TRACKS / 96];
        int len = 4 + (int)(t * H * 0.16f);
        for (int side = -1; side <= 1; side += 2) {   /* mirrored left and right */
            float aa = side < 0 ? a : (float)VIS_PI - a;
            int x0 = cx + (int)(cosf(aa) * r0), y0 = cy + (int)(sinf(aa) * r0);
            int x1 = cx + (int)(cosf(aa) * (r0 + len)), y1 = cy + (int)(sinf(aa) * (r0 + len));
            for (int wdt = -1; wdt <= 1; wdt++) line(x0 + wdt, y0, x1 + wdt, y1, alpha(c, (Uint8)(90 + 150 * t)));
        }
    }
    /* beat ripples */
    for (int i = 0; i < ripple_n; i++) {
        ripple_t[i] += dt;
        if (ripple_t[i] > 1.2f) {
            ripple_t[i--] = ripple_t[--ripple_n];
            continue;
        }
        int rr = r0 + (int)(ripple_t[i] * H * 0.5f);
        ring(cx, cy, rr, alpha(HEAD, (Uint8)(120 * (1 - ripple_t[i] / 1.2f))));
    }
    show_overlay("Blobs", "a glowing blob for every track that's playing");
}

/* text with a dark backing, readable over any scene */
static void show_text(int x, int y, const char *s, Col c)
{
    fill((SDL_Rect){x - 6, y - 2, text_w(s) + 12, font_h + 4}, (Col){0, 0, 0, 120});
    text(x, y, s, c);
}

/* what's playing, which scene, and the keys: on top of every scene */
static void show_overlay(const char *name, const char *caption)
{
    char buf[200];
    double secs = (double)P.playhead / SR, beats = secs * P.bpm / 60.0;
    snprintf(buf, sizeof buf, "%.60s   bar %d  beat %d   %.0f BPM   %02d:%02d", proj_path[0] ? base_name(proj_path) : "tinydaw",
             (int)(beats / 4) + 1, (int)fmod(beats, 4.0) + 1, P.bpm, (int)(secs / 60), (int)fmod(secs, 60.0));
    show_text(20, H - font_h - 16, buf, rgb(230, 230, 236));
    snprintf(buf, sizeof buf, "%d/%d  %s", show_scene + 1, scene_count() + 1, name);
    show_text(20, 16, buf, rgb(255, 214, 90));
    if (caption && caption[0]) show_text(20, 20 + font_h, caption, rgb(170, 175, 190));
    const char *hint = show_cycle ? "Left/Right scene  ·  Tab: stop cycling  ·  Space play  ·  Esc leave"
                                  : "Left/Right scene  ·  Tab: cycle scenes  ·  Space play  ·  Esc leave";
    show_text(W - text_w(hint) - 20, H - font_h - 16, hint, rgb(170, 175, 190));
}

/* ---- the other scenes (scenes.c), with cycling, crossfades and the music driving them */

static SDL_Texture *scene_tex;
static int scene_tw, scene_th;
static float *scene_buf, *scene_buf2;
static size_t scene_cap;
static Uint32 *scene_px;
static double show_switch_at, show_wall;
static int show_fade_from = -1;
static double show_fade_left;
static Uint32 show_last_tick;
#define SHOW_FADE 1.6

static const char *show_name(int k) { return k == 0 ? "Blobs" : scene_name(k - 1); }

static void show_go(int k)               /* to scene k, crossfading from the one before */
{
    int n = scene_count() + 1;
    k = ((k % n) + n) % n;
    if (k == show_scene) return;
    show_fade_from = show_scene != 0 && k != 0 ? show_scene : -1;   /* the blobs draw differently: cut */
    show_fade_left = SHOW_FADE;
    show_scene = k;
    show_started[k] = show_clock;            /* each visit draws itself afresh */
    if (k > 0) scene_reset(k - 1);
    show_switch_at = show_wall + show_each;
    settings_save();
}

static void show_draw(void)
{
    Uint32 now = SDL_GetTicks();
    double dt = show_last_tick ? (now - show_last_tick) / 1000.0 : 1 / 60.0;
    if (dt > 0.25) dt = 0.25;
    show_last_tick = now;
    float lv = fminf(1.0f, master_meter[0].level), bass = fminf(1.5f, low_energy);
    double beats = (double)P.playhead / SR * P.bpm / 60.0;
    float bf = P.playing ? (float)(beats - floor(beats)) : 1;
    float beat = fmaxf(beat_pulse, P.playing ? expf(-bf * 7) * fminf(1, low_energy * 3) : 0);
    show_clock += dt * (1 + 0.9 * lv);       /* louder music draws faster */
    show_wall += dt;
    show_fade_left = fmax(0, show_fade_left - dt);
    if (show_cycle && show_wall >= show_switch_at) show_go(show_scene + 1);
    if (show_scene == 0) {
        show_blobs();
        return;
    }
    int si = show_scene - 1, rw = scene_width(si, W), rh = rw * H / (W > 0 ? W : 1);
    if (rh < 90) rh = 90;
    size_t need = (size_t)rw * rh * 3;
    if (need > scene_cap) {
        free(scene_buf);
        free(scene_buf2);
        free(scene_px);
        scene_buf = malloc(need * sizeof(float));
        scene_buf2 = malloc(need * sizeof(float));
        scene_px = malloc(need / 3 * sizeof(Uint32));
        scene_cap = scene_buf && scene_buf2 && scene_px ? need : 0;
        if (!scene_cap) return;
    }
    if (!scene_tex || rw != scene_tw || rh != scene_th) {
        if (scene_tex) SDL_DestroyTexture(scene_tex);
        scene_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, rw, rh);
        if (!scene_tex) return;
        SDL_SetTextureScaleMode(scene_tex, SDL_ScaleModeLinear);
        scene_tw = rw;
        scene_th = rh;
    }
    float zoom = 1 + 0.06f * bass + 0.05f * beat;            /* the bass breathes, beats pulse */
    float gain = 0.9f + 0.35f * lv + 0.4f * beat;
    char caption[96];
    scene_render(si, show_clock - show_started[show_scene], dt, zoom, scene_buf, rw, rh, caption, sizeof caption);
    if (show_fade_from > 0 && show_fade_left > 0) {           /* crossfade from the last scene */
        char dummy[96];
        scene_render(show_fade_from - 1, show_clock - show_started[show_fade_from], dt, zoom, scene_buf2, rw, rh, dummy,
                     sizeof dummy);
        float k = (float)(1 - show_fade_left / SHOW_FADE);
        k = k * k * (3 - 2 * k);
        for (size_t i = 0; i < need; i++) scene_buf[i] = scene_buf2[i] + (scene_buf[i] - scene_buf2[i]) * k;
    }
    for (size_t i = 0; i < need / 3; i++) {
        float r = scene_buf[3 * i] * gain, g = scene_buf[3 * i + 1] * gain, b = scene_buf[3 * i + 2] * gain;
        Uint32 R = r >= 1 ? 255 : r <= 0 ? 0 : (Uint32)(r * 255), G = g >= 1 ? 255 : g <= 0 ? 0 : (Uint32)(g * 255),
               B = b >= 1 ? 255 : b <= 0 ? 0 : (Uint32)(b * 255);
        scene_px[i] = 0xFF000000u | R << 16 | G << 8 | B;
    }
    SDL_UpdateTexture(scene_tex, NULL, scene_px, rw * (int)sizeof(Uint32));
    SDL_RenderCopy(ren, scene_tex, NULL, NULL);
    char cap2[200];
    snprintf(cap2, sizeof cap2, caption[0] ? "%s: %s" : "%s", scene_caption(si), caption);
    show_overlay(scene_name(si), cap2);
}

static void show_toggle(void)
{
    show_open = !show_open;
    if (show_open) {                         /* start the current scene afresh, cycling from now */
        show_last_tick = 0;
        show_started[show_scene] = show_clock;
        show_switch_at = show_wall + show_each;
        show_fade_left = 0;
        if (show_scene > 0) scene_reset(show_scene - 1);
    }
    if (!headless) SDL_SetWindowFullscreen(win, show_open ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

/* ---------------------------------------------------------------- the rail and panels */

/* A column of category buttons down the left. One panel is open beside it at a time; clicking
 * the open one again hides it and gives the timeline the room. */
#define RAIL_W 70
static int *const RAIL_FLAG[RI_COUNT] = {&sounds_open, &theory_open, &pads_open, &mixer_open, &vis_open, &settings_open};
static const char *const RAIL_NAME[RI_COUNT] = {"Sounds", "Theory", "Pads", "Mixer", "Visual", "Settings"};
static int last_panel = RI_SOUNDS;

static void show_panel(int which);

static void open_panel(int which)        /* toggles it; the others close */
{
    int was = *RAIL_FLAG[which];
    for (int i = 0; i < RI_COUNT; i++) *RAIL_FLAG[i] = 0;
    *RAIL_FLAG[which] = !was;
    if (!was) last_panel = which;
}

static void show_panel(int which)        /* opens it (never closes it); the others close */
{
    if (!*RAIL_FLAG[which]) open_panel(which);
}

static void rail_icon(int kind, int cx, int cy, Col c)
{
    switch (kind) {
    case RI_SOUNDS: {                           /* a waveform */
        static const int ys[] = {0, -6, 5, -9, 8, -4, 3, 0};
        for (int i = 0; i < 7; i++) line(cx - 10 + i * 3, cy + ys[i], cx - 10 + (i + 1) * 3, cy + ys[i + 1], c);
        for (int i = 0; i < 7; i++) line(cx - 10 + i * 3, cy + ys[i] + 1, cx - 10 + (i + 1) * 3, cy + ys[i + 1] + 1, c);
        break;
    }
    case RI_THEORY:                             /* a note */
        disc(cx - 3, cy + 5, 4, c);
        fill((SDL_Rect){cx, cy - 9, 2, 14}, c);
        line(cx + 1, cy - 9, cx + 7, cy - 5, c);
        line(cx + 1, cy - 8, cx + 7, cy - 4, c);
        break;
    case RI_PADS:                               /* a 2x2 grid */
        for (int i = 0; i < 4; i++) fill((SDL_Rect){cx - 9 + (i % 2) * 10, cy - 9 + (i / 2) * 10, 8, 8}, c);
        break;
    case RI_MIXER:                              /* three faders */
        for (int i = 0; i < 3; i++) {
            int fx = cx - 7 + i * 7, ky = cy + (i == 1 ? -4 : i == 0 ? 3 : -1);
            line(fx, cy - 9, fx, cy + 9, c);
            fill((SDL_Rect){fx - 3, ky - 2, 7, 4}, c);
        }
        break;
    case RI_VISUAL:                             /* rising bars */
        for (int i = 0; i < 4; i++) {
            int h = 5 + i * 4;
            fill((SDL_Rect){cx - 9 + i * 5, cy + 9 - h, 3, h}, c);
        }
        break;
    case RI_SETTINGS:                           /* a gear */
        for (int i = 0; i < 8; i++) {
            double a = i * VIS_PI / 4;
            int x0 = cx + (int)(cos(a) * 6), y0 = cy + (int)(sin(a) * 6), x1 = cx + (int)(cos(a) * 10), y1 = cy + (int)(sin(a) * 10);
            line(x0, y0, x1, y1, c);
            line(x0 + 1, y0, x1 + 1, y1, c);
        }
        disc(cx, cy, 7, c);
        disc(cx, cy, 3, PANEL);
        break;
    }
}

static void rail(void)
{
    SDL_Rect r = {0, L.top, RAIL_W, H - L.top - L.status};
    if (ST.shaped) shape_fill(r, ST.rail_top, ST.rail_bot, 0, theme_kind == THEME_AERO);
    else fill(r, tone(PANEL, 0.8f));
    line(RAIL_W - 1, r.y, RAIL_W - 1, r.y + r.h, LINE);
    for (int i = 0; i < RI_COUNT; i++) {
        int y = i == RI_SETTINGS ? r.y + r.h - 62 : r.y + 6 + i * 62;
        SDL_Rect b = {4, y, RAIL_W - 8, 56};
        int on = *RAIL_FLAG[i], hov = hit(b) && drag.kind == D_NONE;
        if (ST.shaped) {
            if (on) shape_fill(b, ST.rail_on, ST.rail_on, ST.radius, theme_kind == THEME_AERO);
            else if (hov) shape_fill(b, alpha(rgb(255, 255, 255), 50), alpha(rgb(255, 255, 255), 30), ST.radius, 0);
        } else if (on || hov) {
            fill(b, on ? PANEL3 : PANEL2);
        }
        if (on && !ST.shaped) fill((SDL_Rect){0, y + 6, 3, 44}, ACCENT);
        Col rtext = ST.shaped ? ST.rail_text : TEXT, rdim = ST.shaped ? ST.rail_dim : DIM;
        Col c = on ? (theme_kind == THEME_BLENDER ? rgb(255, 255, 255) : ACCENT) : hov ? rtext : rdim;
        rail_icon(i, RAIL_W / 2, y + 20, c);
        if (i == RI_SOUNDS) {
            if (on) lib_badge = 0;
            if (lib_badge > 0) {                /* "+3": sounds added elsewhere, waiting in the library */
                char bd[8];
                snprintf(bd, sizeof bd, "+%d", lib_badge > 99 ? 99 : lib_badge);
                int bw = text_w(bd) + 8;
                int pulse = SDL_GetTicks() - lib_badge_at < 600;
                SDL_Rect pb = {b.x + b.w - bw - 2, y + 2, bw, font_h + 2};
                fill(pb, pulse ? HEAD : ACCENT);
                text(pb.x + 4, pb.y + 1, bd, DARK);
            }
        }
        Col label = on ? (ST.shaped ? (theme_kind == THEME_BLENDER ? rgb(255, 255, 255) : TEXT) : TEXT) : rdim;
        text(RAIL_W / 2 - text_w(RAIL_NAME[i]) / 2, y + 36, RAIL_NAME[i], label);
        if (hov && lpress) {
            open_panel(i);
            lpress = 0;
        }
    }
}

/* the open panel's rectangle (and scrolling: a panel scrolls when its content is taller) */
static SDL_Rect panel_rect(void) { return (SDL_Rect){L.pane_x, L.top, L.pane_w, H - L.top - L.status}; }

static int panel_begin(SDL_Rect pane, int *scroll, int content_h, int *saved_press)
{
    fill(pane, PANEL);
    line(pane.x + pane.w - 1, pane.y, pane.x + pane.w - 1, pane.y + pane.h, LINE);
    int inside = hit(pane);
    *saved_press = lpress;
    if (!inside) lpress = 0;
    int max_scroll = content_h - pane.h > 0 ? content_h - pane.h : 0;
    *scroll = *scroll < 0 ? 0 : *scroll > max_scroll ? max_scroll : *scroll;
    SDL_RenderSetClipRect(ren, &pane);
    return inside;
}

static void panel_end(SDL_Rect pane, int *scroll, int content_h, int inside, int saved_press)
{
    scrollbar(pane, scroll, content_h);
    SDL_RenderSetClipRect(ren, NULL);
    if (!inside) lpress = saved_press;
}

/* ---- Sounds: the library, and making new sounds */

static int sounds_scroll, sounds_content_h;

/* Takes a sound out of the library, with every clip that plays it (or a pitched or reversed copy
 * of it), off its pads and the instrument. One undo step puts it all back. */
static void remove_sound(int i)
{
    if (i < 0 || i >= P.nsamples || P.samples[i].removed) return;
    int gone = 0;
    for (int c = P.nclips - 1; c >= 0; c--)
        if (sample_base(&P, P.clips[c].sample) == i) {
            clip_delete(c);
            gone++;
        }
    LOCK();
    for (int k = 0; k < NPADS; k++)
        if (P.pads[k].sample >= 0 && sample_base(&P, P.pads[k].sample) == i) P.pads[k].sample = -1;
    if (P.inst_sample >= 0 && sample_base(&P, P.inst_sample) == i) P.inst_sample = -1;
    if (P.preview >= 0 && sample_base(&P, P.preview) == i) P.preview = -1;
    for (int v = 0; v < MAX_VOICES; v++)
        if (P.voices[v].active && sample_base(&P, P.voices[v].sample) == i) P.voices[v].active = 0;
    P.samples[i].removed = 1;
    UNLOCK();
    if (gone) status("Removed \"%s\" and its %d clip%s. Ctrl+Z brings them back.", P.samples[i].name, gone,
                     gone == 1 ? "" : "s");
    else status("Removed \"%s\". Ctrl+Z brings it back.", P.samples[i].name);
}


/* ---- copying sounds between the library, the pads and the instrument (right-click, or Ctrl+C
 * and Ctrl+V with the pointer over one) */

static struct {
    int has, sample, from_pad;
    Pad pad;                            /* a pad's settings, when copied from a pad */
} sclip;
enum { CMA_COPY, CMA_PASTE, CMA_REMOVE, CMA_CLEAR };
static struct {
    int open, kind, idx, n;
    int act[4], enabled[4];
    char label[4][80];
    SDL_Rect box;
} cm;
static int lib_flash = -1;              /* a sound just pasted into the library: lit for a moment */
static Uint32 lib_flash_at;

static void sound_copy(int kind, int idx)
{
    int s = kind == CM_LIB ? idx : kind == CM_PAD ? P.pads[idx].sample : P.inst_sample;
    if (s < 0 || s >= P.nsamples) return;
    sclip.has = 1;
    sclip.sample = s;
    sclip.from_pad = kind == CM_PAD;
    if (kind == CM_PAD) sclip.pad = P.pads[idx];
    status("Copied \"%s\": right-click a pad, the instrument or the library and Paste", P.samples[s].name);
}

/* the sound the clipboard would put in the library: with a pad's pitch, a pitched copy */
static int sclip_library_sound(void)
{
    if (!sclip.has || sclip.sample < 0 || sclip.sample >= P.nsamples) return -1;
    if (sclip.from_pad && sclip.pad.pitch) return sample_pitched(&P, sclip.sample, sclip.pad.pitch);
    return sclip.sample;
}

static void sound_paste(int kind, int idx)
{
    if (!sclip.has || sclip.sample < 0 || sclip.sample >= P.nsamples) return;
    if (P.samples[sclip.sample].removed) P.samples[sclip.sample].removed = 0;   /* removed since: back it comes */
    if (kind == CM_PAD) {
        LOCK();
        voice_stop(&P, idx, -1);
        if (sclip.from_pad) {                   /* pad to pad: the settings come too */
            P.pads[idx] = sclip.pad;
        } else {
            P.pads[idx].sample = sclip.sample;
        }
        UNLOCK();
        pad_sel = idx;
        status("Pad %d plays \"%s\"", idx + 1, P.samples[sclip.sample].name);
    } else if (kind == CM_INST) {
        LOCK();
        P.inst_sample = sclip.sample;
        UNLOCK();
        status("MIDI keys and new note clips play \"%s\"", P.samples[sclip.sample].name);
    } else {                                    /* into the library */
        int s = sclip_library_sound();
        if (s < 0) {
            status("Couldn't make that sound");
            return;
        }
        Sample *sm = &P.samples[s];
        int shown = (!sm->pitched || sm->listed) && !sm->removed;
        if (shown && !(sclip.from_pad && sclip.pad.pitch)) {
            status("\"%s\" is already in the library", sm->name);
        } else {
            sm->removed = 0;
            if (sm->pitched) sm->listed = 1;
            status("Pasted \"%s\" into the library. Ctrl+Z takes it out again", sm->name);
        }
        P.preview = -1;
        for (int row = 0, k = 0; k < P.nsamples; k++) {   /* scroll to it */
            if ((P.samples[k].pitched && !P.samples[k].listed) || P.samples[k].removed) continue;
            if (k == s) {
                if (row < lib_scroll || row > lib_scroll + 8) lib_scroll = row > 3 ? row - 3 : 0;
                break;
            }
            row++;
        }
        lib_flash = s;
        lib_flash_at = SDL_GetTicks();
    }
}

static void remove_sound(int i);

/* The library row, pad or instrument slot under the pointer, if any (for Ctrl+C / Ctrl+V). */
static int sound_under_mouse(int *kind, int *idx)
{
    if (pads_open) {
        for (int i = 0; i < NPADS; i++)
            if (hit(pad_rects[i])) {
                *kind = CM_PAD;
                *idx = i;
                return 1;
            }
        if (hit(inst_drop)) {
            *kind = CM_INST;
            *idx = 0;
            return 1;
        }
    }
    if (sounds_open && sounds_sub == 0) {
        for (int i = 0; i < P.nsamples; i++)
            if (lib_rects[i].w && hit(lib_rects[i]) && !P.samples[i].removed &&
                (!P.samples[i].pitched || P.samples[i].listed)) {
                *kind = CM_LIB;
                *idx = i;
                return 1;
            }
        SDL_Rect pr = panel_rect();             /* anywhere else in the library: paste into it */
        if (hit(pr)) {
            *kind = CM_LIB;
            *idx = -1;
            return 1;
        }
    }
    return 0;
}

/* Opens the right-click menu for a library sound, a pad or the instrument slot. */
static void cm_open(int kind, int idx)
{
    int s = kind == CM_LIB ? idx : kind == CM_PAD ? P.pads[idx].sample : P.inst_sample;
    int has = s >= 0 && s < P.nsamples;
    memset(&cm, 0, sizeof cm);
    cm.open = 1;
    cm.kind = kind;
    cm.idx = idx;
    snprintf(cm.label[0], sizeof cm.label[0], "Copy");
    cm.act[0] = CMA_COPY;
    cm.enabled[0] = has;
    if (sclip.has && sclip.sample < P.nsamples)
        snprintf(cm.label[1], sizeof cm.label[1], "Paste \"%.50s\"", P.samples[sclip.sample].name);
    else
        snprintf(cm.label[1], sizeof cm.label[1], "Paste");
    cm.act[1] = CMA_PASTE;
    cm.enabled[1] = sclip.has && sclip.sample < P.nsamples;
    if (kind == CM_LIB) {
        snprintf(cm.label[2], sizeof cm.label[2], "Remove from the library");
        cm.act[2] = CMA_REMOVE;
    } else {
        snprintf(cm.label[2], sizeof cm.label[2], kind == CM_PAD ? "Clear the pad" : "Clear the instrument");
        cm.act[2] = CMA_CLEAR;
    }
    cm.enabled[2] = has;
    cm.n = 3;
    int w = 0;
    for (int i = 0; i < cm.n; i++) w = text_w(cm.label[i]) > w ? text_w(cm.label[i]) : w;
    w += 28;
    int h = cm.n * (font_h + 12) + 8;
    cm.box = (SDL_Rect){mx + 2, my + 2, w, h};
    if (cm.box.x + w > W - 4) cm.box.x = W - 4 - w;
    if (cm.box.y + h > H - 4) cm.box.y = H - 4 - h;
}

/* Drawn last, over everything; takes the next click. */
static void cm_draw(void)
{
    if (!cm.open) return;
    fill((SDL_Rect){cm.box.x + 3, cm.box.y + 3, cm.box.w, cm.box.h}, alpha(DARK, 90));
    fill(cm.box, PANEL);
    outline(cm.box, LINE);
    int chosen = -1;
    for (int i = 0; i < cm.n; i++) {
        SDL_Rect r = {cm.box.x + 4, cm.box.y + 4 + i * (font_h + 12), cm.box.w - 8, font_h + 12};
        int hov = hit(r) && cm.enabled[i];
        if (hov) fill(r, tone(ACCENT, 0.5f));
        text(r.x + 10, r.y + 6, cm.label[i], cm.enabled[i] ? TEXT : tone(DIM, 0.7f));
        if (hov && lpress) chosen = i;
    }
    if (chosen >= 0) {
        switch (cm.act[chosen]) {
        case CMA_COPY: sound_copy(cm.kind, cm.idx); break;
        case CMA_PASTE: sound_paste(cm.kind, cm.idx); break;
        case CMA_REMOVE: remove_sound(cm.idx); break;
        case CMA_CLEAR:
            LOCK();
            if (cm.kind == CM_PAD) {
                voice_stop(&P, cm.idx, -1);
                P.pads[cm.idx].sample = -1;
            } else {
                P.inst_sample = -1;
            }
            UNLOCK();
            status(cm.kind == CM_PAD ? "Cleared pad %d" : "Cleared the instrument", cm.idx + 1);
            break;
        }
    }
    if (lpress || rpress) cm.open = 0;          /* any click closes it */
    lpress = rpress = 0;
}

static void library_list(int x, int y, int w, int bottom)
{
    char buf[64];
    memset(lib_rects, 0, sizeof lib_rects);   /* only the rows drawn now can be hit */
    static int vis[MAX_SAMPLES];              /* pitched/reversed copies made for clips are hidden */
    int nvis = 0;
    for (int i = 0; i < P.nsamples; i++)
        if ((!P.samples[i].pitched || P.samples[i].listed) && !P.samples[i].removed && !P.samples[i].hidden) vis[nvis++] = i;
    snprintf(buf, sizeof buf, "%d sound%s", nvis, nvis == 1 ? "" : "s");
    text(x, y, buf, DIM);
    y += font_h + 10;
    SDL_Rect list = {x, y, w, bottom - y - 2 * font_h - 20};
    int item_h = 26, visible = list.h / item_h;
    if (visible < 1) visible = 1;
    if (nvis == 0) {
        text(x, y + 4, "Nothing here yet. Make one under", DIM);
        text(x, y + 4 + font_h + 2, "New sound, or drop audio files", DIM);
        text(x, y + 4 + 2 * (font_h + 2), "(wav, flac, ogg, mp3) on the window.", DIM);
        return;
    }
    if (hit(list) && wheel_y && drag.kind == D_NONE) {
        lib_scroll -= wheel_y;
        wheel_y = 0;
    }
    int max_scroll = nvis - visible > 0 ? nvis - visible : 0;
    lib_scroll = lib_scroll < 0 ? 0 : lib_scroll > max_scroll ? max_scroll : lib_scroll;
    int lw = max_scroll > 0 ? w - 14 : w;
    for (int row = lib_scroll; row < nvis && row < lib_scroll + visible; row++) {
        int i = vis[row];
        SDL_Rect r = {x, y + (row - lib_scroll) * item_h, lw, item_h - 3};
        lib_rects[i] = r;
        int hov = hit(r) && drag.kind == D_NONE;
        fill(r, P.preview == i ? tone(ACCENT, 0.45f) : hov ? PANEL3 : PANEL2);
        if (lib_flash == i && SDL_GetTicks() - lib_flash_at < 1500) outline(r, ACCENT);   /* just pasted */
        snprintf(buf, sizeof buf, "%.2fs", (double)P.samples[i].frames / SR);
        int dw = text_w(buf);
        SDL_Rect xr = {r.x + r.w - 24, r.y + 2, 20, r.h - 4};     /* remove: shown on the row under the mouse */
        text_clip((SDL_Rect){r.x + 6, r.y, r.w - dw - 18 - (hov ? 24 : 0), r.h}, r.x + 8, r.y + (r.h - font_h) / 2,
                  P.samples[i].name, TEXT);
        text(r.x + r.w - dw - 8 - (hov ? 24 : 0), r.y + (r.h - font_h) / 2, buf, DIM);
        if (hov) {
            int xh = hit(xr);
            fill(xr, xh ? rgb(200, 60, 55) : PANEL3);
            Col xc = xh ? rgb(255, 255, 255) : TEXT;
            for (int d = -1; d <= 1; d++) {
                line(xr.x + 6 + d, xr.y + 5, xr.x + 14 + d, xr.y + xr.h - 6, xc);
                line(xr.x + 14 + d, xr.y + 5, xr.x + 6 + d, xr.y + xr.h - 6, xc);
            }
            if (xh && lpress) {
                lpress = 0;
                remove_sound(i);
                continue;
            }
        }
        if (hov && rpress) {                     /* right-click: copy, paste, remove */
            rpress = 0;
            cm_open(CM_LIB, i);
        }
        if (hov && lpress) {
            drag = (typeof(drag)){.kind = D_LIBPEND, .sample = i, .sx = mx, .sy = my};
            lpress = 0;
        }
    }
    if (max_scroll > 0) {                       /* a scrollbar in pixels, the list in whole rows */
        static int lib_px;
        if (!(drag.kind == D_SCROLL && drag.inum == &lib_px)) lib_px = lib_scroll * item_h;
        SDL_Rect area = {x, y, w + 2, visible * item_h};
        scrollbar(area, &lib_px, nvis * item_h);
        lib_scroll = (lib_px + item_h / 2) / item_h;
    }
    text(x, bottom - 2 * font_h - 10, "Click to hear, drag onto a track.", DIM);
    text(x, bottom - font_h - 6, "Drop audio files here to import.", DIM);
}

static float *heard_buf;                /* what the New sound Play button started */
static SDL_Rect ns_note_r;              /* where New sound's Note box is, for the self-test */

static int new_sound(int x, int y, int w)
{
    int hw = (w - 8) / 2;
    text(x, y, "SOUND", DIM);
    y += font_h + 8;
    int bw = (w - 3 * 4) / 4;
    for (int i = 0; i < WAVE_COUNT; i++) {
        SDL_Rect r = {x + (i % 4) * (bw + 4), y + (i / 4) * 30, bw, 26};
        if (button(r, WAVE_NAMES[i], G.wave == i)) G.wave = i;
    }
    y += 70;
    int drum = G.wave >= WAVE_KICK;
    ns_note_r = (SDL_Rect){x, y, hw, 26};
    if (!drum) note_number(ns_note_r, "Note", &G.note);
    number((SDL_Rect){drum ? x : x + hw + 8, y, drum ? w : hw, 26}, "Length", &G.beats, 0.0625, 32, 0.01, "%.2f b");
    y += 34;
    number((SDL_Rect){x, y, hw, 26}, "Volume", &G.volume, 0, 1, 0.005, "%.2f");
    number((SDL_Rect){x + hw + 8, y, hw, 26}, "A4", &P.a4, 415, 466, 0.05, "%.1f Hz");
    y += 34;
    if (!drum) {
        text(x, y, "ENVELOPE", DIM);
        y += font_h + 8;
        number((SDL_Rect){x, y, hw, 26}, "Attack", &G.attack_ms, 0, 2000, 1, "%.0f ms");
        number((SDL_Rect){x + hw + 8, y, hw, 26}, "Release", &G.release_ms, 0, 4000, 2, "%.0f ms");
        y += 34;
    }
    y += 6;
    int hearing = scratch && scratch == heard_buf && scratch_pos < scratch_len;
    if (button((SDL_Rect){x, y, 96, 32}, hearing ? "Stop" : "Play", hearing)) {
        if (hearing) {
            play_scratch(NULL, 0);
        } else {                                 /* hear it as it stands, without keeping it */
            GenParams g = G;
            g.a4 = P.a4;
            long len;
            int note = G.note;
            float *b = render_notes(&g, P.bpm, &note, 1, &len);
            if (b) {
                for (long k = 0; k < 2 * len; k++) b[k] /= 0.8f;   /* render_notes scales chords down */
                play_scratch(b, len);
                heard_buf = b;
            }
        }
    }
    if (button((SDL_Rect){x + 102, y, w - 102, 32}, "+ Add to library", 1)) {
        G.a4 = P.a4;
        int idx = sample_generate(&P, &G);
        if (idx >= 0) {
            status("Made \"%s\": it's in the library, drag it onto a track", P.samples[idx].name);
            preview(idx);
            lib_scroll = P.nsamples;
            sounds_sub = 0;                     /* show it where it went */
        } else {
            status("Library is full");
        }
    }
    y += 44;
    text(x, y, "Chords, scales and drum beats", DIM);
    text(x, y + font_h + 2, "are in the Theory panel.", DIM);
    return y + 2 * font_h + 10;
}


/* ---- Sounds > Generator: genre presets on a small synth, shaped with five plain controls.
 * Made for people who've never used a synth: pick a style, pick what the sound is for, click a
 * sound to hear it, nudge it with words like "brighter" and "fatter", then keep it. */

static int gx_genre = -1, gx_kind = -1, gx_preset = -1;   /* -1 = any */
static SynthSound GX;
static float *gx_heard;                 /* what the Generator's Play started */
static double *slide_done;              /* the slider whose drag just ended */

static const char *const KIND_ABOUT[] = {
    "the low notes under everything", "the main tune people hum", "long, soft background chords",
    "piano-like, for chords and melodies", "short notes for riffs and arpeggios", "voices: choirs, oohs and aahs, talking synths",
    "single drum hits to build beats",
    "whooshes, booms and effects for transitions"};

/* A 0..1 slider with a mark at the middle ("as designed"). Returns 1 when a drag on it ends,
 * or a double-click puts it back to the middle. */
static int slider(SDL_Rect r, double *v)
{
    int active = drag.kind == D_SLIDE && drag.num == v, hov = hit(r) && (drag.kind == D_NONE || active);
    int cy = r.y + r.h / 2, kx = r.x + (int)lround(*v * r.w);
    fill((SDL_Rect){r.x, cy - 2, r.w, 4}, PANEL3);
    int mid = r.x + r.w / 2;
    fill(kx >= mid ? (SDL_Rect){mid, cy - 2, kx - mid, 4} : (SDL_Rect){kx, cy - 2, mid - kx, 4}, ACCENT);
    fill((SDL_Rect){mid - 1, cy - 7, 2, 14}, DIM);
    SDL_Rect knob = {kx - 7, cy - 9, 14, 18};
    fill(knob, active ? HEAD : hov ? tone(ACCENT, 1.25f) : ACCENT);
    outline(knob, DARK);
    if (drag.kind == D_NONE && hit((SDL_Rect){r.x - 8, r.y, r.w + 16, r.h})) {
        if (lpress && ldouble) {
            lpress = 0;
            *v = 0.5;
            return 1;
        }
        if (lpress) {
            lpress = 0;
            drag = (typeof(drag)){.kind = D_SLIDE, .num = v, .sx = r.x, .sy = r.w};
            double nv = (double)(mx - r.x) / r.w;
            *v = nv < 0 ? 0 : nv > 1 ? 1 : nv;
        }
        if (wheel_y) {
            *v += wheel_y * 0.05;
            *v = *v < 0 ? 0 : *v > 1 ? 1 : *v;
            wheel_y = 0;
        }
    }
    if (slide_done == v) {
        slide_done = NULL;
        return 1;
    }
    return 0;
}

static void gx_play(void)
{
    SynthSound s = GX;
    s.a4 = P.a4;
    long len;
    float *b = render_synth(&s, &len);
    if (b) {
        play_scratch(b, len);
        gx_heard = b;
    }
}

static int gx_reveal;                   /* scroll the picked sound's controls into view */
static SDL_Rect gx_row_r[128], gx_genre_r[16], gx_kind_r[12], gx_slide_r[MACROS], gx_lib_r, gx_place_r, gx_inst_r;

static void gx_pick(int preset)
{
    gx_reveal = gx_preset != preset;
    gx_preset = preset;
    synth_from_preset(&GX, preset);
    gx_play();
}

static int gx_matches(int i)
{
    const SynthPreset *p = &SYNTH_PRESETS[i];
    return (gx_genre < 0 || !strcmp(p->genre, SYNTH_GENRES[gx_genre])) &&
           (gx_kind < 0 || !strcmp(p->kind, SYNTH_KINDS[gx_kind]));
}

/* the library name: the preset's name, plus the note when it isn't the preset's own */
static void gx_name(char *buf, size_t n)
{
    const SynthPreset *p = &SYNTH_PRESETS[gx_preset];
    char nn[16];
    note_name(GX.note, nn, sizeof nn);
    if (GX.note != p->note && strcmp(p->kind, "Drums") && strcmp(p->kind, "FX")) snprintf(buf, n, "%s %s", p->name, nn);
    else snprintf(buf, n, "%s", p->name);
}

static int gx_keep(void)
{
    char name[96];
    gx_name(name, sizeof name);
    GX.a4 = P.a4;
    int idx = sample_generate_synth(&P, &GX, name);
    if (idx < 0) status("Library is full");
    return idx;
}

/* the first track with nothing playing from `at` for `len` frames */
static int free_track_at(long at, long len)
{
    for (int t = 0; t < P.ntracks; t++) {
        int busy = 0;
        for (int c = 0; c < P.nclips && !busy; c++)
            busy = P.clips[c].track == t && P.clips[c].start < at + len && P.clips[c].start + P.clips[c].length > at;
        if (!busy) return t;
    }
    return 0;
}

static void gx_surprise(void)
{
    static uint32_t r = 0x6d2b79f5u;
    int pool[256], n = 0;
    for (int i = 0; i < NSYNTH_PRESETS && n < 256; i++)
        if (gx_matches(i)) pool[n++] = i;
    if (!n) return;
    r = r * 1664525u + 1013904223u + SDL_GetTicks();
    synth_from_preset(&GX, pool[(r >> 8) % (uint32_t)n]);
    gx_preset = pool[(r >> 8) % (uint32_t)n];
    for (int m = 0; m < MACROS; m++) {       /* a nudge either way, not a wreck */
        r = r * 1664525u + 1013904223u;
        GX.macro[m] = 0.5 + ((double)(r >> 8) / 16777216.0 - 0.5) * (m == MAC_WOBBLE ? 0.5 : 0.7);
    }
    gx_play();
}

/* one sound in the list; returns its height */
static int gx_row(int x, int y, int w, int i)
{
    char buf[96];
    const SynthPreset *p = &SYNTH_PRESETS[i];
    SDL_Rect r = {x, y, w, 2 * font_h + 14};
    if (i < 128) gx_row_r[i] = r;
    int hov = hit(r) && drag.kind == D_NONE, on = gx_preset == i;
    fill(r, on ? tone(ACCENT, 0.45f) : hov ? PANEL3 : PANEL2);
    if (on) fill((SDL_Rect){r.x, r.y, 4, r.h}, ACCENT);
    snprintf(buf, sizeof buf, "%s %s", p->genre, p->kind);
    int tw = text_w(buf);
    text_clip((SDL_Rect){r.x + 10, r.y, r.w - tw - 26, r.h}, r.x + 12, r.y + 5, p->name, on ? HEAD : TEXT);
    text(r.x + r.w - tw - 10, r.y + 5, buf, DIM);
    text_clip((SDL_Rect){r.x + 10, r.y, r.w - 20, r.h}, r.x + 12, r.y + 9 + font_h, p->about, DIM);
    if (hov && lpress) {
        lpress = 0;
        gx_pick(i);
    }
    return r.h;
}

/* the controls for the picked sound, opened under its row */
static int gx_shape(int x, int y, int w)
{
    int top = y;
    x += 12;
    w -= 24;
    y += 12;
    int changed = 0, lw = 0, lo = 0, ww = 0;
    for (int m = 0; m < MACROS; m++) {
        int a = text_w(MACRO_NAMES[m][0]), b = text_w(MACRO_NAMES[m][1]), c = text_w(MACRO_NAMES[m][2]);
        lw = a > lw ? a : lw;
        lo = b > lo ? b : lo;
        ww = c > ww ? c : ww;
    }
    lw += 12;
    lo += 16;
    ww += 4;
    for (int m = 0; m < MACROS; m++) {
        text(x, y + 4, MACRO_NAMES[m][0], TEXT);
        text(x + lw, y + 4, MACRO_NAMES[m][1], DIM);
        int hx = x + w - ww;
        text(hx, y + 4, MACRO_NAMES[m][2], DIM);
        gx_slide_r[m] = (SDL_Rect){x + lw + lo, y, hx - (x + lw + lo) - 16, font_h + 8};
        changed |= slider(gx_slide_r[m], &GX.macro[m]);
        y += font_h + 16;
    }
    y += 4;
    int hw = (w - 8) / 2;
    const char *kind = SYNTH_PRESETS[gx_preset].kind;
    int pitched = strcmp(kind, "Drums") && strcmp(kind, "FX");
    note_number((SDL_Rect){x, y, hw, 26}, pitched ? "Note" : "Pitch", &GX.note);
    number((SDL_Rect){x + hw + 8, y, hw, 26}, "Length", &GX.seconds, 0.05, 12, 0.01, "%.2f s");
    y += 34;
    number((SDL_Rect){x, y, hw, 26}, "Volume", &GX.volume, 0.05, 1, 0.005, "%.2f");
    if (button((SDL_Rect){x + hw + 8, y, hw, 26}, "Back to how it was", 0)) {
        synth_from_preset(&GX, gx_preset);
        changed = 1;
    }
    y += 38;
    if (changed) gx_play();                  /* hear each change as soon as you let go */

    int hearing = scratch && scratch == gx_heard && scratch_pos < scratch_len;
    int bw3 = (w - 8) / 3;
    if (button((SDL_Rect){x, y, bw3, 32}, hearing ? "Stop" : "Play", hearing)) {
        if (hearing) play_scratch(NULL, 0);
        else gx_play();
    }
    if (button((SDL_Rect){x + bw3 + 4, y, bw3, 32}, "Surprise me", 0)) gx_surprise();
    gx_lib_r = (SDL_Rect){x + 2 * (bw3 + 4), y, w - 2 * (bw3 + 4), 32};
    if (button(gx_lib_r, "+ Library", 1)) {
        int idx = gx_keep();
        if (idx >= 0) {
            status("Made \"%s\": it's in the library, drag it onto a track", P.samples[idx].name);
            lib_scroll = P.nsamples;
        }
    }
    y += 38;
    gx_place_r = (SDL_Rect){x, y, hw, 32};
    gx_inst_r = (SDL_Rect){x + hw + 8, y, hw, 32};
    if (button(gx_place_r, "Put it at the playhead", 0)) {
        int idx = gx_keep();
        if (idx >= 0) {
            long at = snap(P.playhead);
            int t = free_track_at(at, P.samples[idx].frames);
            int c = clip_add(idx, t, at, 0, P.samples[idx].frames);
            if (c >= 0) {
                selected = c;
                status("Placed \"%s\" on track %d at the playhead", P.samples[idx].name, t + 1);
            }
        }
    }
    if (button(gx_inst_r, "Play notes with it", 0)) {
        int idx = gx_keep();
        if (idx >= 0) {
            LOCK();
            P.inst_sample = idx;
            P.inst_root = GX.note;
            UNLOCK();
            status("\"%s\" is the instrument: double-click a track to write notes, or play a MIDI keyboard",
                   P.samples[idx].name);
        }
    }
    y += 40;
    text(x, y, "Double-click a slider to put it back in the middle.", DIM);
    y += font_h + 12;
    SDL_Rect box = {x - 12, top, w + 24, y - top};
    outline(box, tone(ACCENT, 0.7f));
    if (gx_reveal) {                         /* bring the controls into view, keeping the row on screen */
        gx_reveal = 0;
        SDL_Rect pr = panel_rect();
        int over = y - (pr.y + pr.h - 8), room = top - 2 * font_h - 14 - (pr.y + 8);
        if (over > 0 && room > 0) sounds_scroll += over < room ? over : room;
    }
    return y - top;
}

static int generator(int x, int y, int w)
{
    y = section(x, y, w, "1  WHAT KIND OF MUSIC?");
    {
        int cols = 5, gap = 4, bw = (w - gap * (cols - 1)) / cols;
        for (int i = -1; i < NSYNTH_GENRES; i++) {
            int k = i + 1;
            SDL_Rect r = {x + (k % cols) * (bw + gap), y + (k / cols) * 32, bw, 28};
            if (k < 16) gx_genre_r[k] = r;
            if (button(r, i < 0 ? "Any" : SYNTH_GENRES[i], gx_genre == i)) gx_genre = i;
        }
        y += ((NSYNTH_GENRES + 1 + cols - 1) / cols) * 32 + 10;
    }
    y = section(x, y, w, "2  WHAT IS THE SOUND FOR?");
    {
        int cols = 5, gap = 4, bw = (w - gap * (cols - 1)) / cols;
        for (int i = -1; i < NSYNTH_KINDS; i++) {
            int k = i + 1;
            SDL_Rect r = {x + (k % cols) * (bw + gap), y + (k / cols) * 32, bw, 28};
            if (k < 12) gx_kind_r[k] = r;
            if (button(r, i < 0 ? "Anything" : SYNTH_KINDS[i], gx_kind == i)) gx_kind = i;
        }
        y += ((NSYNTH_KINDS + 1 + cols - 1) / cols) * 32 + 2;
        text(x, y, gx_kind < 0 ? "Every kind of sound" : KIND_ABOUT[gx_kind], DIM);
        y += font_h + 14;
    }
    y = section(x, y, w, "3  CLICK A SOUND TO HEAR IT AND SHAPE IT");
    int shown = 0;
    memset(gx_row_r, 0, sizeof gx_row_r);
    if (gx_preset >= 0 && !gx_matches(gx_preset)) {   /* the one you're shaping stays at the top */
        y += gx_row(x, y, w, gx_preset);
        if (gx_preset >= 0) y += gx_shape(x, y, w);
        y += 10;
    }
    for (int i = 0; i < NSYNTH_PRESETS; i++) {
        if (!gx_matches(i)) continue;
        y += gx_row(x, y, w, i);
        if (gx_preset == i) y += gx_shape(x, y, w);
        y += 4;
        shown++;
    }
    if (gx_kind >= 0 && !strcmp(SYNTH_KINDS[gx_kind], "Vocals")) {
        text(x, y + 4, "To sing real words, try the Singer tab.", HEAD);
        y += font_h + 12;
    }
    if (!shown) {
        text(x, y + 4, "Nothing like that yet: pick Any or Anything.", DIM);
        y += font_h + 12;
    }
    y += 8;
    text(x, y, "Sounds you keep go in the Library, ready to drag", DIM);
    text(x, y + font_h + 2, "onto a track.", DIM);
    return y + 2 * font_h + 10;
}

/* ---- Sounds > Singer: type words, pick a tune and a voice, and it sings them. The words are
 * spoken by eSpeak NG and sung through a vocoder (voice.c). */

enum { TUNE_ONE, TUNE_UPDOWN, TUNE_RISE, TUNE_FALL, TUNE_BOUNCE, TUNE_ANSWER, TUNE_CLIP, TUNES };
static const char *const TUNE_NAMES[TUNES] = {"One note", "Up and down", "Rising", "Falling", "Bouncy",
                                              "Call and answer", "From a note clip"};
static const int TUNE_DEGREES[TUNE_CLIP][8] = {
    {0, 0, 0, 0, 0, 0, 0, 0}, {0, 1, 2, 3, 4, 3, 2, 1}, {0, 1, 2, 3, 4, 5, 6, 7},
    {4, 3, 2, 1, 0, 1, 2, 3}, {0, 2, 4, 2, 0, 2, 4, 7}, {0, 0, 2, 4, 4, 2, 1, 0}};
static VocalSpec SV;
static int sv_init, sv_espeak = -1;
static float *sv_heard;
static char sv_err[200];
static SDL_Rect sv_words_r, sv_tune_r[TUNES], sv_sing_r, sv_lib_r, sv_place_r, sv_voice_r[2], sv_style_r[VS_STYLES],
    sv_singer_r[8];

static void sv_defaults(void)
{
    memset(&SV, 0, sizeof SV);
    SV.style = VS_CLEAR;
    SV.singer = 0;
    SV.high = 1;
    SV.vibrato = 0.5;
    SV.volume = 0.8;
    SV.tune = TUNE_UPDOWN;
    SV.step = 0.5;
    SV.first = 60 + th_root;                 /* a high voice, on the key's root */
    sv_init = 1;
}

/* the note `deg` scale steps above (or below) `first` in the Theory key */
static int key_note(int first, int deg)
{
    const Scale *sc = &SCALES[th_scale];
    int rel = ((first - th_root) % 12 + 12) % 12, idx = 0, best = 99;
    for (int k = 0; k < sc->n; k++) {            /* the scale note nearest the start */
        int d = abs(sc->steps[k] - rel);
        if (d < best) {
            best = d;
            idx = k;
        }
    }
    int base = first - rel + sc->steps[idx], k = idx + deg, oct = 0;
    while (k < 0) {
        k += sc->n;
        oct--;
    }
    oct += k / sc->n;
    k %= sc->n;
    return base - sc->steps[idx] + sc->steps[k] + 12 * oct;
}

/* the note clip the "From a note clip" tune uses: the selected one, or else the first */
static int sv_clip(void)
{
    if (selected >= 0 && selected < P.nclips && P.clips[selected].pattern > 0) return selected;
    for (int c = 0; c < P.nclips; c++)
        if (P.clips[c].pattern > 0) return c;
    return -1;
}

/* Fills SV.notes from the words and the tune. Returns how many syllables there are. */
static int sv_make_notes(void)
{
    char syl[VOCAL_NOTES][40];
    int rest[VOCAL_NOTES];
    int n = vocal_syllables(SV.words, syl, rest, VOCAL_NOTES);
    SV.bpm = P.bpm;
    SV.a4 = P.a4;
    SV.nnotes = 0;
    if (SV.tune == TUNE_CLIP) {
        int c = sv_clip();
        if (c < 0) return n;
        const Clip *cl = &P.clips[c];
        int idx[MAX_NOTES], m = 0;
        for (int i = 0; i < P.nnotes && m < MAX_NOTES; i++) {
            const Note *nt = &P.notes[i];
            if (nt->pattern == cl->pattern && nt->start >= cl->offset && nt->start < cl->offset + cl->length) idx[m++] = i;
        }
        for (int i = 1; i < m; i++)              /* in time order */
            for (int j = i; j > 0 && P.notes[idx[j]].start < P.notes[idx[j - 1]].start; j--) {
                int t = idx[j];
                idx[j] = idx[j - 1];
                idx[j - 1] = t;
            }
        double fpb = SR * 60.0 / P.bpm;
        for (int i = 0; i < m && SV.nnotes < VOCAL_NOTES; i++) {
            const Note *nt = &P.notes[idx[i]];
            if (SV.nnotes > 0 && nt->start == P.notes[idx[i - 1]].start) continue;   /* chords: the first note */
            SV.notes[SV.nnotes++] = (VocalNote){nt->note, (nt->start - cl->offset) / fpb, nt->length / fpb};
        }
        return n;
    }
    double t = 0, step = SV.step > 0 ? SV.step : 0.5;
    for (int i = 0; i < n; i++) {
        t += rest[i] * step;
        double len = step;
        if (i == n - 1) len = 2 * step;          /* the last word is held */
        else if (rest[i + 1] == 2) len += step;  /* and the end of each line */
        int deg = TUNE_DEGREES[SV.tune][i % 8];
        SV.notes[SV.nnotes++] = (VocalNote){key_note(SV.first, deg), t, len * 0.95};
        t += len;
    }
    return n;
}

static void sv_name(char *buf, size_t n)
{
    char w[40] = "";
    int k = 0;
    for (const char *p = SV.words; *p && k < 30; p++)    /* the first words */
        if (*p != '-') w[k++] = *p;
    w[k] = 0;
    snprintf(buf, n, "Sung: %s%s", w, strlen(SV.words) > 30 ? "..." : "");
}

static int sv_keep(void)
{
    sv_make_notes();
    char name[96];
    sv_name(name, sizeof name);
    long len;
    sv_err[0] = 0;
    float *d = render_vocal(&SV, &len, sv_err, sizeof sv_err);
    if (!d) {
        status("Couldn't sing that: %s", sv_err);
        return -1;
    }
    int idx = sample_add(&P, name, d, len);
    if (idx < 0) {
        status("Library is full");
        return -1;
    }
    P.samples[idx].vocal = 1;
    P.samples[idx].voc = SV;
    return idx;
}

static void sv_sing(void)
{
    sv_make_notes();
    long len;
    sv_err[0] = 0;
    float *b = render_vocal(&SV, &len, sv_err, sizeof sv_err);
    if (b) {
        play_scratch(b, len);
        sv_heard = b;
        status("Singing %.1f seconds at %.0f BPM", (double)len / SR, P.bpm);
    } else {
        status("Couldn't sing that: %s", sv_err);
    }
}

static int singer(int x, int y, int w)
{
    char buf[400];
    if (!sv_init) sv_defaults();
    if (sv_espeak < 0) sv_espeak = speech_available(NULL, 0);
    if (!sv_espeak) {                            /* what's needed, said plainly */
        int h = 5 * (font_h + 4) + 48;
        SDL_Rect box = {x, y, w, h};
        fill(box, alpha(ACCENT, 40));
        outline(box, ACCENT);
        int ty = y + 10;
        text(x + 12, ty, "Singing needs eSpeak NG, a free speech program.", HEAD);
        ty += font_h + 8;
#ifdef _WIN32
        text(x + 12, ty, "It comes in the espeak-ng folder next to tinydaw.exe:", TEXT);
        text(x + 12, ty + font_h + 4, "unzip tinydaw again to get it back.", TEXT);
#else
        text(x + 12, ty, "Install the espeak-ng package, for example:", TEXT);
        text(x + 12, ty + font_h + 4, "sudo pacman -S espeak-ng   or   sudo apt install espeak-ng", TEXT);
#endif
        ty += 2 * (font_h + 4) + 4;
        if (button((SDL_Rect){x + 12, ty, 140, 28}, "Look again", 0)) {
            sv_espeak = speech_available(NULL, 0);
            status(sv_espeak ? "Found eSpeak NG: ready to sing" : "eSpeak NG still isn't there");
        }
        y += h + 14;
    }

    y = section(x, y, w, "1  TYPE THE WORDS");
    sv_words_r = (SDL_Rect){x, y, w, 32};
    textfield(sv_words_r, SV.words, sizeof SV.words, 1, "click here and type, like: hel-lo world");
    y += 40;
    char syl[VOCAL_NOTES][40];
    int rest[VOCAL_NOTES];
    int n = vocal_syllables(SV.words, syl, rest, VOCAL_NOTES), k = 0;
    if (n) {                                     /* how it will be sung: one piece per note */
        k = snprintf(buf, sizeof buf, "%d note%s:  ", n, n == 1 ? "" : "s");
        for (int i = 0; i < n && k < (int)sizeof buf - 50; i++)
            k += snprintf(buf + k, sizeof buf - k, "%s%s", i ? (rest[i] ? "  /  " : " · ") : "", syl[i]);
        text_clip((SDL_Rect){x, y, w, font_h + 2}, x, y, buf, TEXT);
        y += font_h + 6;
    }
    text(x, y, "Each word gets a note. Split longer words with -", DIM);
    text(x, y + font_h + 2, "to give each part its own: hap-py birth-day.", DIM);
    text(x, y + 2 * (font_h + 2), "Commas and full stops leave a short gap.", DIM);
    y += 3 * (font_h + 2) + 12;

    y = section(x, y, w, "2  PICK A TUNE");
    {
        int cols = 3, gap = 4, bw = (w - gap * (cols - 1)) / cols;
        for (int i = 0; i < TUNES; i++) {
            SDL_Rect r = {x + (i % cols) * (bw + gap), y + (i / cols) * 32, i == TUNE_CLIP ? 2 * bw + gap : bw, 28};
            sv_tune_r[i] = r;
            if (button(r, TUNE_NAMES[i], SV.tune == i)) SV.tune = i;
        }
        y += ((TUNES + cols - 1) / cols) * 32 + 6;
    }
    if (SV.tune == TUNE_CLIP) {
        int c = sv_clip();
        if (c < 0) {
            text(x, y, "No note clips yet: double-click a track to write a", DIM);
            text(x, y + font_h + 2, "melody, then come back.", DIM);
        } else {
            sv_make_notes();
            int m = SV.nnotes;
            snprintf(buf, sizeof buf, "Sings on the %d notes of the %s note clip on track %d", m,
                     c == selected ? "selected" : "first", P.clips[c].track + 1);
            text_clip((SDL_Rect){x, y, w, font_h + 2}, x, y, buf, TEXT);
            text(x, y + font_h + 2, n > m ? "More words than notes: the extra words are left out."
                                          : "Select a different note clip to use that one.", DIM);
        }
        y += 2 * (font_h + 2) + 10;
    } else {
        int hw = (w - 8) / 2;
        note_number((SDL_Rect){x, y, hw, 26}, "Starts on", &SV.first);
        static const double STEPS[3] = {1, 0.5, 0.25};
        static const char *const SPEED[3] = {"Slow", "Medium", "Fast"};
        int sw = (hw - 8) / 3;
        for (int i = 0; i < 3; i++)
            if (button((SDL_Rect){x + hw + 8 + i * (sw + 4), y, sw, 26}, SPEED[i], SV.step == STEPS[i])) SV.step = STEPS[i];
        y += 32;
        snprintf(buf, sizeof buf, "In %s %s: change the key in Theory > Keys", pitch_name(th_root, key_uses_flats(th_root, th_scale)),
                 SCALES[th_scale].name);
        text(x, y, buf, DIM);
        y += font_h + 12;
    }

    y = section(x, y, w, "3  PICK A VOICE");
    {
        int hw = (w - 8) / 2;
        static const int ORDER[VS_STYLES] = {VS_CLEAR, VS_ROBOT, VS_TALKBOX, VS_CHOIR, VS_WHISPER};
        int bw = (w - 16) / VS_STYLES;
        for (int k = 0; k < VS_STYLES; k++) {
            int i = ORDER[k];
            sv_style_r[i] = (SDL_Rect){x + k * (bw + 4), y, k == VS_STYLES - 1 ? w - k * (bw + 4) : bw, 28};
            if (button(sv_style_r[i], VOCAL_STYLES[i], SV.style == i)) SV.style = i;
        }
        y += 32;
        static const char *const ABOUT[VS_STYLES] = {"Robot: a clean electronic voice, like a vocoder",
                                                     "Talkbox: buzzy and funky", "Choir: a robot group, spread wide",
                                                     "Whisper: breath, no pitch",
                                                     "Clear: a singer, smooth and in tune"};
        text(x, y, ABOUT[SV.style], DIM);
        y += font_h + 10;
        if (SV.style == VS_CLEAR) {              /* the singers, by name */
            int cw = (w - 4) / 2;
            for (int i = 0; i < NCLEAR_SINGERS && i < 8; i++) {
                SDL_Rect r = {x + (i % 2) * (cw + 4), y + (i / 2) * (2 * font_h + 18), cw, 2 * font_h + 14};
                sv_singer_r[i] = r;
                int on = SV.singer == i, hov = hit(r) && drag.kind == D_NONE;
                fill(r, on ? tone(ACCENT, 0.45f) : hov ? PANEL3 : PANEL2);
                if (on) fill((SDL_Rect){r.x, r.y, 4, r.h}, ACCENT);
                text(r.x + 12, r.y + 5, CLEAR_SINGERS[i].name, on ? HEAD : TEXT);
                text_clip((SDL_Rect){r.x + 8, r.y, r.w - 12, r.h}, r.x + 12, r.y + 9 + font_h, CLEAR_SINGERS[i].about, DIM);
                if (hov && lpress) {
                    lpress = 0;
                    if (CLEAR_SINGERS[i].high != SV.high) SV.first += CLEAR_SINGERS[i].high ? 12 : -12;   /* their range */
                    SV.high = CLEAR_SINGERS[i].high;
                    SV.singer = i;
                }
            }
            y += ((NCLEAR_SINGERS + 1) / 2) * (2 * font_h + 18) + 6;
        } else {
            static const char *const VOICES[2] = {"Low voice", "High voice"};
            for (int i = 0; i < 2; i++) {
                sv_voice_r[i] = (SDL_Rect){x + i * (hw + 8), y, hw, 28};
                if (button(sv_voice_r[i], VOICES[i], SV.high == i) && SV.high != i) {
                    SV.high = i;
                    SV.first += i ? 12 : -12;    /* and sing an octave up or down */
                }
            }
            y += 38;
        }
        static const char *langs[32];
        for (int i = 0; i < NVOCAL_LANGS && i < 32; i++) langs[i] = VOCAL_LANGS[i][0];
        dropdown((SDL_Rect){x, y, hw, 28}, "Language: ", langs, NVOCAL_LANGS, &SV.lang, 0);
        text(x + hw + 12, y + 6, "Vibrato", TEXT);
        slider((SDL_Rect){x + hw + 12 + text_w("Vibrato") + 14, y, w - hw - 26 - text_w("Vibrato") - 14, 28}, &SV.vibrato);
        y += 40;
    }

    y = section(x, y, w, "4  HEAR IT AND KEEP IT");
    int ready = sv_espeak && n > 0 && (SV.tune != TUNE_CLIP || sv_clip() >= 0);
    int hearing = scratch && scratch == sv_heard && scratch_pos < scratch_len;
    int hw2 = (w - 8) / 2;
    sv_sing_r = (SDL_Rect){x, y, w, 34};
    sv_lib_r = (SDL_Rect){x, y + 40, hw2, 32};
    sv_place_r = (SDL_Rect){x + hw2 + 8, y + 40, w - hw2 - 8, 32};
    if (button2(sv_sing_r, hearing ? "Stop" : "Sing it", 1, ready || hearing)) {
        if (hearing) play_scratch(NULL, 0);
        else sv_sing();
    }
    if (button2(sv_lib_r, "+ Library", 0, ready)) {
        int idx = sv_keep();
        if (idx >= 0) {
            status("Made \"%s\": it's in the library, drag it onto a track", P.samples[idx].name);
            lib_scroll = P.nsamples;
        }
    }
    if (button2(sv_place_r, SV.tune == TUNE_CLIP ? "Put it with the clip" : "Put it at the playhead", 0, ready)) {
        int c = SV.tune == TUNE_CLIP ? sv_clip() : -1;
        int idx = sv_keep();
        if (idx >= 0) {
            long at = c >= 0 ? P.clips[c].start : snap(P.playhead);
            int t = free_track_at(at, P.samples[idx].frames);
            int cl = clip_add(idx, t, at, 0, P.samples[idx].frames);
            if (cl >= 0) {
                selected = cl;
                status("Placed \"%s\" on track %d", P.samples[idx].name, t + 1);
            }
        }
    }
    y += 80;
    if (!n) text(x, y, "Type some words above to start.", DIM);
    else if (!sv_espeak) text(x, y, "Install eSpeak NG (above) to hear it.", DIM);
    else text(x, y, "Change anything and press Sing it again to hear it.", DIM);
    return y + font_h + 12;
}

static void sounds_pane(void)
{
    SDL_Rect pane = panel_rect();
    int saved, inside = panel_begin(pane, &sounds_scroll, sounds_content_h, &saved);
    int x = pane.x + 12, w = pane.w - 24, y0 = pane.y + 10 - (sounds_sub ? sounds_scroll : 0), y = y0;
    static const char *const SUBS[] = {"Library", "Generator", "Singer", "New sound"};
    y = subtabs(x, y, w, SUBS, 4, &sounds_sub);
    if (sounds_sub == 0) {
        library_list(x, y, w, pane.y + pane.h);
        sounds_content_h = 0;                   /* the list scrolls itself */
    } else if (sounds_sub == 1) {
        y = generator(x, y, w - 14);           /* clear of the scrollbar */
        sounds_content_h = y - y0 + 10;
    } else if (sounds_sub == 2) {
        y = singer(x, y, w - 14);
        sounds_content_h = y - y0 + 10;
    } else {
        y = new_sound(x, y, w);
        sounds_content_h = y - y0 + 10;
    }
    panel_end(pane, &sounds_scroll, sounds_content_h, inside, saved);
}

/* ---- Settings */

static char audio_out[256];             /* output device name, "" = the system default */
static int settings_scroll, settings_content_h;
static void settings_save(void);
static void theme_follow(void);

static int settings_content(int x, int y, int w)
{
    char buf[1200];
    static const char *const SUBS[] = {"Look", "Audio", "Input", "Keys", "About"};
    y = subtabs(x, y, w, SUBS, 5, &settings_sub);
    int tw = (w - 8) / 3;
    switch (settings_sub) {
    case 0: {
        y = section(x, y, w, "THEME");
        static const char *const T[THEME_COUNT] = {"Dark", "Light", "System", "Windows XP", "Frutiger Aero", "Blender"};
        for (int i = 0; i < THEME_COUNT; i++)
            if (button((SDL_Rect){x + (i % 3) * (tw + 4), y + (i / 3) * 36, tw, 30}, T[i], theme_choice == i)) {
                theme_choice = i;
                if (i == THEME_SYSTEM) theme_follow();
                else theme_apply(i);
                settings_save();
            }
        y += 76;
        const char *const ABOUT[THEME_COUNT] = {
            "Dark: the original look.", "Light: the same layout on white.",
            theme_light ? "Following your desktop: light right now." : "Following your desktop: dark right now.",
            "Windows XP: blue title bar, beige windows, green for on.",
            "Frutiger Aero: glossy glass, sky and aqua, bubbles.", "Blender: rounded grey widgets, blue and orange."};
        text(x, y, ABOUT[theme_choice], DIM);
        y += font_h + 16;
        y = section(x, y, w, "PREVIEW");
        for (int t = 0; t < 8; t++) {             /* the track colours on this background */
            SDL_Rect c = {x + t * (w / 8), y, w / 8 - 4, 34};
            fill(c, wash(TRACK_COL[t], 0.42f));
            fill((SDL_Rect){c.x, c.y, c.w, 10}, wash(TRACK_COL[t], 0.78f));
        }
        y += 46;
        text(x, y, "The full-screen show is always dark.", DIM);
        y += font_h + 10;
        break;
    }
    case 1: {
        y = section(x, y, w, "OUTPUT");
        {                                       /* the system default, or a particular device */
            static char names[DD_MAX][48];
            static const char *ptrs[DD_MAX];
            static int pick;
            int n = SDL_GetNumAudioDevices(0), m = 1;
            snprintf(names[0], sizeof names[0], "System default");
            ptrs[0] = names[0];
            int cur = 0;
            for (int i = 0; i < n && m < DD_MAX; i++) {
                const char *name = SDL_GetAudioDeviceName(i, 0);
                if (!name) continue;
                if (!strcmp(audio_out, name)) cur = m;
                snprintf(names[m], sizeof names[m], "%s", name);
                ptrs[m] = names[m];
                m++;
            }
            if (!(dd.open && dd.sel == &pick)) pick = cur;
            if (dropdown((SDL_Rect){x, y, w, 30}, "", ptrs, m, &pick, 0)) {
                if (pick == 0) audio_out[0] = 0;
                else snprintf(audio_out, sizeof audio_out, "%s", names[pick]);
                open_audio();
                settings_save();
            }
            y += 42;
        }
        y = section(x, y, w, "PROJECT");
        text(x, y + 6, "Sample rate", DIM);
        y += 28;
        static const char *const RL[6] = {"44.1k", "48k", "88.2k", "96k", "176.4k", "192k"};
        int rw = (w - 5 * 4) / 6;
        for (int i = 0; i < 6; i++)
            if (button((SDL_Rect){x + i * (rw + 4), y, rw, 28}, RL[i], SR == PROJECT_RATES[i]) && SR != PROJECT_RATES[i])
                set_rate(PROJECT_RATES[i]);
        y += 36;
        fnumber((SDL_Rect){x, y, w, 26}, "Master volume", &P.master, 0, 1.5, 0.004, "%.2f");
        y += 36;
        text(x, y, "The rate is saved with the project; higher rates keep", DIM);
        text(x, y + font_h + 2, "more detail but use more memory and processor.", DIM);
        y += 2 * font_h + 12;
        break;
    }
    case 2:
        y = section(x, y, w, "DEVICES");
        y = devices_section(x, y, w);
        y += 10;
        y = section(x, y, w, "MIDI");
        if (button((SDL_Rect){x, y, w, 28}, inst_midi ? "MIDI keys play the instrument: on" : "MIDI keys play the instrument: off",
                   inst_midi)) {
            inst_midi = !inst_midi;
            binds_changed = 1;
        }
        y += 34;
        if (button((SDL_Rect){x, y, w, 28}, "Stop MIDI notes triggering pads", 0)) {
            LOCK();
            for (int i = 0; i < NPADS; i++) binds[i].midi = -1;
            UNLOCK();
            binds_changed = 1;
            status("MIDI notes no longer trigger pads: every key plays the instrument");
        }
        y += 38;
        text(x, y, "Pad bindings belong to your hardware, so they're kept", DIM);
        text(x, y + font_h + 2, "for every project. Change them in Pads > Learn input.", DIM);
        y += 2 * font_h + 16;
        y = section(x, y, w, "CONTROLLER");
        text(x, y + 6, "Pitch bend", TEXT);
        {
            int lw = text_w("Pitch bend") + 14;
            if (dropdown((SDL_Rect){x + lw, y, w - lw, 28}, "", JB_NAMES, JB_COUNT, &joy_bend, 0)) binds_changed = 1;
        }
        y += 36;
        text(x, y, "Push the stick to bend pads and the instrument like a", DIM);
        text(x, y + font_h + 2, "pitch wheel; it springs back when you let go. Set how", DIM);
        text(x, y + 2 * (font_h + 2), "far it bends with Bend range in Mixer > Live.", DIM);
        y += 3 * (font_h + 2) + 12;
        break;
    case 3: {
        static const char *const KEYS[][2] = {
            {"Space", "play / stop"}, {"Home / End", "start / end of the song"},
            {"Ctrl+Left / Right", "playhead one bar"}, {"Ctrl+Shift+Left / Right", "playhead one beat"},
            {"Alt+Left / Right", "previous / next clip edge"}, {"F", "show the playhead"}, {"Z", "zoom to fit"},
            {"M", "metronome on/off"}, {"T", "tap tempo"}, {"L", "loop on/off"},
            {"Ctrl+Z / Ctrl+Shift+Z", "undo / redo"}, {"Ctrl+S, Ctrl+O, Ctrl+N", "save, open, new"},
            {"Ctrl+E", "export"}, {"Ctrl+1 .. Ctrl+6", "open a panel"}, {"Ctrl+B", "hide / show the panel"},
            {"Ctrl+,", "settings"}, {"F11", "full-screen show"}, {"Ctrl+A, Esc", "select all, none"},
            {"Shift+click, drag a box", "select several"}, {"Delete", "delete selected"},
            {"Ctrl+C / X / V", "copy, cut, paste"}, {"Ctrl+D", "duplicate"}, {"Arrows", "nudge selected"},
            {"[ and ]", "transpose selected"}, {"Ctrl+R", "reverse selected"}, {"Alt+wheel", "clip gain"},
            {"Ctrl+wheel, + / -", "zoom"}, {"Shift (dragging)", "no snapping"}, {"Page Up / Down", "scroll a panel"},
            {"Double-click a track", "new note clip"}, {"Double-click / Enter", "open a note clip"},
            {"A (track header)", "automation lane"}, {"Roll: click, drag", "add, move a note"},
            {"Roll: drag a note's end", "stretch it"}, {"Roll: right-click", "delete a note"},
            {"Roll: Up / Down", "transpose (Shift: octave)"}, {"Roll: Shift+drag", "select with a box"},
            {"Roll: Ctrl+drag, Ctrl+D", "copy notes"}, {"Roll: wheel", "up and down the keys"}};
        for (size_t i = 0; i < sizeof KEYS / sizeof *KEYS; i++) {
            text(x, y, KEYS[i][0], TEXT);
            text(x + w / 2, y, KEYS[i][1], DIM);
            y += font_h + 7;
        }
        y += 6;
        break;
    }
    case 4: {
        snprintf(buf, sizeof buf, "tinydaw %s", TINYDAW_VERSION);
        text(x, y, buf, TEXT);
        y += font_h + 4;
        text(x, y, "A small timeline sequencer.", DIM);
        y += font_h + 16;
        y = section(x, y, w, "WHERE THINGS GO");
        char dir[900];
        projects_dir(dir, sizeof dir);
        const char *rows[][2] = {{"Projects", dir}, {"Pad inputs", binds_file()}, {"Autosave", autosave_file()}};
        for (int i = 0; i < 3; i++) {
            text(x, y, rows[i][0], DIM);
            y += font_h + 2;
            text_clip((SDL_Rect){x, y, w, font_h + 2}, x, y, rows[i][1], TEXT);
            y += font_h + 8;
        }
        y += 6;
        y = section(x, y, w, "BUILT WITH");
        text(x, y, "SDL2, SDL2_ttf, libsndfile, libsamplerate,", DIM);
        text(x, y + font_h + 2, "ALSA (MIDI on Linux), DejaVu fonts.", DIM);
        y += 2 * font_h + 10;
        text(x, y, "github.com/WaywardSkylark/tinydaw", TEXT);
        y += font_h + 10;
        break;
    }
    }
    return y;
}

static void settings_pane(void)
{
    SDL_Rect pane = panel_rect();
    int saved, inside = panel_begin(pane, &settings_scroll, settings_content_h, &saved);
    int y0 = pane.y + 10 - settings_scroll;
    int y = settings_content(pane.x + 14, y0, pane.w - 28);
    settings_content_h = y - y0 + 12;
    panel_end(pane, &settings_scroll, settings_content_h, inside, saved);
}

/* ---- settings that belong to this computer (theme, output device), not to a project */

static const char *settings_file(void)
{
    static char path[1024];
    if (!path[0]) {
        char dir[900];
        user_dir("config", dir, sizeof dir);
        ensure_dir(dir);
        snprintf(path, sizeof path, "%s/settings.conf", dir);
    }
    return path;
}

static void settings_save(void)
{
    if (headless) return;
    FILE *f = fopen(settings_file(), "w");
    if (!f) return;
    fprintf(f, "# tinydaw settings\ntheme %d\naudio_output %s\npanel %d\n", theme_choice, audio_out, last_panel);
    fprintf(f, "metronome %d %d %d %d %d %d %.3f\n", MET.on, MET.sound, MET.sub, MET.accent, MET.countin,
            MET.countin_rec_only, MET.volume);
    fprintf(f, "show %d %d %.0f\n", show_scene, show_cycle, show_each);
    fclose(f);
}

static void settings_load(void)
{
    FILE *f = fopen(settings_file(), "r");
    if (!f) return;
    char line_[400];
    while (fgets(line_, sizeof line_, f)) {
        line_[strcspn(line_, "\r\n")] = 0;
        int v;
        if (sscanf(line_, "theme %d", &v) == 1 && v >= 0 && v < THEME_COUNT) theme_choice = v;
        else if (!strncmp(line_, "audio_output ", 13)) snprintf(audio_out, sizeof audio_out, "%.255s", line_ + 13);
        else if (sscanf(line_, "panel %d", &v) == 1 && v >= 0 && v < RI_COUNT) last_panel = v;
        else if (!strncmp(line_, "show ", 5)) {
            int sc, cy;
            double each;
            if (sscanf(line_, "show %d %d %lf", &sc, &cy, &each) == 3 && sc >= 0 && sc <= SCENE_COUNT) {
                show_scene = sc;
                show_cycle = cy != 0;
                show_each = each < 5 ? 5 : each > 600 ? 600 : each;
            }
        } else if (!strncmp(line_, "metronome ", 10)) {
            int on, so, sub, ac, ci, ro;
            float vol;
            if (sscanf(line_, "metronome %d %d %d %d %d %d %f", &on, &so, &sub, &ac, &ci, &ro, &vol) == 7 &&
                so >= 0 && so < CLICK_SOUNDS && (sub == 1 || sub == 2 || sub == 4) && ci >= 0 && ci <= 2)
                MET = (typeof(MET)){on, so, sub, ac, ci, ro, vol < 0 ? 0 : vol > 1.5f ? 1.5f : vol};
        }
    }
    fclose(f);
}

/* ---- following the desktop's dark/light preference (asked on a thread: it runs a program) */

static SDL_atomic_t theme_probe;        /* 0 idle/unknown, 1 dark, 2 light, 3 asking */

static int ask_desktop_theme(void)       /* 1 dark, 2 light, 0 unknown */
{
#ifdef _WIN32
    DWORD v = 1, sz = sizeof v;
    if (RegGetValueA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     "AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &v, &sz) == ERROR_SUCCESS)
        return v ? 2 : 1;
    return 0;
#else
    char out[256] = "";
    /* the desktop portal works on GNOME, KDE, Cinnamon and others: 1 = dark, 2 = light */
    FILE *p = popen("timeout 2 gdbus call --session --dest org.freedesktop.portal.Desktop "
                    "--object-path /org/freedesktop/portal/desktop "
                    "--method org.freedesktop.portal.Settings.ReadOne org.freedesktop.appearance color-scheme "
                    "2>/dev/null", "r");
    if (p) {
        if (!fgets(out, sizeof out, p)) out[0] = 0;
        pclose(p);
        char *u = strstr(out, "uint32 ");
        if (u && (u[7] == '1' || u[7] == '2')) return u[7] - '0';
    }
    p = popen("timeout 2 gsettings get org.gnome.desktop.interface gtk-theme 2>/dev/null", "r");
    if (p) {                                     /* otherwise go by the theme's name */
        out[0] = 0;
        if (!fgets(out, sizeof out, p)) out[0] = 0;
        pclose(p);
        if (out[0]) return strstr(out, "dark") || strstr(out, "Dark") ? 1 : 2;
    }
    return 0;
#endif
}

static int theme_thread(void *arg)
{
    (void)arg;
    int t = ask_desktop_theme();
    SDL_AtomicSet(&theme_probe, t ? t : 1);
    return 0;
}

static void theme_follow(void)          /* ask again (at start, when the window comes back to front) */
{
    if (theme_choice != THEME_SYSTEM || SDL_AtomicGet(&theme_probe) == 3) return;
    SDL_AtomicSet(&theme_probe, 3);
    SDL_Thread *t = SDL_CreateThread(theme_thread, "theme", NULL);
    if (t) SDL_DetachThread(t);
    else SDL_AtomicSet(&theme_probe, 1);
}

static void theme_poll(void)
{
    int t = SDL_AtomicGet(&theme_probe);
    if (t != 1 && t != 2) return;
    SDL_AtomicSet(&theme_probe, 0);
    if (theme_choice == THEME_SYSTEM && (t == 2) != theme_light) theme_apply(t == 2);
}

/* ---------------------------------------------------------------- panels */

static void top_bar(void)
{
    char buf[96];
    Col bar_text = ST.shaped ? ST.bar_text : TEXT, bar_dim = ST.shaped ? ST.bar_dim : DIM;
    Col bar_head = ST.shaped && theme_kind != THEME_BLENDER ? ST.bar_text : HEAD;
    Col sep = ST.shaped ? alpha(ST.bar_dim, 110) : LINE;
    if (ST.shaped) {                             /* the theme's title bar */
        shape_fill((SDL_Rect){0, 0, W, L.top}, ST.bar_top, ST.bar_bot, 0, ST.bar_gloss);
        line(0, 0, W, 0, ST.bar_hi);
        if (ST.bubbles)                          /* Aero: a few soft bubbles drifting in the glass */
            for (int i = 0; i < 9; i++) {
                int bx = (int)(fmod(i * 397.0 + SDL_GetTicks() * 0.004 * (1 + i % 3), W + 80)) - 40;
                int by = 8 + (i * 13) % 30, br = 4 + (i * 7) % 9;
                ring(bx, by, br, alpha(ST.bar_hi, 110));
                disc(bx - br / 3, by - br / 3, br / 4 + 1, alpha(ST.bar_hi, 140));
            }
        line(0, L.top - 1, W, L.top - 1, theme_kind == THEME_BLENDER ? rgb(30, 30, 30) : tone(ST.bar_bot, 0.7f));
    } else {
        fill((SDL_Rect){0, 0, W, L.top}, PANEL);
        line(0, L.top - 1, W, L.top - 1, LINE);
    }
    int x = 10, y = 9, h = 30;
    file_btn = (SDL_Rect){x, y, 44, h};          /* the menu: files, export, settings, show */
    if (button(file_btn, "", menu_open)) menu_open = !menu_open;
    for (int i = 0; i < 3; i++) fill((SDL_Rect){x + 13, y + 9 + i * 5, 18, 2}, btn_fg(menu_open));
    x += 54;
    if (button2((SDL_Rect){x, y, 54, h}, "Undo", 0, nundo > 0 || !state_same(&committed))) undo_redo(0);
    x += 58;
    if (button2((SDL_Rect){x, y, 54, h}, "Redo", 0, nredo > 0)) undo_redo(1);
    x += 66;
    line(x - 6, y + 4, x - 6, y + h - 4, sep);
    if (button((SDL_Rect){x, y, 72, h}, P.playing ? "Stop" : "Play", P.playing)) toggle_play();
    x += 76;
    if (button((SDL_Rect){x, y, 62, h}, "Loop", P.loop)) toggle_loop();
    x += 72;
    line(x - 6, y + 4, x - 6, y + h - 4, sep);
    bpm_rect = (SDL_Rect){x, y, 100, h};
    {
        double before = P.bpm;
        number(bpm_rect, "BPM", &P.bpm, 40, 300, 0.25, "%.0f");
        tempo_touch(before);
    }
    x += 104;
    a4_rect = (SDL_Rect){x, y, 116, h};
    number(a4_rect, "A4", &P.a4, 415, 466, 0.05, "%.1f Hz");   /* tuning for new sounds */
    x += 120;
    snap_rect = (SDL_Rect){x, y, 110, h};
    dropdown(snap_rect, "Snap ", SNAP_NAMES, 6, &snap_i, 1);   /* click: next, arrow: list */
    x += 118;
    if (button((SDL_Rect){x, y, 62, h}, "Click", MET.on)) {
        MET.on = !MET.on;
        settings_save();
    }
    met_btn = (SDL_Rect){x + 64, y, 22, h};
    if (button(met_btn, "", met_open)) met_open = !met_open;
    for (int i = 0; i < 4; i++)                     /* a small down arrow */
        line(met_btn.x + 7 + i, y + 13 + i, met_btn.x + 15 - i, y + 13 + i, btn_fg(met_open));
    x += 92;
    {                                               /* the beat: four lights, the first is the bar */
        double bt = countin_left > 0 ? (double)(countin_total - countin_left) / beat_frames()
                                     : (double)P.playhead / beat_frames();
        int beat = (int)floor(bt) % 4, lit = P.playing && bt - floor(bt) < 0.35;
        for (int i = 0; i < 4; i++) {
            SDL_Rect d = {x + i * 12, y + 11, 8, 8};
            fill(d, lit && i == beat ? (i == 0 ? ACCENT : HEAD) : PANEL3);
        }
        if (countin_left > 0) {
            snprintf(buf, sizeof buf, "%d", (int)((countin_left - 1) / beat_frames()) + 1);
            text(x + 52, y + (h - font_h) / 2, buf, ACCENT);
        }
    }
    x += 70;
    line(x - 6, y + 4, x - 6, y + h - 4, sep);

    double secs = (double)P.playhead / SR, beats = secs * P.bpm / 60.0;
    snprintf(buf, sizeof buf, "bar %d  beat %.2f", (int)(beats / 4) + 1, fmod(beats, 4.0) + 1);
    text(x + 6, y + (h - font_h) / 2, buf, bar_head);
    x += text_w("bar 000  beat 0.00") + 26;
    snprintf(buf, sizeof buf, "%02d:%06.3f", (int)(secs / 60), fmod(secs, 60.0));
    text(x, y + (h - font_h) / 2, buf, bar_text);
    x += text_w("00:00.000") + 16;

    if (W - 110 > x) {                          /* master level */
        meter_draw((SDL_Rect){W - 100, y + 6, 88, 7}, &master_meter[0], 0);
        meter_draw((SDL_Rect){W - 100, y + 17, 88, 7}, &master_meter[1], 0);
        snprintf(buf, sizeof buf, "%.80s%s", proj_path[0] ? base_name(proj_path) : "untitled", is_dirty() ? " *" : "");
        int pw = W - 116 - x;                    /* and the project's name, when there's room */
        if (pw > 60) text_clip((SDL_Rect){x, y, pw, h}, W - 116 - text_w(buf) > x ? W - 116 - text_w(buf) : x,
                               y + (h - font_h) / 2, buf, bar_dim);
    }
}


/* ---------------------------------------------------------------- automation lanes */

static float auto_y_to_v(SDL_Rect r, int y)
{
    float v = 1 - (float)(y - r.y - 6) / (float)(r.h - 12);
    return v < 0 ? 0 : v > 1 ? 1 : v;
}
static int auto_v_to_y(SDL_Rect r, float v) { return r.y + 6 + (int)lroundf((1 - v) * (r.h - 12)); }

static void auto_text(int param, float v, char *buf, size_t n)
{
    float x = auto_to_value(param, v);
    if (param == AUTO_PAN) pan_text(x, buf, n);
    else if (param == AUTO_CUTOFF) snprintf(buf, n, x >= 1000 ? "%.1f kHz" : "%.0f Hz", x >= 1000 ? x / 1000 : x);
    else snprintf(buf, n, "%.0f%%", x * 100);
}

static float track_base(int t, int param)       /* what the track is set to without automation */
{
    const Track *tr = &P.tracks[t];
    switch (param) {
    case AUTO_VOLUME: return tr->volume;
    case AUTO_PAN: return tr->pan;
    case AUTO_CUTOFF: return tr->filter ? tr->cutoff : 20000;
    case AUTO_DELAY: return tr->delay_send;
    default: return tr->reverb_send;
    }
}

static void auto_status(int i)
{
    if (i < 0 || i >= P.nautos) return;
    const AutoPoint *a = &P.autos[i];
    char v[32];
    auto_text(a->param, a->v, v, sizeof v);
    double beats = (double)a->at / beat_frames();
    status("%s: %s %s at bar %d beat %.2f", P.tracks[a->track].name, AUTO_NAMES[a->param], v, (int)(beats / 4) + 1,
           fmod(beats, 4) + 1);
}

static int auto_add(int t, int param, long at, float v)   /* returns the new point's index */
{
    if (P.nautos >= MAX_AUTO) {
        status("Automation point limit reached (%d)", MAX_AUTO);
        return -1;
    }
    LOCK();
    P.autos[P.nautos++] = (AutoPoint){t, param, at, v};
    auto_sort(&P);
    UNLOCK();
    int first, n = auto_lane(&P, t, param, &first);
    for (int i = first + n - 1; i >= first; i--)      /* the last of equals: it was added after them */
        if (P.autos[i].at == at && P.autos[i].v == v) return i;
    return -1;
}

static void auto_delete(int i)
{
    LOCK();
    memmove(&P.autos[i], &P.autos[i + 1], (size_t)(P.nautos - i - 1) * sizeof(AutoPoint));
    P.nautos--;
    UNLOCK();
}

static void auto_clear(int t, int param)
{
    int first, n = auto_lane(&P, t, param, &first);
    LOCK();
    memmove(&P.autos[first], &P.autos[first + n], (size_t)(P.nautos - first - n) * sizeof(AutoPoint));
    P.nautos -= n;
    UNLOCK();
}

static int auto_point_at(int t, int param)       /* the point under the mouse, or -1 */
{
    SDL_Rect r = auto_rect(t);
    int first, n = auto_lane(&P, t, param, &first), best = -1, bd = 1 << 30;
    for (int i = first; i < first + n; i++) {
        int dx = abs(mx - (int)lround(frame_to_x(P.autos[i].at))), dy = abs(my - auto_v_to_y(r, P.autos[i].v));
        if (dx <= 6 && dy <= 6 && dx + dy < bd) {
            bd = dx + dy;
            best = i;
        }
    }
    return best;
}

/* the lane's header, under the track's: what it automates, the value now, and Clear */
static void auto_header(int t, int y)
{
    SDL_Rect hr = {L.left, y, L.head, L.auto_h};
    fill(hr, tone(PANEL, theme_light ? 0.96f : 0.88f));
    fill((SDL_Rect){L.left, y, 4, L.auto_h}, wash(TRACK_COL[t], 0.55f));
    line(L.left, y + L.auto_h - 1, L.left + L.head, y + L.auto_h - 1, LINE);
    static char names[MAX_TRACKS][AUTO_PARAMS][40];
    static const char *ptr[MAX_TRACKS][AUTO_PARAMS];
    for (int p = 0; p < AUTO_PARAMS; p++) {
        int np = auto_lane(&P, t, p, NULL);
        if (np) snprintf(names[t][p], sizeof names[t][p], "%s (%d)", AUTO_NAMES[p], np);
        else snprintf(names[t][p], sizeof names[t][p], "%s", AUTO_NAMES[p]);
        ptr[t][p] = names[t][p];
    }
    dropdown((SDL_Rect){L.left + 10, y + 5, L.head - 18, 24}, "", ptr[t], AUTO_PARAMS, &auto_param[t], 0);
    int pa = auto_param[t], has;
    float v = auto_at(&P, t, pa, P.playhead, &has);
    char val[32], buf[48];
    auto_text(pa, has ? v : value_to_auto(pa, track_base(t, pa)), val, sizeof val);
    snprintf(buf, sizeof buf, "%s%s", val, has ? "" : " (set)");
    int ry = y + 33;
    text_clip((SDL_Rect){L.left + 10, ry, L.head - 70, font_h + 4}, L.left + 12, ry + 2, buf, has ? TEXT : DIM);
    if (has && button((SDL_Rect){L.left + L.head - 60, ry, 52, 20}, "Clear", 0)) {
        auto_clear(t, pa);
        status("Cleared %s automation on %s. Ctrl+Z brings it back", AUTO_NAMES[pa], P.tracks[t].name);
    }
}

static void auto_lane_draw(int t)
{
    SDL_Rect r = auto_rect(t);
    int pa = auto_param[t], first, n = auto_lane(&P, t, pa, &first);
    Col col = theme_light ? shade(TRACK_COL[t], 0.7f) : TRACK_COL[t];
    fill(r, tone(BG, theme_light ? 0.95f : 0.78f));
    line(r.x, r.y, W, r.y, tone(LINE, 0.9f));
    line(r.x, r.y + r.h - 1, W, r.y + r.h - 1, LINE);
    if (pa == AUTO_VOLUME || pa == AUTO_PAN) {    /* a guide at unity volume or centre pan */
        int gy = auto_v_to_y(r, value_to_auto(pa, pa == AUTO_VOLUME ? 1.0f : 0.0f));
        for (int x = r.x; x < W; x += 6) line(x, gy, x + 2, gy, alpha(DIM, 120));
    }
    if (!n) {
        int y = auto_v_to_y(r, value_to_auto(pa, track_base(t, pa)));
        for (int x = r.x; x < W; x += 10) line(x, y, x + 5, y, alpha(col, 150));
        text(r.x + 8, r.y + 6, "Click to add a point. Drag points to move them, right-click removes one", DIM);
        return;
    }
    int prev = -1;
    for (int x = r.x; x < W; x++) {
        double f = x_to_frame_raw(x);
        float v = auto_at(&P, t, pa, f < 0 ? 0 : (long)f, NULL);
        int y = auto_v_to_y(r, v);
        line(x, y, x, r.y + r.h - 2, alpha(col, 38));
        if (prev >= 0) {
            line(x - 1, prev, x, y, col);
            line(x - 1, prev + 1, x, y + 1, col);
        }
        prev = y;
    }
    int hov = drag.kind == D_NONE && auto_lane_at(my) == t ? auto_point_at(t, pa) : -1;
    for (int i = first; i < first + n; i++) {
        int x = (int)lround(frame_to_x(P.autos[i].at)), y = auto_v_to_y(r, P.autos[i].v);
        if (x < r.x - 6 || x > W + 6) continue;
        int big = i == hov || (drag.kind == D_AUTO && drag.clip == i);
        fill((SDL_Rect){x - 4 - big, y - 4 - big, 9 + 2 * big, 9 + 2 * big}, big ? TEXT : col);
        outline((SDL_Rect){x - 4 - big, y - 4 - big, 9 + 2 * big, 9 + 2 * big}, DARK);
    }
}

/* ---------------------------------------------------------------- note clips */

static int black_key(int note) { return (0x54A >> (note % 12)) & 1; }

static void draw_clip_notes(const Clip *c, SDL_Rect r, int body_y, int body_h, Col col)
{
    int lo = 127, hi = 0, any = 0;
    for (int i = 0; i < P.nnotes; i++)
        if (P.notes[i].pattern == c->pattern) {
            any = 1;
            if (P.notes[i].note < lo) lo = P.notes[i].note;
            if (P.notes[i].note > hi) hi = P.notes[i].note;
        }
    if (!any) {
        if (r.w > 120 && body_h > font_h) text_clip(r, r.x + 6, body_y + (body_h - font_h) / 2, "double-click to add notes", DARK);
        return;
    }
    if (hi - lo < 11) {                           /* a little headroom when the notes are close together */
        lo = (lo + hi) / 2 - 6;
        hi = lo + 11;
    }
    float rowh = (float)(body_h - 4) / (hi - lo + 1);
    long origin = c->start - c->offset, end = c->start + c->length;
    for (int i = 0; i < P.nnotes; i++) {
        const Note *n = &P.notes[i];
        if (n->pattern != c->pattern) continue;
        long a = origin + n->start, b = a + n->length;
        if (a < c->start) a = c->start;
        if (b > end) b = end;
        if (a >= b) continue;
        int x0 = (int)frame_to_x(a), x1 = (int)frame_to_x(b);
        if (x1 < L.tl_x || x0 > W) continue;
        int y = body_y + 2 + (int)((hi - n->note) * rowh), h = rowh > 3 ? (int)rowh - 1 : 2;
        fill((SDL_Rect){x0, y, x1 - x0 > 2 ? x1 - x0 - 1 : 2, h}, col);
    }
}

static int note_add(int pattern, int note, long start, long len, float vel)
{
    if (P.nnotes >= MAX_NOTES) {
        status("Note limit reached (%d)", MAX_NOTES);
        return -1;
    }
    LOCK();
    P.notes[P.nnotes] = (Note){pattern, note, start, len, vel};
    P.nnotes++;
    UNLOCK();
    nsel[P.nnotes - 1] = 0;
    return P.nnotes - 1;
}

static void note_delete(int i)
{
    LOCK();
    memmove(&P.notes[i], &P.notes[i + 1], (size_t)(P.nnotes - i - 1) * sizeof(Note));
    P.nnotes--;
    UNLOCK();
    memmove(&nsel[i], &nsel[i + 1], (size_t)(P.nnotes - i));
    nsel[P.nnotes] = 0;
}

/* ---------------------------------------------------------------- the piano roll */

static SDL_Rect roll_grid, roll_keys, roll_vel, roll_ruler;
static int roll_voice = -1;             /* the note being heard while clicking or dragging */
static struct { long start, length; int note; } norig[MAX_NOTES];
static unsigned char nband_base[MAX_NOTES];

static void roll_open(int c)
{
    if (c < 0 || c >= P.nclips || P.clips[c].pattern <= 0) return;
    if (roll_clip < 0) {
        roll_saved_view = view_start;
        roll_saved_pps = pps;
    }
    roll_clip = c;
    roll_pattern = P.clips[c].pattern;
    memset(nsel, 0, sizeof nsel);
    roll_top = -1;                              /* centred on the notes when first drawn */
    const Clip *cl = &P.clips[c];
    double secs = (double)cl->length / SR;
    pps = (W - L.tl_x - 60) / (secs > 0.5 ? secs : 0.5);
    if (pps > 2000) pps = 2000;
    view_start = (double)cl->start / SR - 20 / pps;
    if (view_start < 0) view_start = 0;
    sel_only(c);
    status("Piano roll: click to add a note, drag to move, drag its end to stretch, right-click deletes. Esc closes");
}

static void roll_quiet(void)
{
    if (roll_voice < 0) return;
    LOCK();
    voice_stop(&P, -1, 1000 + roll_voice);
    UNLOCK();
    roll_voice = -1;
}

static void roll_close(void)
{
    roll_quiet();
    roll_clip = -1;
    view_start = roll_saved_view;
    pps = roll_saved_pps;
    memset(nsel, 0, sizeof nsel);
}

static void roll_hear(int note)                 /* plays a note through the clip's sound and track */
{
    if (roll_clip < 0 || note < 0 || note > 127) return;
    const Clip *c = &P.clips[roll_clip];
    roll_quiet();
    LOCK();
    int v = voice_start(&P, c->sample, pow(2.0, (note - c->root) / 12.0), 0.8f, 0, -1, 1000 + note);
    if (v >= 0) P.voices[v].track = c->track;
    UNLOCK();
    roll_voice = note;
}

static void roll_check(void)                    /* undo or a deletion can take the clip away */
{
    if (roll_clip < 0) return;
    if (roll_clip < P.nclips && P.clips[roll_clip].pattern == roll_pattern) return;
    for (int i = 0; i < P.nclips; i++)
        if (P.clips[i].pattern == roll_pattern) {
            roll_clip = i;
            return;
        }
    roll_close();
}

static long roll_origin(void) { return P.clips[roll_clip].start - P.clips[roll_clip].offset; }

static SDL_Rect note_rect(int i)
{
    const Note *n = &P.notes[i];
    int x0 = (int)floor(frame_to_x(roll_origin() + n->start)), x1 = (int)ceil(frame_to_x(roll_origin() + n->start + n->length));
    return (SDL_Rect){x0, roll_grid.y + (roll_top - n->note) * roll_kh + 1, x1 - x0 < 3 ? 3 : x1 - x0, roll_kh - 1};
}

static int roll_note_at(void)                   /* the note under the mouse, or -1 */
{
    for (int i = P.nnotes - 1; i >= 0; i--)
        if (P.notes[i].pattern == roll_pattern && hit(note_rect(i))) return i;
    return -1;
}

static int roll_row_note(int y) { return roll_top - (y - roll_grid.y) / roll_kh; }

static int nsel_count(void)
{
    int n = 0;
    for (int i = 0; i < P.nnotes; i++) n += nsel[i] && P.notes[i].pattern == roll_pattern;
    return n;
}

static void nsel_only(int i)
{
    memset(nsel, 0, sizeof nsel);
    if (i >= 0) nsel[i] = 1;
}

static void roll_fit_clip(void)                 /* the clip grows (by bars) to take in every note */
{
    Clip *c = &P.clips[roll_clip];
    long end = roll_origin() + pattern_end(&P, roll_pattern), bar = 4 * beat_frames();
    if (end <= c->start + c->length) return;
    long len = ((end - c->start + bar - 1) / bar) * bar;
    LOCK();
    c->length = len;
    UNLOCK();
}

static int roll_delete_selected(void)
{
    int n = 0;
    for (int i = P.nnotes - 1; i >= 0; i--)
        if (nsel[i] && P.notes[i].pattern == roll_pattern) {
            note_delete(i);
            n++;
        }
    return n;
}

static void roll_duplicate_selected(void)       /* copies straight after the selection */
{
    long a = LONG_MAX, b = 0;
    int idx[MAX_NOTES], n = 0;
    for (int i = 0; i < P.nnotes; i++)
        if (nsel[i] && P.notes[i].pattern == roll_pattern) {
            idx[n++] = i;
            if (P.notes[i].start < a) a = P.notes[i].start;
            if (P.notes[i].start + P.notes[i].length > b) b = P.notes[i].start + P.notes[i].length;
        }
    if (!n) return;
    long bf = beat_frames(), shift = ((b - a + bf - 1) / bf) * bf;     /* whole beats keep it in time */
    memset(nsel, 0, sizeof nsel);
    for (int k = 0; k < n; k++) {
        Note o = P.notes[idx[k]];
        int j = note_add(o.pattern, o.note, o.start + shift, o.length, o.vel);
        if (j >= 0) nsel[j] = 1;
    }
    roll_fit_clip();
    status("Copied %d note%s", n, n == 1 ? "" : "s");
}

static void roll_make_unique(void)              /* this clip gets its own copy of the notes */
{
    int old = roll_pattern, id = pattern_new_id(&P), n = P.nnotes;
    for (int i = 0; i < n; i++)
        if (P.notes[i].pattern == old) note_add(id, P.notes[i].note, P.notes[i].start, P.notes[i].length, P.notes[i].vel);
    LOCK();
    P.clips[roll_clip].pattern = id;
    UNLOCK();
    roll_pattern = id;
    memset(nsel, 0, sizeof nsel);
    status("This clip's notes are its own now; the other copies keep theirs");
}

static int new_note_clip(int t, long at)        /* an empty one-bar note clip, opened in the roll */
{
    long bar = 4 * beat_frames();
    at = at / bar * bar;
    int s = P.inst_sample, root = P.inst_root;
    if (s < 0 || s >= P.nsamples || P.samples[s].removed) {   /* no instrument chosen: a plain saw */
        int c4 = 60;
        s = chord_sample(&c4, 1, "C4", 4);
        root = 60;
    }
    if (s < 0) return -1;
    int c = clip_add(s, t, at, 0, bar);
    if (c < 0) return -1;
    LOCK();
    P.clips[c].pattern = pattern_new_id(&P);
    P.clips[c].root = root;
    UNLOCK();
    roll_open(c);
    return c;
}

static void roll_drag(void)                     /* the roll's drags, called from update_drag */
{
    if (roll_clip < 0) return;
    long origin = roll_origin();
    switch (drag.kind) {
    case D_NMOVE: {
        long anchor = norig[drag.clip].start;
        long want = anchor + (long)(x_to_frame_raw(mx) - drag.a);
        long df = snap(origin + want) - origin - anchor;
        int dn = (int)floor(-(double)(my - drag.sy) / roll_kh + 0.5);
        long min_start = LONG_MAX;
        int lo = 127, hi = 0;
        for (int i = 0; i < P.nnotes; i++)
            if (nsel[i]) {
                if (norig[i].start < min_start) min_start = norig[i].start;
                if (norig[i].note < lo) lo = norig[i].note;
                if (norig[i].note > hi) hi = norig[i].note;
            }
        if (df < -min_start) df = -min_start;
        if (dn < -lo) dn = -lo;
        if (dn > 127 - hi) dn = 127 - hi;
        LOCK();
        for (int i = 0; i < P.nnotes; i++)
            if (nsel[i]) {
                P.notes[i].start = norig[i].start + df;
                P.notes[i].note = norig[i].note + dn;
            }
        UNLOCK();
        if (P.notes[drag.clip].note != roll_voice && roll_voice >= 0) roll_hear(P.notes[drag.clip].note);
        break;
    }
    case D_NLEN: {
        long end0 = norig[drag.clip].start + norig[drag.clip].length;
        long want = end0 + (long)(x_to_frame_raw(mx) - drag.a);
        long dl = snap(origin + want) - origin - end0;
        long min = snap_frames() && !(mods & KMOD_SHIFT) ? snap_frames() : SR / 50;
        LOCK();
        for (int i = 0; i < P.nnotes; i++)
            if (nsel[i]) {
                long len = norig[i].length + dl;
                P.notes[i].length = len < min ? min : len;
            }
        UNLOCK();
        break;
    }
    case D_NBAND: {
        SDL_Rect band = {mx < drag.sx ? mx : drag.sx, my < drag.sy ? my : drag.sy, abs(mx - drag.sx) + 1, abs(my - drag.sy) + 1};
        for (int i = 0; i < P.nnotes; i++) {
            SDL_Rect r = note_rect(i), o;
            nsel[i] = P.notes[i].pattern == roll_pattern && (nband_base[i] || SDL_IntersectRect(&band, &r, &o));
        }
        break;
    }
    case D_VEL: {                               /* paint: every note the mouse passes over */
        int x0 = (mx < drag.sx ? mx : drag.sx) - 3, x1 = (mx < drag.sx ? drag.sx : mx) + 3, only_sel = nsel_count() > 0;
        float v = (float)(roll_vel.y + roll_vel.h - 4 - my) / (roll_vel.h - 10);
        v = v < 0.02f ? 0.02f : v > 1 ? 1 : v;
        LOCK();
        for (int i = 0; i < P.nnotes; i++) {
            if (P.notes[i].pattern != roll_pattern || (only_sel && !nsel[i])) continue;
            int x = (int)frame_to_x(origin + P.notes[i].start);
            if (x >= x0 && x <= x1) P.notes[i].vel = v;
        }
        UNLOCK();
        drag.sx = mx;
        status("Velocity %.0f%%", v * 100);
        break;
    }
    }
}

static void roll_drag_end(void)
{
    if (roll_clip < 0) return;
    if (drag.kind == D_NMOVE && abs(mx - drag.sx) + abs(my - drag.sy) < 3 && !(mods & (KMOD_SHIFT | KMOD_CTRL)))
        nsel_only(drag.clip);                   /* a click on one note of a selection picks just that one */
    if (drag.kind == D_NLEN || (drag.kind == D_NMOVE && drag.b)) roll_len = P.notes[drag.clip].length;
    if (drag.kind == D_NBAND) status("%d note%s selected   Delete, arrows, Ctrl+D act on them", nsel_count(),
                                     nsel_count() == 1 ? "" : "s");
    roll_fit_clip();
}

static int roll_key(SDL_Keysym k)               /* the roll's own keys; 0 = not one of them */
{
    int ctrl = k.mod & KMOD_CTRL, shift = k.mod & KMOD_SHIFT, n = nsel_count();
    switch (k.sym) {
    case SDLK_ESCAPE:
        if (n) memset(nsel, 0, sizeof nsel);
        else roll_close();
        return 1;
    case SDLK_DELETE:
    case SDLK_BACKSPACE:
        if (n) status("Deleted %d note%s", roll_delete_selected(), n == 1 ? "" : "s");
        return 1;
    case SDLK_a:
        if (!ctrl) return 0;
        for (int i = 0; i < P.nnotes; i++) nsel[i] = P.notes[i].pattern == roll_pattern;
        status("%d notes selected", nsel_count());
        return 1;
    case SDLK_d:
        if (!ctrl) return 0;
        roll_duplicate_selected();
        return 1;
    case SDLK_UP:
    case SDLK_DOWN: {
        int d = (k.sym == SDLK_UP ? 1 : -1) * (shift ? 12 : 1);
        if (!n) {
            roll_top += d;
            return 1;
        }
        for (int i = 0; i < P.nnotes; i++)
            if (nsel[i] && (P.notes[i].note + d < 0 || P.notes[i].note + d > 127)) return 1;
        LOCK();
        for (int i = 0; i < P.nnotes; i++)
            if (nsel[i]) P.notes[i].note += d;
        UNLOCK();
        return 1;
    }
    case SDLK_LEFT:
    case SDLK_RIGHT: {
        if (ctrl || !n) return 0;                       /* Ctrl+arrows still move the playhead */
        long d = (snap_frames() && !shift ? snap_frames() : SR / 100) * (k.sym == SDLK_RIGHT ? 1 : -1);
        for (int i = 0; i < P.nnotes; i++)
            if (nsel[i] && P.notes[i].start + d < 0) return 1;
        LOCK();
        for (int i = 0; i < P.nnotes; i++)
            if (nsel[i]) P.notes[i].start += d;
        UNLOCK();
        roll_fit_clip();
        return 1;
    }
    }
    return 0;
}

static void piano_roll(void)
{
    Clip *c = &P.clips[roll_clip];
    int x0 = L.left, top = L.top, bottom = H - L.status;
    char buf[160];

    /* the bar along the top: the sound, its root, the length, sharing, Done */
    SDL_Rect bar = {x0, top, W - x0, 40};
    fill(bar, PANEL);
    line(x0, top + 39, W, top + 39, LINE);
    int x = x0 + 12, by = top + 6, bh = 28;
    fill((SDL_Rect){x0, top, 4, 40}, TRACK_COL[c->track]);
    text(x, by + (bh - font_h) / 2, "PIANO ROLL", DIM);
    x += text_w("PIANO ROLL") + 14;
    static int smap[MAX_SAMPLES], ssel;
    static const char *snames[MAX_SAMPLES];
    int ns = 0, cur = 0, base = sample_base(&P, c->sample);
    for (int i = 0; i < P.nsamples && ns < DD_MAX; i++)
        if (!P.samples[i].pitched && !P.samples[i].removed && !P.samples[i].hidden) {
            if (i == base) cur = ns;
            smap[ns] = i;
            snames[ns++] = P.samples[i].name;
        }
    if (dd.changed_for != &ssel && !(dd.open && dd.sel == &ssel)) ssel = cur;
    int sw = W - x - 560 > 320 ? 320 : W - x - 560 > 160 ? W - x - 560 : 160;
    if (ns && dropdown((SDL_Rect){x, by, sw, bh}, "Sound: ", snames, ns, &ssel, 0) && ssel != cur) {
        int s = smap[ssel];
        LOCK();
        c->sample = s;
        c->root = sample_root(&P.samples[s]);
        UNLOCK();
        status("Notes play \"%s\" (unpitched at %s)", P.samples[s].name, (note_name(c->root, buf, sizeof buf), buf));
    }
    x += sw + 8;
    note_number((SDL_Rect){x, by, 108, bh}, "Root", &c->root);
    x += 116;
    static double bars;
    long barf = 4 * beat_frames();
    if (!(drag.kind == D_NUM && drag.num == &bars) && !(numedit.active && numedit.v == &bars)) bars = (double)c->length / barf;
    double bars0 = bars;
    number((SDL_Rect){x, by, 104, bh}, "Bars", &bars, 0.25, 64, 0.02, "%.2f");
    if (bars != bars0) {
        LOCK();
        c->length = llround(bars * barf);
        if (c->length < 1) c->length = 1;
        UNLOCK();
    }
    x += 112;
    int shared = 0;
    for (int i = 0; i < P.nclips; i++) shared += P.clips[i].pattern == roll_pattern;
    if (shared > 1) {
        snprintf(buf, sizeof buf, "Shared by %d: make unique", shared);
        if (button((SDL_Rect){x, by, text_w(buf) + 20, bh}, buf, 0)) roll_make_unique();
    }
    if (button((SDL_Rect){W - 96, by, 84, bh}, "Done", 1)) {
        roll_close();
        return;
    }

    /* areas */
    roll_ruler = (SDL_Rect){L.tl_x, top + 40, W - L.tl_x, 22};
    int vh = 76;
    roll_vel = (SDL_Rect){L.tl_x, bottom - vh, W - L.tl_x, vh};
    roll_grid = (SDL_Rect){L.tl_x, roll_ruler.y + roll_ruler.h, W - L.tl_x, roll_vel.y - roll_ruler.y - roll_ruler.h};
    roll_keys = (SDL_Rect){x0, roll_grid.y, L.tl_x - x0, roll_grid.h};
    int rows = roll_grid.h / roll_kh + 1;
    if (roll_top < 0) {                         /* centre on the notes, or on the root */
        int lo = 128, hi = -1;
        for (int i = 0; i < P.nnotes; i++)
            if (P.notes[i].pattern == roll_pattern) {
                if (P.notes[i].note < lo) lo = P.notes[i].note;
                if (P.notes[i].note > hi) hi = P.notes[i].note;
            }
        if (hi < 0) lo = hi = c->root;
        roll_top = (lo + hi) / 2 + rows / 2;
    }
    if (roll_top > 127) roll_top = 127;
    if (roll_top < rows - 2 && rows - 2 <= 127) roll_top = rows - 2;

    /* the ruler: bar numbers; click or drag it to move the playhead */
    fill(roll_ruler, PANEL);
    fill((SDL_Rect){x0, roll_ruler.y, L.tl_x - x0, roll_ruler.h}, PANEL);
    double spb = 60.0 / P.bpm, beat_px = spb * pps;
    long stepb = 1;
    while (beat_px * stepb < 9) stepb *= 2;
    SDL_Rect gridall = {x0, roll_ruler.y, W - x0, roll_vel.y + roll_vel.h - roll_ruler.y};
    SDL_RenderSetClipRect(ren, &gridall);

    /* rows: one per note, the black keys darker, a stronger line under each C */
    for (int r = 0; r <= rows; r++) {
        int note = roll_top - r, y = roll_grid.y + r * roll_kh;
        if (note < 0) break;
        int blk = black_key(note);
        fill((SDL_Rect){roll_grid.x, y, roll_grid.w, roll_kh}, blk ? tone(BG, theme_light ? 0.9f : 0.82f) : tone(BG, theme_light ? 1.0f : 1.12f));
        if (note % 12 == 0) line(roll_grid.x, y + roll_kh - 1, W, y + roll_kh - 1, LINE);
        Col kc = blk ? rgb(38, 38, 44) : rgb(226, 226, 232);
        if (note == roll_voice) kc = ACCENT;
        fill((SDL_Rect){x0, y, roll_keys.w - 1, roll_kh - 1}, kc);
        if ((note % 12 == 0 || roll_kh >= 14) && !blk) {
            note_name(note, buf, sizeof buf);
            text(L.tl_x - text_w(buf) - 8, y + (roll_kh - font_h) / 2, buf, note % 12 == 0 ? rgb(20, 20, 26) : rgb(110, 110, 120));
        }
    }
    long b = (long)floor(view_start / spb);
    b -= b % stepb;
    for (;; b += stepb) {                       /* the beat grid and bar numbers */
        double xd = L.tl_x + (b * spb - view_start) * pps;
        if (xd > W) break;
        if (xd < L.tl_x) continue;
        int xx = (int)xd, isbar = b % 4 == 0;
        line(xx, roll_grid.y, xx, roll_vel.y + roll_vel.h, isbar ? tone(LINE, 1.3f) : tone(LINE, 0.75f));
        if (isbar) {
            snprintf(buf, sizeof buf, "%ld", b / 4 + 1);
            text(xx + 4, roll_ruler.y + 3, buf, DIM);
            line(xx, roll_ruler.y + 12, xx, roll_ruler.y + roll_ruler.h, DIM);
        }
    }
    long sf = snap_frames();                    /* finer snap lines inside a beat */
    if (sf && sf < beat_frames() && sf / (double)SR * pps >= 8) {
        long f0 = (long)(view_start * SR) / sf * sf;
        for (long f = f0; frame_to_x(f) < W; f += sf)
            if (f % beat_frames() && frame_to_x(f) >= L.tl_x)
                line((int)frame_to_x(f), roll_grid.y, (int)frame_to_x(f), roll_grid.y + roll_grid.h, alpha(LINE, 70));
    }
    int cx0 = (int)frame_to_x(c->start), cx1 = (int)frame_to_x(c->start + c->length);   /* outside the clip: dimmed */
    if (cx0 > L.tl_x) fill((SDL_Rect){L.tl_x, roll_grid.y, cx0 - L.tl_x, roll_grid.h}, alpha(DARK, 110));
    if (cx1 < W) fill((SDL_Rect){cx1, roll_grid.y, W - cx1, roll_grid.h}, alpha(DARK, 110));
    line(cx1, roll_ruler.y, cx1, roll_grid.y + roll_grid.h, ACCENT);

    /* notes */
    Col tc = TRACK_COL[c->track];
    long origin = roll_origin();
    for (int i = 0; i < P.nnotes; i++) {
        const Note *n = &P.notes[i];
        if (n->pattern != roll_pattern) continue;
        SDL_Rect r = note_rect(i);
        if (r.x > W || r.x + r.w < L.tl_x) continue;
        fill(r, tone(tc, 0.55f + 0.5f * n->vel));
        outline(r, nsel[i] ? rgb(255, 255, 255) : shade(tc, 0.55f));
        if (nsel[i]) outline((SDL_Rect){r.x + 1, r.y + 1, r.w - 2, r.h - 2}, rgb(255, 255, 255));
        if (r.w > 34 && roll_kh >= 12) {
            note_name(n->note, buf, sizeof buf);
            text_clip(r, r.x + 4, r.y + (r.h - font_h) / 2, buf, DARK);
        }
    }
    if (drag.kind == D_NBAND) {
        SDL_Rect band = {mx < drag.sx ? mx : drag.sx, my < drag.sy ? my : drag.sy, abs(mx - drag.sx), abs(my - drag.sy)};
        fill(band, alpha(HEAD, 30));
        outline(band, alpha(HEAD, 200));
    }

    /* velocity lane */
    fill(roll_vel, tone(PANEL, 0.9f));
    line(roll_vel.x, roll_vel.y, W, roll_vel.y, LINE);
    fill((SDL_Rect){x0, roll_vel.y, roll_keys.w, roll_vel.h}, PANEL);
    text(x0 + 12, roll_vel.y + 8, "VELOCITY", DIM);
    text_clip((SDL_Rect){x0 + 12, roll_vel.y + 26, roll_keys.w - 16, 2 * font_h + 6}, x0 + 12, roll_vel.y + 28, "drag to set", DIM);
    for (int i = 0; i < P.nnotes; i++) {
        const Note *n = &P.notes[i];
        if (n->pattern != roll_pattern) continue;
        int xx = (int)frame_to_x(origin + n->start), hh = (int)(n->vel * (roll_vel.h - 10));
        if (xx < L.tl_x - 4 || xx > W) continue;
        Col vc = nsel[i] ? rgb(255, 255, 255) : tone(tc, 0.6f + 0.5f * n->vel);
        fill((SDL_Rect){xx, roll_vel.y + roll_vel.h - 4 - hh, 4, hh}, vc);
        fill((SDL_Rect){xx - 2, roll_vel.y + roll_vel.h - 6 - hh, 8, 3}, vc);
    }

    /* the playhead */
    int phx = (int)frame_to_x(P.playhead);
    if (phx >= L.tl_x && phx < W) {
        line(phx, roll_ruler.y, phx, roll_vel.y + roll_vel.h, HEAD);
        fill((SDL_Rect){phx - 5, roll_ruler.y, 11, 7}, HEAD);
    }
    SDL_RenderSetClipRect(ren, NULL);
    line(L.tl_x - 1, roll_ruler.y, L.tl_x - 1, bottom, LINE);

    int over = hit(roll_grid) ? roll_note_at() : -1;
    if (drag.kind == D_NLEN || (over >= 0 && mx >= note_rect(over).x + note_rect(over).w - 5 && note_rect(over).w > 8))
        SDL_SetCursor(cur_resize);
    else if (drag.kind == D_NMOVE || over >= 0) SDL_SetCursor(cur_hand);
    else SDL_SetCursor(cur_arrow);
    if (drag.kind != D_NONE) return;

    /* wheel: up and down the keys; Shift+wheel: along; Ctrl+wheel: zoom */
    SDL_Rect wheel_area = {x0, roll_grid.y, W - x0, roll_vel.y + roll_vel.h - roll_grid.y};
    if (hit(wheel_area) && (wheel_y || wheel_x)) {
        if (mods & KMOD_CTRL) zoom(pow(1.25, wheel_y), mx);
        else if ((mods & KMOD_SHIFT) || wheel_x) {
            view_start -= ((mods & KMOD_SHIFT) ? wheel_y : 0) * 60.0 / pps - wheel_x * 60.0 / pps;
            if (view_start < 0) view_start = 0;
        } else roll_top += wheel_y * 3;
        wheel_x = wheel_y = 0;
    }
    if (hit(roll_ruler) && lpress) {
        drag = (typeof(drag)){.kind = D_PLAYHEAD};
        LOCK();
        P.playhead = x_to_frame(mx);
        UNLOCK();
        lpress = 0;
    }
    if (hit(roll_keys) && lpress) {             /* a key: hear it */
        roll_hear(roll_row_note(my));
        lpress = 0;
    }
    if (hit(roll_vel) && lpress) {
        drag = (typeof(drag)){.kind = D_VEL, .sx = mx, .sy = my};
        roll_drag();
        lpress = 0;
    }
    if (!hit(roll_grid) || !(lpress || rpress)) return;
    int i = roll_note_at();
    if (rpress) {
        rpress = 0;
        if (i < 0) return;
        if (nsel[i] && nsel_count() > 1) status("Deleted %d notes", roll_delete_selected());
        else note_delete(i);
        return;
    }
    lpress = 0;
    int made = 0;
    if (i < 0 && (mods & KMOD_SHIFT)) {         /* Shift+drag on empty grid: select with a box */
        memcpy(nband_base, nsel, sizeof nband_base);
        drag = (typeof(drag)){.kind = D_NBAND, .sx = mx, .sy = my};
        return;
    }
    if (i < 0) {                                /* empty grid: a new note, which can be dragged at once */
        long f = (long)x_to_frame_raw(mx) - origin, s = snap_frames();
        if (f < 0) return;
        long start = s ? f / s * s : f, len = roll_len > 0 ? roll_len : s ? s : beat_frames();
        if (s && len < s) len = s;
        int note = roll_row_note(my);
        if (note < 0 || note > 127) return;
        i = note_add(roll_pattern, note, start, len, 0.8f);
        if (i < 0) return;
        nsel_only(i);
        made = 1;
    } else if (mods & KMOD_SHIFT) {             /* Shift+click: add to or take out of the selection */
        nsel[i] = !nsel[i];
        if (!nsel[i]) return;
    } else if (!nsel[i]) {
        nsel_only(i);
    }
    if ((mods & KMOD_CTRL) && !made) {          /* Ctrl+drag: copies of the selection */
        int n = P.nnotes, under = i;
        for (int k = 0; k < n; k++)
            if (nsel[k]) {
                int j = note_add(roll_pattern, P.notes[k].note, P.notes[k].start, P.notes[k].length, P.notes[k].vel);
                if (j < 0) break;
                nsel[k] = 0;
                nsel[j] = 1;
                if (k == under) i = j;
            }
    }
    for (int k = 0; k < P.nnotes; k++) {
        norig[k].start = P.notes[k].start;
        norig[k].length = P.notes[k].length;
        norig[k].note = P.notes[k].note;
    }
    SDL_Rect r = note_rect(i);
    int stretch = !made && r.w > 8 && mx >= r.x + r.w - 5;
    drag = (typeof(drag)){.kind = stretch ? D_NLEN : D_NMOVE, .clip = i, .sx = mx, .sy = my,
                          .a = (long)x_to_frame_raw(mx), .b = made};
    roll_hear(P.notes[i].note);
}

static void track_headers(void)
{
    fill((SDL_Rect){L.left, L.top, L.head, L.ruler}, PANEL);
    text(L.left + 12, L.top + (L.ruler - font_h) / 2, "TRACKS", DIM);
    SDL_Rect add = {L.left + L.head - 32, L.top + 4, 22, L.ruler - 8};
    if (button2(add, "+", 0, P.ntracks < MAX_TRACKS)) {
        LOCK();
        int t = project_add_track(&P, NULL);
        UNLOCK();
        status("Added track %d", t + 1);
    }
    int compact = L.lane_h < 40;
    for (int t = 0; t < P.ntracks; t++) {
        int y = track_y(t);
        SDL_Rect r = {L.left, y, L.head, L.lane_h};
        Track *tr = &P.tracks[t];
        fill(r, t % 2 ? PANEL : tone(PANEL, 1.08f));
        fill((SDL_Rect){L.left, y, 4, L.lane_h}, TRACK_COL[t]);
        line(L.left, y + L.lane_h - 1, L.left + L.head, y + L.lane_h - 1, LINE);
        int bh = compact ? L.lane_h - 10 : 20, by = compact ? y + 3 : y + 5;
        if (bh > 20) bh = 20;
        if (mixer_open && t == sel_track) fill((SDL_Rect){L.left + 4, y, L.head - 4, L.lane_h - 1}, tone(PANEL3, 1.1f));
        text_clip((SDL_Rect){L.left + 8, y, L.head - 102, L.lane_h}, L.left + 12, by + (bh - font_h) / 2, tr->name, TEXT);
        if (track_has_fx(tr) && L.lane_h >= 56) text(L.left + 12, by + bh + 3, "fx", ACCENT);
        SDL_Rect name_r = {L.left + 4, y, L.head - 92, L.lane_h - 16};
        if (drag.kind == D_NONE && hit(name_r) && lpress) {      /* pick it for the Mixer pane */
            sel_track = t;
            if (mixer_open) mixer_sub = 1;
            lpress = 0;
        }
        SDL_Rect m = {L.left + L.head - 58, by, 22, bh}, s = {L.left + L.head - 32, by, 22, bh};
        if (button(m, "M", tr->mute)) { LOCK(); tr->mute = !tr->mute; UNLOCK(); }
        if (button(s, "S", tr->solo)) { LOCK(); tr->solo = !tr->solo; UNLOCK(); }
        SDL_Rect ab = {L.left + L.head - 84, by, 22, bh};
        int automated = 0;
        for (int p = 0; p < AUTO_PARAMS; p++) automated |= auto_lane(&P, t, p, NULL) > 0;
        if (button(ab, "A", auto_open[t])) {           /* show or hide its automation lane */
            auto_open[t] = !auto_open[t];
            if (auto_open[t] && !auto_lane(&P, t, auto_param[t], NULL))   /* show a lane with points in it */
                for (int p = 0; p < AUTO_PARAMS; p++)
                    if (auto_lane(&P, t, p, NULL)) {
                        auto_param[t] = p;
                        break;
                    }
            if (auto_open[t]) status("Automation for %s: pick what to automate, click to add points, drag them, right-click removes", tr->name);
        }
        if (automated && !auto_open[t]) fill((SDL_Rect){ab.x + ab.w - 6, ab.y + 2, 4, 4}, ACCENT);
        if (auto_open[t]) auto_header(t, y + L.lane_h);
        meter_draw((SDL_Rect){L.left + L.head - 7, y + 3, 4, L.lane_h - 7}, &track_meter[t], 1);
        SDL_Rect vb = volume_bar(t);
        fill(vb, PANEL3);
        int vol_auto;
        float av = auto_at(&P, t, AUTO_VOLUME, P.playhead, &vol_auto), shown = vol_auto ? auto_to_value(AUTO_VOLUME, av) : tr->volume;
        fill((SDL_Rect){vb.x, vb.y, (int)(vb.w * (shown > 1 ? 1 : shown)), vb.h}, tr->mute ? DIM : vol_auto ? ACCENT : TRACK_COL[t]);
        SDL_Rect grab = {vb.x - 4, vb.y - 5, vb.w + 8, vb.h + 10};
        if (vol_auto && drag.kind == D_NONE && hit(grab) && lpress) {
            status("%s's volume follows its automation lane (A)", tr->name);
            lpress = 0;
        }
        if (drag.kind == D_NONE && hit(grab) && lpress) {
            drag = (typeof(drag)){.kind = D_TVOL, .track = t};
            lpress = 0;
        }
    }
}

/* "1:05", or "1:05.25" with hundredths */
static void fmt_clock(double secs, int fine, char *buf, size_t n)
{
    if (secs < 0) secs = 0;
    int m = (int)(secs / 60);
    double r = secs - m * 60;
    if (fine) snprintf(buf, n, "%d:%05.2f", m, r);
    else if (r - floor(r) > 1e-6) snprintf(buf, n, "%d:%04.1f", m, r);
    else snprintf(buf, n, "%d:%02d", m, (int)r);
}

/* The minutes:seconds scale: labelled ticks at a step that keeps labels apart, small ones between */
static void time_scale(SDL_Rect r)
{
    static const double STEPS[][2] = {{0.1, 0.02}, {0.25, 0.05}, {0.5, 0.1}, {1, 0.25}, {2, 0.5}, {5, 1},
                                      {10, 2}, {15, 5}, {30, 10}, {60, 15}, {120, 30}, {300, 60}, {600, 120}};
    int k = 0;
    while (k < 12 && STEPS[k][0] * pps < 64) k++;
    double major = STEPS[k][0], minor = STEPS[k][1];
    fill(r, tone(PANEL, 0.92f));
    double t0 = floor(view_start / minor) * minor;
    for (double t = t0;; t += minor) {
        double xd = L.tl_x + (t - view_start) * pps;
        if (xd > W) break;
        if (xd < r.x) continue;
        int x = (int)xd;
        int is_major = fabs(t / major - floor(t / major + 0.5)) < 1e-6;
        line(x, r.y, x, r.y + (is_major ? 8 : 3), DIM);
        if (is_major) {
            char b[16];
            fmt_clock(t, 0, b, sizeof b);
            text(x + 3, r.y + 3, b, DIM);
        }
    }
}

static void timeline(void)
{
    SDL_Rect area = {L.tl_x, L.lanes_y, L.tl_w, lanes_h()};
    SDL_Rect ruler = {L.tl_x, L.top, L.tl_w, L.ruler};
    SDL_Rect whole = {L.tl_x, L.top, L.tl_w, L.ruler + lanes_h()};
    SDL_RenderSetClipRect(ren, &whole);

    for (int t = 0; t < P.ntracks; t++)
        fill((SDL_Rect){L.tl_x, track_y(t), L.tl_w, L.lane_h}, t % 2 ? BG : tone(BG, 1.18f));
    fill(ruler, PANEL);

    /* loop region */
    if (P.loop_end > P.loop_start) {
        int x0 = (int)frame_to_x(P.loop_start), x1 = (int)frame_to_x(P.loop_end);
        fill((SDL_Rect){x0, L.top + 4, x1 - x0, 18}, alpha(ACCENT, P.loop ? 110 : 40));
        if (P.loop) fill((SDL_Rect){x0, area.y, x1 - x0, area.h}, alpha(ACCENT, 14));
    }

    /* beat grid and bar numbers */
    double spb = 60.0 / P.bpm, beat_px = spb * pps;
    long step = 1;
    while (beat_px * step < 9) step *= 2;
    long label_every = 1;
    while (4 * beat_px * label_every < 34) label_every *= 2;
    long b = (long)floor(view_start / spb);
    b -= b % step;
    for (;; b += step) {
        double xd = L.tl_x + (b * spb - view_start) * pps;
        if (xd > W) break;
        if (xd < L.tl_x) continue;
        int x = (int)xd, bar = b % 4 == 0;
        line(x, area.y, x, area.y + area.h, bar ? LINE : tone(LINE, 0.62f));
        line(x, L.top + 24 - (bar ? 8 : 4), x, L.top + 24, DIM);
        if (bar && (b / 4) % label_every == 0) {
            char buf[24];
            snprintf(buf, sizeof buf, "%ld", b / 4 + 1);
            text(x + 4, L.top + 3, buf, DIM);
        }
    }
    time_scale((SDL_Rect){L.tl_x, L.top + 24, L.tl_w, L.ruler - 24});
    line(L.tl_x, L.top + 24, W, L.top + 24, tone(LINE, 0.8f));
    line(L.tl_x, L.top + L.ruler - 1, W, L.top + L.ruler - 1, LINE);

    /* clips */
    int hover_edge = 0;
    for (int i = 0; i < P.nclips; i++) {
        const Clip *c = &P.clips[i];
        SDL_Rect r = clip_rect(c);
        if (r.x > W || r.x + r.w < L.tl_x) continue;
        Col col = TRACK_COL[c->track];
        fill(r, wash(col, 0.42f));
        fill((SDL_Rect){r.x, r.y, r.w, font_h + 4}, wash(col, 0.78f));
        const Sample *s = &P.samples[c->sample];
        int body_y = r.y + font_h + 4, body_h = r.h - font_h - 4, mid = body_y + body_h / 2;
        int px0 = r.x > L.tl_x ? r.x : L.tl_x, px1 = r.x + r.w < W ? r.x + r.w : W;
        float wave_gain = c->gain_db == 0 ? 1 : powf(10, c->gain_db / 20);
        Col wave = theme_light ? shade(col, 0.62f) : col;
        SDL_SetRenderDrawColor(ren, wave.r, wave.g, wave.b, 255);
        if (c->pattern > 0) draw_clip_notes(c, r, body_y, body_h, wave);
        else for (int px = px0; px < px1; px++) {
            long fa = (long)x_to_frame_raw(px) - c->start + c->offset;
            long fb = (long)x_to_frame_raw(px + 1) - c->start + c->offset;
            if (fa < c->offset) fa = c->offset;
            if (fb > c->offset + c->length) fb = c->offset + c->length;
            if (fb <= fa) fb = fa + 1;
            float lo, hi;
            sample_minmax(s, fa, fb, &lo, &hi);
            long rel = fa - c->offset;                  /* draw it as loud as it plays */
            float g = wave_gain;
            if (c->fade_in > 0 && rel < c->fade_in) g *= (float)rel / c->fade_in;
            if (c->fade_out > 0 && c->length - rel < c->fade_out) g *= (float)(c->length - rel) / c->fade_out;
            hi = hi * g > 1 ? 1 : hi * g;
            lo = lo * g < -1 ? -1 : lo * g;
            int y0 = mid - (int)(hi * body_h / 2), y1 = mid - (int)(lo * body_h / 2);
            SDL_RenderDrawLine(ren, px, y0, px, y1);
        }
        char label[128];
        if (c->gain_db != 0) snprintf(label, sizeof label, "%s%s  %+.1fdB", c->pattern ? "Notes: " : "", s->name, c->gain_db);
        else snprintf(label, sizeof label, "%s%s", c->pattern ? "Notes: " : "", s->name);
        text_clip((SDL_Rect){r.x + 2, r.y, r.w - 4, font_h + 4}, r.x + 5, r.y + 2, label, DARK);
        SDL_Rect hin, hout;
        if (fade_handles(c, &hin, &hout)) {                /* fades as lines, handles on hover */
            if (c->fade_in > 0) line(r.x, r.y + r.h - 1, hin.x + 4, r.y + 1, TEXT);
            if (c->fade_out > 0) line(hout.x + 4, r.y + 1, r.x + r.w - 1, r.y + r.h - 1, TEXT);
            if (i == selected || (drag.kind == D_NONE && hit(r)) || drag.kind == D_FADEIN || drag.kind == D_FADEOUT) {
                if (i == selected || (drag.kind != D_NONE && drag.clip == i) || hit(r)) {
                    fill(hin, hit(hin) ? HEAD : TEXT);
                    fill(hout, hit(hout) ? HEAD : TEXT);
                }
            }
        }
        outline(r, csel[i] ? TEXT : theme_light ? shade(col, 0.8f) : shade(col, 0.95f));
        if (csel[i]) outline((SDL_Rect){r.x + 1, r.y + 1, r.w - 2, r.h - 2}, i == selected ? HEAD : TEXT);
        if (drag.kind == D_NONE && hit(r)) {
            int edge = r.w / 3 < 6 ? r.w / 3 : 6;
            if (mx < r.x + edge || mx >= r.x + r.w - edge) hover_edge = 1;
        }
    }

    for (int t = 0; t < P.ntracks; t++)
        if (auto_open[t]) auto_lane_draw(t);

    /* drop preview while dragging from the library */
    if (drag.kind == D_LIB && hit(area)) {
        const Sample *s = &P.samples[drag.sample];
        Clip ghost = {drag.sample, lane_at(my), snap(x_to_frame(mx)), 0, s->frames, 0, 0, 0, 0, 0};
        SDL_Rect r = clip_rect(&ghost);
        fill(r, alpha(TRACK_COL[ghost.track], 90));
        outline(r, TEXT);
    }
    if (drag.kind == D_BEAT && hit(area)) {   /* one ghost row per part of the beat */
        long pat;
        int reps, parts = beat_layout(drag.sample, &pat, &reps);
        for (int i = 0; i < parts; i++) {
            int t = lane_at(my) + i;
            Clip ghost = {0, t, snap(x_to_frame(mx)), 0, pat * reps, 0, 0, 0, 0, 0};
            SDL_Rect r = clip_rect(&ghost);
            fill(r, alpha(TRACK_COL[t % MAX_TRACKS], 90));
            outline(r, TEXT);
        }
    }

    if (drag.kind == D_BAND) {          /* the selection box */
        SDL_Rect band = {mx < drag.sx ? mx : drag.sx, my < drag.sy ? my : drag.sy, abs(mx - drag.sx), abs(my - drag.sy)};
        fill(band, alpha(HEAD, 30));
        outline(band, alpha(HEAD, 200));
    }

    /* playhead */
    int phx = (int)frame_to_x(P.playhead);
    if (phx >= L.tl_x && phx < W) {
        line(phx, L.top, phx, area.y + area.h, HEAD);
        fill((SDL_Rect){phx - 5, L.top, 11, 8}, HEAD);
        char tb[24];                            /* the playhead's time, on the time scale */
        fmt_clock((double)P.playhead / SR, 1, tb, sizeof tb);
        int tw = text_w(tb) + 8, tx = phx + 4 + tw > W ? phx - 4 - tw : phx + 4;
        fill((SDL_Rect){tx, L.top + 26, tw, font_h + 2}, HEAD);
        text(tx + 4, L.top + 27, tb, DARK);
    }
    SDL_RenderSetClipRect(ren, NULL);

    /* cursor shape */
    if (drag.kind == D_TRIML || drag.kind == D_TRIMR || drag.kind == D_FADEIN || drag.kind == D_FADEOUT || hover_edge)
        SDL_SetCursor(cur_resize);
    else if (drag.kind == D_MOVE || drag.kind == D_LIB || drag.kind == D_BEAT) SDL_SetCursor(cur_hand);
    else SDL_SetCursor(cur_arrow);

    if (drag.kind != D_NONE) return;

    /* wheel: scroll, Ctrl+wheel: zoom, Alt+wheel over a clip: its gain */
    if (hit(whole) && (wheel_y || wheel_x)) {
        int over = -1;
        for (int i = P.nclips - 1; i >= 0 && over < 0 && (mods & KMOD_ALT); i--)
            if (hit(clip_rect(&P.clips[i]))) over = i;
        if (over >= 0) {
            float step = wheel_y * ((mods & KMOD_SHIFT) ? 0.1f : 1.0f);
            int all = csel[over];               /* over a selected clip: the whole selection */
            LOCK();
            for (int i = 0; i < P.nclips; i++)
                if (i == over || (all && csel[i])) {
                    float gdb = P.clips[i].gain_db + step;
                    P.clips[i].gain_db = gdb < -36 ? -36 : gdb > 12 ? 12 : gdb;
                }
            UNLOCK();
            status("Clip gain %+.1f dB%s", P.clips[over].gain_db, all && sel_count() > 1 ? " (all selected)" : "");
        } else if (mods & KMOD_CTRL) {
            zoom(pow(1.25, wheel_y), mx);
        } else {
            view_start -= (wheel_x - wheel_y) * 60.0 / pps;
            if (view_start < 0) view_start = 0;
        }
        wheel_x = wheel_y = 0;
    }

    if (hit(ruler) && lpress) {
        drag = (typeof(drag)){.kind = D_PLAYHEAD};
        LOCK();
        P.playhead = x_to_frame(mx);
        UNLOCK();
        lpress = 0;
    }
    if (hit(ruler) && rpress) {
        drag = (typeof(drag)){.kind = D_LOOP, .a = snap(x_to_frame(mx))};
        rpress = 0;
    }

    int at_t = auto_lane_at(my);              /* an automation lane: add, drag or remove points */
    if (at_t >= 0 && hit(area) && (lpress || rpress)) {
        SDL_Rect r = auto_rect(at_t);
        int pa = auto_param[at_t], pt = auto_point_at(at_t, pa);
        if (rpress && pt >= 0) {
            auto_delete(pt);
            status("Removed an automation point");
        } else if (lpress) {
            if (pt < 0) pt = auto_add(at_t, pa, snap(x_to_frame(mx)), auto_y_to_v(r, my));
            if (pt >= 0) {
                drag = (typeof(drag)){.kind = D_AUTO, .clip = pt, .track = at_t, .sample = pa, .sx = mx, .sy = my};
                auto_status(pt);
            }
        }
        lpress = rpress = 0;
    }

    if (hit(area) && (lpress || rpress)) {
        int found = -1;
        for (int i = P.nclips - 1; i >= 0 && lpress; i--)   /* fade handles can stick out above a clip */
            if (fade_hit(i) != D_NONE) {
                found = i;
                break;
            }
        for (int i = P.nclips - 1; i >= 0 && found < 0; i--)
            if (hit(clip_rect(&P.clips[i]))) { found = i; break; }
        if (lpress && ldouble && !(mods & (KMOD_CTRL | KMOD_SHIFT))) {   /* double-click: the piano roll */
            if (found >= 0 && P.clips[found].pattern > 0) roll_open(found);
            else if (found < 0) new_note_clip(lane_at(my), x_to_frame(mx));
            else editor_open(found);             /* an audio clip: its own editor window */
            lpress = 0;
        }
        if (found >= 0 && rpress) {             /* right-click: delete it, or the whole selection it's in */
            if (csel[found] && sel_count() > 1) {
                status("Deleted %d clips", delete_selection());
            } else {
                status("Deleted clip \"%s\"", P.samples[P.clips[found].sample].name);
                clip_delete(found);
            }
        } else if (found >= 0 && lpress && !(mods & (KMOD_CTRL | KMOD_SHIFT)) && fade_hit(found) != D_NONE) {
            sel_only(found);
            drag = (typeof(drag)){.kind = fade_hit(found), .clip = found};
        } else if (found >= 0 && lpress && (mods & KMOD_SHIFT) && !(mods & KMOD_CTRL)) {
            csel[found] = !csel[found];             /* Shift+click: add to or take out of the selection */
            if (csel[found]) {
                selected = found;
                group_remember();
                drag = (typeof(drag)){.kind = D_MOVE, .clip = found, .a = P.clips[found].start,
                                      .track = P.clips[found].track, .sx = mx, .sy = my,
                                      .grab = (long)x_to_frame_raw(mx) - P.clips[found].start};
            } else {
                sel_fix_primary();
            }
            status("%d clip%s selected", sel_count(), sel_count() == 1 ? "" : "s");
        } else if (found >= 0 && lpress) {
            if (!csel[found]) sel_only(found);
            if (mods & KMOD_CTRL) {            /* Ctrl+drag: copy the clip, or the whole selection */
                long off = P.clips[found].start;
                int before = P.nclips;
                duplicate_selection(0);
                for (int i = before; i < P.nclips; i++)    /* follow the copy of the clip under the mouse */
                    if (P.clips[i].start == off && P.clips[i].track == P.clips[found].track &&
                        P.clips[i].sample == P.clips[found].sample)
                        found = i;
                selected = found;
            }
            selected = found;
            Clip *c = &P.clips[found];
            SDL_Rect r = clip_rect(c);
            int edge = r.w / 3 < 6 ? r.w / 3 : 6;
            int kind = mx < r.x + edge ? D_TRIML : mx >= r.x + r.w - edge ? D_TRIMR : D_MOVE;
            if (kind != D_MOVE) sel_only(found);   /* trimming is one clip at a time */
            group_remember();
            drag = (typeof(drag)){.kind = kind, .clip = found, .a = c->start, .b = c->offset, .c = c->length,
                                  .track = c->track, .sx = mx, .sy = my,
                                  .grab = (long)x_to_frame_raw(mx) - c->start};
        } else if (lpress) {                    /* empty space: drag a box to select */
            if (!(mods & KMOD_SHIFT)) sel_clear();
            memcpy(band_base, csel, sizeof band_base);
            drag = (typeof(drag)){.kind = D_BAND, .sx = mx, .sy = my};
        }
        lpress = rpress = 0;
    }
}

/* the transport's icons, drawn so they look the same in every theme and font */
enum { TI_START, TI_BACK, TI_PLAY, TI_PAUSE, TI_STOP, TI_FWD, TI_END, TI_LOOP, TI_REC };

static void tri(int x, int cy, int hgt, int dir, Col c)  /* a filled triangle pointing left (-1) or right */
{
    for (int i = 0; i <= hgt; i++) {
        int half = (hgt - i) * 6 / 10;
        int xx = dir > 0 ? x + i : x - i;
        line(xx, cy - half, xx, cy + half, c);
    }
}

static void transport_icon(int kind, SDL_Rect r, Col c)
{
    int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    switch (kind) {
    case TI_START: fill((SDL_Rect){cx - 6, cy - 5, 2, 11}, c); tri(cx + 4, cy, 8, -1, c); break;
    case TI_BACK: tri(cx, cy, 7, -1, c); tri(cx + 7, cy, 7, -1, c); break;
    case TI_PLAY: tri(cx - 4, cy, 9, 1, c); break;
    case TI_PAUSE: fill((SDL_Rect){cx - 5, cy - 5, 3, 11}, c); fill((SDL_Rect){cx + 2, cy - 5, 3, 11}, c); break;
    case TI_STOP: fill((SDL_Rect){cx - 5, cy - 5, 10, 10}, c); break;
    case TI_FWD: tri(cx - 7, cy, 7, 1, c); tri(cx, cy, 7, 1, c); break;
    case TI_END: tri(cx - 4, cy, 8, 1, c); fill((SDL_Rect){cx + 5, cy - 5, 2, 11}, c); break;
    case TI_LOOP:
        ring(cx, cy, 6, c);
        ring(cx, cy, 5, c);
        tri(cx + 5, cy - 1, 4, 1, c);
        break;
    case TI_REC: disc(cx, cy, 5, c); break;
    }
}

static int transport_button(SDL_Rect r, int kind, int on, Col tint)
{
    int hov = hit(r) && drag.kind == D_NONE;
    style_box(r, on ? 2 : hov ? 1 : 0);
    transport_icon(kind, r, on ? btn_fg(1) : tint.a ? tint : btn_fg(0));
    if (hov && lpress) {
        lpress = 0;
        return 1;
    }
    return 0;
}

/* the bottom bar: messages on the left, the transport in the middle, a hint on the right */
static SDL_Rect transport_rect;

static void status_bar(void)
{
    int y = H - L.status;
    fill((SDL_Rect){0, y, W, L.status}, PANEL);
    line(0, y, W, y, LINE);
    int bw = 30, gap = 3, n = 8, tw = n * bw + (n - 1) * gap, tx = (W - tw) / 2, by = y + 2, bh = L.status - 4;
    transport_rect = (SDL_Rect){tx, by, tw, bh};
    long bar = 4 * beat_frames();
    Col none = {0, 0, 0, 0};
    if (transport_button((SDL_Rect){tx, by, bw, bh}, TI_START, 0, none)) playhead_to(0);
    tx += bw + gap;
    if (transport_button((SDL_Rect){tx, by, bw, bh}, TI_BACK, 0, none)) playhead_step(bar, -1);
    tx += bw + gap;
    if (transport_button((SDL_Rect){tx, by, bw, bh}, P.playing ? TI_PAUSE : TI_PLAY, P.playing, none)) toggle_play();
    tx += bw + gap;
    if (transport_button((SDL_Rect){tx, by, bw, bh}, TI_STOP, 0, none)) transport_stop();
    tx += bw + gap;
    if (transport_button((SDL_Rect){tx, by, bw, bh}, TI_FWD, 0, none)) playhead_step(bar, 1);
    tx += bw + gap;
    if (transport_button((SDL_Rect){tx, by, bw, bh}, TI_END, 0, none)) playhead_to(project_end(&P));
    tx += bw + gap;
    if (transport_button((SDL_Rect){tx, by, bw, bh}, TI_LOOP, P.loop, none)) toggle_loop();
    tx += bw + gap;
    if (transport_button((SDL_Rect){tx, by, bw, bh}, TI_REC, pads_rec, rgb(220, 60, 55))) rec_toggle();
    rec_timer((SDL_Rect){tx + bw + 8, by, 110, bh});
    int left_w = transport_rect.x - 18;
    text_clip((SDL_Rect){8, y, left_w, L.status}, 10, y + (L.status - font_h) / 2, status_msg, TEXT);
    const char *hint = roll_clip >= 0 ? "Esc or Done returns to the timeline" : "Double-click a track for a note clip";
    int hw = text_w(hint), right_x = transport_rect.x + transport_rect.w + 16 + (pads_rec ? 118 : 0);
    if (W - hw - 12 > right_x) text(W - hw - 12, y + (L.status - font_h) / 2, hint, DIM);
}

/* ---------------------------------------------------------------- dragging */

#define MIN_CLIP (SR / 100)

static void update_drag(void)
{
    switch (drag.kind) {
    case D_LIBPEND:
        if (abs(mx - drag.sx) + abs(my - drag.sy) > 4) drag.kind = D_LIB;
        break;
    case D_MOVE: {                      /* the clip under the mouse snaps; the rest keep their places */
        if (over_library()) {           /* held over the library: it's going there, not moving */
            group_place(0, 0);
            break;
        }
        long f = snap((long)x_to_frame_raw(mx) - drag.grab);
        group_place(f - drag.a, lane_at(my) - drag.track);
        break;
    }
    case D_BAND: {
        SDL_Rect band = {mx < drag.sx ? mx : drag.sx, my < drag.sy ? my : drag.sy, abs(mx - drag.sx) + 1,
                         abs(my - drag.sy) + 1};
        for (int i = 0; i < P.nclips; i++) {
            SDL_Rect r = clip_rect(&P.clips[i]), o;
            csel[i] = band_base[i] || SDL_IntersectRect(&band, &r, &o);
        }
        sel_fix_primary();
        break;
    }
    case D_SCROLL: {                    /* a scrollbar thumb */
        int v = drag.iorig + (int)((my - drag.sy) * drag.nstep);
        *drag.inum = v < 0 ? 0 : v > drag.imax ? drag.imax : v;
        break;
    }
    case D_AUTO: {
        int i = drag.clip, first, n = auto_lane(&P, drag.track, drag.sample, &first);
        if (i < first || i >= first + n) break;
        long at = snap(x_to_frame(mx));      /* stays between its neighbours, so the lane stays in order */
        long lo = i > first ? P.autos[i - 1].at : 0, hi = i < first + n - 1 ? P.autos[i + 1].at : LONG_MAX;
        at = at < lo ? lo : at > hi ? hi : at;
        LOCK();
        P.autos[i].at = at;
        P.autos[i].v = auto_y_to_v(auto_rect(drag.track), my);
        UNLOCK();
        auto_status(i);
        break;
    }
    case D_NMOVE:
    case D_NLEN:
    case D_NBAND:
    case D_VEL:
        roll_drag();
        break;
    case D_TRIMR: {
        Clip *c = &P.clips[drag.clip];
        long len = snap(x_to_frame(mx)) - c->start, max = c->pattern ? 64 * 4 * beat_frames() : P.samples[c->sample].frames - c->offset;
        len = len < MIN_CLIP ? MIN_CLIP : len > max ? max : len;
        LOCK();
        c->length = len;
        UNLOCK();
        break;
    }
    case D_TRIML: {
        long ns = snap(x_to_frame(mx));
        long lo = drag.a - drag.b, hi = drag.a + drag.c - MIN_CLIP;
        if (lo < 0) lo = 0;
        ns = ns < lo ? lo : ns > hi ? hi : ns;
        Clip *c = &P.clips[drag.clip];
        LOCK();
        c->start = ns;
        c->offset = drag.b + (ns - drag.a);
        c->length = drag.c - (ns - drag.a);
        UNLOCK();
        break;
    }
    case D_PLAYHEAD:
        LOCK();
        P.playhead = x_to_frame(mx);
        UNLOCK();
        break;
    case D_LOOP: {
        long f = snap(x_to_frame(mx));
        LOCK();
        P.loop_start = f < drag.a ? f : drag.a;
        P.loop_end = f < drag.a ? drag.a : f;
        if (P.loop_end > P.loop_start) P.loop = 1;
        UNLOCK();
        break;
    }
    case D_NUM: {
        double d = ((mx - drag.sx) - (my - drag.sy)) * drag.nstep * ((mods & KMOD_SHIFT) ? 0.1 : 1.0);
        double v = drag.norig + d;
        *drag.num = v < drag.nmin ? drag.nmin : v > drag.nmax ? drag.nmax : v;
        break;
    }
    case D_ED:                          /* a handle in the clip editor window */
        editor_drag();
        break;
    case D_SLIDE: {                     /* sx, sy: the slider's x and width */
        double v = (double)(mx - drag.sx) / (drag.sy > 1 ? drag.sy : 1);
        *drag.num = v < 0 ? 0 : v > 1 ? 1 : v;
        break;
    }
    case D_NOTE: {
        int v = drag.iorig + ((mx - drag.sx) - (my - drag.sy)) / 8;
        *drag.inum = v < drag.imin ? drag.imin : v > drag.imax ? drag.imax : v;
        break;
    }
    case D_FADEIN:
    case D_FADEOUT: {
        Clip *c = &P.clips[drag.clip];
        long f = (long)x_to_frame_raw(mx);
        long v = drag.kind == D_FADEIN ? f - c->start : c->start + c->length - f;
        long other = drag.kind == D_FADEIN ? c->fade_out : c->fade_in;
        v = v < 0 ? 0 : v > c->length - other ? c->length - other : v;
        LOCK();
        if (drag.kind == D_FADEIN) c->fade_in = v;
        else c->fade_out = v;
        UNLOCK();
        break;
    }
    case D_TVOL: {
        SDL_Rect vb = volume_bar(drag.track);
        float v = (float)(mx - vb.x) / vb.w;
        P.tracks[drag.track].volume = v < 0 ? 0 : v > 1 ? 1 : v;
        break;
    }
    }
}

/* Clips dropped on the library: one new sound of them, with their tracks' effects, saved as a
 * WAV file in Music/tinydaw/Sounds (so the project can find it again). */
static void bounce_clips(const int *list, int n);

static void bounce_to_library(void)
{
    int list[MAX_CLIPS], n = 0;
    if (drag.clip >= 0 && drag.clip < P.nclips && csel[drag.clip]) {
        for (int c = 0; c < P.nclips; c++)
            if (csel[c]) list[n++] = c;
    } else if (drag.clip >= 0 && drag.clip < P.nclips) {
        list[n++] = drag.clip;
    }
    bounce_clips(list, n);
}

static void bounce_clips(const int *list, int n)
{
    if (!n) return;
    char dir[900], name[96], file[96], path[1200], stamp[32], err[300];
    user_dir("music", dir, sizeof dir);
    snprintf(dir + strlen(dir), sizeof dir - strlen(dir), "/tinydaw/Sounds");
    ensure_dir(dir);
    const Clip *c0 = &P.clips[list[0]];
    if (n == 1) snprintf(name, sizeof name, "%.80s fx", c0->pattern > 0 ? "Notes" : P.samples[c0->sample].name);
    else snprintf(name, sizeof name, "%d clips together", n);
    int k = 0;                                   /* a file name from it: letters, digits, - and _ */
    for (const char *q = name; *q && k < 60; q++)
        file[k++] = isalnum((unsigned char)*q) || *q == '-' ? *q : '_';
    file[k] = 0;
    time_t now = time(NULL);
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&now));
    snprintf(path, sizeof path, "%s/%s-%s.wav", dir, file, stamp);
    for (int i = 2; file_exists(path) && i < 100; i++) snprintf(path, sizeof path, "%s/%s-%s-%d.wav", dir, file, stamp, i);
    status("Rendering...");
    int idx = sample_bounce(&P, list, n, path, name, err, sizeof err);
    if (idx < 0) {
        status("Couldn't make a sound of that: %s", err);
        return;
    }
    lib_flash = idx;
    lib_flash_at = SDL_GetTicks();
    lib_scroll = P.nsamples;
    status("Made \"%s\" (%.2f s) with its effects, saved as %s", P.samples[idx].name,
           (double)P.samples[idx].frames / SR, path_base(path));
}

static void end_drag(void)
{
    if (drag.kind == D_MOVE && over_library()) {
        group_place(0, 0);                      /* the clips stay where they were */
        bounce_to_library();
        drag.kind = D_NONE;
        return;
    }
    if (drag.kind == D_SLIDE) slide_done = drag.num;
    if (drag.kind == D_NOTE && abs(mx - drag.sx) + abs(my - drag.sy) < 3) {
        *drag.inum = drag.iorig;                /* a click, not a drag: type the note instead */
        numedit.active = 1;
        numedit.pending = 0;
        numedit.v = NULL;
        numedit.iv = drag.inum;
        numedit.lo = drag.imin;
        numedit.hi = drag.imax;
        numedit.buf[0] = 0;
        SDL_StartTextInput();
        status("Type a note like C4, F#3 or Bb2 (or a number), Enter to set it, Esc to cancel");
    }
    if (drag.kind == D_NUM && abs(mx - drag.sx) + abs(my - drag.sy) < 3) {
        *drag.num = drag.norig;                 /* a click, not a drag: type the value instead */
        numedit_begin(drag.num, drag.nmin, drag.nmax);
    }
    SDL_Rect area = {L.tl_x, L.lanes_y, L.tl_w, lanes_h()};
    if (drag.kind == D_NMOVE || drag.kind == D_NLEN || drag.kind == D_NBAND || drag.kind == D_VEL) roll_drag_end();
    if (drag.kind == D_LIB && pads_drop(drag.sample)) {
        /* dropped onto a pad or the instrument slot */
    } else if (drag.kind == D_LIB && hit(area)) {
        const Sample *s = &P.samples[drag.sample];
        int c = clip_add(drag.sample, lane_at(my), snap(x_to_frame(mx)), 0, s->frames);
        if (c >= 0) {
            selected = c;
            status("Placed \"%s\" on track %d", s->name, lane_at(my) + 1);
        }
    } else if (drag.kind == D_LIBPEND) {
        preview(drag.sample);
    } else if (drag.kind == D_MOVE && abs(mx - drag.sx) + abs(my - drag.sy) < 3 && !(mods & (KMOD_SHIFT | KMOD_CTRL))) {
        sel_only(drag.clip);            /* a click on one clip of a selection picks just that one */
    } else if (drag.kind == D_BAND) {
        int n = sel_count();
        if (n) status("%d clip%s selected   Delete, arrows, Ctrl+D, Ctrl+C/V, [ ], Ctrl+R act on all of them", n,
                      n == 1 ? "" : "s");
    } else if (drag.kind == D_BEAT && hit(area)) {
        place_beat(drag.sample, snap(x_to_frame(mx)), lane_at(my));
    }
    drag.kind = D_NONE;
}


/* The tempo went from `from` to P.bpm: the selected clips keep their place in bars and beats
 * and play at the new speed. Recipes made at a tempo (beats, chords, tones, sung lines) are made
 * again at it, audio is time-stretched (same pitch), note clips' notes move with them, and
 * one-shots (drum hits, Generator sounds) just move. Clips not selected keep their times. */
static void tempo_apply(double from)
{
    double r = from / P.bpm;                     /* how much longer everything gets */
    int n = 0, stretched = 0, remade = 0;
    if (fabs(r - 1) < 1e-9) return;
    for (int c = 0; c < P.nclips; c++) {
        if (!csel[c]) continue;
        Clip cl = P.clips[c];
        n++;
        cl.start = (long)llround(cl.start * r);
        if (cl.pattern > 0) {                    /* note clips: the notes move with it */
            int shared = 0;
            for (int o = 0; o < P.nclips; o++) shared |= o != c && P.clips[o].pattern == cl.pattern && !csel[o];
            int pat = cl.pattern;
            if (shared) {                        /* copies not selected keep their timing */
                int id = pattern_new_id(&P), nn = P.nnotes;
                for (int i = 0; i < nn; i++)
                    if (P.notes[i].pattern == pat) note_add(id, P.notes[i].note, P.notes[i].start, P.notes[i].length, P.notes[i].vel);
                pat = id;
            }
            int done = 0;                        /* a pattern several selected clips share: once */
            for (int o = 0; o < c && !done; o++) done = csel[o] && P.clips[o].pattern == pat && !shared;
            LOCK();
            if (!done)
                for (int i = 0; i < P.nnotes; i++)
                    if (P.notes[i].pattern == pat) {
                        P.notes[i].start = (long)llround(P.notes[i].start * r);
                        P.notes[i].length = (long)llround(P.notes[i].length * r);
                    }
            cl.pattern = pat;
            cl.offset = (long)llround(cl.offset * r);
            cl.length = (long)llround(cl.length * r);
            cl.fade_in = (long)llround(cl.fade_in * r);
            cl.fade_out = (long)llround(cl.fade_out * r);
            P.clips[c] = cl;
            UNLOCK();
            continue;
        }
        int ns = sample_at_tempo(&P, cl.sample, r);
        if (ns >= 0) {
            const Sample *sm = &P.samples[ns];
            if (sm->pitched && sm->stretch > 0 && fabs(sm->stretch - 1) > 1e-6) stretched++;
            else remade++;
            cl.sample = ns;
            cl.offset = (long)llround(cl.offset * r);
            cl.length = (long)llround(cl.length * r);
            cl.fade_in = (long)llround(cl.fade_in * r);
            cl.fade_out = (long)llround(cl.fade_out * r);
        } else {                                 /* a one-shot: same sound, its musical length */
            cl.length = (long)llround(cl.length * r);
        }
        long room = P.samples[cl.sample].frames - cl.offset;
        if (cl.length > room) cl.length = room;
        if (cl.length < 1) cl.length = 1;
        if (cl.fade_in + cl.fade_out > cl.length) cl.fade_in = cl.fade_out = 0;
        LOCK();
        P.clips[c] = cl;
        UNLOCK();
    }
    if (n)
        status("Tempo %.1f BPM: %d selected clip%s now follow%s it (%d remade, %d time-stretched)", P.bpm, n,
               n == 1 ? "" : "s", n == 1 ? "s" : "", remade, stretched);
}

/* in each frame: a tempo edit that has settled applies to the selection */
static void tempo_check(void)
{
    if (!tempo_from) return;
    if ((drag.kind == D_NUM && drag.num == &P.bpm) || (numedit.active && numedit.v == &P.bpm)) return;
    if (SDL_GetTicks() - tempo_at < 350) return;
    double from = tempo_from;
    tempo_from = 0;
    if (sel_count()) tempo_apply(from);
}

/* ---------------------------------------------------------------- keyboard */

static void key(SDL_Keysym k)
{
    int ctrl = k.mod & KMOD_CTRL, hk = 0, hi = 0;
    long nudge = snap_frames() ? snap_frames() : SR / 100;
    if (export_open) {                  /* the dialog takes the keyboard */
        if (SDL_AtomicGet(&fd_state) != 0) return;          /* the system file dialog is up */
        if (k.sym == SDLK_ESCAPE) export_open = 0;
        if ((k.sym == SDLK_RETURN || k.sym == SDLK_KP_ENTER) && export_name[0] && dir_writable(export_dir)) {
            export_open = 0;
            export_pending = 1;
        }
        return;
    }
    if (show_open) {                    /* the show takes the keyboard */
        if (k.sym == SDLK_ESCAPE || k.sym == SDLK_F11) show_toggle();
        else if (k.sym == SDLK_RIGHT || k.sym == SDLK_LEFT) show_go(show_scene + (k.sym == SDLK_RIGHT ? 1 : -1));
        else if (k.sym == SDLK_TAB) {
            show_cycle = !show_cycle;
            show_switch_at = show_wall + show_each;
            settings_save();
        }
        else if (k.sym == SDLK_SPACE) toggle_play();
        else if (k.sym == SDLK_HOME) {
            LOCK();
            P.playhead = P.loop ? P.loop_start : 0;
            UNLOCK();
        }
        return;
    }
    if (confirm_act) {                  /* "save changes?" is up */
        if (k.sym == SDLK_ESCAPE) confirm_act = ACT_NONE;
        return;
    }
    if (roll_clip >= 0 && !dd.open && roll_key(k)) return;
    if (roll_clip < 0 && (k.sym == SDLK_RETURN || k.sym == SDLK_KP_ENTER) && selected >= 0 && selected < P.nclips &&
        P.clips[selected].pattern > 0) {
        roll_open(selected);
        return;
    }
    if ((menu_open || met_open || dd.open) && k.sym == SDLK_ESCAPE) {
        menu_open = met_open = dd.open = 0;
        return;
    }
    if (learn_pad >= 0 && k.sym == SDLK_ESCAPE) {
        learn_pad = -1;
        status("Learning cancelled");
        return;
    }
    if (ctrl) {
        int shift = k.mod & KMOD_SHIFT;
        switch (k.sym) {
        case SDLK_z: undo_redo(shift != 0); return;
        case SDLK_y: undo_redo(1); return;
        case SDLK_s: save_project(shift != 0); return;
        case SDLK_o: request(ACT_OPEN, NULL); return;
        case SDLK_n: request(ACT_NEW, NULL); return;
        case SDLK_r:                    /* reverse: every selected clip follows the one clicked last */
            if (selected >= 0) {
                int rev = !clip_rev(&P.clips[selected]);
                for (int i = 0; i < P.nclips; i++)
                    if (csel[i]) clip_variant(i, clip_semis(&P.clips[i]), rev);
                status(rev ? "Reversed %d clip%s" : "%d clip%s play forwards", sel_count(), sel_count() == 1 ? "" : "s");
            }
            return;
        case SDLK_1: case SDLK_2: case SDLK_3: case SDLK_4: case SDLK_5: case SDLK_6:
            open_panel(k.sym - SDLK_1);
            return;
        case SDLK_COMMA:
            open_panel(RI_SETTINGS);
            return;
        case SDLK_b: {                  /* hide the panel, or bring the last one back */
            int any = 0;
            for (int i = 0; i < RI_COUNT; i++) any |= *RAIL_FLAG[i];
            if (any) {
                for (int i = 0; i < RI_COUNT; i++) *RAIL_FLAG[i] = 0;
            } else {
                open_panel(last_panel);
            }
            return;
        }
        case SDLK_q:
            request(ACT_QUIT, NULL);
            return;
        case SDLK_a:
            for (int i = 0; i < P.nclips; i++) csel[i] = 1;
            sel_fix_primary();
            status("All %d clips selected", P.nclips);
            return;
        case SDLK_c:
            if (sound_under_mouse(&hk, &hi) && !(hk == CM_LIB && hi < 0)) {   /* over a sound or pad: copy it */
                sound_copy(hk, hi);
                return;
            }
            if (sel_count()) {
                copy_selection();
                status("Copied %d clip%s: Ctrl+V pastes them at the playhead", nclipboard, nclipboard == 1 ? "" : "s");
            }
            return;
        case SDLK_x:
            if (sel_count()) {
                copy_selection();
                status("Cut %d clip%s", delete_selection(), nclipboard == 1 ? "" : "s");
            }
            return;
        case SDLK_v:
            if (sclip.has && sound_under_mouse(&hk, &hi)) {
                sound_paste(hk, hi);
                return;
            }
            if (nclipboard) status("Pasted %d clip%s", paste_at(snap(P.playhead)), nclipboard == 1 ? "" : "s");
            return;
        }
    }
    switch (k.sym) {
    case SDLK_SPACE: toggle_play(); break;
    case SDLK_HOME:
        LOCK();
        P.playhead = P.loop ? P.loop_start : 0;
        UNLOCK();
        view_start = (double)P.playhead / SR;
        break;
    case SDLK_DELETE:
    case SDLK_BACKSPACE:
        if (sel_count()) {
            int n = delete_selection();
            status("Deleted %d clip%s", n, n == 1 ? "" : "s");
        }
        break;
    case SDLK_d:                        /* Ctrl+D: the selection again, straight after itself */
        if (ctrl && sel_count()) {
            long a, b;
            selection_span(&a, &b);
            duplicate_selection(b - a);
        }
        break;
    case SDLK_l: toggle_loop(); break;
    case SDLK_END: playhead_to(project_end(&P)); break;
    case SDLK_f:                        /* bring the playhead into the middle of the view */
        view_start = (double)P.playhead / SR - L.tl_w / 2.0 / pps;
        if (view_start < 0) view_start = 0;
        break;
    case SDLK_z: zoom_to_fit(); break;
    case SDLK_t: tap_tempo(); break;
    case SDLK_m:
        MET.on = !MET.on;
        settings_save();
        status(MET.on ? "Metronome on" : "Metronome off");
        break;
    case SDLK_e:
        if (ctrl) open_export();
        break;
    case SDLK_LEFT:
    case SDLK_RIGHT:
        if (k.mod & KMOD_CTRL) {        /* Ctrl: the playhead by a bar, Ctrl+Shift by a beat */
            playhead_step(k.mod & KMOD_SHIFT ? beat_frames() : 4 * beat_frames(), k.sym == SDLK_RIGHT ? 1 : -1);
            break;
        }
        if (k.mod & KMOD_ALT) {         /* Alt: to the next/previous clip edge */
            playhead_to_edge(k.sym == SDLK_RIGHT ? 1 : -1);
            break;
        }
        if (sel_count()) {
            group_remember();
            group_place(k.sym == SDLK_LEFT ? -nudge : nudge, 0);
        }
        break;
    case SDLK_UP:
    case SDLK_DOWN:
        if (sel_count()) {
            group_remember();
            group_place(0, k.sym == SDLK_UP ? -1 : 1);
        }
        break;
    case SDLK_EQUALS:
    case SDLK_KP_PLUS: zoom(1.25, L.tl_x + L.tl_w / 2); break;
    case SDLK_MINUS:
    case SDLK_KP_MINUS: zoom(0.8, L.tl_x + L.tl_w / 2); break;
    case SDLK_ESCAPE: sel_clear(); break;
    case SDLK_F1: open_panel(RI_THEORY); break;
    case SDLK_F2: open_panel(RI_PADS); break;
    case SDLK_F3: open_panel(RI_MIXER); break;
    case SDLK_F4: open_panel(RI_VISUAL); break;
    case SDLK_F11: show_toggle(); break;
    case SDLK_PAGEUP:
    case SDLK_PAGEDOWN: {               /* scroll the open pane, or the library */
        int *sc = theory_open ? &th_scroll : pads_open ? &pads_scroll : mixer_open ? &mixer_scroll
                : vis_open ? &vis_scroll : settings_open ? &settings_scroll : sounds_open && sounds_sub ? &sounds_scroll : NULL;
        int page = (H - L.top - L.status) * 9 / 10, dir = k.sym == SDLK_PAGEUP ? -1 : 1;
        if (sc) *sc += dir * page;          /* clamped when the pane is drawn */
        else lib_scroll += dir * 8;
        break;
    }
    case SDLK_LEFTBRACKET:
    case SDLK_RIGHTBRACKET:             /* transpose the selected clips */
        if (sel_count()) {
            int step = k.sym == SDLK_RIGHTBRACKET ? 1 : -1;
            for (int i = 0; i < P.nclips; i++) {
                if (!csel[i]) continue;
                int st = clip_semis(&P.clips[i]) + step;
                if (st >= -24 && st <= 24) clip_variant(i, st, clip_rev(&P.clips[i]));
            }
            if (selected >= 0) status("Clip pitch %+d semitones%s", clip_semis(&P.clips[selected]),
                                      sel_count() > 1 ? " (all selected moved by one)" : "");
        }
        break;
    }
}

/* ---------------------------------------------------------------- audio */

static Mixer *MX;                       /* the live mixer: its effects keep ringing between callbacks */

static void audio_cb(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    float *out = (float *)stream;
    int frames = len / (int)(2 * sizeof(float));
    memset(stream, 0, (size_t)len);
    int done = 0;
    if (P.playing && countin_left > 0) {        /* the count-in: clicks only, the song waits */
        int n = countin_left < frames ? (int)countin_left : frames;
        mixer_run(MX, &P, P.playhead, 0, out, n, -1, 1);
        metronome_mix(out, countin_total - countin_left, n, 1);
        countin_left -= n;
        done = n;
    }
    if (!P.playing) mixer_run(MX, &P, P.playhead, 0, out, frames, -1, 1);   /* pads, instrument, tails */
    if (P.playing) {
        while (done < frames) {
            int n = frames - done;
            int looping = P.loop && P.loop_end > P.loop_start && P.playhead < P.loop_end;
            if (looping && P.playhead + n > P.loop_end) n = (int)(P.loop_end - P.playhead);
            if (n <= 0) {
                P.playhead = P.loop_start;
                continue;
            }
            mixer_run(MX, &P, P.playhead, 1, out + 2 * done, n, -1, 1);
            metronome_mix(out + 2 * done, P.playhead, n, 0);
            P.playhead += n;
            done += n;
            if (looping && P.playhead >= P.loop_end) P.playhead = P.loop_start;
        }
    }
    if (P.preview >= 0) {
        const Sample *s = &P.samples[P.preview];
        for (int i = 0; i < frames && P.preview_pos < s->frames; i++, P.preview_pos++) {
            out[2 * i] += s->data[2 * P.preview_pos] * 0.9f;
            out[2 * i + 1] += s->data[2 * P.preview_pos + 1] * 0.9f;
        }
        if (P.preview_pos >= s->frames) P.preview = -1;
    }
    if (scratch) {
        for (int i = 0; i < frames && scratch_pos < scratch_len; i++, scratch_pos++) {
            out[2 * i] += scratch[2 * scratch_pos];
            out[2 * i + 1] += scratch[2 * scratch_pos + 1];
        }
    }
    for (int i = 0; i < 2 * frames; i++)
        out[i] = soft_clip(out[i] * P.master);
    for (int i = 0; i < frames; i++) {          /* for the meters and the visualizer */
        vis_ring[2 * vis_pos] = out[2 * i];
        vis_ring[2 * vis_pos + 1] = out[2 * i + 1];
        vis_pos = (vis_pos + 1) % VIS_RING;
    }
}

/* (Re)opens the output at the project rate. SDL converts if the hardware runs at another rate. */
static void open_audio(void)
{
    if (dev) {
        SDL_CloseAudioDevice(dev);
        dev = 0;
    }
    if (mixer_rate(MX) != SR) {         /* the device is closed: nothing is using the old one */
        mixer_free(MX);
        MX = mixer_new();
    }
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = SR;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = SR > 96000 ? 2048 : SR > 48000 ? 1024 : 512;   /* about 10 ms */
    want.callback = audio_cb;
    dev = SDL_OpenAudioDevice(audio_out[0] ? audio_out : NULL, 0, &want, &have, 0);
    if (!dev && audio_out[0]) {         /* that device has gone: use the default */
        status("Output \"%s\" isn't available: using the default", audio_out);
        dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    }
    if (dev) SDL_PauseAudioDevice(dev, 0);
    else status("No audio output: %s", SDL_GetError());
}

static void set_rate(int rate)
{
    char msg[300];
    double ratio = (double)rate / SR;
    LOCK();
    project_set_rate(&P, rate, msg, sizeof msg);
    UNLOCK();
    history_scale(ratio);
    open_audio();
    metronome_prepare();
    status("%s", msg);
}

/* ---------------------------------------------------------------- demo */

static int gen(int wave, int note, double beats, double vol, double att, double rel)
{
    GenParams g = {wave, note, beats, vol, att, rel, P.a4};
    return sample_generate(&P, &g);
}

static void make_demo(void)
{
    int kick = gen(WAVE_KICK, 36, 0.5, 0.75, 0, 0);
    int snare = gen(WAVE_SNARE, 60, 0.5, 0.5, 0, 0);
    int hat = gen(WAVE_HAT, 60, 0.25, 0.45, 0, 0);
    int bass[3] = {gen(WAVE_SAW, 33, 0.75, 0.35, 3, 60), gen(WAVE_SAW, 36, 0.75, 0.35, 3, 60),
                   gen(WAVE_SAW, 31, 0.75, 0.35, 3, 60)};
    int pad[3] = {gen(WAVE_TRIANGLE, 57, 4, 0.22, 400, 900), gen(WAVE_TRIANGLE, 60, 4, 0.22, 400, 900),
                  gen(WAVE_TRIANGLE, 64, 4, 0.22, 400, 900)};
    int lead = gen(WAVE_SQUARE, 76, 0.5, 0.16, 5, 120);
    long bf = beat_frames();
    for (int bar = 0; bar < 2; bar++) {
        long b0 = bar * 4 * bf;
        for (int b = 0; b < 4; b++) clip_add(kick, 0, b0 + b * bf, 0, P.samples[kick].frames);
        clip_add(snare, 1, b0 + bf, 0, P.samples[snare].frames);
        clip_add(snare, 1, b0 + 3 * bf, 0, P.samples[snare].frames);
        for (int h = 0; h < 8; h++) clip_add(hat, 2, b0 + h * bf / 2, 0, P.samples[hat].frames);
        static const double bass_at[4] = {0, 1.5, 2, 3};
        static const int bass_note[4] = {0, 0, 1, 2};
        for (int i = 0; i < 4; i++) {
            int s = bass[bass_note[i]];
            clip_add(s, 3, b0 + (long)(bass_at[i] * bf), 0, P.samples[s].frames);
        }
        for (int i = 0; i < 3; i++) clip_add(pad[i], 4 + i, b0, 0, P.samples[pad[i]].frames);
    }
    clip_add(lead, 7, 7 * bf, 0, P.samples[lead].frames);
    clip_add(lead, 7, (long)(7.5 * bf), 0, P.samples[lead].frames);
    P.loop_start = 0;
    P.loop_end = 8 * bf;
    P.loop = 1;
    status("Demo loaded: press Space to play");
}

/* ---------------------------------------------------------------- self-test */

static void frame(void);
static void preview_notes(const int *notes, int n, double beats);
static void place_progression(const Progression *pr);
static int roll_put_chords(const int (*notes)[6], const int *counts, int nch, double beats, const char *what);
static int roll_put_chord(const int *notes, int n, double beats, const char *name);
static SDL_Rect tab_rect(int pos, int x, int y, int w);
static BeatSpec genre_spec(int g);
static int gn_sel, gn_bars, gn_split;
static char gn_pat[32][DRUM_LANES][33];
static SDL_Rect gn_handle, gn_cell[DRUM_LANES][32];

/* Feed one synthetic mouse step through the same path as real input. */
static void sim(int x, int y, int press, int release, int right, SDL_Keymod m)
{
    mx = x;
    my = y;
    lpress = press;
    lrel = release;
    rpress = right;
    wheel_x = wheel_y = 0;
    if (press) ldown = 1;
    if (release) ldown = 0;
    mods = m;
    if (lrel && drag.kind != D_NONE) update_drag();
    frame();
}

static void sim_drag(int x0, int y0, int x1, int y1, SDL_Keymod m)
{
    sim(x0, y0, 1, 0, 0, m);
    for (int i = 1; i <= 8; i++) sim(x0 + (x1 - x0) * i / 8, y0 + (y1 - y0) * i / 8, 0, 0, 0, m);
    sim(x1, y1, 0, 1, 0, m);
}

/* the clip editor's mouse, for the self-test: one editor frame */
static void ed_sim(int x, int y, int press, int release)
{
    ein.mx = x;
    ein.my = y;
    ein.lpress = press;
    ein.lrel = release;
    if (press) ein.ldown = 1;
    if (release) ein.ldown = 0;
    if (release && drag.kind != D_NONE && drag_in_editor) {   /* the final position, as the main loop does */
        int sx = mx, sy = my;
        mx = x;
        my = y;
        update_drag();
        mx = sx;
        my = sy;
    }
    editor_frame();
}

static int failures;
static void check(int ok, const char *what)
{
    printf("  %s  %s\n", ok ? "pass" : "FAIL", what);
    failures += !ok;
}

static int lane_y(int t) { return track_y(t) + L.lane_h / 2; }

static int dropdown_probe_snap(int x) { return x >= snap_rect.x && x < snap_rect.x + 4; }
static int snap_arrow_x(void) { return snap_rect.x + snap_rect.w - 11; }

static float *test_render(Project *p, long a, long n, int only)
{
    float *b = calloc((size_t)n * 2, sizeof(float));
    Mixer *m = mixer_new();
    for (long pos = 0; pos < n; pos += 1024) {
        int k = n - pos < 1024 ? (int)(n - pos) : 1024;
        mixer_run(m, p, a + pos, 1, b + 2 * pos, k, only, 0);
    }
    mixer_free(m);
    return b;
}

static double test_rms(const float *b, long a, long e, int ch)   /* ch 0 left, 1 right, 2 both */
{
    double sum = 0;
    long n = 0;
    for (long i = a; i < e; i++)
        for (int c = 0; c < 2; c++)
            if (ch == 2 || ch == c) {
                sum += (double)b[2 * i + c] * b[2 * i + c];
                n++;
            }
    return n ? sqrt(sum / n) : 0;
}

static int selftest(void)
{
    GenParams g = {WAVE_SINE, 69, 2.0, 0.5, 5, 50, 440};
    sample_generate(&P, &g);
    long bf = beat_frames();
    snap_i = 3;                                  /* 1/4 = one beat */
    frame();

    SDL_Rect item = lib_rects[0];
    sim_drag(item.x + 30, item.y + 10, (int)frame_to_x(2 * bf) + 3, lane_y(2), 0);
    check(P.nclips == 1, "drag from library creates a clip");
    check(P.nclips == 1 && P.clips[0].track == 2, "clip lands on the track under the pointer");
    check(P.nclips == 1 && P.clips[0].start == 2 * bf, "clip start snaps to the beat");
    check(P.nclips == 1 && P.clips[0].length == P.samples[0].frames, "clip plays the whole sound");

    SDL_Rect r = clip_rect(&P.clips[0]);
    int beat_px = (int)(60.0 / P.bpm * pps);
    sim_drag(r.x + r.w / 2, r.y + r.h / 2, r.x + r.w / 2 + beat_px, lane_y(5), 0);
    check(P.clips[0].start == 3 * bf && P.clips[0].track == 5, "drag moves the clip in time and between tracks");

    r = clip_rect(&P.clips[0]);
    sim_drag(r.x + r.w - 2, r.y + r.h / 2, r.x + r.w - 2 - beat_px, r.y + r.h / 2, 0);
    check(P.clips[0].length == bf, "dragging the right edge trims the end");

    r = clip_rect(&P.clips[0]);                  /* clip: start 3, length 1 beat */
    sim_drag(r.x + r.w - 2, r.y + r.h / 2, r.x + r.w - 2 + beat_px, r.y + r.h / 2, 0);
    check(P.clips[0].length == 2 * bf, "dragging the right edge back out restores the end");

    r = clip_rect(&P.clips[0]);
    sim_drag(r.x + 2, r.y + r.h / 2, r.x + 2 + beat_px, r.y + r.h / 2, 0);
    check(P.clips[0].start == 4 * bf && P.clips[0].offset == bf && P.clips[0].length == bf,
          "dragging the left edge trims the start (later start, skips into the sound)");

    r = clip_rect(&P.clips[0]);
    sim_drag(r.x + 2, r.y + r.h / 2, r.x + 2 - 2 * beat_px, r.y + r.h / 2, 0);
    check(P.clips[0].start == 3 * bf && P.clips[0].offset == 0 && P.clips[0].length == 2 * bf,
          "left edge stops at the beginning of the sound");

    r = clip_rect(&P.clips[0]);
    sim_drag(r.x + r.w / 2, r.y + r.h / 2, r.x + r.w / 2 + 2 * beat_px, r.y + r.h / 2, KMOD_CTRL);
    check(P.nclips == 2 && P.clips[0].start == 3 * bf && P.clips[1].start == 5 * bf,
          "Ctrl+drag copies the clip and leaves the original");

    r = clip_rect(&P.clips[1]);
    sim(r.x + r.w / 2, r.y + r.h / 2, 0, 0, 1, 0);
    check(P.nclips == 1, "right-click deletes a clip");

    sim(item.x + 30, item.y + 10, 1, 0, 0, 0);
    sim(item.x + 30, item.y + 10, 0, 1, 0, 0);
    check(P.preview == 0, "clicking a library sound previews it");

    sim(L.tl_x + 200, L.top + 10, 1, 0, 0, 0);
    sim(L.tl_x + 200, L.top + 10, 0, 1, 0, 0);
    check(labs(P.playhead - x_to_frame(L.tl_x + 200)) <= 1, "clicking the ruler moves the playhead");

    /* theory: spelling and chords */
    char name[64];
    int t;
    char num[24];
    diatonic_chord(0, 0, 6, 0, &t, num, sizeof num);
    diatonic_chord_name(0, 0, 6, t, name, sizeof name);
    check(!strcmp(name, "Bdim") && !strcmp(num, "vii°"), "C major vii is Bdim");
    diatonic_chord(3, 1, 5, 0, &t, num, sizeof num);
    diatonic_chord_name(3, 1, 5, t, name, sizeof name);
    check(!strcmp(name, "Cb"), "Eb minor bVI is spelled Cb");
    chord_tone_names(7, CH_MIN, 0, name, sizeof name);
    check(!strcmp(name, "G Bb D"), "Gm is spelled G Bb D");
    chord_tone_names(7, CH_DIM7, 0, name, sizeof name);
    check(!strcmp(name, "G Bb Db Fb"), "Gdim7 is spelled G Bb Db Fb");
    note_above(6, 1, 3, 5, name, sizeof name);
    check(!strcmp(name, "Cb"), "IV of Gb major is Cb");

    /* theory pane: tabs, previews, placing a progression */
    theory_open = 1;
    sim(-1, -1, 0, 0, 0, 0);
    th_scroll = 0;
    SDL_Rect ref_tab = tab_rect(3, L.pane_x + 14, L.top + 10, PANE_W - 28);   /* Beats, Keys, Chords, Reference */
    sim(ref_tab.x + ref_tab.w / 2, ref_tab.y + 14, 1, 0, 0, 0);
    sim(ref_tab.x + ref_tab.w / 2, ref_tab.y + 14, 0, 1, 0, 0);
    check(theory_tab == 1, "clicking Reference opens its first page, the circle of fifths");
    int chord[3] = {57, 60, 64};
    preview_notes(chord, 3, 2);
    check(scratch && scratch_len == 2 * beat_frames(), "chord preview renders two beats");
    int before = P.nclips, samples_before = P.nsamples;
    th_root = 9;
    th_track = 4;
    LOCK();
    P.playhead = 4 * beat_frames() + 10;       /* nearest bar: bar 2 */
    UNLOCK();
    place_progression(&PROGRESSIONS[1]);       /* I - V - vi - IV in A: A E F#m D */
    check(P.nclips == before + 4, "placing a progression adds one clip per chord");
    check(P.clips[before].start == 4 * beat_frames() && P.clips[before + 3].start == 16 * beat_frames() &&
              P.clips[before + 2].track == 4, "chords start at the nearest bar, one per bar, on the chosen track");
    check(!strncmp(P.samples[P.clips[before + 2].sample].name, "F#m ", 4), "third chord is F#m");
    place_progression(&PROGRESSIONS[1]);
    check(P.nsamples == samples_before + 4, "placing again reuses the same chord sounds");
    long fr = P.samples[P.clips[before].sample].frames;
    char msg[300];
    LOCK();
    project_set_rate(&P, 96000, msg, sizeof msg);
    UNLOCK();
    check(labs(P.samples[P.clips[before].sample].frames - 2 * fr) <= 2, "chords rebuild at a new project rate");

    /* genres: rendering, placing on new rows, splitting, editing, dragging */
    BeatSpec hb = genre_spec(0);                 /* House */
    long hl;
    float *hd = render_beat(&hb, (1 << DRUM_LANES) - 1, P.bpm, &hl);
    float hpeak = 0, first_ms = 0;
    for (long i = 0; i < 2 * hl; i++) hpeak = fabsf(hd[i]) > hpeak ? fabsf(hd[i]) : hpeak;
    for (long i = 0; i < 2 * (SR / 100); i++) first_ms = fabsf(hd[i]) > first_ms ? fabsf(hd[i]) : first_ms;
    free(hd);
    check(hl == (long)llround(16 * 60.0 / P.bpm / 4 * SR), "a 16-step beat lasts exactly one bar");
    check(hpeak > 0.2f && hpeak < 1.5f && first_ms > 0.1f, "the beat has sound, with the kick on step 1");

    theory_tab = 6;
    gn_sel = 0;
    gn_split = 0;
    gn_bars = 4;
    int rows0 = P.ntracks, clips0 = P.nclips;
    place_beat(0, 0, -1);
    check(P.ntracks == rows0 + 1 && P.nclips == clips0 + 4, "Place adds one new row with 4 one-bar clips");
    check(!strcmp(P.tracks[rows0].name, "House beat"), "the new row is named after the genre");

    gn_split = 1;
    gn_bars = 2;
    rows0 = P.ntracks;
    clips0 = P.nclips;
    place_beat(0, 0, -1);                        /* House uses kick, clap, hat, open hat */
    check(P.ntracks == rows0 + 4 && P.nclips == clips0 + 8, "Row per drum: 4 rows, 2 bars each");
    check(!strcmp(P.tracks[rows0].name, "House Kick") && !strcmp(P.tracks[rows0 + 3].name, "House Open"),
          "split rows are named per drum");

    gn_split = 0;
    gn_bars = 4;
    clips0 = P.nclips;
    place_beat(4, 0, -1);                        /* drum & bass: a 32-step (2-bar) groove */
    check(P.nclips == clips0 + 2, "a 2-bar groove repeats twice to fill 4 bars");

    gn_sel = 0;
    frame();
    char was = gn_pat[0][0][1];
    sim(gn_cell[0][1].x + 4, gn_cell[0][1].y + 4, 1, 0, 0, 0);
    sim(gn_cell[0][1].x + 4, gn_cell[0][1].y + 4, 0, 1, 0, 0);
    check(was == '.' && gn_pat[0][0][1] == 'x', "clicking a step turns it on");
    sim(gn_cell[0][1].x + 4, gn_cell[0][1].y + 4, 1, 0, 0, 0);
    sim(gn_cell[0][1].x + 4, gn_cell[0][1].y + 4, 0, 1, 0, 0);
    sim(gn_cell[0][1].x + 4, gn_cell[0][1].y + 4, 1, 0, 0, 0);
    sim(gn_cell[0][1].x + 4, gn_cell[0][1].y + 4, 0, 1, 0, 0);
    sim(gn_cell[0][1].x + 4, gn_cell[0][1].y + 4, 1, 0, 0, 0);
    sim(gn_cell[0][1].x + 4, gn_cell[0][1].y + 4, 0, 1, 0, 0);
    check(gn_pat[0][0][1] == '.', "clicking cycles accent, ghost and back to rest");

    clips0 = P.nclips;
    rows0 = P.ntracks;
    view_start = 0;                              /* the pane covers the right side: drop where visible */
    sim_drag(gn_handle.x + 40, gn_handle.y + 10, (int)frame_to_x(2 * beat_frames()) + 2, lane_y(1), 0);
    check(P.nclips == clips0 + 4 && P.ntracks == rows0, "dragging the beat drops it on an existing row");
    check(P.clips[clips0].track == 1 && P.clips[clips0].start == 2 * beat_frames(),
          "the dropped beat starts where it was dropped, on that row");
    check(P.clips[clips0 + 1].start == P.clips[clips0].start + P.clips[clips0].length,
          "the bars follow each other with no gap");

    /* undo and redo */
    frame();
    int n0 = P.nclips, u0 = nundo;
    clip_add(0, 0, 0, 0, P.samples[0].frames);
    frame();
    check(P.nclips == n0 + 1 && nundo == u0 + 1, "adding a clip makes one undo step");
    undo_redo(0);
    check(P.nclips == n0, "undo removes the clip just added");
    undo_redo(1);
    check(P.nclips == n0 + 1, "redo puts it back");
    sel_clear();
    view_start = 0;
    frame();
    Clip before_drag = P.clips[P.nclips - 1];
    r = clip_rect(&P.clips[P.nclips - 1]);
    u0 = nundo;
    sim_drag(r.x + r.w / 2, r.y + r.h / 2, r.x + r.w / 2 + 2 * (int)(60.0 / P.bpm * pps), r.y + r.h / 2, 0);
    check(nundo == u0 + 1 && P.clips[P.nclips - 1].start != before_drag.start, "a whole drag is a single undo step");
    undo_redo(0);
    check(P.clips[P.nclips - 1].start == before_drag.start, "undoing the drag puts the clip back");
    double bpm0 = P.bpm;
    P.bpm = bpm0 + 7;
    frame();
    undo_redo(0);
    check(P.bpm == bpm0, "tempo changes can be undone");

    /* saving and opening */
    char tpath[1100];
    const char *tmpdir = getenv("TMPDIR") ? getenv("TMPDIR") : getenv("TEMP") ? getenv("TEMP") : "/tmp";
    snprintf(tpath, sizeof tpath, "%s/tinydaw-selftest-%u.tdaw", tmpdir, (unsigned)SDL_GetTicks() ^ (unsigned)time(NULL));
    P.pads[2] = (Pad){1, PAD_HOLD, 0.5f, 3, 4, 0};
    P.tracks[1].mute = 1;
    frame();
    check(project_save(&P, tpath, msg, sizeof msg) == 0, "the project saves");
    Project *q = malloc(sizeof *q);
    int lr = project_load(q, tpath, msg, sizeof msg);
    check(lr == 0, "the saved project opens again");
    if (lr == 0) {
        int same = q->nclips == P.nclips && !memcmp(q->clips, P.clips, (size_t)P.nclips * sizeof(Clip)) &&
                   q->nsamples == P.nsamples && q->ntracks == P.ntracks && q->bpm == P.bpm &&
                   q->loop_start == P.loop_start && q->loop_end == P.loop_end;
        check(same, "clips, tracks, tempo and loop come back exactly");
        int names = 1;
        for (int i = 0; i < P.nsamples && i < q->nsamples; i++)
            names &= !strcmp(q->samples[i].name, P.samples[i].name) && q->samples[i].frames == P.samples[i].frames;
        check(names, "every sound is rebuilt with its name and length");
        check(q->samples[0].frames == P.samples[0].frames &&
                  !memcmp(q->samples[0].data, P.samples[0].data, (size_t)P.samples[0].frames * 2 * sizeof(float)),
              "a generated tone is rebuilt sample for sample");
        check(!memcmp(&q->pads[2], &P.pads[2], sizeof(Pad)) && q->tracks[1].mute, "pads and track settings are kept");
        project_free(q);
    }
    free(q);
    FILE *bad = fopen(tpath, "w");
    if (bad) {
        fputs("not a project\n", bad);
        fclose(bad);
    }
    q = malloc(sizeof *q);
    int old_rate = g_rate;
    check(project_load(q, tpath, msg, sizeof msg) != 0 && g_rate == old_rate, "a file that isn't a project is refused");
    free(q);
    project_save(&P, tpath, msg, sizeof msg);
    int clips_before = P.nclips;
    open_project(tpath, 0);
    check(P.nclips == clips_before && !is_dirty() && nundo == 0, "opening replaces the project, clean, with fresh history");
    P.bpm += 1;
    frame();
    check(is_dirty(), "an edit marks the project unsaved");
    undo_redo(0);
    check(!is_dirty(), "undoing back to the saved state is clean again");
    remove(tpath);

    /* pads */
    binds_default();
    P.pads[0] = (Pad){0, PAD_ONESHOT, 0.8f, 0, 2, 0};
    LOCK();
    voices_clear(&P);
    UNLOCK();
    input_event(SRC_KEY, binds[0].key, 110, 1, -1);
    check(voice_playing(&P, 0), "pressing a pad's key plays its sound");
    P.pads[1] = (Pad){0, PAD_HOLD, 0.8f, 0, 2, 0};
    input_event(SRC_JOY, binds[1].joy, 127, 1, 0);
    int held = voice_playing(&P, 1);
    input_event(SRC_JOY, binds[1].joy, 0, 0, 0);
    check(held && !voice_playing(&P, 1), "a Hold pad plays while its button is down");
    P.pads[2] = (Pad){0, PAD_LOOP, 0.8f, 0, 2, 0};
    input_event(SRC_MIDI, binds[2].midi, 100, 1, 0);
    input_event(SRC_MIDI, binds[2].midi, 0, 0, 0);
    int looping = voice_playing(&P, 2);
    input_event(SRC_MIDI, binds[2].midi, 100, 1, 0);
    check(looping && !voice_playing(&P, 2), "a Loop pad starts on one press and stops on the next");
    learn_pad = 3;
    input_event(SRC_MIDI, 60, 100, 1, 0);
    check(binds[3].midi == 60 && learn_pad < 0, "Learn binds the next MIDI note to the pad");
    learn_pad = 4;
    input_event(SRC_KEY, binds[0].key, 100, 1, -1);
    check(binds[4].key != -1 && binds[0].key == -1, "an input moves to the pad that learns it");
    char jl[32];
    joy_label(201, jl, sizeof jl);
    check(!strcmp(jl, "axis 0-"), "controller inputs have readable names");
    P.inst_sample = 0;
    P.inst_root = 60;
    inst_midi = 1;
    input_event(SRC_MIDI, 72, 100, 1, 0);
    int found_inst = 0;
    for (int i = 0; i < MAX_VOICES; i++)
        if (P.voices[i].active && P.voices[i].note == 72 && fabs(P.voices[i].step - 2.0) < 1e-9) found_inst = 1;
    check(found_inst, "a MIDI note not on a pad plays the instrument an octave up");
    float vbuf[2 * 256] = {0}, vpeak = 0;
    Mixer *vm = mixer_new();
    LOCK();
    mixer_run(vm, &P, 0, 0, vbuf, 256, -1, 1);
    UNLOCK();
    mixer_free(vm);
    for (int i = 0; i < 2 * 256; i++) vpeak = fabsf(vbuf[i]) > vpeak ? fabsf(vbuf[i]) : vpeak;
    check(vpeak > 0.01f, "live voices are heard in the output");
    LOCK();
    voices_clear(&P);
    UNLOCK();

    /* recording pad hits */
    binds_default();
    frame();
    u0 = nundo;
    pads_rec = 1;
    int c0 = P.nclips;
    LOCK();
    P.playing = 1;
    P.loop = 0;
    P.playhead = 4 * beat_frames() + 37;
    UNLOCK();
    input_event(SRC_KEY, binds[0].key, 110, 1, -1);
    frame();
    LOCK();
    P.playhead = 6 * beat_frames() + 11;
    UNLOCK();
    input_event(SRC_KEY, binds[0].key, 110, 1, -1);
    frame();
    LOCK();
    P.playing = 0;
    UNLOCK();
    pads_rec = 0;
    frame();
    {
        int tt = P.nclips == c0 + 2 ? P.clips[c0].track : -1, open = tt >= 0 && P.clips[c0 + 1].track == tt;
        for (int c = 0; c < c0 && open; c++)    /* nothing else on that track from the take on */
            open = !(P.clips[c].track == tt && P.clips[c].start + P.clips[c].length > 4 * beat_frames());
        check(open && P.clips[c0].start == 4 * beat_frames() && P.clips[c0 + 1].start == 6 * beat_frames(),
              "a take lands on an open track, snapped to the grid");
        check(!take.on && take.hits == 2 && strstr(status_msg, "Recorded 2 hits"), "stopping ends the take and says where it went");
    }
    check(nundo == u0 + 1, "a recorded take is one undo step");
    {
        int nt = P.ntracks;
        undo_redo(0);                           /* back to the demo as it was, for the tests after */
        check(P.nclips == c0 && P.ntracks <= nt, "undo takes the take (and any track it added) away");
    }
    {                                           /* Rec while stopped: starts playing, the timer runs */
        double keep_view = view_start;
        long keep_ph = P.playhead;
        LOCK();
        P.playing = 0;
        P.playhead = 0;
        UNLOCK();
        int keep = MET.countin;
        MET.countin = 0;
        rec_toggle();
        frame();
        int started = pads_rec && P.playing && take.on;
        SDL_Delay(120);
        frame();
        double secs = (SDL_GetTicks() - take.since) / 1000.0;
        rec_toggle();
        LOCK();
        P.playing = 0;
        UNLOCK();
        frame();
        MET.countin = keep;
        check(started && secs > 0.1 && !take.on && !pads_rec, "Rec starts playing, its timer runs, and Rec again stops the take");
        LOCK();
        P.playhead = keep_ph;                   /* as it was, for the tests after */
        UNLOCK();
        view_start = keep_view;
        history_check();
    }

    /* effects: each one measured on rendered audio, in a separate test project */
    {
        Project *T = malloc(sizeof *T);
        project_init(T);
        GenParams saw = {WAVE_SAW, 69, 2.0, 0.5, 2, 2, 440};          /* 440 Hz saw, 1 s at 120 BPM */
        int ss = sample_generate(T, &saw);
        long sl = T->samples[ss].frames, half = sl / 2;
        T->clips[T->nclips++] = (Clip){ss, 0, 0, 0, sl, 0, 0, 0, 0, 0};
        float *b0 = test_render(T, 0, sl, -1);
        double ref = test_rms(b0, 0, sl, 2);

        T->tracks[0].pan = -1;
        float *b1 = test_render(T, 0, sl, -1);
        check(test_rms(b1, 0, sl, 1) < 1e-4 && test_rms(b1, 0, sl, 0) > ref * 1.3, "pan hard left silences the right");
        T->tracks[0].pan = 0;

        T->tracks[0].filter = FILT_LOW;
        T->tracks[0].cutoff = 150;
        T->tracks[0].reso = 0;
        float *b2 = test_render(T, 0, sl, -1);
        check(test_rms(b2, half, sl, 2) < ref * 0.4, "a low-pass well below the note takes most of it away");
        T->tracks[0].filter = FILT_HIGH;
        T->tracks[0].cutoff = 4000;
        float *b3 = test_render(T, 0, sl, -1);
        check(test_rms(b3, half, sl, 2) < ref * 0.5 && test_rms(b3, half, sl, 2) > ref * 0.02,
              "a high-pass above the note leaves only its harmonics");
        T->tracks[0].filter = FILT_LOW;
        T->tracks[0].cutoff = 440;
        T->tracks[0].reso = 0;
        float *b4a = test_render(T, 0, sl, -1);
        T->tracks[0].reso = 0.95f;
        float *b4b = test_render(T, 0, sl, -1);
        check(test_rms(b4b, half, sl, 2) > test_rms(b4a, half, sl, 2) * 1.5, "resonance boosts the cutoff frequency");
        T->tracks[0].filter = FILT_OFF;

        T->tracks[0].eq[0] = 12;
        GenParams low = {WAVE_SINE, 33, 2.0, 0.3, 2, 2, 440};          /* 55 Hz */
        int ls = sample_generate(T, &low);
        T->clips[0].sample = ls;
        T->clips[0].length = T->samples[ls].frames;
        float *b5 = test_render(T, 0, sl, -1);
        T->tracks[0].eq[0] = 0;
        float *b5r = test_render(T, 0, sl, -1);
        check(test_rms(b5, half, sl, 2) > test_rms(b5r, half, sl, 2) * 2.5, "the low EQ band boosts a bass note");
        T->clips[0].sample = ss;
        T->clips[0].length = sl;

        T->clips[0].gain_db = -6.0206f;
        float *b6 = test_render(T, 0, sl, -1);
        check(fabs(test_rms(b6, half, sl, 2) / test_rms(b0, half, sl, 2) - 0.5) < 0.01, "clip gain -6 dB halves it");
        T->clips[0].gain_db = 0;
        T->clips[0].fade_in = sl / 4;
        T->clips[0].fade_out = sl / 4;
        float *b7 = test_render(T, 0, sl, -1);
        check(test_rms(b7, 0, sl / 40, 2) < ref * 0.06 && test_rms(b7, sl - sl / 40, sl, 2) < ref * 0.06 &&
                  test_rms(b7, sl / 3, 2 * sl / 3, 2) > ref * 0.9, "fades start and end at silence");
        T->clips[0].fade_in = T->clips[0].fade_out = 0;

        T->tracks[0].drive = 1;
        T->clips[0].gain_db = -20;
        float *b8 = test_render(T, 0, sl, -1);
        T->tracks[0].drive = 0;
        float *b8r = test_render(T, 0, sl, -1);
        check(test_rms(b8, half, sl, 2) > test_rms(b8r, half, sl, 2) * 2, "drive makes a quiet sound hotter");
        T->clips[0].gain_db = 0;

        long beat = (long)llround(60.0 / T->bpm * SR), end = sl + 3 * SR;
        T->clips[0].length = beat / 4;                                 /* a short blip, then silence */
        T->tracks[0].delay_send = 1;
        T->fx.delay_div = 5;                                           /* 1/4 = one beat */
        T->fx.delay_feedback = 0.5f;
        float *b9 = test_render(T, 0, end, -1);
        check(test_rms(b9, beat + beat / 16, beat + beat / 5, 2) > 0.02 && test_rms(b9, beat / 2, beat - 100, 2) < 1e-3,
              "the delay repeats the sound one beat later, silent in between");
        T->tracks[0].delay_send = 0;
        T->tracks[0].reverb_send = 1;
        float *b10 = test_render(T, 0, end, -1);
        check(test_rms(b10, beat / 2, beat, 2) > 1e-3, "the reverb rings on after the sound stops");
        check(project_has_tails(T), "a reverb send counts as a tail for export");
        T->tracks[0].reverb_send = 0;
        T->clips[0].length = sl;

        GenParams kick = {WAVE_KICK, 36, 0.5, 0.9, 0, 0, 440};
        int ks = sample_generate(T, &kick);
        T->clips[T->nclips++] = (Clip){ks, 1, 0, 0, T->samples[ks].frames, 0, 0, 0, 0, 0};
        T->fx.sc_source = 1;
        T->tracks[0].duck = 1;
        float *b11 = test_render(T, 0, sl, 0);                         /* track 0 alone: its stem */
        T->tracks[0].duck = 0;
        float *b11r = test_render(T, 0, sl, 0);
        check(test_rms(b11, 0, SR / 20, 2) < test_rms(b11r, 0, SR / 20, 2) * 0.4 &&
                  test_rms(b11, sl - SR / 10, sl, 2) > test_rms(b11r, sl - SR / 10, sl, 2) * 0.8,
              "the sidechain ducks the track under the kick, then lets it back up");

        int up = sample_variant(T, ss, 12, 0), rv = sample_variant(T, ss, 0, 1), kup = sample_variant(T, ks, 12, 0);
        int zc0 = 0, zc1 = 0;
        for (long i = 1; i < sl; i++) {
            zc0 += (T->samples[ss].data[2 * i - 2] < 0) != (T->samples[ss].data[2 * i] < 0);
            zc1 += (T->samples[up].data[2 * i - 2] < 0) != (T->samples[up].data[2 * i] < 0);
        }
        check(up >= 0 && T->samples[up].frames == sl && abs(zc1 - 2 * zc0) < zc0 / 20,
              "a synth note an octave up is made again: same length, twice the frequency");
        check(kup >= 0 && labs(T->samples[kup].frames - T->samples[ks].frames / 2) <= 2,
              "a recorded sound an octave up is resampled: half the length");
        check(rv >= 0 && T->samples[rv].data[0] == T->samples[ss].data[2 * (sl - 1)], "reverse plays it backwards");
        check(sample_variant(T, up, 12, 0) == up && sample_base(T, up) == ss, "variants are reused, not duplicated");

        T->tracks[0].filter = FILT_BAND;
        T->tracks[0].eq[2] = -3;
        T->fx.pingpong = 1;
        T->clips[0].gain_db = -2.5f;
        T->clips[0].fade_in = 100;
        T->clips[T->nclips++] = (Clip){rv, 2, 0, 0, T->samples[rv].frames, 0, 0, 0, 0, 0};
        char fpath[1200];
        snprintf(fpath, sizeof fpath, "%s.fx", tpath);
        project_save(T, fpath, msg, sizeof msg);
        Project *U = malloc(sizeof *U);
        int lr2 = project_load(U, fpath, msg, sizeof msg);
        check(lr2 == 0 && !memcmp(U->tracks, T->tracks, sizeof T->tracks) && !memcmp(&U->fx, &T->fx, sizeof T->fx) &&
                  !memcmp(U->clips, T->clips, (size_t)T->nclips * sizeof(Clip)),
              "track effects, master effects and clip gain/fades are saved exactly");
        check(lr2 == 0 && U->samples[rv].pitched && U->samples[rv].reversed && U->samples[rv].frames == sl,
              "a reversed clip's sound comes back reversed");
        if (lr2 == 0) project_free(U);
        free(U);
        remove(fpath);

        float *bufs[] = {b0, b1, b2, b3, b4a, b4b, b5, b5r, b6, b7, b8, b8r, b9, b10, b11, b11r};
        for (size_t i = 0; i < sizeof bufs / sizeof *bufs; i++) free(bufs[i]);
        project_free(T);
        free(T);
    }

    /* analysis: a 1 kHz sine at full scale peaks at 0 dB in the right bin */
    {
        static float sine[VIS_FFT], db[VIS_FFT / 2];
        double hz = 1000.0;                            /* need not fall exactly on a bin */
        for (int i = 0; i < VIS_FFT; i++) sine[i] = (float)sin(2 * VIS_PI * hz * i / SR);
        spectrum_db(sine, VIS_FFT, db);
        int best = 1;
        for (int k = 1; k < VIS_FFT / 2; k++)
            if (db[k] > db[best]) best = k;
        double peak_hz = best * (double)SR / VIS_FFT;
        check(fabs(peak_hz - 1000) < (double)SR / VIS_FFT && db[best] > -1.5 && db[best] < 0.5,
              "the analyzer shows a full-scale 1 kHz sine at 1 kHz, 0 dB");
        check(db[best * 3] < db[best] - 60, "and nothing at its third harmonic");

        Project *T = malloc(sizeof *T);
        project_init(T);
        GenParams g = {WAVE_SINE, 69, 1.0, 0.5, 2, 2, 440};
        int si = sample_generate(T, &g);
        T->clips[T->nclips++] = (Clip){si, 2, 0, 0, T->samples[si].frames, 0, 0, 0, 0, 0};
        Mixer *m = mixer_new();
        float buf[2 * 2048] = {0}, lv[MAX_TRACKS][2];
        mixer_run(m, T, 4096, 1, buf, 2048, -1, 0);
        mixer_levels(m, lv);
        check(fabsf(lv[2][0] - 0.5f * T->tracks[2].volume) < 0.02f && lv[0][0] == 0,
              "track meters read each track's level after its fader");
        mixer_levels(m, lv);
        check(lv[2][0] == 0, "and reset once read");
        mixer_free(m);
        project_free(T);
        free(T);
        Meter mt = {0};
        meter_feed(&mt, 1.3f, 0.016f);
        check(mt.clipped && mt.peak > 1.29f, "a meter lights its clip warning above 0 dB");
    }

    /* the pitch wheel and mod wheel arrive as continuous MIDI controls, not pad presses */
    midi_in(MIDI_BEND, 16383, 0, NULL);
    check(P.bend > 0.99f, "the pitch wheel bends live sounds");
    midi_in(MIDI_BEND, 8192, 0, NULL);
    midi_in(MIDI_CCV + 1, 127, 0, NULL);
    check(P.modwheel > 0.99f && P.bend == 0, "the mod wheel is read, and the wheel springs back to centre");
    midi_in(MIDI_CCV + 1, 0, 0, NULL);

    /* clip edits through the UI helpers */
    frame();
    int ci = P.nclips - 1;
    long len0 = P.clips[ci].length, fr0 = P.samples[P.clips[ci].sample].frames;
    u0 = nundo;
    clip_variant(ci, 12, 0);
    frame();
    long want = llround(len0 * (double)P.samples[P.clips[ci].sample].frames / fr0);
    check(clip_semis(&P.clips[ci]) == 12 && labs(P.clips[ci].length - want) <= 2 && nundo == u0 + 1,
          "transposing a clip keeps the same stretch of sound, as one undo step");
    undo_redo(0);
    check(clip_semis(&P.clips[ci]) == 0 && P.clips[ci].length == len0, "and undo brings the original back");

    /* multiselect: box, shift-click, group moves, delete, duplicate, copy and paste */
    {
        theory_open = pads_open = mixer_open = vis_open = 0;
        LOCK();
        P.nclips = 0;
        P.playing = 0;
        UNLOCK();
        sel_clear();
        view_start = 0;
        snap_i = 3;
        frame();
        long bt = beat_frames();
        clip_add(0, 0, 1 * bt, 0, bt);
        clip_add(0, 1, 2 * bt, 0, bt);
        clip_add(0, 2, 6 * bt, 0, bt);
        frame();
        int bx0 = (int)frame_to_x(0) + 3, bx1 = (int)frame_to_x(4 * bt);
        sim_drag(bx0, lane_y(0) - L.lane_h / 2 + 3, bx1, lane_y(1) + 10, 0);
        check(sel_count() == 2 && csel[0] && csel[1] && !csel[2], "dragging a box over empty space selects the clips in it");
        SDL_Rect r3 = clip_rect(&P.clips[2]);
        sim(r3.x + r3.w / 2, r3.y + r3.h / 2, 1, 0, 0, KMOD_SHIFT);
        sim(r3.x + r3.w / 2, r3.y + r3.h / 2, 0, 1, 0, KMOD_SHIFT);
        check(sel_count() == 3 && csel[2], "Shift+click adds a clip to the selection");
        sim(r3.x + r3.w / 2, r3.y + r3.h / 2, 1, 0, 0, KMOD_SHIFT);
        sim(r3.x + r3.w / 2, r3.y + r3.h / 2, 0, 1, 0, KMOD_SHIFT);
        check(sel_count() == 2 && !csel[2], "and Shift+click again takes it out");
        frame();
        int u1 = nundo;
        SDL_Rect r1 = clip_rect(&P.clips[0]);
        int bpx = (int)(60.0 / P.bpm * pps);
        sim_drag(r1.x + r1.w / 2, r1.y + r1.h / 2, r1.x + r1.w / 2 + 2 * bpx, r1.y + r1.h / 2 + L.lane_h, 0);
        check(P.clips[0].start == 3 * bt && P.clips[1].start == 4 * bt && P.clips[0].track == 1 && P.clips[1].track == 2 &&
                  P.clips[2].start == 6 * bt, "dragging one selected clip moves them all, the others stay");
        check(nundo == u1 + 1, "a group move is one undo step");
        SDL_Keysym kk = {0};
        kk.sym = SDLK_LEFT;
        for (int i = 0; i < 6; i++) key(kk);
        check(P.clips[0].start == 0 && P.clips[1].start == bt, "nudging stops when the first clip reaches the start");
        kk.sym = SDLK_d;
        kk.mod = KMOD_CTRL;
        key(kk);
        check(P.nclips == 5 && sel_count() == 2 && csel[3] && csel[4] && P.clips[3].start == 2 * bt,
              "Ctrl+D copies the selection to just after itself and selects the copies");
        kk.sym = SDLK_c;
        key(kk);
        LOCK();
        P.playhead = 10 * bt + 5;
        UNLOCK();
        kk.sym = SDLK_v;
        key(kk);
        check(P.nclips == 7 && P.clips[5].start == 10 * bt && P.clips[6].start == 11 * bt,
              "Ctrl+C then Ctrl+V pastes the selection at the playhead");
        kk.sym = SDLK_DELETE;
        kk.mod = 0;
        key(kk);
        check(P.nclips == 5 && sel_count() == 0, "Delete removes every selected clip");
        kk.sym = SDLK_a;
        kk.mod = KMOD_CTRL;
        key(kk);
        check(sel_count() == 5, "Ctrl+A selects everything");
        SDL_Rect rr = clip_rect(&P.clips[2]);
        sim(rr.x + rr.w / 2, rr.y + rr.h / 2, 1, 0, 0, 0);
        sim(rr.x + rr.w / 2, rr.y + rr.h / 2, 0, 1, 0, 0);
        check(sel_count() == 1 && selected == 2, "a plain click on one of them selects just that one");

        /* scrollbars: the Theory panel's Keys page is taller than the window */
        open_panel(RI_THEORY);
        theory_tab = 0;
        th_scroll = 0;
        frame();
        frame();
        SDL_Rect pane = {L.pane_x, L.top, L.pane_w, H - L.top - L.status};
        int sx = pane.x + pane.w - 7;
        check(th_content_h > pane.h, "the Theory panel's Keys page is taller than the window here");
        sim(sx, pane.y + pane.h - 20, 1, 0, 0, 0);
        sim(sx, pane.y + pane.h - 20, 0, 1, 0, 0);
        check(th_scroll > 0, "clicking the scrollbar below its handle scrolls down a page");
        th_scroll = 0;
        frame();
        sim(sx, pane.y + 20, 1, 0, 0, 0);
        for (int i = 1; i <= 6; i++) sim(sx, pane.y + 20 + i * 15, 0, 0, 0, 0);
        sim(sx, pane.y + 110, 0, 1, 0, 0);
        check(th_scroll > 0 && drag.kind == D_NONE, "dragging the handle scrolls");
        kk.sym = SDLK_PAGEDOWN;
        kk.mod = 0;
        th_scroll = 0;
        key(kk);
        frame();
        check(th_scroll > 0, "Page Down scrolls the open panel");

        /* the rail and panels */
        open_panel(RI_MIXER);
        frame();
        check(mixer_open && !theory_open && L.pane_w == PANE_W, "opening a panel closes the one that was open");
        int tl_with = L.tl_w;
        kk.sym = SDLK_b;
        kk.mod = KMOD_CTRL;
        key(kk);
        frame();
        check(!mixer_open && L.tl_w > tl_with, "Ctrl+B hides the panel and the timeline takes the room");
        key(kk);
        check(mixer_open, "and brings the same panel back");
        sim(RAIL_W / 2, L.top + 6 + RI_VISUAL * 62 + 20, 1, 0, 0, 0);
        sim(RAIL_W / 2, L.top + 6 + RI_VISUAL * 62 + 20, 0, 1, 0, 0);
        check(vis_open && !mixer_open, "clicking a rail button opens its panel");
        sim(RAIL_W / 2, L.top + 6 + RI_VISUAL * 62 + 20, 1, 0, 0, 0);
        sim(RAIL_W / 2, L.top + 6 + RI_VISUAL * 62 + 20, 0, 1, 0, 0);
        check(!vis_open, "and clicking it again hides it");
        Col bg_dark = BG;
        theme_apply(1);
        check(BG.r > 200 && TEXT.r < 60, "the light theme is light, with dark text");
        theme_apply(0);
        check(BG.r == bg_dark.r, "and the dark theme comes back exactly");
        int shaped_ok = 1;
        for (int k = THEME_XP; k <= THEME_BLENDER; k++) {     /* each shaped theme draws a whole frame */
            theme_apply(k);
            frame();
            shaped_ok &= ST.shaped && theme_kind == k && (theme_light == (k != THEME_BLENDER));
        }
        check(shaped_ok, "Windows XP, Frutiger Aero and Blender themes each draw a frame in their own style");
        theme_apply(0);
        check(!ST.shaped && BG.r == bg_dark.r, "and switching back leaves no trace of them");
        open_panel(RI_SOUNDS);
    }

    /* the time scale, the metronome, count-in and navigation keys */
    {
        char tb[24];
        fmt_clock(65.25, 1, tb, sizeof tb);
        check(!strcmp(tb, "1:05.25"), "the playhead clock reads 1:05.25");
        fmt_clock(90, 0, tb, sizeof tb);
        char tc[24];
        fmt_clock(2.5, 0, tc, sizeof tc);
        check(!strcmp(tb, "1:30") && !strcmp(tc, "0:02.5"), "the time scale labels read 1:30 and 0:02.5");

        typeof(MET) saved_met = MET;
        MET = (typeof(MET)){1, CLICK_CLICK, 1, 1, 0, 1, 1.0f};
        metronome_prepare();
        long bf2 = beat_frames();
        float *mb = calloc((size_t)bf2 * 4 * 2, sizeof(float));
        LOCK();
        click_pos = -1;
        metronome_mix(mb, 0, (int)(2 * bf2), 0);
        UNLOCK();
        double at0 = test_rms(mb, 0, SR / 50, 2), at1 = test_rms(mb, bf2, bf2 + SR / 50, 2);
        double between = test_rms(mb, bf2 / 2, bf2 / 2 + SR / 50, 2);
        check(at0 > 0.01 && at1 > 0.01 && between < 1e-6, "the metronome clicks on each beat and is silent between");
        check(at0 > at1 * 1.2, "the first beat of the bar is louder");
        memset(mb, 0, sizeof(float) * (size_t)bf2 * 8);
        MET.sub = 2;
        LOCK();
        click_pos = -1;
        metronome_mix(mb, 0, (int)bf2, 0);
        UNLOCK();
        check(test_rms(mb, bf2 / 2, bf2 / 2 + SR / 50, 2) > 0.005, "8ths add a tick halfway through the beat");
        free(mb);

        MET.sub = 1;
        MET.countin = 1;
        MET.countin_rec_only = 0;
        int was_paused = !dev || SDL_GetAudioDeviceStatus(dev) == SDL_AUDIO_PAUSED;
        if (dev) SDL_PauseAudioDevice(dev, 1);  /* only this test drives the audio, or it races */
        LOCK();
        P.playing = 0;
        P.loop = 0;
        P.playhead = 0;
        UNLOCK();
        toggle_play();
        check(P.playing && countin_left == 4 * bf2, "with a count-in, play starts with a bar of clicks");
        float *cb = calloc((size_t)bf2 * 2, sizeof(float));
        LOCK();
        audio_cb(NULL, (Uint8 *)cb, (int)(bf2 * 2 * sizeof(float)));
        long ph_during = P.playhead;
        for (int i = 0; i < 3; i++) audio_cb(NULL, (Uint8 *)cb, (int)(bf2 * 2 * sizeof(float)));
        audio_cb(NULL, (Uint8 *)cb, (int)(bf2 * 2 * sizeof(float)));
        long ph_after = P.playhead;
        UNLOCK();
        free(cb);
        if (dev && !was_paused) SDL_PauseAudioDevice(dev, 0);   /* as it was */
        check(ph_during == 0 && ph_after == bf2, "the song waits for the count-in, then plays from where it was");
        toggle_play();
        MET = saved_met;
        metronome_prepare();

        LOCK();
        P.nclips = 0;
        UNLOCK();
        sel_clear();
        clip_add(0, 0, 2 * bf2, 0, bf2);
        clip_add(0, 1, 9 * bf2, 0, 3 * bf2);
        playhead_to(0);
        SDL_Keysym nk = {0};
        nk.sym = SDLK_RIGHT;
        nk.mod = KMOD_CTRL;
        key(nk);
        check(P.playhead == 4 * bf2, "Ctrl+Right moves the playhead to the next bar");
        nk.mod = KMOD_CTRL | KMOD_SHIFT;
        key(nk);
        check(P.playhead == 5 * bf2, "Ctrl+Shift+Right moves it one beat");
        nk.sym = SDLK_LEFT;
        nk.mod = KMOD_CTRL;
        key(nk);
        key(nk);
        check(P.playhead == 0, "Ctrl+Left goes back to the bar, then the bar before");
        nk.mod = KMOD_ALT;
        nk.sym = SDLK_RIGHT;
        key(nk);
        long e1 = P.playhead;
        key(nk);
        check(e1 == 2 * bf2 && P.playhead == 3 * bf2, "Alt+Right jumps to each clip edge in turn");
        nk.mod = 0;
        nk.sym = SDLK_END;
        key(nk);
        check(P.playhead == 12 * bf2, "End goes to the end of the song");
        frame();                                     /* the layout as it is now */
        nk.sym = SDLK_z;
        key(nk);
        frame();
        check(frame_to_x(12 * bf2) <= L.tl_x + L.tl_w && view_start == 0, "Z zooms so the whole song fits");
        pps = 400;
        nk.sym = SDLK_f;
        key(nk);
        frame();
        double phx = frame_to_x(P.playhead);
        check(phx > L.tl_x + L.tl_w / 4 && phx < L.tl_x + 3 * L.tl_w / 4, "F brings the playhead into view");
        pps = 110;
        Uint32 tnow = SDL_GetTicks();
        taps[0] = tnow - 1500;
        taps[1] = tnow - 1000;
        taps[2] = tnow - 500;
        ntaps = 3;
        tap_tempo();
        check(fabs(P.bpm - 120) <= 2, "tapping every half second sets 120 BPM");
    }

    /* Theory: + puts sounds in the library */
    {
        open_panel(RI_THEORY);
        theory_tab = 2;                          /* Chords */
        th_scroll = 0;
        th_root = 0;
        th_len = 2;
        lib_badge = 0;
        frame();
        int ns0 = P.nsamples;
        SDL_Rect pane = {L.pane_x, L.top, L.pane_w, H - L.top - L.status};
        /* the first chord row's + is at the right edge, below the tabs, strip and headings */
        int found = -1;
        for (int yy = pane.y + 60; yy < pane.y + pane.h && found < 0; yy += 2) {
            sim(pane.x + pane.w - 14 - 13, yy, 1, 0, 0, 0);
            sim(pane.x + pane.w - 14 - 13, yy, 0, 1, 0, 0);
            if (P.nsamples > ns0) found = yy;
        }
        check(found > 0 && !strncmp(P.samples[P.nsamples - 1].name, "C ", 2) &&
                  P.samples[P.nsamples - 1].nchord == 3, "+ on the Major row adds a C major chord to the library");
        check(lib_badge == 1, "and the Sounds button counts it");
        int ns1 = P.nsamples;
        sim(pane.x + pane.w - 14 - 13, found, 1, 0, 0, 0);
        sim(pane.x + pane.w - 14 - 13, found, 0, 1, 0, 0);
        check(P.nsamples == ns1, "adding the same chord again doesn't make a copy");
        th_len = 0;
        int note = 69;
        int s1 = add_to_library(&note, 1, "A4");
        check(s1 >= 0 && labs(P.samples[s1].frames - beat_frames()) <= 2, "the length setting sets how long added sounds are");
        int ns2 = P.nsamples;
        gn_split = 1;
        add_beat_to_library(0);
        check(P.nsamples == ns2 + 4 && P.samples[ns2].is_beat, "+ Library on a beat adds one loop per drum");
        gn_split = 0;
        open_panel(RI_SOUNDS);
        frame();
        check(lib_badge == 0, "opening Sounds clears the count");
    }

    /* typing the tempo: a click on BPM opens it for typing */
    {
        frame();
        double bpm0 = P.bpm;
        int bx = bpm_rect.x + bpm_rect.w / 2;
        sim(bx, 24, 1, 0, 0, 0);
        sim(bx, 24, 0, 1, 0, 0);
        check(numedit.active && numedit.v == &P.bpm && P.bpm == bpm0, "clicking BPM opens it for typing");
        numedit_text("98.5");
        SDL_Keysym ek = {0};
        ek.sym = SDLK_RETURN;
        numedit_key(ek);
        frame();
        check(!numedit.active && P.bpm == 98.5, "typing 98.5 and Enter sets the tempo");
        sim(bx, 24, 1, 0, 0, 0);
        sim(bx, 24, 0, 1, 0, 0);
        numedit_text("500");
        numedit_key(ek);
        frame();
        check(P.bpm == 300, "a tempo above 300 is held at 300");
        sim(bx, 24, 1, 0, 0, 0);
        sim(bx, 24, 0, 1, 0, 0);
        numedit_text("60");
        ek.sym = SDLK_ESCAPE;
        numedit_key(ek);
        frame();
        check(P.bpm == 300, "Esc leaves it as it was");
        sim(bx, 24, 1, 0, 0, 0);
        sim(bx, 24, 0, 1, 0, 0);
        numedit_text("110");
        sim(W / 2, H / 2, 1, 0, 0, 0);
        sim(W / 2, H / 2, 0, 1, 0, 0);
        check(P.bpm == 110 && !numedit.active, "clicking elsewhere sets what was typed");
        sim(bx, 24, 1, 0, 0, 0);
        for (int i = 1; i <= 5; i++) sim(bx + i * 6, 24, 0, 0, 0, 0);
        sim(bx + 30, 24, 0, 1, 0, 0);
        check(!numedit.active && P.bpm != 110, "dragging still changes it without typing");
        P.bpm = bpm0;
    }

    /* drop-down lists */
    {
        open_panel(RI_THEORY);
        theory_tab = 6;                          /* Beats */
        th_scroll = 0;
        gn_sel = 0;
        frame();
        frame();
        SDL_Rect pane = {L.pane_x, L.top, L.pane_w, H - L.top - L.status};
        check(th_content_h <= pane.h, "the Beats page fits without scrolling");
        int fx0 = L.pane_x + 14, fy = L.top + 10 + 40 + 15;          /* the Style field, under the tabs */
        sim(fx0 + 60, fy, 1, 0, 0, 0);
        sim(fx0 + 60, fy, 0, 1, 0, 0);
        check(dd.open && dd.sel == &gn_sel && dd.n == NGENRES && dd.cols == 2, "the style list opens in two columns");
        SDL_Rect it = dd_item(20);
        sim(it.x + 10, it.y + 10, 1, 0, 0, 0);
        sim(it.x + 10, it.y + 10, 0, 1, 0, 0);
        check(!dd.open && gn_sel == 20, "picking a style from the list selects it and closes the list");
        sim(fx0 + 60, fy, 1, 0, 0, 0);
        sim(fx0 + 60, fy, 0, 1, 0, 0);
        sim(W - 20, H - 60, 1, 0, 0, 0);
        sim(W - 20, H - 60, 0, 1, 0, 0);
        check(!dd.open && gn_sel == 20, "clicking elsewhere closes it without changing anything");
        gn_sel = 0;

        snap_i = 4;
        frame();
        SDL_Rect sn = {0, 0, 0, 0};
        for (int xx = 300; xx < 760 && !sn.w; xx += 2)      /* find the Snap field in the top bar */
            if (dropdown_probe_snap(xx)) sn = (SDL_Rect){xx, 9, 1, 30};
        sim(sn.x + 10, 24, 1, 0, 0, 0);
        sim(sn.x + 10, 24, 0, 1, 0, 0);
        check(snap_i == 5, "clicking Snap still steps to the next value");
        sim(snap_arrow_x(), 24, 1, 0, 0, 0);
        sim(snap_arrow_x(), 24, 0, 1, 0, 0);
        check(dd.open && dd.sel == &snap_i, "its arrow opens the list");
        it = dd_item(3);
        sim(it.x + 10, it.y + 10, 1, 0, 0, 0);
        sim(it.x + 10, it.y + 10, 0, 1, 0, 0);
        check(snap_i == 3, "and a value can be picked directly");
        snap_i = 3;
        open_panel(RI_SOUNDS);
    }

    /* the example songs open, sound right, and open as untitled copies */
    {
        examples_scan();
        check(nexamples == 3, "the three example songs are found next to the program");
        int good = 0;
        for (int i = 0; i < nexamples; i++) {
            Project *q = malloc(sizeof *q);
            int old_rate = g_rate;
            if (project_load(q, example_path[i], msg, sizeof msg) == 0) {
                long end = project_end(q), n = 2 * SR, at = end / 2;
                float *b = calloc((size_t)n * 2, sizeof(float));
                Mixer *m = mixer_new();
                for (long pos = 0; pos < n; pos += 1024)
                    mixer_run(m, q, at + pos, 1, b + 2 * pos, (int)(n - pos < 1024 ? n - pos : 1024), -1, 0);
                double r = test_rms(b, 0, n, 2);
                if (q->nclips > 100 && end > 50L * SR && r > 0.02 && strstr(msg, "missing") == NULL) good++;
                else fprintf(stderr, "example %s: %d clips, %.1f s, rms %.3f: %s\n", example_title[i], q->nclips,
                             (double)end / SR, r, msg);
                mixer_free(m);
                free(b);
                project_free(q);
            }
            free(q);
            g_rate = old_rate;
        }
        check(good == nexamples && good > 0, "each example opens, runs about a minute and is playing sound mid-song");
        if (nexamples) {
            snprintf(act_path, sizeof act_path, "%s", example_path[0]);
            run_action(ACT_OPEN_EXAMPLE);
            check(proj_path[0] == 0 && !is_dirty() && P.nclips > 100, "an example opens as an untitled copy, nothing to save yet");
        }
    }

    /* tuning in the top bar, beside BPM: both open for typing and both still drag */
    {
        frame();
        double bpm0 = P.bpm, a40 = P.a4;
        int ax = a4_rect.x + a4_rect.w / 2, bx = bpm_rect.x + bpm_rect.w / 2;
        sim(ax, 24, 1, 0, 0, 0);
        sim(ax, 24, 0, 1, 0, 0);
        check(numedit.active && numedit.v == &P.a4, "clicking A4 opens the tuning for typing");
        numedit_text("432");
        SDL_Keysym ek = {0};
        ek.sym = SDLK_RETURN;
        numedit_key(ek);
        frame();
        check(P.a4 == 432 && P.bpm == bpm0, "typing 432 sets the tuning and leaves the tempo alone");
        sim(bx, 24, 1, 0, 0, 0);
        for (int i = 1; i <= 5; i++) sim(bx + i * 8, 24, 0, 0, 0, 0);
        sim(bx + 40, 24, 0, 1, 0, 0);
        check(P.bpm > bpm0 && P.a4 == 432 && !numedit.active, "dragging BPM still changes the tempo, not the tuning");
        P.bpm = bpm0;
        P.a4 = a40;
        frame();
    }

    /* pad presets fill all 16 pads, and undo puts the old ones back */
    {
        frame();
        Pad before[NPADS];
        memcpy(before, P.pads, sizeof before);
        int u0 = nundo;
        gn_sel = 0;
        load_pad_preset(0);
        frame();
        int filled = 0, kicks = 0;
        for (int k = 0; k < NPADS; k++) {
            filled += P.pads[k].sample >= 0;
            kicks += P.pads[k].sample >= 0 && P.samples[P.pads[k].sample].is_beat == 2 && P.samples[P.pads[k].sample].beat_mask == 0;
        }
        check(filled == 16 && kicks >= 4 && P.pads[7].volume < 0.5f, "Drum kit fills 16 pads: kicks, toms from the kick, a ghost snare");
        check(nundo == u0 + 1, "loading a preset is one undo step");
        undo_redo(0);
        check(!memcmp(P.pads, before, sizeof before), "and undo puts the old pads back");
        th_root = 9;
        th_scale = 1;
        load_pad_preset(2);
        frame();
        check(P.pads[0].mode == PAD_HOLD && P.samples[P.pads[0].sample].nchord == 3 &&
                  !strncmp(P.samples[P.pads[0].sample].name, "Am", 2), "Chords of the key in A minor starts on Am, held");
        load_pad_preset(3);
        check(P.samples[P.pads[0].sample].chord[0] == 69 && P.samples[P.pads[7].sample].chord[0] == 81,
              "Scale notes climb the scale: A4 on pad 1, A5 on pad 8");
        load_pad_preset(5);
        check(P.pads[0].mode == PAD_LOOP && P.samples[P.pads[0].sample].is_beat == 1, "Beat loops start and stop on a press");
        load_pad_preset(0);
        int ns = P.nsamples;
        load_pad_preset(0);
        check(P.nsamples == ns, "loading a preset again reuses its sounds");
        frame();
    }

    /* removing a sound takes its clips too, and undo brings both back */
    {
        frame();
        GenParams g = {WAVE_SINE, 72, 1.0, 0.5, 5, 50, 440};
        int si = sample_generate(&P, &g);
        clip_add(si, 0, 0, 0, P.samples[si].frames);
        clip_add(si, 1, beat_frames(), 0, P.samples[si].frames);
        frame();
        int n0 = P.nclips, u0 = nundo;
        remove_sound(si);
        frame();
        check(P.samples[si].removed && P.nclips == n0 - 2 && nundo == u0 + 1, "removing a sound removes its 2 clips, as one step");
        show_panel(RI_SOUNDS);
        sounds_sub = 0;
        lib_scroll = 1000;                        /* the newest sounds are at the end */
        memset(lib_rects, 0, sizeof lib_rects);
        frame();
        int shown_other = 0;
        for (int k = 0; k < P.nsamples; k++) shown_other |= k != si && lib_rects[k].w > 0;
        check(lib_rects[si].w == 0 && shown_other, "a removed sound leaves the library list");
        undo_redo(0);
        check(!P.samples[si].removed && P.nclips == n0, "undo brings the sound and its clips back");
        undo_redo(1);
        check(P.samples[si].removed && P.nclips == n0 - 2, "and redo removes them again");
        undo_redo(0);
        frame();
    }

    /* New sound: Play hears it without adding it to the library */
    {
        show_panel(RI_SOUNDS);
        sounds_sub = 3;
        frame();
        int ns = P.nsamples;
        SDL_Rect pane = {L.pane_x, L.top, L.pane_w, H - L.top - L.status};
        int found = 0;
        for (int yy = pane.y + 60; yy < pane.y + pane.h && !found; yy += 4) {   /* find the Play button */
            sim(pane.x + 12 + 40, yy, 1, 0, 0, 0);
            sim(pane.x + 12 + 40, yy, 0, 1, 0, 0);
            if (scratch && scratch == heard_buf) found = yy;
        }
        check(found && P.nsamples == ns, "Play in New sound plays it without adding it to the library");
        sim(pane.x + 12 + 40, found, 1, 0, 0, 0);
        sim(pane.x + 12 + 40, found, 0, 1, 0, 0);
        check(!scratch, "and pressing it again stops it");
        sounds_sub = 0;
    }

    /* the transport at the bottom */
    {
        frame();
        LOCK();
        P.playing = 0;
        P.loop = 0;
        P.playhead = 0;
        UNLOCK();
        SDL_Rect tr = transport_rect;
        int bw = 33, cy = tr.y + tr.h / 2;
        long bar = 4 * beat_frames();
        sim(tr.x + 4 * bw + 15, cy, 1, 0, 0, 0);             /* forward a bar */
        sim(tr.x + 4 * bw + 15, cy, 0, 1, 0, 0);
        check(P.playhead == bar, "the forward button moves the playhead a bar on");
        sim(tr.x + 2 * bw + 15, cy, 1, 0, 0, 0);             /* play */
        sim(tr.x + 2 * bw + 15, cy, 0, 1, 0, 0);
        int playing = P.playing;
        LOCK();
        P.playhead = 3 * bar;                                  /* as if it had played on */
        UNLOCK();
        sim(tr.x + 3 * bw + 15, cy, 1, 0, 0, 0);             /* stop */
        sim(tr.x + 3 * bw + 15, cy, 0, 1, 0, 0);
        check(playing && !P.playing && P.playhead == bar, "Play plays; Stop stops and goes back to where it began");
        sim(tr.x + 3 * bw + 15, cy, 1, 0, 0, 0);
        sim(tr.x + 3 * bw + 15, cy, 0, 1, 0, 0);
        check(P.playhead == 0, "Stop again goes to the start");
        sim(tr.x + 5 * bw + 15, cy, 1, 0, 0, 0);             /* to the end */
        sim(tr.x + 5 * bw + 15, cy, 0, 1, 0, 0);
        check(P.playhead == project_end(&P), "the end button goes to the end of the song");
        sim(tr.x + 6 * bw + 15, cy, 1, 0, 0, 0);             /* loop */
        sim(tr.x + 6 * bw + 15, cy, 0, 1, 0, 0);
        check(P.loop, "the loop button turns looping on");
        P.loop = 0;
        sim(tr.x + 7 * bw + 15, cy, 1, 0, 0, 0);             /* record */
        sim(tr.x + 7 * bw + 15, cy, 0, 1, 0, 0);
        check(pads_rec && P.playing, "the record button arms recording and starts playing");
        pads_rec = 0;
        LOCK();
        P.playing = 0;
        UNLOCK();
        countin_left = 0;
        frame();
        sim(tr.x + 15, cy, 1, 0, 0, 0);                       /* to the start */
        sim(tr.x + 15, cy, 0, 1, 0, 0);
        check(P.playhead == 0, "the start button goes to the beginning");
    }

    /* note clips and the piano roll */
    {
        frame();
        int t = -1;
        for (int k = 0; k < P.ntracks && t < 0; k++) {
            int used = 0;
            for (int i = 0; i < P.nclips; i++) used |= P.clips[i].track == k;
            if (!used) t = k;
        }
        if (t < 0 && P.ntracks < MAX_TRACKS) {
            LOCK();
            t = project_add_track(&P, NULL);
            UNLOCK();
        }
        if (t < 0) t = P.ntracks - 1;
        P.tracks[t].mute = P.tracks[t].solo = 0;
        P.tracks[t].volume = 1;
        int inst0 = P.inst_sample;
        P.inst_sample = -1;
        long bf = beat_frames(), bar = 4 * bf, at = (project_end(&P) / bar + 2) * bar;
        int c = new_note_clip(t, at + bf);
        P.inst_sample = inst0;
        check(c >= 0 && P.clips[c].pattern > 0 && P.clips[c].start == at && P.clips[c].length == bar && roll_clip == c,
              "a new note clip is one bar, on the bar, and opens in the piano roll");
        int pat = P.clips[c].pattern, root = P.clips[c].root;
        note_add(pat, root, 0, bf, 0.8f);
        int hi = note_add(pat, root + 12, 2 * bf, bf, 0.8f);
        float *b = test_render(&P, at, bar, t);
        long m = SR / 100;
        double r1 = test_rms(b, m, bf - m, 2), gap = test_rms(b, bf + SR / 20, 2 * bf - m, 2), r2 = test_rms(b, 2 * bf + m, 3 * bf - m, 2);
        int z1 = 0, z2 = 0;
        for (long i = m; i < bf - m; i++) z1 += (b[2 * i] < 0) != (b[2 * i + 2] < 0);
        for (long i = 2 * bf + m; i < 3 * bf - m; i++) z2 += (b[2 * i] < 0) != (b[2 * i + 2] < 0);
        free(b);
        check(r1 > 0.01 && r2 > 0.01 && gap < r1 * 0.02, "notes sound where they are and nowhere else");
        check(z1 > 20 && z2 > 1.85 * z1 && z2 < 2.15 * z1, "a note an octave up plays at twice the pitch");
        P.notes[hi].vel = 0.4f;
        b = test_render(&P, at, bar, t);
        double r2b = test_rms(b, 2 * bf + m, 3 * bf - m, 2);
        free(b);
        check(fabs(r2b / r2 - 0.5) < 0.03, "velocity scales a note's loudness");

        /* editing with the mouse */
        frame();
        int nn = P.nnotes;
        int ny = roll_grid.y + (roll_top - (root + 7)) * roll_kh + roll_kh / 2;
        int nx = (int)frame_to_x(at + 3 * bf + bf / 4);
        sim(nx, ny, 1, 0, 0, 0);
        sim(nx, ny, 0, 1, 0, 0);
        check(P.nnotes == nn + 1 && P.notes[nn].note == root + 7 && P.notes[nn].pattern == pat &&
                  P.notes[nn].start >= 3 * bf && P.notes[nn].start < 4 * bf, "clicking the grid adds a note there");
        SDL_Rect nr = note_rect(nn);
        sim_drag(nr.x + 3, nr.y + nr.h / 2, nr.x + 3, nr.y + nr.h / 2 - 2 * roll_kh, 0);
        check(P.notes[nn].note == root + 9, "dragging a note up two rows raises it two semitones");
        nr = note_rect(nn);
        long len0 = P.notes[nn].length;
        int beat_px = (int)(bf / (double)SR * pps);
        sim_drag(nr.x + nr.w - 2, nr.y + nr.h / 2, nr.x + nr.w - 2 + beat_px, nr.y + nr.h / 2, 0);
        check(P.notes[nn].length >= len0 + bf - snap_frames(), "dragging a note's end stretches it");
        check(P.clips[c].length == 2 * bar, "the clip grows by a bar to take in a note past its end");
        nr = note_rect(nn);
        sim(nr.x + 3, nr.y + 3, 0, 0, 1, 0);
        check(P.nnotes == nn, "right-clicking a note deletes it");
        undo_redo(0);
        check(P.nnotes == nn + 1 && P.notes[nn].note == root + 9, "and undo brings it back");
        SDL_Keysym kk = {0};
        kk.sym = SDLK_a;
        kk.mod = KMOD_CTRL;
        key(kk);
        check(nsel_count() == 3, "Ctrl+A selects every note in the clip");
        kk.sym = SDLK_UP;
        kk.mod = 0;
        key(kk);
        check(P.notes[nn].note == root + 10 && P.notes[nn - 2].note == root + 1, "Up moves the selected notes a semitone");
        kk.sym = SDLK_DELETE;
        key(kk);
        check(nsel_count() == 0 && pattern_end(&P, pat) == 0, "Delete removes the selected notes");
        frame();
        undo_redo(0);
        check(pattern_end(&P, pat) > 0, "undo puts them back");
        kk.sym = SDLK_ESCAPE;
        key(kk);
        key(kk);
        check(roll_clip < 0, "Esc clears the selection, then closes the roll");

        /* double-clicks on the timeline */
        view_start = (double)at / SR - 0.5;
        frame();
        SDL_Rect cr = clip_rect(&P.clips[c]);
        ldouble = 1;
        sim(cr.x + cr.w / 2, cr.y + cr.h / 2, 1, 0, 0, 0);
        sim(cr.x + cr.w / 2, cr.y + cr.h / 2, 0, 1, 0, 0);
        ldouble = 0;
        check(roll_clip == c, "double-clicking a note clip opens it in the piano roll");
        roll_close();
        view_start = (double)(at + 3 * bar) / SR - 0.5;
        frame();
        int n0 = P.nclips, ex = (int)frame_to_x(at + 3 * bar + bf);
        ldouble = 1;
        sim(ex, lane_y(t), 1, 0, 0, 0);
        sim(ex, lane_y(t), 0, 1, 0, 0);
        ldouble = 0;
        check(P.nclips == n0 + 1 && P.clips[n0].pattern > 0 && P.clips[n0].pattern != pat && P.clips[n0].start == at + 3 * bar &&
                  roll_clip == n0, "double-clicking an empty spot makes a new note clip there");
        roll_close();

        /* copies share their notes until made unique */
        sel_only(c);
        duplicate_selection(4 * bar);
        int cp = P.nclips - 1;
        check(P.clips[cp].pattern == pat, "a copy of a note clip shares its notes");
        roll_open(cp);
        roll_make_unique();
        int own = 0, orig = 0;
        for (int i = 0; i < P.nnotes; i++) {
            own += P.notes[i].pattern == P.clips[cp].pattern;
            orig += P.notes[i].pattern == pat;
        }
        check(P.clips[cp].pattern != pat && own == orig && own == 3, "make unique gives the copy its own notes");
        roll_close();
        frame();

        /* automation: what it does to the sound */
        b = test_render(&P, at, bf, t);
        double base = test_rms(b, m, bf - m, 2);
        free(b);
        auto_add(t, AUTO_VOLUME, at, 0);
        auto_add(t, AUTO_VOLUME, at + bf, 1 / 1.5f);
        b = test_render(&P, at, bf, t);
        double q1 = test_rms(b, m, bf / 4, 2), q4 = test_rms(b, 3 * bf / 4, bf - m, 2);
        free(b);
        check(q1 < q4 * 0.4 && q4 < base * 1.05, "a volume ramp fades the track in");
        auto_clear(t, AUTO_VOLUME);
        auto_add(t, AUTO_PAN, at, 0);
        b = test_render(&P, at, bf, t);
        double left = test_rms(b, m, bf - m, 0), right = test_rms(b, m, bf - m, 1);
        free(b);
        check(left > 0.01 && right < left * 0.01, "pan automation at the bottom puts it hard left");
        auto_clear(t, AUTO_PAN);
        frame();

        /* automation lanes with the mouse */
        auto_open[t] = 1;
        auto_param[t] = AUTO_CUTOFF;
        view_start = (double)at / SR - 0.5;
        frame();
        SDL_Rect ar = auto_rect(t);
        check(ar.h >= 58 && ar.y == track_y(t) + L.lane_h && auto_lane_at(ar.y + 5) == t &&
                  (t + 1 >= P.ntracks || track_y(t + 1) == ar.y + ar.h), "an open lane sits under its track and moves the rest down");
        int ax = (int)frame_to_x(at + bf);
        sim(ax, ar.y + 6, 1, 0, 0, 0);
        sim(ax, ar.y + 6, 0, 1, 0, 0);
        int first, np = auto_lane(&P, t, AUTO_CUTOFF, &first);
        check(np == 1 && P.autos[first].v > 0.97f && auto_to_value(AUTO_CUTOFF, P.autos[first].v) > 15000,
              "clicking the lane adds a point; at the top the cutoff is wide open");
        sim_drag(ax, ar.y + 6, ax + beat_px, ar.y + ar.h - 6, 0);
        np = auto_lane(&P, t, AUTO_CUTOFF, &first);
        check(np == 1 && P.autos[first].v < 0.03f && P.autos[first].at > at + bf, "dragging a point moves it in time and value");
        Track eff;
        auto_apply(&P, t, at, &eff);
        check(eff.filter == FILT_LOW && eff.cutoff < 25, "cutoff automation turns the filter on for that track");
        int px = (int)frame_to_x(P.autos[first].at), py = auto_v_to_y(ar, P.autos[first].v);
        sim(px, py, 0, 0, 1, 0);
        check(auto_lane(&P, t, AUTO_CUTOFF, NULL) == 0, "right-clicking a point removes it");
        undo_redo(0);
        check(auto_lane(&P, t, AUTO_CUTOFF, NULL) == 1, "undo brings the point back");

        /* it all saves and opens again */
        char tp[1100], msg2[256];
        const char *td = getenv("TMPDIR") ? getenv("TMPDIR") : getenv("TEMP") ? getenv("TEMP") : "/tmp";
        snprintf(tp, sizeof tp, "%s/tinydaw-roll-%u.tdaw", td, (unsigned)SDL_GetTicks() ^ (unsigned)time(NULL));
        int ok = project_save(&P, tp, msg2, sizeof msg2) == 0;
        Project *q = malloc(sizeof *q);
        ok = ok && project_load(q, tp, msg2, sizeof msg2) == 0;
        if (ok) {
            ok = q->nclips == P.nclips && !memcmp(q->clips, P.clips, (size_t)P.nclips * sizeof(Clip)) &&
                 q->nnotes == P.nnotes && q->nautos == P.nautos;
            for (int i = 0; ok && i < P.nnotes; i++)
                ok = q->notes[i].pattern == P.notes[i].pattern && q->notes[i].note == P.notes[i].note &&
                     q->notes[i].start == P.notes[i].start && q->notes[i].length == P.notes[i].length &&
                     fabsf(q->notes[i].vel - P.notes[i].vel) < 1e-5f;
            for (int i = 0; ok && i < P.nautos; i++)
                ok = q->autos[i].track == P.autos[i].track && q->autos[i].param == P.autos[i].param &&
                     q->autos[i].at == P.autos[i].at && fabsf(q->autos[i].v - P.autos[i].v) < 1e-5f;
            project_free(q);
        }
        free(q);
        remove(tp);
        check(ok, "note clips, notes and automation save and open again exactly");
        auto_open[t] = 0;
        frame();
    }

    /* the full-screen show: every scene draws, arrows step through them, cycling moves on */
    {
        show_open = 1;
        show_scene = 0;
        show_cycle = 0;
        SDL_Keysym sk = {0};
        int drew = 0;
        for (int i = 0; i <= SCENE_COUNT; i++) {
            frame();
            frame();
            drew += show_scene == 0 ? show_tex != NULL : scene_tex != NULL && scene_tw > 0;
            sk.sym = SDLK_RIGHT;
            key(sk);
        }
        check(drew == SCENE_COUNT + 1 && show_scene == 0, "the show steps through all 17 scenes and wraps back to the blobs");
        sk.sym = SDLK_LEFT;
        key(sk);
        check(show_scene == SCENE_COUNT, "Left goes back, round to the last scene");
        sk.sym = SDLK_TAB;
        key(sk);
        check(show_cycle == 1, "Tab turns cycling on");
        show_go(3);
        show_switch_at = show_wall - 1;          /* as if its time were up */
        frame();
        check(show_scene == 4 && show_fade_from == 3 && show_fade_left > 0, "cycling moves on by itself, crossfading");
        show_cycle = 0;
        show_scene = 0;
        show_open = 0;
        float probe[64 * 36 * 3];
        int lit = 0;
        for (int i = 0; i < SCENE_COUNT; i++) {
            char cap[64];
            double sum = 0;
            for (int f = 0; f < 8; f++) scene_render(i, 12 + f * 0.05, 0.05, 1, probe, 64, 36, cap, sizeof cap);
            for (int k = 0; k < 64 * 36 * 3; k++) sum += probe[k] == probe[k] ? probe[k] : 1e9;
            lit += sum / (64 * 36 * 3) > 0.002 && sum / (64 * 36 * 3) < 1.0;
        }
        check(lit == SCENE_COUNT, "every geometry and shader scene draws something, with no bad values");
    }


    /* ---- Sounds > Generator: the synth */
    {
        int bad = 0, quiet = 0, dc = 0;
        for (int i = 0; i < NSYNTH_PRESETS; i++) {
            SynthSound y;
            synth_from_preset(&y, i);
            long n;
            float *d = render_synth(&y, &n);
            if (!d) {
                bad++;
                continue;
            }
            double pk = 0, sum = 0, mean = 0;
            for (long k = 0; k < 2 * n; k++) {
                if (!isfinite(d[k])) bad++;
                pk = fmax(pk, fabs(d[k]));
                sum += d[k] * d[k];
                mean += d[k];
            }
            quiet += sqrt(sum / (2 * n)) < 0.02 || fabs(pk - 0.64) > 0.02;
            dc += fabs(mean / (2 * n)) > 0.03;
            free(d);
        }
        check(!bad && !quiet && !dc, "every Generator preset renders clean, audible, level-matched audio with no DC");

        int bass = 0;
        for (int i = 0; i < NSYNTH_PRESETS; i++)
            if (!strcmp(SYNTH_PRESETS[i].name, "Deep bass")) bass = i;
        double edge[2];
        for (int b = 0; b < 2; b++) {           /* brightness: more high end */
            SynthSound y;
            synth_from_preset(&y, bass);
            y.macro[MAC_BRIGHT] = b;
            long n;
            float *d = render_synth(&y, &n);
            double kk = exp(-2 * 3.14159265358979 * 1000.0 / SR), lp = 0, e = 0, a = 0;   /* the share above 1 kHz */
            for (long k = 0; k < n; k++) {
                double x = d[2 * k];
                lp = x + (lp - x) * kk;
                e += (x - lp) * (x - lp);
                a += x * x;
            }
            edge[b] = e / a;
            free(d);
        }
        check(edge[1] > edge[0] * 2, "Brightness opens up the high end");

        int sub = 0;
        for (int i = 0; i < NSYNTH_PRESETS; i++)
            if (!strcmp(SYNTH_PRESETS[i].name, "Rolling sub")) sub = i;
        int zc[2];
        for (int o = 0; o < 2; o++) {           /* the note: an octave up crosses zero twice as often */
            SynthSound y;
            synth_from_preset(&y, sub);
            y.note += 12 * o;
            long n;
            float *d = render_synth(&y, &n);
            zc[o] = 0;
            for (long k = 1; k < n; k++) zc[o] += (d[2 * k] >= 0) != (d[2 * k - 2] >= 0);
            free(d);
        }
        check(abs(zc[1] - 2 * zc[0]) < zc[0] / 20, "the Note control sets the pitch");

        Project *T = malloc(sizeof *T);
        project_init(T);
        SynthSound y;
        synth_from_preset(&y, bass);
        y.macro[MAC_GRIT] = 0.8;
        y.note = 40;
        int si = sample_generate_synth(T, &y, "My bass");
        check(si >= 0 && T->samples[si].synth && sample_root(&T->samples[si]) == 40,
              "a Generator sound plays unpitched at its note in note clips");
        int vi = sample_variant(T, si, 12, 0);
        y.note = 52;
        long vn;
        float *want = render_synth(&y, &vn);
        int same = vi >= 0 && want && T->samples[vi].frames == vn;
        for (long k = 0; same && k < 2 * vn; k++) same = T->samples[vi].data[k] == want[k];
        free(want);
        check(same, "a Generator sound is pitched by making it again at the new note");
        char msg[1200];
        int saved = project_save(T, tpath, msg, sizeof msg) == 0;
        Project *U = malloc(sizeof *U);
        int loaded = saved && project_load(U, tpath, msg, sizeof msg) == 0;
        check(loaded && U->samples[si].synth && !strcmp(U->samples[si].name, "My bass") &&
                  !memcmp(&U->samples[si].syn.patch, &T->samples[si].syn.patch, sizeof(SynthPatch)) &&
                  U->samples[si].syn.macro[MAC_GRIT] == 0.8 && U->samples[si].syn.note == 40 &&
                  U->samples[si].frames == T->samples[si].frames &&
                  !memcmp(U->samples[si].data, T->samples[si].data, (size_t)T->samples[si].frames * 2 * sizeof(float)),
              "a Generator sound saves as its recipe and comes back exactly");
        long before = T->samples[si].frames;
        int rate_ok = project_set_rate(T, 96000, msg, sizeof msg) == 0 && T->samples[si].frames == before * 2;
        project_set_rate(T, 48000, msg, sizeof msg);
        check(rate_ok, "a Generator sound is made again at a new project rate");
        if (loaded) project_free(U);
        free(U);
        project_free(T);
        free(T);
        remove(tpath);
    }
    {
        show_panel(RI_SOUNDS);
        sounds_sub = 1;
        sounds_scroll = 0;
        gx_genre = gx_kind = gx_preset = -1;
        frame();
        check(L.pane_w == PANE_W, "the Generator gets the wide panel");
        int dub = -1, bassk = -1, wob = -1, kick = -1;
        for (int i = 0; i < NSYNTH_GENRES; i++)
            if (!strcmp(SYNTH_GENRES[i], "Dubstep")) dub = i;
        for (int i = 0; i < NSYNTH_KINDS; i++)
            if (!strcmp(SYNTH_KINDS[i], "Bass")) bassk = i;
        for (int i = 0; i < NSYNTH_PRESETS; i++) {
            if (!strcmp(SYNTH_PRESETS[i].name, "Wobble bass")) wob = i;
            if (!strcmp(SYNTH_PRESETS[i].name, "Floor kick")) kick = i;
        }
        SDL_Rect r = gx_genre_r[dub + 1];
        sim(r.x + 5, r.y + 5, 1, 1, 0, 0);
        r = gx_kind_r[bassk + 1];
        sim(r.x + 5, r.y + 5, 1, 1, 0, 0);
        frame();
        int only = gx_row_r[wob].w > 0 && gx_row_r[kick].w == 0;
        for (int i = 0; i < NSYNTH_PRESETS && i < 128; i++)
            if (gx_row_r[i].w && (strcmp(SYNTH_PRESETS[i].genre, "Dubstep") || strcmp(SYNTH_PRESETS[i].kind, "Bass"))) only = 0;
        check(gx_genre == dub && gx_kind == bassk && only, "picking a style and a kind lists just those sounds");
        r = gx_row_r[wob];
        sim(r.x + 20, r.y + 5, 1, 1, 0, 0);
        frame();
        check(gx_preset == wob && scratch && scratch == gx_heard, "clicking a sound picks it and plays it");
        float *heard = gx_heard;
        r = gx_slide_r[MAC_BRIGHT];
        sim_drag(r.x + r.w / 2, r.y + r.h / 2, r.x - 30, r.y + r.h / 2, 0);
        frame();
        check(GX.macro[MAC_BRIGHT] == 0 && gx_heard != heard, "dragging a slider changes the sound and plays it again");
        r = gx_slide_r[MAC_BRIGHT];
        ldouble = 1;
        sim(r.x + 3, r.y + r.h / 2, 1, 1, 0, 0);
        ldouble = 0;
        check(GX.macro[MAC_BRIGHT] == 0.5, "double-clicking a slider puts it back in the middle");
        int ns = P.nsamples;
        r = gx_lib_r;
        sim(r.x + 5, r.y + 5, 1, 1, 0, 0);
        check(P.nsamples == ns + 1 && P.samples[ns].synth && !strcmp(P.samples[ns].name, "Wobble bass"),
              "+ Library keeps the sound");
        int nc = P.nclips;
        P.playhead = 8 * (long)SR;
        frame();
        r = gx_place_r;
        sim(r.x + 5, r.y + 5, 1, 1, 0, 0);
        int placed = P.nclips == nc + 1 && P.clips[nc].start == snap(8 * (long)SR) && P.samples[P.clips[nc].sample].synth;
        for (int c = 0; c < nc && placed; c++)              /* on a track that was free there */
            placed = !(P.clips[c].track == P.clips[nc].track && P.clips[c].start < P.clips[nc].start + P.clips[nc].length &&
                       P.clips[c].start + P.clips[c].length > P.clips[nc].start);
        check(placed, "Put it at the playhead places it on a free track");
        GX.note = 38;
        frame();
        r = gx_inst_r;
        sim(r.x + 5, r.y + 5, 1, 1, 0, 0);
        check(P.inst_sample == P.nsamples - 1 && P.inst_root == 38, "Play notes with it makes it the instrument at its note");
        r = gx_kind_r[0];                       /* the sound being shaped stays when the filter changes */
        sim(r.x + 5, r.y + 5, 1, 1, 0, 0);
        gx_kind = -1;
        for (int i = 0; i < NSYNTH_KINDS; i++)
            if (!strcmp(SYNTH_KINDS[i], "Drums")) gx_kind = i;
        frame();
        check(gx_row_r[wob].w > 0 && gx_preset == wob, "the sound you're shaping stays listed when you change the filter");
        play_scratch(NULL, 0);
        gx_genre = gx_kind = gx_preset = -1;
        sounds_sub = 0;
        history_check();
    }


    /* ---- vowels and the Singer */
    {
        double share[2];
        for (int k = 0; k < 2; k++) {           /* "aah" has more up around 1 kHz than "ooh" */
            SynthSound y;
            synth_from_preset(&y, 0);
            memset(&y.patch, 0, sizeof y.patch);
            y.patch.wave = SW_SAW;
            y.patch.sustain = 1;
            y.patch.vowel = k ? 5 : 1;
            y.note = 48;
            y.seconds = 0.5;
            long n;
            float *d = render_synth(&y, &n);
            double kk = exp(-2 * 3.14159265358979 * 900.0 / SR), lp = 0, e = 0, a = 0;
            for (long i = 0; i < n; i++) {
                double x = d[2 * i];
                lp = x + (lp - x) * kk;
                e += (x - lp) * (x - lp);
                a += x * x;
            }
            share[k] = e / a;
            free(d);
        }
        check(share[0] > share[1] * 2, "the vowel controls change the voice (aah is brighter than ooh)");

        char syl[16][40];
        int rest[16];
        int n = vocal_syllables("Hap-py birth-day, to you. Ça va", syl, rest, 16);
        check(n == 8 && !strcmp(syl[0], "Hap") && !strcmp(syl[3], "day") && rest[4] == 1 && rest[6] == 2 &&
                  !strcmp(syl[6], "Ça") && rest[1] == 0,
              "words split into syllables at spaces and hyphens, with rests at commas and full stops");

        sv_defaults();
        int keep_root = th_root, keep_scale = th_scale;
        th_root = 0;
        th_scale = 0;
        SV.first = 48;
        SV.tune = TUNE_UPDOWN;
        snprintf(SV.words, sizeof SV.words, "one two three four five six");
        sv_make_notes();
        check(SV.nnotes == 6 && SV.notes[0].note == 48 && SV.notes[1].note == 50 && SV.notes[2].note == 52 &&
                  SV.notes[3].note == 53 && SV.notes[4].note == 55 && SV.notes[5].note == 53 &&
                  SV.notes[5].len > SV.notes[4].len,
              "a tune walks the key's scale, and the last word is held");
        th_root = keep_root;
        th_scale = keep_scale;
    }
    if (speech_available(NULL, 0)) {
        VocalSpec v;
        memset(&v, 0, sizeof v);
        snprintf(v.words, sizeof v.words, "la");
        v.nnotes = 1;
        v.notes[0] = (VocalNote){57, 0, 2};      /* A3, 220 Hz, held a second at 120 BPM */
        v.bpm = 120;
        v.volume = 0.8;
        char err[200] = "";
        long n;
        float *d = render_vocal(&v, &n, err, sizeof err);
        double best = 0;
        int lag = 0;
        if (d) {                                 /* the pitch in the middle, by autocorrelation */
            long mid = n / 2;
            for (int L = SR / 400; L < SR / 100; L++) {
                double c = 0, e1 = 0, e2 = 0;
                for (long i = mid; i < mid + 4096; i++) {
                    c += d[2 * i] * d[2 * (i + L)];
                    e1 += d[2 * i] * d[2 * i];
                    e2 += d[2 * (i + L)] * d[2 * (i + L)];
                }
                double r = c / sqrt(e1 * e2 + 1e-12);
                if (r > best) {
                    best = r;
                    lag = L;
                }
            }
        }
        double hz = lag ? (double)SR / lag : 0;
        check(d && fabs(hz - 220) < 220 * 0.03 && n >= SR, "a sung note is in tune and as long as the note");
        free(d);

        int bad = 0;
        snprintf(v.words, sizeof v.words, "hel-lo world");
        v.nnotes = 3;
        v.notes[0] = (VocalNote){60, 0, 0.5};
        v.notes[1] = (VocalNote){62, 0.5, 0.5};
        v.notes[2] = (VocalNote){64, 1, 1};
        for (int st = 0; st < VS_STYLES; st++)
            for (int hi = 0; hi < 2; hi++) {
                v.style = st;
                v.high = hi;
                d = render_vocal(&v, &n, err, sizeof err);
                double e = 0;
                for (long i = 0; d && i < 2 * n; i++) {
                    if (!isfinite(d[i])) bad++;
                    e += d[i] * d[i];
                }
                if (!d || sqrt(e / (2 * n)) < 0.02) bad++;
                free(d);
            }
        check(!bad, "every voice sings, low and high, with clean audio");

        int off = 0;                             /* the clear singers: in tune on every note */
        for (int sg = 0; sg < NCLEAR_SINGERS; sg++) {
            VocalSpec c;
            memset(&c, 0, sizeof c);
            c.style = VS_CLEAR;
            c.singer = sg;
            c.bpm = 120;
            c.volume = 0.8;
            snprintf(c.words, sizeof c.words, "sing to me");
            int base = CLEAR_SINGERS[sg].high ? 64 : 50;
            for (int k = 0; k < 3; k++) c.notes[k] = (VocalNote){base + 2 * k, k * 1.0, 0.95};
            c.nnotes = 3;
            float *cd = render_vocal(&c, &n, err, sizeof err);
            for (int k = 0; cd && k < 3; k++) {
                double want = 440 * pow(2, (c.notes[k].note - 69) / 12.0);
                int T = (int)lround(SR / want);
                long m = (long)((k + 0.5) * 0.5 * SR);
                double cc = 0, e1 = 0, e2 = 0, ch = 0, e3 = 0;   /* alike a period later, not half a period */
                for (long q = m; q < m + 2048; q++) {
                    cc += cd[2 * q] * cd[2 * (q + T)];
                    ch += cd[2 * q] * cd[2 * (q + T / 2)];
                    e1 += cd[2 * q] * cd[2 * q];
                    e2 += cd[2 * (q + T)] * cd[2 * (q + T)];
                    e3 += cd[2 * (q + T / 2)] * cd[2 * (q + T / 2)];
                }
                double rt = cc / sqrt(e1 * e2 + 1e-12), rh = ch / sqrt(e1 * e3 + 1e-12);
                if (!(rt > 0.85 && rh < rt - 0.2)) off++;
            }
            if (!cd) off += 3;
            free(cd);
        }
        check(!off, "every Clear singer sings each note in tune");

        Project *T = malloc(sizeof *T);
        project_init(T);
        v.style = VS_CHOIR;
        int si = sample_generate_vocal(T, &v, "Sung: hello world", err, sizeof err);
        char msg[1200];
        int loaded = 0;
        Project *U = malloc(sizeof *U);
        if (si >= 0 && project_save(T, tpath, msg, sizeof msg) == 0) loaded = project_load(U, tpath, msg, sizeof msg) == 0;
        check(loaded && U->samples[si].vocal && !strcmp(U->samples[si].voc.words, "hel-lo world") &&
                  U->samples[si].voc.nnotes == 3 && U->samples[si].voc.notes[2].note == 64 &&
                  U->samples[si].voc.style == VS_CHOIR && U->samples[si].frames == T->samples[si].frames &&
                  !memcmp(U->samples[si].data, T->samples[si].data, (size_t)T->samples[si].frames * 2 * sizeof(float)),
              "a sung line saves as words and notes and sings the same when opened");
        long before = T->samples[si].frames;
        int rate_ok = project_set_rate(T, 96000, msg, sizeof msg) == 0 && labs(T->samples[si].frames - 2 * before) <= 2;
        project_set_rate(T, 48000, msg, sizeof msg);
        check(rate_ok, "a sung line is sung again at a new project rate");
        if (loaded) project_free(U);
        free(U);
        project_free(T);
        free(T);
        remove(tpath);

        /* the Singer tab, by mouse and keyboard */
        show_panel(RI_SOUNDS);
        sounds_sub = 2;
        sounds_scroll = 0;
        sv_defaults();
        sv_espeak = -1;
        frame();
        SDL_Rect r = sv_words_r;
        sim(r.x + 5, r.y + 5, 1, 1, 0, 0);
        tf_insert("sing a song");
        tf_key((SDL_Keysym){.sym = SDLK_RETURN});
        frame();
        check(!strcmp(SV.words, "sing a song"), "typing words into the Singer");
        r = sv_sing_r;
        sim(r.x + 5, r.y + 5, 1, 1, 0, 0);
        check(scratch && scratch == sv_heard, "Sing it sings them");
        {                                       /* scroll down to the buttons, as you would */
            SDL_Rect pr = panel_rect();
            int over = sv_place_r.y + sv_place_r.h + 10 - (pr.y + pr.h);
            if (over > 0) sounds_scroll += over;
            frame();
        }
        int ns = P.nsamples;
        r = sv_lib_r;
        sim(r.x + 5, r.y + 5, 1, 1, 0, 0);
        check(P.nsamples == ns + 1 && P.samples[ns].vocal && !strcmp(P.samples[ns].name, "Sung: sing a song"),
              "+ Library keeps the sung line");
        int nc = P.nclips;
        P.playhead = 4 * (long)SR;
        r = sv_place_r;
        sim(r.x + 5, r.y + 5, 1, 1, 0, 0);
        check(P.nclips == nc + 1 && P.samples[P.clips[nc].sample].vocal && P.clips[nc].start == snap(4 * (long)SR),
              "Put it at the playhead places the sung line");
        int c = new_note_clip(P.ntracks - 1, 12 * beat_frames());   /* a melody to sing on */
        roll_close();
        int pat = P.clips[c].pattern;
        long bf = beat_frames();
        note_add(pat, 67, P.clips[c].offset, bf, 0.8f);
        note_add(pat, 69, P.clips[c].offset + bf, bf, 0.8f);
        note_add(pat, 71, P.clips[c].offset + 2 * bf, 2 * bf, 0.8f);
        selected = c;
        r = sv_tune_r[TUNE_CLIP];
        sim(r.x + 5, r.y + 5, 1, 1, 0, 0);
        frame();
        check(SV.tune == TUNE_CLIP && SV.nnotes == 3 && SV.notes[1].note == 69 && fabs(SV.notes[2].start - 2) < 1e-6 &&
                  fabs(SV.notes[2].len - 2) < 1e-6,
              "From a note clip sings on that clip's notes");
        nc = P.nclips;
        r = sv_place_r;
        sim(r.x + 5, r.y + 5, 1, 1, 0, 0);
        check(P.nclips == nc + 1 && P.clips[nc].start == P.clips[c].start && P.clips[nc].track != P.clips[c].track,
              "Put it with the clip lines it up under the melody");
        play_scratch(NULL, 0);
        sounds_sub = 0;
        history_check();
    } else {
        check(1, "(eSpeak NG isn't installed here: the singing tests were skipped)");
    }


    /* New sound's Note box: drag it, scroll it, or click and type a note */
    {
        show_panel(RI_SOUNDS);
        sounds_sub = 3;
        sounds_scroll = 0;
        G.wave = WAVE_SAW;
        G.note = 60;
        frame();
        SDL_Rect r = ns_note_r;
        sim_drag(r.x + 20, r.y + 10, r.x + 20 + 40, r.y + 10, 0);
        check(G.note == 65, "dragging New sound's Note box changes the note");
        sim(r.x + 20, r.y + 10, 1, 0, 0, 0);
        sim(r.x + 20, r.y + 10, 0, 1, 0, 0);
        check(numedit.active && numedit.iv == &G.note && G.note == 65, "clicking the Note box opens it for typing");
        numedit_text("F#3");
        numedit_key((SDL_Keysym){.sym = SDLK_RETURN});
        frame();
        check(G.note == 54 && !numedit.active, "typing F#3 sets the note");
        sim(r.x + 20, r.y + 10, 1, 0, 0, 0);
        sim(r.x + 20, r.y + 10, 0, 1, 0, 0);
        numedit_text("bb2");
        numedit_key((SDL_Keysym){.sym = SDLK_RETURN});
        frame();
        int flat = G.note == 46;
        sim(r.x + 20, r.y + 10, 1, 0, 0, 0);
        sim(r.x + 20, r.y + 10, 0, 1, 0, 0);
        numedit_text("72");
        numedit_key((SDL_Keysym){.sym = SDLK_RETURN});
        frame();
        int num = G.note == 72;
        sim(r.x + 20, r.y + 10, 1, 0, 0, 0);
        sim(r.x + 20, r.y + 10, 0, 1, 0, 0);
        numedit_text("#5");
        numedit_key((SDL_Keysym){.sym = SDLK_RETURN});
        frame();
        check(flat && num && G.note == 72, "Bb2 and 72 work too, and something that isn't a note changes nothing");
        mx = r.x + 20;
        my = r.y + 10;
        wheel_y = 2;
        lpress = lrel = rpress = 0;
        frame();
        check(G.note == 74, "the mouse wheel steps the note");
        G.note = 45;
        sounds_sub = 0;
    }


    /* ---- copying sounds between the library, pads and the instrument */
    {
        show_panel(RI_PADS);
        frame();
        int a = -1, b = -1;
        for (int i = 0; i < P.nsamples; i++)
            if (!P.samples[i].pitched && !P.samples[i].removed) {
                if (a < 0) a = i;
                else if (b < 0) b = i;
            }
        LOCK();
        for (int i = 0; i < NPADS; i++) P.pads[i] = (Pad){-1, PAD_ONESHOT, 0.8f, 0, 0, 0};
        UNLOCK();
        history_check();
        sound_copy(CM_LIB, a);                  /* library to pad, by the right-click menu */
        SDL_Rect r = pad_rects[5];
        sim(r.x + 10, r.y + 10, 0, 0, 1, 0);
        int opened = cm.open && cm.kind == CM_PAD && cm.idx == 5 && cm.enabled[1];
        sim(cm.box.x + 14, cm.box.y + 4 + 1 * (font_h + 12) + 6, 1, 1, 0, 0);
        check(opened && !cm.open && P.pads[5].sample == a, "right-click a pad, Paste: it plays the copied library sound");
        LOCK();
        P.pads[5].pitch = 7;
        P.pads[5].mode = PAD_LOOP;
        UNLOCK();
        frame();
        mx = pad_rects[5].x + 10;               /* pad to pad with Ctrl+C / Ctrl+V: its settings too */
        my = pad_rects[5].y + 10;
        key((SDL_Keysym){.sym = SDLK_c, .mod = KMOD_CTRL});
        mx = pad_rects[9].x + 10;
        my = pad_rects[9].y + 10;
        key((SDL_Keysym){.sym = SDLK_v, .mod = KMOD_CTRL});
        check(P.pads[9].sample == a && P.pads[9].pitch == 7 && P.pads[9].mode == PAD_LOOP,
              "Ctrl+C on a pad and Ctrl+V on another copies the sound and the pad's settings");
        frame();
        history_check();
        show_panel(RI_SOUNDS);                  /* a pitched pad into the library: its own sound */
        sounds_sub = 0;
        frame();
        int ns = P.nsamples, nvis0 = 0, nvis1 = 0;
        for (int i = 0; i < P.nsamples; i++) nvis0 += (!P.samples[i].pitched || P.samples[i].listed) && !P.samples[i].removed;
        SDL_Rect pr = panel_rect();
        mx = pr.x + pr.w / 2;
        my = pr.y + pr.h - 80;
        key((SDL_Keysym){.sym = SDLK_v, .mod = KMOD_CTRL});
        int pasted = -1;
        for (int i = 0; i < P.nsamples; i++) {
            nvis1 += (!P.samples[i].pitched || P.samples[i].listed) && !P.samples[i].removed;
            if (P.samples[i].listed) pasted = i;
        }
        check(pasted >= 0 && P.samples[pasted].src == a && P.samples[pasted].semis == 7 && nvis1 == nvis0 + 1 &&
                  P.nsamples <= ns + 1,
              "pasting a pitched pad into the library adds that pitched sound to it");
        frame();
        history_check();
        undo_redo(0);
        int gone = !P.samples[pasted].listed;
        undo_redo(1);
        check(gone && P.samples[pasted].listed, "undo takes the pasted sound out of the library, redo puts it back");
        remove_sound(b);                        /* a removed sound comes back when pasted */
        frame();
        history_check();
        sclip = (typeof(sclip)){1, b, 0, {0}};
        sound_paste(CM_LIB, -1);
        check(!P.samples[b].removed, "pasting a removed sound brings it back into the library");
        lib_scroll = 0;
        frame();
        r = lib_rects[a];                       /* right-click a library sound: the menu */
        sim(r.x + 10, r.y + 5, 0, 0, 1, 0);
        int lib_menu = cm.open && cm.kind == CM_LIB && cm.idx == a && cm.act[2] == CMA_REMOVE;
        sim(cm.box.x - 50, cm.box.y - 50, 1, 1, 0, 0);    /* a click elsewhere closes it, doing nothing */
        check(lib_menu && !cm.open && !P.samples[a].removed, "right-clicking a library sound opens Copy, Paste, Remove");
        history_check();
    }


    /* ---- a tempo change: selected clips follow it */
    {
        double sr = SR;
        int ok_len = 0, ok_pitch = 0;
        long n = SR;                             /* time-stretch keeps the pitch */
        float *sine = malloc((size_t)n * 2 * sizeof(float));
        for (long i = 0; i < n; i++) sine[2 * i] = sine[2 * i + 1] = (float)(0.5 * sin(2 * 3.14159265358979 * 440 * i / sr));
        long sn;
        float *st = time_stretch(sine, n, 1.5, &sn);
        if (st) {
            ok_len = labs(sn - (long)(1.5 * n)) < 4;
            int zc = 0;
            double e = 0, emin = 1e9;
            for (long i = 1; i < sn; i++) zc += (st[2 * i] >= 0) != (st[2 * i - 2] >= 0);
            for (long b = SR / 10; b + SR / 50 < sn - SR / 10; b += SR / 50) {   /* no dips between frames */
                e = 0;
                for (long i = b; i < b + SR / 50; i++) e += st[2 * i] * st[2 * i];
                emin = fmin(emin, e / (SR / 50));
            }
            ok_pitch = fabs(zc / (sn / sr) / 2 - 440) < 6 && emin > 0.08;
        }
        free(st);
        check(ok_len && ok_pitch, "time-stretching makes it longer at the same pitch, without dips");

        new_project();
        P.bpm = 120;
        long beat = beat_frames();
        int raw = sample_add(&P, "recorded", sine, n);   /* audio with no recipe: stretched */
        sine = NULL;
        BeatSpec bs = genre_spec(0);
        int loop = sample_generate_beat(&P, &bs, 0x3F, "house beat");
        SynthSound y;
        synth_from_preset(&y, 0);
        int shot = sample_generate_synth(&P, &y, "Deep bass");
        int c_raw = clip_add(raw, 0, 4 * beat, 0, n);
        int c_loop = clip_add(loop, 1, 8 * beat, 0, P.samples[loop].frames);
        int c_shot = clip_add(shot, 2, 2 * beat, 0, beat);
        int c_keep = clip_add(shot, 3, 6 * beat, 0, beat);
        int c_note = clip_add(shot, 4, 4 * beat, 0, 4 * beat);
        int pat = pattern_new_id(&P);
        P.clips[c_note].pattern = pat;
        P.clips[c_note].root = 36;
        note_add(pat, 40, beat, beat, 0.8f);
        frame();
        history_check();
        sel_clear();
        csel[c_raw] = csel[c_loop] = csel[c_shot] = csel[c_note] = 1;
        sel_fix_primary();
        int u0 = nundo;
        double before = P.bpm;
        P.bpm = 100;                             /* as if typed into the BPM box */
        tempo_touch(before);
        tempo_at = SDL_GetTicks() - 1000;
        frame();
        if (drag.kind == D_NONE) history_check();
        double r = 1.2;
        const Sample *rs = &P.samples[P.clips[c_raw].sample], *ls = &P.samples[P.clips[c_loop].sample];
        check(P.clips[c_raw].start == (long)llround(4 * beat * r) && rs->pitched && fabs(rs->stretch - r) < 1e-9 &&
                  labs(rs->frames - (long)(n * r)) < 4 && labs(P.clips[c_raw].length - (long)llround(n * r)) <= 1,
              "selected audio keeps its bar and is time-stretched to the new tempo");
        check(P.clips[c_loop].start == (long)llround(8 * beat * r) && ls->is_beat == 1 && ls->gen_bpm == 100 && ls->hidden &&
                  labs(ls->frames - (long)llround(P.samples[loop].frames * r)) < 8,
              "a selected beat loop is made again at the new tempo");
        int ni = -1;
        for (int i = 0; i < P.nnotes; i++)
            if (P.notes[i].pattern == P.clips[c_note].pattern) ni = i;
        check(ni >= 0 && P.notes[ni].start == (long)llround(beat * r) && P.notes[ni].length == (long)llround(beat * r) &&
                  P.clips[c_note].length == (long)llround(4 * beat * r),
              "a selected note clip's notes move to the new tempo");
        check(P.clips[c_shot].sample == shot && P.clips[c_shot].start == (long)llround(2 * beat * r) &&
                  P.clips[c_keep].start == 6 * beat,
              "one-shots just move, and clips not selected keep their times");
        undo_redo(0);
        check(nundo == u0 && P.bpm == 120 && P.clips[c_raw].sample == raw && P.clips[c_loop].start == 8 * beat &&
                  P.notes[ni].start == beat,
              "one undo puts the tempo and the clips back");
        undo_redo(1);
        char msg[1200];
        int saved = project_save(&P, tpath, msg, sizeof msg) == 0;
        Project *U = malloc(sizeof *U);
        int loaded = saved && project_load(U, tpath, msg, sizeof msg) == 0;
        int si = P.clips[c_raw].sample, li = P.clips[c_loop].sample;
        check(loaded && fabs(U->samples[si].stretch - r) < 1e-9 && U->samples[si].frames == P.samples[si].frames &&
                  U->samples[li].hidden && U->samples[li].gen_bpm == 100,
              "stretched and remade sounds save as recipes and open the same");
        if (loaded) project_free(U);
        free(U);
        remove(tpath);
        sel_clear();
    }


    /* ---- chords from Theory into the piano roll; clips dropped on the library */
    {
        new_project();
        P.bpm = 120;
        long bar = 4 * beat_frames();
        int c = new_note_clip(0, 2 * bar);       /* opens the roll on an empty one-bar clip */
        P.playhead = 0;                          /* outside the clip: chords go after the last note */
        frame();
        history_check();
        int cmaj[3] = {48, 52, 55}, u0 = nundo;
        roll_put_chord(cmaj, 3, 4, "C");
        int first = 0, sel = 0;
        for (int i = 0; i < P.nnotes; i++)
            if (P.notes[i].pattern == roll_pattern) {
                first += P.notes[i].start == P.clips[c].offset && P.notes[i].length == bar;
                sel += nsel[i];
            }
        check(first == 3 && sel == 3 && P.playhead == P.clips[c].start + bar,
              "Roll puts a chord at the start of an empty note clip, selected, with the playhead after it");
        frame();
        history_check();
        const Progression *pr = &PROGRESSIONS[0];
        int ns[8][6], cnt[8], m = pr->n < 8 ? pr->n : 8;
        for (int k = 0; k < m; k++) cnt[k] = chord_notes(pr->root[k] % 12, pr->type[k], 48, ns[k]);
        roll_put_chords((const int (*)[6])ns, cnt, m, 4, pr->numerals);
        long last = 0;
        for (int i = 0; i < P.nnotes; i++)
            if (P.notes[i].pattern == roll_pattern && P.notes[i].start > last) last = P.notes[i].start;
        check(last == P.clips[c].offset + (long)m * bar && P.clips[c].length == (long)(m + 1) * bar,
              "a progression follows on, one chord a bar, and the clip grows to hold it");
        frame();
        history_check();
        undo_redo(0);
        undo_redo(0);
        int left = 0;
        for (int i = 0; i < P.nnotes; i++) left += P.notes[i].pattern == roll_pattern;
        check(nundo == u0 && left == 0, "each Roll is one undo step");
        roll_close();

        /* a clip dropped on the library: a new sound, with its track's effects, saved as a file */
        new_project();
        long n = SR / 2;
        float *saw = malloc((size_t)n * 2 * sizeof(float));
        for (long i = 0; i < n; i++) saw[2 * i] = saw[2 * i + 1] = (float)(0.5 * (2 * fmod(i * 220.0 / SR, 1.0) - 1));
        int raw = sample_add(&P, "buzz", saw, n);
        int cl = clip_add(raw, 0, beat_frames(), 0, n);
        P.tracks[0].filter = FILT_LOW;           /* an effect that's easy to measure */
        P.tracks[0].cutoff = 300;
        P.tracks[0].mute = 1;                    /* muted tracks still bounce */
        show_panel(RI_SOUNDS);
        sounds_sub = 0;
        view_start = 0;
        frame();
        SDL_Rect r = clip_rect(&P.clips[cl]), pr2 = panel_rect();
        long start0 = P.clips[cl].start;
        int nsamp = P.nsamples;
        sim_drag(r.x + r.w / 2, r.y + r.h / 2, pr2.x + pr2.w / 2, pr2.y + pr2.h / 2, 0);
        int got = P.nsamples == nsamp + 1 ? nsamp : -1;
        double hf_raw = 0, hf_new = 0, e_raw = 0, e_new = 0;   /* the share above 2 kHz */
        if (got >= 0) {
            const Sample *b = &P.samples[got], *o = &P.samples[raw];
            double kk = exp(-2 * 3.14159265358979 * 2000.0 / SR), l1 = 0, l2 = 0;
            for (long i = 0; i < n && i < b->frames; i++) {
                double x1 = o->data[2 * i], x2 = b->data[2 * i];
                l1 = x1 + (l1 - x1) * kk;
                l2 = x2 + (l2 - x2) * kk;
                hf_raw += (x1 - l1) * (x1 - l1);
                e_raw += x1 * x1;
                hf_new += (x2 - l2) * (x2 - l2);
                e_new += x2 * x2;
            }
        }
        check(got >= 0 && P.clips[cl].start == start0 && P.nclips == 1 && file_exists(P.samples[got].path) &&
                  labs(P.samples[got].frames - n) < SR / 100 && e_new > 0 && hf_new / e_new < 0.2 * hf_raw / e_raw,
              "dropping a clip on the library makes a new sound with its effects, saved as a file; the clip stays");
        if (got >= 0) remove(P.samples[got].path);
        new_project();
        make_demo();                             /* the tests after this expect the demo song */
        sounds_sub = 0;
    }


    /* ---- the clip editor window */
    {
        new_project();
        long n = 2 * SR;
        float *tone = malloc((size_t)n * 2 * sizeof(float));
        for (long i = 0; i < n; i++) tone[2 * i] = tone[2 * i + 1] = (float)(0.25 * sin(2 * 3.14159265358979 * 330 * i / (double)SR));
        int sm = sample_add(&P, "tone", tone, n);
        long bar = 4 * beat_frames();
        int c = clip_add(sm, 1, bar, 0, n);
        view_start = 0;
        frame();
        history_check();
        SDL_Rect r = clip_rect(&P.clips[c]);       /* double-click an audio clip: its editor opens */
        ldouble = 1;
        sim(r.x + r.w / 2, r.y + r.h / 2, 1, 1, 0, 0);
        ldouble = 0;
        check(ewin && ed_clip == c, "double-clicking an audio clip opens it in its own editor window");
        if (ewin) {
            ed_sim(-100, -100, 0, 0);
            Clip o = P.clips[c];
            int ex = ed_frame_to_x(o.offset + o.length), sx = ed_frame_to_x(o.offset), cy = ed_wave.y + ed_wave.h / 2;
            ed_sim(ex, cy, 1, 0);                  /* drag the end in */
            ed_sim(ex - 100, cy, 0, 0);
            ed_sim(ex - 100, cy, 0, 1);
            long len1 = P.clips[c].length;
            check(len1 < o.length - SR / 10 && P.clips[c].start == o.start && drag.kind == D_NONE,
                  "dragging the end trims the clip");
            ed_sim(sx, cy, 1, 0);                  /* and the start: the end stays where it was */
            ed_sim(sx + 60, cy, 0, 0);
            ed_sim(sx + 60, cy, 0, 1);
            Clip t = P.clips[c];
            check(t.offset > 0 && t.start == o.start + t.offset && t.start + t.length == o.start + len1,
                  "dragging the start trims it from the front, keeping the end");
            int fx = ed_frame_to_x(t.offset);
            ed_sim(fx, ed_wave.y, 1, 0);           /* the fade-in square */
            ed_sim(fx + 40, ed_wave.y, 0, 0);
            ed_sim(fx + 40, ed_wave.y, 0, 1);
            check(P.clips[c].fade_in > SR / 20, "dragging the square on top fades it in");
            SDL_Rect b = ed_btn[1];
            ed_sim(b.x + 5, b.y + 5, 1, 1);        /* Normalize: the loudest moment at -1 dB */
            check(fabs(P.clips[c].gain_db - (-1 - 20 * log10(0.25))) < 0.2, "Normalize sets the gain so the peak is -1 dB");
            frame();
            history_check();
            undo_redo(0);
            int undone = fabs(P.clips[c].gain_db) < 0.01;
            undo_redo(1);
            check(undone, "editor changes undo like any other");
            LOCK();
            P.playhead = P.clips[c].start + P.clips[c].length / 2;
            UNLOCK();
            Clip before = P.clips[c];
            int nc = P.nclips;
            ed_sim(-100, -100, 0, 0);
            b = ed_btn[3];
            ed_sim(b.x + 5, b.y + 5, 1, 1);        /* Split at the playhead */
            check(P.nclips == nc + 1 && P.clips[c].length + P.clips[nc].length == before.length &&
                      P.clips[nc].start == before.start + P.clips[c].length &&
                      P.clips[nc].offset == before.offset + P.clips[c].length,
                  "Split at the playhead makes two clips that play the same as one");
            ed_sim(-100, -100, 0, 0);
            b = ed_btn[5];
            ed_sim(b.x + 5, b.y + 5, 1, 1);        /* Delete: the clip goes, and so does the window */
            check(!ewin && P.nclips == nc, "Delete removes the clip and closes the editor");
        }
        editor_close();
        new_project();
        make_demo();
    }

    /* ---- Export: choosing the folder and file name */
    {
        char keep_proj[1024];
        snprintf(keep_proj, sizeof keep_proj, "%s", proj_path);
        snprintf(proj_path, sizeof proj_path, "/somewhere/My Song.tdaw");
        open_export();
        check(export_open && !strcmp(export_name, "My Song") && export_dir[0], "Export names the file after the project");
        export_take_path("/a/b/take two.flac");
        check(!strcmp(export_dir, "/a/b") && !strcmp(export_name, "take two") && EXO.format == EXP_FLAC,
              "a file chosen in the system dialog sets the folder, the name and the format");
        const char *tmpd = getenv("TMPDIR") ? getenv("TMPDIR") : getenv("TEMP") ? getenv("TEMP") : "/tmp";
        snprintf(export_dir, sizeof export_dir, "%s", tmpd);
        EXO.format = EXP_WAV;
        frame();
        SDL_Rect r = export_name_r;
        sim(r.x + 5, r.y + 5, 1, 1, 0, 0);
        check(tf.buf == export_name, "clicking the file name lets you type it");
        export_name[0] = 0;
        tf_insert("mix/1.mp3");
        tf_key((SDL_Keysym){.sym = SDLK_RETURN});
        frame();
        check(!tf.buf && !strcmp(export_name, "mix1") && EXO.format == EXP_MP3,
              "typing a name keeps slashes out, and .mp3 picks MP3");
        sim(r.x + 5, r.y + 5, 1, 1, 0, 0);
        tf_insert("zzz");
        tf_key((SDL_Keysym){.sym = SDLK_ESCAPE});
        check(!strcmp(export_name, "mix1"), "Esc puts the name back");
        snprintf(export_dir, sizeof export_dir, "%s/no-such-dir-%u", tmpd, (unsigned)SDL_GetTicks());
        frame();
        r = export_go_r;
        sim(r.x + 5, r.y + 5, 1, 1, 0, 0);
        check(export_open && !export_pending, "Export waits for a folder that exists");
        snprintf(export_dir, sizeof export_dir, "%s", tmpd);
        snprintf(export_name, sizeof export_name, "tinydaw-selftest-export-%u", (unsigned)SDL_GetTicks());
        EXO.format = EXP_WAV;
        EXO.loop_only = 0;
        EXO.stems = 0;
        frame();
        sim(r.x + 5, r.y + 5, 1, 1, 0, 0);
        frame();
        frame();
        char out[1400];
        snprintf(out, sizeof out, "%s/%s.wav", tmpd, export_name);
        check(!export_open && file_exists(out), "Export writes to the chosen folder and name");
        remove(out);
        snprintf(proj_path, sizeof proj_path, "%s", keep_proj);
    }

    /* a controller, through SDL's own events (a virtual gamepad stands in for hardware) */
    int vj = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 4, 12, 1);
    if (vj >= 0) {
        SDL_Joystick *vh = SDL_JoystickOpen(vj);
        int got_added = 0, got_hit = 0;
        binds_default();
        P.pads[0] = (Pad){0, PAD_HOLD, 0.8f, 0, 0, 0};
        P.pads[12] = (Pad){0, PAD_HOLD, 0.8f, 0, 0, 0};
        LOCK();
        voices_clear(&P);
        UNLOCK();
        for (int step = 0; step < 4; step++) {
            if (step == 1) SDL_JoystickSetVirtualButton(vh, 0, 1);
            if (step == 2) SDL_JoystickSetVirtualHat(vh, 0, SDL_HAT_UP);
            SDL_JoystickUpdate();
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_JOYDEVICEADDED) {
                    joy_added(e.jdevice.which);
                    got_added = 1;
                } else {
                    joy_event(&e);
                }
            }
            if (step == 1) got_hit = voice_playing(&P, 0);
        }
        check(got_added && njoys > 0, "a plugged-in controller is picked up");
        check(got_hit, "a controller button plays its pad");
        check(voice_playing(&P, 12), "a d-pad direction plays its pad");
        {                                       /* the right stick bends pitch */
            int k = joy_index(SDL_JoystickInstanceID(vh));
            int keep = joy_bend;
            float b[3];
            joy_bend = JB_RIGHT_Y;
            for (int step = 0; step < 3; step++) {
                SDL_JoystickSetVirtualAxis(vh, k >= 0 ? stick_axis[k][3] : 3, step == 0 ? -32767 : step == 1 ? 0 : -32767);
                if (step == 2) joy_bend = JB_OFF;
                SDL_JoystickUpdate();
                SDL_Event e;
                while (SDL_PollEvent(&e))
                    if (e.type != SDL_JOYDEVICEADDED) joy_event(&e);
                b[step] = P.bend;
            }
            joy_bend = keep;
            check(b[0] > 0.99f && b[1] == 0, "pushing the right stick up bends the pitch up, and it springs back");
            check(b[2] == 0, "with pitch bend off the stick doesn't bend");
            P.bend = 0;
        }
        SDL_JoystickClose(vh);
        SDL_JoystickDetachVirtual(vj);
    } else {
        check(0, "SDL virtual joystick for the controller test");
    }

    printf("%d failed\n", failures);
    return failures ? 1 : 0;
}

/* ---------------------------------------------------------------- main */

static TTF_Font *open_font(int size)
{
    char beside[1100] = "";                  /* a font shipped next to the program (Windows) */
    char *exe_dir = SDL_GetBasePath();
    if (exe_dir) {
        snprintf(beside, sizeof beside, "%sDejaVuSansMono.ttf", exe_dir);
        SDL_free(exe_dir);
    }
    const char *paths[] = {getenv("TINYDAW_FONT"), beside,
#ifdef _WIN32
                           "C:/Windows/Fonts/consola.ttf", "C:/Windows/Fonts/cour.ttf",
#endif
                           "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
                           "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                           "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
                           "/usr/share/fonts/noto/NotoSansMono-Regular.ttf",
                           "/usr/share/fonts/liberation/LiberationMono-Regular.ttf"};
    for (size_t i = 0; i < sizeof paths / sizeof *paths; i++) {
        if (!paths[i]) continue;
        TTF_Font *f = TTF_OpenFont(paths[i], size);
        if (f) return f;
    }
    return NULL;
}

/* ---------------------------------------------------------------- theory pane */

#define PI_D 3.14159265358979323846

static void play_scratch(float *buf, long frames)
{
    LOCK();
    float *old = scratch;
    scratch = buf;
    scratch_len = buf ? frames : 0;
    scratch_pos = 0;
    UNLOCK();
    free(old);
}

/* The generator's current sound, made suitable for chords: pitched, with soft edges. */
static GenParams chord_params(double beats)
{
    GenParams g = G;
    g.a4 = P.a4;
    if (g.wave >= WAVE_NOISE) g.wave = WAVE_TRIANGLE;
    g.beats = beats;
    if (g.attack_ms < 5) g.attack_ms = 5;
    if (g.release_ms < 120) g.release_ms = 120;
    return g;
}

static void preview_notes(const int *notes, int n, double beats)
{
    GenParams g = chord_params(beats);
    long len;
    float *b = render_notes(&g, P.bpm, notes, n, &len);
    if (b) play_scratch(b, len);
}

/* A library sound for these notes with the current settings; reuses an identical one. */
static int chord_sample(const int *notes, int n, const char *label, double beats)
{
    GenParams g = chord_params(beats);
    char name[96];
    snprintf(name, sizeof name, "%s %s %gb", label, WAVE_NAMES[g.wave], beats);
    for (int i = 0; i < P.nsamples; i++) {
        const Sample *s = &P.samples[i];
        if (s->generated && !s->removed && s->nchord == n && !strcmp(s->name, name) && s->gen.volume == g.volume &&
            s->gen.attack_ms == g.attack_ms && s->gen.release_ms == g.release_ms && s->gen_bpm == P.bpm &&
            !memcmp(s->chord, notes, (size_t)n * sizeof(int)))
            return i;
    }
    return sample_generate_notes(&P, &g, notes, n, name);
}

/* Sounds added from Theory: the New sound panel's wave and envelope, at this length */
static const double TH_LENS[4] = {1, 2, 4, 8};
static const char *const TH_LEN_NAMES[4] = {"1 beat", "2 beats", "1 bar", "2 bars"};

static void library_added(int s, int existed)
{
    if (s < 0) {
        status("Library is full");
        return;
    }
    if (existed) {
        status("\"%s\" is already in the library", P.samples[s].name);
        return;
    }
    lib_badge++;
    lib_badge_at = SDL_GetTicks();
    status("Added \"%s\" to the library (Sounds)", P.samples[s].name);
}

/* notes (one or a chord) as a library sound; returns its index */
static int add_to_library(const int *notes, int n, const char *label)
{
    int before = P.nsamples;
    int s = chord_sample(notes, n, label, TH_LENS[th_len]);
    library_added(s, s >= 0 && s < before);
    return s;
}


/* ---- chords from the Theory pane into the open piano roll */

/* where the next chord goes, in the pattern's own frames: the playhead when it's inside the
 * clip, else straight after the last note (on the next beat) */
static long roll_insert_at(void)
{
    const Clip *c = &P.clips[roll_clip];
    long beat = beat_frames();
    if (P.playhead >= c->start && P.playhead < c->start + c->length)
        return c->offset + (snap(P.playhead) - c->start);
    long end = pattern_end(&P, roll_pattern);
    if (end <= c->offset) return c->offset;
    return (end + beat - 1) / beat * beat;
}

/* Puts chords into the roll one after another, each `beats` long; the new notes are selected,
 * so arrows move them, and the playhead moves on so the next chord follows. */
static int roll_put_chords(const int (*notes)[6], const int *counts, int nch, double beats, const char *what)
{
    if (roll_clip < 0) return 0;
    long at = roll_insert_at(), len = (long)llround(beats * beat_frames()), added = 0;
    memset(nsel, 0, sizeof nsel);
    for (int k = 0; k < nch; k++)
        for (int i = 0; i < counts[k]; i++) {
            int j = note_add(roll_pattern, notes[k][i], at + k * len, len, 0.8f);
            if (j >= 0) {
                nsel[j] = 1;
                added++;
            }
        }
    roll_fit_clip();
    int hi = 0, lo = 127;                        /* scroll the keys to show them */
    for (int k = 0; k < nch; k++)
        for (int i = 0; i < counts[k]; i++) {
            hi = notes[k][i] > hi ? notes[k][i] : hi;
            lo = notes[k][i] < lo ? notes[k][i] : lo;
        }
    int rows = roll_kh > 0 && roll_grid.h > 0 ? roll_grid.h / roll_kh : 24;
    if (hi > roll_top || lo < roll_top - rows + 1) roll_top = hi + 4;
    const Clip *c = &P.clips[roll_clip];
    LOCK();
    P.playhead = c->start + (at + nch * len - c->offset);   /* the next one goes after these */
    UNLOCK();
    long bar = 4 * beat_frames();
    status("Put %s in the roll at bar %ld (%ld notes): arrows move them, Ctrl+Z takes them out", what,
           (c->start + at - c->offset) / bar + 1, added);
    return (int)added;
}

static int roll_put_chord(const int *notes, int n, double beats, const char *name)
{
    int one[1][6], cnt[1] = {n};
    memcpy(one[0], notes, sizeof(int) * (size_t)(n < 6 ? n : 6));
    return roll_put_chords((const int (*)[6])one, cnt, 1, beats, name);
}

/* a small "Roll" button under a chord, shown while the piano roll is open */
static int roll_button(SDL_Rect r)
{
    if (roll_clip < 0) return 0;
    return button(r, "Roll", 0);
}

/* a small "+" button: adds something to the library */
static int plus_button(SDL_Rect r)
{
    int hov = hit(r) && drag.kind == D_NONE;
    fill(r, hov ? ACCENT : PANEL3);
    Col c = hov ? DARK : TEXT;
    int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    fill((SDL_Rect){cx - 5, cy - 1, 11, 2}, c);
    fill((SDL_Rect){cx - 1, cy - 5, 2, 11}, c);
    if (hov && lpress) {
        lpress = 0;
        return 1;
    }
    return 0;
}

/* the strip above Theory's sound pages: what added sounds will be */
static int adds_as(int x, int y, int w)
{
    char buf[48];
    static const int PITCHED[4] = {WAVE_SINE, WAVE_SQUARE, WAVE_SAW, WAVE_TRIANGLE};
    GenParams g = chord_params(1);
    text(x, y, "+ adds to the library as:", DIM);
    y += font_h + 6;
    static int wi;
    static const char *const PNAMES[4] = {"sine", "square", "saw", "tri"};
    if (!(dd.open && dd.sel == &wi))
        for (int i = 0; i < 4; i++)
            if (PITCHED[i] == g.wave) wi = i;
    if (dropdown((SDL_Rect){x, y, 128, 26}, "Sound: ", PNAMES, 4, &wi, 1)) G.wave = PITCHED[wi];
    (void)buf;
    int lw = (w - 136 - 12) / 4;
    for (int i = 0; i < 4; i++)
        if (button((SDL_Rect){x + 136 + i * (lw + 4), y, lw, 26}, TH_LEN_NAMES[i], th_len == i)) th_len = i;
    return y + 38;
}

static int prog_flats(const Progression *pr)
{
    return key_uses_flats(th_root, pr->type[0] == CH_MIN ? 1 : 0);
}

static void play_progression(const Progression *pr)
{
    GenParams g = chord_params(2);
    long step = (long)(2 * 60.0 / P.bpm * SR);
    float *all = calloc((size_t)step * pr->n * 2, sizeof(float));
    if (!all) return;
    for (int i = 0; i < pr->n; i++) {
        int notes[6], n = chord_notes((th_root + pr->root[i]) % 12, pr->type[i], 48, notes);
        long len;
        float *b = render_notes(&g, P.bpm, notes, n, &len);
        if (!b) continue;
        memcpy(all + 2 * i * step, b, (size_t)(len < step ? len : step) * 2 * sizeof(float));
        free(b);
    }
    play_scratch(all, step * pr->n);
}

static void place_progression(const Progression *pr)
{
    long bar = 4 * beat_frames();
    long start = (long)llround((double)P.playhead / bar) * bar;
    int flats = prog_flats(pr), placed = 0;
    for (int i = 0; i < pr->n; i++) {
        int notes[6], n = chord_notes((th_root + pr->root[i]) % 12, pr->type[i], 48, notes);
        char name[32];
        chord_name((th_root + pr->root[i]) % 12, pr->type[i], flats, name, sizeof name);
        int s = chord_sample(notes, n, name, 4);
        if (s < 0) break;
        long len = P.samples[s].frames < bar ? P.samples[s].frames : bar;
        if (clip_add(s, th_track, start + i * bar, 0, len) >= 0) placed++;
    }
    status("Placed %s in %s on track %d: %d bars from bar %ld", pr->numerals, pitch_name(th_root, flats),
           th_track + 1, placed, start / bar + 1);
}

static int chord_button(SDL_Rect r, const char *top, const char *bottom)
{
    int hov = hit(r) && drag.kind == D_NONE;
    fill(r, hov ? PANEL3 : PANEL2);
    outline(r, LINE);
    text(r.x + (r.w - text_w(top)) / 2, r.y + 4, top, DIM);
    text(r.x + (r.w - text_w(bottom)) / 2, r.y + 6 + font_h, bottom, TEXT);
    if (hov && lpress) {
        lpress = 0;
        return 1;
    }
    return 0;
}

/* a clickable table row: highlights on hover, returns 1 when clicked */
static int row_hit(SDL_Rect r)
{
    int hov = hit(r) && drag.kind == D_NONE;
    if (hov) fill(r, PANEL3);
    if (hov && lpress) {
        lpress = 0;
        return 1;
    }
    return 0;
}

static void key_signature_text(int parent, int flats, char *buf, size_t n)
{
    int sig = key_signature(parent);
    if (parent == 6 && flats) sig = -6;
    if (sig == 0) {
        snprintf(buf, n, "Key signature: no sharps or flats");
        return;
    }
    const char *order = sig > 0 ? "FCGDAEB" : "BEADGCF";
    int k = sig > 0 ? sig : -sig, len = snprintf(buf, n, "Key signature: %d %s:", k,
                                                 sig > 0 ? (k == 1 ? "sharp" : "sharps") : (k == 1 ? "flat" : "flats"));
    for (int i = 0; i < k && len > 0 && (size_t)len < n; i++)
        len += snprintf(buf + len, n - len, " %c%s", order[i], sig > 0 ? "#" : "b");
}

static int tab_keys(int x, int y, int w)
{
    char buf[256], b2[64];
    int bw = (w - 11 * 3) / 12;
    for (int i = 0; i < 12; i++)
        if (button((SDL_Rect){x + i * (bw + 3), y, bw, 26}, pitch_name(i, key_uses_flats(i, th_scale)), th_root == i))
            th_root = i;
    y += 34;
    int sw = (w - 3 * 4) / 4;
    for (int i = 0; i < NSCALES; i++)
        if (button((SDL_Rect){x + (i % 4) * (sw + 4), y + (i / 4) * 30, sw, 26}, SCALES[i].name, th_scale == i))
            th_scale = i;
    y += ((NSCALES + 3) / 4) * 30 + 10;

    const Scale *sc = &SCALES[th_scale];
    int flats = key_uses_flats(th_root, th_scale);
    text(x, y + 4, "Notes", DIM);
    if (plus_button((SDL_Rect){x, y + font_h + 10, 52, 20})) {     /* every note of the scale */
        int added = 0;
        for (int k = 0; k < sc->n; k++) {
            int note = 60 + th_root + sc->steps[k];
            char nn[16];
            scale_note_name(th_root, th_scale, k, nn, sizeof nn);
            snprintf(b2, sizeof b2, "%s4", nn);
            if (add_to_library(&note, 1, b2) >= 0) added++;
        }
        status("Added the %d notes of %s %s to the library", added, pitch_name(th_root, flats), sc->name);
    }
    int cell = (w - 64) / 7;
    for (int k = 0; k < sc->n; k++) {
        SDL_Rect r = {x + 64 + k * cell, y, cell - 4, 2 * font_h + 8};
        scale_note_name(th_root, th_scale, k, buf, sizeof buf);
        scale_degree_name(th_scale, k, b2, sizeof b2);
        if (chord_button(r, b2, buf)) {
            int note = 60 + th_root + sc->steps[k];
            preview_notes(&note, 1, 1);
        }
    }
    y += 2 * font_h + 16;
    int len = snprintf(buf, sizeof buf, "Steps ");
    for (int k = 0; k < sc->n && len > 0 && (size_t)len < sizeof buf; k++) {
        int d = (k + 1 < sc->n ? sc->steps[k + 1] : 12) - sc->steps[k];
        len += snprintf(buf + len, sizeof buf - len, " %s", d == 1 ? "H" : d == 2 ? "W" : d == 3 ? "W+H" : "2W");
    }
    text(x, y, buf, TEXT);
    y += font_h + 4;
    key_signature_text(scale_parent_major(th_root, th_scale), flats, buf, sizeof buf);
    text(x, y, buf, TEXT);
    y += font_h + 14;

    if (sc->n == 7) {
        text(x, y + 6, "Chords   click to hear", DIM);
        if (button((SDL_Rect){x + w - 124, y, 60, 26}, "Triads", !th_sevenths)) th_sevenths = 0;
        if (button((SDL_Rect){x + w - 60, y, 60, 26}, "7ths", th_sevenths)) th_sevenths = 1;
        y += 32;
        int cw = (w - 6 * 4) / 7, all = 0;
        for (int d = 0; d < 7; d++) {
            int t;
            char num[24];
            int pc = diatonic_chord(th_root, th_scale, d, th_sevenths, &t, num, sizeof num);
            if (pc < 0) continue;
            diatonic_chord_name(th_root, th_scale, d, t, buf, sizeof buf);
            int notes[6], n = chord_notes(pc, t, 48, notes);
            if (chord_button((SDL_Rect){x + d * (cw + 4), y, cw, 2 * font_h + 12}, num, buf)) {
                if (mods & KMOD_SHIFT) add_to_library(notes, n, buf);
                else preview_notes(notes, n, 2);
            }
            if (plus_button((SDL_Rect){x + d * (cw + 4), y + 2 * font_h + 16, cw, 20})) add_to_library(notes, n, buf);
            if (roll_button((SDL_Rect){x + d * (cw + 4), y + 2 * font_h + 40, cw, 22}))
                roll_put_chord(notes, n, TH_LENS[th_len], buf);
        }
        y += 3 * font_h + 26 + (roll_clip >= 0 ? 26 : 0);
        if (button((SDL_Rect){x, y, w, 26}, "+ Add all 7 chords to the library", 0)) all = 1;
        if (all) {
            int added = 0;
            for (int d = 0; d < 7; d++) {
                int t;
                char num[24];
                int pc = diatonic_chord(th_root, th_scale, d, th_sevenths, &t, num, sizeof num);
                if (pc < 0) continue;
                diatonic_chord_name(th_root, th_scale, d, t, buf, sizeof buf);
                int notes[6], n = chord_notes(pc, t, 48, notes);
                if (chord_sample(notes, n, buf, TH_LENS[th_len]) >= 0) added++;
            }
            lib_badge += added;
            lib_badge_at = SDL_GetTicks();
            status("The %d chords of %s %s are in the library", added, pitch_name(th_root, flats), sc->name);
        }
        y += 38;
    } else {
        text(x, y, "Pentatonic and blues scales: use the chords of", DIM);
        text(x, y + font_h + 2, "the Major or Minor key with the same root.", DIM);
        y += 2 * font_h + 16;
    }

    snprintf(buf, sizeof buf, "Progressions from %s", pitch_name(th_root, flats));
    text(x, y + 6, buf, DIM);
    int sx = x + w - 140;
    (void)sx;
    track_dropdown((SDL_Rect){x + w - 170, y, 170, 26}, "on ", &th_track);
    y += 34;
    for (int i = 0; i < NPROGRESSIONS; i++) {
        const Progression *pr = &PROGRESSIONS[i];
        int pf = prog_flats(pr);
        len = 0;
        buf[0] = 0;
        for (int c = 0; c < pr->n && len >= 0 && (size_t)len < sizeof buf; c++) {
            chord_name((th_root + pr->root[c]) % 12, pr->type[c], pf, b2, sizeof b2);
            len += snprintf(buf + len, sizeof buf - len, c ? "  %s" : "%s", b2);
        }
        int tx = text(x, y, pr->numerals, TEXT);
        text_clip((SDL_Rect){x + tx + 10, y, w - tx - 164, font_h + 2}, x + tx + 10, y, pr->style, DIM);
        text_clip((SDL_Rect){x, y + font_h + 3, w - 154, font_h + 2}, x, y + font_h + 3, buf, HEAD);
        if (plus_button((SDL_Rect){x + w - 148, y + 4, 28, 26})) {   /* its chords, into the library */
            int added = 0;
            for (int c = 0; c < pr->n; c++) {
                int notes[6], n = chord_notes((th_root + pr->root[c]) % 12, pr->type[c], 48, notes);
                char nm[32];
                chord_name((th_root + pr->root[c]) % 12, pr->type[c], pf, nm, sizeof nm);
                int before = P.nsamples;
                if (chord_sample(notes, n, nm, TH_LENS[th_len]) >= before) added++;
            }
            lib_badge += added;
            lib_badge_at = SDL_GetTicks();
            status("%s: %d new chord%s in the library", pr->numerals, added, added == 1 ? "" : "s");
        }
        if (button((SDL_Rect){x + w - 116, y + 4, 52, 26}, "Play", 0)) play_progression(pr);
        if (button((SDL_Rect){x + w - 60, y + 4, 60, 26}, roll_clip >= 0 ? "Roll" : "Place", 0)) {
            if (roll_clip >= 0) {                /* one chord a bar, into the open piano roll */
                int ns[8][6], cnt[8], m = pr->n < 8 ? pr->n : 8;
                for (int c = 0; c < m; c++) cnt[c] = chord_notes((th_root + pr->root[c]) % 12, pr->type[c], 48, ns[c]);
                roll_put_chords((const int (*)[6])ns, cnt, m, 4, pr->numerals);
            } else {
                place_progression(pr);
            }
        }
        y += 2 * font_h + 14;
    }
    if (roll_clip >= 0) {
        text(x, y, "+ adds the chords to the library. Roll puts one per", DIM);
        text(x, y + font_h + 2, "bar into the open piano roll.", DIM);
    } else {
        text(x, y, "+ adds the chords to the library. Place puts one per", DIM);
        text(x, y + font_h + 2, "bar on the timeline, from the bar nearest the playhead.", DIM);
    }
    return y + 2 * font_h + 8;
}

static void disc(int cx, int cy, int r, Col c)
{
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    for (int dy = -r; dy <= r; dy++) {
        int dx = (int)sqrt((double)(r * r - dy * dy));
        SDL_RenderDrawLine(ren, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

static void ring(int cx, int cy, int r, Col c)
{
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    int px = cx + r, py = cy;
    for (int i = 1; i <= 120; i++) {
        double a = i * 2 * PI_D / 120;
        int nx = cx + (int)lround(r * cos(a)), ny = cy + (int)lround(r * sin(a));
        SDL_RenderDrawLine(ren, px, py, nx, ny);
        px = nx;
        py = ny;
    }
}

static int tab_circle(int x, int y, int w)
{
    char buf[256], mj[16], mn[16];
    int R = (w < 440 ? w : 440) / 2 - 6, cx = x + w / 2, cy = y + R + 4;
    ring(cx, cy, R, LINE);
    ring(cx, cy, (int)(R * 0.68), LINE);
    ring(cx, cy, (int)(R * 0.36), LINE);
    int parent = scale_parent_major(th_root, th_scale), flats = key_uses_flats(th_root, th_scale);
    int minor_sel = SCALES[th_scale].minor;
    for (int i = 0; i < 12; i++) {
        double a = (-90 + i * 30) * PI_D / 180;
        int maj = (i * 7) % 12, min = (maj + 9) % 12;
        int f = i >= 7 || (i == 6 && flats);
        snprintf(mj, sizeof mj, "%s", pitch_name(maj, f));
        snprintf(mn, sizeof mn, "%sm", pitch_name(min, f));
        int x1 = cx + (int)(cos(a) * R * 0.84), y1 = cy + (int)(sin(a) * R * 0.84);
        int x2 = cx + (int)(cos(a) * R * 0.52), y2 = cy + (int)(sin(a) * R * 0.52);
        int here = parent == maj;
        SDL_Rect r1 = {x1 - 22, y1 - 14, 44, 28}, r2 = {x2 - 22, y2 - 12, 44, 24};
        int h1 = hit(r1) && drag.kind == D_NONE, h2 = hit(r2) && drag.kind == D_NONE;
        if (here) {
            disc(x1, y1, 19, minor_sel ? PANEL3 : ACCENT);
            disc(x2, y2, 16, minor_sel ? ACCENT : PANEL3);
        } else {
            if (h1) disc(x1, y1, 19, PANEL3);
            if (h2) disc(x2, y2, 16, PANEL3);
        }
        text(x1 - text_w(mj) / 2, y1 - font_h / 2, mj, here && !minor_sel ? DARK : TEXT);
        text(x2 - text_w(mn) / 2, y2 - font_h / 2, mn, here && minor_sel ? DARK : here ? TEXT : DIM);
        int sig = i <= 6 ? i : i - 12;
        if (i == 6 && f) sig = -6;
        snprintf(buf, sizeof buf, sig == 0 ? "0" : sig > 0 ? "%d#" : "%db", sig > 0 ? sig : -sig);
        int x3 = cx + (int)(cos(a) * R * 0.21), y3 = cy + (int)(sin(a) * R * 0.21);
        text(x3 - text_w(buf) / 2, y3 - font_h / 2, buf, DIM);
        if (h1 && lpress) {
            th_root = maj;
            th_scale = 0;
            lpress = 0;
        }
        if (h2 && lpress) {
            th_root = min;
            th_scale = 1;
            lpress = 0;
        }
    }
    y = cy + R + 14;

    int pf = key_uses_flats(parent, 0) || (parent == 6 && flats);
    int rel = (parent + 9) % 12;
    snprintf(buf, sizeof buf, "%s major  /  %sm (relative minor)", pitch_name(parent, pf), pitch_name(rel, pf));
    text(x, y, buf, HEAD);
    y += font_h + 6;
    key_signature_text(parent, flats, buf, sizeof buf);
    text(x, y, buf, TEXT);
    y += font_h + 4;
    char iv_name[8], v_name[8];
    note_above(parent, pf, 3, 5, iv_name, sizeof iv_name);    /* spelled from the key: Gb -> Cb */
    note_above(parent, pf, 4, 7, v_name, sizeof v_name);
    snprintf(buf, sizeof buf, "Neighbours: %s (IV, one flat more)  %s (V, one sharp more)", iv_name, v_name);
    text(x, y, buf, TEXT);
    y += font_h + 10;
    text(x, y, "Clockwise = up a fifth = one more sharp.", DIM);
    text(x, y + font_h + 2, "Neighbouring keys share 6 of 7 notes, so they mix well.", DIM);
    text(x, y + 2 * (font_h + 2), "Outer ring: major keys. Inner: relative minors.", DIM);
    text(x, y + 3 * (font_h + 2), "Click a key to select it.", DIM);
    y += font_h + 2;
    return y + 3 * (font_h + 2) + 6;
}

static int tab_chords(int x, int y, int w)
{
    char buf[128], sym[32];
    int flats = key_uses_flats(th_root, 0);
    snprintf(buf, sizeof buf, "On %s (root set in Keys)   click: hear   +: add", pitch_name(th_root, flats));
    text_clip((SDL_Rect){x, y, w, font_h + 2}, x, y, buf, DIM);
    y += font_h + 10;
    text(x + 6, y, "Chord", DIM);
    text(x + 160, y, "Symbol", DIM);
    text(x + 230, y, "Formula", DIM);
    text(x + 318, y, "Notes", DIM);
    y += font_h + 6;
    for (int t = 0; t < CH_COUNT; t++) {
        const ChordType *c = &CHORDS[t];
        SDL_Rect r = {x, y - 2, w - 30, font_h + 6};
        chord_name(th_root, t, flats, sym, sizeof sym);
        int notes[6], n = chord_notes(th_root, t, 48, notes);
        if (plus_button((SDL_Rect){x + w - 26, y - 2, 26, font_h + 6})) add_to_library(notes, n, sym);
        if (roll_button((SDL_Rect){x + w - 80, y - 2, 50, font_h + 6})) roll_put_chord(notes, n, TH_LENS[th_len], sym);
        if (roll_clip >= 0) r.w = w - 84;
        int clicked = row_hit(r);
        chord_tone_names(th_root, t, flats, buf, sizeof buf);
        text_clip((SDL_Rect){x, y, 156, font_h + 2}, x + 6, y, c->name, TEXT);
        text(x + 160, y, sym, HEAD);
        text(x + 230, y, c->formula, TEXT);
        text_clip((SDL_Rect){x + 318, y, w - 350, font_h + 2}, x + 318, y, buf, TEXT);
        if (clicked) {
            if (mods & KMOD_SHIFT) add_to_library(notes, n, sym);
            else preview_notes(notes, n, 2);
        }
        y += font_h + 7;
    }
    return y + 4;
}

static int tab_intervals(int x, int y, int w)
{
    char buf[64];
    text(x, y, "Click a row to hear it from the key root, + to add it.", DIM);
    y += font_h + 10;
    static const char *head[6] = {"St", "Interval", "", "Ratio", "Cents", "Sound"};
    static const int col[6] = {6, 34, 132, 164, 220, 300};
    for (int i = 0; i < 6; i++) text(x + col[i], y, head[i], DIM);
    y += font_h + 6;
    for (int k = 0; k <= 12; k++) {
        const Interval *iv = &INTERVALS[k];
        SDL_Rect r = {x, y - 2, w - 30, font_h + 6};
        int inotes[2] = {60 + th_root, 60 + th_root + k};
        if (plus_button((SDL_Rect){x + w - 26, y - 2, 26, font_h + 6})) {
            char lb[32], n0[8], n1[8];
            note_name(inotes[0], n0, sizeof n0);
            note_name(inotes[1], n1, sizeof n1);
            snprintf(lb, sizeof lb, k ? "%s+%s %s" : "%s%s %s", n0, k ? n1 : "", iv->abbr);
            add_to_library(inotes, k ? 2 : 1, lb);
        }
        int clicked = row_hit(r);
        double cents = 100.0 * k - 1200.0 * log2((double)iv->num / iv->den);
        snprintf(buf, sizeof buf, "%d", k);
        text(x + col[0], y, buf, DIM);
        text(x + col[1], y, iv->name, TEXT);
        text(x + col[2], y, iv->abbr, HEAD);
        snprintf(buf, sizeof buf, "%d:%d", iv->num, iv->den);
        text(x + col[3], y, buf, TEXT);
        snprintf(buf, sizeof buf, "%+.1f c", fabs(cents) < 0.05 ? 0.0 : cents);
        text(x + col[4], y, buf, fabs(cents) > 10 ? ACCENT : TEXT);
        text_clip((SDL_Rect){x + col[5], y, w - col[5] - 32, font_h + 2}, x + col[5], y, iv->feel, DIM);
        if (clicked) {
            int notes[2] = {60 + th_root, 60 + th_root + k};
            preview_notes(notes, k ? 2 : 1, 2);
        }
        y += font_h + 7;
    }
    y += 6;
    text(x, y, "Ratio: the pure (just intonation) frequency ratio.", DIM);
    text(x, y + font_h + 2, "Cents: how far equal temperament is from it,", DIM);
    text(x, y + 2 * (font_h + 2), "in cents (100 cents = 1 semitone).", DIM);
    text(x, y + 3 * (font_h + 2), "Orange: more than 10 cents off.", DIM);
    return y + 4 * (font_h + 2) + 6;
}

static int tab_tempo(int x, int y, int w)
{
    char buf[160];
    snprintf(buf, sizeof buf, "At %.0f BPM", P.bpm);
    text(x, y, buf, HEAD);
    y += font_h + 10;
    static const char *names[6] = {"1/1", "1/2", "1/4", "1/8", "1/16", "1/32"};
    static const double beats[6] = {4, 2, 1, 0.5, 0.25, 0.125};
    static const int col[5] = {6, 80, 180, 280, 372};
    static const char *head[5] = {"Note", "Straight", "Dotted", "Triplet", "Rate"};
    for (int i = 0; i < 5; i++) text(x + col[i], y, head[i], DIM);
    y += font_h + 6;
    for (int i = 0; i < 6; i++) {
        double ms = beats[i] * 60000.0 / P.bpm;
        text(x + col[0], y, names[i], TEXT);
        snprintf(buf, sizeof buf, "%.1f ms", ms);
        text(x + col[1], y, buf, TEXT);
        snprintf(buf, sizeof buf, "%.1f ms", ms * 1.5);
        text(x + col[2], y, buf, TEXT);
        snprintf(buf, sizeof buf, "%.1f ms", ms * 2 / 3);
        text(x + col[3], y, buf, TEXT);
        snprintf(buf, sizeof buf, "%.2f Hz", 1000.0 / ms);
        text(x + col[4], y, buf, DIM);
        y += font_h + 6;
    }
    y += 4;
    snprintf(buf, sizeof buf, "1 beat = %.1f ms = %ld samples at %g kHz", 60000.0 / P.bpm, beat_frames(), SR / 1000.0);
    text(x, y, buf, TEXT);
    y += font_h + 4;
    snprintf(buf, sizeof buf, "1 bar (4/4) = %.3f s   %.2f bars per minute", 240.0 / P.bpm, P.bpm / 4);
    text(x, y, buf, TEXT);
    y += font_h + 4;
    text(x, y, "Delay times: Straight, Dotted and Triplet columns.", DIM);
    text(x, y + font_h + 2, "LFO sync: Rate column (one cycle per note value).", DIM);
    y += 2 * (font_h + 2) + 12;

    Uint32 now = SDL_GetTicks();
    if (button((SDL_Rect){x, y, 150, 34}, "Tap tempo (T)", 0)) tap_tempo();
    if (ntaps && now - taps[ntaps - 1] > 2000) ntaps = 0;
    snprintf(buf, sizeof buf, ntaps ? "%d taps: keep tapping" : "Tap along on the beat, 4+ times", ntaps);
    text(x + 162, y + (34 - font_h) / 2, buf, DIM);
    y += 48;

    text(x, y, "Tempo markings (rough guide; sources differ)", DIM);
    y += font_h + 8;
    for (int i = 0; i < NTEMPO_MARKS; i++) {
        const TempoMark *m = &TEMPO_MARKS[i];
        int on = P.bpm >= m->lo && P.bpm < m->hi;
        if (on) fill((SDL_Rect){x, y - 2, w, font_h + 5}, tone(ACCENT, 0.4f));
        text(x + 6, y, m->name, on ? TEXT : DIM);
        snprintf(buf, sizeof buf, "%d-%d BPM", m->lo, m->hi);
        text(x + 150, y, buf, on ? TEXT : DIM);
        y += font_h + 6;
    }
    return y + 4;
}

static int tab_notes(int x, int y, int w)
{
    char buf[160];
    snprintf(buf, sizeof buf, "C4 = middle C = MIDI 60.  A4 = %g Hz, equal temperament.", P.a4);
    text(x, y, buf, DIM);
    y += font_h + 4;
    text(x, y, "Click a note to hear it and choose it (also for New sound).", DIM);
    y += font_h + 10;
    int cw = (w - 34) / 9, hover = -1;
    for (int o = 0; o <= 8; o++) {
        snprintf(buf, sizeof buf, "%d", o);
        text(x + 34 + o * cw + (cw - text_w(buf)) / 2, y, buf, DIM);
    }
    y += font_h + 6;
    for (int pc = 0; pc < 12; pc++) {
        text(x + 2, y + 2, pitch_name(pc, 0), DIM);
        for (int o = 0; o <= 8; o++) {
            int midi = 12 * (o + 1) + pc;
            double f = P.a4 * pow(2.0, (midi - 69) / 12.0);
            SDL_Rect r = {x + 34 + o * cw, y, cw - 2, font_h + 4};
            int hov = hit(r) && drag.kind == D_NONE;
            if (midi == G.note) fill(r, tone(ACCENT, 0.5f));
            else if (hov) fill(r, PANEL3);
            if (midi == 69) outline(r, ACCENT);
            snprintf(buf, sizeof buf, f < 100 ? "%.2f" : f < 1000 ? "%.1f" : "%.0f", f);
            text(r.x + (r.w - text_w(buf)) / 2, r.y + 2, buf, pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10 ? DIM : TEXT);
            if (hov) hover = midi;
            if (hov && lpress) {
                lpress = 0;
                if (midi >= 12 && midi <= 108) {
                    G.note = midi;
                    if (G.wave >= WAVE_NOISE) G.wave = WAVE_SINE;
                    preview_notes(&midi, 1, 1);
                } else {
                    status("The generator covers C0 to C8");
                }
            }
        }
        y += font_h + 6;
    }
    y += 6;
    int m = hover >= 0 ? hover : G.note;
    double f = P.a4 * pow(2.0, (m - 69) / 12.0);
    char nn[16];
    note_name(m, nn, sizeof nn);
    snprintf(buf, sizeof buf, "%s   MIDI %d   %.2f Hz", nn, m, f);
    text(x, y, buf, HEAD);
    snprintf(buf, sizeof buf, "period %.3f ms   wavelength in air %.2f m", 1000.0 / f, 343.0 / f);
    text(x, y + font_h + 3, buf, DIM);
    y += 2 * font_h + 12;
    note_name(G.note, nn, sizeof nn);
    snprintf(buf, sizeof buf, "+ Add %s to the library", nn);
    if (button((SDL_Rect){x, y, w, 28}, buf, 0)) {
        int note = G.note;
        add_to_library(&note, 1, nn);
    }
    return y + 40;
}

/* ---------------------------------------------------------------- genre beats */

static int gn_sel, gn_bars = 4, gn_split, gn_init;  /* (declared earlier for the self-test) */
static char gn_pat[32][DRUM_LANES][33];         /* editable copies of the grooves */
static SDL_Rect gn_handle, gn_cell[DRUM_LANES][32];  /* drawn positions (used by --selftest) */

static void genre_init(void)
{
    if (gn_init) return;
    gn_init = 1;
    for (int g = 0; g < NGENRES && g < 32; g++)
        for (int l = 0; l < DRUM_LANES; l++) snprintf(gn_pat[g][l], 33, "%s", GENRES[g].lanes[l]);
}

static double kick_tuned_hz(void)               /* the key's root, moved into 40-80 Hz */
{
    double f = P.a4 * pow(2.0, (th_root - 9) / 12.0);
    while (f >= 80) f /= 2;
    while (f < 40) f *= 2;
    return f;
}

static double genre_bpm(int g) { return P.nclips == 0 ? GENRES[g].bpm : P.bpm; }

static BeatSpec genre_spec(int g)
{
    genre_init();
    const Genre *ge = &GENRES[g];
    BeatSpec b;
    memset(&b, 0, sizeof b);
    for (int l = 0; l < DRUM_LANES; l++) memcpy(b.lanes[l], gn_pat[g][l], 33);
    b.steps = ge->steps;
    b.swing = ge->swing;
    b.swing8 = ge->swing8;
    b.kick_hz = kick_tuned_hz();
    b.kick_decay = ge->kick_decay;
    b.snare_hz = ge->snare_hz;
    b.hat_decay = ge->hat_decay;
    b.open_decay = ge->open_decay;
    b.volume = 0.85;                             /* headroom for kick, snare and hats landing together */
    return b;
}

static int lane_used(const BeatSpec *b, int l)
{
    for (int st = 0; st < b->steps && b->lanes[l][st]; st++)
        if (strchr("xXor", b->lanes[l][st])) return 1;
    return 0;
}

/* Splits the beat into the parts that get their own rows: one, or one per used drum. */
static int beat_parts(const BeatSpec *b, int *masks, const char **labels)
{
    int n = 0, all = 0;
    for (int l = 0; l < DRUM_LANES; l++) {
        if (!lane_used(b, l)) continue;
        all |= 1 << l;
        if (gn_split) {
            masks[n] = 1 << l;
            labels[n++] = DRUM_LANE_NAMES[l];
        }
    }
    if (!gn_split && all) {
        masks[0] = all;
        labels[0] = "beat";
        n = 1;
    }
    return n;
}

static int beat_layout(int g, long *pattern_frames, int *reps)
{
    BeatSpec b = genre_spec(g);
    int masks[DRUM_LANES];
    const char *labels[DRUM_LANES];
    *pattern_frames = (long)llround(b.steps * 60.0 / genre_bpm(g) / 4 * SR);
    *reps = gn_bars * 16 / b.steps > 0 ? gn_bars * 16 / b.steps : 1;
    return beat_parts(&b, masks, labels);
}

static int same_beat(const BeatSpec *a, const BeatSpec *b)
{
    return !memcmp(a->lanes, b->lanes, sizeof a->lanes) && a->steps == b->steps && a->swing == b->swing &&
           a->swing8 == b->swing8 && a->kick_hz == b->kick_hz && a->kick_decay == b->kick_decay &&
           a->snare_hz == b->snare_hz && a->hat_decay == b->hat_decay && a->open_decay == b->open_decay &&
           a->volume == b->volume;
}

static int beat_sample(const BeatSpec *b, int mask, const char *name)
{
    for (int i = 0; i < P.nsamples; i++) {
        const Sample *s = &P.samples[i];
        if (s->is_beat == 1 && !s->removed && s->beat_mask == mask && s->gen_bpm == P.bpm && same_beat(&s->beat, b)) return i;
    }
    return sample_generate_beat(&P, b, mask, name);
}

/* ---- pad presets */

static int hit_sample(const BeatSpec *b, int lane, const char *name)    /* one drum of a kit, reused */
{
    for (int i = 0; i < P.nsamples; i++) {
        const Sample *s = &P.samples[i];
        if (s->is_beat == 2 && !s->removed && s->beat_mask == lane && same_beat(&s->beat, b)) return i;
    }
    return sample_generate_hit(&P, b, lane, name);
}

static void set_pad(int k, int sample, int mode, float vol, int pitch, int track)
{
    if (sample < 0 || k < 0 || k >= NPADS) return;
    LOCK();
    voice_stop(&P, k, -1);
    P.pads[k] = (Pad){sample, mode, vol, pitch, track < P.ntracks ? track : P.ntracks - 1, 0};
    UNLOCK();
}

static void preset_drums(int first, int count)  /* the Beats style's kit, with tuned variations */
{
    BeatSpec b = genre_spec(gn_sel);
    int lane_s[DRUM_LANES];
    for (int l = 0; l < DRUM_LANES; l++) {
        char name[64];
        snprintf(name, sizeof name, "%s %s", GENRES[gn_sel].name, DRUM_LANE_NAMES[l]);
        lane_s[l] = hit_sample(&b, l, name);
    }
    /* lane, pitch, volume: kit pieces first, then toms from the kick, a shaker from the hat... */
    static const int layout[16][3] = {{0, 0, 90}, {1, 0, 80}, {2, 0, 80}, {3, 0, 70}, {4, 0, 70}, {5, 0, 75},
                                      {0, -5, 90}, {1, 0, 35}, {0, 5, 80}, {0, 9, 80}, {0, 12, 80}, {5, -7, 75},
                                      {2, -3, 80}, {3, 7, 55}, {4, -5, 70}, {1, 4, 75}};
    for (int i = 0; i < count; i++)
        set_pad(first + i, lane_s[layout[i][0]], PAD_ONESHOT, layout[i][2] / 100.0f, layout[i][1], 0);
}

static void preset_chords(int first, int count, int track)   /* the chords of the Theory key */
{
    for (int i = 0; i < count; i++) {
        int seventh = i >= 7 && i < 14, deg = i % 7, t;
        char num[24], name[48];
        if (i >= 14) {                                  /* the last two: I up an octave, and IV */
            deg = i == 14 ? 0 : 3;
            seventh = 0;
        }
        int pc = diatonic_chord(th_root, th_scale, deg, seventh, &t, num, sizeof num);
        if (pc < 0) {                                   /* pentatonic and blues: use the major/minor chords */
            pc = diatonic_chord(th_root, SCALES[th_scale].minor ? 1 : 0, deg, seventh, &t, num, sizeof num);
            if (pc < 0) continue;
            diatonic_chord_name(th_root, SCALES[th_scale].minor ? 1 : 0, deg, t, name, sizeof name);
        } else {
            diatonic_chord_name(th_root, th_scale, deg, t, name, sizeof name);
        }
        int notes[6], n = chord_notes(pc, t, i == 14 ? 60 : 48, notes);
        set_pad(first + i, chord_sample(notes, n, name, 4), PAD_HOLD, 0.8f, 0, track);
    }
}

static void preset_notes(int base, int track, const char *tag)   /* 16 notes of the scale, upwards */
{
    const Scale *sc = &SCALES[th_scale];
    for (int i = 0; i < NPADS; i++) {
        int note = base + th_root + 12 * (i / sc->n) + sc->steps[i % sc->n];
        char nn[16], label[32];
        note_name(note, nn, sizeof nn);
        snprintf(label, sizeof label, "%s%s", nn, tag);
        set_pad(i, chord_sample(&note, 1, label, 1), PAD_HOLD, 0.8f, 0, track);
    }
}

static void preset_loops(void)                  /* this style's parts, then other styles' whole beats */
{
    BeatSpec b = genre_spec(gn_sel);
    static const int masks[5] = {63, 1, 2 | 4, 8 | 16, 32};
    static const char *const parts[5] = {"beat", "kick", "snare+clap", "hats", "perc"};
    int k = 0;
    for (int i = 0; i < 5; i++) {
        int used = 0;
        for (int l = 0; l < DRUM_LANES; l++) used |= (masks[i] >> l & 1) && strpbrk(b.lanes[l], "xXor") != NULL;
        if (!used) continue;
        char name[64];
        snprintf(name, sizeof name, "%s %s %.0f", GENRES[gn_sel].name, parts[i], P.bpm);
        set_pad(k++, beat_sample(&b, masks[i], name), PAD_LOOP, 0.8f, 0, 0);
    }
    for (int g = 0; g < NGENRES && k < NPADS; g++) {
        if (g == gn_sel) continue;
        BeatSpec o = genre_spec(g);
        char name[64];
        snprintf(name, sizeof name, "%s beat %.0f", GENRES[g].name, P.bpm);
        set_pad(k++, beat_sample(&o, 63, name), PAD_LOOP, 0.8f, 0, 0);
    }
}

static void load_pad_preset(int k)
{
    switch (k) {
    case 0: preset_drums(0, 16); break;
    case 1:
        preset_drums(0, 8);
        preset_chords(8, 8, 1);
        break;
    case 2: preset_chords(0, 16, 1); break;
    case 3: preset_notes(60, 2, ""); break;
    case 4: preset_notes(36, 3, " bass"); break;
    default: preset_loops(); break;
    }
    pad_sel = 0;
    status("Pads: %s, %s. Ctrl+Z puts the old pads back.", PAD_PRESETS[k],
           k == 0 || k == 5 ? GENRES[gn_sel].name : SCALES[th_scale].name);
}

static int track_empty(int t)
{
    for (int c = 0; c < P.nclips; c++)
        if (P.clips[c].track == t) return 0;
    return 1;
}

/* Places the genre's beat from `start`. first_track < 0 puts it on new rows (or empty ones
 * when all 16 exist); otherwise on consecutive rows from first_track, adding rows as needed. */
static void place_beat(int g, long start, int first_track)
{
    const Genre *ge = &GENRES[g];
    int tempo_set = 0;
    if (P.nclips == 0 && P.bpm != ge->bpm) {    /* empty timeline: take the genre's tempo */
        P.bpm = ge->bpm;
        tempo_set = 1;
    }
    BeatSpec b = genre_spec(g);
    int masks[DRUM_LANES], rows[DRUM_LANES];
    const char *labels[DRUM_LANES];
    int n = beat_parts(&b, masks, labels);
    if (!n) {
        status("The %s pattern is empty: click some steps first", ge->name);
        return;
    }
    for (int i = 0; i < n; i++) {
        char tname[32];
        snprintf(tname, sizeof tname, "%s %s", ge->name, labels[i]);
        int t = -1;
        if (first_track >= 0 && first_track + i < P.ntracks) {
            t = first_track + i;
        } else {
            LOCK();
            t = project_add_track(&P, tname);
            UNLOCK();
            for (int k = 0; t < 0 && k < P.ntracks; k++) {   /* all 16 rows exist: reuse empty ones */
                int taken = 0;
                for (int j = 0; j < i; j++) taken |= rows[j] == k;
                if (!taken && track_empty(k)) t = k;
            }
        }
        if (t < 0) {
            status("No free rows left (%d max): delete clips or drop onto existing rows", MAX_TRACKS);
            n = i;
            break;
        }
        if (track_empty(t)) snprintf(P.tracks[t].name, sizeof P.tracks[t].name, "%s", tname);
        rows[i] = t;
    }
    long pat = 0;
    int reps = gn_bars * 16 / b.steps > 0 ? gn_bars * 16 / b.steps : 1, placed = 0;
    for (int i = 0; i < n; i++) {
        char name[64];
        snprintf(name, sizeof name, "%s %s %.0f", ge->name, labels[i], P.bpm);
        int s = beat_sample(&b, masks[i], name);
        if (s < 0) {
            status("Library is full");
            return;
        }
        pat = P.samples[s].frames;
        for (int r = 0; r < reps; r++)
            if (clip_add(s, rows[i], start + r * pat, 0, pat) >= 0) placed++;
    }
    if (n > 0)
        status("Placed %s at %.0f BPM%s: %d bar%s, %d clips on %d row%s from track %d", ge->name, P.bpm,
               tempo_set ? " (tempo set)" : "", gn_bars, gn_bars == 1 ? "" : "s", placed, n, n == 1 ? "" : "s",
               rows[0] + 1);
}

/* The beat as library sounds (one loop, or one per drum with Row per drum), at the current tempo */
static void add_beat_to_library(int g)
{
    BeatSpec b = genre_spec(g);
    int masks[DRUM_LANES];
    const char *labels[DRUM_LANES];
    int n = beat_parts(&b, masks, labels), added = 0, last = -1;
    for (int i = 0; i < n; i++) {
        char name[64];
        snprintf(name, sizeof name, "%s %s %.0f", GENRES[g].name, labels[i], P.bpm);
        int before = P.nsamples;
        last = beat_sample(&b, masks[i], name);
        if (last < 0) break;
        if (last >= before) added++;
    }
    if (n == 0) status("The %s pattern is empty: click some steps first", GENRES[g].name);
    else if (last < 0) status("Library is full");
    else if (!added) status("That beat is already in the library");
    else {
        lib_badge += added;
        lib_badge_at = SDL_GetTicks();
        status("Added the %s beat to the library as %d loop%s at %.0f BPM", GENRES[g].name, added,
               added == 1 ? "" : "s", P.bpm);
    }
}

static void play_beat(int g)
{
    BeatSpec b = genre_spec(g);
    long len;
    float *one = render_beat(&b, (1 << DRUM_LANES) - 1, genre_bpm(g), &len);
    if (!one) return;
    int reps = b.steps == 16 ? 2 : 1;            /* about two bars */
    float *all = malloc((size_t)len * reps * 2 * sizeof(float));
    if (!all) {
        free(one);
        return;
    }
    for (int r = 0; r < reps; r++) memcpy(all + 2 * r * len, one, (size_t)len * 2 * sizeof(float));
    free(one);
    play_scratch(all, len * reps);
}

/* simple word wrap; returns the y below the text */
static int wrapped(int x, int y, int w, const char *s, Col c)
{
    char line_buf[160];
    int n = 0;
    while (*s) {
        const char *word = s;
        while (*s && *s != ' ') s++;
        int wl = (int)(s - word);
        char trial[160];
        snprintf(trial, sizeof trial, "%.*s%s%.*s", n, line_buf, n ? " " : "", wl, word);
        if (n && text_w(trial) > w) {
            text(x, y, line_buf, c);
            y += font_h + 2;
            n = snprintf(line_buf, sizeof line_buf, "%.*s", wl, word);
        } else {
            n = snprintf(line_buf, sizeof line_buf, "%s", trial);
        }
        while (*s == ' ') s++;
    }
    if (n) {
        text(x, y, line_buf, c);
        y += font_h + 2;
    }
    return y;
}

static int tab_genres(int x, int y, int w)
{
    genre_init();
    char buf[200];
    static const char *gnames[32];
    for (int g = 0; g < NGENRES && g < 32; g++) gnames[g] = GENRES[g].name;
    dropdown((SDL_Rect){x, y, w - 64, 30}, "Style: ", gnames, NGENRES < 32 ? NGENRES : 32, &gn_sel, 0);
    if (button((SDL_Rect){x + w - 60, y, 28, 30}, "<", 0)) gn_sel = (gn_sel + NGENRES - 1) % NGENRES;
    if (button((SDL_Rect){x + w - 28, y, 28, 30}, ">", 0)) gn_sel = (gn_sel + 1) % NGENRES;
    y += 40;

    const Genre *ge = &GENRES[gn_sel];
    text(x, y, ge->name, HEAD);
    y = wrapped(x, y + font_h + 4, w, ge->blurb, DIM) + 8;

    /* settings, each with its own apply button */
    int bx = x + w - 64, row_h = 28;
    snprintf(buf, sizeof buf, "Tempo   %d BPM  (typical %d-%d)", ge->bpm, ge->bpm_lo, ge->bpm_hi);
    text(x, y + 6, buf, TEXT);
    if (button((SDL_Rect){bx, y, 64, 24}, "Set", P.bpm == ge->bpm)) {
        P.bpm = ge->bpm;
        status("Tempo %d BPM (clips keep their times)", ge->bpm);
    }
    y += row_h;
    char key[16];
    scale_note_name(ge->root, ge->scale, 0, key, sizeof key);
    snprintf(buf, sizeof buf, "Key     %s %s", key, SCALES[ge->scale].name);
    text(x, y + 6, buf, TEXT);
    if (button((SDL_Rect){bx, y, 64, 24}, "Use", th_root == ge->root && th_scale == ge->scale)) {
        th_root = ge->root;
        th_scale = ge->scale;
        status("Theory key set to %s %s", key, SCALES[ge->scale].name);
    }
    y += row_h;
    snprintf(buf, sizeof buf, "Tuning  A4 = %g Hz", ge->a4);
    text(x, y + 6, buf, TEXT);
    if (button((SDL_Rect){bx, y, 64, 24}, "Set", P.a4 == ge->a4)) {
        P.a4 = ge->a4;
        status("Tuning A4 = %g Hz for new sounds", ge->a4);
    }
    y += row_h;
    snprintf(buf, sizeof buf, "Kick    tuned to %s: %.1f Hz", pitch_name(th_root, key_uses_flats(th_root, th_scale)),
             kick_tuned_hz());
    text(x, y + 6, buf, DIM);
    y += row_h - 4;
    snprintf(buf, sizeof buf, "Swing   %.0f%% on the off-beat %s", ge->swing * 100, ge->swing8 ? "8ths" : "16ths");
    text(x, y + 6, buf, DIM);
    y += row_h;
    if (button((SDL_Rect){x, y, w, 26}, "Apply tempo, key and tuning", 0)) {
        P.bpm = ge->bpm;
        P.a4 = ge->a4;
        th_root = ge->root;
        th_scale = ge->scale;
        status("%s: %d BPM, %s %s, A4 = %g Hz", ge->name, ge->bpm, key, SCALES[ge->scale].name, ge->a4);
    }
    y += 36;

    /* step grid: click a step to cycle hit -> accent -> ghost (-> roll on hats) -> rest */
    int lab = 46, cell = (w - lab) / ge->steps, ch = 20;
    for (int l = 0; l < DRUM_LANES; l++) {
        text(x, y + (ch - font_h) / 2, DRUM_LANE_NAMES[l], DIM);
        for (int st = 0; st < ge->steps; st++) {
            SDL_Rect r = {x + lab + st * cell, y, cell - 2, ch};
            gn_cell[l][st] = r;
            int hov = hit(r) && drag.kind == D_NONE;
            fill(r, hov ? PANEL3 : (st / 4) % 2 ? PANEL2 : tone(PANEL2, 1.25f));
            char c = gn_pat[gn_sel][l][st];
            Col lc = TRACK_COL[l];
            if (c == 'x') fill((SDL_Rect){r.x + 1, r.y + 2, r.w - 2, r.h - 4}, lc);
            else if (c == 'X') { fill(r, lc); outline(r, TEXT); }
            else if (c == 'o') fill((SDL_Rect){r.x + 2, r.y + r.h / 2, r.w - 4, r.h / 2 - 2}, shade(lc, 0.7f));
            else if (c == 'r')
                for (int k = 0; k < 3; k++) fill((SDL_Rect){r.x + 1 + k * r.w / 3, r.y + 3, r.w / 3 - 1, r.h - 6}, lc);
            if (hov && lpress) {
                lpress = 0;
                const char *cycle = l == 3 ? ".xXor" : ".xXo";
                const char *at = strchr(cycle, c);
                gn_pat[gn_sel][l][st] = at && at[1] ? at[1] : cycle[0];
            }
        }
        y += ch + 3;
    }
    y += 4;
    if (button((SDL_Rect){x, y, 96, 24}, "Reset", 0))
        for (int l = 0; l < DRUM_LANES; l++) snprintf(gn_pat[gn_sel][l], 33, "%s", ge->lanes[l]);
    if (button((SDL_Rect){x + 100, y, 96, 24}, "Clear", 0))
        for (int l = 0; l < DRUM_LANES; l++) memset(gn_pat[gn_sel][l], '.', (size_t)ge->steps);
    text_clip((SDL_Rect){x + 206, y, w - 206, font_h + 8}, x + 206, y + 4, "hit, accent, ghost, roll", DIM);
    y += 34;

    text(x, y + 5, "Bars", DIM);
    static const int bar_opts[4] = {1, 2, 4, 8};
    for (int i = 0; i < 4; i++) {
        snprintf(buf, sizeof buf, "%d", bar_opts[i]);
        if (button((SDL_Rect){x + 48 + i * 40, y, 36, 24}, buf, gn_bars == bar_opts[i])) gn_bars = bar_opts[i];
    }
    if (button((SDL_Rect){x + 220, y, 110, 24}, "One row", !gn_split)) gn_split = 0;
    if (button((SDL_Rect){x + 334, y, w - 334, 24}, "Row per drum", gn_split)) gn_split = 1;
    y += 32;

    snprintf(buf, sizeof buf, "Play (%.0f BPM)", genre_bpm(gn_sel));
    if (button((SDL_Rect){x, y, 140, 30}, buf, 0)) play_beat(gn_sel);
    if (button((SDL_Rect){x + 146, y, 120, 30}, "+ Library", 0)) add_beat_to_library(gn_sel);
    if (button((SDL_Rect){x + 272, y, w - 272, 30}, "Place on new row(s)", 0)) {
        long bar = 4 * beat_frames();
        long start = (long)llround((double)P.playhead / bar) * bar;
        place_beat(gn_sel, start, -1);
    }
    y += 38;
    gn_handle = (SDL_Rect){x, y, w, 34};
    int hov = hit(gn_handle) && drag.kind == D_NONE;
    fill(gn_handle, hov ? tone(ACCENT, 0.55f) : tone(ACCENT, 0.35f));
    outline(gn_handle, ACCENT);
    text_center(gn_handle, ":: Drag this beat onto the timeline ::", TEXT);
    if (hov && lpress && drag.kind == D_NONE) {
        drag = (typeof(drag)){.kind = D_BEAT, .sample = gn_sel, .sx = mx, .sy = my};
        lpress = 0;
    }
    y += 42;
    y = wrapped(x, y, w, "Place starts at the bar nearest the playhead. On an empty timeline the genre's "
                         "tempo is used. Each drum is synthesized; the kick follows the key in Keys.", DIM);
    return y + 4;
}

/* where Theory's main tab `pos` (Beats, Keys, Chords, Reference) is drawn: as subtabs() lays it out */
static SDL_Rect tab_rect(int pos, int x, int y, int w)
{
    int bw = (w - 4 * 3) / 4;
    return (SDL_Rect){x + pos * (bw + 4), y, pos == 3 ? w - 3 * (bw + 4) : bw, 28};
}

static void theory_pane(void)
{
    SDL_Rect pane = {L.pane_x, L.top, L.pane_w, H - L.top - L.status};
    fill(pane, PANEL);
    line(pane.x + pane.w - 1, pane.y, pane.x + pane.w - 1, pane.y + pane.h, LINE);
    int inside = hit(pane), saved_press = lpress;
    if (!inside) lpress = 0;                    /* nothing scrolled out of view takes clicks */
    int max_scroll = th_content_h - pane.h > 0 ? th_content_h - pane.h : 0;
    th_scroll = th_scroll < 0 ? 0 : th_scroll > max_scroll ? max_scroll : th_scroll;

    SDL_RenderSetClipRect(ren, &pane);
    int x = pane.x + 14, w = pane.w - 28, y0 = pane.y + 10 - th_scroll, y = y0;
    static const char *const MAIN[] = {"Beats", "Keys", "Chords", "Reference"};
    static const char *const REF[] = {"Circle", "Intervals", "Tempo", "Notes"};
    static const int REF_ID[] = {1, 3, 4, 5};
    static int ref_tab = 1;
    int main_sel = theory_tab == 6 ? 0 : theory_tab == 0 ? 1 : theory_tab == 2 ? 2 : 3, was = main_sel;
    y = subtabs(x, y, w, MAIN, 4, &main_sel);
    if (main_sel != was) {
        theory_tab = main_sel == 0 ? 6 : main_sel == 1 ? 0 : main_sel == 2 ? 2 : ref_tab;
        th_scroll = 0;
    }
    if (main_sel == 3) {
        int rs = 0;
        for (int i = 0; i < 4; i++)
            if (REF_ID[i] == theory_tab) rs = i;
        y = subtabs(x, y - 6, w, REF, 4, &rs);
        theory_tab = ref_tab = REF_ID[rs];
    }
    if (roll_clip >= 0 && (theory_tab == 0 || theory_tab == 2)) {   /* say where chords can go */
        SDL_Rect b = {x, y, w, font_h + 12};
        fill(b, alpha(ACCENT, 50));
        outline(b, ACCENT);
        text(x + 8, y + 6, "The piano roll is open: Roll puts a chord into it", HEAD);
        y += font_h + 20;
    }
    if (theory_tab == 0 || theory_tab == 2 || theory_tab == 3 || theory_tab == 5) y = adds_as(x, y, w);
    switch (theory_tab) {
    case 0: y = tab_keys(x, y, w); break;
    case 1: y = tab_circle(x, y, w); break;
    case 2: y = tab_chords(x, y, w); break;
    case 3: y = tab_intervals(x, y, w); break;
    case 4: y = tab_tempo(x, y, w); break;
    case 5: y = tab_notes(x, y, w); break;
    case 6: y = tab_genres(x, y, w); break;
    }
    th_content_h = y - y0 + 10;
    (void)max_scroll;
    scrollbar(pane, &th_scroll, th_content_h);
    SDL_RenderSetClipRect(ren, NULL);
    if (!inside) lpress = saved_press;
}

static const char *RATE_LABELS[6] = {"44.1k", "48k", "88.2k", "96k", "176.4k", "192k"};

static void export_dialog(void)
{
    fill((SDL_Rect){0, 0, W, H}, alpha(DARK, 170));
    int w = 700, h = H - 16 < 590 ? H - 16 : 590;    /* rows close up a little in a short window */
    SDL_Rect d = {(W - w) / 2, (H - h) / 2, w, h};
    fill(d, PANEL);
    outline(d, LINE);
    text(d.x + 18, d.y + 14, "Export", TEXT);

    const ExportFormat *f = &EXPORT_FORMATS[EXO.format];
    int rate, bits;
    export_effective(&EXO, &rate, &bits);
    int lx = d.x + 18, ox = d.x + 150, y = d.y + 48, row = h < 590 ? 35 : 40, bh = row - 12;
    char buf[1200];
#define ROW(label) do { text(lx, y + (bh - font_h) / 2, label, DIM); } while (0)

    ROW("Format");
    for (int i = 0; i < EXP_COUNT; i++)
        if (button((SDL_Rect){ox + i * 86, y, 80, bh}, EXPORT_FORMATS[i].name, EXO.format == i)) EXO.format = i;
    y += row;

    ROW("Bit depth");
    static const int depths[3] = {16, 24, 32};
    static const char *depth_names[3] = {"16-bit", "24-bit", "32-bit float"};
    for (int i = 0; i < 3; i++) {
        int ok = !f->lossy && depths[i] <= f->max_bits;
        if (button2((SDL_Rect){ox + i * 120, y, 114, bh}, depth_names[i], ok && bits == depths[i], ok))
            EXO.bits = depths[i];
    }
    if (f->lossy) text(ox + 370, y + (bh - font_h) / 2, "(lossy: set quality)", DIM);
    y += row;

    ROW("Quality");
    if (f->lossy) {
        number((SDL_Rect){ox, y, 234, bh}, "VBR quality", &EXO.quality, 0, 1, 0.004, "%.2f");
        text(ox + 246, y + (bh - font_h) / 2, "0 = smallest, 1 = best", DIM);
    } else {
        text(ox, y + (bh - font_h) / 2, "lossless", DIM);
    }
    y += row;

    ROW("Sample rate");
    snprintf(buf, sizeof buf, "Project %gk", SR / 1000.0);
    int proj_ok = (!f->fixed_rate || f->fixed_rate == SR) && (!f->max_rate || SR <= f->max_rate);
    if (button2((SDL_Rect){ox, y, 112, bh}, buf, EXO.rate == 0 && proj_ok, proj_ok)) EXO.rate = 0;
    for (int i = 0; i < 6; i++) {
        int r = PROJECT_RATES[i];
        int ok = (!f->fixed_rate || f->fixed_rate == r) && (!f->max_rate || r <= f->max_rate);
        if (button2((SDL_Rect){ox + 118 + i * 70, y, 64, bh}, RATE_LABELS[i],
                    ok && rate == r && !(EXO.rate == 0 && proj_ok), ok))
            EXO.rate = r;
    }
    y += row;

    ROW("Channels");
    if (button((SDL_Rect){ox, y, 100, bh}, "Stereo", !EXO.mono)) EXO.mono = 0;
    if (button((SDL_Rect){ox + 106, y, 100, bh}, "Mono", EXO.mono)) EXO.mono = 1;
    y += row;

    ROW("Range");
    int has_loop = P.loop_end > P.loop_start;
    if (button((SDL_Rect){ox, y, 130, bh}, "Whole song", !EXO.loop_only)) EXO.loop_only = 0;
    if (button2((SDL_Rect){ox + 136, y, 130, bh}, "Loop region", EXO.loop_only, has_loop)) EXO.loop_only = 1;
    y += row;

    ROW("Output");
    if (button((SDL_Rect){ox, y, 130, bh}, "Mix", !EXO.stems)) EXO.stems = 0;
    if (button((SDL_Rect){ox + 136, y, 200, bh}, "Stems (file per track)", EXO.stems)) EXO.stems = 1;
    y += row;

    ROW("Normalize");
    static const char *norm_names[3] = {"Off", "-1 dB", "-0.1 dB"};
    for (int i = 0; i < 3; i++)
        if (button2((SDL_Rect){ox + i * 86, y, 80, bh}, norm_names[i], EXO.normalize == i && !EXO.stems, !EXO.stems))
            EXO.normalize = i;
    if (EXO.stems) text(ox + 270, y + (bh - font_h) / 2, "(mixes only: keeps stems in balance)", DIM);
    y += row;

    ROW("Dither");
    int dith_ok = bits == 16;
    if (button2((SDL_Rect){ox, y, 80, bh}, "On", dith_ok && EXO.dither, dith_ok)) EXO.dither = 1;
    if (button2((SDL_Rect){ox + 86, y, 80, bh}, "Off", dith_ok && !EXO.dither, dith_ok)) EXO.dither = 0;
    text(ox + 180, y + (bh - font_h) / 2, dith_ok ? "TPDF, recommended for 16-bit" : "only used for 16-bit", DIM);
    y += row + 6;

    ROW("Folder");
    int picking = SDL_AtomicGet(&fd_state) != 0;
    export_dir_r = (SDL_Rect){ox, y, w - (ox - d.x) - 18 - 116, bh};
    textfield(export_dir_r, export_dir, sizeof export_dir, 1, "choose a folder");
    if (button2((SDL_Rect){d.x + w - 18 - 110, y, 110, bh}, "Choose...", 0, !picking)) {
        tf_end(1);
        export_file_dialog();
    }
    y += row;
    ROW("File name");
    if (tf.buf != export_name) export_take_ext(export_name);    /* "song.mp3" picks MP3 */
    snprintf(buf, sizeof buf, "%s.%s", EXO.stems ? "-trackN" : "", f->ext);
    int ew = text_w(buf);
    export_name_r = (SDL_Rect){ox, y, w - (ox - d.x) - 18 - ew - 12, bh};
    textfield(export_name_r, export_name, sizeof export_name, 0, "untitled");
    text(d.x + w - 18 - ew, y + (bh - font_h) / 2, buf, DIM);
    y += row + 4;

    /* summary */
    long a = EXO.loop_only ? P.loop_start : 0, b = EXO.loop_only ? P.loop_end : project_end(&P);
    char depth[24];
    if (f->lossy) snprintf(depth, sizeof depth, "quality %.2f", EXO.quality);
    else snprintf(depth, sizeof depth, bits == 32 ? "32-bit float" : "%d-bit", bits);
    snprintf(buf, sizeof buf, "%s  %g kHz  %s  %s  %.2f s%s", f->name, rate / 1000.0, depth,
             EXO.mono ? "mono" : "stereo", b > a ? (double)(b - a) / SR : 0.0,
             rate != SR ? "  (resampled, best quality)" : "");
    text(lx, y, buf, HEAD);
    y += font_h + 6;
    export_make_base();
    char full[1300];
    snprintf(full, sizeof full, "%s%s.%s", export_base_path, EXO.stems ? "-track1" : "", f->ext);
    int dir_ok = export_dir[0] && dir_writable(export_dir), name_ok = export_name[0] != 0;
    if (!dir_ok) {
        text(lx, y, "That folder doesn't exist or can't be written to: choose another.", rgb(225, 90, 80));
    } else if (!name_ok) {
        text(lx, y, "Give the file a name.", rgb(225, 90, 80));
    } else {
        const char *home = home_dir();
        const char *shown = full;
        char tilde[1400];
        if (home && home[0] && !strncmp(shown, home, strlen(home))) {
            snprintf(tilde, sizeof tilde, "~%s", shown + strlen(home));
            shown = tilde;
        }
        snprintf(buf, sizeof buf, "Saves as %.1100s", shown);
        text_clip((SDL_Rect){lx, y, w - 36, font_h + 2}, lx, y, buf, DIM);
        if (file_exists(full)) {
            y += font_h + 4;
            text(lx, y, EXO.stems ? "Files with these names are already there: they'll be replaced."
                                  : "A file with this name is already there: it'll be replaced.", ACCENT);
        }
    }

    SDL_Rect cancel = {d.x + w - 236, d.y + h - 46, 100, 32}, go = {d.x + w - 126, d.y + h - 46, 108, 32};
    export_go_r = go;
    if (button(cancel, "Cancel", 0)) {
        tf_end(0);
        export_open = 0;
    }
    if (button2(go, "Export", 1, b > a && dir_ok && name_ok)) {
        tf_end(1);
        export_open = 0;
        export_pending = 1;
    }
#undef ROW
}


/* ---------------------------------------------------------------- the clip editor window
 *
 * Double-clicking an audio clip opens it in a window of its own: a big waveform of the whole
 * sound with the clip's part lit (drag its edges to trim, its top corners to fade, the wheel to
 * zoom, click to hear from there), the clip settings from the Mixer, exact numbers, and actions.
 * It's drawn by the same immediate-mode code as the main window: for its pass the renderer,
 * size and mouse are swapped for the editor's. */


static void editor_close(void)
{
    if (!ewin) return;
    if (drag_in_editor) drag.kind = D_NONE;
    drag_in_editor = 0;
    text_forget(eren);
    SDL_DestroyRenderer(eren);
    SDL_DestroyWindow(ewin);
    ewin = NULL;
    eren = NULL;
    ed_clip = -1;
}

static void editor_view_clip(void)
{
    const Clip *c = &P.clips[ed_clip];
    long frames = P.samples[c->sample].frames, pad = c->length / 6 + SR / 20;
    ed_v0 = c->offset - pad < 0 ? 0 : c->offset - pad;
    ed_v1 = c->offset + c->length + pad > frames ? frames : c->offset + c->length + pad;
    if (ed_v1 <= ed_v0) ed_v1 = ed_v0 + 1;
}

static void editor_open(int c)
{
    if (c < 0 || c >= P.nclips || P.clips[c].pattern > 0) return;
    sel_only(c);
    ed_clip = c;
    ed_last = P.clips[c];
    ed_nclips = P.nclips;
    editor_view_clip();
    char title[160];
    snprintf(title, sizeof title, "%s - clip editor - tinydaw", P.samples[sample_base(&P, P.clips[c].sample)].name);
    if (!ewin) {
        ewin = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 840, 660, SDL_WINDOW_RESIZABLE);
        if (!ewin) {
            status("Couldn't open the editor window: %s", SDL_GetError());
            return;
        }
        eren = SDL_CreateRenderer(ewin, -1, SDL_RENDERER_ACCELERATED);
        if (!eren) eren = SDL_CreateRenderer(ewin, -1, SDL_RENDERER_SOFTWARE);
        if (!eren) {
            SDL_DestroyWindow(ewin);
            ewin = NULL;
            status("Couldn't open the editor window: %s", SDL_GetError());
            return;
        }
        SDL_SetRenderDrawBlendMode(eren, SDL_BLENDMODE_BLEND);
        SDL_SetWindowMinimumSize(ewin, 640, 560);
        ewin_id = SDL_GetWindowID(ewin);
        memset(&ein, 0, sizeof ein);
        ein.mx = ein.my = -1000;
    } else {
        SDL_SetWindowTitle(ewin, title);
        SDL_RaiseWindow(ewin);
    }
    status("Editing \"%s\" in its own window", P.samples[sample_base(&P, P.clips[c].sample)].name);
}

/* the clip the editor shows may have moved in the list (clips deleted, undo): find it again */
static int editor_find(void)
{
    if (ed_clip >= 0 && ed_clip < P.nclips && P.nclips == ed_nclips && P.clips[ed_clip].pattern == 0) return 1;
    for (int i = 0; i < P.nclips; i++)
        if (P.clips[i].pattern == 0 && P.clips[i].start == ed_last.start && P.clips[i].track == ed_last.track &&
            P.clips[i].sample == ed_last.sample) {
            ed_clip = i;
            ed_nclips = P.nclips;
            return 1;
        }
    return 0;
}

static long ed_x_to_frame(int x)
{
    long f = ed_v0 + (long)((double)(x - ed_wave.x) * (ed_v1 - ed_v0) / (ed_wave.w > 1 ? ed_wave.w : 1));
    long frames = P.samples[P.clips[ed_clip].sample].frames;
    return f < 0 ? 0 : f > frames ? frames : f;
}

static int ed_frame_to_x(long f) { return ed_wave.x + (int)((double)(f - ed_v0) * ed_wave.w / (ed_v1 - ed_v0)); }

static void editor_drag(void)            /* a handle on the waveform, following the mouse */
{
    if (ed_clip < 0 || ed_clip >= P.nclips) return;
    Clip *c = &P.clips[ed_clip];
    const Clip *o = &ed_orig;
    long f = ed_x_to_frame(mx), frames = P.samples[c->sample].frames, min = SR / 100;
    LOCK();
    switch (ed_handle) {
    case 1: {                            /* the start: the end stays put */
        long off = f < 0 ? 0 : f > o->offset + o->length - min ? o->offset + o->length - min : f;
        long d = off - o->offset;
        if (o->start + d < 0) d = -o->start;
        c->offset = o->offset + d;
        c->start = o->start + d;
        c->length = o->length - d;
        break;
    }
    case 2: {                            /* the end */
        long len = f - o->offset;
        c->length = len < min ? min : len > frames - o->offset ? frames - o->offset : len;
        break;
    }
    case 3: {
        long v = f - c->offset;
        c->fade_in = v < 0 ? 0 : v > c->length - c->fade_out ? c->length - c->fade_out : v;
        break;
    }
    case 4: {
        long v = c->offset + c->length - f;
        c->fade_out = v < 0 ? 0 : v > c->length - c->fade_in ? c->length - c->fade_in : v;
        break;
    }
    }
    if (c->fade_in + c->fade_out > c->length) c->fade_in = c->fade_out = 0;
    UNLOCK();
}

static void editor_play_region(long from)  /* hear the clip from `from` (a frame in its sound) */
{
    const Clip *c = &P.clips[ed_clip];
    const Sample *s = &P.samples[c->sample];
    if (from < c->offset || from >= c->offset + c->length) from = c->offset;
    long n = c->offset + c->length - from;
    float *b = malloc((size_t)n * 2 * sizeof(float));
    if (!b) return;
    float g = powf(10, c->gain_db / 20);
    for (long i = 0; i < n; i++) {
        long k = from - c->offset + i;          /* where in the clip, for its fades */
        float e = 1;
        if (c->fade_in > 0 && k < c->fade_in) e = (float)k / c->fade_in;
        if (c->fade_out > 0 && c->length - k < c->fade_out) e = fminf(e, (float)(c->length - k) / c->fade_out);
        b[2 * i] = s->data[2 * (from + i)] * g * e;
        b[2 * i + 1] = s->data[2 * (from + i) + 1] * g * e;
    }
    play_scratch(b, n);
}

static void editor_draw(void)
{
    char buf[200];
    Clip *c = &P.clips[ed_clip];
    const Sample *s = &P.samples[c->sample];
    int x = 16, w = W - 32, y = 12, hw, tw;
    fill((SDL_Rect){0, 0, W, H}, PANEL);
    snprintf(buf, sizeof buf, "%s", P.samples[sample_base(&P, c->sample)].name);
    text(x, y + 4, buf, HEAD);
    snprintf(buf, sizeof buf, "track %d (%s)   %.2f s of a %.2f s sound", c->track + 1, P.tracks[c->track].name,
             (double)c->length / SR, (double)s->frames / SR);
    text(x + text_w(P.samples[sample_base(&P, c->sample)].name) + 16, y + 4, buf, DIM);
    if (button((SDL_Rect){W - 16 - 90, y, 90, 28}, "Close", 0)) {
        editor_close();
        return;
    }
    y += 40;

    /* the waveform: the whole sound, the clip's part lit */
    int wh = H - y - 350;
    ed_wave = (SDL_Rect){x, y, w, wh < 120 ? 120 : wh};
    fill(ed_wave, PANEL2);
    int mid = ed_wave.y + ed_wave.h / 2, half = ed_wave.h / 2 - 6;
    for (int px = 0; px < ed_wave.w; px++) {
        long f0 = ed_v0 + (long)((double)px * (ed_v1 - ed_v0) / ed_wave.w);
        long f1 = ed_v0 + (long)((double)(px + 1) * (ed_v1 - ed_v0) / ed_wave.w);
        if (f1 <= f0) f1 = f0 + 1;
        float lo, hi;
        sample_minmax(s, f0, f1, &lo, &hi);
        int in = f0 >= c->offset && f0 < c->offset + c->length;
        float e = 1;                              /* the fades, drawn into the shape */
        long k = f0 - c->offset;
        if (in && c->fade_in > 0 && k < c->fade_in) e = (float)k / c->fade_in;
        if (in && c->fade_out > 0 && c->length - k < c->fade_out) e = fminf(e, (float)(c->length - k) / c->fade_out);
        float g = in ? powf(10, c->gain_db / 20) * e : 1;
        int y0 = mid - (int)(fminf(hi * g, 1) * half), y1 = mid - (int)(fmaxf(lo * g, -1) * half);
        line(ed_wave.x + px, y0, ed_wave.x + px, y1 < y0 + 1 ? y0 + 1 : y1, in ? TRACK_COL[c->track % MAX_TRACKS] : tone(DIM, 0.6f));
    }
    int rx0 = ed_frame_to_x(c->offset), rx1 = ed_frame_to_x(c->offset + c->length);
    int fx0 = ed_frame_to_x(c->offset + c->fade_in), fx1 = ed_frame_to_x(c->offset + c->length - c->fade_out);
    line(rx0, ed_wave.y + 4, fx0, ed_wave.y + 4, ACCENT);
    line(fx1, ed_wave.y + 4, rx1, ed_wave.y + 4, ACCENT);
    SDL_Rect hs = {rx0 - 3, ed_wave.y, 6, ed_wave.h}, he = {rx1 - 3, ed_wave.y, 6, ed_wave.h};
    SDL_Rect hfi = {fx0 - 6, ed_wave.y - 6, 12, 12}, hfo = {fx1 - 6, ed_wave.y - 6, 12, 12};
    fill(hs, ed_handle == 1 && drag.kind == D_ED ? HEAD : ACCENT);
    fill(he, ed_handle == 2 && drag.kind == D_ED ? HEAD : ACCENT);
    fill(hfi, ACCENT);
    fill(hfo, ACCENT);
    outline(hfi, DARK);
    outline(hfo, DARK);
    if (P.playing && P.playhead >= c->start && P.playhead < c->start + c->length) {   /* where the song is */
        int px = ed_frame_to_x(c->offset + P.playhead - c->start);
        line(px, ed_wave.y, px, ed_wave.y + ed_wave.h, HEAD);
    }
    outline(ed_wave, LINE);
    int ry = ed_wave.y + ed_wave.h + 4;           /* seconds into the sound */
    double span = (double)(ed_v1 - ed_v0) / SR, step = span > 20 ? 5 : span > 8 ? 2 : span > 3 ? 0.5 : span > 1 ? 0.25 : 0.05;
    for (double t = ceil(ed_v0 / (double)SR / step) * step; t * SR < ed_v1; t += step) {
        int px = ed_frame_to_x((long)(t * SR));
        line(px, ry, px, ry + 4, DIM);
        snprintf(buf, sizeof buf, "%.2fs", t);
        text(px + 2, ry + 2, buf, DIM);
    }
    if (drag.kind == D_NONE) {                    /* the waveform's mouse */
        int which = hit(hfi) ? 3 : hit(hfo) ? 4 : hit((SDL_Rect){hs.x - 4, hs.y, hs.w + 8, hs.h}) ? 1
                  : hit((SDL_Rect){he.x - 4, he.y, he.w + 8, he.h}) ? 2 : 0;
        if (lpress && which) {
            lpress = 0;
            ed_handle = which;
            ed_orig = *c;
            drag = (typeof(drag)){.kind = D_ED, .clip = ed_clip, .sx = mx, .sy = my};
        } else if (lpress && hit(ed_wave)) {
            lpress = 0;
            editor_play_region(ed_x_to_frame(mx));
        }
        if (hit(ed_wave) && wheel_y) {           /* zoom about the mouse; Shift+wheel scrolls */
            long frames = s->frames, span_f = ed_v1 - ed_v0, at = ed_x_to_frame(mx);
            if (mods & KMOD_SHIFT) {
                long d = -wheel_y * span_f / 8;
                ed_v0 += d;
                ed_v1 += d;
            } else {
                double k = wheel_y > 0 ? 0.8 : 1.25;
                long nspan = (long)(span_f * k);
                if (nspan < SR / 50) nspan = SR / 50;
                ed_v0 = at - (long)((double)(at - ed_v0) * nspan / span_f);
                ed_v1 = ed_v0 + nspan;
            }
            if (ed_v0 < 0) ed_v1 -= ed_v0, ed_v0 = 0;
            if (ed_v1 > frames) ed_v0 -= ed_v1 - frames, ed_v1 = frames;
            if (ed_v0 < 0) ed_v0 = 0;
            if (ed_v1 <= ed_v0) ed_v1 = ed_v0 + 1;
            wheel_y = 0;
        }
    }
    y = ry + font_h + 12;

    /* the numbers */
    hw = (w - 16) / 3;
    tw = hw;
    y = section(x, y, w, "WHERE IT PLAYS");
    double bf = beat_frames();
    static double st_b, len_b, off_ms;
    if (!(drag.kind == D_NUM && drag.num == &st_b) && !(numedit.active && numedit.v == &st_b)) st_b = c->start / bf + 1;
    if (!(drag.kind == D_NUM && drag.num == &len_b) && !(numedit.active && numedit.v == &len_b)) len_b = c->length / bf;
    if (!(drag.kind == D_NUM && drag.num == &off_ms) && !(numedit.active && numedit.v == &off_ms)) off_ms = c->offset * 1000.0 / SR;
    number((SDL_Rect){x, y, tw, 26}, "Starts at beat", &st_b, 1, 100000, 0.05, "%.2f");
    number((SDL_Rect){x + tw + 8, y, tw, 26}, "Length", &len_b, 0.01, (s->frames - c->offset) / bf, 0.02, "%.2f beats");
    number((SDL_Rect){x + 2 * (tw + 8), y, tw, 26}, "Into the sound", &off_ms, 0, s->frames * 1000.0 / SR, 2, "%.0f ms");
    {
        long ns = (long)llround((st_b - 1) * bf), nl = (long)llround(len_b * bf), no = (long)llround(off_ms * SR / 1000);
        if (no > s->frames - SR / 100) no = s->frames - SR / 100;
        if (no < 0) no = 0;
        if (nl > s->frames - no) nl = s->frames - no;
        if (ns != c->start || nl != c->length || no != c->offset) {
            LOCK();
            c->start = ns < 0 ? 0 : ns;
            c->length = nl < SR / 100 ? SR / 100 : nl;
            c->offset = no;
            if (c->fade_in + c->fade_out > c->length) c->fade_in = c->fade_out = 0;
            UNLOCK();
        }
    }
    y += 34;
    snprintf(buf, sizeof buf, "Track %d: %s", c->track + 1, P.tracks[c->track].name);
    text(x, y + 6, buf, TEXT);
    if (button((SDL_Rect){x + tw - 60, y, 28, 26}, "<", 0) && c->track > 0) c->track--;
    if (button((SDL_Rect){x + tw - 28, y, 28, 26}, ">", 0) && c->track < P.ntracks - 1) c->track++;
    y += 40;

    y = section(x, y, w, "HOW IT SOUNDS");
    fnumber((SDL_Rect){x, y, tw, 26}, "Gain", &c->gain_db, -36, 12, 0.05, "%+.1f dB");
    static double semis;
    if (!(drag.kind == D_NUM && drag.num == &semis) && !(numedit.active && numedit.v == &semis)) semis = clip_semis(c);
    number((SDL_Rect){x + tw + 8, y, tw, 26}, "Pitch", &semis, -24, 24, 0.08, "%+.0f st");
    if (drag.kind != D_NUM && (int)lround(semis) != clip_semis(c)) clip_variant(ed_clip, (int)lround(semis), clip_rev(c));
    c = &P.clips[ed_clip];
    if (button((SDL_Rect){x + 2 * (tw + 8), y, tw, 26}, "Reverse", clip_rev(c))) clip_variant(ed_clip, clip_semis(c), !clip_rev(c));
    c = &P.clips[ed_clip];
    y += 34;
    static double fin, fout;
    if (!(drag.kind == D_NUM && drag.num == &fin) && !(numedit.active && numedit.v == &fin)) fin = c->fade_in * 1000.0 / SR;
    if (!(drag.kind == D_NUM && drag.num == &fout) && !(numedit.active && numedit.v == &fout)) fout = c->fade_out * 1000.0 / SR;
    double maxms = c->length * 1000.0 / SR;
    number((SDL_Rect){x, y, tw, 26}, "Fade in", &fin, 0, maxms, 2, "%.0f ms");
    number((SDL_Rect){x + tw + 8, y, tw, 26}, "Fade out", &fout, 0, maxms, 2, "%.0f ms");
    {
        long a = (long)llround(fin * SR / 1000), b = (long)llround(fout * SR / 1000);
        if (a > c->length) a = c->length;
        if (a + b > c->length) b = c->length - a;
        if (a != c->fade_in || b != c->fade_out) {
            LOCK();
            c->fade_in = a;
            c->fade_out = b < 0 ? 0 : b;
            UNLOCK();
        }
    }
    y += 42;

    /* actions */
    static const char *const ACT[6] = {"Play the clip", "Normalize (peak -1 dB)", "Fit to whole bars",
                                       "Split at the playhead", "New sound with its effects", "Delete the clip"};
    int bw = (w - 2 * 6) / 3, act = -1;
    for (int i = 0; i < 6; i++) {
        ed_btn[i] = (SDL_Rect){x + (i % 3) * (bw + 6), y + (i / 3) * 36, i % 3 == 2 ? w - 2 * (bw + 6) : bw, 30};
        if (button(ed_btn[i], ACT[i], 0)) act = i;
    }
    y += 36;
    switch (act) {
    case 0: editor_play_region(c->offset); break;
    case 1: {                                    /* the loudest moment to -1 dB */
        float lo, hi;
        sample_minmax(s, c->offset, c->offset + c->length, &lo, &hi);
        float pk = fmaxf(fabsf(lo), fabsf(hi));
        if (pk > 1e-6f) {
            LOCK();
            c->gain_db = fmaxf(-36, fminf(12, -1 - 20 * log10f(pk)));
            UNLOCK();
            status("Normalized: gain %+.1f dB brings its peak to -1 dB", c->gain_db);
        }
        break;
    }
    case 2: {
        long bar = 4 * beat_frames(), n = (c->length + bar / 2) / bar;
        if (n < 1) n = 1;
        long len = n * bar;
        if (len > s->frames - c->offset) len = s->frames - c->offset;
        LOCK();
        c->length = len;
        if (c->fade_in + c->fade_out > c->length) c->fade_in = c->fade_out = 0;
        UNLOCK();
        status("Length: %ld bar%s%s", n, n == 1 ? "" : "s", len < n * bar ? " (as much as the sound has)" : "");
        break;
    }
    case 3:
        if (P.playhead > c->start && P.playhead < c->start + c->length) {
            long cut = P.playhead - c->start;
            Clip right = *c;
            right.start = P.playhead;
            right.offset = c->offset + cut;
            right.length = c->length - cut;
            right.fade_in = 0;
            if (right.fade_out > right.length) right.fade_out = 0;
            LOCK();
            c->length = cut;
            c->fade_out = 0;
            if (c->fade_in > c->length) c->fade_in = 0;
            UNLOCK();
            int k = clip_add(right.sample, right.track, right.start, right.offset, right.length);
            if (k >= 0) {
                LOCK();
                P.clips[k].gain_db = right.gain_db;
                P.clips[k].fade_out = right.fade_out;
                UNLOCK();
            }
            ed_nclips = P.nclips;
            status("Split at the playhead: this is the left part; the right part is its own clip");
        } else {
            status("Put the playhead inside this clip to split it there");
        }
        break;
    case 4: {
        int one = ed_clip;
        bounce_clips(&one, 1);
        break;
    }
    case 5:
        clip_delete(ed_clip);
        editor_close();
        status("Deleted the clip");
        return;
    }
    y += 40;
    text(x, y, "Drag the edges to trim, the squares on top to fade, the wheel to zoom (Shift to scroll).", DIM);
    text(x, y + font_h + 3, "Click the waveform to hear it from there. Ctrl+Z undoes, Esc closes.", DIM);
}

/* The editor's pass: its renderer, size and mouse in place of the main window's. */
static void editor_frame(void)
{
    if (!ewin) return;
    if (!editor_find()) {
        editor_close();
        return;
    }
    SDL_Renderer *m_ren = ren;
    int mW = W, mH = H, smx = mx, smy = my, sld = ldown, slp = lpress, slr = lrel, srp = rpress, swx = wheel_x,
        swy = wheel_y, sdb = ldouble;
    ren = eren;
    SDL_GetWindowSize(ewin, &W, &H);
    mx = ein.mx;
    my = ein.my;
    ldown = ein.ldown;
    lpress = ein.lpress;
    lrel = ein.lrel;
    rpress = ein.rpress;
    wheel_x = ein.wheel_x;
    wheel_y = ein.wheel_y;
    ldouble = ein.ldouble;
    int had = drag.kind != D_NONE;
    if (numedit.active && (lpress || rpress) && !hit(numedit.r)) numedit_end(1);
    SDL_SetRenderDrawColor(ren, BG.r, BG.g, BG.b, 255);
    SDL_RenderClear(ren);
    editor_draw();
    if (ewin) {
        if (drag_in_editor && drag.kind != D_NONE && ldown) update_drag();
        if (drag_in_editor && lrel && drag.kind != D_NONE) end_drag();
        if (!had && drag.kind != D_NONE) drag_in_editor = 1;
        if (drag.kind == D_NONE) drag_in_editor = 0;
        if (ed_clip >= 0 && ed_clip < P.nclips) {
            ed_last = P.clips[ed_clip];
            ed_nclips = P.nclips;
        }
        SDL_RenderPresent(ren);
    }
    ren = m_ren;
    W = mW;
    H = mH;
    mx = smx;
    my = smy;
    ldown = sld;
    lpress = slp;
    lrel = slr;
    rpress = srp;
    wheel_x = swx;
    wheel_y = swy;
    ldouble = sdb;
    ein.lpress = ein.lrel = ein.rpress = ein.wheel_x = ein.wheel_y = 0;
}

static void frame(void)
{
    static int min_for = -1;            /* the pane needs a wider window */
    int need = 70 + (theory_open || pads_open || mixer_open || vis_open || sounds_open ? PANE_W : settings_open ? 440 : 0) + 520;
    if (min_for != need) {              /* the panel plus a usable timeline */
        min_for = need;
        SDL_SetWindowMinimumSize(win, need, 560);
        int ww, wh;
        SDL_GetWindowSize(win, &ww, &wh);
        if (ww < need) SDL_SetWindowSize(win, need, wh);
    }
    theme_poll();
    layout();
    if (P.playing) {                    /* keep the playhead in view */
        double x = frame_to_x(P.playhead);
        if (x > L.tl_x + L.tl_w - 30 || x < L.tl_x) view_start = (double)P.playhead / SR - 20 / pps;
    }
    SDL_SetRenderDrawColor(ren, BG.r, BG.g, BG.b, 255);
    SDL_RenderClear(ren);
    vis_update();
    tempo_check();
    dd_input();
    if (numedit.active && (lpress || rpress) && !hit(numedit.r)) numedit_end(1);
    if (tf.buf && (((lpress || rpress) && !hit(tf.r)) || !tf_drawn)) tf_end(1);   /* clicked away, or it's gone */
    tf_drawn = 0;

    /* while the export dialog is open, the rest of the window gets no input */
    int smx = mx, smy = my, slp = lpress, srp = rpress, swx = wheel_x, swy = wheel_y;
    int picking = SDL_AtomicGet(&fd_state) != 0;
    int modal = export_open || export_pending || menu_open || met_open || confirm_act || picking || cm.open;
    if (modal) {
        mx = my = -10000;
        lpress = rpress = wheel_x = wheel_y = 0;
    }
    if (show_open) {                    /* the show covers everything */
        lpress = rpress = wheel_x = wheel_y = 0;
        show_draw();
    } else {
        top_bar();
        roll_check();
        if (roll_clip >= 0) {
            piano_roll();
        } else {
            track_headers();
            timeline();
        }
        rail();
        if (theory_open) theory_pane();
        else if (pads_open) pads_pane();
        else if (mixer_open) mixer_pane();
        else if (vis_open) vis_pane();
        else if (settings_open) settings_pane();
        else if (sounds_open) sounds_pane();
        if (wheel_y && drag.kind == D_NONE && L.pane_w && hit(panel_rect())) {
            /* a wheel that no box or list in the panel took scrolls the panel (the panel
             * takes it last, so number and note boxes can be stepped with the wheel) */
            int *sc = theory_open ? &th_scroll : pads_open ? &pads_scroll : mixer_open ? &mixer_scroll
                    : vis_open ? &vis_scroll : settings_open ? &settings_scroll : sounds_open && sounds_sub ? &sounds_scroll : NULL;
            if (sc) *sc -= wheel_y * 48;           /* clamped when the pane is next drawn */
            wheel_y = 0;
        }
        if (drag.kind == D_MOVE && over_library()) {   /* say what a drop here does */
            SDL_Rect pr = panel_rect();
            fill(pr, alpha(ACCENT, 60));
            outline(pr, ACCENT);
            SDL_Rect b = {pr.x + 12, pr.y + pr.h / 2 - 30, pr.w - 24, 2 * font_h + 24};
            fill(b, PANEL);
            outline(b, ACCENT);
            text_center((SDL_Rect){b.x, b.y + 6, b.w, font_h + 4}, sel_count() > 1 && csel[drag.clip]
                        ? "Drop: one new sound of the selected clips" : "Drop: a new sound of this clip", HEAD);
            text_center((SDL_Rect){b.x, b.y + 12 + font_h, b.w, font_h + 4}, "with its track's effects", TEXT);
        }
        status_bar();
    }
    if (modal) {
        mx = smx;
        my = smy;
        lpress = slp;
        rpress = srp;
        wheel_x = swx;
        wheel_y = swy;
    }
    if (export_open) {
        if (picking) lpress = rpress = 0;  /* the system file dialog has the mouse */
        export_dialog();
    }
    if (cm.open && !picking) cm_draw();
    if (menu_open && !picking) file_menu();
    if (met_open && !picking) metronome_menu();
    dd_draw();
    if (confirm_act && !picking) confirm_dialog();
    if (picking) {
        lpress = rpress = 0;
        waiting_overlay("Choose a file in the file dialog...");
    }

    if (drag.kind != D_NONE && ldown && !drag_in_editor) update_drag();   /* the editor runs its own */
    if (lrel && drag.kind != D_NONE && !drag_in_editor) end_drag();
    if (lrel) roll_quiet();
    if (lrel && mouse_pad >= 0) {       /* a clicked pad is let go */
        input_event(SRC_MOUSE, mouse_pad, 0, 0, -1);
        mouse_pad = -1;
    }
    if (drag.kind == D_LIB || drag.kind == D_BEAT) {   /* label following the pointer */
        char beat_label[64];
        snprintf(beat_label, sizeof beat_label, "%s beat", GENRES[drag.sample].name);
        const char *name = drag.kind == D_LIB ? P.samples[drag.sample].name : beat_label;
        SDL_Rect r = {mx + 14, my + 10, text_w(name) + 12, font_h + 6};
        fill(r, alpha(PANEL3, 230));
        text(r.x + 6, r.y + 3, name, TEXT);
    }

    /* show "Exporting" for one frame, then do the (blocking) export on the next */
    if (export_pending == 1) {
        fill((SDL_Rect){0, 0, W, H}, alpha(DARK, 150));
        SDL_Rect r = {(W - 260) / 2, (H - 60) / 2, 260, 60};
        fill(r, PANEL);
        outline(r, ACCENT);
        text_center(r, "Exporting...", TEXT);
        export_pending = 2;
    } else if (export_pending == 2) {
        export_pending = 0;
        do_export_now();
    }

    if (dd.changed_for == &example_pick) {   /* an example was picked from the list */
        dd.changed_for = NULL;
        if (example_pick >= 0 && example_pick < nexamples) request(ACT_OPEN_EXAMPLE, example_path[example_pick]);
    }
    take_check();
    rec_drain();
    if (drag.kind == D_NONE && !(pads_rec && P.playing) && !tempo_from)   /* a take, or a tempo change */
        history_check();                                                    /* with its clips: one step */
    file_dialog_poll();
    if (learned >= 0) {
        status("Pad %d: input learned", learned + 1);
        learned = -1;
    }
    if (!headless) {
        if (binds_changed) binds_save();
        update_title();
        autosave_tick();
    }
    binds_changed = 0;
}

int main(int argc, char **argv)
{
    theme_apply(0);
#ifdef _WIN32
    /* a windowed program has no console; borrow the one it was started from, so --help,
     * --selftest and --export can print */
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    int redirected = out && out != INVALID_HANDLE_VALUE && GetFileType(out) != FILE_TYPE_UNKNOWN;
    if (!redirected && AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
#endif
    const char *export_path = NULL, *shot = NULL, *save_path = NULL;
    int demo = 0, test = 0, format_given = 0;
    const char *beat_arg = NULL;
    project_init(&P);
    export_defaults(&EXO);

    static const char *usage =
        "usage: tinydaw [options] [project.tdaw] [audio files...]\n"
        "  --demo                 start with a two-bar demo beat\n"
        "  --beat GENRE           add a 4-bar beat (house, trap, rock, ... or a number)\n"
        "  --project-rate HZ      engine rate: 44100 48000 88200 96000 176400 192000\n"
        "  --version              print the version\n"
        "  --save FILE.tdaw       save the project (after --demo, --beat, files) and exit\n"
        "  --export FILE          render without a window (format from the extension)\n"
        "    --format F           wav aiff flac ogg opus mp3\n"
        "    --bits N             16, 24 or 32 (float)\n"
        "    --rate HZ            export sample rate (default: project rate)\n"
        "    --quality Q          lossy quality 0..1\n"
        "    --mono --stems --loop --normalize --no-dither\n";
    /* options that take a value */
    static const char *with_value[] = {"--save", "--export", "--screenshot", "--format", "--bits", "--rate",
                                       "--quality", "--project-rate", "--beat"};
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        int takes = 0;
        for (size_t k = 0; k < sizeof with_value / sizeof *with_value; k++) takes |= !strcmp(a, with_value[k]);
        if (takes && !v) {
            fprintf(stderr, "tinydaw: %s needs a value\n%s", a, usage);
            return 2;
        }
        if (!strcmp(a, "--demo")) demo = 1;
        else if (!strcmp(a, "--selftest")) test = 1;
        else if (!strcmp(a, "--beat")) beat_arg = v;
        else if (!strcmp(a, "--export")) export_path = v;
        else if (!strcmp(a, "--save")) save_path = v;
        else if (!strcmp(a, "--screenshot")) shot = v;
        else if (!strcmp(a, "--bits")) EXO.bits = atoi(v);
        else if (!strcmp(a, "--rate")) EXO.rate = atoi(v);
        else if (!strcmp(a, "--quality")) EXO.quality = atof(v);
        else if (!strcmp(a, "--mono")) EXO.mono = 1;
        else if (!strcmp(a, "--stems")) EXO.stems = 1;
        else if (!strcmp(a, "--loop")) EXO.loop_only = 1;
        else if (!strcmp(a, "--normalize")) EXO.normalize = 1;
        else if (!strcmp(a, "--no-dither")) EXO.dither = 0;
        else if (!strcmp(a, "--project-rate")) {
            int r = atoi(v);
            if (r < 8000 || r > 384000) {
                fprintf(stderr, "tinydaw: bad project rate %s\n", v);
                return 2;
            }
            g_rate = r;
        } else if (!strcmp(a, "--format")) {
            int found = -1;
            for (int k = 0; k < EXP_COUNT; k++)
                if (!strcasecmp(v, EXPORT_FORMATS[k].ext) || !strcasecmp(v, EXPORT_FORMATS[k].name)) found = k;
            if (found < 0) {
                fprintf(stderr, "tinydaw: unknown format %s\n", v);
                return 2;
            }
            EXO.format = found;
            format_given = 1;
        } else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            printf("tinydaw %s\n%s", TINYDAW_VERSION, usage);
            return 0;
        } else if (!strcmp(a, "--version")) {
            printf("tinydaw %s\n", TINYDAW_VERSION);
            return 0;
        } else if (a[0] == '-') {
            fprintf(stderr, "tinydaw: unknown option %s\n%s", a, usage);
            return 2;
        }
        if (takes) i++;
    }
    if (EXO.bits != 16 && EXO.bits != 24 && EXO.bits != 32) {
        fprintf(stderr, "tinydaw: --bits must be 16, 24 or 32\n");
        return 2;
    }
    if (test || shot) headless = 1;
    int loaded = 0;
    for (int i = 1; i < argc && !loaded; i++) {   /* a project file opens first */
        size_t n = strlen(argv[i]);
        if (n > 5 && !strcmp(argv[i] + n - 5, ".tdaw") && (i == 1 || argv[i - 1][0] != '-' ||
                                                           !strcmp(argv[i - 1], "--demo"))) {
            if (open_project(argv[i], 0) != 0) {
                fprintf(stderr, "tinydaw: %s\n", status_msg);
                return 1;
            }
            loaded = 1;
        }
    }
    if (demo) make_demo();
    if (beat_arg) {                     /* genre by name (case-insensitive prefix) or number */
        int g = -1;
        for (int k = 0; k < NGENRES && g < 0; k++)
            if (!strncasecmp(beat_arg, GENRES[k].name, strlen(beat_arg))) g = k;
        if (g < 0 && beat_arg[0] >= '0' && beat_arg[0] <= '9' && atoi(beat_arg) < NGENRES) g = atoi(beat_arg);
        if (g < 0) {
            fprintf(stderr, "tinydaw: unknown genre %s\n", beat_arg);
            return 2;
        }
        th_root = GENRES[g].root;           /* the genre's key, tuning and tempo */
        th_scale = GENRES[g].scale;
        P.a4 = GENRES[g].a4;
        gn_sel = g;
        place_beat(g, 0, -1);
        if (export_path) fprintf(stderr, "%s\n", status_msg);
    }
    for (int i = 1; i < argc; i++) {
        int takes = 0;
        for (size_t k = 0; k < sizeof with_value / sizeof *with_value; k++) takes |= !strcmp(argv[i], with_value[k]);
        if (takes) {
            i++;
            continue;
        }
        if (argv[i][0] == '-') continue;
        size_t len = strlen(argv[i]);
        if (len > 5 && !strcmp(argv[i] + len - 5, ".tdaw")) continue;
        import_file(argv[i]);
        if (export_path || shot || test) fprintf(stderr, "%s\n", status_msg);
    }

    history_reset();
    if (!loaded) mark_saved();
    if (save_path) {
        char msg[1200];
        int rc = project_save(&P, save_path, msg, sizeof msg);
        fprintf(rc ? stderr : stdout, "%s\n", msg);
        if (rc || !export_path) return rc ? 1 : 0;
    }
    if (export_path) {                  /* headless render, no window */
        char base[1024], msg[1500];
        snprintf(base, sizeof base, "%s", export_path);
        const char *after = path_base(base);
        char *dot = strrchr(base, '.'), *slash = after > base ? (char *)after - 1 : NULL;
        if (dot && (!slash || dot > slash)) {
            int ext_fmt = -1;
            for (int k = 0; k < EXP_COUNT; k++)
                if (!strcasecmp(dot + 1, EXPORT_FORMATS[k].ext)) ext_fmt = k;
            if (!format_given && ext_fmt >= 0) EXO.format = ext_fmt;
            if (ext_fmt >= 0) *dot = 0;     /* the extension is added back for the format */
        }
        if (project_export(&P, &EXO, base, msg, sizeof msg) != 0) {
            fprintf(stderr, "tinydaw: %s\n", msg);
            return 1;
        }
        printf("%s\n", msg);
        return 0;
    }

    if (test) shot = "";
    if (shot) {
        SDL_setenv("SDL_VIDEODRIVER", "offscreen", 1);
        SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    }
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");   /* play pads with the window behind */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "tinydaw: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (TTF_Init() != 0 || !(font = open_font(13))) {
        fprintf(stderr, "tinydaw: no usable font (set TINYDAW_FONT to a .ttf file)\n");
        return 1;
    }
    font_h = TTF_FontHeight(font);
    win = SDL_CreateWindow("tinydaw", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H,
                           SDL_WINDOW_RESIZABLE);
    if (!win) {
        fprintf(stderr, "tinydaw: window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(win, 900, 560);
    Uint32 flags = shot ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC;
    ren = SDL_CreateRenderer(win, -1, flags);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) {
        fprintf(stderr, "tinydaw: renderer: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    cur_arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    cur_resize = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
    cur_hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEALL);
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

    open_audio();
    if (!status_msg[0]) status("Generate a sound or drop audio files on the window, then drag them onto a track.");
    if (headless) {
        binds_default();
    } else {
        settings_load();
        if (theme_choice == THEME_SYSTEM) theme_follow();
        else theme_apply(theme_choice);
        if (audio_out[0]) open_audio();          /* the saved output device */
        metronome_prepare();
        open_panel(last_panel);
        sounds_open = last_panel == RI_SOUNDS;
        binds_load();
        midi_ok = midi_start(midi_in, NULL) == 0;
        if (getenv("TINYDAW_DEBUG_INPUT")) {
            char names[MIDI_MAX_DEVICES][64];
            int n = midi_ok ? midi_devices(names, MIDI_MAX_DEVICES) : 0;
            fprintf(stderr, "midi: %s, %d device%s\n", midi_ok ? "listening" : midi_error(), n, n == 1 ? "" : "s");
            for (int i = 0; i < n; i++) fprintf(stderr, "  midi device %d: %s\n", i, names[i]);
        }
        if (!loaded && autosave_exists())
            status("Unsaved work from last time was kept: File > Recover unsaved work");
    }

    if (test) {
        layout();
        int rc = selftest();
        SDL_Quit();
        return rc;
    }
    if (shot) {                         /* render offscreen and save, for testing */
        layout();
        P.playhead = (long)(1.5 * beat_frames());
        selected = P.nclips > 0 ? P.nclips - 1 : -1;
        if (getenv("TINYDAW_SHOT_EXPORT")) open_export();   /* screenshot the dialog */
        if (getenv("TINYDAW_SHOT_PADS")) {
            pads_open = 1;
            for (int i = 0; i < NPADS && i < P.nsamples; i++) P.pads[i].sample = i;
            pad_flash[5] = SDL_GetTicks();
            pads_scroll = atoi(getenv("TINYDAW_SHOT_PADS"));
            pads_content_h = 2000;
        }
        if (getenv("TINYDAW_SHOT_MENU")) menu_open = 1;
        if (getenv("TINYDAW_SHOT_LIGHT")) theme_apply(atoi(getenv("TINYDAW_SHOT_LIGHT")));   /* 1 light, 3-5 the others */
        if (getenv("TINYDAW_SHOT_AUTO")) {       /* a volume swell on track 1, a filter sweep on track 4 */
            long bar = 4 * beat_frames();
            auto_add(0, AUTO_VOLUME, 0, 0.1f);
            auto_add(0, AUTO_VOLUME, 2 * bar, 0.67f);
            auto_add(0, AUTO_VOLUME, 3 * bar, 0.67f);
            auto_add(0, AUTO_VOLUME, 3 * bar + bar / 2, 0.3f);
            auto_add(3, AUTO_CUTOFF, 0, 0.25f);
            auto_add(3, AUTO_CUTOFF, 4 * bar, 0.95f);
            auto_open[0] = auto_open[3] = 1;
            auto_param[3] = AUTO_CUTOFF;
        }
        if (getenv("TINYDAW_SHOT_ROLL")) {       /* a little melody in a new note clip, in the roll */
            layout();
            int c = new_note_clip(P.ntracks - 1, 0);
            static const int mel[][3] = {{60, 0, 2}, {64, 2, 2}, {67, 4, 2}, {72, 6, 4}, {71, 10, 2}, {67, 12, 2},
                                         {69, 14, 4}, {65, 18, 2}, {64, 20, 2}, {62, 22, 2}, {60, 24, 8}};
            long e = beat_frames() / 2;
            for (int i = 0; c >= 0 && i < 11; i++) {
                int j = note_add(P.clips[c].pattern, mel[i][0], mel[i][1] * e, mel[i][2] * e, 0.45f + 0.05f * i);
                if (i == 3 || i == 4) nsel[j] = 1;
            }
            if (c >= 0) roll_fit_clip();
            if (c >= 0) roll_open(c), nsel[3] = nsel[4] = 0;
        }
        if (getenv("TINYDAW_SHOT_SCENE")) {      /* "k,seconds": the show on scene k, that far in */
            int k = 0;
            double at = 12;
            sscanf(getenv("TINYDAW_SHOT_SCENE"), "%d,%lf", &k, &at);
            show_open = 1;
            show_cycle = 0;
            show_scene = k;
            show_clock = at;
            show_started[k] = 0;
        }
        if (getenv("TINYDAW_SHOT_FIT")) {
            for (int i = 0; i < RI_COUNT; i++) *RAIL_FLAG[i] = 0;
            layout();
            zoom_to_fit();
        }
        if (getenv("TINYDAW_SHOT_TYPE")) {      /* the BPM box being typed into */
            numedit_begin(&P.bpm, 40, 300);
            numedit_text("12");
        }
        if (getenv("TINYDAW_SHOT_MET")) {
            met_open = 1;
            MET.on = 1;
        }
        if (getenv("TINYDAW_SHOT_PANEL")) {      /* "index[:sub]", or -1 for none */
            int pn = -1, sub = 0;
            sscanf(getenv("TINYDAW_SHOT_PANEL"), "%d:%d", &pn, &sub);
            for (int i = 0; i < RI_COUNT; i++) *RAIL_FLAG[i] = 0;
            if (pn >= 0 && pn < RI_COUNT) open_panel(pn);
            int *subs[RI_COUNT] = {&sounds_sub, &theory_tab, &pads_sub, &mixer_sub, &vis_sub, &settings_sub};
            if (pn >= 0 && pn < RI_COUNT) *subs[pn] = sub;
            if (pn == RI_THEORY) {                 /* show the badge as if three sounds were added */
                lib_badge = 3;
                lib_badge_at = 0;
            }
        }
        if (getenv("TINYDAW_SHOT_SING")) {       /* the Singer, with words typed */
            show_panel(RI_SOUNDS);
            sounds_sub = 2;
            sv_defaults();
            snprintf(SV.words, sizeof SV.words, "%s", getenv("TINYDAW_SHOT_SING"));
            sounds_scroll = getenv("TINYDAW_SHOT_SCROLL") ? atoi(getenv("TINYDAW_SHOT_SCROLL")) : 0;
        }
        if (getenv("TINYDAW_SHOT_GEN")) {        /* "genre,kind,preset,scroll": the Generator */
            int ge = -1, ki = -1, pr = -1, sc = 0;
            sscanf(getenv("TINYDAW_SHOT_GEN"), "%d,%d,%d,%d", &ge, &ki, &pr, &sc);
            show_panel(RI_SOUNDS);
            sounds_sub = 1;
            gx_genre = ge;
            gx_kind = ki;
            if (pr >= 0 && pr < NSYNTH_PRESETS) synth_from_preset(&GX, gx_preset = pr);
            sounds_scroll = sc;
        }
        if (getenv("TINYDAW_SHOT_VIS") || getenv("TINYDAW_SHOT_SHOW")) {   /* fill the meters with the demo */
            vis_open = getenv("TINYDAW_SHOT_VIS") != NULL;
            show_open = getenv("TINYDAW_SHOT_SHOW") != NULL;
            P.playing = 1;
            long target = (long)(atof(getenv(show_open ? "TINYDAW_SHOT_SHOW" : "TINYDAW_SHOT_VIS")) * beat_frames());
            for (int f = 0; f < 40; f++) {        /* 40 "frames" of real audio leading up to it */
                P.playhead = target - (39 - f) * 1024;
                float tmp[2 * 1024] = {0};
                LOCK();
                mixer_run(MX, &P, P.playhead - 1024, 1, tmp, 1024, -1, 1);
                for (int i = 0; i < 1024; i++) {
                    vis_ring[2 * vis_pos] = soft_clip(tmp[2 * i] * P.master);
                    vis_ring[2 * vis_pos + 1] = soft_clip(tmp[2 * i + 1] * P.master);
                    vis_pos = (vis_pos + 1) % VIS_RING;
                }
                UNLOCK();
                vis_update();
                if (show_open) show_draw();               /* lets the blobs grow to size */
            }
            P.playing = 0;
            SDL_PauseAudioDevice(dev, 1);
            P.playing = 1;
        }
        if (getenv("TINYDAW_SHOT_MIXER")) {
            mixer_open = 1;
            mixer_scroll = atoi(getenv("TINYDAW_SHOT_MIXER"));
            mixer_content_h = 3000;
            sel_track = 3;
            for (int i = 0; i < P.nclips; i++)
                if (P.clips[i].track == 3) {
                    selected = i;
                    break;
                }
            P.tracks[3].filter = FILT_LOW;
            P.tracks[3].cutoff = 800;
            P.tracks[3].pan = -0.3f;
            P.tracks[3].reverb_send = 0.25f;
            P.clips[selected].gain_db = -3;
            P.clips[selected].fade_out = P.clips[selected].length / 2;
        }
        if (getenv("TINYDAW_SHOT_CONFIRM")) confirm_act = ACT_QUIT;
        if (getenv("TINYDAW_SHOT_SELECT")) {     /* a few clips selected, a pane scrolled part way */
            sel_clear();
            for (int i = 0; i < P.nclips; i++)
                if (P.clips[i].track == 3 || P.clips[i].track == 4) csel[i] = 1;
            sel_fix_primary();
            mixer_open = 1;
            mixer_scroll = atoi(getenv("TINYDAW_SHOT_SELECT"));
            mixer_content_h = 3000;
        }
        const char *th = getenv("TINYDAW_SHOT_THEORY");      /* "tab,root,scale" */
        const char *gs = getenv("TINYDAW_SHOT_GENRE");
        if (gs) gn_sel = atoi(gs);
        if (th) {
            theory_open = 1;
            sscanf(th, "%d,%d,%d", &theory_tab, &th_root, &th_scale);
        }
        frame();
        if (getenv("TINYDAW_SHOT_CLICK")) {      /* "x,y": click there first (opens a list, a menu...) */
            int cx = 0, cy = 0;
            sscanf(getenv("TINYDAW_SHOT_CLICK"), "%d,%d", &cx, &cy);
            sim(cx, cy, 1, 0, 0, 0);
            sim(cx, cy, 0, 1, 0, 0);
            mx = cx + 40;                        /* hover inside the list */
            my = cy + 60;
        }
        if (getenv("TINYDAW_SHOT_EDITOR")) {     /* the clip editor window, for clip N */
            editor_open(atoi(getenv("TINYDAW_SHOT_EDITOR")));
            if (ewin) {
                for (int i = 0; i < 2; i++) editor_frame();
                SDL_Renderer *m = ren;           /* draw it once more and keep the pixels */
                int ew, eh;
                SDL_GetWindowSize(ewin, &ew, &eh);
                ren = eren;
                int sw = W, sh = H;
                W = ew;
                H = eh;
                editor_draw();
                SDL_Surface *es = SDL_CreateRGBSurfaceWithFormat(0, ew, eh, 32, SDL_PIXELFORMAT_ARGB8888);
                SDL_RenderReadPixels(eren, NULL, SDL_PIXELFORMAT_ARGB8888, es->pixels, es->pitch);
                int ok = SDL_SaveBMP(es, shot) == 0;
                SDL_FreeSurface(es);
                ren = m;
                W = sw;
                H = sh;
                SDL_Quit();
                return ok ? 0 : 1;
            }
        }
        for (int i = 0; i < 2; i++) frame();
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
        SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
        int ok = SDL_SaveBMP(s, shot) == 0;
        SDL_FreeSurface(s);
        SDL_Quit();
        return ok ? 0 : 1;
    }

    int running = 1;
    while (running) {
        lpress = lrel = rpress = wheel_x = wheel_y = 0;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT:
                if (SDL_AtomicGet(&fd_state) == 0) request(ACT_QUIT, NULL);
                break;
            case SDL_MOUSEMOTION:
                if (ewin && e.motion.windowID == ewin_id) {
                    ein.mx = e.motion.x;
                    ein.my = e.motion.y;
                } else {
                    mx = e.motion.x;
                    my = e.motion.y;
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (ewin && e.button.windowID == ewin_id) {   /* the editor window has its own mouse */
                    ein.mx = e.button.x;
                    ein.my = e.button.y;
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        ein.lpress = ein.ldown = 1;
                        ein.ldouble = e.button.clicks >= 2;
                    } else if (e.button.button == SDL_BUTTON_RIGHT) ein.rpress = 1;
                    break;
                }
                mx = e.button.x;
                my = e.button.y;
                if (e.button.button == SDL_BUTTON_LEFT) {
                    lpress = ldown = 1;
                    ldouble = e.button.clicks >= 2;
                }
                else if (e.button.button == SDL_BUTTON_RIGHT) rpress = 1;
                break;
            case SDL_MOUSEBUTTONUP:
                if (e.button.button == SDL_BUTTON_LEFT) {   /* to the window that owns the drag, else the one under it */
                    int to_editor = drag.kind != D_NONE ? drag_in_editor : ewin && e.button.windowID == ewin_id;
                    if (to_editor) {
                        ein.lrel = 1;
                        ein.ldown = 0;
                    } else {
                        lrel = 1;
                        ldown = 0;
                    }
                }
                break;
            case SDL_MOUSEWHEEL:
                if (ewin && e.wheel.windowID == ewin_id) {
                    ein.wheel_y += e.wheel.y;
                    ein.wheel_x += e.wheel.x;
                    break;
                }
                wheel_y += e.wheel.y;
                wheel_x += e.wheel.x;
                break;
            case SDL_DROPFILE: {
                size_t n = strlen(e.drop.file);
                if (n > 5 && !strcmp(e.drop.file + n - 5, ".tdaw")) request(ACT_OPEN_PATH, e.drop.file);
                else import_file(e.drop.file);
                SDL_free(e.drop.file);
                break;
            }
            case SDL_TEXTINPUT:
                numedit_text(e.text.text);
                tf_insert(e.text.text);
                break;
            case SDL_KEYDOWN:
                if (numedit_key(e.key.keysym)) break;      /* typing a value */
                if (tf_key(e.key.keysym)) break;           /* typing a name */
                if (ewin && e.key.windowID == ewin_id) {    /* the editor: Esc closes it, the rest as usual */
                    if (e.key.keysym.sym == SDLK_ESCAPE) {
                        editor_close();
                        break;
                    }
                    key(e.key.keysym);
                    break;
                }
                if (pads_take_key(e.key.keysym) && e.key.keysym.sym != SDLK_ESCAPE) {
                    int sc = e.key.keysym.scancode, bound = 0;
                    for (int i = 0; i < NPADS; i++) bound |= binds[i].key == sc;
                    if (learn_pad >= 0 || bound) {          /* the pads have this key */
                        if (!e.key.repeat) input_event(SRC_KEY, sc, 110, 1, -1);
                        break;
                    }
                }
                key(e.key.keysym);
                break;
            case SDL_KEYUP:
                if (pads_take_key(e.key.keysym)) input_event(SRC_KEY, e.key.keysym.scancode, 0, 0, -1);
                break;
            case SDL_WINDOWEVENT:               /* the desktop theme may have changed meanwhile */
                if (e.window.event == SDL_WINDOWEVENT_CLOSE) {   /* with two windows, there's no SDL_QUIT */
                    if (ewin && e.window.windowID == ewin_id) editor_close();
                    else if (SDL_AtomicGet(&fd_state) == 0) request(ACT_QUIT, NULL);
                    break;
                }
                if (e.window.event == SDL_WINDOWEVENT_LEAVE && ewin && e.window.windowID == ewin_id)
                    ein.mx = ein.my = -1000;
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) theme_follow();
                break;
            case SDL_JOYDEVICEADDED: joy_added(e.jdevice.which); break;
            case SDL_JOYDEVICEREMOVED: joy_removed(e.jdevice.which); break;
            case SDL_JOYBUTTONDOWN:
            case SDL_JOYBUTTONUP:
            case SDL_JOYHATMOTION:
            case SDL_JOYAXISMOTION:
                joy_event(&e);
                break;
            }
        }
        mods = SDL_GetModState();
        if (lrel && drag.kind != D_NONE && !drag_in_editor) update_drag();   /* final position */
        if (ein.lrel && drag.kind != D_NONE && drag_in_editor) {             /* the editor's, likewise */
            int sx = mx, sy = my;
            mx = ein.mx;
            my = ein.my;
            update_drag();
            mx = sx;
            my = sy;
        }
        frame();
        SDL_RenderPresent(ren);
        editor_frame();
        if (quit_now) running = 0;
        if (!(flags & SDL_RENDERER_PRESENTVSYNC)) SDL_Delay(8);
    }

    midi_stop();
    if (!is_dirty()) remove(autosave_file());   /* a clean exit leaves nothing to recover */
    if (dev) SDL_CloseAudioDevice(dev);
    SDL_Quit();
    return 0;
}
