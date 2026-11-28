/* tinydaw: the chord track, Create > Parts and the timeline. */
#include "ui.h"

SDL_Rect chd_rect_[MAX_CHORDS], chp_box;
double chd_at0, chd_menu_beat;    /* a dragged chord's start as it was; where the strip was right-clicked */

double chd_beat_at_x(int x)        /* the beat under x, on the Snap grid (Shift: off), to a 16th at least */
{
    double fr = x_to_frame_raw(x);
    long f = snap(fr > 0 ? (long)fr : 0);
    double b = floor(meter_beats(&P, f) * 4 + 0.5) / 4;
    return b < 0 ? 0 : b;
}

double chd_bar_beats(double beat)  /* how long the bar at `beat` is, in quarter notes */
{
    int num, den;
    meter_at(&P, meter_beats_frame(&P, beat), NULL, &num, &den, NULL, NULL, NULL);
    return num * 4.0 / den;
}

double chd_bar_start(double beat) { return meter_beats(&P, bar_floor(meter_beats_frame(&P, beat + 1e-6))); }

int chd_bar_of(double beat) { return (int)floor(meter_bars(&P, meter_beats_frame(&P, beat)) + 1e-4); }

/* frames round: a little slack */

double chd_next_after(double beat) /* where the next chord starts, or far away */
{
    for (int i = 0; i < P.nchords; i++)
        if (P.chords[i].at > beat + 1e-9) return P.chords[i].at;
    return 1e9;
}

void chd_hear(const SongChord *c)
{
    int notes[8], n = chord_voice(c, NULL, 0, 52, 72, notes);
    notes[n++] = 36 + (c->bass >= 0 ? c->bass : c->root);   /* and the bass under it */
    preview_notes(notes, n, 2);
}

void chd_where(const SongChord *c, char *buf, size_t n)   /* "bar 3, beat 2" */
{
    int bar = chd_bar_of(c->at);
    double into = floor((c->at - meter_beats(&P, meter_bar_frame(&P, bar))) * 4 + 0.5) / 4;   /* to a 16th */
    if (into <= 0) snprintf(buf, n, "bar %d", bar + 1);
    else snprintf(buf, n, "bar %d, beat %g", bar + 1, into + 1);
}

/* A good next chord in the key: I, V, vi, IV round again, after whatever came before. */
void chd_suggest(double at, int *root, int *type)
{
    int kr, ks, deg = 0, prev = -1;
    key_at_beat(&P, at, &kr, &ks);
    for (int i = 0; i < P.nchords; i++)
        if (P.chords[i].at < at) prev = i;
    if (prev >= 0 && SCALES[ks].n == 7) {
        static const int NEXT[7] = {4, 4, 5, 0, 5, 3, 0};
        int iv = ((P.chords[prev].root - kr) % 12 + 12) % 12;
        for (int d = 0; d < 7; d++)
            if (SCALES[ks].steps[d] == iv) deg = NEXT[d];
    }
    char num[16];
    int t, r = diatonic_chord(kr, ks, deg, th_sevenths, &t, num, sizeof num);
    if (r < 0) {
        r = kr;
        t = SCALES[ks].minor ? CH_MIN : CH_MAJ;
    }
    *root = r;
    *type = t;
}

int chd_add(double at, double len)  /* a suggested chord, picker open on it */
{
    int r, t;
    chd_suggest(at, &r, &t);
    int i = chord_put(&P, at, len, r, t, -1);
    if (i < 0) {
        status("The chord track is full (%d chords)", MAX_CHORDS);
        return -1;
    }
    chd_sel = chp_idx = i;
    chp_open = 1;
    chd_hear(&P.chords[i]);
    char nm[32], wh[48];
    chord_label(&P, &P.chords[i], nm, sizeof nm);
    chd_where(&P.chords[i], wh, sizeof wh);
    status("%s at %s, %g beats. Pick another chord here, drag it to move it, drag its end to resize it", nm, wh, P.chords[i].len);
    return i;
}

void chd_add_here(void)            /* the + in the header: a bar's chord at the playhead */
{
    double at = chd_bar_start(meter_beats(&P, P.playhead));
    int in = chord_at(&P, at);
    if (in >= 0) at = P.chords[in].at + P.chords[in].len;   /* one there already: straight after it */
    double len = chd_bar_beats(at), next = chd_next_after(at - 1e-9);
    if (at + len > next) len = next - at;
    if (len < 0.25) {
        status("No room for a chord there: there's one straight after");
        return;
    }
    chd_add(at, len);
}

void open_key_tab(void)
{
    show_panel(RI_THEORY);
    theory_tab = 0;
    th_scroll = 0;
}

/* Theory's key is the key where the playhead is: following it as the playhead moves, and setting it
 * (that stretch of the song's) when changed in Theory. */
void key_sync(void)
{
    int bar = (int)floor(meter_bars(&P, P.playhead) + 1e-9), r, sc;
    if (bar < 0) bar = 0;
    key_at_bar(&P, bar, &r, &sc);
    if (th_synced_root >= 0 && (th_root != th_synced_root || th_scale != th_synced_scale)) {
        if (r != th_root || sc != th_scale) key_set(&P, bar, th_root, th_scale);
    } else if (r != th_root || sc != th_scale) {
        th_root = r;
        th_scale = sc;
    }
    th_synced_root = th_root;
    th_synced_scale = th_scale;
}

void key_name(int root, int scale, char *buf, size_t n)
{
    snprintf(buf, n, "%s %s", pitch_name(root, key_uses_flats(root, scale)), SCALES[scale].name);
}

void chd_key_change(double beat)
{
    int bar = chd_bar_of(beat);
    if (bar < 1) {
        open_key_tab();
        status("The key at the start is the song's key: pick it in Theory > Keys");
        return;
    }
    int k = key_change_add(&P, bar);
    if (k < 0) {
        status("A song can have %d key changes", MAX_KEYS);
        return;
    }
    playhead_to(meter_bar_frame(&P, bar));
    open_key_tab();
    status("A key change at bar %d: pick the new key in Theory > Keys (right-click the flag to remove it)", bar + 1);
}

void chord_header(void)
{
    SDL_Rect hr = {L.left, L.top + L.ruler + L.arr, L.head, L.chd};
    fill(hr, tone(PANEL, 0.86f));
    line(L.left, hr.y + hr.h - 1, L.left + L.head, hr.y + hr.h - 1, LINE);
    int tw = text(L.left + 12, hr.y + (hr.h - font_h) / 2, "CHORDS", DIM);
    SDL_Rect sa = {L.left + L.head - 32, hr.y + 2, 22, hr.h - 4};
    tip(sa, "A chord at the playhead's bar. Or click along the strip");
    if (button(sa, "+", 0)) chd_add_here();
    int bar = (int)floor(meter_bars(&P, P.playhead) + 1e-9), kr, ks;
    key_at_bar(&P, bar < 0 ? 0 : bar, &kr, &ks);
    char kn[40];
    key_name(kr, ks, kn, sizeof kn);
    SDL_Rect kb = {L.left + 12 + tw + 8, hr.y + 2, sa.x - 4 - (L.left + 12 + tw + 8), hr.h - 4};
    if (kb.w > 10) {
        int hov = hit(kb) && drag.kind == D_NONE;
        if (hov) fill(kb, PANEL3);
        text_clip(kb, kb.x + 4, hr.y + (hr.h - font_h) / 2, kn, hov ? TEXT : HEAD);
        tip(kb, "The key here: %s. Click to change it in Theory > Keys. Right-click the chord strip for a key change", kn);
        if (hov && lpress) {
            lpress = 0;
            open_key_tab();
        }
    }
}

void cm_open_list(int kind, int idx, const char *const *labels, const int *acts, const int *enabled, int n)
{
    memset(&cm, 0, sizeof cm);
    cm.open = 1;
    cm.kind = kind;
    cm.idx = idx;
    int w = 0;
    for (int k = 0; k < n && k < 10; k++) {
        snprintf(cm.label[k], sizeof cm.label[k], "%s", labels[k]);
        cm.act[k] = acts[k];
        cm.enabled[k] = enabled ? enabled[k] : 1;
        w = text_w(labels[k]) > w ? text_w(labels[k]) : w;
    }
    cm.n = n < 10 ? n : 10;
    w += 28;
    int h = cm.n * (font_h + 12) + 8;
    cm.box = (SDL_Rect){mx + 2, my + 2, w, h};
    if (cm.box.x + w > W - 4) cm.box.x = W - 4 - w;
    if (cm.box.y + h > H - 4) cm.box.y = H - 4 - h;
}

void chord_strip(void)
{
    SDL_Rect strip = {L.tl_x, L.top + L.ruler + L.arr, L.tl_w, L.chd};
    fill(strip, tone(PANEL, 0.86f));
    line(L.tl_x, strip.y + strip.h - 1, W, strip.y + strip.h - 1, LINE);
    int over = -1, edge = -1;
    double now = meter_beats(&P, P.playhead);
    if (chd_sel >= P.nchords) chd_sel = -1;
    for (int i = 0; i < P.nchords; i++) {
        const SongChord *c = &P.chords[i];
        int x0 = (int)frame_to_x(meter_beats_frame(&P, c->at)), x1 = (int)frame_to_x(meter_beats_frame(&P, c->at + c->len));
        SDL_Rect r = {x0, strip.y + 2, x1 - x0 - 1 > 2 ? x1 - x0 - 1 : 2, strip.h - 4};
        chd_rect_[i] = r;
        if (r.x + r.w < L.tl_x || r.x > W) continue;
        int kr, ks;
        key_at_beat(&P, c->at, &kr, &ks);
        Col col = TRACK_COL[(((c->root - kr) % 12 + 12) % 12 * 7) % 12];   /* a fifth apart, a colour apart */
        int here = now >= c->at && now < c->at + c->len;
        int dragged = drag.kind == D_CHD && drag.clip == i && abs(mx - drag.sx) > 4;
        fill(r, dragged ? alpha(col, 90) : wash(col, here ? 0.85f : i == chd_sel ? 0.75f : 0.55f));
        if (i == chd_sel) outline(r, TEXT);
        else if (here) outline(r, HEAD);
        char nm[32], num[16];
        chord_label(&P, c, nm, sizeof nm);
        chord_numeral(&P, c, num, sizeof num);
        int lx = r.x > L.tl_x ? r.x : L.tl_x;
        for (int k = 0; k < P.nkeys; k++)          /* a key change's flag starts here: the name goes after it */
            if (fabs(meter_beats(&P, meter_bar_frame(&P, P.keys[k].bar)) - c->at) < 1e-6) {
                char kn[40];
                key_name(P.keys[k].root, P.keys[k].scale, kn, sizeof kn);
                if (lx < r.x + text_w(kn) + 12) lx = r.x + text_w(kn) + 12;
            }
        SDL_Rect clipr = {lx, r.y, r.x + r.w - lx - 2, r.h};
        int tw = text_w(nm);
        if (clipr.w > 12) {                        /* cut short like a clip's name; the tooltip has it all */
            text_clip(clipr, lx + 5, r.y + (r.h - font_h) / 2, nm, DARK);
            if (clipr.w > tw + text_w(num) + 16) text_clip(clipr, lx + 5 + tw + 7, r.y + (r.h - font_h) / 2, num, alpha(DARK, 150));
        }
        if (hit(r) && drag.kind == D_NONE) {
            over = i;
            if (mx >= r.x + r.w - 5) edge = i;
            char wh[48];
            chd_where(c, wh, sizeof wh);
            tip(r, "%s (%s), %s, %g beats. Click to hear it, double-click to change it, drag to move it, drag its right "
                   "end to resize, right-click for more", nm, num, wh, c->len);
        }
    }
    for (int k = 0; k < P.nkeys; k++) {          /* key changes: a flag each */
        int x = (int)frame_to_x(meter_bar_frame(&P, P.keys[k].bar));
        if (x < L.tl_x - 120 || x > W) continue;
        char kn[40];
        key_name(P.keys[k].root, P.keys[k].scale, kn, sizeof kn);
        SDL_Rect f = {x, strip.y, text_w(kn) + 10, font_h + 2};
        int hov = hit(f) && drag.kind == D_NONE;
        fill((SDL_Rect){x - 1, strip.y, 2, strip.h}, ACCENT);
        fill(f, hov ? ACCENT : alpha(ACCENT, 200));
        text(f.x + 5, f.y + 1, kn, DARK);
        if (hov) {
            over = -2;
            tip(f, "From bar %d the key is %s. Click to go there and change it in Theory > Keys; right-click removes the "
                   "key change", P.keys[k].bar + 1, kn);
            if (lpress) {
                lpress = 0;
                playhead_to(meter_bar_frame(&P, P.keys[k].bar));
                open_key_tab();
            } else if (rpress) {
                rpress = 0;
                key_change_remove(&P, k);
                status("Removed the key change at bar %d", P.keys[k].bar + 1);
                break;
            }
        }
    }
    if (!P.nchords && drag.kind != D_CHDNEW)
        text_clip(strip, L.tl_x + 8, strip.y + (strip.h - font_h) / 2,
                  "Click here to add a chord, or put in a progression with Theory > Keys > Place", tone(DIM, 0.8f));
    if (hit(strip) && drag.kind == D_NONE && over == -1)
        tip(strip, "The song's chords. Click to add one here, drag along to set how long; right-click for a key change, "
                   "chords from a note clip, or a part made from the chords");
    if (hit(strip) && drag.kind == D_NONE) SDL_SetCursor(edge >= 0 ? cur_resize : cur_arrow);

    /* the mouse */
    if (hit(strip) && drag.kind == D_NONE && lpress && over != -2) {
        lpress = 0;
        if (over >= 0 && ldouble) {
            chd_sel = chp_idx = over;
            chp_open = 1;
        } else if (edge >= 0) {
            drag = (typeof(drag)){.kind = D_CHDEND, .clip = edge, .sx = mx, .sy = my};
        } else if (over >= 0) {
            drag = (typeof(drag)){.kind = D_CHD, .clip = over, .sx = mx, .sy = my};
            chd_at0 = P.chords[over].at;
        } else {
            drag = (typeof(drag)){.kind = D_CHDNEW, .sx = mx, .sy = my};
            chd_at0 = chd_beat_at_x(mx);
        }
    }
    if (hit(strip) && drag.kind == D_NONE && rpress && over != -2) {
        rpress = 0;
        double fr = x_to_frame_raw(mx);
        chd_menu_beat = meter_beats(&P, fr > 0 ? (long)fr : 0);
        if (over >= 0) {
            chd_sel = over;
            static const char *const LB[5] = {"Change the chord", "Hear it", "Copy it in after itself", "A key change at its bar",
                                              "Remove it"};
            static const int AC[5] = {CMA_CCHG, CMA_CHEAR, CMA_CDUP, CMA_CKEY, CMA_CDEL};
            cm_open_list(CM_CHD, over, LB, AC, NULL, 5);
        } else {
            static const char *const LB[4] = {"A key change here", "Chords from the selected note clip",
                                              "Make a chord part (Create > Parts)", "Remove every chord"};
            static const int AC[4] = {CMA_CKEY, CMA_CDETECT, CMA_CPART, CMA_CCLEAR};
            int en[4] = {1, selected >= 0 && selected < P.nclips && P.clips[selected].pattern > 0, P.nchords > 0, P.nchords > 0};
            cm_open_list(CM_CHDS, -1, LB, AC, en, 4);
        }
    }
    /* drags in progress */
    if (drag.kind == D_CHDNEW) {
        double a = chd_at0, b = chd_beat_at_x(mx);
        if (b < a) {
            double t = a;
            a = b;
            b = t;
        }
        if (b - a < 0.25) b = a + 0.25;
        int x0 = (int)frame_to_x(meter_beats_frame(&P, a)), x1 = (int)frame_to_x(meter_beats_frame(&P, b));
        SDL_Rect g = {x0, strip.y + 2, x1 - x0, strip.h - 4};
        fill(g, alpha(ACCENT, 110));
        outline(g, ACCENT);
    } else if (drag.kind == D_CHDEND && drag.clip < P.nchords) {
        SongChord *c = &P.chords[drag.clip];
        double len = chd_beat_at_x(mx) - c->at, next = chd_next_after(c->at);
        if (len < 0.25) len = 0.25;
        if (c->at + len > next) len = next - c->at;
        c->len = len;
    } else if (drag.kind == D_CHD && drag.clip < P.nchords && abs(mx - drag.sx) > 4) {
        const SongChord *c = &P.chords[drag.clip];
        double at = chd_beat_at_x(chd_rect_[drag.clip].x + (mx - drag.sx));
        int x0 = (int)frame_to_x(meter_beats_frame(&P, at)), x1 = (int)frame_to_x(meter_beats_frame(&P, at + c->len));
        SDL_Rect g = {x0, strip.y + 2, x1 - x0, strip.h - 4};
        char nm[32];
        chord_label(&P, c, nm, sizeof nm);
        fill(g, alpha(ACCENT, 170));
        outline(g, TEXT);
        text_clip(g, g.x + 5, g.y + (g.h - font_h) / 2, nm, DARK);
        SDL_SetCursor(cur_hand);
    }
}

void chord_release(void)
{
    char nm[32], wh[48];
    if (drag.kind == D_CHDNEW) {
        double a = chd_at0, b = chd_beat_at_x(mx);
        int click = abs(mx - drag.sx) < 4;
        if (b < a) {
            double t = a;
            a = b;
            b = t;
        }
        double len = click ? chd_bar_start(a) + chd_bar_beats(a) - a : b - a;   /* a click: to the end of the bar */
        if (len < 0.25) len = click ? chd_bar_beats(a) : 0.25;
        int in = chord_at(&P, a);
        if (in >= 0) return;
        double next = chd_next_after(a);
        if (a + len > next) len = next - a;
        if (len >= 0.25 - 1e-9) chd_add(a, len);
    } else if (drag.kind == D_CHDEND && drag.clip < P.nchords) {
        chord_label(&P, &P.chords[drag.clip], nm, sizeof nm);
        status("%s: %g beats", nm, P.chords[drag.clip].len);
    } else if (drag.kind == D_CHD && drag.clip < P.nchords) {
        SongChord c = P.chords[drag.clip];
        chord_label(&P, &c, nm, sizeof nm);
        if (abs(mx - drag.sx) < 4) {             /* a click: hear it */
            chd_sel = drag.clip;
            chd_hear(&c);
            char num[16];
            chord_numeral(&P, &c, num, sizeof num);
            chd_where(&c, wh, sizeof wh);
            status("%s (%s) at %s, %g beats. Double-click to change it, Delete removes it", nm, num, wh, c.len);
        } else {
            double at = chd_beat_at_x(chd_rect_[drag.clip].x + (mx - drag.sx));
            chord_remove(&P, drag.clip);
            chd_sel = chord_put(&P, at, c.len, c.root, c.type, c.bass);
            if (chd_sel >= 0) {
                chd_where(&P.chords[chd_sel], wh, sizeof wh);
                status("Moved %s to %s (it takes the place of any chord it lands on). Ctrl+Z puts it back", nm, wh);
            }
        }
    }
}

/* The chord picker: the key's own chords, then any root, type and bass note. Changes apply as you
 * click, and you hear each. */
void chord_picker(void)
{
    if (chp_idx < 0 || chp_idx >= P.nchords) {
        chp_open = 0;
        return;
    }
    SongChord *c = &P.chords[chp_idx];
    int kr, ks;
    key_at_beat(&P, c->at, &kr, &ks);
    int flats = key_uses_flats(kr, ks), seven = SCALES[ks].n == 7;
    int w = 540, rowh = 26, per = 7, trows = (CH_COUNT + per - 1) / per;
    int h = 12 + (font_h + 10) + (seven ? font_h + 8 + 2 * font_h + 14 : 0) + 3 * (font_h + 8) + 3 * rowh + 24 + trows * (rowh + 4) + 14;
    SDL_Rect cr = chd_rect_[chp_idx];
    SDL_Rect m = {cr.x, L.lanes_y + 4, w, h};
    if (m.x + w > W - 4) m.x = W - 4 - w;
    if (m.x < L.left + 4) m.x = L.left + 4;
    if (m.y + h > H - 4) m.y = H - 4 - h;
    chp_box = m;
    fill((SDL_Rect){m.x + 3, m.y + 3, m.w, m.h}, alpha(DARK, 90));
    fill(m, PANEL);
    outline(m, LINE);
    int x = m.x + 14, y = m.y + 12, iw = w - 28, changed = 0;
    char nm[32], num[16], wh[48], buf[96], kn[40];
    chord_label(&P, c, nm, sizeof nm);
    chord_numeral(&P, c, num, sizeof num);
    chd_where(c, wh, sizeof wh);
    snprintf(buf, sizeof buf, "%s  (%s)", nm, num);
    int tw = text(x, y, buf, HEAD);
    snprintf(buf, sizeof buf, "%s, %g beats", wh, c->len);
    text_clip((SDL_Rect){x + tw + 12, y, iw - tw - 12, font_h + 2}, x + tw + 12, y, buf, DIM);
    y += font_h + 10;
    key_name(kr, ks, kn, sizeof kn);
    if (seven) {
        snprintf(buf, sizeof buf, "IN %s", kn);
        for (char *q = buf; *q; q++) *q = (char)toupper((unsigned char)*q);
        text(x, y + 2, buf, DIM);
        if (button((SDL_Rect){x + iw - 70, y - 4, 70, font_h + 6}, "7ths", th_sevenths)) th_sevenths = !th_sevenths;
        y += font_h + 8;
        int bw = (iw - 6 * 4) / 7;
        for (int d = 0; d < 7; d++) {
            int t;
            char dn[16], dname[32];
            int r = diatonic_chord(kr, ks, d, th_sevenths, &t, dn, sizeof dn);
            if (r < 0) continue;
            chord_name(r, t, flats, dname, sizeof dname);
            SDL_Rect b = {x + d * (bw + 4), y, bw, 2 * font_h + 10};
            if (chord_button(b, dn, dname)) {
                c->root = r;
                c->type = t;
                c->bass = -1;
                changed = 1;
            }
            if (c->root == r && c->type == t) outline(b, ACCENT);
        }
        y += 2 * font_h + 14;
    }
    text(x, y + 2, "ROOT", DIM);
    y += font_h + 8;
    int bw = (iw - 11 * 3) / 12;
    for (int i = 0; i < 12; i++)
        if (button((SDL_Rect){x + i * (bw + 3), y, bw, rowh}, pitch_name(i, flats), c->root == i)) {
            c->root = i;
            if (c->bass == i) c->bass = -1;
            changed = 1;
        }
    y += rowh + 8;
    text(x, y + 2, "TYPE", DIM);
    y += font_h + 8;
    int tw2 = (iw - (per - 1) * 3) / per;
    for (int t = 0; t < CH_COUNT; t++) {
        SDL_Rect b = {x + (t % per) * (tw2 + 3), y + (t / per) * (rowh + 4), tw2, rowh};
        if (button(b, CHORDS[t].suffix[0] ? CHORDS[t].suffix : "maj", c->type == t)) {
            c->type = t;
            changed = 1;
        }
        tip(b, "%s: %s", CHORDS[t].name, CHORDS[t].formula);
    }
    y += trows * (rowh + 4) + 4;
    text(x, y + 2, "BASS NOTE", DIM);
    y += font_h + 8;
    int bw2 = (iw - 12 * 3) / 13;
    if (button((SDL_Rect){x, y, bw2, rowh}, "Root", c->bass < 0)) c->bass = -1, changed = 1;
    for (int i = 0; i < 12; i++) {
        SDL_Rect b = {x + (i + 1) * (bw2 + 3), y, bw2, rowh};
        if (button(b, pitch_name(i, flats), c->bass == i)) {
            c->bass = i == c->root ? -1 : i;
            changed = 1;
        }
    }
    tip((SDL_Rect){x, y, iw, rowh}, "A slash chord: the chord over another bass note (C/E is C with E in the bass)");
    y += rowh + 12;
    if (button((SDL_Rect){x, y, 80, rowh}, "Hear it", 0)) chd_hear(c);
    if (button((SDL_Rect){x + 88, y, 80, rowh}, "Remove", 0)) {
        chord_remove(&P, chp_idx);
        chd_sel = -1;
        chp_open = 0;
        status("Removed %s from the chord track", nm);
        lpress = 0;
        return;
    }
    if (button((SDL_Rect){x + iw - 80, y, 80, rowh}, "Done", 1)) chp_open = 0;
    if (changed) {
        chd_hear(c);
        chord_label(&P, c, nm, sizeof nm);
        chord_numeral(&P, c, num, sizeof num);
        status("%s (%s) at %s", nm, num, wh);
    }
    if ((lpress || rpress) && !hit(m)) chp_open = 0;     /* a click outside closes it */
    lpress = rpress = 0;
}

int chord_part(void) { return part_make(PART_CHORDS, 0); }

/* The chords a note clip plays, half a bar at a time (a bar at a time in 3/4 and 6/8), into the
 * chord track over the clip's stretch. */
int chords_from_clip(int ci)
{
    if (ci < 0 || ci >= P.nclips || P.clips[ci].pattern <= 0) {
        status("Select a note clip first (click it), then try again");
        return 0;
    }
    const Clip *cl = &P.clips[ci];
    double a = meter_beats(&P, cl->start), b = meter_beats(&P, cl->start + cl->length);
    chord_split(&P, a);
    chord_split(&P, b);
    for (int i = P.nchords - 1; i >= 0; i--)
        if (P.chords[i].at >= a - 1e-9 && P.chords[i].at + P.chords[i].len <= b + 1e-9) chord_remove(&P, i);
    int made = 0, lasti = -1;
    double w0 = a;
    while (w0 < b - 1e-6) {
        double bs = chd_bar_start(w0), bb = chd_bar_beats(w0);
        double step = bb >= 4 && fmod(bb, 2) < 1e-9 ? bb / 2 : bb;
        double w1 = bs + step * (floor((w0 - bs) / step + 1e-9) + 1);
        if (w1 > b) w1 = b;
        float wt[12] = {0};
        int low = 999;
        for (int i = 0; i < P.nnotes; i++) {
            const Note *n = &P.notes[i];
            if (n->pattern != cl->pattern) continue;
            long ns = cl->start + n->start - cl->offset, ne = ns + n->length;
            if (ns < cl->start) ns = cl->start;
            if (ne > cl->start + cl->length) ne = cl->start + cl->length;
            if (ne <= ns) continue;
            double nb0 = meter_beats(&P, ns), nb1 = meter_beats(&P, ne);
            double ov = (nb1 < w1 ? nb1 : w1) - (nb0 > w0 ? nb0 : w0);
            if (ov <= 1e-6) continue;
            wt[n->note % 12] += (float)(ov * (0.3 + n->vel));
            if (n->note < low) low = n->note;
        }
        int type, bass, r = chord_guess(wt, low < 999 ? low % 12 : -1, &type, &bass);
        if (r < 0) {
            lasti = -1;
        } else if (lasti >= 0 && P.chords[lasti].root == r && P.chords[lasti].type == type && P.chords[lasti].bass == bass &&
                   fabs(P.chords[lasti].at + P.chords[lasti].len - w0) < 1e-6) {
            P.chords[lasti].len = w1 - P.chords[lasti].at;     /* the same chord still */
        } else {
            lasti = chord_put(&P, w0, w1 - w0, r, type, bass);
            made += lasti >= 0;
        }
        w0 = w1;
    }
    if (!made) status("No chords found in that clip: it has no notes");
    else status("Found %d chord%s in the clip, now on the chord track. Click one to hear it", made, made == 1 ? "" : "s");
    return made;
}

void chord_action(int act, int i)
{
    int ok = i >= 0 && i < P.nchords;
    char nm[32];
    switch (act) {
    case CMA_CCHG:
        if (ok) chd_sel = chp_idx = i, chp_open = 1;
        break;
    case CMA_CHEAR:
        if (ok) chd_hear(&P.chords[i]);
        break;
    case CMA_CDUP:
        if (ok) {
            SongChord c = P.chords[i];
            int k = chord_put(&P, c.at + c.len, c.len, c.root, c.type, c.bass);
            if (k >= 0) chd_sel = k;
            chord_label(&P, &c, nm, sizeof nm);
            status("A copy of %s after itself", nm);
        }
        break;
    case CMA_CKEY: chd_key_change(ok ? P.chords[i].at : chd_menu_beat); break;
    case CMA_CDEL:
        if (ok) {
            chord_label(&P, &P.chords[i], nm, sizeof nm);
            chord_remove(&P, i);
            chd_sel = -1;
            status("Removed %s from the chord track", nm);
        }
        break;
    case CMA_CDETECT: chords_from_clip(selected); break;
    case CMA_CPART: chord_part(); break;
    case CMA_CCLEAR: {
        int n = P.nchords;
        P.nchords = 0;
        chd_sel = -1;
        status("Removed all %d chords. Ctrl+Z brings them back", n);
        break;
    }
    }
}

int pt_kind;                                     /* 0 chords, 1 bass, 2 melody, 3 fills */
SDL_Rect pt_handle;                              /* the "drag it onto the timeline" strip */
int pt_style[PART_KINDS] = {0, 1, 0};
double pt_busy[PART_KINDS] = {0.5, 0.5, 0.5};
int pt_octave[PART_KINDS];                       /* -1 lower, 0 as is, 1 higher */
int pt_sound[PART_KINDS] = {1, 1, 1};            /* 0 = the instrument, else a Generator preset (pt_pre) */
int pt_where;                                    /* 0 the whole chord track, 1 the loop, 2 the section at the playhead */
unsigned pt_seed[PART_KINDS] = {1, 1, 1};
T_pt_last pt_last[PART_KINDS] = {{-1, 0}, {-1, 0}, {-1, 0}};
int pt_pre[PART_KINDS][64], pt_npre[PART_KINDS];
const char *pt_names[PART_KINDS][65];
const int PT_LO[PART_KINDS] = {52, 28, 60}, PT_HI[PART_KINDS] = {72, 55, 81};
const char *const PT_TRACK[PART_KINDS] = {"Chords", "Bass", "Melody"};
int fl_style, fl_where, fl_track = -1;           /* fills: before each section / every 4 / every 8 bars */
int pt_force_preset = -1, pt_force_track = -1;   /* the command line's sound= and track= */
double pt_from = -1, pt_to = -1;                 /* the command line's bars=: beats */
double fl_busy = 0.5;
unsigned fl_seed = 1;
T_fl_made fl_made[MAX_DRUMS];
int fl_n;

void pt_init(void)
{
    static int done;
    if (done) return;
    done = 1;
    static const char *const WANT[PART_KINDS][4] = {{"Pad", "Keys", "Pluck", NULL}, {"Bass", NULL}, {"Lead", "Pluck", "Keys", NULL}};
    for (int k = 0; k < PART_KINDS; k++) {
        pt_names[k][0] = "The instrument (Pads)";
        pt_npre[k] = 0;
        for (int w = 0; WANT[k][w]; w++)
            for (int i = 0; i < NSYNTH_PRESETS && pt_npre[k] < 64; i++)
                if (!strcmp(SYNTH_PRESETS[i].kind, WANT[k][w])) {
                    pt_pre[k][pt_npre[k]] = i;
                    pt_names[k][1 + pt_npre[k]++] = SYNTH_PRESETS[i].name;
                }
    }
}

/* where a part goes, in beats: the chords' stretch, or the loop or section within it */
int pt_range(double *a, double *b)
{
    if (!P.nchords) return 0;
    double ca = P.chords[0].at, cb = P.chords[P.nchords - 1].at + P.chords[P.nchords - 1].len;
    *a = ca;
    *b = cb;
    if (pt_from >= 0 && pt_to > pt_from) {
        *a = pt_from;
        *b = pt_to;
    } else if (pt_where == 1 && P.loop_end > P.loop_start) {
        *a = meter_beats(&P, P.loop_start);
        *b = meter_beats(&P, P.loop_end);
    } else if (pt_where == 2) {
        int si = sec_at_bar((int)floor(meter_bars(&P, P.playhead) + 1e-9));
        if (si < 0) return -1;
        *a = meter_beats(&P, meter_bar_frame(&P, P.sections[si].bar));
        *b = meter_beats(&P, meter_bar_frame(&P, P.sections[si].bar + P.sections[si].bars));
    }
    if (*a < ca) *a = ca;
    if (*b > cb) *b = cb;
    return *b > *a + 1e-6 ? 1 : -2;
}

int pt_sample(int kind, double longest)
{
    int s = P.inst_sample;
    if (pt_sound[kind] == 0 && s >= 0 && s < P.nsamples && !P.samples[s].removed) return s;
    int pi = pt_force_preset >= 0 ? pt_force_preset
             : pt_sound[kind] >= 1 && pt_sound[kind] <= pt_npre[kind] ? pt_pre[kind][pt_sound[kind] - 1] : pt_pre[kind][0];
    int held = kind == PART_CHORDS && pt_style[kind] == 0;
    char name[96];
    snprintf(name, sizeof name, held ? "%s (held)" : "%s", SYNTH_PRESETS[pi].name);
    SynthSound y;
    synth_from_preset(&y, pi);
    if (held) y.seconds = longest + 1 > y.seconds ? (longest + 1 > 20 ? 20 : longest + 1) : y.seconds;
    for (int i = 0; i < P.nsamples; i++)
        if (!P.samples[i].removed && !P.samples[i].hidden && !strcmp(P.samples[i].name, name) &&
            P.samples[i].frames >= (long)(y.seconds * SR * 0.9))
            return i;
    y.a4 = P.a4;
    s = sample_generate_synth(&P, &y, name);
    if (s >= 0) sample_make_layers(&P, s, 3, 2);
    return s;
}

int pt_free_track(long a, long b, const char *name)
{
    for (int k = 0; k < P.ntracks; k++) {
        int busy = 0;
        for (int c = 0; c < P.nclips && !busy; c++)
            busy = P.clips[c].track == k && P.clips[c].start < b && P.clips[c].start + P.clips[c].length > a;
        if (!busy) return k;
    }
    int t = -1;
    if (P.ntracks < MAX_TRACKS) {
        LOCK();
        t = project_add_track(&P, name);
        UNLOCK();
    }
    return t >= 0 ? t : P.ntracks - 1;
}

/* Writes the part: a new note clip, or (again) new notes in the clip it made last. Returns the clip. */
int part_make(int kind, int again)
{
    pt_init();
    double a, b;
    int r = pt_range(&a, &b);
    if (r == 0) {
        status("No chords yet: add some on the CHORDS strip, or a progression from Theory > Keys > Place");
        return -1;
    }
    if (r == -1) {
        status("The playhead isn't in a section: mark one, or pick another Where");
        return -1;
    }
    if (r == -2) {
        status("No chords there: pick another Where");
        return -1;
    }
    PartSpec ps = {kind, pt_style[kind], a, b, PT_LO[kind] + 12 * pt_octave[kind], PT_HI[kind] + 12 * pt_octave[kind],
                   pt_busy[kind], pt_seed[kind]};
    static PartNote pn[MAX_NOTES];
    int n = part_write(&P, &ps, pn, MAX_NOTES - P.nnotes > 0 ? MAX_NOTES - P.nnotes : 0);
    if (!n) {
        status("Nothing to write there (or the song has as many notes as it can hold)");
        return -1;
    }
    double longest = 0;
    for (int i = 0; i < n; i++) {
        double sec = (double)(meter_beats_frame(&P, pn[i].at + pn[i].len) - meter_beats_frame(&P, pn[i].at)) / SR;
        longest = sec > longest ? sec : longest;
    }
    long fa = bar_floor(meter_beats_frame(&P, a)), fb = meter_beats_frame(&P, b);
    int s = pt_sample(kind, longest);
    if (s < 0) {
        status("The library is full");
        return -1;
    }
    int c = pt_last[kind].clip;
    int reuse = again && c >= 0 && c < P.nclips && P.clips[c].pattern == pt_last[kind].pattern && pt_last[kind].pattern > 0;
    if (!reuse) {
        c = clip_add(s, pt_force_track >= 0 ? pt_force_track : pt_free_track(fa, fb, PT_TRACK[kind]), fa, 0, fb - fa);
        if (c < 0) return -1;
    }
    LOCK();
    if (reuse) {                                 /* again: the same clip, new notes */
        int w = 0;
        for (int i = 0; i < P.nnotes; i++)
            if (P.notes[i].pattern != P.clips[c].pattern) P.notes[w++] = P.notes[i];
        P.nnotes = w;
        P.clips[c].start = fa;
        P.clips[c].length = fb - fa;
        P.clips[c].offset = 0;
    } else {
        P.clips[c].pattern = pattern_new_id(&P);
    }
    P.clips[c].sample = s;
    P.clips[c].root = sample_root(&P.samples[s]);
    for (int i = 0; i < n && P.nnotes < MAX_NOTES; i++) {
        long f0 = meter_beats_frame(&P, pn[i].at), f1 = meter_beats_frame(&P, pn[i].at + pn[i].len);
        P.notes[P.nnotes++] = (Note){P.clips[c].pattern, pn[i].note, f0 - fa, f1 - f0 > 1 ? f1 - f0 : 1, pn[i].vel};
    }
    UNLOCK();
    pt_last[kind].clip = c;
    pt_last[kind].pattern = P.clips[c].pattern;
    sel_only(c);
    status("%s %s (%s) on track %d: %d notes on \"%s\". Try another writes it again differently; double-click it to edit",
           reuse ? "Wrote the" : "Made a", kind == PART_CHORDS ? "chord part" : kind == PART_BASS ? "bassline" : "melody",
           PART_STYLES[kind][pt_style[kind]], P.clips[c].track + 1, n, P.samples[s].name);
    return c;
}

/* Dragged from the Parts pane: where it would go, in beats, for a drop at `frame`. It starts at that
 * bar; Where says how far it runs (to the chords' end, as long as the loop, or the section there).
 * 1 if there are chords there, 0 if not. */
int part_drop_range(int kind, long frame, double *a, double *b)
{
    if (!P.nchords || kind >= PART_KINDS) return 0;
    double ca = P.chords[0].at, cb = P.chords[P.nchords - 1].at + P.chords[P.nchords - 1].len;
    long bf = bar_floor(frame < 0 ? 0 : frame);
    *a = meter_beats(&P, bf);
    *b = cb;
    int si = sec_at_bar((int)floor(meter_bars(&P, bf) + 1e-9));
    if (pt_where == 1 && P.loop_end > P.loop_start)
        *b = *a + meter_beats(&P, P.loop_end) - meter_beats(&P, P.loop_start);
    else if (pt_where == 2 && si >= 0) {
        *a = meter_beats(&P, meter_bar_frame(&P, P.sections[si].bar));
        *b = meter_beats(&P, meter_bar_frame(&P, P.sections[si].bar + P.sections[si].bars));
    }
    if (*a < ca) *a = ca;
    if (*b > cb) *b = cb;
    return *b > *a + 1e-6;
}

/* the drop itself: the part written there, on that track (a fill: that track's drums get them) */
int part_drop(int kind, long frame, int track)
{
    if (kind >= PART_KINDS) {
        fl_track = track;
        return fills_make(0);
    }
    double a, b;
    if (!part_drop_range(kind, frame, &a, &b)) {
        status(P.nchords ? "No chords there: drop it where the CHORDS strip has some"
                         : "No chords yet: add some on the CHORDS strip, or a progression from Theory > Keys > Place");
        return -1;
    }
    pt_from = a, pt_to = b;                      /* (part_make's range, just this once) */
    pt_force_track = track;
    int c = part_make(kind, 0);
    pt_from = pt_to = -1;
    pt_force_track = -1;
    return c;
}

/* ---- fills */

int fl_default_track(void)
{
    int best = -1, most = 0;
    for (int t = 0; t < P.ntracks; t++) {
        int k = 0;
        for (int c = 0; c < P.nclips; c++) k += P.clips[c].track == t && P.clips[c].pattern < 0;
        if (k > most) most = k, best = t;
    }
    return best;
}

int clip_split_one(int c, long f)        /* clip c becomes two at frame f; the right one's index */
{
    Clip *cl = &P.clips[c];
    if (f <= cl->start || f >= cl->start + cl->length || P.nclips >= MAX_CLIPS) return -1;
    long cut = f - cl->start;
    Clip r = *cl;
    r.start = f;
    r.offset = cl->offset + cut;
    r.length = cl->length - cut;
    r.fade_in = 0;
    cl->length = cut;
    cl->fade_out = 0;
    P.clips[P.nclips] = r;
    csel[P.nclips] = 0;
    return P.nclips++;
}

void fl_rewrite(int i)                   /* a fill written again from its pattern, with the current settings */
{
    DrumPat *fp = drum_find(&P, fl_made[i].id);
    const DrumPat *src = drum_find_c(&P, fl_made[i].src);
    if (!fp || !src) return;
    DrumPat out;
    drum_fill(&P, src, fl_made[i].from, fl_made[i].steps, fl_style, fl_busy, fl_seed * 977u + (unsigned)fl_made[i].bar, &out);
    out.id = fp->id;
    out.track = fp->track;
    snprintf(out.name, sizeof out.name, "%s", fp->name);
    LOCK();
    *fp = out;
    UNLOCK();
}

int fills_make(int again)
{
    if (again && fl_n > 0) {
        int alive = 0;
        for (int i = 0; i < fl_n; i++) alive += drum_find(&P, fl_made[i].id) != NULL;
        if (alive) {
            for (int i = 0; i < fl_n; i++) fl_rewrite(i);
            status("Wrote the %d fill%s again (%s)", fl_n, fl_n == 1 ? "" : "s", FILL_STYLES[fl_style]);
            return fl_n;
        }
    }
    int t = fl_track >= 0 && fl_track < P.ntracks ? fl_track : fl_default_track();
    if (t < 0) {
        status("No drum clips yet: put a beat in with Create > Beats or the Drums panel first");
        return 0;
    }
    int bars[256], nb = 0;
    long end = 0;
    for (int c = 0; c < P.nclips; c++)
        if (P.clips[c].track == t && P.clips[c].start + P.clips[c].length > end) end = P.clips[c].start + P.clips[c].length;
    int last_bar = (int)floor(meter_bars(&P, end) + 1e-6);
    if (fl_where == 0) {
        for (int i = 0; i < P.nsections && nb < 256; i++)
            if (P.sections[i].bar > 0) bars[nb++] = P.sections[i].bar - 1;
    } else {
        int every = fl_where == 1 ? 4 : 8;
        for (int bb = every - 1; bb < last_bar && nb < 256; bb += every) bars[nb++] = bb;
    }
    fl_n = 0;
    int made = 0;
    for (int i = 0; i < nb; i++) {
        long fa = meter_bar_frame(&P, bars[i]), fe = meter_bar_frame(&P, bars[i] + 1);
        int c = -1;
        for (int k = 0; k < P.nclips; k++)
            if (P.clips[k].track == t && P.clips[k].pattern < 0 && P.clips[k].start <= fa && P.clips[k].start + P.clips[k].length >= fe)
                c = k;
        if (c < 0 || P.ndrums >= MAX_DRUMS) continue;
        const DrumPat *src = drum_find_c(&P, -P.clips[c].pattern);
        if (!src || !strncmp(src->name, "Fill", 4)) continue;
        long origin = P.clips[c].start - P.clips[c].offset;
        int from = (int)(llround((meter_beats(&P, fa) - meter_beats(&P, origin)) * 4) % src->steps);
        double bl = meter_beats(&P, fe) - meter_beats(&P, fa);
        int steps = (int)llround(bl * 4);
        if (steps > DRUM_STEPS) steps = DRUM_STEPS;
        DrumPat out;
        drum_fill(&P, src, from, steps, fl_style, fl_busy, fl_seed * 977u + (unsigned)bars[i], &out);
        int id = 0;
        for (int k = 0; k < P.ndrums; k++) id = P.drums[k].id > id ? P.drums[k].id : id;
        out.id = id + 1;
        out.track = t;
        snprintf(out.name, sizeof out.name, "Fill, bar %d", bars[i] + 1);
        int src_id = src->id;
        LOCK();
        P.drums[P.ndrums++] = out;
        int right = clip_split_one(c, fa);
        int mid = right >= 0 ? right : c;
        clip_split_one(mid, fe);
        P.clips[mid].pattern = -out.id;
        P.clips[mid].offset = 0;
        UNLOCK();
        fl_made[fl_n++] = (typeof(fl_made[0])){out.id, src_id, from, steps, bars[i]};
        made++;
    }
    sel_clear();
    if (!made)
        status(fl_where == 0 && !P.nsections ? "No sections to put fills before: mark some, or pick every 4 or 8 bars"
                                              : "No drum clips there on track %d to put fills in", t + 1);
    else
        status("Added %d fill%s (%s) on track %d. Each is a pattern you can open in Drums; Try another writes them again",
               made, made == 1 ? "" : "s", FILL_STYLES[fl_style], t + 1);
    return made;
}

int parts_tab(int x, int y, int w)
{
    pt_init();
    char buf[200];
    int kr, ks;
    key_at_bar(&P, 0, &kr, &ks);
    if (!P.nchords) {
        y = wrapped(x, y, w, "Parts are written from the chord track, and it's empty. Click the CHORDS strip to add "
                             "chords, or put a progression in with Theory > Keys > Place.", DIM) + 8;
    } else {
        char kn[40];
        key_name(kr, ks, kn, sizeof kn);
        snprintf(buf, sizeof buf, "From the chord track: %d chord%s, in %s%s", P.nchords, P.nchords == 1 ? "" : "s", kn,
                 P.nkeys ? " (with key changes)" : "");
        y = wrapped(x, y, w, buf, DIM) + 8;
    }
    static const char *const KINDS[] = {"Chords", "Bass", "Melody", "Fills"};
    y = subtabs(x, y, w, KINDS, 4, &pt_kind);
    if (pt_kind < PART_KINDS) {
        int k = pt_kind;
        y = section(x, y, w, "STYLE");
        int ns = PART_NSTYLES[k], per = 3, bw = (w - (per - 1) * 4) / per;
        for (int i = 0; i < ns; i++)
            if (button((SDL_Rect){x + (i % per) * (bw + 4), y + (i / per) * 32, bw, 28}, PART_STYLES[k][i], pt_style[k] == i))
                pt_style[k] = i;
        y += ((ns + per - 1) / per) * 32 + 6;
        text(x, y + 6, "Busy", DIM);
        slider((SDL_Rect){x + 70, y, w - 70, 26}, &pt_busy[k]);
        tip((SDL_Rect){x, y, w, 26}, "Sparse to busy: how many notes it plays");
        y += 34;
        text(x, y + 6, "Octave", DIM);
        static const char *const OCT[] = {"Lower", "Middle", "Higher"};
        int ow = (w - 70 - 8) / 3;
        for (int i = 0; i < 3; i++)
            if (button((SDL_Rect){x + 70 + i * (ow + 4), y, ow, 26}, OCT[i], pt_octave[k] == i - 1)) pt_octave[k] = i - 1;
        y += 34;
        dropdown((SDL_Rect){x, y, w, 28}, "Sound: ", pt_names[k], 1 + pt_npre[k], &pt_sound[k], 0);
        tip((SDL_Rect){x, y, w, 28}, "The Generator sound it plays (made into the library), or the instrument chosen in Pads");
        y += 36;
    } else {
        y = section(x, y, w, "STYLE");
        int bw = (w - 4) / 2;
        for (int i = 0; i < FILL_KINDS; i++)
            if (button((SDL_Rect){x + (i % 2) * (bw + 4), y + (i / 2) * 32, bw, 28}, FILL_STYLES[i], fl_style == i)) fl_style = i;
        y += 2 * 32 + 6;
        text(x, y + 6, "Length", DIM);
        slider((SDL_Rect){x + 70, y, w - 70, 26}, &fl_busy);
        tip((SDL_Rect){x, y, w, 26}, "The last beat, the last half bar, or the whole bar");
        y += 34;
        if (fl_track < 0 || fl_track >= P.ntracks) fl_track = fl_default_track() >= 0 ? fl_default_track() : 0;
        track_dropdown((SDL_Rect){x, y, w, 28}, "Drums on ", &fl_track);
        y += 36;
    }
    y = section(x, y, w, "WHERE");
    {
        static const char *const WP[] = {"Whole song", "The loop", "This section"};
        static const char *const WF[] = {"Before sections", "Every 4 bars", "Every 8 bars"};
        int *sel = pt_kind < PART_KINDS ? &pt_where : &fl_where;
        const char *const *names = pt_kind < PART_KINDS ? WP : WF;
        int bw = (w - 8) / 3;
        for (int i = 0; i < 3; i++)
            if (button((SDL_Rect){x + i * (bw + 4), y, bw, 28}, names[i], *sel == i)) *sel = i;
        y += 38;
    }
    int bw = (w - 6) / 2;
    if (pt_kind < PART_KINDS) {
        if (button2((SDL_Rect){x, y, bw, 32}, "Make it", 1, P.nchords > 0)) part_make(pt_kind, 0);
        int can = pt_last[pt_kind].clip >= 0 && pt_last[pt_kind].clip < P.nclips &&
                  P.clips[pt_last[pt_kind].clip].pattern == pt_last[pt_kind].pattern;
        if (button2((SDL_Rect){x + bw + 6, y, bw, 32}, "Try another", 0, can)) {
            pt_seed[pt_kind]++;
            part_make(pt_kind, 1);
        }
    } else {
        if (button((SDL_Rect){x, y, bw, 32}, "Add fills", 1)) fills_make(0);
        if (button2((SDL_Rect){x + bw + 6, y, bw, 32}, "Try another", 0, fl_n > 0)) {
            fl_seed++;
            fills_make(1);
        }
    }
    y += 40;
    pt_handle = (SDL_Rect){x, y, w, 30};          /* or drag it to where it should go */
    int ok = pt_kind < PART_KINDS ? P.nchords > 0 : 1;
    int hov = ok && hit(pt_handle) && drag.kind == D_NONE;
    fill(pt_handle, !ok ? PANEL2 : hov ? tone(ACCENT, 0.55f) : tone(ACCENT, 0.35f));
    outline(pt_handle, ok ? ACCENT : LINE);
    text_center(pt_handle, pt_kind < PART_KINDS ? ":: Drag it onto the timeline ::" : ":: Drag onto a drum track ::",
                ok ? TEXT : DIM);
    tip(pt_handle, pt_kind < PART_KINDS ? "Drop it on a track and bar: it's written from there (Where says how far: to the "
                                          "chords' end, as long as the loop, or the section you drop it in)"
                                        : "Drop it on a track with drum clips: the fills go there");
    if (hov && lpress) {
        drag = (typeof(drag)){.kind = D_PART, .sample = pt_kind, .sx = mx, .sy = my};
        lpress = 0;
    }
    y += 38;
    static const char *const ABOUT[4] = {
        "Each chord voiced close to the last, so the voices move as little as they can. Held suits pads; Stabs and "
        "Pulse suit keys; the arpeggios suit plucks.",
        "The chord's root (or its slash bass), kept close from one chord to the next. Walking leads into each new chord a "
        "step away; Groove plays a syncopated pattern with the fifth and octave.",
        "Short cells that come back (A A B A), landing on chord tones on the strong beats and moving by step in between, "
        "and coming home to the root or third at each phrase's end. Hook is catchy and repetitive, Flowing moves more, "
        "Sparse leaves room to breathe (and to sing).",
        "Turns the end of the bar before each section (or every 4 or 8 bars) into a fill, made from that bar of your own "
        "pattern. Each fill is a pattern of its own, open it in Drums to change it."};
    y = wrapped(x, y, w, ABOUT[pt_kind], DIM) + 6;
    return y;
}
