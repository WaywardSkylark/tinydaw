/* tinydaw: the piano roll and the timeline. */
#include "ui.h"

/* ---------------------------------------------------------------- the piano roll */

SDL_Rect roll_grid, roll_keys, roll_vel, roll_ruler;

int roll_voice = -1;             /* the note being heard while clicking or dragging */
T_norig norig[MAX_NOTES];
unsigned char nband_base[MAX_NOTES];

void roll_open(int c)
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

void roll_quiet(void)
{
    if (roll_voice < 0) return;
    LOCK();
    voice_stop(&P, -1, 1000 + roll_voice);
    UNLOCK();
    roll_voice = -1;
}

void roll_close(void)
{
    roll_quiet();
    roll_clip = -1;
    view_start = roll_saved_view;
    pps = roll_saved_pps;
    memset(nsel, 0, sizeof nsel);
}

void roll_hear(int note)                 /* plays a note through the clip's sound and track */
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

void roll_check(void)                    /* undo or a deletion can take the clip away */
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

long roll_origin(void) { return P.clips[roll_clip].start - P.clips[roll_clip].offset; }

SDL_Rect note_rect(int i)
{
    const Note *n = &P.notes[i];
    int x0 = (int)floor(frame_to_x(roll_origin() + n->start)), x1 = (int)ceil(frame_to_x(roll_origin() + n->start + n->length));
    return (SDL_Rect){x0, roll_grid.y + (roll_top - n->note) * roll_kh + 1, x1 - x0 < 3 ? 3 : x1 - x0, roll_kh - 1};
}

int roll_note_at(void)                   /* the note under the mouse, or -1 */
{
    for (int i = P.nnotes - 1; i >= 0; i--)
        if (P.notes[i].pattern == roll_pattern && hit(note_rect(i))) return i;
    return -1;
}

int roll_row_note(int y) { return roll_top - (y - roll_grid.y) / roll_kh; }

int nsel_count(void)
{
    int n = 0;
    for (int i = 0; i < P.nnotes; i++) n += nsel[i] && P.notes[i].pattern == roll_pattern;
    return n;
}

void nsel_only(int i)
{
    memset(nsel, 0, sizeof nsel);
    if (i >= 0) nsel[i] = 1;
}

void roll_fit_clip(void)                 /* the clip grows (by bars) to take in every note */
{
    Clip *c = &P.clips[roll_clip];
    long end = roll_origin() + pattern_end(&P, roll_pattern);
    if (end <= c->start + c->length) return;
    long len = bar_ceil(end) - c->start;
    LOCK();
    c->length = len;
    UNLOCK();
}

int roll_delete_selected(void)
{
    int n = 0;
    for (int i = P.nnotes - 1; i >= 0; i--)
        if (nsel[i] && P.notes[i].pattern == roll_pattern) {
            note_delete(i);
            n++;
        }
    return n;
}

void roll_duplicate_selected(void)       /* copies straight after the selection */
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

void roll_make_unique(void)              /* this clip gets its own copy of the notes */
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

/* ---- MIDI files: note clips in and out */

/* the first track with nothing on it from `at` on, or a new one; -1 when there's no room */
int open_track_from(long at, int *fresh)
{
    *fresh = 0;
    for (int t = 0; t < P.ntracks; t++) {
        int busy = 0, any = 0;
        for (int c = 0; c < P.nclips; c++)
            if (P.clips[c].track == t) {
                any = 1;
                busy |= P.clips[c].start + P.clips[c].length > at;
            }
        if (!busy) {
            *fresh = !any;
            return t;
        }
    }
    if (P.ntracks >= MAX_TRACKS) return -1;
    LOCK();
    int t = project_add_track(&P, NULL);
    UNLOCK();
    *fresh = t >= 0;
    return t;
}

/* A file's later tempo and time signature changes, onto bar lines of the project's map. */
void meter_from_file(const MfFile *f)
{
    P.nchanges = 0;
    for (int i = 0; i < f->nmeter && P.nchanges < MAX_METER; i++) {
        const MfMeter *m = &f->meter[i];
        if (m->beat <= 0) continue;
        long fr = meter_beats_frame(&P, m->beat);
        int bar = (int)lround(meter_bars(&P, fr));
        if (bar < 1 || (P.nchanges && bar <= P.changes[P.nchanges - 1].bar)) continue;
        double bpm;
        int num, den;
        meter_at(&P, fr, &bpm, &num, &den, NULL, NULL, NULL);
        P.changes[P.nchanges++] = (MeterChange){bar, m->bpm > 0 ? m->bpm : bpm, m->num > 0 ? m->num : num,
                                                m->den > 0 ? m->den : den};
    }
}

void midi_import(const char *path)
{
    static MfFile f;
    char err[300];
    if (mf_read(path, &f, err, sizeof err) != 0) {
        status("Couldn't import %s: %s", path_base(path), err);
        return;
    }
    if (P.nclips == 0 && f.bpm >= 20 && f.bpm <= 999) {   /* an empty song takes its tempo and time signature */
        P.bpm = round(f.bpm * 100) / 100;
        if (f.has_meter && f.num >= 1 && f.num <= 32 && (f.den == 2 || f.den == 4 || f.den == 8 || f.den == 16)) {
            P.ts_num = f.num;
            P.ts_den = f.den;
        }
        meter_from_file(&f);
    }
    long at = bar_floor(P.playhead);
    double at_b = meter_beats(&P, at);
    int s = P.inst_sample, root = P.inst_root;
    if (s < 0 || s >= P.nsamples || P.samples[s].removed) {   /* no instrument chosen: a plain saw */
        int c4 = 60;
        s = chord_sample(&c4, 1, "C4", 4);
        root = 60;
    } else {
        s = sample_base(&P, s);
    }
    int parts = 0, notes = 0;
    for (int k = 0; k < f.ntracks && s >= 0; k++) {
        const MfTrack *mt = &f.tracks[k];
        if (!mt->n) continue;
        int fresh, t = open_track_from(at, &fresh);
        if (t < 0) {
            status("Imported %d parts: no room for more tracks", parts);
            break;
        }
        int pat = pattern_new_id(&P);
        double end = 0;
        for (int i = 0; i < mt->n; i++) {          /* beats to frames through the tempo map */
            const MfNote *n = &mt->notes[i];
            long st = meter_beats_frame(&P, at_b + n->start) - at, ln = meter_beats_frame(&P, at_b + n->start + n->len) - at - st;
            if (note_add(pat, n->note, st, ln < SR / 100 ? SR / 100 : ln, n->vel / 127.0f) >= 0) notes++;
            if (n->start + n->len > end) end = n->start + n->len;
        }
        long len = bar_ceil(meter_beats_frame(&P, at_b + end)) - at;
        if (len < bar_len_at(at)) len = bar_len_at(at);
        int c = clip_add(s, t, at, 0, len);
        if (c < 0) break;
        LOCK();
        P.clips[c].pattern = pat;
        P.clips[c].root = root;
        if (fresh && mt->name[0]) snprintf(P.tracks[t].name, sizeof P.tracks[t].name, "%.31s", mt->name);
        UNLOCK();
        parts++;
    }
    mf_free(&f);
    if (parts)
        status("Imported %s: %d part%s, %d notes, from bar %d. Double-click a clip to edit its notes", path_base(path), parts,
               parts == 1 ? "" : "s", notes, (int)floor(meter_bars(&P, at) + 1e-9) + 1);
}

void midi_export(const char *path)
{
    static MfFile f;
    memset(&f, 0, sizeof f);
    f.bpm = P.bpm;
    f.num = P.ts_num;
    f.den = P.ts_den;
    f.meter[f.nmeter++] = (MfMeter){0, P.bpm, P.ts_num, P.ts_den};   /* the tempo map, at its beats */
    for (int i = 0; i < P.nchanges && f.nmeter < MF_MAX_METER; i++)
        f.meter[f.nmeter++] = (MfMeter){meter_beats(&P, meter_bar_frame(&P, P.changes[i].bar)), P.changes[i].bpm,
                                         P.changes[i].num, P.changes[i].den};
    int total = 0;
    for (int t = 0; t < P.ntracks && f.ntracks < MF_MAX_TRACKS; t++) {
        MfTrack *mt = NULL;
        for (int c = 0; c < P.nclips; c++) {
            const Clip *cl = &P.clips[c];
            if (cl->track != t || cl->pattern <= 0) continue;
            long origin = cl->start - cl->offset;
            for (int i = 0; i < P.nnotes; i++) {
                const Note *n = &P.notes[i];
                if (n->pattern != cl->pattern) continue;
                long a = origin + n->start, b = a + n->length;
                if (b <= cl->start || a >= cl->start + cl->length) continue;   /* outside the clip */
                if (a < cl->start) a = cl->start;
                if (b > cl->start + cl->length) b = cl->start + cl->length;
                if (!mt) {
                    mt = &f.tracks[f.ntracks++];
                    snprintf(mt->name, sizeof mt->name, "%s", P.tracks[t].name);
                    mt->channel = (t < 9 ? t : t + 1) % 16;   /* channel 10 is drums by convention */
                }
                if (mt->n == mt->cap) {
                    int cap = mt->cap ? mt->cap * 2 : 256;
                    MfNote *nn = realloc(mt->notes, (size_t)cap * sizeof *nn);
                    if (!nn) break;
                    mt->notes = nn;
                    mt->cap = cap;
                }
                double qa = meter_beats(&P, a);
                mt->notes[mt->n++] = (MfNote){n->note, qa, meter_beats(&P, b) - qa, (int)lroundf(n->vel * 127)};
                total++;
            }
        }
    }
    if (!total) {
        mf_free(&f);
        status("Nothing to export: MIDI holds notes, so it needs note clips (audio clips stay out)");
        return;
    }
    char err[300];
    int nt = f.ntracks;
    if (mf_write(path, &f, err, sizeof err) == 0)
        status("Exported %d track%s, %d notes, to %s", nt, nt == 1 ? "" : "s", total, path_base(path));
    else
        status("MIDI export failed: %s", err);
    mf_free(&f);
}

int new_note_clip(int t, long at)        /* an empty one-bar note clip, opened in the roll */
{
    at = bar_floor(at);
    long bar = bar_len_at(at);
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

void roll_drag(void)                     /* the roll's drags, called from update_drag */
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

void roll_drag_end(void)
{
    if (roll_clip < 0) return;
    if (drag.kind == D_NMOVE && abs(mx - drag.sx) + abs(my - drag.sy) < 3 && !(mods & (KMOD_SHIFT | KMOD_CTRL)))
        nsel_only(drag.clip);                   /* a click on one note of a selection picks just that one */
    if (drag.kind == D_NLEN || (drag.kind == D_NMOVE && drag.b)) roll_len = P.notes[drag.clip].length;
    if (drag.kind == D_NBAND) status("%d note%s selected   Delete, arrows, Ctrl+D act on them", nsel_count(),
                                     nsel_count() == 1 ? "" : "s");
    roll_fit_clip();
}

int roll_key(SDL_Keysym k)               /* the roll's own keys; 0 = not one of them */
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

void grid_walk(int xmin, int xmax, double sub, GridFn fn, void *ud)
{
    long f0 = (long)x_to_frame_raw(xmin);
    if (f0 < 0) f0 = 0;
    for (double b = floor(meter_bars(&P, f0) + 1e-9);; b++) {
        long bf = meter_bar_frame(&P, b);
        int x = (int)frame_to_x(bf);
        if (x > xmax) break;
        int num, den;
        long bl;
        meter_at(&P, bf, NULL, &num, &den, NULL, NULL, &bl);
        double bar_px = (double)bl / SR * pps;
        long label_every = 1, line_every = 1;
        while (bar_px * label_every < 34) label_every *= 2;
        while (bar_px * line_every < 9) line_every *= 2;
        int bi = (int)b;
        if (bi % line_every == 0 && x >= xmin) fn(x, bi, 0, bi % label_every == 0, ud);
        if (bar_px / num >= 9)
            for (int k = 1; k < num; k++) {
                int xx = (int)frame_to_x(bf + (long)llround(k * (double)bl / num));
                if (xx >= xmin && xx <= xmax) fn(xx, bi, 1, 0, ud);
            }
        if (sub > 0) {                          /* the snap grid inside the beats */
            double bs = meter_beats(&P, bf), be = meter_beats(&P, bf + bl), unit = (double)num ? (be - bs) / num : 1;
            if ((double)sub * bar_px / (be - bs) >= 8 && sub < unit - 1e-9)
                for (double q = bs + sub; q < be - 1e-9; q += sub) {
                    double rel = (q - bs) / unit;
                    if (fabs(rel - round(rel)) < 1e-6) continue;   /* a beat line already */
                    int xx = (int)frame_to_x(meter_beats_frame(&P, q));
                    if (xx >= xmin && xx <= xmax) fn(xx, bi, 2, 0, ud);
                }
        }
    }
}

void tl_grid_fn(int x, int bar, int kind, int labelled, void *ud)
{
    const SDL_Rect *area = &((TlGrid *)ud)->area;
    if (kind == 2) return;
    line(x, area->y, x, area->y + area->h, kind == 0 ? LINE : tone(LINE, 0.62f));
    line(x, L.top + 24 - (kind == 0 ? 8 : 4), x, L.top + 24, DIM);
    if (kind == 0 && labelled) {
        char buf[24];
        snprintf(buf, sizeof buf, "%d", bar + 1);
        text(x + 4, L.top + 3, buf, DIM);
    }
}

void roll_grid_fn(int x, int bar, int kind, int labelled, void *ud)
{
    (void)ud;
    if (kind == 2) {
        line(x, roll_grid.y, x, roll_grid.y + roll_grid.h, alpha(LINE, 70));
        return;
    }
    line(x, roll_grid.y, x, roll_vel.y + roll_vel.h, kind == 0 ? tone(LINE, 1.3f) : tone(LINE, 0.75f));
    if (kind == 0 && labelled) {
        char buf[24];
        snprintf(buf, sizeof buf, "%d", bar + 1);
        text(x + 4, roll_ruler.y + 3, buf, DIM);
        line(x, roll_ruler.y + 12, x, roll_ruler.y + roll_ruler.h, DIM);
    }
}

void piano_roll(void)
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
    int sw = W - x - 760 > 320 ? 320 : W - x - 760 > 160 ? W - x - 760 : 160;
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
    double b0 = meter_bars(&P, c->start);         /* bars by the tempo map, from where the clip starts */
    if (!(drag.kind == D_NUM && drag.num == &bars) && !(numedit.active && numedit.v == &bars))
        bars = meter_bars(&P, c->start + c->length) - b0;
    double bars0 = bars;
    number((SDL_Rect){x, by, 104, bh}, "Bars", &bars, 0.25, 64, 0.02, "%.2f");
    if (bars != bars0) {
        LOCK();
        c->length = meter_bar_frame(&P, b0 + bars) - c->start;
        if (c->length < 1) c->length = 1;
        UNLOCK();
    }
    x += 112;
    {                                           /* tidy the timing, or loosen it */
        int sel = nsel_count();
        if (button((SDL_Rect){x, by, 92, bh}, "Quantize", 0)) {
            int q = notes_quantize(roll_pattern, roll_origin(), sel ? nsel : NULL, quant_grid(), 1.0);
            status("Quantized %d note%s to %s (Snap sets the grid)", q, q == 1 ? "" : "s",
                   snap_frames() ? SNAP_NAMES[snap_i] : "1/16");
        }
        if (button((SDL_Rect){x + 98, by, 96, bh}, "Humanize", 0)) {
            int q = notes_humanize(roll_pattern, sel ? nsel : NULL, 12);
            roll_fit_clip();
            status("Humanized %d note%s: a little looser in time and touch (again for more)", q, q == 1 ? "" : "s");
        }
        x += 202;
    }
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
    grid_walk(L.tl_x, W, snap_beats() > 0 ? snap_beats() : 0, roll_grid_fn, NULL);   /* bars, beats, the snap grid */
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

int session_view;

void track_headers(void)
{
    if (session_view) session_header();
    else {                                      /* the arranger strip's own header */
        SDL_Rect hr = {L.left, L.top + L.ruler, L.head, L.arr};
        fill(hr, tone(PANEL, 0.92f));
        line(L.left, hr.y + hr.h - 1, L.left + L.head, hr.y + hr.h - 1, LINE);
        text(L.left + 12, hr.y + (hr.h - font_h) / 2, "SECTIONS", DIM);
        SDL_Rect sa = {L.left + L.head - 32, hr.y + 2, 22, hr.h - 4};
        tip(sa, "A new section at the playhead's bar (Intro, Verse, Chorus...). Or drag along the strip");
        if (button(sa, "+", 0)) sec_add_here();
    }
    if (!session_view) chord_header();
    fill((SDL_Rect){L.left, L.top, L.head, L.ruler}, PANEL);
    {                                           /* the timeline, or the session view (where "TRACKS" was) */
        SDL_Rect vb = {L.left + 8, L.top + 8, L.head - 32 - 8 - 6, L.ruler - 16};
        int live = 0;                           /* tracks the session has (playing a slot, or stopped) */
        for (int t = 0; t < P.ntracks; t++) live += P.sess_play[t] != SESS_TIMELINE || P.sess_q[t] != SESS_NONE;
        int busy = live > 0 || P.sess_mode != SESS_BOTH;
        char vl[40];
        if (session_view) snprintf(vl, sizeof vl, "Timeline view");
        else if (live && P.sess_mode != SESS_ONLY_TIMELINE) snprintf(vl, sizeof vl, "Session (%d)", live);
        else snprintf(vl, sizeof vl, P.sess_mode == SESS_ONLY_TIMELINE && live ? "Session (off)" : "Session view");
        tip(vb, session_view ? "Back to the timeline (Tab). + beside it adds a track"
            : busy ? "The session has tracks: the number playing or stopped there, or (off) when Plays is timeline only. "
                     "Their lanes say so. Tab to see the session and choose what plays"
                   : "The session view: scenes of slots to launch live (Tab). + beside it adds a track");
        if (button(vb, vl, session_view || busy)) session_view = !session_view;
    }
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
        text_clip((SDL_Rect){L.left + 8, y, L.lane_h >= 63 ? L.head - 102 : L.head - 128, L.lane_h}, L.left + 12,
                  by + (bh - font_h) / 2, tr->name, TEXT);
        if (track_has_fx(tr) && L.lane_h >= 56) text(L.left + 12, by + bh + 3, "fx", ACCENT);
        SDL_Rect name_r = {L.left + 4, y, L.head - 92, L.lane_h - 16};
        if (drag.kind == D_NONE && hit(name_r) && lpress) {      /* pick it for the Mixer pane */
            sel_track = t;
            if (mixer_open) mixer_sub = 2;
            lpress = 0;
        }
        SDL_Rect m = {L.left + L.head - 58, by, 22, bh}, s = {L.left + L.head - 32, by, 22, bh};
        {                                       /* record-arm: the microphone records onto this track */
            int roomy = L.lane_h >= 63;
            SDL_Rect rb = roomy ? (SDL_Rect){L.left + L.head - 32, by + bh + 4, 22, 18}
                                : (SDL_Rect){L.left + L.head - 110, by, 22, bh};
            tip(rb, track_arm[t] ? "Armed: Rec records the microphone onto %s (click to disarm)"
                                 : "Arm %s to record the microphone onto it, then press Rec", tr->name);
            int hov = hit(rb) && drag.kind == D_NONE;
            style_box(rb, track_arm[t] ? 2 : hov ? 1 : 0);
            if (track_arm[t]) {                 /* red, with the microphone's level rising inside */
                fill(rb, rgb(190, 45, 40));
                float db = in_meter > 1e-5f ? 20 * log10f(in_meter) : -60, k = (db + 60) / 60;
                k = k < 0 ? 0 : k > 1 ? 1 : k;
                int lh = (int)((rb.h - 4) * k);
                fill((SDL_Rect){rb.x + 2, rb.y + rb.h - 2 - lh, rb.w - 4, lh}, in_meter > 0.95f ? rgb(255, 220, 80) : rgb(255, 150, 140));
            }
            disc_small(rb.x + rb.w / 2, rb.y + rb.h / 2, 4, track_arm[t] ? rgb(255, 255, 255) : rgb(210, 60, 55));   /* a record dot */
            if (hov && lpress) {
                lpress = 0;
                int was = track_arm[t];
                memset(track_arm, 0, sizeof track_arm);   /* one track at a time: one microphone */
                track_arm[t] = !was;
                status(track_arm[t] ? "%s is armed: press Rec to record the microphone onto it (Settings > Audio picks the input)"
                                    : "%s disarmed", tr->name);
            }
        }
        tip(m, "Mute %s", tr->name);
        tip(s, "Solo %s: hear only the soloed tracks", tr->name);
        if (button(m, "M", tr->mute)) { LOCK(); tr->mute = !tr->mute; UNLOCK(); }
        if (button(s, "S", tr->solo)) { LOCK(); tr->solo = !tr->solo; UNLOCK(); }
        SDL_Rect ab = {L.left + L.head - 84, by, 22, bh};
        int automated = 0;
        for (int p = 0; p < AUTO_PARAMS; p++) automated |= auto_lane(&P, t, p, NULL) > 0;
        tip(ab, "Automation: draw %s's volume, pan, filter or sends changing over time", tr->name);
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
        {
            char db[24];
            db_text(tr->volume, db, sizeof db);
            tip((SDL_Rect){vb.x, vb.y - 4, vb.w, vb.h + 8}, "%s volume %s: drag it, or use the Mixer's Levels for faders in dB", tr->name, db);
        }
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
void fmt_clock(double secs, int fine, char *buf, size_t n)
{
    if (secs < 0) secs = 0;
    int m = (int)(secs / 60);
    double r = secs - m * 60;
    if (fine) snprintf(buf, n, "%d:%05.2f", m, r);
    else if (r - floor(r) > 1e-6) snprintf(buf, n, "%d:%04.1f", m, r);
    else snprintf(buf, n, "%d:%02d", m, (int)r);
}

/* The minutes:seconds scale: labelled ticks at a step that keeps labels apart, small ones between */
void time_scale(SDL_Rect r)
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

/* ---- the arranger strip: the song's sections (intro, verse, chorus...) above the tracks.
 * Click one to go there; drag it to move it (and everything in it); drag its right end to change
 * its length; drag along the empty strip to make one; double-click to rename; right-click for more. */

SDL_Rect sec_rect_[MAX_SECTIONS];

Col sec_col(const char *name)            /* the same name, the same colour: every Chorus matches */
{
    unsigned h = 5381;
    for (const char *q = name; *q; q++)
        if (isalpha((unsigned char)*q)) h = h * 33 + (unsigned)tolower((unsigned char)*q);
    return TRACK_COL[h % 12];
}

int sec_at_bar(int bar)
{
    for (int i = 0; i < P.nsections; i++)
        if (bar >= P.sections[i].bar && bar < P.sections[i].bar + P.sections[i].bars) return i;
    return -1;
}

int sec_room(int bar)                    /* bars free from `bar` until the next section */
{
    int room = 100000;
    for (int i = 0; i < P.nsections; i++)
        if (P.sections[i].bar > bar && P.sections[i].bar - bar < room) room = P.sections[i].bar - bar;
    return room;
}

void sec_sort(void)
{
    for (int i = 1; i < P.nsections; i++)
        for (int j = i; j > 0 && P.sections[j - 1].bar > P.sections[j].bar; j--) {
            Section t = P.sections[j];
            P.sections[j] = P.sections[j - 1];
            P.sections[j - 1] = t;
        }
}

int sec_add(int bar, int bars, const char *name)
{
    if (bar < 0 || sec_at_bar(bar) >= 0) return -1;
    if (P.nsections >= MAX_SECTIONS) {
        status("A song can have %d sections", MAX_SECTIONS);
        return -1;
    }
    int room = sec_room(bar);
    if (bars > room) bars = room;
    if (bars < 1) return -1;
    if (!name) {                                /* the usual order of a song */
        static const char *const ORDER[] = {"Intro", "Verse", "Chorus", "Verse", "Chorus", "Bridge", "Chorus", "Outro"};
        int before = 0;
        for (int i = 0; i < P.nsections; i++) before += P.sections[i].bar < bar;
        name = ORDER[before < 8 ? before : 7];
    }
    Section sc = {bar, bars, ""};
    snprintf(sc.name, sizeof sc.name, "%s", name);
    P.sections[P.nsections++] = sc;
    sec_sort();
    for (int i = 0; i < P.nsections; i++)
        if (P.sections[i].bar == bar) return i;
    return -1;
}

void sec_add_here(void)
{
    int bar = (int)floor(meter_bars(&P, P.playhead) + 1e-9);
    if (sec_at_bar(bar) >= 0) {                 /* inside one: the bar after the last */
        int last = 0;
        for (int i = 0; i < P.nsections; i++)
            if (P.sections[i].bar + P.sections[i].bars > last) last = P.sections[i].bar + P.sections[i].bars;
        bar = last;
    }
    int i = sec_add(bar, 4, NULL);
    if (i >= 0) status("\"%s\" from bar %d, %d bars. Double-click to rename it, drag it to move it", P.sections[i].name,
                       P.sections[i].bar + 1, P.sections[i].bars);
}

void sec_jump(int dir)                   /* to the next (1) or previous (-1) section's start */
{
    if (!P.nsections) {
        status("No sections yet: + beside SECTIONS makes one");
        return;
    }
    long now = P.playhead;
    int pick = -1;
    for (int i = 0; i < P.nsections; i++) {
        long f = meter_bar_frame(&P, P.sections[i].bar);
        if (dir > 0 && f > now + 16 && pick < 0) pick = i;
        if (dir < 0 && f < now - SR / 4) pick = i;   /* a moment in: back to this one's start first */
    }
    if (pick < 0) pick = dir > 0 ? P.nsections - 1 : 0;
    playhead_to(meter_bar_frame(&P, P.sections[pick].bar));
    status("%s (bar %d)", P.sections[pick].name, P.sections[pick].bar + 1);
}

int sec_mouse_bar(int round_it)
{
    double b = meter_bars(&P, (long)(x_to_frame_raw(mx) > 0 ? x_to_frame_raw(mx) : 0));
    int bar = round_it ? (int)floor(b + 0.5) : (int)floor(b);
    return bar < 0 ? 0 : bar;
}

/* the boundary a dragged section would go in front of: a section's start or the end of the last */
int sec_target(int moving)
{
    const Section *m = &P.sections[moving];
    int best = -1;
    double bestd = 1e18;
    double x = mx - (drag.sx - frame_to_x(meter_bar_frame(&P, m->bar)));   /* where its start would be */
    for (int i = 0; i <= P.nsections; i++) {
        int b = i < P.nsections ? P.sections[i].bar
                                : (P.nsections ? P.sections[P.nsections - 1].bar + P.sections[P.nsections - 1].bars : 0);
        if (b == m->bar || b == m->bar + m->bars) continue;   /* where it is already */
        double d = fabs(frame_to_x(meter_bar_frame(&P, b)) - x);
        if (d < bestd) {
            bestd = d;
            best = b;
        }
    }
    return best;
}

void arranger_strip(void)
{
    SDL_Rect strip = {L.tl_x, L.top + L.ruler, L.tl_w, L.arr};
    fill(strip, tone(PANEL, 0.92f));
    line(L.tl_x, strip.y + strip.h - 1, W, strip.y + strip.h - 1, LINE);
    int over = -1, edge = -1;
    long now = P.playhead;
    for (int i = 0; i < P.nsections; i++) {
        const Section *sc = &P.sections[i];
        int x0 = (int)frame_to_x(meter_bar_frame(&P, sc->bar)), x1 = (int)frame_to_x(meter_bar_frame(&P, sc->bar + sc->bars));
        SDL_Rect r = {x0, strip.y + 2, x1 - x0 - 1, strip.h - 4};
        sec_rect_[i] = r;
        if (r.x + r.w < L.tl_x || r.x > W) continue;
        Col c = sec_col(sc->name);
        int here = now >= meter_bar_frame(&P, sc->bar) && now < meter_bar_frame(&P, sc->bar + sc->bars);
        int dragged = drag.kind == D_SEC && drag.clip == i && abs(mx - drag.sx) > 4;
        fill(r, dragged ? alpha(c, 90) : wash(c, here ? 0.85f : 0.6f));
        if (here) outline(r, HEAD);
        if (sec_renaming == i && tf.buf == P.sections[i].name) {
            SDL_Rect tr = {r.x, r.y - 2, r.w < 150 ? 150 : r.w, r.h + 4};
            textfield(tr, P.sections[i].name, sizeof P.sections[i].name, 0, "name");
        } else {
            SDL_Rect clipr = {r.x > L.tl_x ? r.x : L.tl_x, r.y, r.x + r.w - (r.x > L.tl_x ? r.x : L.tl_x) - 2, r.h};
            if (clipr.w > 4) text_clip(clipr, (r.x > L.tl_x ? r.x : L.tl_x) + 5, r.y + (r.h - font_h) / 2, sc->name, DARK);
        }
        if (hit(r) && drag.kind == D_NONE) {
            over = i;
            if (mx >= r.x + r.w - 5) edge = i;
            tip(r, "%s: bars %d-%d. Click to go there, drag to move it with everything in it, drag its right end to "
                   "resize, double-click to rename, right-click for more", sc->name, sc->bar + 1, sc->bar + sc->bars);
        }
    }
    if (sec_renaming >= 0 && (sec_renaming >= P.nsections || tf.buf != P.sections[sec_renaming].name)) {
        if (sec_renaming < P.nsections && !P.sections[sec_renaming].name[0])
            snprintf(P.sections[sec_renaming].name, sizeof P.sections[sec_renaming].name, "Section");
        sec_renaming = -1;
    }
    if (!P.nsections && drag.kind != D_SECNEW)
        text_clip(strip, L.tl_x + 8, strip.y + (strip.h - font_h) / 2,
                  "Drag here to mark a section (intro, verse, chorus), or press + beside SECTIONS", tone(DIM, 0.8f));
    if (hit(strip) && drag.kind == D_NONE && over < 0)
        tip(strip, "Drag along here to mark a section of the song");
    if (hit(strip) && drag.kind == D_NONE) SDL_SetCursor(edge >= 0 ? cur_resize : cur_arrow);

    /* the mouse */
    if (hit(strip) && drag.kind == D_NONE && lpress) {
        lpress = 0;
        if (over >= 0 && ldouble) {              /* rename */
            tf_end(1);
            tf.buf = P.sections[over].name;
            tf.caret = (size_t)-1;
            tf.cap = sizeof P.sections[over].name;
            tf.path = 0;
            tf.r = sec_rect_[over];
            tf_drawn = 1;
            snprintf(tf.orig, sizeof tf.orig, "%s", tf.buf);
            SDL_StartTextInput();
            sec_renaming = over;
        } else if (edge >= 0) {
            drag = (typeof(drag)){.kind = D_SECEND, .clip = edge, .sx = mx, .sy = my};
        } else if (over >= 0) {
            drag = (typeof(drag)){.kind = D_SEC, .clip = over, .sx = mx, .sy = my, .b = -1};
        } else {
            drag = (typeof(drag)){.kind = D_SECNEW, .a = sec_mouse_bar(0), .sx = mx, .sy = my};
        }
    }
    if (hit(strip) && drag.kind == D_NONE && rpress && over >= 0) {
        rpress = 0;
        cm_open_section(over);
    }
    /* drags in progress */
    if (drag.kind == D_SECNEW) {
        int a = (int)drag.a, b = sec_mouse_bar(1);
        if (b <= a) b = a + 1;
        int room = sec_room(a);
        if (b - a > room) b = a + room;
        int x0 = (int)frame_to_x(meter_bar_frame(&P, a)), x1 = (int)frame_to_x(meter_bar_frame(&P, b));
        SDL_Rect g = {x0, strip.y + 2, x1 - x0, strip.h - 4};
        fill(g, alpha(ACCENT, 110));
        outline(g, ACCENT);
    } else if (drag.kind == D_SECEND && drag.clip < P.nsections) {
        Section *sc = &P.sections[drag.clip];
        int b = sec_mouse_bar(1), room = sec_room(sc->bar);
        int nb = b - sc->bar < 1 ? 1 : b - sc->bar > room ? room : b - sc->bar;
        sc->bars = nb;
    } else if (drag.kind == D_SEC && drag.clip < P.nsections && abs(mx - drag.sx) > 4) {
        const Section *sc = &P.sections[drag.clip];
        int t = sec_target(drag.clip);
        drag.b = t;
        int w = sec_rect_[drag.clip].w;
        SDL_Rect g = {mx - (drag.sx - sec_rect_[drag.clip].x), strip.y + 2, w, strip.h - 4};
        fill(g, alpha(sec_col(sc->name), 200));
        outline(g, TEXT);
        text_clip(g, g.x + 5, g.y + (g.h - font_h) / 2, sc->name, DARK);
        if (t >= 0) {
            int tx = (int)frame_to_x(meter_bar_frame(&P, t));
            fill((SDL_Rect){tx - 1, strip.y, 3, L.arr + L.chd + lanes_h()}, HEAD);
        }
        SDL_SetCursor(cur_hand);
    }
}

void arranger_release(void)
{
    if (drag.kind == D_SECNEW) {
        int a = (int)drag.a, b = sec_mouse_bar(1), click = abs(mx - drag.sx) < 4;
        int i = sec_add(a, click ? 4 : (b > a ? b - a : 1), NULL);
        if (i >= 0) status("\"%s\" from bar %d, %d bar%s. Double-click to rename it", P.sections[i].name, P.sections[i].bar + 1,
                           P.sections[i].bars, P.sections[i].bars == 1 ? "" : "s");
    } else if (drag.kind == D_SECEND && drag.clip < P.nsections) {
        status("%s: %d bars", P.sections[drag.clip].name, P.sections[drag.clip].bars);
    } else if (drag.kind == D_SEC && drag.clip < P.nsections) {
        Section sc = P.sections[drag.clip];
        if (abs(mx - drag.sx) < 4) {             /* a click: go there */
            playhead_to(meter_bar_frame(&P, sc.bar));
            status("%s (bar %d). Ctrl+PageUp and Ctrl+PageDown go to the previous and next section", sc.name, sc.bar + 1);
        } else if (drag.b >= 0) {
            song_edit((SongEdit){SE_MOVE, sc.bar, sc.bar + sc.bars, (int)drag.b});
            int at = (int)drag.b <= sc.bar ? (int)drag.b : (int)drag.b - sc.bars;
            status("Moved %s, with everything in it, to bar %d. Ctrl+Z puts it back", sc.name, at + 1);
        }
    }
}
