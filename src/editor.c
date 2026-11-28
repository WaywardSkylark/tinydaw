/* tinydaw: the clip editor window. */
#include "ui.h"

/* ---------------------------------------------------------------- the clip editor window
 *
 * Double-clicking an audio clip opens it in a window of its own: a big waveform of the whole
 * sound with the clip's part lit (drag its edges to trim, its top corners to fade, the wheel to
 * zoom, click to hear from there), the clip settings from the Mixer, exact numbers, and actions.
 * It's drawn by the same immediate-mode code as the main window: for its pass the renderer,
 * size and mouse are swapped for the editor's. */

void pop_close(int k)
{
    Pop *pw = &pops[k];
    if (!pw->win) return;
    if (drag_pop == k) {
        drag.kind = D_NONE;
        drag_pop = -1;
    }
    text_forget(pw->ren);
    SDL_DestroyRenderer(pw->ren);
    SDL_DestroyWindow(pw->win);
    pw->win = NULL;
    pw->ren = NULL;
}

/* Opens pop-out k (or raises it): a resizable window with its own renderer. 0 when it can't. */
int pop_open(int k, const char *title, int w, int h, int minw, int minh)
{
    Pop *pw = &pops[k];
    if (pw->win) {
        SDL_SetWindowTitle(pw->win, title);
        SDL_RaiseWindow(pw->win);
        return 1;
    }
    pw->win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, (int)(w * ui_user),
                               (int)(h * ui_user), SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!pw->win) {
        status("Couldn't open a window: %s", SDL_GetError());
        return 0;
    }
    pw->ren = SDL_CreateRenderer(pw->win, -1, SDL_RENDERER_ACCELERATED);
    if (!pw->ren) pw->ren = SDL_CreateRenderer(pw->win, -1, SDL_RENDERER_SOFTWARE);
    if (!pw->ren) {
        SDL_DestroyWindow(pw->win);
        pw->win = NULL;
        status("Couldn't open a window: %s", SDL_GetError());
        return 0;
    }
    SDL_SetRenderDrawBlendMode(pw->ren, SDL_BLENDMODE_BLEND);
    SDL_RenderSetScale(pw->ren, ui_px, ui_px);
    SDL_SetWindowMinimumSize(pw->win, (int)(minw * ui_user), (int)(minh * ui_user));
    pw->id = SDL_GetWindowID(pw->win);
    memset(&pw->in, 0, sizeof pw->in);
    pw->in.mx = pw->in.my = -1000;
    return 1;
}

/* A pop-out's pass: its renderer, size and mouse in place of the main window's while `draw` runs.
 * Drags that start in it are followed and ended here. */
void pop_pass(int k, void (*draw)(void))
{
    Pop *pw = &pops[k];
    if (!pw->win) return;
    SDL_Renderer *m_ren = ren;
    int mW = W, mH = H, smx = mx, smy = my, sld = ldown, slp = lpress, slr = lrel, srp = rpress, swx = wheel_x,
        swy = wheel_y, sdb = ldouble;
    ren = pw->ren;
    SDL_RenderSetScale(ren, ui_px, ui_px);
    logical_size(ren, &W, &H);
    mx = pw->in.mx;
    my = pw->in.my;
    ldown = pw->in.ldown;
    lpress = pw->in.lpress;
    lrel = pw->in.lrel;
    rpress = pw->in.rpress;
    wheel_x = pw->in.wheel_x;
    wheel_y = pw->in.wheel_y;
    ldouble = pw->in.ldouble;
    int had = drag.kind != D_NONE;
    if (numedit.active && (lpress || rpress) && !hit(numedit.r)) numedit_end(1);
    SDL_SetRenderDrawColor(ren, BG.r, BG.g, BG.b, 255);
    SDL_RenderClear(ren);
    ntlog = 0;
    draw();
    if (pw->win) tip_draw(k);
    tcheck_report(k == POP_EDITOR ? "clip editor" : "mixing console");
    if (pw->win) {
        if (drag_pop == k && drag.kind != D_NONE && ldown) update_drag();
        if (drag_pop == k && lrel && drag.kind != D_NONE) end_drag();
        if (!had && drag.kind != D_NONE) drag_pop = k;
        if (drag.kind == D_NONE && drag_pop == k) drag_pop = -1;
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
    pw->in.lpress = pw->in.lrel = pw->in.rpress = pw->in.wheel_x = pw->in.wheel_y = 0;
}

void editor_close(void)
{
    if (!ewin) return;
    pop_close(POP_EDITOR);
    ed_clip = -1;
}

Clip *ed_c(void) { return ed_clip == ED_LIB ? &ed_lib : &P.clips[ed_clip]; }

void editor_view_clip(void)
{
    const Clip *c = ed_c();
    long frames = P.samples[c->sample].frames, pad = c->length / 6 + SR / 20;
    ed_v0 = c->offset - pad < 0 ? 0 : c->offset - pad;
    ed_v1 = c->offset + c->length + pad > frames ? frames : c->offset + c->length + pad;
    if (ed_v1 <= ed_v0) ed_v1 = ed_v0 + 1;
}

void editor_open(int c)
{
    if (c < 0 || c >= P.nclips || P.clips[c].pattern > 0) return;
    sel_only(c);
    ed_clip = c;
    ed_last = P.clips[c];
    ed_nclips = P.nclips;
    editor_view_clip();
    char title[160];
    snprintf(title, sizeof title, "%s - clip editor - tinydaw", P.samples[sample_base(&P, P.clips[c].sample)].name);
    if (!pop_open(POP_EDITOR, title, 840, 660, 640, 560)) return;
    status("Editing \"%s\" in its own window", P.samples[sample_base(&P, P.clips[c].sample)].name);
}

/* A library sound in the editor: trim it, fade it, change its gain, pitch or direction, and keep
 * the result as a new sound (the original stays as it is). */
void editor_open_sound(int s)
{
    if (s < 0 || s >= P.nsamples) return;
    ed_clip = ED_LIB;
    ed_lib = (Clip){.sample = s, .track = sel_track >= 0 && sel_track < P.ntracks ? sel_track : 0, .length = P.samples[s].frames};
    editor_view_clip();
    char title[160];
    snprintf(title, sizeof title, "%s - clip editor - tinydaw", P.samples[s].name);
    if (!pop_open(POP_EDITOR, title, 840, 660, 640, 560)) return;
    status("Editing the sound \"%s\": Save as a new sound keeps what you make of it", P.samples[s].name);
}

/* the clip the editor shows may have moved in the list (clips deleted, undo): find it again */
int editor_find(void)
{
    if (ed_clip == ED_LIB) return ed_lib.sample >= 0 && ed_lib.sample < P.nsamples && P.samples[ed_lib.sample].data;
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

long ed_x_to_frame(int x)
{
    long f = ed_v0 + (long)((double)(x - ed_wave.x) * (ed_v1 - ed_v0) / (ed_wave.w > 1 ? ed_wave.w : 1));
    long frames = P.samples[ed_c()->sample].frames;
    return f < 0 ? 0 : f > frames ? frames : f;
}

int ed_frame_to_x(long f) { return ed_wave.x + (int)((double)(f - ed_v0) * ed_wave.w / (ed_v1 - ed_v0)); }

void editor_drag(void)            /* a handle on the waveform, following the mouse */
{
    if (ed_clip != ED_LIB && (ed_clip < 0 || ed_clip >= P.nclips)) return;
    Clip *c = ed_c();
    const Clip *o = &ed_orig;
    long f = ed_x_to_frame(mx), frames = P.samples[c->sample].frames, min = SR / 100;
    LOCK();
    switch (ed_handle) {
    case 1: {                            /* the start: the end stays put */
        long off = f < 0 ? 0 : f > o->offset + o->length - min ? o->offset + o->length - min : f;
        long d = off - o->offset;
        if (o->start + d < 0 && ed_clip != ED_LIB) d = -o->start;
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

void editor_play_region(long from)  /* hear the clip from `from` (a frame in its sound) */
{
    const Clip *c = ed_c();
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

void editor_draw(void)
{
    char buf[200];
    int lib = ed_clip == ED_LIB;
    Clip *c = ed_c();
    const Sample *s = &P.samples[c->sample];
    int x = 16, w = W - 32, y = 12, hw, tw;
    fill((SDL_Rect){0, 0, W, H}, PANEL);
    snprintf(buf, sizeof buf, "%s", P.samples[sample_base(&P, c->sample)].name);
    text(x, y + 4, buf, HEAD);
    if (lib)
        snprintf(buf, sizeof buf, "a library sound   keeping %.2f s of %.2f s", (double)c->length / SR, (double)s->frames / SR);
    else
        snprintf(buf, sizeof buf, "track %d (%s)   %.2f s of a %.2f s sound", c->track + 1, P.tracks[c->track].name,
                 (double)c->length / SR, (double)s->frames / SR);
    text(x + text_w(P.samples[sample_base(&P, c->sample)].name) + 16, y + 4, buf, DIM);
    if (button((SDL_Rect){W - 16 - 90, y, 90, 28}, "Close", 0)) {
        editor_close();
        return;
    }
    y += 40;

    /* the waveform: the whole sound, the clip's part lit */
    int wh = H - y - 350 - (lib ? 70 : 0);
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
    if (!lib && P.playing && P.playhead >= c->start && P.playhead < c->start + c->length) {   /* where the song is */
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
    double bf = beat_frames();
    static double st_b, len_b, off_ms;
    if (lib) {                                   /* which part of the sound is kept */
        static double from_ms, len_ms;
        y = section(x, y, w, "THE PART TO KEEP");
        if (!(drag.kind == D_NUM && drag.num == &from_ms) && !(numedit.active && numedit.v == &from_ms)) from_ms = c->offset * 1000.0 / SR;
        if (!(drag.kind == D_NUM && drag.num == &len_ms) && !(numedit.active && numedit.v == &len_ms)) len_ms = c->length * 1000.0 / SR;
        number((SDL_Rect){x, y, tw, 26}, "From", &from_ms, 0, s->frames * 1000.0 / SR, 2, "%.0f ms");
        number((SDL_Rect){x + tw + 8, y, tw, 26}, "Length", &len_ms, 10, (s->frames - c->offset) * 1000.0 / SR, 2, "%.0f ms");
        long no = (long)llround(from_ms * SR / 1000), nl = (long)llround(len_ms * SR / 1000);
        if (no > s->frames - SR / 100) no = s->frames - SR / 100;
        if (no < 0) no = 0;
        if (nl > s->frames - no) nl = s->frames - no;
        if (nl < SR / 100) nl = SR / 100;
        if (no != c->offset || nl != c->length) {
            c->offset = no;
            c->length = nl;
            if (c->fade_in + c->fade_out > c->length) c->fade_in = c->fade_out = 0;
        }
        if (button((SDL_Rect){x + 2 * (tw + 8), y, w - 2 * (tw + 8), 26}, "The whole sound", 0)) {
            c->offset = 0;
            c->length = s->frames;
            editor_view_clip();
        }
        y += 42;
        y = section(x, y, w, "VELOCITY LAYERS AND ROUND-ROBINS");
        {                                        /* on the library sound itself */
            int b = sample_base(&P, c->sample);
            Sample *bs = &P.samples[b];
            int nv = bs->nvel > 0 ? bs->nvel : 1, nr = bs->nrr > 0 ? bs->nrr : 1, bw = 30, want_v = nv, want_r = nr;
            char nb[8];
            text(x, y + 6, "Layers", DIM);
            int lx = x + text_w("Layers") + 8;
            for (int i = 1; i <= MAX_VEL; i++) {
                snprintf(nb, sizeof nb, "%d", i);
                SDL_Rect r = {lx + (i - 1) * (bw + 3), y, bw, 26};
                tip(r, i == 1 ? "One sound at every velocity" : "%d velocity layers: softer hits play a darker, gentler copy", i);
                ed_layer_r[i - 1] = r;
                if (button(r, nb, nv == i)) want_v = i;
            }
            int rx = lx + MAX_VEL * (bw + 3) + 14;
            text(rx, y + 6, "Round-robins", DIM);
            rx += text_w("Round-robins") + 8;
            for (int i = 1; i <= MAX_RR; i++) {
                snprintf(nb, sizeof nb, "%d", i);
                SDL_Rect r = {rx + (i - 1) * (bw + 3), y, bw, 26};
                tip(r, i == 1 ? "Every hit the same" : "%d slightly different copies taken in turn, so repeats don't sound machine-made", i);
                ed_rr_r[i - 1] = r;
                if (button(r, nb, nr == i)) want_r = i;
            }
            int hx = rx + MAX_RR * (bw + 3) + 14;
            SDL_Rect hb = {hx, y, x + w - hx, 26};
            tip(hb, "Each layer from soft to hard, then the round-robins of the hardest");
            if (hb.w > 60 && button(hb, "Hear them", 0)) {
                long each = SR * 6 / 10, gap = SR / 8, n = (long)(nv + nr) * (each + gap);
                float *buf = calloc((size_t)n * 2, sizeof(float));
                if (buf) {
                    long at = 0;
                    for (int k = 0; k < nv + nr - 1; k++) {
                        int l = k < nv ? k : nv - 1, r = k < nv ? 0 : k - nv + 1;
                        int si = bs->nvel > 0 ? bs->layer[l][r] : b;
                        const Sample *ls = &P.samples[si >= 0 && si < P.nsamples ? si : b];
                        float g = (float)(l + 1) / nv;
                        for (long i = 0; i < each && i < ls->frames; i++) {
                            float e = each - i < SR / 50 ? (float)(each - i) / (SR / 50) : 1;
                            buf[2 * (at + i)] = ls->data[2 * i] * g * e;
                            buf[2 * (at + i) + 1] = ls->data[2 * i + 1] * g * e;
                        }
                        at += each + gap;
                    }
                    play_scratch(buf, at);
                }
            }
            if (want_v != nv || want_r != nr) {
                if (sample_make_layers(&P, b, want_v, want_r) == 0)
                    status(want_v * want_r > 1 ? "\"%s\": %d velocity layer%s, %d round-robin%s"
                                               : "\"%s\": one sound at every velocity again",
                           bs->name, want_v, want_v == 1 ? "" : "s", want_r, want_r == 1 ? "" : "s");
                else
                    status("Library is full");
            }
            y += 34;
            text(x, y, "Drum patterns, note clips, pads and MIDI keys pick a layer by how hard each hit is.", DIM);
            y += font_h + 14;
        }
    } else {
    y = section(x, y, w, "WHERE IT PLAYS");
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
    }

    y = section(x, y, w, "HOW IT SOUNDS");
    fnumber((SDL_Rect){x, y, tw, 26}, "Gain", &c->gain_db, -36, 12, 0.05, "%+.1f dB");
    static double semis;
    if (!(drag.kind == D_NUM && drag.num == &semis) && !(numedit.active && numedit.v == &semis)) semis = clip_semis(c);
    number((SDL_Rect){x + tw + 8, y, tw, 26}, "Pitch", &semis, -24, 24, 0.08, "%+.0f st");
    if (drag.kind != D_NUM && (int)lround(semis) != clip_semis(c)) clip_variant(ed_clip, (int)lround(semis), clip_rev(c));
    c = ed_c();
    s = &P.samples[c->sample];
    if (button((SDL_Rect){x + 2 * (tw + 8), y, tw, 26}, "Reverse", clip_rev(c))) clip_variant(ed_clip, clip_semis(c), !clip_rev(c));
    c = ed_c();
    s = &P.samples[c->sample];
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

    if (lib) {                                   /* a library sound's actions */
        static const char *const LACT[4] = {"Play it", "Normalize (peak -1 dB)", "Save as a new sound", "Put on the timeline"};
        int bw4 = (w - 3 * 6) / 4, la = -1;
        for (int i = 0; i < 4; i++) {
            ed_btn[i] = (SDL_Rect){x + i * (bw4 + 6), y, i == 3 ? w - 3 * (bw4 + 6) : bw4, 30};
            if (button(ed_btn[i], LACT[i], i == 2)) la = i;
        }
        tip(ed_btn[2], "Adds what you hear (this part, its gain, fades, pitch and direction) to the library as a sound "
                       "of its own. The original stays as it was");
        tip(ed_btn[3], "A clip of this part on track %d at the playhead", c->track + 1);
        switch (la) {
        case 0: editor_play_region(c->offset); break;
        case 1: {
            float lo, hi;
            sample_minmax(s, c->offset, c->offset + c->length, &lo, &hi);
            float pk = fmaxf(fabsf(lo), fabsf(hi));
            if (pk > 1e-6f) c->gain_db = fmaxf(-36, fminf(12, -1 - 20 * log10f(pk)));
            break;
        }
        case 2: {
            long n = c->length;
            float *b = malloc((size_t)n * 2 * sizeof(float));
            if (!b) break;
            float g = powf(10, c->gain_db / 20);
            for (long i = 0; i < n; i++) {
                float e = 1;
                if (c->fade_in > 0 && i < c->fade_in) e = (float)i / c->fade_in;
                if (c->fade_out > 0 && n - i < c->fade_out) e = fminf(e, (float)(n - i) / c->fade_out);
                b[2 * i] = s->data[2 * (c->offset + i)] * g * e;
                b[2 * i + 1] = s->data[2 * (c->offset + i) + 1] * g * e;
            }
            char dir[900], path[1200], name[96], err[300], base[90];
            user_dir("music", dir, sizeof dir);
            snprintf(dir + strlen(dir), sizeof dir - strlen(dir), "/tinydaw/Edited sounds");
            ensure_dir(dir);
            snprintf(name, sizeof name, "%.80s (edited)", P.samples[sample_base(&P, c->sample)].name);
            snprintf(base, sizeof base, "%.80s", name);
            for (char *q = base; *q; q++)
                if (strchr("/\\:*?\"<>|", *q) || (unsigned char)*q < 32) *q = '_';
            snprintf(path, sizeof path, "%s/%s.wav", dir, base);
            for (int k = 2; file_exists(path) && k < 1000; k++) snprintf(path, sizeof path, "%s/%s %d.wav", dir, base, k);
            int ns = sample_record(&P, b, n, path, name, err, sizeof err);
            free(b);
            if (ns < 0) status("Couldn't keep it: %s", err);
            else {
                lib_flash = ns;
                lib_flash_at = SDL_GetTicks();
                status("\"%s\" is in the library (%.2f s)", name, (double)n / SR);
            }
            break;
        }
        case 3: {
            int t = c->track < P.ntracks ? c->track : 0;
            int k = clip_add(c->sample, t, snap(P.playhead), c->offset, c->length);
            if (k >= 0) {
                LOCK();
                P.clips[k].gain_db = c->gain_db;
                P.clips[k].fade_in = c->fade_in;
                P.clips[k].fade_out = c->fade_out;
                UNLOCK();
                status("Put it on %s at the playhead", P.tracks[t].name);
            }
            break;
        }
        }
        y += 40;
        text(x, y, "Drag the edges to trim, the squares on top to fade, the wheel to zoom (Shift to scroll).", DIM);
        text(x, y + font_h + 3, "Click the waveform to hear it from there. Esc closes.", DIM);
        return;
    }

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
        double b0 = meter_bars(&P, c->start);
        long n = lround(meter_bars(&P, c->start + c->length) - b0);
        if (n < 1) n = 1;
        long len = meter_bar_frame(&P, b0 + n) - c->start, full = len;
        if (len > s->frames - c->offset) len = s->frames - c->offset;
        LOCK();
        c->length = len;
        if (c->fade_in + c->fade_out > c->length) c->fade_in = c->fade_out = 0;
        UNLOCK();
        status("Length: %ld bar%s%s", n, n == 1 ? "" : "s", len < full ? " (as much as the sound has)" : "");
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
