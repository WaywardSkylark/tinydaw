/* tinydaw: colours, text, input, layout, project edits, the metronome and the widgets. */
#include "ui.h"

Project P;
SDL_AudioDeviceID dev;
int out_buffer = 512;                /* the output's buffer, frames (for the recording offset) */
SDL_AudioDeviceID cap_dev;
int cap_channels = 2, cap_buffer, cap_fake;   /* cap_fake: the self-test feeds it directly */
char audio_in[256];                  /* input device name, "" = the system default */
float in_gain_db = 0;                /* input gain */
int monitor_on;                      /* hear the microphone through the output */
double rec_offset_ms = -1;           /* < 0: automatic (the buffers' round trip) */
float *cap_ring;
SDL_atomic_t cap_w;                  /* frames written, ever (wraps around the ring) */
unsigned cap_r, mon_r;               /* frames read by the interface and by the monitor */
float in_peak, in_meter;             /* the loudest input since last looked at; the meter */
unsigned char track_arm[MAX_TRACKS];
T_cap_marks cap_marks[64];   /* each chunk: where the song was */
SDL_atomic_t cap_nmark;
T_mic mic;
SDL_Window *win;
SDL_Renderer *ren;
TTF_Font *font;
int font_h;
int W = 1280, H = 760;
SDL_Cursor *cur_arrow, *cur_resize, *cur_hand;

/* The theme: set by theme_apply. Dark is the original look; light keeps the same structure
 * with the greys inverted and the accents deepened so they read on white. */
Col BG, PANEL, PANEL2, PANEL3, LINE, TEXT, DIM, ACCENT, DARK, HEAD;

int theme_light;

/* How controls are drawn. Dark and Light are flat; the others shape buttons, fields and the bars
 * with gradients, gloss and rounded corners, in the spirit of their namesakes. */
T_ST ST;

int theme_kind;                  /* the look in use: THEME_DARK .. THEME_BLENDER (never SYSTEM) */

Col rgb(int r, int g, int b) { return (Col){(Uint8)r, (Uint8)g, (Uint8)b, 255}; }

int theme_choice = THEME_SYSTEM;

void theme_apply(int kind)       /* THEME_DARK, THEME_LIGHT (1 also means light) or a shaped look */
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

const Col TRACK_COL[MAX_TRACKS] = {
    {226, 104, 92, 255}, {232, 158, 70, 255}, {212, 198, 82, 255}, {118, 198, 110, 255},
    {80, 188, 190, 255}, {92, 142, 230, 255}, {160, 112, 220, 255}, {220, 100, 168, 255},
    {236, 128, 112, 255}, {240, 186, 96, 255}, {170, 210, 90, 255}, {90, 200, 150, 255},
    {110, 170, 240, 255}, {140, 130, 240, 255}, {200, 130, 230, 255}, {240, 130, 160, 255},
};

Col shade(Col c, float k)
{
    float r = c.r * k, g = c.g * k, b = c.b * k;
    return (Col){r > 255 ? 255 : (Uint8)r, g > 255 ? 255 : (Uint8)g, b > 255 ? 255 : (Uint8)b, c.a};
}

Col alpha(Col c, Uint8 a) { c.a = a; return c; }

/* shade() for interface greys that follows the theme: "dimmer" means closer to the background,
 * so on a light theme it lightens instead of darkening, and "brighter" darkens */
Col tone(Col c, float k)
{
    if (!theme_light) return shade(c, k);
    if (k >= 1) return shade(c, 1 - (k - 1) * 0.35f);   /* gentler: light greys darken fast */
    float t = 1 - k;
    return (Col){(Uint8)(c.r + (255 - c.r) * t), (Uint8)(c.g + (255 - c.g) * t), (Uint8)(c.b + (255 - c.b) * t), c.a};
}

/* A track colour at strength k: darkened on the dark theme, washed towards white on the light */
Col wash(Col c, float k)
{
    if (!theme_light) return shade(c, k);
    float t = (1 - k) * 0.85f;
    return (Col){(Uint8)(c.r + (255 - c.r) * t), (Uint8)(c.g + (255 - c.g) * t), (Uint8)(c.b + (255 - c.b) * t), c.a};
}

void fill(SDL_Rect r, Col c)
{
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(ren, &r);
    if (c.a >= 230 || (c.a >= 150 && r.w >= W && r.h >= H)) tcheck_cover(r);   /* (checking text: hidden can't overlap) */
}

void outline(SDL_Rect r, Col c)
{
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    SDL_RenderDrawRect(ren, &r);
}

void line(int x0, int y0, int x1, int y1, Col c)
{
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    SDL_RenderDrawLine(ren, x0, y0, x1, y1);
}

Col mix_col(Col a, Col b, float t)
{
    return (Col){(Uint8)(a.r + (b.r - a.r) * t), (Uint8)(a.g + (b.g - a.g) * t), (Uint8)(a.b + (b.b - a.b) * t),
                 (Uint8)(a.a + (b.a - a.a) * t)};
}

int corner_inset(int radius, int yy, int h)   /* how far a rounded corner pulls row yy in */
{
    if (radius <= 0) return 0;
    if (radius * 2 > h) radius = h / 2;
    double dy = yy < radius ? radius - yy - 0.5 : yy >= h - radius ? yy - (h - radius) + 0.5 : 0;
    if (dy <= 0) return 0;
    return radius - (int)floor(sqrt((double)radius * radius - dy * dy) + 0.5);
}

/* A box with rounded corners, shaded from `top` to `bot`; gloss adds a glassy highlight on top */
void shape_fill(SDL_Rect r, Col top, Col bot, int radius, int gloss)
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

void shape_outline(SDL_Rect r, Col c, int radius)
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
void style_box(SDL_Rect r, int state)
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

Col btn_fg(int on) { return on ? (ST.shaped ? ST.on_text : DARK) : (ST.shaped ? ST.btn_text : TEXT); }

/* a text field's box */
void field_box(SDL_Rect r, int hov, int active)
{
    if (!ST.shaped) {
        fill(r, active || hov ? PANEL3 : PANEL2);
        outline(r, active ? ACCENT : LINE);
        return;
    }
    shape_fill(r, ST.field_bg, ST.field_bg, ST.field_radius, 0);
    shape_outline(r, active ? ACCENT : hov ? ST.hover_border : ST.field_border, ST.field_radius);
}

TextEntry tcache[512];
unsigned tclock;

TextEntry *text_get(const char *s, Col c)
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

/* ---- the interface's size. Everything is laid out in logical units; each window's renderer is
 * scaled by ui_px, its device pixels per unit (the chosen size times the screen's own pixel density),
 * and text is rendered at its real size in device pixels, so it stays sharp at any size. */
float ui_user = 1;               /* the chosen interface size: 1 = 100% */

int ui_choice;                   /* 0 = automatic (matches the screen), else the percentage */
float ui_px = 1;                 /* device pixels per logical unit, in the main window */
int font_pt;                     /* the size the font is open at, in device pixels */

void text_forget(SDL_Renderer *r)   /* a window is closing (NULL: the size changed): its text goes */
{
    for (size_t i = 0; i < sizeof tcache / sizeof *tcache; i++)
        if (tcache[i].t && (!r || tcache[i].r == r)) {
            SDL_DestroyTexture(tcache[i].t);
            tcache[i].t = NULL;
        }
}

/* TINYDAW_CHECK_TEXT: every piece of text's visible box, to find any that overlap (tcheck_report) */
int tcheck = -1, ntlog;

T_tlog tlog[4096];

/* an opaque box drawn over text hides it: it no longer counts */
void tcheck_cover(SDL_Rect r)
{
    if (tcheck <= 0) return;
    if (SDL_RenderIsClipEnabled(ren)) {
        SDL_Rect clip;
        SDL_RenderGetClipRect(ren, &clip);
        if (!SDL_IntersectRect(&r, &clip, &r)) return;
    }
    for (int i = 0; i < ntlog; i++) {
        SDL_Rect o, *t = &tlog[i].r;
        if (!SDL_IntersectRect(t, &r, &o)) continue;
        if (o.w * o.h * 2 > t->w * t->h) {      /* mostly hidden: gone */
            tlog[i--] = tlog[--ntlog];
        } else if (o.h >= t->h / 2) {           /* hidden at one end: what's left of it */
            if (o.x <= t->x) {
                t->w -= o.x + o.w - t->x;
                t->x = o.x + o.w;
            } else {
                t->w = o.x - t->x;
            }
        }
    }
}

void tcheck_report(const char *where)
{
    if (tcheck <= 0) return;
    for (int i = 0; i < ntlog; i++)
        for (int j = i + 1; j < ntlog; j++) {
            SDL_Rect o;
            if (SDL_IntersectRect(&tlog[i].r, &tlog[j].r, &o) && o.w > 2 && o.h > 3)
                fprintf(stderr, "overlap [%s] \"%s\" (%d,%d %dx%d) and \"%s\" (%d,%d %dx%d)\n", where, tlog[i].s,
                        tlog[i].r.x, tlog[i].r.y, tlog[i].r.w, tlog[i].r.h, tlog[j].s, tlog[j].r.x, tlog[j].r.y,
                        tlog[j].r.w, tlog[j].r.h);
        }
    ntlog = 0;
}

int text(int x, int y, const char *s, Col c)
{
    TextEntry *e = text_get(s, c);
    if (!e) return 0;
    if (tcheck < 0) tcheck = getenv("TINYDAW_CHECK_TEXT") != NULL;
    if (tcheck && ntlog < 4096 && s[0] && strcmp(s, " ")) {   /* what of it can be seen */
        int tw = (int)ceilf(e->w / ui_px), th = font_h;
        int lead = 0, trail = 0;                 /* the glyphs' own box, without padding spaces */
        while (s[lead] == ' ') lead++;
        size_t n = strlen(s);
        while (n > (size_t)lead && s[n - 1 - trail] == ' ') trail++;
        SDL_Rect r = {x, y + th / 6, tw, th * 2 / 3}, clip;
        if (SDL_RenderIsClipEnabled(ren)) {
            SDL_RenderGetClipRect(ren, &clip);
            if (!SDL_IntersectRect(&r, &clip, &r)) r.w = 0;
        }
        if (r.x < 0) r.w += r.x, r.x = 0;
        if (r.x + r.w > W) r.w = W - r.x;
        if (r.w > 0) {
            tlog[ntlog].r = r;
            snprintf(tlog[ntlog].s, sizeof tlog[ntlog].s, "%s", s);
            ntlog++;
        }
    }
    /* on whole device pixels, at its own size: one texture pixel to one screen pixel */
    SDL_FRect d = {roundf(x * ui_px) / ui_px, roundf(y * ui_px) / ui_px, e->w / ui_px, e->h / ui_px};
    SDL_RenderCopyF(ren, e->t, NULL, &d);
    return (int)ceilf(e->w / ui_px);
}

int text_w(const char *s)
{
    int w = 0, h = 0;
    TTF_SizeUTF8(font, s, &w, &h);
    return (int)ceilf(w / ui_px);
}

void text_clip(SDL_Rect r, int x, int y, const char *s, Col c)
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

void text_center(SDL_Rect r, const char *s, Col c)
{
    int w = text_w(s);
    if (tcheck > 0 && w > r.w - 2 && r.w > 8)   /* (checking text: a label wider than its box) */
        fprintf(stderr, "overlap [too wide] \"%s\" is %d wide in a box %d wide at %d,%d\n", s, w, r.w, r.x, r.y);
    text(r.x + (r.w - w) / 2, r.y + (r.h - font_h) / 2, s, c);
}

char status_msg[300];

void status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(status_msg, sizeof status_msg, fmt, ap);
    va_end(ap);
    if (getenv("TINYDAW_DEBUG_INPUT")) fprintf(stderr, "status: %s\n", status_msg);
}

/* ---------------------------------------------------------------- input */

int mx, my, ldown, lpress, lrel, rpress, wheel_x, wheel_y;

SDL_Keymod mods;

int hit(SDL_Rect r) { return mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h; }

Pop pops[NPOPS];
int drag_pop = -1;

int pop_of(Uint32 id)            /* which pop-out window an event is for, or -1 */
{
    for (int k = 0; k < NPOPS; k++)
        if (pops[k].win && pops[k].id == id) return k;
    return -1;
}

SDL_Rect lv_fader[MAX_TRACKS], cs_fader[MAX_TRACKS], cs_mute[MAX_TRACKS];   /* where they were drawn (self-test) */
int ed_clip = -1, ed_handle;
Clip ed_last, ed_orig;            /* the clip as last seen (to find it again), and at a drag's start */
Clip ed_lib;                     /* the part of it being kept, its gain and fades */
SDL_Rect ed_layer_r[4], ed_rr_r[3];   /* (for the self-test) */
int ed_nclips;
long ed_v0, ed_v1;                /* the part of the sound shown, in frames */
SDL_Rect ed_wave, ed_btn[8];
T_drag drag;

/* ---------------------------------------------------------------- layout and view */

T_L L;

/* automation lanes (one shown under a track when its A is on) and the piano roll */
int auto_open[MAX_TRACKS], auto_param[MAX_TRACKS];

int roll_clip = -1, roll_pattern, roll_top = -1, roll_kh = 14;
double roll_saved_view, roll_saved_pps;
long roll_len;                   /* the length of the last note drawn or stretched */
unsigned char nsel[MAX_NOTES];   /* selected notes in the piano roll */
int ldouble;                     /* this left press is a double-click */
int pads_sub, mixer_sub, vis_sub, settings_sub, sounds_sub;   /* each panel's open sub-tab */
int pads_open;                   /* the Pads pane (shares the right-hand slot) */
int drums_open;                  /* the Drums pane: a track's step patterns */
int drum_cur;                    /* the pattern it shows (its id), 0 = none */
int drum_mode;                   /* editing: 0 hits, 1 velocity, 2 chance */
int drums_scroll;
SDL_Rect dr_cell[DRUM_ROWS][DRUM_STEPS], dr_head[DRUM_ROWS], dr_add;
int sec_renaming = -1;           /* the arranger section whose name is being typed */
int mixer_open;                  /* the Mixer pane: clip, track and master effects */
int vis_open;                    /* the Visualizer pane: spectrum, scope, meters */
int show_open;                   /* the full-screen show */
int show_scene, show_cycle = 1;  /* 0 = blobs, 1.. = scenes.c's scene show_scene - 1 */
double show_each = 24, show_clock, show_started[SCENE_COUNT + 1];
int sounds_open = 1;             /* the Create panel: the library and everything that makes sounds and parts */
int lib_badge;                   /* sounds added since the Create panel was last opened */
Uint32 lib_badge_at;
int th_len = 2;
int settings_open;
Mixer *MX;                       /* the live mixer (see audio_cb) */
int sel_track;                   /* the track the Mixer pane shows */
int headless;                    /* self-test or screenshot: touch no files */
int theory_open, theory_tab = 0, th_root, th_scale, th_sevenths, th_scroll, th_content_h;
int chd_sel = -1;                /* the chord on the chord track clicked last */
int chp_open, chp_idx;           /* the chord picker, and the chord it changes */

/* one-off preview buffer (chords, intervals, notes) played on top of everything */
float *scratch;

long scratch_len, scratch_pos;
double view_start = 0;           /* seconds at the left edge of the timeline */
double pps = 110;                /* pixels per second */
int selected = -1;              /* the clip clicked last (the Mixer pane edits it) */
unsigned char csel[MAX_CLIPS];   /* every selected clip; selected is always one of them */
int lib_scroll = 0;
Loud live_loud;                  /* the output's loudness, since play last started */

/* A tempo edit by hand (BPM box, its menu, tap tempo): the tempo it started from, and when it
 * last moved. Once it settles, the selected clips are matched to the new tempo (tempo_apply). */
double tempo_from;

Uint32 tempo_at;

void tempo_touch(double before)
{
    if (P.bpm == before) return;
    if (tempo_from == 0) tempo_from = before;
    tempo_at = SDL_GetTicks();
}

SDL_Rect lib_rects[MAX_SAMPLES];  /* where each library row was drawn (used by --selftest) */
GenParams G = {WAVE_SAW, 45, 1.0, 0.6, 5, 60, 440};
int snap_i = 4;

/* the screen's own pixels per window point (2 on a "Retina"-style screen, 1.5 on Windows at 150%) */
float window_density(SDL_Window *w)
{
    int pw = 0, ph = 0, ww = 0, wh = 0;
    SDL_GetWindowSizeInPixels(w, &pw, &ph);
    SDL_GetWindowSize(w, &ww, &wh);
    return ww > 0 && pw > 0 ? (float)pw / ww : 1;
}

/* The size "Automatic" picks: 100% where the system already scales the window for us (it reports
 * more device pixels than window points), else from the desktop's scale or the screen's DPI. */
float ui_auto(void)
{
    const char *e = getenv("TINYDAW_SCALE");
    if (e && atof(e) >= 0.5 && atof(e) <= 3) return (float)atof(e);
    if (win && window_density(win) > 1.1f) return 1;   /* the system scales already */
    e = getenv("GDK_SCALE");
    if (!e) e = getenv("QT_SCALE_FACTOR");
    if (e && atof(e) >= 1 && atof(e) <= 3) return (float)atof(e);
    float ddpi = 0, hdpi = 0, vdpi = 0;
    if (win && SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(win), &ddpi, &hdpi, &vdpi) == 0 && hdpi >= 130 && hdpi < 600) {
        float k = roundf(hdpi / 96 * 4) / 4;    /* to the nearest quarter */
        return k < 1 ? 1 : k > 2 ? 2 : k;
    }
    return 1;
}

/* The largest size that leaves the window room for the whole layout on its screen. */
int ui_offscreen;                /* drawing off-screen (screenshots, the self-test): no real screen to fit */

float ui_max(void)
{
    SDL_Rect b;
    if (ui_offscreen) return 3;
    if (!win || SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(win), &b) != 0 || b.w <= 0) return 3;
    float k = fminf(b.w / 1070.0f, (b.h - 40) / 560.0f);   /* the smallest the main window can be */
    return k < 1 ? 1 : k;
}

/* a window's size in logical units */
void logical_size(SDL_Renderer *r, int *w, int *h)
{
    int ow = 0, oh = 0;                         /* the window's own pixels: they don't lag a resize */
    SDL_Window *wn = SDL_RenderGetWindow(r);
    if (wn) SDL_GetWindowSizeInPixels(wn, &ow, &oh);
    else SDL_GetRendererOutputSize(r, &ow, &oh);
    *w = (int)(ow / ui_px);
    *h = (int)(oh / ui_px);
}

/* Puts the chosen size into effect: the scale every window draws at, the font at its real size,
 * the cached text redrawn, and the window big enough for the layout. */
void ui_apply(void)
{
    float want = ui_choice ? ui_choice / 100.0f : ui_auto(), top = ui_max();
    ui_user = want > top ? top : want;
    ui_px = ui_user * window_density(win);
    int pt = (int)floorf(13 * ui_px);           /* never larger than the layout expects */
    if (pt != font_pt) {
        TTF_Font *f = open_font(pt);
        if (f) {
            if (font) TTF_CloseFont(font);
            font = f;
            font_pt = pt;
        }
    }
    font_h = (int)lroundf(TTF_FontHeight(font) / ui_px);
    text_forget(NULL);
    SDL_RenderSetScale(ren, ui_px, ui_px);
    for (int k = 0; k < NPOPS; k++)
        if (pops[k].ren) SDL_RenderSetScale(pops[k].ren, ui_px, ui_px);
}

/* The window at its size for the interface: the layout's 1280 x 760 (or what's there, if bigger),
 * no smaller than its minimum, and no bigger than the screen. */
void ui_fit_window(void)
{
    int lw, lh;
    logical_size(ren, &lw, &lh);
    int want_w = (int)((lw > 1280 ? lw : 1280) * ui_user), want_h = (int)((lh > 760 ? lh : 760) * ui_user);
    SDL_Rect b;
    if (!ui_offscreen && SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(win), &b) == 0 && b.w > 0) {
        if (want_w > b.w) want_w = b.w;
        if (want_h > b.h - 40) want_h = b.h - 40;
    }
    int ww, wh;
    SDL_GetWindowSize(win, &ww, &wh);
    if (ww != want_w || wh != want_h) SDL_SetWindowSize(win, want_w, want_h);
}

/* A new interface size (0 = automatic): applied, the window resized to keep the layout, saved. */
void ui_set(int percent)
{
    float top = ui_max();
    if (percent && percent / 100.0f > top + 0.001f) {
        status("This screen has room for up to %.0f%%", floorf(top * 4) / 4 * 100);
        percent = (int)(floorf(top * 4) / 4 * 100);
    }
    float before = ui_user;
    ui_choice = percent;
    ui_apply();
    if (ui_user != before) {                    /* the window grows or shrinks with what's in it */
        int ww, wh;
        SDL_GetWindowSize(win, &ww, &wh);
        int nw = (int)(ww * ui_user / before), nh = (int)(wh * ui_user / before);
        SDL_Rect b;
        if (!ui_offscreen && SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(win), &b) == 0 && b.w > 0) {
            if (nw > b.w) nw = b.w;
            if (nh > b.h - 40) nh = b.h - 40;
        }
        SDL_SetWindowMinimumSize(win, 1, 1);    /* the old size's minimum mustn't hold it; the next frame sets the new one */
        SDL_SetWindowSize(win, nw, nh);
    }
    settings_save();
    status("Interface size %.0f%%%s", ui_user * 100, percent ? "" : " (automatic: matches your screen)");
}

/* where the mouse is, in logical units of the window it's over (events can arrive already scaled
 * or not, depending on the system; the mouse state is always in window points) */
void mouse_logical(int *x, int *y)
{
    int sx = 0, sy = 0;
    SDL_GetMouseState(&sx, &sy);
    *x = (int)floorf(sx / ui_user);
    *y = (int)floorf(sy / ui_user);
}

void layout(void)
{
    logical_size(ren, &W, &H);
    L.top = 48;
    L.head = 170;
    L.ruler = 44;                       /* bar numbers above, minutes:seconds below */
    L.status = 26;
    L.pane_x = 70;                      /* RAIL_W: the category rail is on the far left */
    L.pane_w = theory_open || pads_open || drums_open || mixer_open || vis_open || sounds_open ? PANE_W : settings_open ? 440 : 0;
    L.left = L.pane_x + L.pane_w;
    L.tl_x = L.left + L.head;
    L.tl_w = W - L.tl_x;
    L.arr = 22;                         /* the arranger strip: the song's sections */
    L.chd = 24;                         /* the chord track, under it */
    L.lanes_y = L.top + L.ruler + L.arr + L.chd;
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
int track_y(int t)
{
    int y = L.lanes_y;
    for (int k = 0; k < t && k < P.ntracks; k++) y += L.lane_h + (auto_open[k] ? L.auto_h : 0);
    return y;
}

int lanes_h(void) { return track_y(P.ntracks) - L.lanes_y; }

SDL_Rect auto_rect(int t) { return (SDL_Rect){L.tl_x, track_y(t) + L.lane_h, L.tl_w, auto_open[t] ? L.auto_h : 0}; }

int auto_lane_at(int y)          /* the track whose automation lane is at y, or -1 */
{
    for (int t = 0; t < P.ntracks; t++) {
        int y0 = track_y(t) + L.lane_h;
        if (auto_open[t] && y >= y0 && y < y0 + L.auto_h) return t;
    }
    return -1;
}

double frame_to_x(long f) { return L.tl_x + ((double)f / SR - view_start) * pps; }

double x_to_frame_raw(int x) { return (view_start + (x - L.tl_x) / pps) * SR; }

long x_to_frame(int x)
{
    double f = x_to_frame_raw(x);
    return f < 0 ? 0 : (long)f;
}

int lane_at(int y)
{
    for (int t = 0; t < P.ntracks; t++)
        if (y < track_y(t + 1)) return t;
    return P.ntracks - 1;
}

/* ---- the tempo map in the interface: bars and beats where they fall, with tempo and time
 * signature changes taken into account */
long beat_at(long f) { return (long)llround(60.0 / meter_bpm_at(&P, f) * SR); }

/* a quarter note there */
long beat_frames(void) { return beat_at(P.playhead); }

long bar_floor(long f) { return meter_bar_frame(&P, floor(meter_bars(&P, f) + 1e-9)); }

long bar_ceil(long f) { return meter_bar_frame(&P, ceil(meter_bars(&P, f) - 1e-9)); }

long bar_round(long f) { return meter_bar_frame(&P, round(meter_bars(&P, f))); }

long bar_len_at(long f)
{
    long l;
    meter_at(&P, f, NULL, NULL, NULL, NULL, NULL, &l);
    return l;
}

/* `f` to the nearest point of a grid `g` quarter notes apart (or whole bars when g < 0), counted
 * from the start of its bar so the grid lines up with odd time signatures */
long grid_snap(long f, double g)
{
    if (g < 0) return bar_round(f);
    double bar = floor(meter_bars(&P, f) + 1e-9), bs = meter_beats(&P, meter_bar_frame(&P, bar)), b = meter_beats(&P, f);
    double q = bs + round((b - bs) / g) * g, nb = meter_beats(&P, meter_bar_frame(&P, bar + 1));
    if (q > nb) q = nb;                         /* a grid step can't run past the next bar */
    return meter_beats_frame(&P, q);
}

double snap_beats(void) { return snap_i == 1 ? -1 : SNAP_BEATS[snap_i]; }

/* 0 = off, -1 = bar */
/* "bar 3 beat 2.50": the bar, and the beat of its time signature (eighths in 6/8) */
void pos_text(long f, char *buf, size_t n)
{
    int bar, num;
    long bs, bl;
    meter_at(&P, f < 0 ? 0 : f, NULL, &num, NULL, &bar, &bs, &bl);
    snprintf(buf, n, "bar %d  beat %.2f", bar + 1, (double)(f - bs) / ((double)bl / num) + 1);
}

/* the next (dir 1) or previous (-1) bar line, or beat line, from f */
long meter_step(long f, int dir, int beat)
{
    int num;
    long bs, bl;
    meter_at(&P, f, NULL, &num, NULL, NULL, &bs, &bl);
    if (!beat) {
        double b = meter_bars(&P, f);
        double t = dir > 0 ? floor(b + 1e-9) + 1 : ceil(b - 1e-9) - 1;
        return meter_bar_frame(&P, t < 0 ? 0 : t);
    }
    double unit = (double)bl / num, k = (f - bs) / unit;
    double t = dir > 0 ? floor(k + 1e-9) + 1 : ceil(k - 1e-9) - 1;
    if (t < 0) {                                /* back into the bar before */
        if (bs <= 0) return 0;
        meter_at(&P, bs - 1, NULL, &num, NULL, NULL, &bs, &bl);
        return bs + (long)llround((num - 1) * (double)bl / num);
    }
    return bs + (long)llround(t * unit);
}

long snap_frames(void)           /* the grid's size where the playhead is (for nudging) */
{
    if (!SNAP_BEATS[snap_i]) return 0;
    return snap_i == 1 ? bar_len_at(P.playhead) : (long)llround(SNAP_BEATS[snap_i] * beat_frames());
}

long snap(long f)                /* Shift held = no snapping */
{
    if (!SNAP_BEATS[snap_i] || (mods & KMOD_SHIFT)) return f;
    return grid_snap(f, snap_beats());
}

/* the grid for quantizing, in quarter notes: the Snap setting, or 1/16 when it's off */
double quant_grid(void) { return SNAP_BEATS[snap_i] ? snap_beats() : 0.25; }

/* Moves note starts (of `pattern`, those in `mask` or all of them) onto the grid, measured in the
 * song (origin = where the pattern's frame 0 is). strength 1 = right on it. Returns how many. */
int notes_quantize(int pattern, long origin, const unsigned char *mask, double grid, double strength)
{
    int n = 0;
    LOCK();
    for (int i = 0; i < P.nnotes; i++) {
        Note *nt = &P.notes[i];
        if (nt->pattern != pattern || (mask && !mask[i])) continue;
        long abs = origin + nt->start, q = grid_snap(abs, grid);
        long ns = abs + (long)llround((q - abs) * strength) - origin;
        nt->start = ns < 0 ? 0 : ns;
        n++;
    }
    UNLOCK();
    return n;
}

/* Loosens notes up the way a player would: a little early or late (up to `ms`), a little softer or
 * harder, a little shorter or longer. */
int notes_humanize(int pattern, const unsigned char *mask, double ms)
{
    static uint32_t r = 0x9e3779b9u;
    int n = 0;
    LOCK();
    for (int i = 0; i < P.nnotes; i++) {
        Note *nt = &P.notes[i];
        if (nt->pattern != pattern || (mask && !mask[i])) continue;
        double u[3];
        for (int k = 0; k < 3; k++) {            /* triangular: small nudges more often than big ones */
            r ^= r << 13;
            r ^= r >> 17;
            r ^= r << 5;
            double a = (r & 0xffff) / 65535.0;
            r ^= r << 13;
            r ^= r >> 17;
            r ^= r << 5;
            u[k] = a + (r & 0xffff) / 65535.0 - 1;   /* -1..1 */
        }
        long d = (long)llround(u[0] * ms / 1000 * SR);
        nt->start = nt->start + d < 0 ? 0 : nt->start + d;
        float v = nt->vel * (float)(1 + 0.12 * u[1]);
        nt->vel = v < 0.05f ? 0.05f : v > 1 ? 1 : v;
        long len = (long)llround(nt->length * (1 + 0.05 * u[2]));
        nt->length = len < SR / 100 ? SR / 100 : len;
        n++;
    }
    UNLOCK();
    return n;
}

SDL_Rect clip_rect(const Clip *c)
{
    int x0 = (int)floor(frame_to_x(c->start)), x1 = (int)ceil(frame_to_x(c->start + c->length));
    return (SDL_Rect){x0, track_y(c->track) + 2, x1 - x0 < 2 ? 2 : x1 - x0, L.lane_h - 4};
}

/* The little squares on a clip's top corners that drag its fades (none on very narrow clips). */
int fade_handles(const Clip *c, SDL_Rect *in, SDL_Rect *out)
{
    SDL_Rect r = clip_rect(c);
    if (r.w < 40 || r.h < 20) return 0;
    int fi = (int)(c->fade_in / (double)SR * pps), fo = (int)(c->fade_out / (double)SR * pps);
    int xi = r.x + (fi > 8 ? fi : 8), xo = r.x + r.w - (fo > 8 ? fo : 8);
    *in = (SDL_Rect){xi - 4, r.y + 1, 9, 9};
    *out = (SDL_Rect){xo - 5, r.y + 1, 9, 9};
    return 1;
}

int fade_hit(int i)
{
    SDL_Rect a, b;
    if (!fade_handles(&P.clips[i], &a, &b)) return D_NONE;
    a = (SDL_Rect){a.x - 2, a.y - 2, a.w + 4, a.h + 4};
    b = (SDL_Rect){b.x - 2, b.y - 2, b.w + 4, b.h + 4};
    return hit(a) ? D_FADEIN : hit(b) ? D_FADEOUT : D_NONE;
}

SDL_Rect volume_bar(int t)
{
    if (L.lane_h < 40)                  /* compact rows: a thin bar along the bottom */
        return (SDL_Rect){L.left + 12, track_y(t) + L.lane_h - 6, L.head - 24, 3};
    return (SDL_Rect){L.left + 12, track_y(t) + L.lane_h - 14, L.head - 24, 6};
}

/* ---------------------------------------------------------------- project edits */

int clip_add(int sample, int track, long start, long offset, long length)
{
    if (P.nclips >= MAX_CLIPS) {
        status("Clip limit reached (%d)", MAX_CLIPS);
        return -1;
    }
    LOCK();
    P.clips[P.nclips] = (Clip){sample, track, start, offset, length, 0, 0, 0, 0, 0, 0};
    P.nclips++;
    UNLOCK();
    csel[P.nclips - 1] = 0;
    return P.nclips - 1;
}

/* ---- the selection */

void sel_clear(void)
{
    memset(csel, 0, sizeof csel);
    selected = -1;
}

void sel_only(int i)
{
    sel_clear();
    if (i >= 0 && i < P.nclips) {
        csel[i] = 1;
        selected = i;
    }
}

int sel_count(void)
{
    int n = 0;
    for (int i = 0; i < P.nclips; i++) n += csel[i];
    return n;
}

void sel_fix_primary(void)       /* keep `selected` pointing at a selected clip */
{
    if (selected >= 0 && selected < P.nclips && csel[selected]) return;
    selected = -1;
    for (int i = P.nclips - 1; i >= 0 && selected < 0; i--)
        if (csel[i]) selected = i;
}

void clip_delete(int i)
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

int delete_selection(void)       /* returns how many went */
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
int duplicate_selection(long shift)
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

void selection_span(long *a, long *b)
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
Clip clipboard[MAX_CLIPS];

int nclipboard;
int last_copy;                   /* what was copied last: a paste into a session slot takes that */

void copy_selection(void)
{
    long a, b;
    selection_span(&a, &b);
    nclipboard = 0;
    for (int i = 0; i < P.nclips; i++)
        if (csel[i]) {
            clipboard[nclipboard] = P.clips[i];
            clipboard[nclipboard++].start -= a;
        }
    if (nclipboard) last_copy = COPY_CLIPS;
}

int paste_at(long at)
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
long g_start[MAX_CLIPS];

unsigned char band_base[MAX_CLIPS];  /* the selection when a box drag began (Shift adds) */
int g_track[MAX_CLIPS];

void group_remember(void)
{
    for (int i = 0; i < P.nclips; i++) {
        g_start[i] = P.clips[i].start;
        g_track[i] = P.clips[i].track;
    }
}

/* the library list is showing and the pointer is over it (a drop target for clips) */
int over_library(void)
{
    return sounds_open && sounds_sub == 0 && L.pane_w && mx >= L.pane_x && mx < L.pane_x + L.pane_w && my >= L.top &&
           my < H - L.status;
}

void group_place(long df, int dt)
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

void preview(int sample)
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

T_MET MET = {0, CLICK_CLICK, 1, 1, 1, 1, 0.6f};
const char *const CLICK_NAMES[CLICK_SOUNDS] = {"Click", "Beep", "Wood"};
float *click_buf[3];             /* bar, beat, subdivision */
long click_len, click_pos = -1;
int click_kind, click_rate, click_sound = -1;
long countin_left, countin_total; /* frames of count-in still to play (audio thread) */
int pads_rec;

void metronome_prepare(void)     /* (re)make the click sounds at the project rate */
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
/* The next click at or after `pos` and what kind it is (0 accent, 1 beat, 2 subdivision). Song
 * clicks follow the tempo map: a click on each beat of the bar's time signature (eighths in 6/8),
 * the first one accented. The count-in counts a bar of the time signature where play starts. */
long next_click(long pos, int countin, int *kind)
{
    int sub = MET.sub < 1 ? 1 : MET.sub, num, den;
    long bs, bl;
    if (countin) {                             /* its own clock, from 0 */
        double bpm;
        meter_at(&P, P.playhead, &bpm, &num, &den, NULL, NULL, &bl);
        bs = pos / bl * bl;
    } else {
        meter_at(&P, pos, NULL, &num, &den, NULL, &bs, &bl);
    }
    double unit = (double)bl / (num * sub);
    long k = (long)ceil((pos - bs) / unit - 1e-9);
    if (k >= num * sub) {                       /* past the last one: the next bar's first */
        long nb = bs + bl;
        if (!countin) meter_at(&P, nb, NULL, &num, &den, NULL, &bs, &bl);
        else bs = nb;
        k = 0;
    }
    *kind = k == 0 && MET.accent ? 0 : k % sub == 0 ? 1 : 2;
    return bs + (long)llround(k * unit);
}

void metronome_mix_ex(float *out, long start, int n, int force, int countin)
{
    if ((!MET.on && !force) || !click_buf[0] || click_rate != SR) return;
    int kind;
    long next = next_click(start, countin, &kind);
    for (int i = 0; i < n; i++) {
        if (start + i == next) {
            click_pos = 0;
            click_kind = kind;
            next = next_click(start + i + 1, countin, &kind);
        }
        if (click_pos >= 0) {
            out[2 * i] += click_buf[click_kind][2 * click_pos] * MET.volume;
            out[2 * i + 1] += click_buf[click_kind][2 * click_pos + 1] * MET.volume;
            if (++click_pos >= click_len) click_pos = -1;
        }
    }
}

void metronome_mix(float *out, long start, int n, int force) { metronome_mix_ex(out, start, n, force, 0); }

long play_from;                  /* where playback last started: Stop goes back there */

void toggle_play(void)
{
    LOCK();
    P.playing = !P.playing;
    if (P.playing) play_from = P.playhead;
    if (P.playing) loud_reset(&live_loud);   /* the song's loudness, from here */
    countin_left = 0;
    if (P.playing && MET.countin > 0 && (!MET.countin_rec_only || pads_rec)) {
        countin_total = countin_left = MET.countin * bar_len_at(P.playhead);   /* a bar or two of clicks first */
        click_pos = -1;
    }
    UNLOCK();
}

Uint32 taps[8];
int ntaps;

void tap_tempo(void)             /* tap along: the average gap sets the tempo */
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

void view_show(long f)           /* scroll so frame f is in view (centred if it was off) */
{
    double x = frame_to_x(f);
    if (x < L.tl_x + 20 || x > L.tl_x + L.tl_w - 20) {
        view_start = (double)f / SR - L.tl_w / 2.0 / pps;
        if (view_start < 0) view_start = 0;
    }
}

void playhead_to(long f)
{
    LOCK();
    P.playhead = f < 0 ? 0 : f;
    UNLOCK();
    view_show(P.playhead);
}

void playhead_to_edge(int dir)   /* the next/previous clip start or end, on any track */
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

void zoom_to_fit(void)
{
    long end = project_end(&P);
    if (end <= 0) end = 8 * beat_frames();
    pps = (L.tl_w - 40) / ((double)end / SR);
    if (pps < 8) pps = 8;
    if (pps > 4000) pps = 4000;
    view_start = 0;
}

void transport_stop(void)         /* stop and go back to where play began; again: to the start */
{
    LOCK();
    int was = P.playing;
    P.playing = 0;
    countin_left = 0;
    P.playhead = was ? play_from : 0;
    UNLOCK();
}

void toggle_loop(void)
{
    LOCK();
    if (P.loop_end <= P.loop_start) {   /* no region yet: loop the first two bars */
        P.loop_start = 0;
        P.loop_end = 8 * beat_frames();
    }
    P.loop = !P.loop;
    UNLOCK();
}

int import_file(const char *path)        /* into the library: its index, or -1 */
{
    char err[200];
    int idx = sample_import(&P, path, err, sizeof err);
    if (idx < 0) {
        status("Could not import %s: %s", path, err);
    } else {
        status("Imported %s (%.2f s)", P.samples[idx].name, (double)P.samples[idx].frames / SR);
        lib_scroll = P.nsamples > 6 ? P.nsamples - 6 : 0;
    }
    return idx;
}

/* export dialog state: options persist for the session */
ExportOptions EXO;

int export_open, export_pending;
char export_dir[900];            /* where it goes: kept for the session once chosen */
char export_name[256];           /* the file name, without the extension */
char export_base_path[1200];     /* export_dir/export_name, made when exporting */
SDL_Rect export_dir_r, export_name_r, export_go_r;   /* where they were drawn, for the self-test */
char proj_path[1024];            /* "" = never saved */

/* A name typed with a format's extension picks that format, and loses the extension. */
void export_take_ext(char *name)
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

void export_make_base(void)
{
    size_t n = strlen(export_dir);
    int slash = n && (export_dir[n - 1] == '/' || export_dir[n - 1] == '\\');
    snprintf(export_base_path, sizeof export_base_path, "%s%s%s", export_dir, slash ? "" : "/",
             export_name[0] ? export_name : "untitled");
}

void open_export(void)
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

void do_export_now(void)
{
    char msg[1500];
    bends_update();
    export_make_base();
    if (project_export(&P, &EXO, export_base_path, msg, sizeof msg) == 0) status("%s", msg);
    else status("Export failed: %s", msg);
}

/* ---- editing a line of text at a caret (text fields and typed numbers). The caret is a byte index,
 * always on a UTF-8 character boundary. */

size_t ed_prev(const char *s, size_t i)
{
    if (!i) return 0;
    i--;
    while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--;
    return i;
}

size_t ed_next(const char *s, size_t i)
{
    size_t n = strlen(s);
    if (i >= n) return n;
    i++;
    while (i < n && ((unsigned char)s[i] & 0xC0) == 0x80) i++;
    return i;
}

int ed_wordch(char c) { return isalnum((unsigned char)c) || (unsigned char)c >= 0x80; }

/* Left / Right (Ctrl: a word at a time), Home / End, Backspace and Delete. 1 when it took the key. */
int ed_key(char *buf, size_t *caret, SDL_Keysym k)
{
    size_t n = strlen(buf), c = *caret > n ? n : *caret;
    int ctrl = (k.mod & (KMOD_CTRL | KMOD_GUI)) != 0;
    switch (k.sym) {
    case SDLK_LEFT:
        if (ctrl) {
            while (c > 0 && !ed_wordch(buf[c - 1])) c = ed_prev(buf, c);
            while (c > 0 && ed_wordch(buf[c - 1])) c = ed_prev(buf, c);
        } else c = ed_prev(buf, c);
        break;
    case SDLK_RIGHT:
        if (ctrl) {
            while (c < n && !ed_wordch(buf[c])) c = ed_next(buf, c);
            while (c < n && ed_wordch(buf[c])) c = ed_next(buf, c);
        } else c = ed_next(buf, c);
        break;
    case SDLK_HOME: case SDLK_UP: c = 0; break;
    case SDLK_END: case SDLK_DOWN: c = n; break;
    case SDLK_BACKSPACE:
        if (c > 0) {
            size_t p = ed_prev(buf, c);
            memmove(buf + p, buf + c, n - c + 1);
            c = p;
        }
        break;
    case SDLK_DELETE:
        if (c < n) {
            size_t q = ed_next(buf, c);
            memmove(buf + c, buf + q, n - q + 1);
        }
        break;
    default: return 0;
    }
    *caret = c;
    return 1;
}

void ed_insert(char *buf, size_t cap, size_t *caret, char ch)   /* one byte, at the caret */
{
    size_t n = strlen(buf), c = *caret > n ? n : *caret;
    if (n + 1 >= cap) return;
    memmove(buf + c + 1, buf + c, n - c + 1);
    buf[c] = ch;
    *caret = c + 1;
}

int prefix_w(const char *s, size_t i)     /* how wide the first i bytes are drawn */
{
    char tmp[1100];
    if (i >= sizeof tmp) i = sizeof tmp - 1;
    memcpy(tmp, s, i);
    tmp[i] = 0;
    return text_w(tmp);
}

size_t ed_caret_at(const char *s, int x0, int x)   /* the boundary nearest x, text drawn from x0 */
{
    size_t best = 0, i = 0, n = strlen(s);
    int bd = abs(x - x0);
    while (i < n) {
        i = ed_next(s, i);
        int d = abs(x - (x0 + prefix_w(s, i)));
        if (d < bd) bd = d, best = i;
    }
    return best;
}

/* ---- a one-line text field (the export folder and file name). Click to type; Enter, Tab or a
 * click elsewhere keeps it, Esc puts it back. */
T_tf tf;

int tf_drawn;                    /* the field being typed in was drawn last frame */

void tf_end(int keep)
{
    if (!tf.buf) return;
    if (!keep) snprintf(tf.buf, tf.cap, "%s", tf.orig);
    tf.buf = NULL;
    tf.caret = (size_t)-1;
}

void tf_insert(const char *t)
{
    if (!tf.buf) return;
    for (; *t; t++) {
        unsigned char c = (unsigned char)*t;
        if (c < 32 || (!tf.path && strchr("/\\:*?\"<>|", c))) continue;
        ed_insert(tf.buf, tf.cap, &tf.caret, (char)c);
    }
}

int tf_key(SDL_Keysym k)         /* 1 when the field took the key */
{
    if (!tf.buf) return 0;
    if (k.sym == SDLK_RETURN || k.sym == SDLK_KP_ENTER || k.sym == SDLK_TAB) tf_end(1);
    else if (k.sym == SDLK_ESCAPE) tf_end(0);
    else if ((k.mod & KMOD_CTRL) && k.sym == SDLK_v) {
        char *clip = SDL_GetClipboardText();
        if (clip) tf_insert(clip);
        SDL_free(clip);
    } else if ((k.mod & KMOD_CTRL) && (k.sym == SDLK_a || k.sym == SDLK_u)) {
        tf.buf[0] = 0;
        tf.caret = 0;
    } else ed_key(tf.buf, &tf.caret, k);
    return 1;
}

void textfield(SDL_Rect r, char *buf, size_t cap, int path, const char *placeholder)
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
    if (active && tf.caret > strlen(buf)) tf.caret = strlen(buf);
    int cw = active ? prefix_w(buf, tf.caret) : 0;
    /* where it's drawn from: the start, or far enough left that the caret is in view */
    int tx = r.x + 10;
    if (active && tw > room) {
        static int shift;                       /* how far it's scrolled, kept while the caret stays in view */
        if (cw - shift > room) shift = cw - room;
        if (cw - shift < 0) shift = cw;
        if (shift > tw - room) shift = tw - room;
        if (shift < 0) shift = 0;
        tx = r.x + 10 - shift;
    }
    if (active) tf.tx = tx;
    if (buf[0]) text_clip((SDL_Rect){r.x + 6, r.y, r.w - 12, r.h}, tx, r.y + (r.h - font_h) / 2, buf, TEXT);
    else if (!active) text(r.x + 10, r.y + (r.h - font_h) / 2, placeholder, DIM);
    if (active && (SDL_GetTicks() / 500) % 2 == 0) {
        int cx = tx + cw;
        if (cx > r.x + r.w - 6) cx = r.x + r.w - 6;
        if (cx < r.x + 4) cx = r.x + 4;
        fill((SDL_Rect){cx, r.y + 6, 2, r.h - 12}, ACCENT);
    }
    if (hov && lpress && active) {               /* a click inside: the caret goes there */
        lpress = 0;
        tf.caret = ed_caret_at(buf, tx, mx);
    }
    if (hov && lpress && !active) {
        lpress = 0;
        tf_end(1);
        tf.buf = buf;
        tf.cap = cap;
        tf.path = path;
        tf.r = r;
        tf_drawn = 1;
        tf.caret = ed_caret_at(buf, r.x + 10, mx);   /* where it was clicked */
        snprintf(tf.orig, sizeof tf.orig, "%s", buf);
        SDL_StartTextInput();
    }
}

void zoom(double factor, int anchor_x)
{
    double t = view_start + (anchor_x - L.tl_x) / pps;
    pps *= factor;
    if (pps < 8) pps = 8;
    if (pps > 4000) pps = 4000;
    view_start = t - (anchor_x - L.tl_x) / pps;
    if (view_start < 0) view_start = 0;
}

/* ---------------------------------------------------------------- widgets */

int button2(SDL_Rect r, const char *label, int on, int enabled)
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

int button(SDL_Rect r, const char *label, int on) { return button2(r, label, on, 1); }

/* ---- tooltips: hold the mouse still over something for a moment and it says what it is.
 * Widgets offer one with tip(); the last one under the mouse in a pass wins (the innermost), and
 * tip_draw() puts it on top at the end of the pass, inside the window, clear of the pointer. */
int tips_on = 1;

T_tipc tipc;
T_tip_seen tip_seen;
char tip_shown[300];

void db_text(float v, char *buf, size_t n)
{
    if (v < 0.0005f) snprintf(buf, n, "-inf");
    else snprintf(buf, n, "%+.1f dB", 20 * log10f(v));
}

void tip(SDL_Rect r, const char *fmt, ...)
{
    if (!tips_on || drag.kind != D_NONE || !hit(r)) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tipc.text, sizeof tipc.text, fmt, ap);
    va_end(ap);
    tipc.have = 1;
    tipc.r = r;
}

void tip_draw(int win_k)
{
    if (!tipc.have) {
        if (tip_seen.win == win_k) tip_seen.r.w = 0;
        return;
    }
    tipc.have = 0;
    Uint32 now = SDL_GetTicks();
    if (tip_seen.win != win_k || memcmp(&tip_seen.r, &tipc.r, sizeof tipc.r)) {
        tip_seen.win = win_k;
        tip_seen.r = tipc.r;
        tip_seen.since = now;
        tip_seen.pressed = 0;
    }
    if (lpress || ldown || rpress) tip_seen.pressed = 1;   /* a click puts it away until the mouse moves on */
    if (tip_seen.pressed || now - tip_seen.since < 550) return;
    snprintf(tip_shown, sizeof tip_shown, "%s", tipc.text);   /* (for the self-test) */
    /* word-wrapped to a comfortable width */
    char lines[8][160];
    int nl = 0, maxw = 0, limit = 340;
    const char *p = tipc.text;
    while (*p && nl < 8) {
        int n = 0, last_space = -1;
        char buf[160];
        while (p[n] && p[n] != '\n' && n < 159) {
            buf[n] = p[n];
            buf[n + 1] = 0;
            if (p[n] == ' ') last_space = n;
            if (text_w(buf) > limit && last_space > 0) {
                n = last_space;
                break;
            }
            n++;
        }
        snprintf(lines[nl], sizeof lines[nl], "%.*s", n, p);
        int lw = text_w(lines[nl]);
        maxw = lw > maxw ? lw : maxw;
        nl++;
        p += n;
        while (*p == ' ' || *p == '\n') p++;
    }
    int bw = maxw + 20, bh = nl * (font_h + 3) + 12;
    int x = mx + 14, y = my + 22;
    if (x + bw > W - 4) x = W - 4 - bw;
    if (x < 4) x = 4;
    if (y + bh > H - 4) y = my - bh - 10;         /* no room below: above the pointer */
    if (y < 4) y = 4;
    SDL_Rect box = {x, y, bw, bh};
    fill((SDL_Rect){x + 3, y + 3, bw, bh}, alpha(DARK, 120));
    fill(box, theme_light ? rgb(255, 252, 232) : rgb(44, 46, 54));
    outline(box, ACCENT);
    for (int i = 0; i < nl; i++) text(x + 10, y + 6 + i * (font_h + 3), lines[i], theme_light ? rgb(30, 30, 34) : rgb(236, 236, 240));
}

T_dd dd;

int dropdown(SDL_Rect r, const char *prefix, const char *const *items, int n, int *sel, int cycle)
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

void dd_layout(void)
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

SDL_Rect dd_item(int i)
{
    int rows = (dd.n + dd.cols - 1) / dd.cols, iw = (dd.box.w - 8) / dd.cols;
    return (SDL_Rect){dd.box.x + 4 + (i / rows) * iw, dd.box.y + 4 + (i % rows) * 26, iw, 26};
}

/* before anything else sees the click: a click on the open list picks, anywhere else closes it */
void dd_input(void)
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

void dd_draw(void)
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
int track_dropdown(SDL_Rect r, const char *prefix, int *track)
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
int subtabs(int x, int y, int w, const char *const *names, int n, int *sel)
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
void scrollbar(SDL_Rect area, int *scroll, int content_h)
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
    if (*scroll == 0 && !hit(area)) {            /* there's more: an arrow at the foot of the scrollbar, */
        SDL_Rect b = {trk.x - 3, area.y + area.h - 17, sw + 6, 14};   /* clear of anything written */
        fill(b, ACCENT);
        for (int i = 0; i < 4; i++)
            line(b.x + b.w / 2 - 3 + i, b.y + 5 + i, b.x + b.w / 2 + 3 - i, b.y + 5 + i, DARK);
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

void number_box(SDL_Rect r, const char *label, const char *value, int active)
{
    tip(r, "%s%s%s: drag sideways or use the mouse wheel to change it (Shift for fine steps), or click it and type",
        label, label[0] ? " " : "", value);
    int hov = hit(r) && (drag.kind == D_NONE || active);
    field_box(r, hov, active);
    text(r.x + 8, r.y + (r.h - font_h) / 2, label, DIM);
    text(r.x + r.w - 8 - text_w(value), r.y + (r.h - font_h) / 2, value, TEXT);
}

/* Typing a value: a click on a number box (one that doesn't drag) opens it for typing.
 * Enter or a click elsewhere sets it, Esc leaves it as it was. The value is handed back to the
 * widget the next time it's drawn, so float settings go through their usual proxy. */
T_numedit numedit;

void numedit_begin(double *v, double lo, double hi)
{
    numedit.active = 1;
    numedit.pending = 0;
    numedit.v = v;
    numedit.iv = NULL;
    numedit.lo = lo;
    numedit.hi = hi;
    numedit.buf[0] = 0;                  /* start empty: just type the new value */
    numedit.caret = 0;
    SDL_StartTextInput();
    status("Type a value (%g to %g), Enter to set it, Esc to cancel", lo, hi);
}

/* A typed note: a name like C4, F#3, Bb2 or Eb (octave 4 when left out), or a MIDI number.
 * Returns -1 when it isn't one. */
int parse_note(const char *s)
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

void numedit_end(int commit)
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

void numedit_text(const char *t)
{
    if (!numedit.active) return;
    for (; *t; t++) {
        const char *ok = numedit.iv ? "0123456789-ABCDEFGabcdefg#" : "0123456789.-";
        if (*t && strchr(ok, *t)) ed_insert(numedit.buf, sizeof numedit.buf, &numedit.caret, *t);
    }
}

int numedit_key(SDL_Keysym k)     /* returns 1 when typing took the key */
{
    if (!numedit.active) return 0;
    if (k.sym == SDLK_RETURN || k.sym == SDLK_KP_ENTER || k.sym == SDLK_TAB) numedit_end(1);
    else if (k.sym == SDLK_ESCAPE) numedit_end(0);
    else ed_key(numedit.buf, &numedit.caret, k);
    return 1;
}

/* the value being typed: right-aligned, with the caret where typing goes; a click inside moves it */
void numedit_draw(SDL_Rect r, const char *label)
{
    numedit.r = r;
    fill(r, PANEL);
    outline(r, ACCENT);
    outline((SDL_Rect){r.x + 1, r.y + 1, r.w - 2, r.h - 2}, ACCENT);
    text(r.x + 8, r.y + (r.h - font_h) / 2, label, DIM);
    if (numedit.caret > strlen(numedit.buf)) numedit.caret = strlen(numedit.buf);
    int tw = text_w(numedit.buf), tx = r.x + r.w - 10 - tw;
    text(tx, r.y + (r.h - font_h) / 2, numedit.buf, TEXT);
    if ((SDL_GetTicks() / 500) % 2 == 0) fill((SDL_Rect){tx + prefix_w(numedit.buf, numedit.caret), r.y + 6, 2, r.h - 12}, ACCENT);
    if (hit(r) && lpress) {
        lpress = 0;
        numedit.caret = ed_caret_at(numedit.buf, tx, mx);
    }
}

/* while typing into v: the text and a caret instead of the value; afterwards, the typed value */
int numedit_widget(SDL_Rect r, const char *label, double *v)
{
    if (numedit.pending && numedit.v == v) {
        *v = numedit.value;
        numedit.pending = 0;
    }
    if (!numedit.active || numedit.v != v) return 0;
    numedit_draw(r, label);
    return 1;
}

/* drag sideways or up/down to change; mouse wheel steps; Shift = fine; click to type */
/* like number(), but shows `shown` instead of formatting the value (log scales, L/R pan...) */
void number_text(SDL_Rect r, const char *label, double *v, double lo, double hi, double step,
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
void pan_text(float pan, char *buf, size_t n)
{
    int k = (int)lroundf(pan * 100);
    if (k == 0) snprintf(buf, n, "centre");
    else snprintf(buf, n, "%s %d", k < 0 ? "L" : "R", abs(k));
}

double *fproxy(float *f)
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

void fcommit(float *f, const double *d)
{
    if ((float)*d != *f) {
        LOCK();
        *f = (float)*d;
        UNLOCK();
    }
}

void fnumber(SDL_Rect r, const char *label, float *f, double lo, double hi, double step, const char *fmt)
{
    double *d = fproxy(f);
    char buf[48];
    snprintf(buf, sizeof buf, fmt, *d);
    number_text(r, label, d, lo, hi, step, buf);
    fcommit(f, d);
}

void number(SDL_Rect r, const char *label, double *v, double lo, double hi, double step,
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

void note_number(SDL_Rect r, const char *label, int *v)
{
    char buf[16];
    if (numedit.pending && numedit.iv == v) {
        *v = (int)numedit.value;
        numedit.pending = 0;
        numedit.iv = NULL;
    }
    if (numedit.active && numedit.iv == v) {     /* being typed into */
        numedit_draw(r, label);
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
