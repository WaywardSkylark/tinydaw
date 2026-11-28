/* tinydaw: the session view. */
#include "ui.h"

/* ---------------------------------------------------------------- the session view
 *
 * Scenes across, tracks down the side (the same tracks, mixer and effects as the timeline). Click a slot
 * to launch it on the next bar (or beat); its track plays it round and round instead of the timeline
 * until it's stopped (the square) or sent back to the timeline (the arrow). Clicking a scene's name
 * launches the whole row. Everything played is remembered, and Capture writes it onto the timeline. */

/* (session_view: Tab, the session grid instead of the timeline) */
int sess_quant;                          /* launches wait for: 0 the next bar, 1 the next beat, 2 nothing */

int sess_scroll;                         /* pixels the scenes are scrolled left */
int sess_renaming = -1;                  /* the scene whose name is being typed */
SDL_Rect sess_cell_[MAX_TRACKS][MAX_SCENES], sess_scene_r[MAX_SCENES], sess_stop_r[MAX_TRACKS], sess_back_r[MAX_TRACKS];
int sess_menu_t, sess_menu_s;
SessSeg sess_log[2048];
int sess_nlog, sess_open_seg[MAX_TRACKS], sess_seen[MAX_TRACKS];
long sess_last_head;

long sess_boundary(void)                 /* when a launch now would start */
{
    if (!P.playing || sess_quant == 2) return P.playhead;
    if (sess_quant == 1) return meter_beats_frame(&P, floor(meter_beats(&P, P.playhead) + 1e-9) + 1);
    return meter_bar_frame(&P, floor(meter_bars(&P, P.playhead) + 1e-9) + 1);
}

void sess_queue(int t, int what)          /* a scene, SESS_STOPPED or SESS_TIMELINE */
{
    if (t < 0 || t >= P.ntracks) return;
    if (what >= 0 && (what >= P.nscenes || P.slots[t][what].kind == SLOT_EMPTY)) return;
    long at = sess_boundary();
    /* looping, with the next boundary at or past the loop's end: the playhead never gets there (it wraps
     * first), so the launch waits for the loop's start instead */
    int wrap = P.playing && P.loop && P.loop_end > P.loop_start && P.playhead < P.loop_end && at >= P.loop_end;
    LOCK();
    P.sess_q[t] = what;
    P.sess_q_at[t] = wrap ? P.loop_start : at;
    P.sess_q_wrap[t] = wrap;
    UNLOCK();
}

void sess_launch(int t, int s)
{
    if (!P.playing) toggle_play();               /* launching starts the music */
    sess_queue(t, s);
}

const char *const LAUNCH_NAMES[LAUNCH_MODES] = {"Loop", "Toggle", "Hold", "Once"};

void sess_release_now(int t)              /* a toggle, hold or once slot letting go: the timeline again, now */
{
    if (t < 0 || t >= P.ntracks) return;
    LOCK();
    P.sess_q[t] = SESS_TIMELINE;
    P.sess_q_at[t] = P.playhead;
    P.sess_q_wrap[t] = 0;
    UNLOCK();
}

void sess_launch_scene(int s)             /* every slot in the row (an empty slot leaves its track be) */
{
    if (s < 0 || s >= P.nscenes) return;
    int any = 0;
    for (int t = 0; t < P.ntracks; t++) any |= P.slots[t][s].kind != SLOT_EMPTY;
    if (!any) {
        status("%s is empty: put clips in its slots first", P.scene_names[s]);
        return;
    }
    if (!P.playing) toggle_play();
    for (int t = 0; t < P.ntracks; t++)
        if (P.slots[t][s].kind != SLOT_EMPTY) sess_queue(t, s);
    status("%s on the next %s", P.scene_names[s], sess_quant == 0 ? "bar" : sess_quant == 1 ? "beat" : "moment");
}

void sess_all(int what)                   /* every track stopped, or back on the timeline */
{
    for (int t = 0; t < P.ntracks; t++)
        if (P.sess_play[t] != what || P.sess_q[t] != SESS_NONE) sess_queue(t, what);
}

int sess_add_scene(const char *name)
{
    if (P.nscenes >= MAX_SCENES) {
        status("A song can have %d scenes", MAX_SCENES);
        return -1;
    }
    int s = P.nscenes++;
    for (int t = 0; t < MAX_TRACKS; t++) memset(&P.slots[t][s], 0, sizeof(Slot));
    if (name) snprintf(P.scene_names[s], sizeof P.scene_names[s], "%s", name);
    else snprintf(P.scene_names[s], sizeof P.scene_names[s], "Scene %d", s + 1);
    return s;
}

void sess_remove_scene(int s)
{
    if (s < 0 || s >= P.nscenes) return;
    LOCK();
    for (int t = 0; t < MAX_TRACKS; t++) {
        memmove(&P.slots[t][s], &P.slots[t][s + 1], (size_t)(P.nscenes - s - 1) * sizeof(Slot));
        memset(&P.slots[t][P.nscenes - 1], 0, sizeof(Slot));
        if (P.sess_play[t] == s) P.sess_play[t] = SESS_TIMELINE;
        else if (P.sess_play[t] > s) P.sess_play[t]--;
        if (P.sess_q[t] >= s) P.sess_q[t] = P.sess_q[t] == s ? SESS_NONE : P.sess_q[t] - 1;
    }
    memmove(P.scene_names[s], P.scene_names[s + 1], (size_t)(P.nscenes - s - 1) * sizeof P.scene_names[0]);
    P.nscenes--;
    UNLOCK();
}

/* one scene for each of the song's sections, each track's slot looping its part of that section */
int sess_from_sections(void)
{
    if (!P.nsections) {
        status("No sections to make scenes from: mark some on the SECTIONS strip, or add scenes with +");
        return 0;
    }
    int made = 0;
    for (int i = 0; i < P.nsections && P.nscenes < MAX_SCENES; i++) {
        const Section *sc = &P.sections[i];
        long a = meter_bar_frame(&P, sc->bar), z = meter_bar_frame(&P, sc->bar + sc->bars);
        int s = sess_add_scene(sc->name);
        if (s < 0) break;
        for (int t = 0; t < P.ntracks; t++) {
            int any = 0;
            for (int c = 0; c < P.nclips && !any; c++)
                any = P.clips[c].track == t && P.clips[c].start < z && P.clips[c].start + P.clips[c].length > a;
            if (!any) continue;
            Slot *sl = &P.slots[t][s];
            sl->kind = SLOT_RANGE;
            sl->bar = sc->bar;
            sl->beats = meter_beats(&P, z) - meter_beats(&P, a);
            snprintf(sl->name, sizeof sl->name, "%s", sc->name);
        }
        made++;
    }
    status("Made %d scene%s from the song's sections: click a scene's name to launch it, or a slot for one track",
           made, made == 1 ? "" : "s");
    return made;
}

void slot_fill_clip(int t, int s, const Clip *c, const char *name)
{
    Slot *sl = &P.slots[t][s];
    memset(sl, 0, sizeof *sl);
    sl->kind = SLOT_CLIP;
    sl->clip = *c;
    sl->clip.track = t;
    sl->clip.start = 0;
    double beats = meter_beats(&P, c->start + c->length) - meter_beats(&P, c->start);
    int num, den;                                /* whole bars or beats of the time signature where it starts */
    meter_at(&P, c->start, NULL, &num, &den, NULL, NULL, NULL);
    double bar = num * 4.0 / den, beat = 4.0 / den;
    double q = beats >= bar - 1e-6 ? bar : beats >= beat - 1e-6 ? beat : 0.25;
    sl->beats = round(beats / q) * q;
    if (sl->beats < 0.25) sl->beats = 0.25;
    snprintf(sl->name, sizeof sl->name, "%.23s", name);
}

void sess_drop_sound(int sample)          /* a library sound dropped on an empty slot */
{
    for (int t = 0; t < P.ntracks; t++)
        for (int s = 0; s < P.nscenes; s++)
            if (hit(sess_cell_[t][s])) {
                Clip c = {sample, t, 0, 0, P.samples[sample].frames, 0, 0, 0, 0, 0, 0};
                LOCK();
                slot_fill_clip(t, s, &c, P.samples[sample].name);
                UNLOCK();
                status("\"%s\" in %s on track %d: click it to launch it", P.samples[sample].name, P.scene_names[s], t + 1);
                return;
            }
}

/* ---- copy and paste: a slot copies into all three clipboards (slots; the timeline's clips, so Ctrl+V
 * there pastes it; and sounds, so the library, a pad or the instrument can take its sound), and a paste
 * into a slot takes whatever was copied last */

Slot slot_board;

int slot_board_has;

int slot_clips(int t, const Slot *sl, Clip *out, int max)   /* what a slot plays once, from 0 */
{
    int n = 0;
    if (sl->kind == SLOT_CLIP && max > 0) {
        out[0] = sl->clip;
        out[0].track = t;
        out[0].start = 0;
        long L = slot_frames(&P, sl, P.playhead);
        long room = out[0].pattern ? L : P.samples[out[0].sample].frames - out[0].offset;
        out[0].length = room < L ? room : L;
        n = out[0].length > 0;
    } else if (sl->kind == SLOT_RANGE) {
        long a = meter_bar_frame(&P, sl->bar), z = a + slot_frames(&P, sl, a);
        for (int c = 0; c < P.nclips && n < max; c++) {
            Clip x = P.clips[c];
            if (x.track != t || x.start >= z || x.start + x.length <= a) continue;
            if (x.start < a) {
                x.offset += a - x.start;
                x.length -= a - x.start;
                x.start = a;
                x.fade_in = 0;
            }
            if (x.start + x.length > z) x.length = z - x.start;
            x.start -= a;
            out[n++] = x;
        }
    }
    return n;
}

void slot_copy(int t, int s)
{
    const Slot *sl = &P.slots[t][s];
    if (sl->kind == SLOT_EMPTY) return;
    slot_board = *sl;
    slot_board_has = 1;
    nclipboard = slot_clips(t, sl, clipboard, MAX_CLIPS);
    if (nclipboard) {
        sclip.has = 1;
        sclip.sample = sample_base(&P, clipboard[0].sample);
        sclip.from_pad = 0;
    }
    last_copy = COPY_SLOT;
    status("Copied \"%s\": paste it into a slot, onto the timeline (Ctrl+V there), or its sound into the library or a pad",
           sl->name);
}

int slot_can_paste(void)
{
    return (last_copy == COPY_SLOT && slot_board_has) || (last_copy == COPY_CLIPS && nclipboard) ||
           (last_copy == COPY_SOUND && sclip.has && sclip.sample >= 0 && sclip.sample < P.nsamples);
}

void slot_paste(int t, int s)
{
    if (t < 0 || t >= P.ntracks || s < 0 || s >= P.nscenes) return;
    char nm[48];
    if (last_copy == COPY_SLOT && slot_board_has) {
        LOCK();
        P.slots[t][s] = slot_board;
        UNLOCK();
        status(slot_board.kind == SLOT_RANGE && t != sess_menu_t ? "Pasted \"%s\" (it loops the same bars of this track)"
                                                                 : "Pasted \"%s\"", slot_board.name);
    } else if (last_copy == COPY_CLIPS && nclipboard) {
        Clip c = clipboard[0];
        c.start = 0;
        snprintf(nm, sizeof nm, "%s", P.samples[sample_base(&P, c.sample)].name);
        if (c.pattern < 0) {
            const DrumPat *d = drum_find_c(&P, -c.pattern);
            if (d) snprintf(nm, sizeof nm, "%s", d->name);
        }
        LOCK();
        slot_fill_clip(t, s, &c, nm);
        UNLOCK();
        status(nclipboard > 1 ? "Pasted the first of the %d copied clips" : "Pasted the copied clip", nclipboard);
    } else if (last_copy == COPY_SOUND && sclip.has && sclip.sample >= 0 && sclip.sample < P.nsamples) {
        int sm = sclip_library_sound();
        if (sm < 0) return;
        Clip c = {sm, t, 0, 0, P.samples[sm].frames, 0, 0, 0, 0, 0, 0};
        LOCK();
        slot_fill_clip(t, s, &c, P.samples[sm].name);
        UNLOCK();
        status("\"%s\" in the slot", P.samples[sm].name);
    } else {
        status("Nothing copied yet: copy a slot, a clip (Ctrl+C) or a sound first");
    }
}

void slot_clear(int t, int s)
{
    LOCK();
    if (P.sess_play[t] == s) P.sess_play[t] = SESS_TIMELINE;
    if (P.sess_q[t] == s) P.sess_q[t] = SESS_NONE;
    memset(&P.slots[t][s], 0, sizeof(Slot));
    UNLOCK();
}

void slot_to_timeline(int t, int s)      /* one pass of it on the timeline at the playhead */
{
    static Clip tmp[256];
    int n = slot_clips(t, &P.slots[t][s], tmp, 256);
    long at = snap(P.playhead);
    sel_clear();
    for (int k = 0; k < n; k++) {
        int c = clip_add(tmp[k].sample, t, at + tmp[k].start, tmp[k].offset, tmp[k].length);
        if (c < 0) break;
        LOCK();
        P.clips[c] = tmp[k];
        P.clips[c].start = at + tmp[k].start;
        UNLOCK();
        csel[c] = 1;
        selected = c;
    }
    status("\"%s\" on track %d's timeline at the playhead: %d clip%s", P.slots[t][s].name, t + 1, n, n == 1 ? "" : "s");
}

int slot_under_mouse(int *t, int *s)
{
    if (!session_view) return 0;
    for (int a = 0; a < P.ntracks; a++)
        for (int b = 0; b < P.nscenes; b++)
            if (sess_cell_[a][b].w > 0 && hit(sess_cell_[a][b]) && mx >= L.tl_x + 58) {
                *t = a;
                *s = b;
                return 1;
            }
    return 0;
}

void slot_import_path(int t, int s, const char *path)   /* a sound file, into the library and the slot */
{
    if (t < 0 || t >= P.ntracks || s < 0 || s >= P.nscenes) return;
    int idx = import_file(path);
    if (idx < 0) return;
    Clip c = {idx, t, 0, 0, P.samples[idx].frames, 0, 0, 0, 0, 0, 0};
    LOCK();
    slot_fill_clip(t, s, &c, P.samples[idx].name);
    UNLOCK();
    status("Imported \"%s\" into %s on track %d: click it to launch it", P.samples[idx].name, P.scene_names[s], t + 1);
}

/* files dropped from outside on the session: the slot under the pointer, then the empty ones after it */
int slot_drop_file(const char *path)
{
    int t, s;
    if (!session_view) return 0;
    int gx, gy, wx, wy;                         /* (the pointer may not have moved over the window yet) */
    SDL_GetGlobalMouseState(&gx, &gy);
    SDL_GetWindowPosition(win, &wx, &wy);
    mx = (int)floorf((gx - wx) / ui_user);
    my = (int)floorf((gy - wy) / ui_user);
    if (!slot_under_mouse(&t, &s)) return 0;
    while (s < P.nscenes && P.slots[t][s].kind != SLOT_EMPTY) s++;
    if (s >= P.nscenes) s = sess_add_scene(NULL);
    if (s < 0) return 0;
    slot_import_path(t, s, path);
    return 1;
}

/* ---- what was played, for Capture */

void sess_close(int t, long to)
{
    int i = sess_open_seg[t];
    if (i >= 0 && i < sess_nlog) sess_log[i].to = to > sess_log[i].from ? to : sess_log[i].from;
    sess_open_seg[t] = -1;
}

void sess_track(void)                    /* each frame: notice launches, stops and the loop going round */
{
    static int ready;
    if (!ready) {
        for (int t = 0; t < MAX_TRACKS; t++) sess_open_seg[t] = -1, sess_seen[t] = SESS_TIMELINE;
        ready = 1;
    }
    long head = P.playhead;
    int wrapped = P.playing && head < sess_last_head - SR / 20;
    for (int t = 0; t < P.ntracks; t++) {
        int now = P.playing ? P.sess_play[t] : SESS_STOPPED;
        if (wrapped && sess_open_seg[t] >= 0) {   /* the loop went round: what played to its end, then again */
            int looping = P.loop && P.loop_end > P.loop_start;
            long end = looping ? P.loop_end : sess_last_head, again = looping && head >= P.loop_start ? P.loop_start : head;
            SessSeg prev = sess_log[sess_open_seg[t]];
            sess_close(t, end);
            if (sess_seen[t] >= 0 && now == sess_seen[t] && sess_nlog < 2048) {   /* from the loop's start, same launch */
                sess_log[sess_nlog] = (SessSeg){t, prev.slot, again, head, prev.at};
                sess_open_seg[t] = sess_nlog++;
            }
        }
        if (now == sess_seen[t]) {
            if (sess_open_seg[t] >= 0) sess_log[sess_open_seg[t]].to = head;
            continue;
        }
        sess_close(t, now >= 0 ? P.sess_at[t] : head);
        if (now >= 0 && now < P.nscenes && sess_nlog < 2048) {
            sess_log[sess_nlog] = (SessSeg){t, P.slots[t][now], P.sess_at[t], head, P.sess_at[t]};
            sess_open_seg[t] = sess_nlog++;
        }
        sess_seen[t] = now;
    }
    sess_last_head = head;
}

/* Writes what was played onto the timeline where it played, replacing what the timeline had there. */
int sess_capture(void)
{
    for (int t = 0; t < P.ntracks; t++) sess_close(t, P.playhead);
    for (int t = 0; t < MAX_TRACKS; t++) sess_seen[t] = SESS_NONE;   /* (what's playing now starts afresh) */
    if (!sess_nlog) {
        status("Nothing played yet: launch some slots, then Capture writes them onto the timeline");
        return 0;
    }
    static Clip made[MAX_CLIPS];
    int nmade = 0;
    for (int i = 0; i < sess_nlog; i++) {         /* first what to write (a looped stretch is read before it's replaced) */
        const SessSeg *g = &sess_log[i];
        if (g->to - g->from < SR / 100) continue;
        long L = slot_frames(&P, &g->slot, g->at);
        if (L < 64) continue;
        /* where it still shows: its stretch less what later segments on the track played over (looping
         * goes over the same bars again, and the newest pass is what you heard last) */
        long iv[64][2];
        int niv = 1;
        iv[0][0] = g->from, iv[0][1] = g->to;
        for (int j = i + 1; j < sess_nlog && niv; j++) {
            const SessSeg *h = &sess_log[j];
            if (h->track != g->track || h->to - h->from < SR / 100) continue;
            long cut[64][2];
            int nc = 0;
            for (int k = 0; k < niv; k++) {
                long a = iv[k][0], b = iv[k][1];
                if (h->to <= a || h->from >= b) {
                    if (nc < 64) cut[nc][0] = a, cut[nc][1] = b, nc++;
                    continue;
                }
                if (h->from > a && nc < 64) cut[nc][0] = a, cut[nc][1] = h->from, nc++;
                if (h->to < b && nc < 64) cut[nc][0] = h->to, cut[nc][1] = b, nc++;
            }
            memcpy(iv, cut, sizeof(long) * 2 * (size_t)nc);
            niv = nc;
        }
        for (int k = 0; k < niv; k++) {
            long lo = iv[k][0], hi = iv[k][1];
            if (hi - lo < SR / 50) continue;
            /* the passes as they played: counted from the launch, whatever the loop did */
            for (long pass = g->at + (long)floor((double)(lo - g->at) / L) * L; pass < hi && nmade < MAX_CLIPS; pass += L) {
                Clip src[256];
                int ns = 0;
                if (g->slot.kind == SLOT_CLIP) {
                    Clip c = g->slot.clip;
                    c.track = g->track;
                    c.start = pass;
                    long room = c.pattern ? L : P.samples[c.sample].frames - c.offset;
                    c.length = room < L ? room : L;
                    src[ns++] = c;
                } else {
                    long a = meter_bar_frame(&P, g->slot.bar), z = a + L;
                    for (int ci = 0; ci < P.nclips && ns < 256; ci++) {
                        Clip c = P.clips[ci];
                        if (c.track != g->track || c.start >= z || c.start + c.length <= a) continue;
                        if (c.start < a) {
                            c.offset += a - c.start;
                            c.length -= a - c.start;
                            c.start = a;
                            c.fade_in = 0;
                        }
                        if (c.start + c.length > z) c.length = z - c.start;
                        c.start += pass - a;
                        src[ns++] = c;
                    }
                }
                for (int m2 = 0; m2 < ns && nmade < MAX_CLIPS; m2++) {   /* each trimmed to what showed */
                    Clip c = src[m2];
                    if (c.start < lo) {
                        long cut = lo - c.start;
                        c.start = lo;
                        c.offset += cut;
                        c.length -= cut;
                        c.fade_in = 0;
                    }
                    if (c.start + c.length > hi) {
                        c.length = hi - c.start;
                        c.fade_out = 0;
                    }
                    if (c.length >= SR / 50) made[nmade++] = c;   /* (not a sliver where it stopped) */
                }
            }
        }
    }
    int tracks = 0;
    LOCK();
    for (int i = 0; i < sess_nlog; i++) {         /* then clear where they go */
        const SessSeg *g = &sess_log[i];
        if (g->to - g->from < SR / 100) continue;
        for (int c = 0; c < P.nclips; c++)
            if (P.clips[c].track == g->track && P.clips[c].start < g->to && P.clips[c].start + P.clips[c].length > g->from) {
                int r = clip_split_one(c, g->from);
                int mid = r >= 0 ? r : c;
                clip_split_one(mid, g->to);
            }
        for (int c = P.nclips - 1; c >= 0; c--)
            if (P.clips[c].track == g->track && P.clips[c].start >= g->from && P.clips[c].start + P.clips[c].length <= g->to)
                clip_delete(c);
    }
    UNLOCK();
    int seen[MAX_TRACKS] = {0};
    for (int i = 0; i < nmade && P.nclips < MAX_CLIPS; i++) {
        int c = clip_add(made[i].sample, made[i].track, made[i].start, made[i].offset, made[i].length);
        if (c < 0) break;
        LOCK();
        P.clips[c] = made[i];
        UNLOCK();
        tracks += !seen[made[i].track];
        seen[made[i].track] = 1;
    }
    sess_nlog = 0;
    for (int t = 0; t < MAX_TRACKS; t++) sess_open_seg[t] = -1;
    sel_clear();
    status("Captured: %d clip%s on %d track%s, where they played. Ctrl+Z takes them off again", nmade, nmade == 1 ? "" : "s",
           tracks, tracks == 1 ? "" : "s");
    return nmade;
}

void sess_forget(void)                   /* a new or opened song: nothing played yet */
{
    sess_nlog = 0;
    for (int t = 0; t < MAX_TRACKS; t++) sess_open_seg[t] = -1, sess_seen[t] = SESS_TIMELINE;
}

void sess_rename_scene(int s)
{
    tf_end(1);
    tf.buf = P.scene_names[s];
    tf.cap = sizeof P.scene_names[s];
    tf.path = 0;
    tf.caret = (size_t)-1;
    tf_drawn = 1;
    tf.r = sess_scene_r[s];
    snprintf(tf.orig, sizeof tf.orig, "%s", tf.buf);
    SDL_StartTextInput();
    sess_renaming = s;
}

void sess_action(int act)
{
    int t = sess_menu_t, s = sess_menu_s;
    Slot *sl = t >= 0 && s >= 0 ? &P.slots[t][s] : NULL;
    switch (act) {
    case SMA_PUT:
        if (selected >= 0 && selected < P.nclips && sl) {
            const Clip *c = &P.clips[selected];
            char nm[48];
            snprintf(nm, sizeof nm, "%s", c->pattern > 0 ? "Notes" : c->pattern < 0 ? "Drums" : P.samples[c->sample].name);
            if (c->pattern < 0) {
                const DrumPat *d = drum_find_c(&P, -c->pattern);
                if (d) snprintf(nm, sizeof nm, "%s", d->name);
            } else if (c->pattern > 0) {
                snprintf(nm, sizeof nm, "%s", P.samples[c->sample].name);
            }
            LOCK();
            slot_fill_clip(t, s, c, nm);
            UNLOCK();
            status("The selected clip is in %s on track %d, %g beats round", P.scene_names[s], t + 1, sl->beats);
        }
        break;
    case SMA_SECTION: {
        int bar = (int)floor(meter_bars(&P, P.playhead) + 1e-9), si = sec_at_bar(bar);
        int b0 = si >= 0 ? P.sections[si].bar : bar - bar % 4, nb = si >= 0 ? P.sections[si].bars : 4;
        LOCK();
        memset(sl, 0, sizeof *sl);
        sl->kind = SLOT_RANGE;
        sl->bar = b0;
        sl->beats = meter_beats(&P, meter_bar_frame(&P, b0 + nb)) - meter_beats(&P, meter_bar_frame(&P, b0));
        if (si >= 0) snprintf(sl->name, sizeof sl->name, "%s", P.sections[si].name);
        else snprintf(sl->name, sizeof sl->name, "Bars %d-%d", b0 + 1, b0 + nb);
        UNLOCK();
        status("Track %d's timeline from bar %d, %d bars, loops in this slot", t + 1, b0 + 1, nb);
        break;
    }
    case SMA_LONGER: case SMA_SHORTER:
        if (sl && sl->kind != SLOT_EMPTY) {
            double nb = act == SMA_LONGER ? sl->beats * 2 : sl->beats / 2;
            if (nb >= 0.25 && nb <= 256) {
                LOCK();
                sl->beats = nb;
                UNLOCK();
            }
            status("%s: %g beats round", sl->name, sl->beats);
        }
        break;
    case SMA_CLEAR:
        if (sl) slot_clear(t, s);
        break;
    case SMA_COPY: slot_copy(t, s); break;
    case SMA_PASTE: slot_paste(t, s); break;
    case SMA_IMPORT: slot_import_dialog(t, s); break;
    case SMA_TO_TIMELINE: slot_to_timeline(t, s); break;
    case SMA_RENAME_S: sess_rename_scene(s); break;
    case SMA_LAUNCH_S: sess_launch_scene(s); break;
    case SMA_INSERT_S: {
        int n = sess_add_scene(NULL);
        if (n >= 0 && s >= 0 && s + 1 < n) {      /* moved in after this one */
            LOCK();
            for (int tt = 0; tt < MAX_TRACKS; tt++) {
                Slot tmp = P.slots[tt][n];
                memmove(&P.slots[tt][s + 2], &P.slots[tt][s + 1], (size_t)(n - s - 1) * sizeof(Slot));
                P.slots[tt][s + 1] = tmp;
            }
            char nm[24];
            memcpy(nm, P.scene_names[n], sizeof nm);
            memmove(P.scene_names[s + 2], P.scene_names[s + 1], (size_t)(n - s - 1) * sizeof nm);
            memcpy(P.scene_names[s + 1], nm, sizeof nm);
            UNLOCK();
        }
        break;
    }
    case SMA_REMOVE_S: sess_remove_scene(s); break;
    case SMA_CAPTURE: sess_capture(); break;
    }
}

void slot_release(void)                  /* a slot let go: a click launches it, a drag moves or copies it */
{
    int ft = drag.clip / MAX_SCENES, fs = drag.clip % MAX_SCENES, t, s;
    if (ft >= P.ntracks || fs >= P.nscenes) return;
    int mode = P.slots[ft][fs].mode;
    if (mode == LAUNCH_HOLD) sess_release_now(ft);   /* hold: let go, it stops (whether clicked or dragged) */
    if (abs(mx - drag.sx) + abs(my - drag.sy) <= 4) {
        if (mode == LAUNCH_HOLD) return;
        if (mode == LAUNCH_TOGGLE && (P.sess_play[ft] == fs || P.sess_q[ft] == fs)) {   /* toggle: again stops it */
            sess_queue(ft, SESS_TIMELINE);
            status("%s stops on the next %s", P.slots[ft][fs].name, sess_quant == 1 ? "beat" : "bar");
            return;
        }
        sess_launch(ft, fs);
        return;
    }
    if (!slot_under_mouse(&t, &s) || (t == ft && s == fs)) return;
    int copy = (mods & KMOD_CTRL) != 0;
    Slot moved = P.slots[ft][fs];
    if (!copy) slot_clear(ft, fs);
    LOCK();
    P.slots[t][s] = moved;
    UNLOCK();
    status("%s \"%s\" to %s on track %d", copy ? "Copied" : "Moved", moved.name, P.scene_names[s], t + 1);
}

/* In the timeline view, what the session is doing to each track, over its lane: so it's never a mystery
 * why a lane's clips aren't what's playing. */
void session_lane_marks(void)
{
    if (P.sess_mode == SESS_ONLY_TIMELINE) return;
    for (int t = 0; t < P.ntracks; t++) {
        int sp = P.sess_play[t], q = P.sess_q[t];
        const char *what = NULL;
        char buf[120];
        if (sp >= 0 && sp < P.nscenes) {
            if (strcmp(P.slots[t][sp].name, P.scene_names[sp]))
                snprintf(buf, sizeof buf, "Playing the session: %s (%s). Tab to see it", P.slots[t][sp].name, P.scene_names[sp]);
            else snprintf(buf, sizeof buf, "Playing the session: %s. Tab to see it", P.slots[t][sp].name);
            what = buf;
        } else if (sp == SESS_STOPPED) {
            what = "Stopped in the session: silent. Tab, then the arrow beside its slots, brings the timeline back";
        } else if (P.sess_mode == SESS_ONLY_SESSION && P.playing) {
            what = "Silent: the session plays alone (Tab: Plays)";
        } else if (q != SESS_NONE && q >= 0 && q < P.nscenes) {
            snprintf(buf, sizeof buf, "Going over to the session: %s, on the next %s", P.slots[t][q].name,
                     sess_quant == 1 ? "beat" : "bar");
            what = buf;
        }
        if (!what) continue;
        SDL_Rect r = {L.tl_x, track_y(t), L.tl_w, L.lane_h};
        fill(r, alpha(BG, 150));
        Col c = sp >= 0 ? TRACK_COL[t] : DIM;
        fill((SDL_Rect){r.x, r.y, 3, r.h}, c);
        SDL_Rect lab = {r.x + 8, r.y + (r.h - font_h - 6) / 2, text_w(what) + 12, font_h + 6};
        if (lab.w > r.w - 16) lab.w = r.w - 16;
        fill(lab, alpha(PANEL, 230));
        text_clip(lab, lab.x + 6, lab.y + 3, what, sp >= 0 ? TEXT : DIM);
    }
}

void session_header(void)                 /* the rows above the tracks, at the left */
{
    SDL_Rect hr = {L.left, L.top + L.ruler, L.head, L.arr + L.chd};
    fill(hr, tone(PANEL, 0.9f));
    line(L.left, hr.y + hr.h - 1, L.left + L.head, hr.y + hr.h - 1, LINE);
    text(L.left + 12, hr.y + (hr.h - font_h) / 2, "SCENES", DIM);
    SDL_Rect add = {L.left + L.head - 32, hr.y + (hr.h - 22) / 2, 22, 22};
    tip(add, "A new scene (a column of slots)");
    if (button2(add, "+", 0, P.nscenes < MAX_SCENES)) sess_add_scene(NULL);
}

void session_grid(void)
{
    SDL_Rect area = {L.tl_x, L.top, L.tl_w, H - L.top - L.status};
    fill(area, BG);
    int ctl = 58;                                /* the stop and back-to-timeline column */
    int gx = L.tl_x + ctl, gw = W - gx;
    int cw = P.nscenes ? (gw - 4) / P.nscenes : 120;
    cw = cw < 104 ? 104 : cw > 170 ? 170 : cw;
    int maxs = P.nscenes * cw - gw;
    sess_scroll = sess_scroll > maxs ? maxs : sess_scroll;
    sess_scroll = sess_scroll < 0 ? 0 : sess_scroll;
    char buf[96];

    /* the toolbar: what plays, when launches start, capture; then the rest, as far as there's room */
    SDL_Rect tb = {L.tl_x, L.top, L.tl_w, L.ruler};
    fill(tb, PANEL);
    int x = L.tl_x + 8, y = L.top + (L.ruler - 28) / 2;
    static const char *const MODES[SESS_MODES] = {"Plays: timeline + session", "Plays: session only", "Plays: timeline only"};
    int mode = P.sess_mode;
    int mw = text_w(MODES[0]) + 40;
    if (dropdown((SDL_Rect){x, y, mw, 28}, "", MODES, SESS_MODES, &mode, 0)) {
        LOCK();
        P.sess_mode = mode;
        UNLOCK();
        static const char *const SAY[SESS_MODES] = {
            "Timeline + session: tracks with a launched slot play it, the others play the timeline",
            "Session only: just the launched slots play; the timeline is silent",
            "Timeline only: the timeline plays as written; launched slots wait (they're kept)"};
        status("%s", SAY[mode]);
    }
    tip((SDL_Rect){x, y, mw, 28}, "What plays: launched slots with the timeline on the other tracks, the session alone, or "
                                  "the timeline alone (the slots stay launched)");
    x += mw + 6;
    static const char *const Q[3] = {"Launch: next bar", "Launch: next beat", "Launch: at once"};
    dropdown((SDL_Rect){x, y, 160, 28}, "", Q, 3, &sess_quant, 0);
    tip((SDL_Rect){x, y, 160, 28}, "When a launched slot starts: on the next bar, the next beat, or straight away");
    x += 166;
    snprintf(buf, sizeof buf, sess_nlog ? "Capture (%d)" : "Capture", sess_nlog);
    if (button((SDL_Rect){x, y, 110, 28}, buf, sess_nlog > 0)) sess_capture();
    tip((SDL_Rect){x, y, 110, 28}, "Write everything you've launched onto the timeline, where it played");
    x += 116;
    struct { const char *label; int w; const char *tipt; } more[3] = {
        {"Stop all", 80, "Every track stops on the next bar (the timeline stays silent too)"},
        {"All timeline", 104, "Every track goes back to playing the timeline"},
        {"From sections", 126, "A scene for each of the song's sections, each track looping its part"}};
    for (int k = 0; k < 3; k++) {
        if (x + more[k].w > W - 4) break;
        SDL_Rect b = {x, y, more[k].w, 28};
        tip(b, "%s", more[k].tipt);
        if (button(b, more[k].label, 0)) {
            if (k == 0) sess_all(SESS_STOPPED);
            else if (k == 1) sess_all(SESS_TIMELINE);
            else sess_from_sections();
        }
        x += more[k].w + 4;
    }

    /* the scene headers */
    SDL_Rect sh = {L.tl_x, L.top + L.ruler, L.tl_w, L.arr + L.chd};
    fill(sh, tone(PANEL, 0.9f));
    line(L.tl_x, sh.y + sh.h - 1, W, sh.y + sh.h - 1, LINE);
    SDL_Rect clip_r = {gx, L.top, gw, H - L.top - L.status};
    if (!P.nscenes)
        text_clip(sh, gx + 8, sh.y + (sh.h - font_h) / 2, "No scenes yet: From sections makes one per section, or + beside SCENES adds one",
                  tone(DIM, 0.8f));
    SDL_RenderSetClipRect(ren, &clip_r);
    for (int s = 0; s < P.nscenes; s++) {
        SDL_Rect r = {gx + s * cw - sess_scroll + 2, sh.y + 4, cw - 4, sh.h - 8};
        sess_scene_r[s] = r;
        int playing = 0;
        for (int t = 0; t < P.ntracks; t++) playing |= P.sess_play[t] == s;
        int hov = hit(r) && hit(clip_r) && drag.kind == D_NONE;
        fill(r, playing ? tone(ACCENT, 0.7f) : hov ? PANEL3 : PANEL2);
        outline(r, LINE);
        if (sess_renaming == s && tf.buf == P.scene_names[s]) {
            textfield(r, P.scene_names[s], sizeof P.scene_names[s], 0, "name");
        } else {
            for (int i = 0; i < 5; i++)          /* a launch triangle */
                line(r.x + 8 + i, r.y + r.h / 2 - 5 + i, r.x + 8 + i, r.y + r.h / 2 + 5 - i, playing ? DARK : HEAD);
            text_clip((SDL_Rect){r.x + 18, r.y, r.w - 20, r.h}, r.x + 20, r.y + (r.h - font_h) / 2, P.scene_names[s],
                      playing ? DARK : TEXT);
        }
        if (hov) {
            tip(r, "%s: click to launch every slot in it, double-click to rename, right-click for more", P.scene_names[s]);
            if (lpress && ldouble) {
                lpress = 0;
                sess_rename_scene(s);
            } else if (lpress) {
                lpress = 0;
                sess_launch_scene(s);
                drag = (typeof(drag)){.kind = D_SCENE, .clip = s, .sx = mx, .sy = my};   /* (its hold slots: until let go) */
            } else if (rpress) {
                rpress = 0;
                sess_menu_t = -1;
                sess_menu_s = s;
                static const char *const LB[5] = {"Launch it", "Rename", "A new scene after it", "Remove the scene",
                                                  "Capture to the timeline"};
                static const int AC[5] = {SMA_LAUNCH_S, SMA_RENAME_S, SMA_INSERT_S, SMA_REMOVE_S, SMA_CAPTURE};
                cm_open_list(CM_SESS, s, LB, AC, NULL, 5);
            }
        }
    }
    SDL_RenderSetClipRect(ren, NULL);
    if (sess_renaming >= 0 && (sess_renaming >= P.nscenes || tf.buf != P.scene_names[sess_renaming])) {
        if (sess_renaming < P.nscenes && !P.scene_names[sess_renaming][0])
            snprintf(P.scene_names[sess_renaming], sizeof P.scene_names[0], "Scene %d", sess_renaming + 1);
        sess_renaming = -1;
    }

    /* each track: stop and back-to-timeline, then its slots */
    for (int t = 0; t < P.ntracks; t++) {
        int ty = track_y(t), th = L.lane_h;
        fill((SDL_Rect){L.tl_x, ty, L.tl_w, th}, t % 2 ? BG : tone(BG, 1.18f));
        SDL_Rect sb = {L.tl_x + 4, ty + (th - 22) / 2, 24, 22}, bk = {L.tl_x + 30, ty + (th - 22) / 2, 24, 22};
        sess_stop_r[t] = sb;
        sess_back_r[t] = bk;
        int stopped = P.sess_play[t] == SESS_STOPPED, on_tl = P.sess_play[t] == SESS_TIMELINE;
        if (button(sb, "", stopped)) sess_queue(t, SESS_STOPPED);
        fill((SDL_Rect){sb.x + 7, sb.y + 6, 10, 10}, stopped ? btn_fg(1) : TEXT);
        tip(sb, "Stop this track on the next bar (it plays nothing, not even the timeline)");
        if (button(bk, "", on_tl)) sess_queue(t, SESS_TIMELINE);
        for (int i = 0; i < 4; i++) line(bk.x + 7 + i, bk.y + 11 - i, bk.x + 7 + i, bk.y + 11 + i, on_tl ? btn_fg(1) : TEXT);
        fill((SDL_Rect){bk.x + 10, bk.y + 10, 8, 3}, on_tl ? btn_fg(1) : TEXT);
        tip(bk, on_tl ? "Playing the timeline" : "Back to the timeline on the next bar");
        SDL_RenderSetClipRect(ren, &clip_r);
        for (int s = 0; s < P.nscenes; s++) {
            SDL_Rect r = {gx + s * cw - sess_scroll + 2, ty + 3, cw - 4, th - 6};
            sess_cell_[t][s] = r;
            const Slot *sl = &P.slots[t][s];
            int hov = hit(r) && hit(clip_r) && drag.kind == D_NONE;
            int playing = P.sess_play[t] == s, queued = P.sess_q[t] == s;
            if (sl->kind == SLOT_EMPTY) {
                int drop = drag.kind == D_LIB && hit(r) && hit(clip_r);
                fill(r, drop ? alpha(ACCENT, 120) : hov ? PANEL2 : tone(PANEL, 0.95f));
                outline(r, tone(LINE, 0.8f));
                int moving = drag.kind == D_SLOT && hit(r) && hit(clip_r) && abs(mx - drag.sx) + abs(my - drag.sy) > 4;
                if (moving) outline(r, ACCENT);
                if (hov) tip(r, "An empty slot: drop a sound on it (from the library or your files), or right-click to paste "
                                "or import one");
            } else {
                Col c = TRACK_COL[t];
                fill(r, playing ? wash(c, 0.9f) : wash(c, hov ? 0.6f : 0.45f));
                if (queued && (SDL_GetTicks() / 250) % 2 == 0) outline(r, HEAD);
                else outline(r, playing ? TEXT : tone(LINE, 1.1f));
                for (int i = 0; i < 5; i++)
                    line(r.x + 7 + i, r.y + 8 - 0 + i - 5 + 5, r.x + 7 + i, r.y + 18 - i, playing ? DARK : alpha(DARK, 160));
                text_clip((SDL_Rect){r.x + 16, r.y, r.w - 18 - (r.w > 90 ? 56 : 0), r.h}, r.x + 18, r.y + 6, sl->name, DARK);
                if (th > 40) {
                    if (sl->kind == SLOT_RANGE) {         /* "bars 13-20": the stretch of the timeline it loops */
                        long a = meter_bar_frame(&P, sl->bar), z = meter_beats_frame(&P, meter_beats(&P, a) + sl->beats);
                        int last = (int)ceil(meter_bars(&P, z) - 1e-6);
                        if (last - sl->bar <= 1) snprintf(buf, sizeof buf, "bar %d", sl->bar + 1);
                        else snprintf(buf, sizeof buf, "bars %d-%d", sl->bar + 1, last);
                    } else {
                        snprintf(buf, sizeof buf, fmod(sl->beats, 4) == 0 ? "%.0f bar%s" : "%g beats",
                                 fmod(sl->beats, 4) == 0 ? sl->beats / 4 : sl->beats, sl->beats == 4 ? "" : "s");
                    }
                    text_clip((SDL_Rect){r.x + 4, r.y, r.w - 8, r.h}, r.x + 8, r.y + 8 + font_h, buf, alpha(DARK, 150));
                }
                if (playing && P.playing) {       /* how far round it is */
                    long L2 = slot_frames(&P, sl, P.sess_at[t]);
                    if (L2 > 0) {
                        long into = ((P.playhead - P.sess_at[t]) % L2 + L2) % L2;
                        fill((SDL_Rect){r.x + 2, r.y + r.h - 5, (int)((double)into / L2 * (r.w - 4)), 3}, DARK);
                    }
                }
                if (hov)
                    tip(r, "%s: click to launch it on track %d (the next %s), drag it to another slot to move it (Ctrl: "
                           "copy). Right-click to copy, paste, import or put it on the timeline", sl->name, t + 1,
                        sess_quant == 0 ? "bar" : sess_quant == 1 ? "beat" : "moment");
            }
            if (sl->kind != SLOT_EMPTY && r.w > 90 && r.h > 22) {   /* how it launches: click to change */
                SDL_Rect mb = {r.x + r.w - 54, r.y + 3, 50, font_h + 4};
                int mh = hit(mb) && hit(clip_r) && drag.kind == D_NONE;
                fill(mb, mh ? alpha(DARK, 150) : alpha(DARK, 90));
                text_center(mb, LAUNCH_NAMES[sl->mode], mh ? TEXT : tone(TEXT, 0.85f));
                if (mh) {
                    static const char *const MT[LAUNCH_MODES] = {
                        "Loop: plays round and round until another slot or Stop takes over",
                        "Toggle: click to start it, click again to stop it", "Hold: plays while you hold it down",
                        "Once: plays one pass, then the track goes back to the timeline"};
                    tip(mb, "%s. Click to change", MT[sl->mode]);
                    if (lpress) {
                        lpress = 0;
                        LOCK();
                        P.slots[t][s].mode = (sl->mode + 1) % LAUNCH_MODES;
                        UNLOCK();
                        status("%s: %s", sl->name, MT[P.slots[t][s].mode]);
                    }
                }
            }
            if (hov && lpress && sl->kind != SLOT_EMPTY) {   /* a click launches it; a drag moves it (Ctrl: copies) */
                lpress = 0;
                drag = (typeof(drag)){.kind = D_SLOT, .clip = t * MAX_SCENES + s, .sx = mx, .sy = my};
                if (sl->mode == LAUNCH_HOLD) sess_launch(t, s);   /* hold: from the press */
            } else if (hov && rpress) {
                rpress = 0;
                sess_menu_t = t;
                sess_menu_s = s;
                int has_sel = selected >= 0 && selected < P.nclips, paste = slot_can_paste();
                if (sl->kind != SLOT_EMPTY) {
                    const char *LB[9] = {"Copy", "Paste here", "Put it on the timeline at the playhead", "Twice as long",
                                         "Half as long", "Import a sound file here...", "Put the selected clip here",
                                         "Clear the slot", "Rename the scene"};
                    int AC[9] = {SMA_COPY, SMA_PASTE, SMA_TO_TIMELINE, SMA_LONGER, SMA_SHORTER, SMA_IMPORT, SMA_PUT,
                                 SMA_CLEAR, SMA_RENAME_S};
                    int en[9] = {1, paste, 1, 1, 1, 1, has_sel, 1, 1};
                    cm_open_list(CM_SESS, s, LB, AC, en, 9);
                } else {
                    const char *LB[5] = {"Paste here", "Import a sound file here...", "Put the selected clip here",
                                         "Loop the timeline here (the section at the playhead)", "Rename the scene"};
                    int AC[5] = {SMA_PASTE, SMA_IMPORT, SMA_PUT, SMA_SECTION, SMA_RENAME_S};
                    int en[5] = {paste, 1, has_sel, 1, 1};
                    cm_open_list(CM_SESS, s, LB, AC, en, 5);
                }
            }
        }
        SDL_RenderSetClipRect(ren, NULL);
        line(L.tl_x, ty + th - 1, W, ty + th - 1, tone(LINE, 0.7f));
    }
    if (drag.kind == D_SLOT && abs(mx - drag.sx) + abs(my - drag.sy) > 4) {   /* a slot being dragged */
        int ft = drag.clip / MAX_SCENES, fs = drag.clip % MAX_SCENES;
        if (ft < P.ntracks && fs < P.nscenes) {
            SDL_Rect g = {mx - 50, my - 12, 100, 24};
            fill(g, alpha(TRACK_COL[ft], 200));
            outline(g, TEXT);
            text_clip(g, g.x + 6, g.y + (g.h - font_h) / 2, P.slots[ft][fs].name, DARK);
            SDL_SetCursor(cur_hand);
        }
    }
    if (hit(clip_r) && wheel_y && drag.kind == D_NONE && P.nscenes) {   /* the wheel scrolls the scenes */
        sess_scroll -= wheel_y * 60;
        wheel_y = 0;
    }
}

void timeline(void)
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

    /* beat grid and bar numbers, by the tempo map */
    TlGrid tg = {area};
    grid_walk(L.tl_x, W, 0, tl_grid_fn, &tg);
    for (int i = 0; i < P.nchanges; i++) {       /* tempo / time signature changes: a flag each */
        const MeterChange *mc = &P.changes[i];
        int x = (int)frame_to_x(meter_bar_frame(&P, mc->bar));
        if (x < L.tl_x - 80 || x > W) continue;
        char lab[32], num[12];
        snprintf(num, sizeof num, "%d", mc->bar + 1);
        snprintf(lab, sizeof lab, "%.0f %d/%d", mc->bpm, mc->num, mc->den);
        SDL_Rect f = {x + 8 + text_w(num), L.top + 2, text_w(lab) + 10, font_h + 2};
        change_flag[i] = f;
        int hov = hit(f) && drag.kind == D_NONE;
        fill(f, hov ? ACCENT : alpha(ACCENT, 170));
        text(f.x + 5, f.y + 1, lab, DARK);
        line(x, L.top + 2, x, L.top + 24, ACCENT);
        if (hov && lpress) {                     /* to it: BPM and the box edit it */
            lpress = 0;
            playhead_to(meter_bar_frame(&P, mc->bar));
            status("Bar %d: %.0f BPM, %d/%d. BPM and the time signature box edit it; right-click the flag removes it",
                   mc->bar + 1, mc->bpm, mc->num, mc->den);
        } else if (hov && rpress) {
            rpress = 0;
            change_remove(i);
            break;
        }
    }
    time_scale((SDL_Rect){L.tl_x, L.top + 24, L.tl_w, L.ruler - 24});
    arranger_strip();
    chord_strip();
    line(L.tl_x, L.top + 24, W, L.top + 24, tone(LINE, 0.8f));
    line(L.tl_x, L.top + L.ruler - 1, W, L.top + L.ruler - 1, LINE);

    /* clips */
    int hover_edge = 0;
    for (int i = 0; i < P.nclips; i++) {
        const Clip *c = &P.clips[i];
        SDL_Rect r = clip_rect(c);
        if (r.x > W || r.x + r.w < L.tl_x) continue;
        {
            char pos[48];
            pos_text(c->start, pos, sizeof pos);
            char gain[24] = "";
            if (c->gain_db != 0) snprintf(gain, sizeof gain, ", %+.1f dB", c->gain_db);
            const DrumPat *dp = c->pattern < 0 ? drum_find_c(&P, -c->pattern) : NULL;
            tip(r, "%s%s on %s, %s, %.2f s%s. Drag to move it, its edges to trim%s. Double-click: %s",
                c->pattern > 0 ? "Notes: " : dp ? "Drums: " : "", dp ? dp->name : P.samples[c->sample].name,
                P.tracks[c->track].name, pos, (double)c->length / SR, gain, dp ? " (the pattern repeats)" : "",
                c->pattern > 0 ? "the piano roll" : dp ? "its steps in the Drums panel" : "the clip editor");
        }
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
        else if (c->pattern < 0) draw_clip_drums(c, r, body_y, body_h, wave);
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
        const DrumPat *cdp = c->pattern < 0 ? drum_find_c(&P, -c->pattern) : NULL;
        const char *cname = cdp ? cdp->name : s->name, *ckind = cdp ? "Drums: " : c->pattern ? "Notes: " : "";
        if (c->gain_db != 0) snprintf(label, sizeof label, "%s%s  %+.1fdB", ckind, cname, c->gain_db);
        else snprintf(label, sizeof label, "%s%s", ckind, cname);
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
        Clip ghost = {drag.sample, lane_at(my), snap(x_to_frame(mx)), 0, s->frames, 0, 0, 0, 0, 0, 0};
        SDL_Rect r = clip_rect(&ghost);
        fill(r, alpha(TRACK_COL[ghost.track], 90));
        outline(r, TEXT);
    }
    if (drag.kind == D_BEAT && hit(area)) {   /* one ghost row per part of the beat */
        long pat;
        int reps, parts = beat_layout(drag.sample, &pat, &reps);
        for (int i = 0; i < parts; i++) {
            int t = lane_at(my) + i;
            Clip ghost = {0, t, snap(x_to_frame(mx)), 0, pat * reps, 0, 0, 0, 0, 0, 0};
            SDL_Rect r = clip_rect(&ghost);
            fill(r, alpha(TRACK_COL[t % MAX_TRACKS], 90));
            outline(r, TEXT);
        }
    }

    if (drag.kind == D_PART && hit(area)) {   /* where the part would be written (or the track the fills go on) */
        double a, b;
        int t = lane_at(my);
        Clip ghost = {0, t, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        if (drag.sample < PART_KINDS && part_drop_range(drag.sample, x_to_frame(mx), &a, &b)) {
            ghost.start = bar_floor(meter_beats_frame(&P, a));
            ghost.length = meter_beats_frame(&P, b) - ghost.start;
        } else if (drag.sample >= PART_KINDS) {
            ghost.start = 0;
            long end = project_end(&P);
            ghost.length = end > 0 ? end : meter_bar_frame(&P, 8);
        }
        if (ghost.length > 0) {
            SDL_Rect r = clip_rect(&ghost);
            fill(r, alpha(TRACK_COL[t % MAX_TRACKS], drag.sample < PART_KINDS ? 90 : 40));
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
    else if (drag.kind == D_MOVE || drag.kind == D_LIB || drag.kind == D_BEAT || drag.kind == D_PART) SDL_SetCursor(cur_hand);
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
            else if (found >= 0 && P.clips[found].pattern < 0) {   /* a drum clip: its pattern in the Drums panel */
                sel_track = P.clips[found].track;
                drums_show(-P.clips[found].pattern);
            }
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

void tri(int x, int cy, int hgt, int dir, Col c)  /* a filled triangle pointing left (-1) or right */
{
    float half = hgt * 6 / 10 + 0.5f, tip = dir > 0 ? x + hgt + 1.0f : x - hgt;
    float bx = dir > 0 ? (float)x : x + 1.0f;
    float xy[6] = {bx, cy + 0.5f - half, tip, cy + 0.5f, bx, cy + 0.5f + half};
    poly_fill(xy, 3, c);
}

void transport_icon(int kind, SDL_Rect r, Col c)
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

int transport_button(SDL_Rect r, int kind, int on, Col tint)
{
    static const char *const TIPS[] = {"To the start (Home)", "Back a bar (Ctrl+Left)", "Play (Space)", "Pause (Space)",
                                       "Stop: back to where play began; again for the start",
                                       "Forward a bar (Ctrl+Right)", "To the end (End)", "Loop the loop region (L)",
                                       "Record: plays the song and records the pads, MIDI keys and (on an armed track) the microphone"};
    tip(r, "%s", TIPS[kind]);
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
SDL_Rect transport_rect;

void status_bar(void)
{
    int y = H - L.status;
    fill((SDL_Rect){0, y, W, L.status}, PANEL);
    line(0, y, W, y, LINE);
    int bw = 30, gap = 3, n = 8, tw = n * bw + (n - 1) * gap, tx = (W - tw) / 2, by = y + 2, bh = L.status - 4;
    transport_rect = (SDL_Rect){tx, by, tw, bh};
    Col none = {0, 0, 0, 0};
    if (transport_button((SDL_Rect){tx, by, bw, bh}, TI_START, 0, none)) playhead_to(0);
    tx += bw + gap;
    if (transport_button((SDL_Rect){tx, by, bw, bh}, TI_BACK, 0, none)) playhead_to(meter_step(P.playhead, -1, 0));
    tx += bw + gap;
    if (transport_button((SDL_Rect){tx, by, bw, bh}, P.playing ? TI_PAUSE : TI_PLAY, P.playing, none)) toggle_play();
    tx += bw + gap;
    if (transport_button((SDL_Rect){tx, by, bw, bh}, TI_STOP, 0, none)) transport_stop();
    tx += bw + gap;
    if (transport_button((SDL_Rect){tx, by, bw, bh}, TI_FWD, 0, none)) playhead_to(meter_step(P.playhead, 1, 0));
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
