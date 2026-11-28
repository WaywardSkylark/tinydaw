/* tinydaw: mixing: faders, strips and the console. */
#include "ui.h"

/* ---------------------------------------------------------------- mixing: faders, strips, console
 *
 * The same controls in two layouts: rows in the Mixer panel's Levels tab, and channel strips side by
 * side in the pop-out console. Faders show dB; their travel is the square root of the gain, so 0 dB
 * sits about four fifths up and the quiet end has room. Double-click puts a fader back to 0 dB. */

/* what a fader moves: track t, the master (-1), or bus b (-2 - b) */
float *fader_target(int t) { return t <= -2 ? &P.buses[-2 - t].volume : t < 0 ? &P.master : &P.tracks[t].volume; }

/* A fader for track t (-1: master), across `r` (or up it, when vertical). Returns its value. */
float fader(SDL_Rect r, int t, int vertical)
{
    float *v = fader_target(t);
    int first, automated = t >= 0 && auto_lane(&P, t, AUTO_VOLUME, &first) > 0;
    float shown = *v;
    if (automated) {                            /* automation decides: show what it says now */
        int has;
        shown = auto_to_value(AUTO_VOLUME, auto_at(&P, t, AUTO_VOLUME, P.playhead, &has));
    }
    float p = sqrtf(shown / 1.5f);
    p = p < 0 ? 0 : p > 1 ? 1 : p;
    {
        char db[24];
        db_text(shown, db, sizeof db);
        tip(r, automated ? "%s: %s, set by its automation lane" : "%s: %s. Drag, or the wheel for half a dB (Shift: a tenth); double-click for 0 dB",
            t <= -2 ? P.buses[-2 - t].name : t < 0 ? "Master" : P.tracks[t].name, db);
    }
    int active = drag.kind == D_FADER && drag.track == t, hov = hit(r);
    fill(r, tone(PANEL, 0.72f));
    Col col = automated ? ACCENT : t >= 0 ? TRACK_COL[t % MAX_TRACKS] : t <= -2 ? tone(ACCENT, 0.8f) : HEAD;
    float unity = sqrtf(1 / 1.5f);              /* a mark at 0 dB */
    if (vertical) {
        int h = (int)(r.h * p);
        fill((SDL_Rect){r.x + r.w / 2 - 2, r.y, 4, r.h}, tone(PANEL3, 0.9f));
        fill((SDL_Rect){r.x + r.w / 2 - 2, r.y + r.h - h, 4, h}, col);
        int uy = r.y + r.h - (int)(r.h * unity);
        line(r.x, uy, r.x + r.w, uy, DIM);
        SDL_Rect cap = {r.x + 2, r.y + r.h - h - 7, r.w - 4, 14};
        fill(cap, active ? HEAD : hov ? tone(col, 1.25f) : col);
        outline(cap, DARK);
    } else {
        int w = (int)(r.w * p);
        fill((SDL_Rect){r.x, r.y + r.h / 2 - 2, r.w, 4}, tone(PANEL3, 0.9f));
        fill((SDL_Rect){r.x, r.y + r.h / 2 - 2, w, 4}, col);
        int ux = r.x + (int)(r.w * unity);
        line(ux, r.y, ux, r.y + r.h, DIM);
        SDL_Rect cap = {r.x + w - 6, r.y + 2, 12, r.h - 4};
        fill(cap, active ? HEAD : hov ? tone(col, 1.25f) : col);
        outline(cap, DARK);
    }
    if (drag.kind == D_NONE && hov) {
        if (automated && (lpress || wheel_y)) {
            status("This track's volume is automated: edit its lane (A on the track), or clear it to use the fader");
            lpress = 0;
            wheel_y = 0;
        } else if (lpress && ldouble) {
            lpress = 0;
            LOCK();
            *v = 1.0f;
            UNLOCK();
            status("%s back to 0 dB", t <= -2 ? P.buses[-2 - t].name : t < 0 ? "Master" : P.tracks[t].name);
        } else if (lpress) {
            lpress = 0;
            drag = (typeof(drag)){.kind = D_FADER, .track = t, .sample = vertical, .sx = r.x, .sy = r.y, .a = r.w, .b = r.h};
            double q = vertical ? 1.0 - (double)(my - r.y) / r.h : (double)(mx - r.x) / r.w;
            q = q < 0 ? 0 : q > 1 ? 1 : q;
            LOCK();
            *v = (float)(1.5 * q * q);
            UNLOCK();
        } else if (wheel_y) {                   /* half a dB a notch (Shift: a tenth) */
            float db = *v > 0.0005f ? 20 * log10f(*v) : -60;
            db += wheel_y * ((mods & KMOD_SHIFT) ? 0.1f : 0.5f);
            float nv = db <= -60 ? 0 : powf(10, db / 20);
            LOCK();
            *v = nv > 1.5f ? 1.5f : nv;
            UNLOCK();
            wheel_y = 0;
        }
    }
    return shown;
}

void mute_solo(SDL_Rect m, SDL_Rect so, int t)
{
    Track *tr = &P.tracks[t];
    if (button(m, "M", tr->mute)) {
        LOCK();
        tr->mute = !tr->mute;
        UNLOCK();
    }
    if (button(so, "S", tr->solo)) {
        LOCK();
        tr->solo = !tr->solo;
        UNLOCK();
    }
}

void pan_box(SDL_Rect r, const char *label, int t)
{
    Track *tr = &P.tracks[t];
    double *d = fproxy(&tr->pan);
    char buf[24];
    pan_text((float)*d, buf, sizeof buf);
    number_text(r, label, d, -1, 1, 0.005, buf);
    fcommit(&tr->pan, d);
}

/* The Levels tab: a row a track (fader, meter, pan, mute, solo), then the master. */
int levels_tab(int x, int y, int w)
{
    char buf[48];
    if (button((SDL_Rect){x, y, w, 28}, pops[POP_MIXER].win ? "The mixing console is open (in its own window)"
                                                             : "Open the mixing console in its own window", 0))
        mixwin_open();
    y += 38;
    int nw = 92, fx0 = x + nw + 58, fw = w - (fx0 - x) - 120;
    for (int t = 0; t < P.ntracks; t++) {
        Track *tr = &P.tracks[t];
        fill((SDL_Rect){x, y + 2, 4, 30}, TRACK_COL[t]);
        text_clip((SDL_Rect){x + 8, y, nw - 14, 18}, x + 8, y, tr->name, sel_track == t ? HEAD : TEXT);
        if (hit((SDL_Rect){x, y, nw, 34}) && lpress) {   /* a click on the name: its Track settings */
            lpress = 0;
            sel_track = t;
            mixer_sub = 2;
        }
        float gr = mixer_track_gr(MX, t);
        snprintf(buf, sizeof buf, "%s%s", tr->comp_on ? "comp " : "", fabsf(tr->width - 1) > 0.01f ? "width" : "");
        text_clip((SDL_Rect){x + 8, y + 17, nw - 14, 16}, x + 8, y + 17, gr > 0.1f ? "comp working" : buf, DIM);
        mute_solo((SDL_Rect){x + nw, y + 4, 24, 24}, (SDL_Rect){x + nw + 26, y + 4, 24, 24}, t);
        lv_fader[t] = (SDL_Rect){fx0, y + 2, fw, 18};
        float v = fader(lv_fader[t], t, 0);
        meter_draw((SDL_Rect){fx0, y + 23, fw, 6}, &track_meter[t], 0);
        db_text(v, buf, sizeof buf);
        text(fx0 + fw + 8, y + 3, buf, TEXT);
        pan_box((SDL_Rect){fx0 + fw + 8, y + 20, 104, 14}, "", t);
        y += 40;
    }
    for (int b = 0; b < P.nbuses; b++) {      /* the buses: a click on the name opens it under Buses */
        Bus *bs = &P.buses[b];
        fill((SDL_Rect){x, y + 2, 4, 30}, tone(ACCENT, 0.8f));
        text_clip((SDL_Rect){x + 8, y, nw - 14, 18}, x + 8, y, bs->name, TEXT);
        snprintf(buf, sizeof buf, "bus%s", bs->rack.n ? ", rack" : "");
        text_clip((SDL_Rect){x + 8, y + 17, nw - 14, 16}, x + 8, y + 17, buf, DIM);
        if (hit((SDL_Rect){x, y, nw, 34}) && lpress) {
            lpress = 0;
            sel_bus = b;
            mixer_sub = 4;
        }
        if (button((SDL_Rect){x + nw, y + 4, 50, 24}, "M", bs->mute)) {
            LOCK();
            bs->mute = !bs->mute;
            UNLOCK();
        }
        float bv = fader((SDL_Rect){fx0, y + 2, fw, 18}, -2 - b, 0);
        meter_draw((SDL_Rect){fx0, y + 23, fw, 6}, &bus_meter[b], 0);
        db_text(bv, buf, sizeof buf);
        text(fx0 + fw + 8, y + 3, buf, TEXT);
        y += 40;
    }
    line(x, y, x + w, y, LINE);
    y += 8;
    text(x + 8, y + 2, "Master", HEAD);
    float v = fader((SDL_Rect){fx0, y + 2, fw, 18}, -1, 0);
    meter_draw((SDL_Rect){fx0, y + 23, fw, 3}, &master_meter[0], 0);
    meter_draw((SDL_Rect){fx0, y + 27, fw, 3}, &master_meter[1], 0);
    db_text(v, buf, sizeof buf);
    text(fx0 + fw + 8, y + 3, buf, TEXT);
    y += 42;
    y = loudness_readout(x, y, w);
    y += 6;
    text(x, y, "Drag a fader, or use the wheel over it (half a dB a", DIM);
    text(x, y + font_h + 2, "notch); double-click puts it back to 0 dB. Click a", DIM);
    text(x, y + 2 * (font_h + 2), "name for that track's effects.", DIM);
    return y + 3 * (font_h + 2) + 10;
}

/* ---- the pop-out console */

int console_scroll;

/* One channel strip: name, EQ, compressor, width, sends, pan, a tall fader and meter, mute, solo. */
void mix_strip(SDL_Rect r, int t)
{
    Track *tr = &P.tracks[t];
    char buf[48];
    fill(r, sel_track == t ? tone(PANEL2, 1.12f) : PANEL2);
    outline(r, LINE);
    fill((SDL_Rect){r.x, r.y, r.w, 4}, TRACK_COL[t]);
    SDL_Rect name = {r.x + 4, r.y + 6, r.w - 8, font_h + 4};
    text_clip(name, r.x + 6, r.y + 7, tr->name, sel_track == t ? HEAD : TEXT);
    if (hit(name) && lpress) {
        lpress = 0;
        sel_track = t;
        status("Track %d: %s (its details are in the Mixer panel's Track tab)", t + 1, tr->name);
    }
    int x = r.x + 4, w = r.w - 8, y = r.y + font_h + 14, rh = 24;
    fnumber((SDL_Rect){x, y, w, 20}, "Lo", &tr->eq[0], -12, 12, 0.05, "%+.0f");
    y += rh;
    fnumber((SDL_Rect){x, y, w, 20}, "Mid", &tr->eq[1], -12, 12, 0.05, "%+.0f");
    y += rh;
    fnumber((SDL_Rect){x, y, w, 20}, "Hi", &tr->eq[2], -12, 12, 0.05, "%+.0f");
    y += rh + 4;
    float gr = mixer_track_gr(MX, t);
    if (button((SDL_Rect){x, y, w, 20}, tr->comp_on ? (gr > 0.1f ? (snprintf(buf, sizeof buf, "Comp -%.0f", gr), buf) : "Comp on")
                                                     : "Comp", tr->comp_on)) {
        LOCK();
        tr->comp_on = !tr->comp_on;
        UNLOCK();
    }
    y += rh;
    fnumber((SDL_Rect){x, y, w, 20}, "Thr", &tr->comp_thresh, -48, 0, 0.1, "%.0f");
    y += rh;
    fnumber((SDL_Rect){x, y, w, 20}, "Rat", &tr->comp_ratio, 1, 20, 0.02, "%.1f");
    y += rh + 4;
    fnumber((SDL_Rect){x, y, w, 20}, "Wid", &tr->width, 0, 2, 0.004, "%.2f");
    y += rh;
    fnumber((SDL_Rect){x, y, w, 20}, "Dly", &tr->delay_send, 0, 1, 0.004, "%.2f");
    y += rh;
    fnumber((SDL_Rect){x, y, w, 20}, "Rev", &tr->reverb_send, 0, 1, 0.004, "%.2f");
    y += rh;
    pan_box((SDL_Rect){x, y, w, 20}, "", t);
    y += rh + 6;
    int bottom = r.y + r.h - 56, fh = bottom - y;
    if (fh > 40) {
        cs_fader[t] = (SDL_Rect){x, y, w - 16, fh};
        float v = fader(cs_fader[t], t, 1);
        meter_draw((SDL_Rect){x + w - 12, y, 12, fh}, &track_meter[t], 1);
        db_text(v, buf, sizeof buf);
        text_center((SDL_Rect){r.x, bottom + 4, r.w, font_h + 2}, buf, TEXT);
    }
    int bw = (w - 4) / 2;
    cs_mute[t] = (SDL_Rect){x, r.y + r.h - 28, bw, 24};
    mute_solo(cs_mute[t], (SDL_Rect){x + bw + 4, r.y + r.h - 28, bw, 24}, t);
}

/* a bus's strip: its name, what's in its rack, mute, a tall fader and its meter */
void bus_strip(SDL_Rect r, int b)
{
    Bus *bs = &P.buses[b];
    char buf[48];
    fill(r, tone(PANEL2, 0.95f));
    outline(r, LINE);
    fill((SDL_Rect){r.x, r.y, r.w, 4}, tone(ACCENT, 0.8f));
    SDL_Rect name = {r.x + 4, r.y + 6, r.w - 8, font_h + 4};
    text_clip(name, r.x + 6, r.y + 7, bs->name, TEXT);
    tip(name, "%s: a bus. Its rack, sends and the tracks playing into it are in the Mixer panel's Buses tab", bs->name);
    int x = r.x + 4, w = r.w - 8, y = r.y + font_h + 14;
    snprintf(buf, sizeof buf, "%d effect%s", bs->rack.n, bs->rack.n == 1 ? "" : "s");
    text_clip((SDL_Rect){x, y, w, font_h + 2}, x + 2, y, b < 2 ? (b == BUS_DELAY ? "the delay" : "the reverb") : "bus", DIM);
    text_clip((SDL_Rect){x, y + font_h + 4, w, font_h + 2}, x + 2, y + font_h + 4, buf, DIM);
    y += 2 * font_h + 14;
    int bottom = r.y + r.h - 56, fh = bottom - y;
    if (fh > 40) {
        float v = fader((SDL_Rect){x, y, w - 16, fh}, -2 - b, 1);
        meter_draw((SDL_Rect){x + w - 12, y, 12, fh}, &bus_meter[b], 1);
        db_text(v, buf, sizeof buf);
        text_center((SDL_Rect){r.x, bottom + 4, r.w, font_h + 2}, buf, TEXT);
    }
    if (button((SDL_Rect){x, r.y + r.h - 28, w, 24}, bs->mute ? "Muted" : "Mute", bs->mute)) {
        LOCK();
        bs->mute = !bs->mute;
        UNLOCK();
    }
}

void master_strip(SDL_Rect r)
{
    char buf[48];
    MasterFx *fx = &P.fx;
    fill(r, tone(PANEL2, 1.08f));
    outline(r, ACCENT);
    text_center((SDL_Rect){r.x, r.y + 6, r.w, font_h + 4}, "Master", HEAD);
    int x = r.x + 6, w = r.w - 12, y = r.y + font_h + 16;
    static const char *const PRE[3] = {"Off", "Glue", "Loud"};
    int cur = !fx->comp_on && !fx->limit_on ? 0 : fx->comp_on && fx->comp_ratio <= 2.6f && fx->comp_thresh >= -19 ? 1
            : fx->comp_on ? 2 : -1;
    for (int i = 0; i < 3; i++)
        if (button((SDL_Rect){x, y + i * 24, w, 20}, PRE[i], cur == i)) {
            LOCK();
            if (i == 0) fx->comp_on = fx->limit_on = 0;
            else if (i == 1) {
                fx->comp_on = fx->limit_on = 1;
                fx->comp_thresh = -18, fx->comp_ratio = 2.5f, fx->comp_attack = 15, fx->comp_release = 180;
                fx->comp_makeup = 3, fx->limit_ceiling = -1;
            } else {
                fx->comp_on = fx->limit_on = 1;
                fx->comp_thresh = -24, fx->comp_ratio = 4, fx->comp_attack = 8, fx->comp_release = 120;
                fx->comp_makeup = 8, fx->limit_ceiling = -1;
            }
            UNLOCK();
        }
    y += 3 * 24 + 6;
    float cgr, lgr;
    mixer_dynamics_meter(MX, &cgr, &lgr);
    snprintf(buf, sizeof buf, "comp -%.1f", cgr);
    text(x, y, buf, cgr > 0.1f ? HEAD : DIM);
    snprintf(buf, sizeof buf, "limit -%.1f", lgr);
    text(x, y + font_h + 2, buf, lgr > 0.1f ? HEAD : DIM);
    y += 2 * font_h + 10;
    double lu[3] = {loud_momentary(&live_loud), loud_short(&live_loud), loud_integrated(&live_loud)};
    static const char *const LN[3] = {"now", "3 s", "song"};
    for (int i = 0; i < 3; i++) {
        if (isfinite(lu[i])) snprintf(buf, sizeof buf, "%s %.1f", LN[i], lu[i]);
        else snprintf(buf, sizeof buf, "%s --", LN[i]);
        text(x, y + i * (font_h + 2), buf, i == 2 ? TEXT : DIM);
    }
    text(x, y + 3 * (font_h + 2), "LUFS", DIM);
    y += 4 * (font_h + 2) + 8;
    int bottom = r.y + r.h - 56, fh = bottom - y;
    if (fh > 40) {
        float v = fader((SDL_Rect){x, y, w - 24, fh}, -1, 1);
        meter_draw((SDL_Rect){x + w - 20, y, 9, fh}, &master_meter[0], 1);
        meter_draw((SDL_Rect){x + w - 9, y, 9, fh}, &master_meter[1], 1);
        db_text(v, buf, sizeof buf);
        text_center((SDL_Rect){r.x, bottom + 4, r.w, font_h + 2}, buf, TEXT);
    }
}

void console_draw(void)
{
    fill((SDL_Rect){0, 0, W, H}, PANEL);
    text(14, 10, "MIXING CONSOLE", DIM);
    text(170, 10, "drag faders (double-click: 0 dB), click a number to type it, click a name for its details", DIM);
    int sw = 96, gap = 6, top = 34, h = H - top - 10, mw = 112;
    int avail = W - mw - 3 * gap - 10, total = (P.ntracks + P.nbuses) * (sw + gap);
    int maxs = total - avail > 0 ? total - avail : 0;
    if (wheel_y && my > top && my < top + 22 + font_h) {   /* the wheel over the names scrolls the strips */
        console_scroll -= wheel_y * (sw + gap);
        wheel_y = 0;
    }
    if (wheel_x) {
        console_scroll -= wheel_x * (sw + gap);
        wheel_x = 0;
    }
    console_scroll = console_scroll < 0 ? 0 : console_scroll > maxs ? maxs : console_scroll;
    SDL_Rect area = {10, top, avail, h};
    SDL_RenderSetClipRect(ren, &area);
    for (int t = 0; t < P.ntracks; t++) {
        SDL_Rect r = {10 + t * (sw + gap) - console_scroll, top, sw, h};
        if (r.x + r.w < area.x || r.x > area.x + area.w) continue;
        int savedp = lpress;                     /* strips scrolled out of view take no clicks */
        if (mx < area.x || mx >= area.x + area.w) lpress = 0;
        mix_strip(r, t);
        if (mx < area.x || mx >= area.x + area.w) lpress = savedp;
    }
    for (int b = 0; b < P.nbuses; b++) {         /* the buses, after the tracks */
        SDL_Rect r = {10 + (P.ntracks + b) * (sw + gap) - console_scroll, top, sw, h};
        if (r.x + r.w < area.x || r.x > area.x + area.w) continue;
        int savedp = lpress;
        if (mx < area.x || mx >= area.x + area.w) lpress = 0;
        bus_strip(r, b);
        if (mx < area.x || mx >= area.x + area.w) lpress = savedp;
    }
    SDL_RenderSetClipRect(ren, NULL);
    if (maxs > 0) {                              /* a scrollbar under the strips */
        int bw = area.w * area.w / total, bx = area.x + (int)((long)(area.w - bw) * console_scroll / maxs);
        fill((SDL_Rect){area.x, H - 8, area.w, 4}, PANEL3);
        fill((SDL_Rect){bx, H - 8, bw, 4}, DIM);
    }
    master_strip((SDL_Rect){W - mw - 10, top, mw, h});
}

void mixwin_open(void)
{
    if (pop_open(POP_MIXER, "Mixing console - tinydaw", 1080, 680, 520, 560))
        status("The mixing console is in its own window: drag it anywhere, Esc closes it");
}

void mixwin_frame(void)
{
    if (pops[POP_MIXER].win) pop_pass(POP_MIXER, console_draw);
}

void editor_post(void)              /* remember the clip, to find it again next frame */
{
    editor_draw();
    if (ewin && ed_clip >= 0 && ed_clip < P.nclips) {   /* (a library sound has nothing to find again) */
        ed_last = P.clips[ed_clip];
        ed_nclips = P.nclips;
    }
}

void editor_frame(void)
{
    if (!ewin) return;
    if (!editor_find()) {
        editor_close();
        return;
    }
    pop_pass(POP_EDITOR, editor_post);
}

void frame(void)
{
    static int min_for = -1;            /* the pane needs a wider window (or the size changed) */
    int need = 70 + (theory_open || pads_open || drums_open || mixer_open || vis_open || sounds_open ? PANE_W : settings_open ? 440 : 0) + 520;
    if (min_for != need + (int)(ui_user * 1000) * 10000) {              /* the panel plus a usable timeline */
        min_for = need + (int)(ui_user * 1000) * 10000;
        SDL_SetWindowMinimumSize(win, (int)(need * ui_user), (int)(560 * ui_user));
        int ww, wh;
        SDL_GetWindowSize(win, &ww, &wh);
        if (ww < need * ui_user) SDL_SetWindowSize(win, (int)(need * ui_user), wh);
    }
    if (fabsf(window_density(win) * ui_user - ui_px) > 0.01f) ui_apply();   /* moved to a screen with another density */
    SDL_RenderSetScale(ren, ui_px, ui_px);       /* every frame: a resize can put the renderer back to 1 */
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
    key_sync();
    sess_track();
    if (drag.kind == D_NONE) bends_update();   /* (a curve being drawn is heard once let go) */
    dd_input();
    if (numedit.active && (lpress || rpress) && !hit(numedit.r)) numedit_end(1);
    if (tf.buf && (((lpress || rpress) && !hit(tf.r)) || !tf_drawn)) tf_end(1);   /* clicked away, or it's gone */
    tf_drawn = 0;

    /* while the export dialog is open, the rest of the window gets no input */
    int smx = mx, smy = my, slp = lpress, srp = rpress, swx = wheel_x, swy = wheel_y;
    int picking = SDL_AtomicGet(&fd_state) != 0;
    int modal = export_open || export_pending || menu_open || met_open || confirm_act || picking || cm.open || chp_open || browse_open;
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
            if (session_view) session_grid();
            else {
                timeline();
                session_lane_marks();
            }
        }
        rail();
        if (theory_open) theory_pane();
        else if (pads_open) pads_pane();
        else if (drums_open) drums_pane();
        else if (mixer_open) mixer_pane();
        else if (vis_open) vis_pane();
        else if (settings_open) settings_pane();
        else if (sounds_open) sounds_pane();
        if (wheel_y && drag.kind == D_NONE && L.pane_w && hit(panel_rect())) {
            /* a wheel that no box or list in the panel took scrolls the panel (the panel
             * takes it last, so number and note boxes can be stepped with the wheel) */
            int *sc = theory_open ? &th_scroll : pads_open ? &pads_scroll : drums_open ? &drums_scroll : mixer_open ? &mixer_scroll
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
    if (browse_open && !picking && !confirm_act) browse_draw();   /* over everything else */
    if (cm.open && !picking) cm_draw();
    if (menu_open && !picking) file_menu();
    if (met_open && !picking) metronome_menu();
    if (chp_open && !picking) chord_picker();
    dd_draw();
    if (confirm_act && !picking) confirm_dialog();
    if (picking) {
        lpress = rpress = 0;
        waiting_overlay("Choose a file in the file dialog...");
    }
    if (!picking && !show_open) tip_draw(-1);   /* the tooltip, on top of everything */
    else tipc.have = 0;

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

    if (dd.changed_for == &recent_pick) {    /* a recent project was picked from the list */
        dd.changed_for = NULL;
        if (recent_pick >= 0 && recent_pick < nrecent) request(ACT_OPEN_PATH, recent[recent_map_[recent_pick]]);
    }
    if (dd.changed_for == &example_pick) {   /* an example was picked from the list */
        dd.changed_for = NULL;
        if (example_pick >= 0 && example_pick < nexamples) request(ACT_OPEN_EXAMPLE, example_path[example_pick]);
    }
    input_update();
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
    tcheck_report("main window");
}

/* ---- the command line's --part and --fills: parts written into a project without a window, e.g.
 *   --part "bass,style=Walking,busy=0.5,octave=0,seed=3,sound=Upright bass,bars=4-12,track=Bass,gain=-3"
 *   --fills "style=Build,len=0.6,where=sections,track=Drums,seed=2"
 * (sound is a Generator preset's name, bars are 1-based and the end is exclusive, track a track's name) */

int cli_track(const char *name)
{
    for (int t = 0; t < P.ntracks; t++)
        if (!strcasecmp(P.tracks[t].name, name)) return t;
    return -1;
}

int cli_spec(const char *spec, int fills)
{
    char buf[512];
    snprintf(buf, sizeof buf, "%s", spec);
    int kind = -1, style = -1;
    double busy = 0.5, gain = 0;
    int octave = 0, where = 0, track = -1, bend = -1;
    unsigned seed = 1;
    pt_force_preset = pt_force_track = -1;
    pt_from = pt_to = -1;
    for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
        char *eq = strchr(tok, '=');
        if (!eq) {
            for (int k = 0; k < PART_KINDS; k++)
                if (!strcasecmp(tok, (const char *[]){"chords", "bass", "melody"}[k])) kind = k;
            if (kind < 0 && !fills) {
                fprintf(stderr, "tinydaw: unknown part %s (chords, bass or melody)\n", tok);
                return -1;
            }
            continue;
        }
        *eq = 0;
        const char *k = tok, *v = eq + 1;
        if (!strcmp(k, "style")) {
            int n = fills ? FILL_KINDS : kind >= 0 ? PART_NSTYLES[kind] : 0;
            for (int i = 0; i < n; i++)
                if (!strcasecmp(v, fills ? FILL_STYLES[i] : PART_STYLES[kind][i])) style = i;
            if (style < 0) {
                fprintf(stderr, "tinydaw: unknown style %s\n", v);
                return -1;
            }
        } else if (!strcmp(k, "busy") || !strcmp(k, "len")) busy = atof(v);
        else if (!strcmp(k, "octave")) octave = atoi(v);
        else if (!strcmp(k, "seed")) seed = (unsigned)atol(v);
        else if (!strcmp(k, "gain")) gain = atof(v);
        else if (!strcmp(k, "bend")) {
            static const char *const BN[CBP_COUNT] = {"flat", "scoops", "fall-offs", "vibrato", "rise", "dive"};
            for (int i = 0; i < CBP_COUNT; i++)
                if (!strcasecmp(v, BN[i])) bend = i;
            if (bend < 0) {
                fprintf(stderr, "tinydaw: bend=%s: scoops, fall-offs, vibrato, rise or dive\n", v);
                return -1;
            }
        }
        else if (!strcmp(k, "where")) where = !strcmp(v, "4") ? 1 : !strcmp(v, "8") ? 2 : 0;
        else if (!strcmp(k, "bars")) {
            int a = 0, b = 0;
            if (sscanf(v, "%d-%d", &a, &b) != 2 || a < 1 || b <= a) {
                fprintf(stderr, "tinydaw: bars=%s: give them as first-end, like 5-13\n", v);
                return -1;
            }
            pt_from = meter_beats(&P, meter_bar_frame(&P, a - 1));
            pt_to = meter_beats(&P, meter_bar_frame(&P, b - 1));
        } else if (!strcmp(k, "sound")) {
            for (int i = 0; i < NSYNTH_PRESETS && pt_force_preset < 0; i++)
                if (!strcasecmp(SYNTH_PRESETS[i].name, v)) pt_force_preset = i;
            if (pt_force_preset < 0) {
                fprintf(stderr, "tinydaw: no Generator sound called %s\n", v);
                return -1;
            }
        } else if (!strcmp(k, "track")) {
            track = cli_track(v);
            if (track < 0) {
                fprintf(stderr, "tinydaw: no track called %s\n", v);
                return -1;
            }
        } else {
            fprintf(stderr, "tinydaw: unknown setting %s\n", k);
            return -1;
        }
    }
    int rc = 0;
    if (fills) {
        fl_style = style >= 0 ? style : 0;
        fl_busy = busy;
        fl_where = where;
        fl_track = track;
        fl_seed = seed;
        fl_n = 0;
        rc = fills_make(0) > 0 ? 0 : -1;
    } else {
        if (kind < 0) {
            fprintf(stderr, "tinydaw: --part needs chords, bass or melody first\n");
            return -1;
        }
        pt_style[kind] = style >= 0 ? style : 0;
        pt_busy[kind] = busy;
        pt_octave[kind] = octave;
        pt_seed[kind] = seed;
        pt_where = 0;
        pt_force_track = track;
        int c = part_make(kind, 0);
        if (c >= 0) P.clips[c].gain_db = (float)gain;
        if (c >= 0 && bend > 0) {               /* a pitch bend preset on the part (part_make selected it) */
            char keep[sizeof status_msg];
            snprintf(keep, sizeof keep, "%s", status_msg);
            cb_preset(bend);
            snprintf(status_msg, sizeof status_msg, "%s", keep);
        }
        rc = c >= 0 ? 0 : -1;
    }
    pt_force_preset = pt_force_track = -1;
    pt_from = pt_to = -1;
    fprintf(rc ? stderr : stdout, "%s\n", status_msg);
    return rc;
}
