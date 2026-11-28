/* tinydaw: fonts and main(). */
#include "ui.h"

/* ---------------------------------------------------------------- main */

TTF_Font *open_font(int size)
{
    char beside[1100] = "";                  /* a font shipped next to the program (Windows) */
    char *exe_dir = SDL_GetBasePath();
    if (exe_dir) {
        snprintf(beside, sizeof beside, "%sDejaVuSansMono.ttf", exe_dir);
        SDL_free(exe_dir);
    }
    const char *paths[] = {getenv("TINYDAW_FONT"), beside,
#ifdef _WIN32
                           "C:/Windows/Fonts/consola.ttf", "C:/Windows/Fonts/cour.ttf",
#endif
                           "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
                           "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                           "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
                           "/usr/share/fonts/noto/NotoSansMono-Regular.ttf",
                           "/usr/share/fonts/liberation/LiberationMono-Regular.ttf"};
    for (size_t i = 0; i < sizeof paths / sizeof *paths; i++) {
        if (!paths[i]) continue;
        TTF_Font *f = TTF_OpenFont(paths[i], size);
        if (f) return f;
    }
    return NULL;
}

int main(int argc, char **argv)
{
    theme_apply(0);
#ifdef _WIN32
    /* a windowed program has no console; borrow the one it was started from, so --help,
     * --selftest and --export can print */
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    int redirected = out && out != INVALID_HANDLE_VALUE && GetFileType(out) != FILE_TYPE_UNKNOWN;
    if (!redirected && AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
#endif
    const char *export_path = NULL, *shot = NULL, *save_path = NULL;
    int demo = 0, test = 0, format_given = 0;
    const char *beat_arg = NULL;
    int has_parts = 0, want_scenes = 0;
    project_init(&P);
    export_defaults(&EXO);

    static const char *usage =
        "usage: tinydaw [options] [project.tdaw] [audio files...]\n"
        "  --demo                 start with a two-bar demo beat\n"
        "  --beat GENRE           add a 4-bar beat (house, trap, rock, ... or a number)\n"
        "  --part SPEC            write a part from the chord track, like\n"
        "                         \"bass,style=Walking,busy=0.5,seed=3,sound=Upright bass,bars=5-13,track=Bass\"\n"
        "                         (chords, bass or melody; also octave=-1..1, gain=dB, bend=vibrato...)\n"
        "  --fills SPEC           drum fills, like \"style=Build,len=0.6,where=sections,track=Drums\"\n"
        "  --scenes               fill the session view: a scene for each section\n"
        "                         (where: sections, 4 or 8 bars)\n"
        "  --project-rate HZ      engine rate: 44100 48000 88200 96000 176400 192000\n"
        "  --version              print the version\n"
        "  --save FILE.tdaw       save the project (after --demo, --beat, files) and exit\n"
        "  --export FILE          render without a window (format from the extension)\n"
        "    --format F           wav aiff flac ogg opus mp3\n"
        "    --bits N             16, 24 or 32 (float)\n"
        "    --rate HZ            export sample rate (default: project rate)\n"
        "    --quality Q          lossy quality 0..1\n"
        "    --mono --stems --loop --normalize --no-dither\n";
    /* options that take a value */
    static const char *with_value[] = {"--save", "--export", "--screenshot", "--format", "--bits", "--rate",
                                       "--quality", "--project-rate", "--beat", "--part", "--fills"};
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        int takes = 0;
        for (size_t k = 0; k < sizeof with_value / sizeof *with_value; k++) takes |= !strcmp(a, with_value[k]);
        if (takes && !v) {
            fprintf(stderr, "tinydaw: %s needs a value\n%s", a, usage);
            return 2;
        }
        if (!strcmp(a, "--demo")) demo = 1;
        else if (!strcmp(a, "--selftest")) test = 1;
        else if (!strcmp(a, "--beat")) beat_arg = v;
        else if (!strcmp(a, "--part") || !strcmp(a, "--fills")) has_parts = 1;
        else if (!strcmp(a, "--scenes")) want_scenes = 1;
        else if (!strcmp(a, "--export")) export_path = v;
        else if (!strcmp(a, "--save")) save_path = v;
        else if (!strcmp(a, "--screenshot")) shot = v;
        else if (!strcmp(a, "--bits")) EXO.bits = atoi(v);
        else if (!strcmp(a, "--rate")) EXO.rate = atoi(v);
        else if (!strcmp(a, "--quality")) EXO.quality = atof(v);
        else if (!strcmp(a, "--mono")) EXO.mono = 1;
        else if (!strcmp(a, "--stems")) EXO.stems = 1;
        else if (!strcmp(a, "--loop")) EXO.loop_only = 1;
        else if (!strcmp(a, "--normalize")) EXO.normalize = 1;
        else if (!strcmp(a, "--no-dither")) EXO.dither = 0;
        else if (!strcmp(a, "--project-rate")) {
            int r = atoi(v);
            if (r < 8000 || r > 384000) {
                fprintf(stderr, "tinydaw: bad project rate %s\n", v);
                return 2;
            }
            g_rate = r;
        } else if (!strcmp(a, "--format")) {
            int found = -1;
            for (int k = 0; k < EXP_COUNT; k++)
                if (!strcasecmp(v, EXPORT_FORMATS[k].ext) || !strcasecmp(v, EXPORT_FORMATS[k].name)) found = k;
            if (found < 0) {
                fprintf(stderr, "tinydaw: unknown format %s\n", v);
                return 2;
            }
            EXO.format = found;
            format_given = 1;
        } else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            printf("tinydaw %s\n%s", TINYDAW_VERSION, usage);
            return 0;
        } else if (!strcmp(a, "--version")) {
            printf("tinydaw %s\n", TINYDAW_VERSION);
            return 0;
        } else if (a[0] == '-') {
            fprintf(stderr, "tinydaw: unknown option %s\n%s", a, usage);
            return 2;
        }
        if (takes) i++;
    }
    if (EXO.bits != 16 && EXO.bits != 24 && EXO.bits != 32) {
        fprintf(stderr, "tinydaw: --bits must be 16, 24 or 32\n");
        return 2;
    }
    if (test || shot) headless = 1;
    int loaded = 0;
    for (int i = 1; i < argc && !loaded; i++) {   /* a project file opens first */
        size_t n = strlen(argv[i]);
        if (n > 5 && !strcmp(argv[i] + n - 5, ".tdaw") && (i == 1 || argv[i - 1][0] != '-' ||
                                                           !strcmp(argv[i - 1], "--demo"))) {
            if (open_project(argv[i], 0) != 0) {
                fprintf(stderr, "tinydaw: %s\n", status_msg);
                return 1;
            }
            loaded = 1;
        }
    }
    if (demo) make_demo();
    if (beat_arg) {                     /* genre by name (case-insensitive prefix) or number */
        int g = -1;
        for (int k = 0; k < NGENRES && g < 0; k++)
            if (!strncasecmp(beat_arg, GENRES[k].name, strlen(beat_arg))) g = k;
        if (g < 0 && beat_arg[0] >= '0' && beat_arg[0] <= '9' && atoi(beat_arg) < NGENRES) g = atoi(beat_arg);
        if (g < 0) {
            fprintf(stderr, "tinydaw: unknown genre %s\n", beat_arg);
            return 2;
        }
        th_root = GENRES[g].root;           /* the genre's key, tuning and tempo */
        th_scale = GENRES[g].scale;
        P.a4 = GENRES[g].a4;
        gn_sel = g;
        place_beat(g, 0, -1);
        if (export_path) fprintf(stderr, "%s\n", status_msg);
    }
    for (int i = 1; has_parts && i + 1 < argc; i++)   /* parts and fills, in the order given */
        if (!strcmp(argv[i], "--part") || !strcmp(argv[i], "--fills")) {
            if (cli_spec(argv[i + 1], !strcmp(argv[i], "--fills")) != 0) return 1;
            i++;
        }
    if (want_scenes) {                  /* the session view: a scene for each section */
        if (!P.nsections) {
            fprintf(stderr, "tinydaw: --scenes: the project has no sections to make scenes from\n");
            return 1;
        }
        P.nscenes = 0;
        sess_from_sections();
        printf("%s\n", status_msg);
    }
    for (int i = 1; i < argc; i++) {
        int takes = 0;
        for (size_t k = 0; k < sizeof with_value / sizeof *with_value; k++) takes |= !strcmp(argv[i], with_value[k]);
        if (takes) {
            i++;
            continue;
        }
        if (argv[i][0] == '-') continue;
        size_t len = strlen(argv[i]);
        if (len > 5 && !strcmp(argv[i] + len - 5, ".tdaw")) continue;
        import_file(argv[i]);
        if (export_path || shot || test) fprintf(stderr, "%s\n", status_msg);
    }

    saved_enabled = !export_path && !shot && !test;
    saved_sync();
    history_reset();
    if (!loaded) mark_saved();
    if (save_path) {
        char msg[1200];
        int rc = project_save(&P, save_path, msg, sizeof msg);
        fprintf(rc ? stderr : stdout, "%s\n", msg);
        if (rc || !export_path) return rc ? 1 : 0;
    }
    if (export_path) {                  /* headless render, no window */
        char base[1024], msg[1500];
        snprintf(base, sizeof base, "%s", export_path);
        const char *after = path_base(base);
        char *dot = strrchr(base, '.'), *slash = after > base ? (char *)after - 1 : NULL;
        if (dot && (!slash || dot > slash)) {
            int ext_fmt = -1;
            for (int k = 0; k < EXP_COUNT; k++)
                if (!strcasecmp(dot + 1, EXPORT_FORMATS[k].ext)) ext_fmt = k;
            if (!format_given && ext_fmt >= 0) EXO.format = ext_fmt;
            if (ext_fmt >= 0) *dot = 0;     /* the extension is added back for the format */
        }
        bends_update();
        if (project_export(&P, &EXO, base, msg, sizeof msg) != 0) {
            fprintf(stderr, "tinydaw: %s\n", msg);
            return 1;
        }
        printf("%s\n", msg);
        return 0;
    }

    if (test) shot = "";
    if (shot) {
        SDL_setenv("SDL_VIDEODRIVER", "offscreen", 1);
        ui_offscreen = 1;
        SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    }
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");   /* play pads with the window behind */
    SDL_SetHint("SDL_WINDOWS_DPI_AWARENESS", "permonitorv2");   /* tinydaw scales itself: never blurry */
    SDL_SetHint("SDL_WINDOWS_DPI_SCALING", "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "tinydaw: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (TTF_Init() != 0 || !(font = open_font(13))) {
        fprintf(stderr, "tinydaw: no usable font (set TINYDAW_FONT to a .ttf file)\n");
        return 1;
    }
    font_h = TTF_FontHeight(font);
    win = SDL_CreateWindow("tinydaw", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H,
                           SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) {
        fprintf(stderr, "tinydaw: window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(win, 900, 560);
    Uint32 flags = shot ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC;
    ren = SDL_CreateRenderer(win, -1, flags);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) {
        fprintf(stderr, "tinydaw: renderer: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    if (getenv("TINYDAW_SHOT_SCALE")) ui_choice = (int)lround(atof(getenv("TINYDAW_SHOT_SCALE")) * 100);
    ui_apply();                                  /* the interface's size, and the font at it */
    ui_fit_window();
    if (shot && getenv("TINYDAW_SHOT_SIZE")) {   /* "WxH" in logical units: e.g. the smallest window */
        int sw = 0, sh = 0;
        if (sscanf(getenv("TINYDAW_SHOT_SIZE"), "%dx%d", &sw, &sh) == 2 && sw > 200 && sh > 200)
            SDL_SetWindowSize(win, (int)(sw * ui_user), (int)(sh * ui_user));
    }
    cur_arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    cur_resize = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
    cur_hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEALL);
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

    open_audio();
    if (!status_msg[0]) status("Generate a sound or drop audio files on the window, then drag them onto a track.");
    if (headless) {
        binds_default();
    } else {
        settings_load();
        ui_apply();                              /* the saved interface size */
        ui_fit_window();
        if (theme_choice == THEME_SYSTEM) theme_follow();
        else theme_apply(theme_choice);
        if (audio_out[0]) open_audio();          /* the saved output device */
        metronome_prepare();
        open_panel(last_panel);
        sounds_open = last_panel == RI_SOUNDS;
        binds_load();
        midi_ok = midi_start(midi_in, NULL) == 0;
        if (getenv("TINYDAW_DEBUG_INPUT")) {
            char names[MIDI_MAX_DEVICES][64];
            int n = midi_ok ? midi_devices(names, MIDI_MAX_DEVICES) : 0;
            fprintf(stderr, "midi: %s, %d device%s\n", midi_ok ? "listening" : midi_error(), n, n == 1 ? "" : "s");
            for (int i = 0; i < n; i++) fprintf(stderr, "  midi device %d: %s\n", i, names[i]);
        }
        if (!loaded && autosave_exists())
            status("Unsaved work from last time was kept: File > Recover unsaved work");
    }

    if (test) {
        layout();
        int rc = selftest();
        SDL_Quit();
        return rc;
    }
    if (shot) {                         /* render offscreen and save, for testing */
        layout();
        P.playhead = (long)(1.5 * beat_frames());
        selected = P.nclips > 0 ? P.nclips - 1 : -1;
        if (getenv("TINYDAW_SHOT_EXPORT")) open_export();   /* screenshot the dialog */
        if (getenv("TINYDAW_SHOT_PADS")) {
            pads_open = 1;
            for (int i = 0; i < NPADS && i < P.nsamples; i++) P.pads[i].sample = i;
            pad_flash[5] = SDL_GetTicks();
            pads_scroll = atoi(getenv("TINYDAW_SHOT_PADS"));
            pads_content_h = 2000;
        }
        if (getenv("TINYDAW_SHOT_MENU")) menu_open = 1;
        if (getenv("TINYDAW_SHOT_LIGHT")) theme_apply(atoi(getenv("TINYDAW_SHOT_LIGHT")));   /* 1 light, 3-5 the others */
        if (getenv("TINYDAW_SHOT_AUTO")) {       /* a volume swell on track 1, a filter sweep on track 4 */
            long bar = 4 * beat_frames();
            auto_add(0, AUTO_VOLUME, 0, 0.1f);
            auto_add(0, AUTO_VOLUME, 2 * bar, 0.67f);
            auto_add(0, AUTO_VOLUME, 3 * bar, 0.67f);
            auto_add(0, AUTO_VOLUME, 3 * bar + bar / 2, 0.3f);
            auto_add(3, AUTO_CUTOFF, 0, 0.25f);
            auto_add(3, AUTO_CUTOFF, 4 * bar, 0.95f);
            auto_open[0] = auto_open[3] = 1;
            auto_param[3] = AUTO_CUTOFF;
        }
        if (getenv("TINYDAW_SHOT_ROLL")) {       /* a little melody in a new note clip, in the roll */
            layout();
            int c = new_note_clip(P.ntracks - 1, 0);
            static const int mel[][3] = {{60, 0, 2}, {64, 2, 2}, {67, 4, 2}, {72, 6, 4}, {71, 10, 2}, {67, 12, 2},
                                         {69, 14, 4}, {65, 18, 2}, {64, 20, 2}, {62, 22, 2}, {60, 24, 8}};
            long e = beat_frames() / 2;
            for (int i = 0; c >= 0 && i < 11; i++) {
                int j = note_add(P.clips[c].pattern, mel[i][0], mel[i][1] * e, mel[i][2] * e, 0.45f + 0.05f * i);
                if (i == 3 || i == 4) nsel[j] = 1;
            }
            if (c >= 0) roll_fit_clip();
            if (c >= 0) roll_open(c), nsel[3] = nsel[4] = 0;
        }
        if (getenv("TINYDAW_SHOT_SCENE")) {      /* "k,seconds": the show on scene k, that far in */
            int k = 0;
            double at = 12;
            sscanf(getenv("TINYDAW_SHOT_SCENE"), "%d,%lf", &k, &at);
            show_open = 1;
            show_cycle = 0;
            show_scene = k;
            show_clock = at;
            show_started[k] = 0;
        }
        if (getenv("TINYDAW_SHOT_FIT")) {
            for (int i = 0; i < RI_COUNT; i++) *RAIL_FLAG[i] = 0;
            layout();
            zoom_to_fit();
        }
        if (getenv("TINYDAW_SHOT_TYPE")) {      /* the BPM box being typed into */
            numedit_begin(&P.bpm, 40, 300);
            numedit_text("12");
        }
        if (getenv("TINYDAW_SHOT_CHORD") && P.nchords) {   /* the chord picker over chord n */
            chp_idx = atoi(getenv("TINYDAW_SHOT_CHORD")) % P.nchords;
            chd_sel = chp_idx;
            chp_open = 1;
        }
        if (getenv("TINYDAW_SHOT_BROWSE")) {     /* the project browser on tab n (2: the examples) */
            browse_tab = atoi(getenv("TINYDAW_SHOT_BROWSE"));
            browse_show();
            browse_tab = atoi(getenv("TINYDAW_SHOT_BROWSE"));
            browse_scan();
        }
        if (getenv("TINYDAW_SHOT_SESSION")) {    /* scenes from sections, slots playing: 1 the session view, 2 the timeline */
            session_view = atoi(getenv("TINYDAW_SHOT_SESSION")) != 2;
            for (int i = 0; i < RI_COUNT; i++) *RAIL_FLAG[i] = 0;
            if (!P.nscenes) sess_from_sections();
            P.playing = 1;
            for (int t = 0; t < P.ntracks && t < 3; t++)
                if (P.nscenes > 2 && P.slots[t][2].kind) P.sess_play[t] = 2, P.sess_at[t] = 0;
            if (P.nscenes > 1 && P.slots[3][1].kind) P.sess_q[3] = 1, P.sess_q_at[3] = meter_bar_frame(&P, 1);
            P.playhead = meter_bar_frame(&P, 0.6);
        }
        if (getenv("TINYDAW_SHOT_MET")) {
            met_open = 1;
            MET.on = 1;
        }
        if (getenv("TINYDAW_SHOT_PANEL")) {      /* "index[:sub]", or -1 for none */
            int pn = -1, sub = 0;
            sscanf(getenv("TINYDAW_SHOT_PANEL"), "%d:%d", &pn, &sub);
            for (int i = 0; i < RI_COUNT; i++) *RAIL_FLAG[i] = 0;
            if (pn >= 0 && pn < RI_COUNT) open_panel(pn);
            int *subs[RI_COUNT] = {&sounds_sub, &theory_tab, &pads_sub, &drum_mode, &mixer_sub, &vis_sub, &settings_sub};
            if (pn >= 0 && pn < RI_COUNT) *subs[pn] = sub;
            if (pn == RI_THEORY) {                 /* show the badge as if three sounds were added */
                lib_badge = 3;
                lib_badge_at = 0;
            }
        }
        if (getenv("TINYDAW_SHOT_DRUMS")) {      /* "style,steps,mode": a pattern from a style, on the timeline */
            int g = 0, st = 16, md = 0;
            sscanf(getenv("TINYDAW_SHOT_DRUMS"), "%d,%d,%d", &g, &st, &md);
            sel_track = P.ntracks > 1 ? 1 : 0;
            DrumPat *d = drum_from_style(sel_track, g);
            if (d) {
                if (st == 32 && d->steps == 16)
                    for (int r = 0; r < d->nrows; r++) memcpy(d->vel[r] + 16, d->vel[r], 16);
                d->steps = st == 32 ? 32 : d->steps;
                d->prob[d->nrows - 1][2] = 50;
                d->vel[0][6] = 60;
                drums_show(d->id);
                drum_mode = md;
                LOCK();
                P.playhead = 0;
                UNLOCK();
                drum_place(d, 2);
            }
        }
        if (getenv("TINYDAW_SHOT_DRUMPAT")) {    /* a song's own drum pattern (its id) in the Drums panel */
            const DrumPat *d = drum_find_c(&P, atoi(getenv("TINYDAW_SHOT_DRUMPAT")));
            if (d) {
                sel_track = d->track;
                drums_show(d->id);
            }
        }
        if (getenv("TINYDAW_SHOT_TIP")) {        /* "x,y": the mouse held still there, its tooltip showing */
            int tx = 0, ty = 0;
            sscanf(getenv("TINYDAW_SHOT_TIP"), "%d,%d", &tx, &ty);
            mx = tx;
            my = ty;
            frame();
            tip_seen.since = SDL_GetTicks() - 2000;
        }
        if (getenv("TINYDAW_SHOT_METER")) {      /* a song in 4/4, then 3/4 slower, then 6/8 faster */
            P.changes[0] = (MeterChange){2, 96, 3, 4};
            P.changes[1] = (MeterChange){4, 140, 6, 8};
            P.nchanges = 2;
            pps = 90;
            view_start = 0;
            P.playhead = meter_bar_frame(&P, 2.5);
        }
        if (getenv("TINYDAW_SHOT_SING")) {       /* the Singer, with words typed */
            show_panel(RI_SOUNDS);
            sounds_sub = 2;
            sv_defaults();
            snprintf(SV.words, sizeof SV.words, "%s", getenv("TINYDAW_SHOT_SING"));
            sounds_scroll = getenv("TINYDAW_SHOT_SCROLL") ? atoi(getenv("TINYDAW_SHOT_SCROLL")) : 0;
            if (getenv("TINYDAW_SHOT_BEND")) {   /* a bend preset drawn in (0 scoops, 1 slides, 2 fall-offs) */
                char syl[VOCAL_NOTES][40];
                int ns = vocal_syllables(SV.words, syl, NULL, VOCAL_NOTES);
                sv_make_notes();
                sv_bend_preset(atoi(getenv("TINYDAW_SHOT_BEND")), ns < SV.nnotes ? ns : SV.nnotes);
            }
        }
        if (getenv("TINYDAW_SHOT_GEN")) {        /* "genre,kind,preset,scroll": the Generator */
            int ge = -1, ki = -1, pr = -1, sc = 0;
            sscanf(getenv("TINYDAW_SHOT_GEN"), "%d,%d,%d,%d", &ge, &ki, &pr, &sc);
            show_panel(RI_SOUNDS);
            sounds_sub = 1;
            gx_genre = ge;
            gx_kind = ki;
            if (pr >= 0 && pr < NSYNTH_PRESETS) synth_from_preset(&GX, gx_preset = pr);
            sounds_scroll = sc;
        }
        if (getenv("TINYDAW_SHOT_VIS") || getenv("TINYDAW_SHOT_SHOW")) {   /* fill the meters with the demo */
            vis_open = getenv("TINYDAW_SHOT_VIS") != NULL;
            show_open = getenv("TINYDAW_SHOT_SHOW") != NULL;
            P.playing = 1;
            long target = (long)(atof(getenv(show_open ? "TINYDAW_SHOT_SHOW" : "TINYDAW_SHOT_VIS")) * beat_frames());
            for (int f = 0; f < 40; f++) {        /* 40 "frames" of real audio leading up to it */
                P.playhead = target - (39 - f) * 1024;
                float tmp[2 * 1024] = {0};
                LOCK();
                mixer_run(MX, &P, P.playhead - 1024, 1, tmp, 1024, -1, 1);
                for (int i = 0; i < 1024; i++) {
                    vis_ring[2 * vis_pos] = soft_clip(tmp[2 * i] * P.master);
                    vis_ring[2 * vis_pos + 1] = soft_clip(tmp[2 * i + 1] * P.master);
                    vis_pos = (vis_pos + 1) % VIS_RING;
                }
                UNLOCK();
                vis_update();
                if (show_open) show_draw();               /* lets the blobs grow to size */
            }
            P.playing = 0;
            SDL_PauseAudioDevice(dev, 1);
            P.playing = 1;
        }
        if (getenv("TINYDAW_SHOT_MIXER")) {
            mixer_open = 1;
            mixer_scroll = atoi(getenv("TINYDAW_SHOT_MIXER"));
            mixer_content_h = 3000;
            if (getenv("TINYDAW_SHOT_MIXSUB")) mixer_sub = atoi(getenv("TINYDAW_SHOT_MIXSUB"));   /* 4: Buses */
            if (getenv("TINYDAW_SHOT_RACK")) {   /* a bus and a rack to show */
                int gb = bus_add("Drums");
                for (int t = 0; t < 3 && gb >= 0; t++) P.tracks[t].out = gb + 1;
                rack_fx_init(&P.tracks[3].rack.fx[0], FXR_DRIVE);
                rack_fx_init(&P.tracks[3].rack.fx[1], FXR_FILTER);
                rack_fx_init(&P.tracks[3].rack.fx[2], FXR_CHORUS);
                P.tracks[3].rack.n = 3;
                rack_fx_init(&P.buses[gb].rack.fx[0], FXR_COMP);
                P.buses[gb].rack.n = 1;
                sel_bus = gb;
            }
            sel_track = 3;
            for (int i = 0; i < P.nclips; i++)
                if (P.clips[i].track == 3) {
                    selected = i;
                    break;
                }
            P.tracks[3].filter = FILT_LOW;
            P.tracks[3].cutoff = 800;
            P.tracks[3].pan = -0.3f;
            P.tracks[3].reverb_send = 0.25f;
            P.clips[selected].gain_db = -3;
            P.clips[selected].fade_out = P.clips[selected].length / 2;
        }
        if (getenv("TINYDAW_SHOT_CONFIRM")) confirm_act = ACT_QUIT;
        if (getenv("TINYDAW_SHOT_SELECT")) {     /* a few clips selected, a pane scrolled part way */
            sel_clear();
            for (int i = 0; i < P.nclips; i++)
                if (P.clips[i].track == 3 || P.clips[i].track == 4) csel[i] = 1;
            sel_fix_primary();
            mixer_open = 1;
            mixer_scroll = atoi(getenv("TINYDAW_SHOT_SELECT"));
            mixer_content_h = 3000;
        }
        const char *th = getenv("TINYDAW_SHOT_THEORY");      /* "tab,root,scale" */
        const char *gs = getenv("TINYDAW_SHOT_GENRE");
        if (gs) gn_sel = atoi(gs);
        if (th) {
            theory_open = 1;
            sscanf(th, "%d,%d,%d", &theory_tab, &th_root, &th_scale);
        }
        frame();
        if (getenv("TINYDAW_SHOT_CLICK")) {      /* "x,y": click there first (opens a list, a menu...) */
            int cx = 0, cy = 0;
            sscanf(getenv("TINYDAW_SHOT_CLICK"), "%d,%d", &cx, &cy);
            sim(cx, cy, 1, 0, 0, 0);
            sim(cx, cy, 0, 1, 0, 0);
            mx = cx + 40;                        /* hover inside the list */
            my = cy + 60;
        }
        if (getenv("TINYDAW_SHOT_CONSOLE")) {    /* the mixing console window */
            mixwin_open();
            if (pops[POP_MIXER].win) {
                for (int t = 0; t < P.ntracks; t++) track_meter[t].level = track_meter[t].peak = 0.3f + 0.05f * t;
                P.tracks[1].comp_on = 1;
                P.tracks[4].width = 1.6f;
                for (int i = 0; i < 2; i++) mixwin_frame();
                SDL_Renderer *m = ren;
                int ew, eh, sw = W, sh = H, ow, oh;
                ren = pops[POP_MIXER].ren;
                logical_size(ren, &ew, &eh);
                SDL_GetRendererOutputSize(ren, &ow, &oh);
                W = ew;
                H = eh;
                console_draw();
                SDL_Surface *es = SDL_CreateRGBSurfaceWithFormat(0, ow, oh, 32, SDL_PIXELFORMAT_ARGB8888);
                SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, es->pixels, es->pitch);
                int ok = SDL_SaveBMP(es, shot) == 0;
                SDL_FreeSurface(es);
                ren = m;
                W = sw;
                H = sh;
                SDL_Quit();
                return ok ? 0 : 1;
            }
        }
        if (getenv("TINYDAW_SHOT_EDITOR")) {     /* the clip editor window, for clip N (or "lib:N", sound N) */
            const char *ev = getenv("TINYDAW_SHOT_EDITOR");
            if (!strncmp(ev, "lib:", 4)) editor_open_sound(atoi(ev + 4));
            else editor_open(atoi(ev));
            if (ewin) {
                for (int i = 0; i < 2; i++) editor_frame();
                SDL_Renderer *m = ren;           /* draw it once more and keep the pixels */
                int ew, eh, ow, oh;
                ren = eren;
                logical_size(ren, &ew, &eh);
                SDL_GetRendererOutputSize(ren, &ow, &oh);
                int sw = W, sh = H;
                W = ew;
                H = eh;
                editor_draw();
                SDL_Surface *es = SDL_CreateRGBSurfaceWithFormat(0, ow, oh, 32, SDL_PIXELFORMAT_ARGB8888);
                SDL_RenderReadPixels(eren, NULL, SDL_PIXELFORMAT_ARGB8888, es->pixels, es->pitch);
                int ok = SDL_SaveBMP(es, shot) == 0;
                SDL_FreeSurface(es);
                ren = m;
                W = sw;
                H = sh;
                SDL_Quit();
                return ok ? 0 : 1;
            }
        }
        for (int i = 0; i < 2; i++) frame();
        int ow, oh;
        SDL_GetRendererOutputSize(ren, &ow, &oh);
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, ow, oh, 32, SDL_PIXELFORMAT_ARGB8888);
        SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
        int ok = SDL_SaveBMP(s, shot) == 0;
        SDL_FreeSurface(s);
        SDL_Quit();
        return ok ? 0 : 1;
    }

    int running = 1;
    while (running) {
        lpress = lrel = rpress = wheel_x = wheel_y = 0;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT:
                if (SDL_AtomicGet(&fd_state) == 0) request(ACT_QUIT, NULL);
                break;
            case SDL_MOUSEMOTION: {
                int k = pop_of(e.motion.windowID);  /* a pop-out window has its own mouse */
                int lx, ly;
                mouse_logical(&lx, &ly);
                if (k >= 0) {
                    pops[k].in.mx = lx;
                    pops[k].in.my = ly;
                } else {
                    mx = lx;
                    my = ly;
                }
                break;
            }
            case SDL_MOUSEBUTTONDOWN: {
                int k = pop_of(e.button.windowID);
                int lx, ly;
                mouse_logical(&lx, &ly);
                if (k >= 0) {
                    PopIn *in = &pops[k].in;
                    in->mx = lx;
                    in->my = ly;
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        in->lpress = in->ldown = 1;
                        in->ldouble = e.button.clicks >= 2;
                    } else if (e.button.button == SDL_BUTTON_RIGHT) in->rpress = 1;
                    break;
                }
                mx = lx;
                my = ly;
                if (e.button.button == SDL_BUTTON_LEFT) {
                    lpress = ldown = 1;
                    ldouble = e.button.clicks >= 2;
                }
                else if (e.button.button == SDL_BUTTON_RIGHT) rpress = 1;
                break;
            }
            case SDL_MOUSEBUTTONUP:
                if (e.button.button == SDL_BUTTON_LEFT) {   /* to the window that owns the drag, else the one under it */
                    int k = drag.kind != D_NONE ? drag_pop : pop_of(e.button.windowID);
                    if (k >= 0) {
                        pops[k].in.lrel = 1;
                        pops[k].in.ldown = 0;
                    } else {
                        lrel = 1;
                        ldown = 0;
                    }
                }
                break;
            case SDL_MOUSEWHEEL: {
                int k = pop_of(e.wheel.windowID);
                if (k >= 0) {
                    pops[k].in.wheel_y += e.wheel.y;
                    pops[k].in.wheel_x += e.wheel.x;
                    break;
                }
                wheel_y += e.wheel.y;
                wheel_x += e.wheel.x;
                break;
            }
            case SDL_DROPFILE: {
                size_t n = strlen(e.drop.file);
                if (n > 5 && !strcmp(e.drop.file + n - 5, ".tdaw")) request(ACT_OPEN_PATH, e.drop.file);
                else if ((n > 4 && !strcasecmp(e.drop.file + n - 4, ".mid")) || (n > 5 && !strcasecmp(e.drop.file + n - 5, ".midi")))
                    midi_import(e.drop.file);
                else if (!slot_drop_file(e.drop.file)) import_file(e.drop.file);   /* on a slot: into it */
                SDL_free(e.drop.file);
                break;
            }
            case SDL_TEXTINPUT:
                numedit_text(e.text.text);
                tf_insert(e.text.text);
                break;
            case SDL_KEYDOWN:
                if (numedit_key(e.key.keysym)) break;      /* typing a value */
                if (tf_key(e.key.keysym)) break;           /* typing a name */
                if (pop_of(e.key.windowID) >= 0) {      /* a pop-out: Esc closes it, the rest as usual */
                    if (e.key.keysym.sym == SDLK_ESCAPE) {
                        if (pop_of(e.key.windowID) == POP_EDITOR) editor_close();
                        else pop_close(pop_of(e.key.windowID));
                        break;
                    }
                    key(e.key.keysym);
                    break;
                }
                if (pads_take_key(e.key.keysym) && e.key.keysym.sym != SDLK_ESCAPE) {
                    int sc = e.key.keysym.scancode, bound = 0;
                    for (int i = 0; i < NPADS; i++) bound |= binds[i].key == sc;
                    if (learn_pad >= 0 || bound) {          /* the pads have this key */
                        if (!e.key.repeat) input_event(SRC_KEY, sc, 110, 1, -1);
                        break;
                    }
                }
                key(e.key.keysym);
                break;
            case SDL_KEYUP:
                if (pads_take_key(e.key.keysym)) input_event(SRC_KEY, e.key.keysym.scancode, 0, 0, -1);
                break;
            case SDL_WINDOWEVENT:               /* the desktop theme may have changed meanwhile */
                if (e.window.event == SDL_WINDOWEVENT_CLOSE) {   /* with two windows, there's no SDL_QUIT */
                    int k = pop_of(e.window.windowID);
                    if (k == POP_EDITOR) editor_close();
                    else if (k >= 0) pop_close(k);
                    else if (SDL_AtomicGet(&fd_state) == 0) request(ACT_QUIT, NULL);
                    break;
                }
                if (e.window.event == SDL_WINDOWEVENT_LEAVE && pop_of(e.window.windowID) >= 0)
                    pops[pop_of(e.window.windowID)].in.mx = pops[pop_of(e.window.windowID)].in.my = -1000;
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) theme_follow();
                break;
            case SDL_JOYDEVICEADDED: joy_added(e.jdevice.which); break;
            case SDL_JOYDEVICEREMOVED: joy_removed(e.jdevice.which); break;
            case SDL_JOYBUTTONDOWN:
            case SDL_JOYBUTTONUP:
            case SDL_JOYHATMOTION:
            case SDL_JOYAXISMOTION:
                joy_event(&e);
                break;
            }
        }
        mods = SDL_GetModState();
        if (lrel && drag.kind != D_NONE && !drag_in_editor) update_drag();   /* final position */
        if (drag_pop >= 0 && pops[drag_pop].in.lrel && drag.kind != D_NONE) {   /* a pop-out's, likewise */
            int sx = mx, sy = my;
            mx = pops[drag_pop].in.mx;
            my = pops[drag_pop].in.my;
            update_drag();
            mx = sx;
            my = sy;
        }
        frame();
        SDL_RenderPresent(ren);
        editor_frame();
        mixwin_frame();
        if (quit_now) running = 0;
        if (!(flags & SDL_RENDERER_PRESENTVSYNC)) SDL_Delay(8);
    }

    midi_stop();
    if (!is_dirty()) remove(autosave_file());   /* a clean exit leaves nothing to recover */
    if (dev) SDL_CloseAudioDevice(dev);
    SDL_Quit();
    return 0;
}
