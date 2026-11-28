/* tinydaw: the Drums panel. */
#include "ui.h"

/* ---------------------------------------------------------------- the Drums panel
 *
 * Each track can have drum patterns: rows of sounds over 16 or 32 sixteenth-note steps. A step
 * holds a velocity (how hard) and a chance (how often it plays). Drum clips on the timeline play
 * a pattern over and over; editing the pattern changes every clip that plays it. */

int dr_paint;                    /* what a hits drag paints: 1 on, 0 off */

int drums_content_h, drum_bars = 4, drum_style;

void drums_show(int id)
{
    drum_cur = id;
    show_panel(RI_DRUMS);
}

DrumPat *drum_new(int track, const char *name)
{
    if (P.ndrums >= MAX_DRUMS) {
        status("Drum pattern limit reached (%d)", MAX_DRUMS);
        return NULL;
    }
    int id = 0;
    for (int i = 0; i < P.ndrums; i++) id = P.drums[i].id > id ? P.drums[i].id : id;
    DrumPat d;
    memset(&d, 0, sizeof d);
    d.id = id + 1;
    d.track = track;
    d.steps = 16;
    if (name) snprintf(d.name, sizeof d.name, "%s", name);
    else {
        int n = 0;                              /* "Pattern A", "Pattern B"... on this track */
        for (int i = 0; i < P.ndrums; i++) n += P.drums[i].track == track;
        snprintf(d.name, sizeof d.name, "Pattern %c", 'A' + n % 26);
    }
    for (int r = 0; r < DRUM_ROWS; r++) {
        d.sample[r] = -1;
        d.row_vol[r] = 1;
        memset(d.prob[r], 100, DRUM_STEPS);
    }
    LOCK();
    P.drums[P.ndrums] = d;
    UNLOCK();
    return &P.drums[P.ndrums++];
}

/* A pattern from one of Theory's styles: its kit, a row per drum it uses, its groove. */
DrumPat *drum_from_style(int track, int g)
{
    BeatSpec b = genre_spec(g);
    char nm[24];
    snprintf(nm, sizeof nm, "%.23s", GENRES[g].name);
    DrumPat *d = drum_new(track, nm);
    if (!d) return NULL;
    DrumPat t = *d;
    t.steps = b.steps > 16 ? 32 : 16;
    t.swing = b.swing8 ? 0 : (float)b.swing;
    for (int l = 0; l < DRUM_LANES && t.nrows < DRUM_ROWS; l++) {
        if (!lane_used(&b, l)) continue;
        char hn[96];
        snprintf(hn, sizeof hn, "%s %s", GENRES[g].name, DRUM_LANE_NAMES[l]);
        int smp = hit_sample(&b, l, hn);
        if (smp < 0) continue;
        int r = t.nrows++;
        t.sample[r] = smp;
        for (int k = 0; k < t.steps; k++) {
            char ch = k < b.steps ? b.lanes[l][k] : '.';
            t.vel[r][k] = ch == 'X' ? 127 : ch == 'x' || ch == 'r' ? 100 : ch == 'o' ? 50 : 0;
        }
    }
    LOCK();
    *d = t;
    UNLOCK();
    return d;
}

int drum_clip_sound(const DrumPat *d)    /* a library sound a drum clip can name */
{
    for (int r = 0; r < d->nrows; r++)
        if (d->sample[r] >= 0 && d->sample[r] < P.nsamples) return d->sample[r];
    return -1;
}

/* Puts a pattern on its track from the bar nearest the playhead, `bars` bars long. */
int drum_place(DrumPat *d, int bars)
{
    int smp = drum_clip_sound(d);
    if (smp < 0) {
        status("Give the pattern a sound first: drag one from the list onto a row");
        return -1;
    }
    long at = bar_round(P.playhead);
    double bar0 = round(meter_bars(&P, at));
    long len = meter_bar_frame(&P, bar0 + bars) - at;
    int t = d->track < P.ntracks ? d->track : 0;
    int c = clip_add(smp, t, at, 0, len > 0 ? len : SR);
    if (c < 0) return -1;
    LOCK();
    P.clips[c].pattern = -d->id;
    UNLOCK();
    sel_only(c);
    status("\"%s\" on %s from bar %d, %d bar%s. Drag its right edge to repeat it longer", d->name, P.tracks[t].name,
           (int)bar0 + 1, bars, bars == 1 ? "" : "s");
    return c;
}

void drum_delete(int id)                 /* the pattern and the clips that play it */
{
    LOCK();
    for (int c = P.nclips - 1; c >= 0; c--)
        if (P.clips[c].pattern == -id) {
            memmove(&P.clips[c], &P.clips[c + 1], (size_t)(P.nclips - c - 1) * sizeof(Clip));
            P.nclips--;
        }
    for (int i = 0; i < P.ndrums; i++)
        if (P.drums[i].id == id) {
            memmove(&P.drums[i], &P.drums[i + 1], (size_t)(P.ndrums - i - 1) * sizeof(DrumPat));
            P.ndrums--;
            break;
        }
    UNLOCK();
    sel_clear();
    if (drum_cur == id) drum_cur = 0;
}

void drum_audition(int smp)
{
    if (smp < 0 || smp >= P.nsamples) return;
    LOCK();
    P.preview = smp;
    P.preview_pos = 0;
    UNLOCK();
}

/* A library sound dropped on the panel: onto a row's name (that row plays it), a step (a hit
 * there, in the row with that sound, a new row if none has it) or the "new row" strip. */
int drums_drop(int smp)
{
    if (!drums_open) return 0;
    DrumPat *d = drum_find(&P, drum_cur);
    if (!d && hit(dr_add)) {                    /* no pattern yet: one, with this sound */
        d = drum_new(sel_track, NULL);
        if (!d) return 1;
        drum_cur = d->id;
    }
    if (!d) return 0;
    DrumPat t = *d;
    int done = 0, row = -1, step = -1;
    for (int r = 0; r < t.nrows && !done; r++) {
        if (hit(dr_head[r])) {
            t.sample[r] = smp;
            row = r;
            done = 1;
        }
        for (int k = 0; k < t.steps && !done; k++)
            if (hit(dr_cell[r][k])) {
                row = r;
                step = k;
                done = 1;
            }
    }
    if (!done && hit(dr_add)) done = 1;
    if (!done) return 0;
    if (row >= 0 && t.sample[row] >= 0 && t.sample[row] != smp) {   /* a step in another sound's row */
        row = -1;
        for (int r = 0; r < t.nrows; r++)
            if (t.sample[r] == smp) row = r;
    }
    if (row < 0) {
        if (t.nrows >= DRUM_ROWS) {
            status("A pattern has room for %d sounds", DRUM_ROWS);
            return 1;
        }
        row = t.nrows++;
        memset(t.vel[row], 0, DRUM_STEPS);
        memset(t.prob[row], 100, DRUM_STEPS);
        t.row_vol[row] = 1;
    }
    t.sample[row] = smp;
    if (step >= 0) {
        t.vel[row][step] = 100;
        t.prob[row][step] = 100;
    }
    LOCK();
    *d = t;
    UNLOCK();
    status("\"%s\" plays row %d of \"%s\"%s", P.samples[smp].name, row + 1, d->name, step >= 0 ? " (and a hit where you dropped it)" : "");
    return 1;
}

/* the step under the playhead, for a pattern a clip is playing now; -1 when none is */
int drum_now(const DrumPat *d)
{
    for (int c = 0; c < P.nclips; c++) {
        const Clip *cl = &P.clips[c];
        if (cl->pattern != -d->id || P.playhead < cl->start || P.playhead >= cl->start + cl->length) continue;
        double b = meter_beats(&P, P.playhead) - meter_beats(&P, cl->start - cl->offset);
        long k = (long)floor(b * 4);
        return k < 0 ? -1 : (int)(k % d->steps);
    }
    return -1;
}

int drums_content(int x, int y, int w)
{
    char buf[200];
    if (sel_track < 0 || sel_track >= P.ntracks) sel_track = 0;
    text(x, y + 7, "Track", DIM);
    int tw = text_w("Track") + 10;
    int was_t = sel_track;
    track_dropdown((SDL_Rect){x + tw, y, w - tw, 30}, "", &sel_track);
    if (sel_track != was_t) drum_cur = 0;
    y += 40;
    DrumPat *d = drum_find(&P, drum_cur);
    if (d && d->track != sel_track) d = NULL;
    if (!d)                                     /* the track's first pattern */
        for (int i = 0; i < P.ndrums && !d; i++)
            if (P.drums[i].track == sel_track) d = &P.drums[i];
    drum_cur = d ? d->id : 0;

    /* the track's patterns */
    {
        int bx = x, n = 0;
        for (int i = 0; i < P.ndrums; i++) {
            DrumPat *o = &P.drums[i];
            if (o->track != sel_track) continue;
            int bw = text_w(o->name) + 20;
            if (bx + bw > x + w - 70) break;
            SDL_Rect b = {bx, y, bw, 26};
            tip(b, "%s: %d steps, %d sound%s. Click to edit it", o->name, o->steps, o->nrows, o->nrows == 1 ? "" : "s");
            if (button(b, o->name, o->id == drum_cur)) {
                drum_cur = o->id;
                d = o;
            }
            bx += bw + 4;
            n++;
        }
        SDL_Rect nb = {x + w - 66, y, 66, 26};
        tip(nb, "A new, empty pattern on this track");
        if (button(nb, "+ New", 0)) {
            DrumPat *o = drum_new(sel_track, NULL);
            if (o) {
                drum_cur = o->id;
                d = o;
                status("New pattern \"%s\": drag sounds from the list below onto it", o->name);
            }
        }
        if (!n) text(x, y + 6, "No patterns on this track yet", DIM);
        y += 36;
    }

    /* start from one of Theory's styles */
    {
        static const char *gnames[32];
        genre_init();
        int ng = NGENRES < 32 ? NGENRES : 32;
        for (int g = 0; g < ng; g++) gnames[g] = GENRES[g].name;
        if (drum_style >= ng) drum_style = 0;
        int bw = 150;
        dropdown((SDL_Rect){x, y, w - bw - 6, 28}, "Style: ", gnames, ng, &drum_style, 0);
        SDL_Rect sb = {x + w - bw, y, bw, 28};
        tip(sb, "A new pattern with this style's drums and groove (the beat as Create > Beats has it)");
        if (button(sb, "New from style", 0)) {
            DrumPat *o = drum_from_style(sel_track, drum_style);
            if (o) {
                drum_cur = o->id;
                d = o;
                status("\"%s\": %d sounds from its kit. Click steps to change it", o->name, o->nrows);
            }
        }
        y += 38;
    }

    if (d) {
        DrumPat t = *d;                         /* edits go to a copy, then in under the lock */
        /* steps, swing, editing mode */
        if (button((SDL_Rect){x, y, 46, 26}, "16", t.steps == 16)) t.steps = 16;
        if (button((SDL_Rect){x + 50, y, 46, 26}, "32", t.steps == 32)) {
            if (t.steps == 16)                  /* the 16 again, so it still sounds the same */
                for (int r = 0; r < t.nrows; r++) {
                    memcpy(t.vel[r] + 16, t.vel[r], 16);
                    memcpy(t.prob[r] + 16, t.prob[r], 16);
                }
            t.steps = 32;
        }
        static double sw;
        if (!(drag.kind == D_NUM && drag.num == &sw) && !(numedit.active && numedit.v == &sw)) sw = t.swing * 200;
        number((SDL_Rect){x + 102, y, w - 102, 26}, "Swing", &sw, 0, 100, 0.25, "%.0f%%");
        t.swing = (float)(sw / 200);
        y += 34;
        static const char *const MODES[3] = {"Hits", "Velocity", "Chance"};
        static const char *const MTIP[3] = {"Click steps to add or take away hits; drag along a row to paint",
                                            "Drag up and down in a hit to set how hard it plays",
                                            "Drag up and down in a hit to set how often it plays (100% = always)"};
        int mw = (w - 8) / 3;
        for (int m = 0; m < 3; m++) {
            SDL_Rect mb = {x + m * (mw + 4), y, m == 2 ? w - 2 * (mw + 4) : mw, 26};
            tip(mb, "%s", MTIP[m]);
            if (button(mb, MODES[m], drum_mode == m)) drum_mode = m;
        }
        y += 34;

        /* the grid */
        int head = 96, cw = (w - head) / t.steps, ch = 24, now = P.playing ? drum_now(&t) : -1;
        Col tc = TRACK_COL[t.track % MAX_TRACKS];
        for (int r = 0; r < t.nrows; r++) {
            SDL_Rect hr = {x, y, head - 6, ch};
            dr_head[r] = hr;
            int has = t.sample[r] >= 0 && t.sample[r] < P.nsamples;
            int drop = drag.kind == D_LIB && hit(hr);
            fill(hr, drop ? PANEL3 : PANEL2);
            if (drop) outline(hr, HEAD);
            text_clip(hr, x + 5, y + (ch - font_h) / 2, has ? P.samples[t.sample[r]].name : "drop a sound", has ? TEXT : DIM);
            tip(hr, has ? "%s: click to hear it, right-click to take the row out, drop a sound here to swap it"
                        : "Drop a sound from the list below here", has ? P.samples[t.sample[r]].name : "");
            if (hit(hr) && drag.kind == D_NONE && lpress) {
                lpress = 0;
                if (has) drum_audition(t.sample[r]);
            }
            if (hit(hr) && drag.kind == D_NONE && rpress) {   /* take the row out */
                rpress = 0;
                for (int q = r; q < t.nrows - 1; q++) {
                    t.sample[q] = t.sample[q + 1];
                    t.row_vol[q] = t.row_vol[q + 1];
                    memcpy(t.vel[q], t.vel[q + 1], DRUM_STEPS);
                    memcpy(t.prob[q], t.prob[q + 1], DRUM_STEPS);
                }
                t.nrows--;
                status("Took a row out of \"%s\"", t.name);
                break;
            }
            for (int k = 0; k < t.steps; k++) {
                SDL_Rect c = {x + head + k * cw, y, cw - 1, ch};
                dr_cell[r][k] = c;
                int v = t.vel[r][k], pr = t.prob[r][k], hov = hit(c);
                Col bg = (k / 4) % 2 ? PANEL2 : tone(PANEL2, 1.25f);
                if (k == now) bg = tone(bg, 1.5f);
                fill(c, hov && drag.kind == D_NONE ? PANEL3 : bg);
                if (drag.kind == D_LIB && hov) outline(c, HEAD);
                if (v) {
                    int hh = drum_mode == 2 ? (ch - 2) * pr / 100 : (ch - 2) * v / 127;
                    if (hh < 2) hh = 2;
                    Col hc = pr < 100 ? alpha(tc, (Uint8)(90 + pr)) : tc;
                    if (drum_mode == 0) fill((SDL_Rect){c.x + 1, c.y + 1, c.w - 2, c.h - 2}, alpha(tc, (Uint8)(70 + v)));
                    else fill((SDL_Rect){c.x + 1, c.y + c.h - 1 - hh, c.w - 2, hh}, hc);
                    if (pr < 100 && drum_mode != 2 && c.w >= 6) fill((SDL_Rect){c.x + c.w / 2 - 1, c.y + 2, 2, 2}, TEXT);
                }
                if (hov && drag.kind == D_NONE) {
                    tip(c, v ? "Step %d: velocity %d, plays %d%% of the time. Right-click takes it away"
                             : "Step %d: click to add a hit", k + 1, v, pr);
                }
                if (hov && rpress && drag.kind == D_NONE) {
                    rpress = 0;
                    t.vel[r][k] = 0;
                }
                if (hov && lpress && drag.kind == D_NONE) {   /* start a stroke */
                    lpress = 0;
                    drag = (typeof(drag)){.kind = D_DRUM, .sx = mx, .sy = my};
                    dr_paint = !v;
                    if (drum_mode == 0 && !v && has) drum_audition(t.sample[r]);
                }
                if (hov && drag.kind == D_DRUM && ldown) {    /* the stroke goes over it */
                    float fy = (float)(c.y + c.h - my) / c.h;
                    fy = fy < 0.02f ? 0.02f : fy > 1 ? 1 : fy;
                    if (drum_mode == 0) {
                        t.vel[r][k] = dr_paint ? (v ? v : 100) : 0;
                        if (dr_paint && !v) t.prob[r][k] = 100;
                    } else if (v && drum_mode == 1) {
                        t.vel[r][k] = (unsigned char)(1 + fy * 126);
                    } else if (v && drum_mode == 2) {
                        t.prob[r][k] = (unsigned char)(1 + fy * 99);
                    }
                }
            }
            y += ch + 3;
        }
        /* where a new row goes */
        dr_add = (SDL_Rect){x, y, w, 28};
        if (t.nrows < DRUM_ROWS) {
            int drop = drag.kind == D_LIB && hit(dr_add);
            fill(dr_add, drop ? PANEL3 : tone(PANEL, 1.15f));
            outline(dr_add, drop ? HEAD : LINE);
            text_center(dr_add, t.nrows ? "Drop a sound here for another row" : "Drop a sound here for the first row",
                        drop ? HEAD : DIM);
            y += 36;
        } else {
            dr_add = (SDL_Rect){0, 0, 0, 0};
        }
        if (memcmp(&t, d, sizeof t)) {
            LOCK();
            *d = t;
            UNLOCK();
        }

        /* onto the timeline */
        text(x, y + 6, "Bars", DIM);
        static const int bar_opts[4] = {1, 2, 4, 8};
        int bx = x + text_w("Bars") + 10;
        for (int i = 0; i < 4; i++) {
            snprintf(buf, sizeof buf, "%d", bar_opts[i]);
            if (button((SDL_Rect){bx + i * 36, y, 32, 26}, buf, drum_bars == bar_opts[i])) drum_bars = bar_opts[i];
        }
        SDL_Rect pb = {bx + 4 * 36 + 4, y, x + w - (bx + 4 * 36 + 4), 26};
        tip(pb, "A drum clip playing this pattern, on its track at the bar nearest the playhead");
        if (button(pb, "Put on the timeline", 0)) drum_place(d, drum_bars);
        y += 34;
        int hw = (w - 8) / 3;
        if (button((SDL_Rect){x, y, hw, 26}, "Copy as new", 0)) {
            DrumPat cp = *d;
            DrumPat *o = drum_new(sel_track, NULL);
            if (o) {
                int id = o->id;
                char nm[24];
                snprintf(nm, sizeof nm, "%s", o->name);
                cp.id = id;
                snprintf(cp.name, sizeof cp.name, "%s", nm);
                LOCK();
                *o = cp;
                UNLOCK();
                drum_cur = id;
                status("\"%s\" is a copy to change: clips of the first one stay as they are", nm);
            }
        }
        if (button((SDL_Rect){x + hw + 4, y, hw, 26}, "Clear steps", 0)) {
            LOCK();
            memset(d->vel, 0, sizeof d->vel);
            UNLOCK();
        }
        if (button((SDL_Rect){x + 2 * (hw + 4), y, w - 2 * (hw + 4), 26}, "Delete pattern", 0)) {
            int uses = 0;
            for (int c = 0; c < P.nclips; c++) uses += P.clips[c].pattern == -d->id;
            snprintf(buf, sizeof buf, "%s", d->name);
            drum_delete(d->id);
            status("Deleted \"%s\"%s. Ctrl+Z brings it back", buf, uses ? " and its clips" : "");
            d = NULL;
        }
        y += 40;
    } else {
        dr_add = (SDL_Rect){x, y, w, 30};
        int drop = drag.kind == D_LIB && hit(dr_add);
        fill(dr_add, drop ? PANEL3 : tone(PANEL, 1.15f));
        outline(dr_add, drop ? HEAD : LINE);
        text_center(dr_add, "Or drop a sound here to start a pattern", drop ? HEAD : DIM);
        y += 40;
    }

    /* sounds to drag in */
    y = section(x, y, w, "SOUNDS: DRAG ONTO A ROW OR A STEP");
    int shown = 0;
    for (int i = 0; i < P.nsamples; i++) {
        const Sample *sm = &P.samples[i];
        if ((sm->pitched && !sm->listed) || sm->removed || sm->hidden) continue;
        SDL_Rect rr = {x, y, w, 22};
        int hov = hit(rr) && drag.kind == D_NONE;
        fill(rr, hov ? PANEL3 : shown % 2 ? PANEL : tone(PANEL, 1.12f));
        snprintf(buf, sizeof buf, "%.2fs", (double)sm->frames / SR);
        text(x + w - 6 - text_w(buf), y + (22 - font_h) / 2, buf, DIM);
        text_clip((SDL_Rect){x, y, w - text_w(buf) - 14, 22}, x + 6, y + (22 - font_h) / 2, sm->name, TEXT);
        tip(rr, "%s: drag it onto a row or a step above; click to hear it", sm->name);
        if (hov && lpress) {
            lpress = 0;
            drag = (typeof(drag)){.kind = D_LIBPEND, .sample = i, .sx = mx, .sy = my};
        }
        y += 22;
        shown++;
    }
    if (!shown) {
        text(x, y + 4, "No sounds yet: make some in Sounds, or start from a style above", DIM);
        y += font_h + 10;
    }
    return y;
}

void drums_pane(void)
{
    SDL_Rect pane = {L.pane_x, L.top, L.pane_w, H - L.top - L.status};
    fill(pane, PANEL);
    line(pane.x + pane.w - 1, pane.y, pane.x + pane.w - 1, pane.y + pane.h, LINE);
    int inside = hit(pane), saved_press = lpress, saved_r = rpress;
    if (!inside) lpress = rpress = 0;
    int max_scroll = drums_content_h - pane.h > 0 ? drums_content_h - pane.h : 0;
    drums_scroll = drums_scroll < 0 ? 0 : drums_scroll > max_scroll ? max_scroll : drums_scroll;
    SDL_RenderSetClipRect(ren, &pane);
    int y0 = pane.y + 12 - drums_scroll;
    int y = drums_content(pane.x + 14, y0, pane.w - 28);
    drums_content_h = y - y0 + 12;
    scrollbar(pane, &drums_scroll, drums_content_h);
    SDL_RenderSetClipRect(ren, NULL);
    if (!inside) {
        lpress = saved_press;
        rpress = saved_r;
    }
}

void set_pad(int k, int sample, int mode, float vol, int pitch, int track)
{
    if (sample < 0 || k < 0 || k >= NPADS) return;
    LOCK();
    voice_stop(&P, k, -1);
    P.pads[k] = (Pad){sample, mode, vol, pitch, track < P.ntracks ? track : P.ntracks - 1, 0};
    UNLOCK();
}

void preset_drums(int first, int count)  /* the Beats style's kit, with tuned variations */
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

void preset_chords(int first, int count, int track)   /* the chords of the Theory key */
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

void preset_notes(int base, int track, const char *tag)   /* 16 notes of the scale, upwards */
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

void preset_loops(void)                  /* this style's parts, then other styles' whole beats */
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

void load_pad_preset(int k)
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

int track_empty(int t)
{
    for (int c = 0; c < P.nclips; c++)
        if (P.clips[c].track == t) return 0;
    return 1;
}

/* Places the genre's beat from `start`. first_track < 0 puts it on new rows (or empty ones
 * when all 16 exist); otherwise on consecutive rows from first_track, adding rows as needed. */
void place_beat(int g, long start, int first_track)
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
void add_beat_to_library(int g)
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

void play_beat(int g)
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
int wrapped(int x, int y, int w, const char *s, Col c)
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

int tab_genres(int x, int y, int w)
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
            tip(r, "Click to step through hit, accent, ghost%s and rest", l == 3 ? ", roll" : "");
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
    {
        SDL_Rect eb = {x + 200, y, w - 200, 24};
        tip(eb, "This beat as a step pattern you can change fully: any sound on each row, 16 or 32 steps, "
                "velocity and chance on every step");
        if (button(eb, "Edit in Drums", 0)) {
            int t = -1;                         /* an empty track, or a new one */
            for (int i = 0; i < P.ntracks && t < 0; i++) {
                int used = 0;
                for (int c = 0; c < P.nclips; c++) used |= P.clips[c].track == i;
                for (int k = 0; k < P.ndrums; k++) used |= P.drums[k].track == i;
                if (!used) t = i;
            }
            if (t < 0) {
                LOCK();
                t = project_add_track(&P, "Drums");
                UNLOCK();
            }
            if (t < 0) t = sel_track >= 0 ? sel_track : 0;
            DrumPat *o = drum_from_style(t, gn_sel);
            if (o) {
                sel_track = t;
                drums_show(o->id);
                status("\"%s\" is a pattern on %s now: change it here, then Put on the timeline", o->name, P.tracks[t].name);
            }
        }
    }
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
        place_beat(gn_sel, bar_round(P.playhead), -1);
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
SDL_Rect tab_rect(int pos, int x, int y, int w)
{
    int bw = (w - 4 * 3) / 4;
    return (SDL_Rect){x + pos * (bw + 4), y, pos == 3 ? w - 3 * (bw + 4) : bw, 28};
}

void theory_pane(void)
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
    static const char *const MAIN[] = {"Keys", "Chords", "Reference"};
    static const char *const REF[] = {"Circle", "Intervals", "Tempo", "Notes"};
    static const int REF_ID[] = {1, 3, 4, 5};
    static int ref_tab = 1;
    if (theory_tab == 6) theory_tab = 0;       /* Beats moved to Create */
    int main_sel = theory_tab == 0 ? 0 : theory_tab == 2 ? 1 : 2, was = main_sel;
    y = subtabs(x, y, w, MAIN, 3, &main_sel);
    if (main_sel != was) {
        theory_tab = main_sel == 0 ? 0 : main_sel == 1 ? 2 : ref_tab;
        th_scroll = 0;
    }
    if (main_sel == 2) {
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
    }
    th_content_h = y - y0 + 10;
    (void)max_scroll;
    scrollbar(pane, &th_scroll, th_content_h);
    SDL_RenderSetClipRect(ren, NULL);
    if (!inside) lpress = saved_press;
}

const char *RATE_LABELS[6] = {"44.1k", "48k", "88.2k", "96k", "176.4k", "192k"};

void export_dialog(void)
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

    ROW("Level");                                /* as mixed, peak-normalized, or to a loudness */
    {
        static const char *names[5] = {"As mixed", "Peak -1 dB", "Peak -0.1", "-14 LUFS", "-9 LUFS"};
        int cur = EXO.loudness ? 2 + EXO.loudness : EXO.normalize, lw = (w - (ox - d.x) - 18 - 4 * 6) / 5;
        for (int i = 0; i < 5; i++)
            if (button2((SDL_Rect){ox + i * (lw + 6), y, lw, bh}, names[i], cur == i && !EXO.stems, !EXO.stems)) {
                EXO.normalize = i <= 2 ? i : 0;
                EXO.loudness = i >= 3 ? i - 2 : 0;
            }
    }
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
    snprintf(buf, sizeof buf, "%s  %g kHz  %s  %s  %.2f s%s%s", f->name, rate / 1000.0, depth,
             EXO.mono ? "mono" : "stereo", b > a ? (double)(b - a) / SR : 0.0,
             EXO.loudness && !EXO.stems ? (EXO.loudness == 1 ? "  at -14 LUFS (streaming)" : "  at -9 LUFS (loud)") : "",
             rate != SR ? "  (resampled)" : "");
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
