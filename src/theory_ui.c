/* tinydaw: the Theory pane and the genre beats. */
#include "ui.h"

void play_scratch(float *buf, long frames)
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
GenParams chord_params(double beats)
{
    GenParams g = G;
    g.a4 = P.a4;
    if (g.wave >= WAVE_NOISE) g.wave = WAVE_TRIANGLE;
    g.beats = beats;
    if (g.attack_ms < 5) g.attack_ms = 5;
    if (g.release_ms < 120) g.release_ms = 120;
    return g;
}

void preview_notes(const int *notes, int n, double beats)
{
    GenParams g = chord_params(beats);
    long len;
    float *b = render_notes(&g, P.bpm, notes, n, &len);
    if (b) play_scratch(b, len);
}

/* A library sound for these notes with the current settings; reuses an identical one. */
int chord_sample(const int *notes, int n, const char *label, double beats)
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
const double TH_LENS[4] = {1, 2, 4, 8};

const char *const TH_LEN_NAMES[4] = {"1 beat", "2 beats", "1 bar", "2 bars"};

void library_added(int s, int existed)
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
int add_to_library(const int *notes, int n, const char *label)
{
    int before = P.nsamples;
    int s = chord_sample(notes, n, label, TH_LENS[th_len]);
    library_added(s, s >= 0 && s < before);
    return s;
}

/* ---- chords from the Theory pane into the open piano roll */

/* where the next chord goes, in the pattern's own frames: the playhead when it's inside the
 * clip, else straight after the last note (on the next beat) */
long roll_insert_at(void)
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
int roll_put_chords(const int (*notes)[6], const int *counts, int nch, double beats, const char *what)
{
    if (roll_clip < 0) return 0;
    long at = roll_insert_at(), added = 0;
    long len = (long)llround(beats * beat_at(P.clips[roll_clip].start + at - P.clips[roll_clip].offset));
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
    status("Put %s in the roll at bar %d (%ld notes): arrows move them, Ctrl+Z takes them out", what,
           (int)floor(meter_bars(&P, c->start + at - c->offset) + 1e-9) + 1, added);
    return (int)added;
}

int roll_put_chord(const int *notes, int n, double beats, const char *name)
{
    int one[1][6], cnt[1] = {n};
    memcpy(one[0], notes, sizeof(int) * (size_t)(n < 6 ? n : 6));
    return roll_put_chords((const int (*)[6])one, cnt, 1, beats, name);
}

/* a small "Roll" button under a chord, shown while the piano roll is open */
int roll_button(SDL_Rect r)
{
    if (roll_clip < 0) return 0;
    return button(r, "Roll", 0);
}

/* a small "+" button: adds something to the library */
int plus_button(SDL_Rect r)
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
int adds_as(int x, int y, int w)
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

int prog_flats(const Progression *pr)
{
    return key_uses_flats(th_root, pr->type[0] == CH_MIN ? 1 : 0);
}

void play_progression(const Progression *pr)
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

void place_progression(const Progression *pr)
{
    double b0 = round(meter_bars(&P, P.playhead));   /* one chord a bar, from the bar nearest the playhead */
    int flats = prog_flats(pr);
    for (int i = 0; i < pr->n; i++) {
        double qa = meter_beats(&P, meter_bar_frame(&P, b0 + i)), qb = meter_beats(&P, meter_bar_frame(&P, b0 + i + 1));
        chord_put(&P, qa, qb - qa, (th_root + pr->root[i]) % 12, pr->type[i], -1);
    }
    status("Put %s in %s on the chord track: %d bars from bar %.0f. Create > Parts writes parts from it", pr->numerals,
           pitch_name(th_root, flats), pr->n, b0 + 1);
}

int chord_button(SDL_Rect r, const char *top, const char *bottom)
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
int row_hit(SDL_Rect r)
{
    int hov = hit(r) && drag.kind == D_NONE;
    if (hov) fill(r, PANEL3);
    if (hov && lpress) {
        lpress = 0;
        return 1;
    }
    return 0;
}

void key_signature_text(int parent, int flats, char *buf, size_t n)
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

int tab_keys(int x, int y, int w)
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
        text(x, y, "+ adds the chords to the library. Place puts one per bar", DIM);
        text(x, y + font_h + 2, "on the chord track, from the nearest bar.", DIM);
    }
    return y + 2 * font_h + 8;
}

void disc(int cx, int cy, int r, Col c) { disc_small(cx, cy, r, c); }

void ring(int cx, int cy, int r, Col c)   /* a circle one unit thick, as triangles: smooth at any size */
{
    enum { N = 64 };
    SDL_Vertex v[2 * N];
    int idx[6 * N], ni = 0;
    float ro = r + 1.0f, ri = r > 0 ? (float)r : 0;
    for (int i = 0; i < N; i++) {
        float a = (float)i / N * 6.2831853f, ca = cosf(a), sa = sinf(a);
        v[2 * i].position = (SDL_FPoint){cx + 0.5f + ro * ca, cy + 0.5f + ro * sa};
        v[2 * i + 1].position = (SDL_FPoint){cx + 0.5f + ri * ca, cy + 0.5f + ri * sa};
        v[2 * i].color = v[2 * i + 1].color = (SDL_Color){c.r, c.g, c.b, c.a};
        v[2 * i].tex_coord = v[2 * i + 1].tex_coord = (SDL_FPoint){0, 0};
        int j = (i + 1) % N;
        int q[6] = {2 * i, 2 * i + 1, 2 * j, 2 * j, 2 * i + 1, 2 * j + 1};
        for (int k = 0; k < 6; k++) idx[ni++] = q[k];
    }
    SDL_RenderGeometry(ren, NULL, v, 2 * N, idx, ni);
}

int tab_circle(int x, int y, int w)
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

int tab_chords(int x, int y, int w)
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

int tab_intervals(int x, int y, int w)
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

int tab_tempo(int x, int y, int w)
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

int tab_notes(int x, int y, int w)
{
    char buf[160];
    snprintf(buf, sizeof buf, "C4 = middle C = MIDI 60.  A4 = %g Hz, equal temperament.", P.a4);
    text(x, y, buf, DIM);
    y += font_h + 4;
    text(x, y, "Click a note to hear it and choose it (also for Tones).", DIM);
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

int gn_bars = 4, gn_init;  /* (declared earlier for the self-test) */

void genre_init(void)
{
    if (gn_init) return;
    gn_init = 1;
    for (int g = 0; g < NGENRES && g < 32; g++)
        for (int l = 0; l < DRUM_LANES; l++) snprintf(gn_pat[g][l], 33, "%s", GENRES[g].lanes[l]);
}

double kick_tuned_hz(void)               /* the key's root, moved into 40-80 Hz */
{
    double f = P.a4 * pow(2.0, (th_root - 9) / 12.0);
    while (f >= 80) f /= 2;
    while (f < 40) f *= 2;
    return f;
}

double genre_bpm(int g) { return P.nclips == 0 ? GENRES[g].bpm : P.bpm; }

BeatSpec genre_spec(int g)
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

int lane_used(const BeatSpec *b, int l)
{
    for (int st = 0; st < b->steps && b->lanes[l][st]; st++)
        if (strchr("xXor", b->lanes[l][st])) return 1;
    return 0;
}

/* Splits the beat into the parts that get their own rows: one, or one per used drum. */
int beat_parts(const BeatSpec *b, int *masks, const char **labels)
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

int beat_layout(int g, long *pattern_frames, int *reps)
{
    BeatSpec b = genre_spec(g);
    int masks[DRUM_LANES];
    const char *labels[DRUM_LANES];
    *pattern_frames = (long)llround(b.steps * 60.0 / genre_bpm(g) / 4 * SR);
    *reps = gn_bars * 16 / b.steps > 0 ? gn_bars * 16 / b.steps : 1;
    return beat_parts(&b, masks, labels);
}

int same_beat(const BeatSpec *a, const BeatSpec *b)
{
    return !memcmp(a->lanes, b->lanes, sizeof a->lanes) && a->steps == b->steps && a->swing == b->swing &&
           a->swing8 == b->swing8 && a->kick_hz == b->kick_hz && a->kick_decay == b->kick_decay &&
           a->snare_hz == b->snare_hz && a->hat_decay == b->hat_decay && a->open_decay == b->open_decay &&
           a->volume == b->volume;
}

int beat_sample(const BeatSpec *b, int mask, const char *name)
{
    for (int i = 0; i < P.nsamples; i++) {
        const Sample *s = &P.samples[i];
        if (s->is_beat == 1 && !s->removed && s->beat_mask == mask && s->gen_bpm == P.bpm && same_beat(&s->beat, b)) return i;
    }
    return sample_generate_beat(&P, b, mask, name);
}

/* ---- pad presets */

int hit_sample(const BeatSpec *b, int lane, const char *name)    /* one drum of a kit, reused */
{
    for (int i = 0; i < P.nsamples; i++) {
        const Sample *s = &P.samples[i];
        if (s->is_beat == 2 && !s->removed && s->beat_mask == lane && same_beat(&s->beat, b)) return i;
    }
    int idx = sample_generate_hit(&P, b, lane, name);
    if (idx >= 0) sample_make_layers(&P, idx, 3, 2);   /* soft to hard, and no two hits in a row the same */
    return idx;
}
