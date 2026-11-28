/* tinydaw: the rail and panels, the mixer pane, automation lanes and note clips. */
#include "ui.h"

int *const RAIL_FLAG[RI_COUNT] = {&sounds_open, &theory_open, &pads_open, &drums_open, &mixer_open, &vis_open, &settings_open};
const char *const RAIL_NAME[RI_COUNT] = {"Create", "Theory", "Pads", "Drums", "Mixer", "Visual", "Settings"};
int last_panel = RI_SOUNDS;

void open_panel(int which)        /* toggles it; the others close */
{
    int was = *RAIL_FLAG[which];
    for (int i = 0; i < RI_COUNT; i++) *RAIL_FLAG[i] = 0;
    *RAIL_FLAG[which] = !was;
    if (!was) last_panel = which;
}

void show_panel(int which)        /* opens it (never closes it); the others close */
{
    if (!*RAIL_FLAG[which]) open_panel(which);
}

void rail_icon(int kind, int cx, int cy, Col c)
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
    case RI_DRUMS:                              /* a drum and two sticks */
        fill((SDL_Rect){cx - 9, cy - 2, 18, 9}, c);
        ring(cx, cy - 3, 0, c);
        for (int d = -9; d <= 9; d++) {          /* its top, an ellipse */
            int hh = (int)lround(3 * sqrt(1 - d * d / 81.0));
            line(cx + d, cy - 2 - hh, cx + d, cy - 2 + hh, c);
        }
        line(cx - 7, cy - 11, cx - 1, cy - 4, c);
        line(cx - 6, cy - 11, cx, cy - 4, c);
        line(cx + 7, cy - 11, cx + 1, cy - 4, c);
        line(cx + 6, cy - 11, cx, cy - 4, c);
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

void rail(void)
{
    SDL_Rect r = {0, L.top, RAIL_W, H - L.top - L.status};
    if (ST.shaped) shape_fill(r, ST.rail_top, ST.rail_bot, 0, theme_kind == THEME_AERO);
    else fill(r, tone(PANEL, 0.8f));
    line(RAIL_W - 1, r.y, RAIL_W - 1, r.y + r.h, LINE);
    for (int i = 0; i < RI_COUNT; i++) {
        int y = i == RI_SETTINGS ? r.y + r.h - 62 : r.y + 6 + i * 62;
        SDL_Rect b = {4, y, RAIL_W - 8, 56};
        static const char *const RTIPS[RI_COUNT] = {
            ("Create (Ctrl+1): your library, and everything that makes something new: synth sounds and tones, "
             "sung lines, drum beats, and parts written from the chord track"),
            "Theory (Ctrl+2): the key and scales, chords and progressions, and reference",
            "Pads (Ctrl+3): play sounds live from the keyboard, a controller or MIDI, and record",
            "Drums (Ctrl+4): a track's step patterns: 16 or 32 steps, velocity and chance per step",
            "Mixer (Ctrl+5): levels, effects, the master, and the mixing console",
            "Visual (Ctrl+6): spectrum, waveform, meters and the full-screen show",
            "Settings (Ctrl+7): look and size, audio, inputs, keys"};
        tip(b, "%s", RTIPS[i]);
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
SDL_Rect panel_rect(void) { return (SDL_Rect){L.pane_x, L.top, L.pane_w, H - L.top - L.status}; }

int panel_begin(SDL_Rect pane, int *scroll, int content_h, int *saved_press)
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

void panel_end(SDL_Rect pane, int *scroll, int content_h, int inside, int saved_press)
{
    scrollbar(pane, scroll, content_h);
    SDL_RenderSetClipRect(ren, NULL);
    if (!inside) lpress = saved_press;
}

/* ---- Sounds: the library, and making new sounds */

int sounds_scroll, sounds_content_h;

/* Takes a sound out of the library, with every clip that plays it (or a pitched or reversed copy
 * of it), off its pads and the instrument. One undo step puts it all back. */
void remove_sound(int i)
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

T_sclip sclip;

T_cm cm;
int lib_flash = -1;              /* a sound just pasted into the library: lit for a moment */
Uint32 lib_flash_at;

void sound_copy(int kind, int idx)
{
    int s = kind == CM_LIB ? idx : kind == CM_PAD ? P.pads[idx].sample : P.inst_sample;
    if (s < 0 || s >= P.nsamples) return;
    sclip.has = 1;
    sclip.sample = s;
    sclip.from_pad = kind == CM_PAD;
    last_copy = COPY_SOUND;
    if (kind == CM_PAD) sclip.pad = P.pads[idx];
    status("Copied \"%s\": right-click a pad, the instrument or the library and Paste", P.samples[s].name);
}

/* the sound the clipboard would put in the library: with a pad's pitch, a pitched copy */
int sclip_library_sound(void)
{
    if (!sclip.has || sclip.sample < 0 || sclip.sample >= P.nsamples) return -1;
    if (sclip.from_pad && sclip.pad.pitch) return sample_pitched(&P, sclip.sample, sclip.pad.pitch);
    return sclip.sample;
}

void sound_paste(int kind, int idx)
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

/* ---- saved and pinned sounds
 *
 * Save puts a copy of a sound in Music/tinydaw/Saved sounds; everything in that folder is in the
 * library of every project. Pin keeps a sound at the top of the list, and saves it. The pinned ones
 * are listed in pinned.txt in the same folder. */

int saved_enabled;               /* off for screenshots and command-line exports */

char pins[256][200];             /* file names of pinned sounds */
int npins, pins_read;

void saved_dir(char *buf, size_t n)
{
    const char *e = SDL_getenv("TINYDAW_SAVED_DIR");
    if (e && *e) {
        snprintf(buf, n, "%s", e);
        return;
    }
    char m[900];
    user_dir("music", m, sizeof m);
    snprintf(buf, n, "%s/tinydaw/Saved sounds", m);
}

int in_saved_dir(const char *path)
{
    char d[1024];
    saved_dir(d, sizeof d);
    size_t n = strlen(d);
    return path[0] && !strncmp(path, d, n) && (path[n] == '/' || path[n] == '\\');
}

void pins_load(void)
{
    char d[1024], f[1100], line_[256];
    saved_dir(d, sizeof d);
    snprintf(f, sizeof f, "%s/pinned.txt", d);
    npins = 0;
    pins_read = 1;
    FILE *fp = fopen(f, "r");
    if (!fp) return;
    while (npins < 256 && fgets(line_, sizeof line_, fp)) {
        line_[strcspn(line_, "\r\n")] = 0;
        if (line_[0]) snprintf(pins[npins++], sizeof pins[0], "%.199s", line_);
    }
    fclose(fp);
}

void pins_save(void)
{
    char d[1024], f[1100];
    saved_dir(d, sizeof d);
    ensure_dir(d);
    snprintf(f, sizeof f, "%s/pinned.txt", d);
    FILE *fp = fopen(f, "w");
    if (!fp) return;
    for (int i = 0; i < npins; i++) fprintf(fp, "%s\n", pins[i]);
    fclose(fp);
}

int pin_find(const char *file)
{
    if (!pins_read) pins_load();
    for (int i = 0; i < npins; i++)
        if (!strcmp(pins[i], file)) return i;
    return -1;
}

void pin_set(const char *file, int on)
{
    int i = pin_find(file);
    if (on && i < 0 && npins < 256) snprintf(pins[npins++], sizeof pins[0], "%.199s", file);
    if (!on && i >= 0) {
        memmove(pins[i], pins[i + 1], (size_t)(npins - i - 1) * sizeof pins[0]);
        npins--;
    }
    pins_save();
}

/* Everything in the Saved sounds folder into the library (once each), marked saved and pinned. */
int saved_sync(void)
{
    if (!saved_enabled) return 0;
    int was_clean = !is_dirty(), added = 0;
    char d[1024];
    saved_dir(d, sizeof d);
    pins_load();
    for (int i = 0; i < P.nsamples; i++) {      /* sounds already here */
        Sample *sm = &P.samples[i];
        sm->saved = in_saved_dir(sm->path) && file_exists(sm->path);
        sm->pinned = sm->saved && pin_find(path_base(sm->path)) >= 0;
    }
    DIR *dir = opendir(d);
    if (!dir) return 0;
    struct dirent *e;
    while ((e = readdir(dir))) {
        const char *nm = e->d_name, *dot = strrchr(nm, '.');
        if (nm[0] == '.' || !dot || (strcasecmp(dot, ".wav") && strcasecmp(dot, ".flac") && strcasecmp(dot, ".ogg") &&
                                     strcasecmp(dot, ".mp3") && strcasecmp(dot, ".aiff") && strcasecmp(dot, ".aif")))
            continue;
        char path[1300], err[300];
        snprintf(path, sizeof path, "%s/%s", d, nm);
        int have = 0;
        for (int i = 0; i < P.nsamples && !have; i++) have = !strcmp(P.samples[i].path, path);
        if (have) continue;
        LOCK();
        int idx = sample_import(&P, path, err, sizeof err);
        if (idx >= 0) {
            Sample *sm = &P.samples[idx];
            snprintf(sm->name, sizeof sm->name, "%.*s", (int)(dot - nm) < 90 ? (int)(dot - nm) : 90, nm);
            sm->saved = 1;
            sm->pinned = pin_find(nm) >= 0;
            added++;
        }
        UNLOCK();
    }
    closedir(dir);
    if (was_clean) saved_nsamples = P.nsamples;   /* the saved sounds don't make a project "changed" */
    return added;
}

/* Saves a copy of sound i in the folder (if it isn't there yet). Returns 0 when it's saved. */
int sound_save(int i)
{
    Sample *sm = &P.samples[i];
    if (sm->saved && file_exists(sm->path)) return 0;
    char d[1024], base[100], path[1300], err[300];
    saved_dir(d, sizeof d);
    ensure_dir(d);
    snprintf(base, sizeof base, "%.80s", sm->name);
    for (char *q = base; *q; q++)
        if (strchr("/\\:*?\"<>|", *q) || (unsigned char)*q < 32) *q = '_';
    snprintf(path, sizeof path, "%s/%s.wav", d, base);
    for (int k = 2; file_exists(path) && k < 1000; k++) snprintf(path, sizeof path, "%s/%s %d.wav", d, base, k);
    if (sample_write_wav(sm, path, err, sizeof err) != 0) {
        status("Couldn't save \"%s\": %s", sm->name, err);
        return -1;
    }
    LOCK();
    snprintf(sm->path, sizeof sm->path, "%.1023s", path);
    sm->saved = 1;
    UNLOCK();
    return 0;
}

void sound_pin(int i, int on)
{
    Sample *sm = &P.samples[i];
    if (on && sound_save(i) != 0) return;
    if (sm->saved) pin_set(path_base(sm->path), on);
    sm->pinned = on && sm->saved;
    status(on ? "Pinned \"%s\": it stays at the top of the library, in every project" : "Unpinned \"%s\" (it's still saved)",
           sm->name);
}

/* Takes a sound out of the saved ones. Its file moves to Music/tinydaw/Unsaved sounds rather than
 * being deleted, so projects that use it still open. */
void sound_unsave(int i)
{
    Sample *sm = &P.samples[i];
    if (!sm->saved) return;
    char d[1024], to[1024];
    saved_dir(d, sizeof d);
    snprintf(to, sizeof to, "%s", d);
    char *slash = strrchr(to, '/');
    if (slash) snprintf(slash + 1, sizeof to - (size_t)(slash + 1 - to), "Unsaved sounds");
    else snprintf(to, sizeof to, "Unsaved sounds");
    ensure_dir(to);
    size_t l = strlen(to);
    snprintf(to + l, sizeof to - l, "/%s", path_base(sm->path));
    pin_set(path_base(sm->path), 0);
    if (rename(sm->path, to) == 0) {
        LOCK();
        snprintf(sm->path, sizeof sm->path, "%.1023s", to);
        UNLOCK();
    } else {
        remove(sm->path);                       /* another drive: the sound stays in this project's memory */
    }
    sm->saved = sm->pinned = 0;
    status("\"%s\" isn't saved any more: it won't be in new projects", sm->name);
}

/* The library row, pad or instrument slot under the pointer, if any (for Ctrl+C / Ctrl+V). */
int sound_under_mouse(int *kind, int *idx)
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
void cm_open(int kind, int idx)
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
    if (kind == CM_LIB && has) {                /* the clip editor, pin and save, before Remove */
        snprintf(cm.label[5], sizeof cm.label[5], "%s", cm.label[2]);
        cm.act[5] = cm.act[2];
        cm.enabled[5] = 1;
        snprintf(cm.label[2], sizeof cm.label[2], "Open in the clip editor");
        cm.act[2] = CMA_EDIT;
        cm.enabled[2] = P.samples[s].data && P.samples[s].frames > 0;
        snprintf(cm.label[3], sizeof cm.label[3], P.samples[s].pinned ? "Unpin" : "Pin to the top");
        cm.act[3] = CMA_PIN;
        cm.enabled[3] = 1;
        snprintf(cm.label[4], sizeof cm.label[4], P.samples[s].saved ? "Forget the saved copy" : "Save for future sessions");
        cm.act[4] = CMA_SAVE;
        cm.enabled[4] = 1;
        cm.n = 6;
    }
    int w = 0;
    for (int i = 0; i < cm.n; i++) w = text_w(cm.label[i]) > w ? text_w(cm.label[i]) : w;
    w += 28;
    int h = cm.n * (font_h + 12) + 8;
    cm.box = (SDL_Rect){mx + 2, my + 2, w, h};
    if (cm.box.x + w > W - 4) cm.box.x = W - 4 - w;
    if (cm.box.y + h > H - 4) cm.box.y = H - 4 - h;
}

void cm_open_section(int i)
{
    memset(&cm, 0, sizeof cm);
    cm.open = 1;
    cm.kind = CM_SEC;
    cm.idx = i;
    static const char *const L5[5] = {"Rename", "Loop it", "Copy it in after itself", "Remove the marker",
                                      "Remove it and its bars"};
    static const int A5[5] = {CMA_SREN, CMA_SLOOP, CMA_SDUP, CMA_SDEL, CMA_SCUT};
    int w = 0;
    for (int k = 0; k < 5; k++) {
        snprintf(cm.label[k], sizeof cm.label[k], "%s", L5[k]);
        cm.act[k] = A5[k];
        cm.enabled[k] = 1;
        w = text_w(L5[k]) > w ? text_w(L5[k]) : w;
    }
    cm.n = 5;
    w += 28;
    int h = cm.n * (font_h + 12) + 8;
    cm.box = (SDL_Rect){mx + 2, my + 2, w, h};
    if (cm.box.x + w > W - 4) cm.box.x = W - 4 - w;
    if (cm.box.y + h > H - 4) cm.box.y = H - 4 - h;
}

void sec_action(int act, int i)
{
    if (i < 0 || i >= P.nsections) return;
    Section sc = P.sections[i];
    switch (act) {
    case CMA_SREN:
        tf_end(1);
        tf.buf = P.sections[i].name;
        tf.caret = (size_t)-1;
        tf.cap = sizeof P.sections[i].name;
        tf.path = 0;
        tf_drawn = 1;
        snprintf(tf.orig, sizeof tf.orig, "%s", tf.buf);
        SDL_StartTextInput();
        sec_renaming = i;
        break;
    case CMA_SLOOP:
        LOCK();
        P.loop_start = meter_bar_frame(&P, sc.bar);
        P.loop_end = meter_bar_frame(&P, sc.bar + sc.bars);
        P.loop = 1;
        UNLOCK();
        status("Looping %s", sc.name);
        break;
    case CMA_SDUP:
        song_edit((SongEdit){SE_COPY, sc.bar, sc.bar + sc.bars, 0});
        status("A copy of %s, with everything in it, from bar %d; what came after moved along", sc.name, sc.bar + sc.bars + 1);
        break;
    case CMA_SDEL:
        memmove(&P.sections[i], &P.sections[i + 1], (size_t)(P.nsections - i - 1) * sizeof(Section));
        P.nsections--;
        status("Removed the %s marker (the music is still there)", sc.name);
        break;
    case CMA_SCUT:
        song_edit((SongEdit){SE_CUT, sc.bar, sc.bar + sc.bars, 0});
        status("Took out %s and its %d bars; what came after moved up. Ctrl+Z brings it back", sc.name, sc.bars);
        break;
    }
}

/* Drawn last, over everything; takes the next click. */
void cm_draw(void)
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
        case CMA_PIN: sound_pin(cm.idx, !P.samples[cm.idx].pinned); break;
        case CMA_EDIT: editor_open_sound(cm.idx); break;
        case CMA_SREN: case CMA_SLOOP: case CMA_SDUP: case CMA_SDEL: case CMA_SCUT: sec_action(cm.act[chosen], cm.idx); break;
        case CMA_CCHG: case CMA_CHEAR: case CMA_CDUP: case CMA_CKEY: case CMA_CDEL: case CMA_CDETECT: case CMA_CPART:
        case CMA_CCLEAR: chord_action(cm.act[chosen], cm.idx); break;
        default:
            if (cm.kind == CM_SESS) sess_action(cm.act[chosen]);
            break;
        case CMA_SAVE:
            if (P.samples[cm.idx].saved) sound_unsave(cm.idx);
            else if (sound_save(cm.idx) == 0)
                status("Saved \"%s\": it will be in the library of every project", P.samples[cm.idx].name);
            break;
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

void library_list(int x, int y, int w, int bottom)
{
    char buf[64];
    memset(lib_rects, 0, sizeof lib_rects);   /* only the rows drawn now can be hit */
    static int vis[MAX_SAMPLES];              /* pitched/reversed copies made for clips are hidden */
    int nvis = 0;
    for (int pass = 0; pass < 2; pass++)         /* pinned sounds first */
        for (int i = 0; i < P.nsamples; i++)
            if ((!P.samples[i].pitched || P.samples[i].listed) && !P.samples[i].removed && !P.samples[i].hidden &&
                (P.samples[i].pinned != 0) == (pass == 0))
                vis[nvis++] = i;
    snprintf(buf, sizeof buf, "%d sound%s", nvis, nvis == 1 ? "" : "s");
    text(x, y, buf, DIM);
    y += font_h + 10;
    SDL_Rect list = {x, y, w, bottom - y - 2 * font_h - 20};
    int item_h = 26, visible = list.h / item_h;
    if (visible < 1) visible = 1;
    if (nvis == 0) {
        text(x, y + 4, "Nothing here yet. Make one under", DIM);
        text(x, y + 4 + font_h + 2, "Synth or Voice, or drop audio files", DIM);
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
        tip(r, "%s (%.2f s%s): click to hear it, drag it onto a track or a pad, right-click to copy, paste, pin, save or remove",
            P.samples[i].name, (double)P.samples[i].frames / SR,
            P.samples[i].pinned ? ", pinned and saved" : P.samples[i].saved ? ", saved" : "");
        int hov = hit(r) && drag.kind == D_NONE;
        fill(r, P.preview == i ? tone(ACCENT, 0.45f) : hov ? PANEL3 : PANEL2);
        if (lib_flash == i && SDL_GetTicks() - lib_flash_at < 1500) outline(r, ACCENT);   /* just pasted */
        if (P.samples[i].nvel > 0)               /* layers: velocity x round-robin */
            snprintf(buf, sizeof buf, "%dx%d  %.2fs", P.samples[i].nvel, P.samples[i].nrr, (double)P.samples[i].frames / SR);
        else
            snprintf(buf, sizeof buf, "%.2fs", (double)P.samples[i].frames / SR);
        int dw = text_w(buf);
        SDL_Rect xr = {r.x + r.w - 24, r.y + 2, 20, r.h - 4};     /* remove: shown on the row under the mouse */
        int nx = r.x + 8;
        if (P.samples[i].pinned) {               /* a pin: a head and its point */
            fill((SDL_Rect){r.x, r.y, 3, r.h}, ACCENT);
            disc_small(r.x + 12, r.y + r.h / 2 - 3, 3, ACCENT);
            line(r.x + 12, r.y + r.h / 2, r.x + 12, r.y + r.h / 2 + 6, ACCENT);
            nx = r.x + 22;
        } else if (P.samples[i].saved) {         /* saved: a small dot */
            disc_small(r.x + 10, r.y + r.h / 2, 2, DIM);
            nx = r.x + 18;
        }
        text_clip((SDL_Rect){r.x + 6, r.y, r.w - dw - 18 - (hov ? 24 : 0), r.h}, nx, r.y + (r.h - font_h) / 2,
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

float *heard_buf;                /* what the New sound Play button started */
SDL_Rect ns_note_r;              /* where New sound's Note box is, for the self-test */

int new_sound(int x, int y, int w)
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

/* ---- Create > Synth > Presets (the Generator): genre presets on a small synth, shaped with five plain controls.
 * Made for people who've never used a synth: pick a style, pick what the sound is for, click a
 * sound to hear it, nudge it with words like "brighter" and "fatter", then keep it. */

int gx_genre = -1, gx_kind = -1, gx_preset = -1;   /* -1 = any */

SynthSound GX;
float *gx_heard;                 /* what the Generator's Play started */
double *slide_done;              /* the slider whose drag just ended */

/* A 0..1 slider with a mark at the middle ("as designed"). Returns 1 when a drag on it ends,
 * or a double-click puts it back to the middle. */
int slider(SDL_Rect r, double *v)
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

void gx_play(void)
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

int gx_reveal;                   /* scroll the picked sound's controls into view */
SDL_Rect gx_row_r[128], gx_genre_r[16], gx_kind_r[12], gx_slide_r[MACROS], gx_lib_r, gx_place_r, gx_inst_r;

void gx_pick(int preset)
{
    gx_reveal = gx_preset != preset;
    gx_preset = preset;
    synth_from_preset(&GX, preset);
    gx_play();
}

int gx_matches(int i)
{
    const SynthPreset *p = &SYNTH_PRESETS[i];
    return (gx_genre < 0 || !strcmp(p->genre, SYNTH_GENRES[gx_genre])) &&
           (gx_kind < 0 || !strcmp(p->kind, SYNTH_KINDS[gx_kind]));
}

/* the library name: the preset's name, plus the note when it isn't the preset's own */
void gx_name(char *buf, size_t n)
{
    const SynthPreset *p = &SYNTH_PRESETS[gx_preset];
    char nn[16];
    note_name(GX.note, nn, sizeof nn);
    if (GX.note != p->note && strcmp(p->kind, "Drums") && strcmp(p->kind, "FX")) snprintf(buf, n, "%s %s", p->name, nn);
    else snprintf(buf, n, "%s", p->name);
}

int gx_nvel = 3, gx_nrr = 2;      /* the layers new Generator sounds get */

int gx_keep(void)
{
    char name[96];
    gx_name(name, sizeof name);
    GX.a4 = P.a4;
    int idx = sample_generate_synth(&P, &GX, name);
    if (idx < 0) status("Library is full");
    else if (gx_nvel * gx_nrr > 1) sample_make_layers(&P, idx, gx_nvel, gx_nrr);
    return idx;
}

/* the first track with nothing playing from `at` for `len` frames */
int free_track_at(long at, long len)
{
    for (int t = 0; t < P.ntracks; t++) {
        int busy = 0;
        for (int c = 0; c < P.nclips && !busy; c++)
            busy = P.clips[c].track == t && P.clips[c].start < at + len && P.clips[c].start + P.clips[c].length > at;
        if (!busy) return t;
    }
    return 0;
}

void gx_surprise(void)
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
int gx_row(int x, int y, int w, int i)
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
int gx_shape(int x, int y, int w)
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
        static const char *const MTIPS[MACROS] = {
            "Brightness: muffled (dark) to crisp (bright)",
            "Softness: a hard start and short tail (snappy) to a slow fade in and long tail (smooth)",
            "Thickness: one thin voice to several detuned, wider and with more sub (fat)",
            "Grit: clean, or saturated and then crushed (dirty)",
            "Wobble: still, or the filter moving in a rhythm (wobbly)"};
        tip((SDL_Rect){x, y, w, font_h + 8}, "%s. Double-click the slider to put it back in the middle", MTIPS[m]);
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
    y += 34;
    {                                       /* velocity layers and round-robins for what's kept */
        char nb[8];
        text(x, y + 5, "Layers", DIM);
        int lx = x + text_w("Layers") + 8, bw = 26;
        for (int i = 1; i <= MAX_VEL; i++) {
            snprintf(nb, sizeof nb, "%d", i);
            SDL_Rect b = {lx + (i - 1) * (bw + 3), y, bw, 24};
            tip(b, i == 1 ? "One sound at every velocity" : "%d velocity layers: softer hits play a darker, gentler copy", i);
            if (button(b, nb, gx_nvel == i)) gx_nvel = i;
        }
        int rx = lx + MAX_VEL * (bw + 3) + 12;
        text(rx, y + 5, "Round-robins", DIM);
        rx += text_w("Round-robins") + 8;
        for (int i = 1; i <= MAX_RR; i++) {
            snprintf(nb, sizeof nb, "%d", i);
            SDL_Rect b = {rx + (i - 1) * (bw + 3), y, bw, 24};
            tip(b, i == 1 ? "Every hit the same" : "%d slightly different copies, taken in turn, so repeated hits don't sound machine-made", i);
            if (button(b, nb, gx_nrr == i)) gx_nrr = i;
        }
        y += 32;
    }
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

int generator(int x, int y, int w)
{
    y = section(x, y, w, "1  WHAT KIND OF MUSIC?");
    {
        int cols = 5, gap = 4, widest = 0;          /* as many columns as the longest name allows */
        for (int i = 0; i < NSYNTH_GENRES; i++) widest = text_w(SYNTH_GENRES[i]) > widest ? text_w(SYNTH_GENRES[i]) : widest;
        while (cols > 2 && (w - gap * (cols - 1)) / cols < widest + 14) cols--;
        int bw = (w - gap * (cols - 1)) / cols;
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
        int cols = 5, gap = 4, widest = text_w("Anything");
        for (int i = 0; i < NSYNTH_KINDS; i++) widest = text_w(SYNTH_KINDS[i]) > widest ? text_w(SYNTH_KINDS[i]) : widest;
        while (cols > 2 && (w - gap * (cols - 1)) / cols < widest + 14) cols--;
        int bw = (w - gap * (cols - 1)) / cols;
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

const char *const TUNE_NAMES[TUNES] = {"One note", "Up and down", "Rising", "Falling", "Bouncy",
                                              "Call and answer", "From a note clip"};

const int TUNE_DEGREES[TUNE_CLIP][8] = {
    {0, 0, 0, 0, 0, 0, 0, 0}, {0, 1, 2, 3, 4, 3, 2, 1}, {0, 1, 2, 3, 4, 5, 6, 7},
    {4, 3, 2, 1, 0, 1, 2, 3}, {0, 2, 4, 2, 0, 2, 4, 7}, {0, 0, 2, 4, 4, 2, 1, 0}};

VocalSpec SV;
int sv_init, sv_espeak = -1;
float *sv_heard;
char sv_err[200];
SDL_Rect sv_words_r, sv_tune_r[TUNES], sv_sing_r, sv_lib_r, sv_place_r, sv_voice_r[2], sv_style_r[VS_STYLES], sv_singer_r[8], sv_bend_r[4];

void sv_defaults(void)
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
int key_note(int first, int deg)
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
int sv_clip(void)
{
    if (selected >= 0 && selected < P.nclips && P.clips[selected].pattern > 0) return selected;
    for (int c = 0; c < P.nclips; c++)
        if (P.clips[c].pattern > 0) return c;
    return -1;
}

/* Fills SV.notes from the words and the tune. Returns how many syllables there are. */
int sv_make_notes(void)
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

void sv_name(char *buf, size_t n)
{
    char w[40] = "";
    int k = 0;
    for (const char *p = SV.words; *p && k < 30; p++)    /* the first words */
        if (*p != '-') w[k++] = *p;
    w[k] = 0;
    snprintf(buf, n, "Sung: %s%s", w, strlen(SV.words) > 30 ? "..." : "");
}

int sv_keep(void)
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

void sv_sing(void)
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

/* ---- the Singer's pitch curve: bends drawn over the line's notes (slides, scoops, fall-offs) */

SDL_Rect sv_curve_r;

double sv_last_beat = -1;

void sv_bend_erase(double a, double b)   /* points with beats in [a, b] go */
{
    if (a > b) {
        double t = a;
        a = b;
        b = t;
    }
    int w = 0;
    for (int i = 0; i < SV.nbend; i++)
        if (SV.bend_beat[i] < a || SV.bend_beat[i] > b) {
            SV.bend_beat[w] = SV.bend_beat[i];
            SV.bend_semis[w++] = SV.bend_semis[i];
        }
    SV.nbend = w;
}

void sv_bend_put(double beat, double semis)
{
    if (SV.nbend >= VOCAL_BENDS) return;
    int k = SV.nbend;
    while (k > 0 && SV.bend_beat[k - 1] > beat) k--;
    memmove(&SV.bend_beat[k + 1], &SV.bend_beat[k], (size_t)(SV.nbend - k) * sizeof(float));
    memmove(&SV.bend_semis[k + 1], &SV.bend_semis[k], (size_t)(SV.nbend - k) * sizeof(float));
    SV.bend_beat[k] = (float)beat;
    SV.bend_semis[k] = (float)(semis < -24 ? -24 : semis > 24 ? 24 : semis);
    SV.nbend++;
}

/* the note sounding at `beat` (or the nearest one), as an index into SV.notes */
int sv_note_at(double beat, int n, int nearest)
{
    int best = -1;
    double bd = 1e9;
    for (int i = 0; i < n; i++) {
        const VocalNote *nt = &SV.notes[i];
        if (beat >= nt->start && beat < nt->start + nt->len) return i;
        double d = beat < nt->start ? nt->start - beat : beat - (nt->start + nt->len);
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return nearest ? best : -1;
}

void sv_bend_preset(int kind, int n)
{
    SV.nbend = 0;
    for (int i = 0; i < n; i++) {
        const VocalNote *nt = &SV.notes[i];
        double s0 = nt->start, e0 = nt->start + nt->len;
        const VocalNote *nx = i + 1 < n ? &SV.notes[i + 1] : NULL;
        if (kind == BEND_SCOOP) {                /* up into each note from below */
            double d = fmin(0.15, nt->len / 3);
            sv_bend_put(s0 - 0.001, 0);
            sv_bend_put(s0, -1.5);
            sv_bend_put(s0 + d, 0);
        } else if (kind == BEND_SLIDE && nx && nx->start - e0 < 0.3 && nx->note != nt->note) {
            double d = fmin(0.25, nt->len / 2);   /* glide into the next note at the end of this one */
            sv_bend_put(e0 - d, 0);
            sv_bend_put(e0 - 0.001, nx->note - nt->note);
            sv_bend_put(nx->start, 0);
        } else if (kind == BEND_FALL && (!nx || nx->start - e0 > 0.1)) {
            double d = fmin(0.35, nt->len / 2);   /* the voice drops away at the end of a phrase */
            sv_bend_put(e0 - d, 0);
            sv_bend_put(e0 - 0.001, -4);
            sv_bend_put(e0 + 0.001, 0);
        }
    }
}

int sv_curve(int x, int y, int w)
{
    char syl[VOCAL_NOTES][40];
    int nsyl = vocal_syllables(SV.words, syl, NULL, VOCAL_NOTES);
    sv_make_notes();
    int n = nsyl < SV.nnotes ? nsyl : SV.nnotes;
    if (n <= 0) {
        text(x, y, "Type some words first: their notes show here to bend.", DIM);
        return y + font_h + 12;
    }
    double end = 0;
    int lo = 127, hi = 0;
    for (int i = 0; i < n; i++) {
        end = fmax(end, SV.notes[i].start + SV.notes[i].len);
        lo = SV.notes[i].note < lo ? SV.notes[i].note : lo;
        hi = SV.notes[i].note > hi ? SV.notes[i].note : hi;
    }
    for (int i = 0; i < SV.nbend; i++) {        /* room for the bends too */
        int nn = sv_note_at(SV.bend_beat[i], n, 1);
        if (nn < 0) continue;
        double p = SV.notes[nn].note + SV.bend_semis[i];
        lo = p < lo ? (int)floor(p) : lo;
        hi = p > hi ? (int)ceil(p) : hi;
    }
    lo -= 3;
    hi += 3;
    if (hi - lo < 12) {
        int c = (hi + lo) / 2;
        lo = c - 6;
        hi = c + 6;
    }
    end += 0.25;
    SDL_Rect r = {x, y, w, 150};
    sv_curve_r = r;
    fill(r, PANEL2);
    SDL_Rect wr = {x, y + r.h, w, font_h + 6};   /* the words, under their notes */
    fill(wr, tone(PANEL2, 0.85f));
#define BX(b) (r.x + (int)((b) / end * r.w))
#define PY(p) (r.y + (int)(((hi) - (p)) / (double)(hi - lo) * r.h))
    for (int p = lo + 1; p < hi; p++)            /* semitones; C stronger */
        line(r.x, PY(p), r.x + r.w - 1, PY(p), (p % 12) == 0 ? tone(PANEL2, 1.5f) : tone(PANEL2, 1.15f));
    double rh = (double)r.h / (hi - lo);
    for (int i = 0; i < n; i++) {                /* the notes, and their words */
        const VocalNote *nt = &SV.notes[i];
        SDL_Rect nr = {BX(nt->start), PY(nt->note + 0.5), BX(nt->start + nt->len) - BX(nt->start) - 1, (int)rh < 4 ? 4 : (int)rh};
        fill(nr, tone(PANEL3, 1.3f));
        SDL_Rect sr = {nr.x, wr.y, nr.w, wr.h};
        if (sr.w > 8) text_clip(sr, sr.x + 2, wr.y + 3, syl[i], DIM);
    }
    int px0 = -1, py0 = -1;                      /* the pitch as it will be sung */
    for (int px = 0; px < r.w; px++) {
        double b = (double)px / r.w * end;
        int nn = sv_note_at(b, n, 0);
        if (nn < 0) {
            px0 = -1;
            continue;
        }
        double p = SV.notes[nn].note + vocal_bend_at(&SV, b);
        int yy = PY(p);
        yy = yy < r.y ? r.y : yy > r.y + r.h - 1 ? r.y + r.h - 1 : yy;
        if (px0 >= 0) {
            line(r.x + px0, py0, r.x + px, yy, ACCENT);
            line(r.x + px0, py0 + 1, r.x + px, yy + 1, ACCENT);
        }
        px0 = px;
        py0 = yy;
    }
    for (int i = 0; i < SV.nbend; i++) {         /* the points */
        int nn = sv_note_at(SV.bend_beat[i], n, 0);   /* (between notes nothing is sung: not shown) */
        if (nn < 0) continue;
        int bx = BX(SV.bend_beat[i]), by = PY(SV.notes[nn].note + SV.bend_semis[i]);
        if (bx >= r.x && bx < r.x + r.w && by >= r.y && by < r.y + r.h) fill((SDL_Rect){bx - 2, by - 2, 5, 5}, HEAD);
    }
    outline(r, LINE);
    tip(r, "Draw over the notes to bend the pitch (Shift: whole semitones). Right-click rubs out a bend, "
           "Ctrl+drag rubs out along the way");
    /* drawing */
    if (hit(r) && drag.kind == D_NONE && rpress) {
        rpress = 0;
        double b = (double)(mx - r.x) / r.w * end;
        sv_bend_erase(b - end / 40, b + end / 40);
    }
    if (hit(r) && drag.kind == D_NONE && lpress) {
        drag = (typeof(drag)){.kind = (mods & KMOD_CTRL) ? D_BENDX : D_BEND, .sx = mx, .sy = my};
        sv_last_beat = -1;
        lpress = 0;
    }
    if ((drag.kind == D_BEND || drag.kind == D_BENDX) && ldown) {
        double b = (double)(mx - r.x) / r.w * end;
        b = b < 0 ? 0 : b > end ? end : b;
        double from = sv_last_beat < 0 ? b : sv_last_beat;
        if (drag.kind == D_BENDX) {
            sv_bend_erase(from - 0.02, b + 0.02);
        } else {
            int nn = sv_note_at(b, n, 1);
            double p = hi - (double)(my - r.y) / r.h * (hi - lo), semis = p - SV.notes[nn].note;
            semis = (mods & KMOD_SHIFT) ? round(semis) : round(semis * 20) / 20;
            if (b > from) sv_bend_erase(from + 1e-6, b);     /* what the stroke passed over, not where it was */
            else if (b < from) sv_bend_erase(b, from - 1e-6);
            else sv_bend_erase(b, b);
            sv_bend_put(b, semis);
        }
        sv_last_beat = b;
    }
#undef BX
#undef PY
    y += r.h + wr.h + 8;
    static const char *const PRE[4] = {"Scoops", "Slides", "Fall-offs", "Flat"};
    static const char *const PTIP[4] = {"Each note scooped up into from below",
                                        "Glides from note to note where they join", "The voice drops away at the end of each phrase",
                                        "No bends: every note straight"};
    int bw = (w - 3 * 4) / 4;
    for (int i = 0; i < 4; i++) {
        SDL_Rect b = {x + i * (bw + 4), y, i == 3 ? w - 3 * (bw + 4) : bw, 26};
        sv_bend_r[i] = b;
        tip(b, "%s", PTIP[i]);
        if (button(b, PRE[i], 0)) {
            if (i == 3) SV.nbend = 0;
            else sv_bend_preset(i, n);
            status(i == 3 ? "Every note straight again" : "%s drawn in: press Sing it to hear them, or draw over them",
                   PRE[i]);
        }
    }
    y += 34;
    text(x, y, "Draw on it to bend, right-click to rub out, then Sing it.", DIM);
    return y + font_h + 12;
}

int singer(int x, int y, int w)
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

    y = section(x, y, w, "4  SHAPE THE PITCH");
    y = sv_curve(x, y, w);

    y = section(x, y, w, "5  HEAR IT AND KEEP IT");
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

void sounds_pane(void)
{
    SDL_Rect pane = panel_rect();
    int saved, inside = panel_begin(pane, &sounds_scroll, sounds_content_h, &saved);
    int x = pane.x + 12, w = pane.w - 24, y0 = pane.y + 10 - (sounds_sub ? sounds_scroll : 0), y = y0;
    /* sounds_sub: 0 library, 1 synth presets (the Generator), 2 voice (the Singer), 3 tones, 4 beats, 5 parts */
    static const char *const SUBS[] = {"Library", "Synth", "Voice", "Beats", "Parts"};
    static int synth_last = 1;
    int top = sounds_sub == 0 ? 0 : sounds_sub == 1 || sounds_sub == 3 ? 1 : sounds_sub == 2 ? 2 : sounds_sub == 4 ? 3 : 4, was = top;
    y = subtabs(x, y, w, SUBS, 5, &top);
    if (top != was) {
        sounds_sub = top == 0 ? 0 : top == 1 ? synth_last : top == 2 ? 2 : top == 3 ? 4 : 5;
        sounds_scroll = 0;
    }
    if (top == 1) {                             /* Synth: the Generator's presets, or plain tones */
        static const char *const SYN[] = {"Presets", "Tones"};
        int s2 = sounds_sub == 3, w2 = s2;
        y = subtabs(x, y - 6, w, SYN, 2, &s2);
        if (s2 != w2) sounds_scroll = 0;
        sounds_sub = synth_last = s2 ? 3 : 1;
    }
    if (sounds_sub == 4) {
        y = tab_genres(x, y, w - 14);
        sounds_content_h = y - y0 + 10;
    } else if (sounds_sub == 5) {
        y = parts_tab(x, y, w - 14);
        sounds_content_h = y - y0 + 10;
    } else if (sounds_sub == 0) {
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

char audio_out[256];             /* output device name, "" = the system default */

int settings_scroll, settings_content_h;

int settings_content(int x, int y, int w)
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
        y += font_h + 16;
        y = section(x, y, w, "TOOLTIPS");
        if (button((SDL_Rect){x, y, w, 28}, tips_on ? "Tooltips: on (hold the mouse still over something)" : "Tooltips: off",
                   tips_on)) {
            tips_on = !tips_on;
            settings_save();
        }
        y += 40;
        y = section(x, y, w, "INTERFACE SIZE");
        {                                       /* everything bigger or smaller, sharp at any size */
            static const int SIZES[6] = {0, 100, 125, 150, 175, 200};
            static const char *const NAMES[6] = {"Automatic", "100%", "125%", "150%", "175%", "200%"};
            float top = ui_max();
            int aw = text_w("Automatic") + 24, bw = (w - aw - 5 * 4) / 5;   /* "Automatic" gets the room it needs */
            for (int i = 0; i < 6; i++) {
                int fits = !SIZES[i] || SIZES[i] / 100.0f <= top + 0.001f;
                SDL_Rect r = i == 0 ? (SDL_Rect){x, y, aw, 30}
                                    : (SDL_Rect){x + aw + 4 + (i - 1) * (bw + 4), y, i == 5 ? w - aw - 4 - 4 * (bw + 4) : bw, 30};
                if (button2(r, NAMES[i], ui_choice == SIZES[i], fits)) ui_set(SIZES[i]);
            }
            y += 38;
            char buf[120];
            snprintf(buf, sizeof buf, "Now %.0f%%%s. Ctrl+= and Ctrl+- change it, Ctrl+0 back to automatic.",
                     ui_user * 100, ui_choice ? "" : " (automatic)");
            text_clip((SDL_Rect){x, y, w, font_h + 2}, x, y, buf, DIM);
            y += font_h + 4;
            if (top < 2) {
                snprintf(buf, sizeof buf, "This screen has room for up to %.0f%%.", floorf(top * 4) / 4 * 100);
                text(x, y, buf, DIM);
                y += font_h + 4;
            }
            y += 6;
        }
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
        y = section(x, y, w, "INPUT: THE MICROPHONE");
        {                                       /* the system default, or a particular device */
            static char names[DD_MAX][48];
            static const char *ptrs[DD_MAX];
            static int pick;
            int n = SDL_GetNumAudioDevices(1), m = 1;
            snprintf(names[0], sizeof names[0], "System default");
            ptrs[0] = names[0];
            int cur = 0;
            for (int i = 0; i < n && m < DD_MAX; i++) {
                const char *name = SDL_GetAudioDeviceName(i, 1);
                if (!name) continue;
                if (!strcmp(audio_in, name)) cur = m;
                snprintf(names[m], sizeof names[m], "%s", name);
                ptrs[m] = names[m];
                m++;
            }
            if (!(dd.open && dd.sel == &pick)) pick = cur;
            if (dropdown((SDL_Rect){x, y, w, 30}, "", ptrs, m, &pick, 0)) {
                if (pick == 0) audio_in[0] = 0;
                else snprintf(audio_in, sizeof audio_in, "%s", names[pick]);
                if (cap_dev) open_input();
                settings_save();
            }
            y += 38;
            SDL_Rect mr = {x, y + 4, w - 90, 12};    /* its level, live */
            static Meter im;
            im.level = in_meter;
            im.peak = fmaxf(im.peak * 0.97f, in_meter);
            if (in_meter > 0.99f) im.clipped = 1;
            meter_draw(mr, &im, 0);
            if (hit(mr) && lpress) im.clipped = 0;
            text(x + w - 82, y + 2, cap_dev ? (in_meter > 0.0005f ? "listening" : "silent") : "closed", cap_dev ? TEXT : DIM);
            y += 26;
            double g = in_gain_db;
            number((SDL_Rect){x, y, (w - 8) / 2, 26}, "Input gain", &g, -24, 24, 0.05, "%+.1f dB");
            if (g != in_gain_db) {
                in_gain_db = (float)g;
                settings_save();
            }
            if (button((SDL_Rect){x + (w - 8) / 2 + 8, y, (w - 8) / 2, 26}, monitor_on ? "Hear yourself: on" : "Hear yourself: off",
                       monitor_on)) {
                monitor_on = !monitor_on;
                settings_save();
            }
            y += 34;
            static double off;
            if (!(drag.kind == D_NUM && drag.num == &off) && !(numedit.active && numedit.v == &off)) off = rec_offset();
            char lab[40];
            snprintf(lab, sizeof lab, rec_offset_ms < 0 ? "Offset (auto)" : "Offset");
            number((SDL_Rect){x, y, (w - 8) / 2, 26}, lab, &off, 0, 500, 0.2, "%.0f ms");
            if (fabs(off - rec_offset()) > 0.01) {
                rec_offset_ms = off;
                settings_save();
            }
            if (button((SDL_Rect){x + (w - 8) / 2 + 8, y, (w - 8) / 2, 26}, "Automatic offset", rec_offset_ms < 0)) {
                rec_offset_ms = -1;
                settings_save();
            }
            y += 34;
            {                                   /* word-wrapped to the panel */
                const char *help = "Arm a track with its red dot, then press Rec. Wear headphones to hear yourself, "
                                   "or the speakers feed back. The offset moves each take earlier, to make up for "
                                   "the system's delay.";
                char ln[200];
                int n = 0;
                const char *q = help;
                while (*q) {
                    int k = 0, brk = 0;
                    while (q[k] && k < (int)sizeof ln - 1) {
                        memcpy(ln, q, (size_t)k + 1);
                        ln[k + 1] = 0;
                        if (text_w(ln) > w) break;
                        if (q[k] == ' ') brk = k;
                        k++;
                    }
                    int take_n = q[k] ? (brk ? brk : k) : k;
                    memcpy(ln, q, (size_t)take_n);
                    ln[take_n] = 0;
                    text(x, y + n++ * (font_h + 2), ln, DIM);
                    q += take_n;
                    while (*q == ' ') q++;
                }
                y += n * (font_h + 2) + 12;
            }
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
            {"Ctrl+E", "export"}, {"Ctrl+1 .. Ctrl+7", "open a panel"}, {"Ctrl+B", "hide / show the panel"},
            {"Ctrl+= / Ctrl+-", "interface bigger / smaller"}, {"Ctrl+0", "interface size: automatic"},
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

void settings_pane(void)
{
    SDL_Rect pane = panel_rect();
    int saved, inside = panel_begin(pane, &settings_scroll, settings_content_h, &saved);
    int y0 = pane.y + 10 - settings_scroll;
    int y = settings_content(pane.x + 14, y0, pane.w - 28);
    settings_content_h = y - y0 + 12;
    panel_end(pane, &settings_scroll, settings_content_h, inside, saved);
}

/* ---- settings that belong to this computer (theme, output device), not to a project */

const char *settings_file(void)
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

void settings_save(void)
{
    if (headless) return;
    FILE *f = fopen(settings_file(), "w");
    if (!f) return;
    fprintf(f, "# tinydaw settings\ntheme %d\naudio_output %s\npanel %d\n", theme_choice, audio_out, last_panel);
    fprintf(f, "ui_scale %d\ntooltips %d\n", ui_choice, tips_on);
    fprintf(f, "audio_input %s\ninput_gain %.2f\nmonitor %d\nrec_offset %.1f\n", audio_in, in_gain_db, monitor_on, rec_offset_ms);
    fprintf(f, "metronome %d %d %d %d %d %d %.3f\n", MET.on, MET.sound, MET.sub, MET.accent, MET.countin,
            MET.countin_rec_only, MET.volume);
    fprintf(f, "show %d %d %.0f\n", show_scene, show_cycle, show_each);
    fclose(f);
}

void settings_load(void)
{
    FILE *f = fopen(settings_file(), "r");
    if (!f) return;
    char line_[400];
    while (fgets(line_, sizeof line_, f)) {
        line_[strcspn(line_, "\r\n")] = 0;
        int v;
        if (sscanf(line_, "theme %d", &v) == 1 && v >= 0 && v < THEME_COUNT) theme_choice = v;
        else if (sscanf(line_, "ui_scale %d", &v) == 1 && (v == 0 || (v >= 50 && v <= 300))) ui_choice = v;
        else if (sscanf(line_, "tooltips %d", &v) == 1) tips_on = v != 0;
        else if (!strncmp(line_, "audio_input ", 12)) snprintf(audio_in, sizeof audio_in, "%.255s", line_ + 12);
        else if (sscanf(line_, "input_gain %f", &in_gain_db) == 1) in_gain_db = in_gain_db < -24 ? -24 : in_gain_db > 24 ? 24 : in_gain_db;
        else if (sscanf(line_, "monitor %d", &v) == 1) monitor_on = v != 0;
        else if (sscanf(line_, "rec_offset %lf", &rec_offset_ms) == 1) rec_offset_ms = rec_offset_ms > 500 ? 500 : rec_offset_ms;
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

SDL_atomic_t theme_probe;        /* 0 idle/unknown, 1 dark, 2 light, 3 asking */

int ask_desktop_theme(void)       /* 1 dark, 2 light, 0 unknown */
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

int theme_thread(void *arg)
{
    (void)arg;
    int t = ask_desktop_theme();
    SDL_AtomicSet(&theme_probe, t ? t : 1);
    return 0;
}

void theme_follow(void)          /* ask again (at start, when the window comes back to front) */
{
    if (theme_choice != THEME_SYSTEM || SDL_AtomicGet(&theme_probe) == 3) return;
    SDL_AtomicSet(&theme_probe, 3);
    SDL_Thread *t = SDL_CreateThread(theme_thread, "theme", NULL);
    if (t) SDL_DetachThread(t);
    else SDL_AtomicSet(&theme_probe, 1);
}

void theme_poll(void)
{
    int t = SDL_AtomicGet(&theme_probe);
    if (t != 1 && t != 2) return;
    SDL_AtomicSet(&theme_probe, 0);
    if (theme_choice == THEME_SYSTEM && (t == 2) != theme_light) theme_apply(t == 2);
}

/* ---------------------------------------------------------------- panels */

void top_bar(void)
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
    tip(file_btn, "The menu: new, open, save, examples, export audio and MIDI, the mixing console, settings");
    if (button(file_btn, "", menu_open)) menu_open = !menu_open;
    for (int i = 0; i < 3; i++) fill((SDL_Rect){x + 13, y + 9 + i * 5, 18, 2}, btn_fg(menu_open));
    x += 54;
    if (nundo > 0) tip((SDL_Rect){x, y, 54, h}, "Undo: %s (Ctrl+Z)", undo_stack[nundo - 1].label[0] ? undo_stack[nundo - 1].label : "the last change");
    if (button2((SDL_Rect){x, y, 54, h}, "Undo", 0, nundo > 0 || !state_same(&committed))) undo_redo(0);
    x += 58;
    if (nredo > 0) tip((SDL_Rect){x, y, 54, h}, "Redo: %s (Ctrl+Shift+Z)", redo_stack[nredo - 1].label[0] ? redo_stack[nredo - 1].label : "what was undone");
    if (button2((SDL_Rect){x, y, 54, h}, "Redo", 0, nredo > 0)) undo_redo(1);
    x += 66;
    line(x - 6, y + 4, x - 6, y + h - 4, sep);
    tip((SDL_Rect){x, y, 72, h}, P.playing ? "Stop playing (Space)" : "Play from the playhead (Space)");
    if (button((SDL_Rect){x, y, 72, h}, P.playing ? "Stop" : "Play", P.playing)) toggle_play();
    x += 76;
    tip((SDL_Rect){x, y, 62, h}, "Loop: play the loop region over and over (L). Right-drag in the ruler to set it");
    if (button((SDL_Rect){x, y, 62, h}, "Loop", P.loop)) toggle_loop();
    x += 72;
    line(x - 6, y + 4, x - 6, y + h - 4, sep);
    bpm_rect = (SDL_Rect){x, y, 100, h};
    {                                           /* the tempo and time signature where the playhead is */
        int k = change_at(P.playhead);
        double *bp = k >= 0 ? &P.changes[k].bpm : &P.bpm, before = *bp;
        number(bpm_rect, k >= 0 ? "BPM*" : "BPM", bp, 40, 300, 0.25, "%.0f");
        if (bp == &P.bpm && !P.nchanges) tempo_touch(before);   /* selected clips follow a one-tempo song */
        x += 104;
        int *pn = k >= 0 ? &P.changes[k].num : &P.ts_num, *pd = k >= 0 ? &P.changes[k].den : &P.ts_den;
        static int tsel;
        if (!(dd.open && dd.sel == &tsel)) {
            tsel = -1;
            for (int i = 0; i < NTS; i++)
                if (TS_NUM[i] == *pn && TS_DEN[i] == *pd) tsel = i;
        }
        ts_rect = (SDL_Rect){x, y, 64, h};
        tip(ts_rect, "Time signature here: beats in a bar (3/4, 6/8...). Add a change mid-song from the metronome menu");
        static char other[16];
        const char *items[NTS];
        for (int i = 0; i < NTS; i++) items[i] = TS_NAMES[i];
        if (tsel < 0) {                         /* an unusual one from a MIDI file: show it as it is */
            snprintf(other, sizeof other, "%d/%d", *pn, *pd);
            fill(ts_rect, PANEL2);
            text_center(ts_rect, other, TEXT);
            if (hit(ts_rect) && lpress) tsel = 2;
        }
        if (tsel >= 0 && dropdown(ts_rect, "", items, NTS, &tsel, 0)) {
            LOCK();
            *pn = TS_NUM[tsel];
            *pd = TS_DEN[tsel];
            UNLOCK();
        }
        x += 68;
    }
    a4_rect = (SDL_Rect){x, y, 116, h};
    number(a4_rect, "A4", &P.a4, 415, 466, 0.05, "%.1f Hz");   /* tuning for new sounds */
    x += 120;
    snap_rect = (SDL_Rect){x, y, 110, h};
    tip(snap_rect, "Snap: the grid clips and notes jump to. Click for the next one, the arrow for the list; hold Shift while dragging to ignore it");
    dropdown(snap_rect, "Snap ", SNAP_NAMES, 6, &snap_i, 1);   /* click: next, arrow: list */
    x += 118;
    tip((SDL_Rect){x, y, 62, h}, "The metronome click (M)");
    if (button((SDL_Rect){x, y, 62, h}, "Click", MET.on)) {
        MET.on = !MET.on;
        settings_save();
    }
    met_btn = (SDL_Rect){x + 64, y, 22, h};
    tip(met_btn, "Metronome and tempo: the click's sound, count-in, tap tempo, and tempo or time signature changes");
    if (button(met_btn, "", met_open)) met_open = !met_open;
    for (int i = 0; i < 4; i++)                     /* a small down arrow */
        line(met_btn.x + 7 + i, y + 13 + i, met_btn.x + 15 - i, y + 13 + i, btn_fg(met_open));
    x += 92;
    {                                               /* the beat: four lights, the first is the bar */
        int num;                                /* one light a beat of the time signature (up to 6) */
        long bs, bl;
        meter_at(&P, P.playhead, NULL, &num, NULL, NULL, &bs, &bl);
        double unit = (double)bl / num;
        double bt = countin_left > 0 ? fmod((double)(countin_total - countin_left), (double)bl) / unit
                                     : (double)(P.playhead - bs) / unit;
        int beat = (int)floor(bt), lit = P.playing && bt - floor(bt) < 0.35, shown = num < 6 ? num : 6;
        for (int i = 0; i < shown; i++) {
            SDL_Rect d = {x + i * 10, y + 11, 7, 8};
            fill(d, lit && (num <= 6 ? i == beat : i == beat * 6 / num) ? (i == 0 ? ACCENT : HEAD) : PANEL3);
        }
        if (countin_left > 0) {
            snprintf(buf, sizeof buf, "%d", (int)((countin_left - 1) / unit) + 1);
            text(x + 62, y + (h - font_h) / 2, buf, ACCENT);
        }
    }
    x += 70;
    line(x - 6, y + 4, x - 6, y + h - 4, sep);

    double secs = (double)P.playhead / SR;
    pos_text(P.playhead, buf, sizeof buf);
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

float auto_y_to_v(SDL_Rect r, int y)
{
    float v = 1 - (float)(y - r.y - 6) / (float)(r.h - 12);
    return v < 0 ? 0 : v > 1 ? 1 : v;
}

int auto_v_to_y(SDL_Rect r, float v) { return r.y + 6 + (int)lroundf((1 - v) * (r.h - 12)); }

void auto_text(int param, float v, char *buf, size_t n)
{
    float x = auto_to_value(param, v);
    if (param == AUTO_PAN) pan_text(x, buf, n);
    else if (param == AUTO_CUTOFF) snprintf(buf, n, x >= 1000 ? "%.1f kHz" : "%.0f Hz", x >= 1000 ? x / 1000 : x);
    else snprintf(buf, n, "%.0f%%", x * 100);
}

float track_base(int t, int param)       /* what the track is set to without automation */
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

void auto_status(int i)
{
    if (i < 0 || i >= P.nautos) return;
    const AutoPoint *a = &P.autos[i];
    char v[32];
    auto_text(a->param, a->v, v, sizeof v);
    char pos[48];
    pos_text(a->at, pos, sizeof pos);
    status("%s: %s %s at %s", P.tracks[a->track].name, AUTO_NAMES[a->param], v, pos);
}

int auto_add(int t, int param, long at, float v)   /* returns the new point's index */
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

void auto_delete(int i)
{
    LOCK();
    memmove(&P.autos[i], &P.autos[i + 1], (size_t)(P.nautos - i - 1) * sizeof(AutoPoint));
    P.nautos--;
    UNLOCK();
}

void auto_clear(int t, int param)
{
    int first, n = auto_lane(&P, t, param, &first);
    LOCK();
    memmove(&P.autos[first], &P.autos[first + n], (size_t)(P.nautos - first - n) * sizeof(AutoPoint));
    P.nautos -= n;
    UNLOCK();
}

int auto_point_at(int t, int param)       /* the point under the mouse, or -1 */
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
void auto_header(int t, int y)
{
    SDL_Rect hr = {L.left, y, L.head, L.auto_h};
    fill(hr, tone(PANEL, theme_light ? 0.96f : 0.88f));
    fill((SDL_Rect){L.left, y, 4, L.auto_h}, wash(TRACK_COL[t], 0.55f));
    line(L.left, y + L.auto_h - 1, L.left + L.head, y + L.auto_h - 1, LINE);
    static char names[MAX_TRACKS][AUTO_PARAMS][40];
    static const char *ptr[MAX_TRACKS][AUTO_PARAMS];
    for (int p = 0; p < AUTO_PARAMS; p++) {
        int np = auto_lane(&P, t, p, NULL);
        const char *pn = p >= AUTO_MACRO1 && P.tracks[t].macro[p - AUTO_MACRO1].name[0]
                             ? P.tracks[t].macro[p - AUTO_MACRO1].name : AUTO_NAMES[p];   /* a macro by its name */
        if (np) snprintf(names[t][p], sizeof names[t][p], "%s (%d)", pn, np);
        else snprintf(names[t][p], sizeof names[t][p], "%s", pn);
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

void auto_lane_draw(int t)
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

int black_key(int note) { return (0x54A >> (note % 12)) & 1; }

/* A drum clip's hits: a dot per hit, a row per sound, repeating as the pattern does */
void draw_clip_drums(const Clip *c, SDL_Rect r, int body_y, int body_h, Col col)
{
    (void)r;
    const DrumPat *d = drum_find_c(&P, -c->pattern);
    if (!d || d->nrows < 1 || body_h < 4) return;
    long origin = c->start - c->offset;
    double b0 = meter_beats(&P, origin), bs = meter_beats(&P, c->start), be = meter_beats(&P, c->start + c->length);
    long k0 = (long)floor((bs - b0) * 4), k1 = (long)ceil((be - b0) * 4);
    double px_per_step = (frame_to_x(c->start + c->length) - frame_to_x(c->start)) / ((be - bs) * 4 + 1e-9);
    int rh = body_h / d->nrows, dw = px_per_step >= 4 ? (int)px_per_step - 1 : 2;
    if (rh < 1) rh = 1;
    if (dw > 10) dw = 10;
    for (long k = k0 < 0 ? 0 : k0; k <= k1; k++) {
        long f = meter_beats_frame(&P, b0 + drum_step_beat(d, k));
        if (f < c->start || f >= c->start + c->length) continue;
        int x = (int)frame_to_x(f);
        if (x + dw < L.tl_x || x > W) continue;
        int st = (int)(k % d->steps);
        for (int rr = 0; rr < d->nrows; rr++) {
            int v = d->vel[rr][st];
            if (!v) continue;
            int hh = rh - 2 > 1 ? rh - 2 : 1;
            Col cc = d->prob[rr][st] < 100 ? alpha(col, 140) : col;
            fill((SDL_Rect){x, body_y + rr * rh + 1 + (hh - (hh * v / 127 > 1 ? hh * v / 127 : 1)), dw,
                            hh * v / 127 > 1 ? hh * v / 127 : 1}, cc);
        }
    }
}

void draw_clip_notes(const Clip *c, SDL_Rect r, int body_y, int body_h, Col col)
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

int note_add(int pattern, int note, long start, long len, float vel)
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

void note_delete(int i)
{
    LOCK();
    memmove(&P.notes[i], &P.notes[i + 1], (size_t)(P.nnotes - i - 1) * sizeof(Note));
    P.nnotes--;
    UNLOCK();
    memmove(&nsel[i], &nsel[i + 1], (size_t)(P.nnotes - i));
    nsel[P.nnotes] = 0;
}
