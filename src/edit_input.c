/* tinydaw: dragging and the keyboard. */
#include "ui.h"

void update_drag(void)
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
    case D_FADER: {                     /* a volume fader: track (or -1, the master) */
        double p = drag.sample ? 1.0 - (double)(my - drag.sy) / (drag.b > 1 ? drag.b : 1)
                               : (double)(mx - drag.sx) / (drag.a > 1 ? drag.a : 1);
        p = p < 0 ? 0 : p > 1 ? 1 : p;
        float v = (float)(1.5 * p * p);             /* the fader's law: a square root of the gain */
        LOCK();
        *fader_target(drag.track) = v;          /* a track, the master or a bus */
        UNLOCK();
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

void bounce_to_library(void)
{
    bends_update();
    int list[MAX_CLIPS], n = 0;
    if (drag.clip >= 0 && drag.clip < P.nclips && csel[drag.clip]) {
        for (int c = 0; c < P.nclips; c++)
            if (csel[c]) list[n++] = c;
    } else if (drag.clip >= 0 && drag.clip < P.nclips) {
        list[n++] = drag.clip;
    }
    bounce_clips(list, n);
}

void bounce_clips(const int *list, int n)
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

void end_drag(void)
{
    if (drag.kind == D_SEC || drag.kind == D_SECNEW || drag.kind == D_SECEND) {
        arranger_release();
        drag.kind = D_NONE;
        return;
    }
    if (drag.kind == D_CHD || drag.kind == D_CHDNEW || drag.kind == D_CHDEND) {
        chord_release();
        drag.kind = D_NONE;
        return;
    }
    if (drag.kind == D_SLOT) {
        slot_release();
        drag.kind = D_NONE;
        return;
    }
    if (drag.kind == D_SCENE) {                 /* a scene let go: its hold slots stop */
        int sc = drag.clip;
        for (int t = 0; t < P.ntracks && sc < P.nscenes; t++)
            if (P.slots[t][sc].mode == LAUNCH_HOLD && (P.sess_play[t] == sc || P.sess_q[t] == sc)) sess_release_now(t);
        drag.kind = D_NONE;
        return;
    }
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
        numedit.caret = 0;
        SDL_StartTextInput();
        status("Type a note like C4, F#3 or Bb2 (or a number), Enter to set it, Esc to cancel");
    }
    if (drag.kind == D_NUM && abs(mx - drag.sx) + abs(my - drag.sy) < 3) {
        *drag.num = drag.norig;                 /* a click, not a drag: type the value instead */
        numedit_begin(drag.num, drag.nmin, drag.nmax);
    }
    SDL_Rect area = {L.tl_x, L.lanes_y, L.tl_w, lanes_h()};
    if (drag.kind == D_NMOVE || drag.kind == D_NLEN || drag.kind == D_NBAND || drag.kind == D_VEL) roll_drag_end();
    if (drag.kind == D_LIB && (pads_drop(drag.sample) || drums_drop(drag.sample))) {
        /* dropped onto a pad, the instrument slot or a drum pattern */
    } else if (drag.kind == D_LIB && session_view && hit(area)) {
        sess_drop_sound(drag.sample);           /* onto an empty slot in the session */
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
void tempo_apply(double from)
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
void tempo_check(void)
{
    if (!tempo_from) return;
    if ((drag.kind == D_NUM && drag.num == &P.bpm) || (numedit.active && numedit.v == &P.bpm)) return;
    if (SDL_GetTicks() - tempo_at < 350) return;
    double from = tempo_from;
    tempo_from = 0;
    if (sel_count()) tempo_apply(from);
}

/* ---------------------------------------------------------------- keyboard */

void key(SDL_Keysym k)
{
    int ctrl = k.mod & KMOD_CTRL, hk = 0, hi = 0;
    long nudge = snap_frames() ? snap_frames() : SR / 100;
    if (browse_open) {                  /* the project browser takes the keyboard: Esc closes it */
        if (k.sym == SDLK_ESCAPE) browse_open = 0;
        return;
    }
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
    if ((menu_open || met_open || dd.open || chp_open) && k.sym == SDLK_ESCAPE) {
        menu_open = met_open = dd.open = chp_open = 0;
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
        case SDLK_o: browse_show(); return;
        case SDLK_n: request(ACT_NEW, NULL); return;
        case SDLK_r:                    /* reverse: every selected clip follows the one clicked last */
            if (selected >= 0) {
                int rev = !clip_rev(&P.clips[selected]);
                for (int i = 0; i < P.nclips; i++)
                    if (csel[i]) clip_variant(i, clip_semis(&P.clips[i]), rev);
                status(rev ? "Reversed %d clip%s" : "%d clip%s play forwards", sel_count(), sel_count() == 1 ? "" : "s");
            }
            return;
        case SDLK_1: case SDLK_2: case SDLK_3: case SDLK_4: case SDLK_5: case SDLK_6: case SDLK_7:
            open_panel(k.sym - SDLK_1);
            return;
        case SDLK_PAGEUP:
        case SDLK_PAGEDOWN:
            sec_jump(k.sym == SDLK_PAGEDOWN ? 1 : -1);
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
        case SDLK_EQUALS:
        case SDLK_PLUS:
        case SDLK_KP_PLUS:
        case SDLK_MINUS:
        case SDLK_KP_MINUS: {                   /* the interface's size, a step at a time */
            static const int STEPS[5] = {100, 125, 150, 175, 200};
            int cur = (int)lroundf(ui_user * 100), up = k.sym != SDLK_MINUS && k.sym != SDLK_KP_MINUS, pick = cur;
            if (up) {
                for (int i = 0; i < 5; i++)
                    if (STEPS[i] > cur) {
                        pick = STEPS[i];
                        break;
                    }
            } else {
                for (int i = 4; i >= 0; i--)
                    if (STEPS[i] < cur) {
                        pick = STEPS[i];
                        break;
                    }
            }
            if (pick != cur) ui_set(pick);
            return;
        }
        case SDLK_0:
        case SDLK_KP_0:
            ui_set(0);
            return;
        case SDLK_a:
            for (int i = 0; i < P.nclips; i++) csel[i] = 1;
            sel_fix_primary();
            status("All %d clips selected", P.nclips);
            return;
        case SDLK_c:
            if (slot_under_mouse(&hk, &hi)) {    /* over a session slot */
                slot_copy(hk, hi);
                return;
            }
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
            if (slot_under_mouse(&hk, &hi) && P.slots[hk][hi].kind != SLOT_EMPTY) {
                slot_copy(hk, hi);
                slot_clear(hk, hi);
                return;
            }
            if (sel_count()) {
                copy_selection();
                status("Cut %d clip%s", delete_selection(), nclipboard == 1 ? "" : "s");
            }
            return;
        case SDLK_v:
            if (slot_under_mouse(&hk, &hi)) {
                slot_paste(hk, hi);
                return;
            }
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
    case SDLK_TAB:                              /* the timeline, or the session view */
        if (roll_clip < 0) {
            session_view = !session_view;
            status(session_view ? "The session view: click a slot to launch it, a scene's name for the whole row (Tab: back)"
                                : "The timeline");
        }
        break;
    case SDLK_HOME:
        LOCK();
        P.playhead = P.loop ? P.loop_start : 0;
        UNLOCK();
        view_start = (double)P.playhead / SR;
        break;
    case SDLK_DELETE:
    case SDLK_BACKSPACE:
        if (slot_under_mouse(&hk, &hi) && P.slots[hk][hi].kind != SLOT_EMPTY) {
            char nm[24];
            memcpy(nm, P.slots[hk][hi].name, sizeof nm);
            slot_clear(hk, hi);
            status("Cleared \"%s\". Ctrl+Z puts it back", nm);
        } else if (chd_sel >= 0 && chd_sel < P.nchords && my >= L.top + L.ruler + L.arr && my < L.lanes_y && mx >= L.tl_x) {
            char nm[32];
            chord_label(&P, &P.chords[chd_sel], nm, sizeof nm);
            chord_remove(&P, chd_sel);
            chd_sel = -1;
            status("Removed %s from the chord track", nm);
        } else if (sel_count()) {
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
            playhead_to(meter_step(P.playhead, k.sym == SDLK_RIGHT ? 1 : -1, (k.mod & KMOD_SHIFT) != 0));
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
        int *sc = theory_open ? &th_scroll : pads_open ? &pads_scroll : drums_open ? &drums_scroll : mixer_open ? &mixer_scroll
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
