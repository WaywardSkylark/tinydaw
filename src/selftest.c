/* tinydaw: the self-test. */
#include "ui.h"

int gn_split;
char gn_pat[32][DRUM_LANES][33];
SDL_Rect gn_handle, gn_cell[DRUM_LANES][32];

/* Feed one synthetic mouse step through the same path as real input. */
void sim(int x, int y, int press, int release, int right, SDL_Keymod m)
{
    mx = x;
    my = y;
    lpress = press;
    lrel = release;
    rpress = right;
    wheel_x = wheel_y = 0;
    if (press) ldown = 1;
    if (release) ldown = 0;
    mods = m;
    if (lrel && drag.kind != D_NONE) update_drag();
    frame();
}

void sim_drag(int x0, int y0, int x1, int y1, SDL_Keymod m)
{
    sim(x0, y0, 1, 0, 0, m);
    for (int i = 1; i <= 8; i++) sim(x0 + (x1 - x0) * i / 8, y0 + (y1 - y0) * i / 8, 0, 0, 0, m);
    sim(x1, y1, 0, 1, 0, m);
}

/* the clip editor's mouse, for the self-test: one editor frame */
void ed_sim(int x, int y, int press, int release)
{
    ein.mx = x;
    ein.my = y;
    ein.lpress = press;
    ein.lrel = release;
    if (press) ein.ldown = 1;
    if (release) ein.ldown = 0;
    if (release && drag.kind != D_NONE && drag_in_editor) {   /* the final position, as the main loop does */
        int sx = mx, sy = my;
        mx = x;
        my = y;
        update_drag();
        mx = sx;
        my = sy;
    }
    editor_frame();
}

int failures;

void check(int ok, const char *what)
{
    printf("  %s  %s\n", ok ? "pass" : "FAIL", what);
    failures += !ok;
}

int lane_y(int t) { return track_y(t) + L.lane_h / 2; }

int dropdown_probe_snap(int x) { return x >= snap_rect.x && x < snap_rect.x + 4; }

int snap_arrow_x(void) { return snap_rect.x + snap_rect.w - 11; }

float *test_render(Project *p, long a, long n, int only)
{
    if (p == &P) bends_update();
    float *b = calloc((size_t)n * 2, sizeof(float));
    Mixer *m = mixer_new();
    for (long pos = 0; pos < n; pos += 1024) {
        int k = n - pos < 1024 ? (int)(n - pos) : 1024;
        mixer_run(m, p, a + pos, 1, b + 2 * pos, k, only, 0);
    }
    mixer_free(m);
    return b;
}

double test_rms(const float *b, long a, long e, int ch)   /* ch 0 left, 1 right, 2 both */
{
    double sum = 0;
    long n = 0;
    for (long i = a; i < e; i++)
        for (int c = 0; c < 2; c++)
            if (ch == 2 || ch == c) {
                sum += (double)b[2 * i + c] * b[2 * i + c];
                n++;
            }
    return n ? sqrt(sum / n) : 0;
}

int selftest(void)
{
    GenParams g = {WAVE_SINE, 69, 2.0, 0.5, 5, 50, 440};
    sample_generate(&P, &g);
    long bf = beat_frames();
    snap_i = 3;                                  /* 1/4 = one beat */
    frame();

    SDL_Rect item = lib_rects[0];
    sim_drag(item.x + 30, item.y + 10, (int)frame_to_x(2 * bf) + 3, lane_y(2), 0);
    check(P.nclips == 1, "drag from library creates a clip");
    check(P.nclips == 1 && P.clips[0].track == 2, "clip lands on the track under the pointer");
    check(P.nclips == 1 && P.clips[0].start == 2 * bf, "clip start snaps to the beat");
    check(P.nclips == 1 && P.clips[0].length == P.samples[0].frames, "clip plays the whole sound");

    SDL_Rect r = clip_rect(&P.clips[0]);
    int beat_px = (int)(60.0 / P.bpm * pps);
    sim_drag(r.x + r.w / 2, r.y + r.h / 2, r.x + r.w / 2 + beat_px, lane_y(5), 0);
    check(P.clips[0].start == 3 * bf && P.clips[0].track == 5, "drag moves the clip in time and between tracks");

    r = clip_rect(&P.clips[0]);
    sim_drag(r.x + r.w - 2, r.y + r.h / 2, r.x + r.w - 2 - beat_px, r.y + r.h / 2, 0);
    check(P.clips[0].length == bf, "dragging the right edge trims the end");

    r = clip_rect(&P.clips[0]);                  /* clip: start 3, length 1 beat */
    sim_drag(r.x + r.w - 2, r.y + r.h / 2, r.x + r.w - 2 + beat_px, r.y + r.h / 2, 0);
    check(P.clips[0].length == 2 * bf, "dragging the right edge back out restores the end");

    r = clip_rect(&P.clips[0]);
    sim_drag(r.x + 2, r.y + r.h / 2, r.x + 2 + beat_px, r.y + r.h / 2, 0);
    check(P.clips[0].start == 4 * bf && P.clips[0].offset == bf && P.clips[0].length == bf,
          "dragging the left edge trims the start (later start, skips into the sound)");

    r = clip_rect(&P.clips[0]);
    sim_drag(r.x + 2, r.y + r.h / 2, r.x + 2 - 2 * beat_px, r.y + r.h / 2, 0);
    check(P.clips[0].start == 3 * bf && P.clips[0].offset == 0 && P.clips[0].length == 2 * bf,
          "left edge stops at the beginning of the sound");

    r = clip_rect(&P.clips[0]);
    sim_drag(r.x + r.w / 2, r.y + r.h / 2, r.x + r.w / 2 + 2 * beat_px, r.y + r.h / 2, KMOD_CTRL);
    check(P.nclips == 2 && P.clips[0].start == 3 * bf && P.clips[1].start == 5 * bf,
          "Ctrl+drag copies the clip and leaves the original");

    r = clip_rect(&P.clips[1]);
    sim(r.x + r.w / 2, r.y + r.h / 2, 0, 0, 1, 0);
    check(P.nclips == 1, "right-click deletes a clip");

    sim(item.x + 30, item.y + 10, 1, 0, 0, 0);
    sim(item.x + 30, item.y + 10, 0, 1, 0, 0);
    check(P.preview == 0, "clicking a library sound previews it");

    sim(L.tl_x + 200, L.top + 10, 1, 0, 0, 0);
    sim(L.tl_x + 200, L.top + 10, 0, 1, 0, 0);
    check(labs(P.playhead - x_to_frame(L.tl_x + 200)) <= 1, "clicking the ruler moves the playhead");

    /* theory: spelling and chords */
    char name[64];
    int t;
    char num[24];
    diatonic_chord(0, 0, 6, 0, &t, num, sizeof num);
    diatonic_chord_name(0, 0, 6, t, name, sizeof name);
    check(!strcmp(name, "Bdim") && !strcmp(num, "vii°"), "C major vii is Bdim");
    diatonic_chord(3, 1, 5, 0, &t, num, sizeof num);
    diatonic_chord_name(3, 1, 5, t, name, sizeof name);
    check(!strcmp(name, "Cb"), "Eb minor bVI is spelled Cb");
    chord_tone_names(7, CH_MIN, 0, name, sizeof name);
    check(!strcmp(name, "G Bb D"), "Gm is spelled G Bb D");
    chord_tone_names(7, CH_DIM7, 0, name, sizeof name);
    check(!strcmp(name, "G Bb Db Fb"), "Gdim7 is spelled G Bb Db Fb");
    note_above(6, 1, 3, 5, name, sizeof name);
    check(!strcmp(name, "Cb"), "IV of Gb major is Cb");

    /* theory pane: tabs, previews, placing a progression */
    theory_open = 1;
    sim(-1, -1, 0, 0, 0, 0);
    th_scroll = 0;
    SDL_Rect ref_tab = tab_rect(3, L.pane_x + 14, L.top + 10, PANE_W - 28);   /* Beats, Keys, Chords, Reference */
    sim(ref_tab.x + ref_tab.w / 2, ref_tab.y + 14, 1, 0, 0, 0);
    sim(ref_tab.x + ref_tab.w / 2, ref_tab.y + 14, 0, 1, 0, 0);
    check(theory_tab == 1, "clicking Reference opens its first page, the circle of fifths");
    int chord[3] = {57, 60, 64};
    preview_notes(chord, 3, 2);
    check(scratch && scratch_len == 2 * beat_frames(), "chord preview renders two beats");
    int before = P.nclips, samples_before = P.nsamples;
    th_root = 9;
    LOCK();
    P.playhead = 4 * beat_frames() + 10;       /* nearest bar: bar 2 */
    P.nchords = 0;
    UNLOCK();
    place_progression(&PROGRESSIONS[1]);       /* I - V - vi - IV in A: A E F#m D */
    check(P.nclips == before && P.nchords == 4 && P.chords[0].at == 4 && P.chords[3].at == 16 && P.chords[2].root == 6 &&
              P.chords[2].type == CH_MIN, "placing a progression puts one chord a bar on the chord track, from the nearest bar");
    int fsm[6], nfsm = chord_notes(6, CH_MIN, 48, fsm);
    int cs = chord_sample(fsm, nfsm, "F#m", 4);
    check(cs >= samples_before && chord_sample(fsm, nfsm, "F#m", 4) == cs, "a chord sound is made once, then reused");
    before = clip_add(cs, 4, 0, 0, P.samples[cs].frames);
    long fr = P.samples[P.clips[before].sample].frames;
    char msg[300];
    LOCK();
    project_set_rate(&P, 96000, msg, sizeof msg);
    UNLOCK();
    check(labs(P.samples[P.clips[before].sample].frames - 2 * fr) <= 2, "chords rebuild at a new project rate");

    /* genres: rendering, placing on new rows, splitting, editing, dragging */
    BeatSpec hb = genre_spec(0);                 /* House */
    long hl;
    float *hd = render_beat(&hb, (1 << DRUM_LANES) - 1, P.bpm, &hl);
    float hpeak = 0, first_ms = 0;
    for (long i = 0; i < 2 * hl; i++) hpeak = fabsf(hd[i]) > hpeak ? fabsf(hd[i]) : hpeak;
    for (long i = 0; i < 2 * (SR / 100); i++) first_ms = fabsf(hd[i]) > first_ms ? fabsf(hd[i]) : first_ms;
    free(hd);
    check(hl == (long)llround(16 * 60.0 / P.bpm / 4 * SR), "a 16-step beat lasts exactly one bar");
    check(hpeak > 0.2f && hpeak < 1.5f && first_ms > 0.1f, "the beat has sound, with the kick on step 1");

    gn_sel = 0;
    gn_split = 0;
    gn_bars = 4;
    int rows0 = P.ntracks, clips0 = P.nclips;
    place_beat(0, 0, -1);
    check(P.ntracks == rows0 + 1 && P.nclips == clips0 + 4, "Place adds one new row with 4 one-bar clips");
    check(!strcmp(P.tracks[rows0].name, "House beat"), "the new row is named after the genre");

    gn_split = 1;
    gn_bars = 2;
    rows0 = P.ntracks;
    clips0 = P.nclips;
    place_beat(0, 0, -1);                        /* House uses kick, clap, hat, open hat */
    check(P.ntracks == rows0 + 4 && P.nclips == clips0 + 8, "Row per drum: 4 rows, 2 bars each");
    check(!strcmp(P.tracks[rows0].name, "House Kick") && !strcmp(P.tracks[rows0 + 3].name, "House Open"),
          "split rows are named per drum");

    gn_split = 0;
    gn_bars = 4;
    clips0 = P.nclips;
    place_beat(4, 0, -1);                        /* drum & bass: a 32-step (2-bar) groove */
    check(P.nclips == clips0 + 2, "a 2-bar groove repeats twice to fill 4 bars");

    for (int i = 0; i < RI_COUNT; i++) *RAIL_FLAG[i] = 0;   /* Create > Beats, and only it */
    show_panel(RI_SOUNDS);
    sounds_sub = 4;
    sounds_scroll = 0;
    gn_sel = 0;
    frame();
    char was = gn_pat[0][0][1];
    sim(gn_cell[0][1].x + 4, gn_cell[0][1].y + 4, 1, 0, 0, 0);
    sim(gn_cell[0][1].x + 4, gn_cell[0][1].y + 4, 0, 1, 0, 0);
    check(was == '.' && gn_pat[0][0][1] == 'x', "clicking a step turns it on");
    sim(gn_cell[0][1].x + 4, gn_cell[0][1].y + 4, 1, 0, 0, 0);
    sim(gn_cell[0][1].x + 4, gn_cell[0][1].y + 4, 0, 1, 0, 0);
    sim(gn_cell[0][1].x + 4, gn_cell[0][1].y + 4, 1, 0, 0, 0);
    sim(gn_cell[0][1].x + 4, gn_cell[0][1].y + 4, 0, 1, 0, 0);
    sim(gn_cell[0][1].x + 4, gn_cell[0][1].y + 4, 1, 0, 0, 0);
    sim(gn_cell[0][1].x + 4, gn_cell[0][1].y + 4, 0, 1, 0, 0);
    check(gn_pat[0][0][1] == '.', "clicking cycles accent, ghost and back to rest");

    clips0 = P.nclips;
    rows0 = P.ntracks;
    view_start = 0;                              /* the pane covers the right side: drop where visible */
    sim_drag(gn_handle.x + 40, gn_handle.y + 10, (int)frame_to_x(2 * beat_frames()) + 2, lane_y(1), 0);
    check(P.nclips == clips0 + 4 && P.ntracks == rows0, "dragging the beat drops it on an existing row");
    check(P.clips[clips0].track == 1 && P.clips[clips0].start == 2 * beat_frames(),
          "the dropped beat starts where it was dropped, on that row");
    check(P.clips[clips0 + 1].start == P.clips[clips0].start + P.clips[clips0].length,
          "the bars follow each other with no gap");

    /* undo and redo */
    frame();
    int n0 = P.nclips, u0 = nundo;
    clip_add(0, 0, 0, 0, P.samples[0].frames);
    frame();
    check(P.nclips == n0 + 1 && nundo == u0 + 1, "adding a clip makes one undo step");
    undo_redo(0);
    check(P.nclips == n0, "undo removes the clip just added");
    undo_redo(1);
    check(P.nclips == n0 + 1, "redo puts it back");
    sel_clear();
    view_start = 0;
    frame();
    Clip before_drag = P.clips[P.nclips - 1];
    r = clip_rect(&P.clips[P.nclips - 1]);
    u0 = nundo;
    sim_drag(r.x + r.w / 2, r.y + r.h / 2, r.x + r.w / 2 + 2 * (int)(60.0 / P.bpm * pps), r.y + r.h / 2, 0);
    check(nundo == u0 + 1 && P.clips[P.nclips - 1].start != before_drag.start, "a whole drag is a single undo step");
    undo_redo(0);
    check(P.clips[P.nclips - 1].start == before_drag.start, "undoing the drag puts the clip back");
    double bpm0 = P.bpm;
    P.bpm = bpm0 + 7;
    frame();
    undo_redo(0);
    check(P.bpm == bpm0, "tempo changes can be undone");

    /* saving and opening */
    char tpath[1100];
    const char *tmpdir = getenv("TMPDIR") ? getenv("TMPDIR") : getenv("TEMP") ? getenv("TEMP") : "/tmp";
    snprintf(tpath, sizeof tpath, "%s/tinydaw-selftest-%u.tdaw", tmpdir, (unsigned)SDL_GetTicks() ^ (unsigned)time(NULL));
    P.pads[2] = (Pad){1, PAD_HOLD, 0.5f, 3, 4, 0};
    P.tracks[1].mute = 1;
    frame();
    check(project_save(&P, tpath, msg, sizeof msg) == 0, "the project saves");
    Project *q = malloc(sizeof *q);
    int lr = project_load(q, tpath, msg, sizeof msg);
    check(lr == 0, "the saved project opens again");
    if (lr == 0) {
        int same = q->nclips == P.nclips && !memcmp(q->clips, P.clips, (size_t)P.nclips * sizeof(Clip)) &&
                   q->nsamples == P.nsamples && q->ntracks == P.ntracks && q->bpm == P.bpm &&
                   q->loop_start == P.loop_start && q->loop_end == P.loop_end;
        check(same, "clips, tracks, tempo and loop come back exactly");
        int names = 1;
        for (int i = 0; i < P.nsamples && i < q->nsamples; i++)
            names &= !strcmp(q->samples[i].name, P.samples[i].name) && q->samples[i].frames == P.samples[i].frames;
        check(names, "every sound is rebuilt with its name and length");
        check(q->samples[0].frames == P.samples[0].frames &&
                  !memcmp(q->samples[0].data, P.samples[0].data, (size_t)P.samples[0].frames * 2 * sizeof(float)),
              "a generated tone is rebuilt sample for sample");
        check(!memcmp(&q->pads[2], &P.pads[2], sizeof(Pad)) && q->tracks[1].mute, "pads and track settings are kept");
        project_free(q);
    }
    free(q);
    FILE *bad = fopen(tpath, "w");
    if (bad) {
        fputs("not a project\n", bad);
        fclose(bad);
    }
    q = malloc(sizeof *q);
    int old_rate = g_rate;
    check(project_load(q, tpath, msg, sizeof msg) != 0 && g_rate == old_rate, "a file that isn't a project is refused");
    free(q);
    project_save(&P, tpath, msg, sizeof msg);
    int clips_before = P.nclips;
    open_project(tpath, 0);
    check(P.nclips == clips_before && !is_dirty() && nundo == 0, "opening replaces the project, clean, with fresh history");
    P.bpm += 1;
    frame();
    check(is_dirty(), "an edit marks the project unsaved");
    undo_redo(0);
    check(!is_dirty(), "undoing back to the saved state is clean again");
    remove(tpath);

    /* pads */
    binds_default();
    P.pads[0] = (Pad){0, PAD_ONESHOT, 0.8f, 0, 2, 0};
    LOCK();
    voices_clear(&P);
    UNLOCK();
    input_event(SRC_KEY, binds[0].key, 110, 1, -1);
    check(voice_playing(&P, 0), "pressing a pad's key plays its sound");
    P.pads[1] = (Pad){0, PAD_HOLD, 0.8f, 0, 2, 0};
    input_event(SRC_JOY, binds[1].joy, 127, 1, 0);
    int held = voice_playing(&P, 1);
    input_event(SRC_JOY, binds[1].joy, 0, 0, 0);
    check(held && !voice_playing(&P, 1), "a Hold pad plays while its button is down");
    P.pads[2] = (Pad){0, PAD_LOOP, 0.8f, 0, 2, 0};
    input_event(SRC_MIDI, binds[2].midi, 100, 1, 0);
    input_event(SRC_MIDI, binds[2].midi, 0, 0, 0);
    int looping = voice_playing(&P, 2);
    input_event(SRC_MIDI, binds[2].midi, 100, 1, 0);
    check(looping && !voice_playing(&P, 2), "a Loop pad starts on one press and stops on the next");
    learn_pad = 3;
    input_event(SRC_MIDI, 60, 100, 1, 0);
    check(binds[3].midi == 60 && learn_pad < 0, "Learn binds the next MIDI note to the pad");
    learn_pad = 4;
    input_event(SRC_KEY, binds[0].key, 100, 1, -1);
    check(binds[4].key != -1 && binds[0].key == -1, "an input moves to the pad that learns it");
    char jl[32];
    joy_label(201, jl, sizeof jl);
    check(!strcmp(jl, "axis 0-"), "controller inputs have readable names");
    P.inst_sample = 0;
    P.inst_root = 60;
    inst_midi = 1;
    input_event(SRC_MIDI, 72, 100, 1, 0);
    int found_inst = 0;
    for (int i = 0; i < MAX_VOICES; i++)
        if (P.voices[i].active && P.voices[i].note == 72 && fabs(P.voices[i].step - 2.0) < 1e-9) found_inst = 1;
    check(found_inst, "a MIDI note not on a pad plays the instrument an octave up");
    float vbuf[2 * 256] = {0}, vpeak = 0;
    Mixer *vm = mixer_new();
    LOCK();
    mixer_run(vm, &P, 0, 0, vbuf, 256, -1, 1);
    UNLOCK();
    mixer_free(vm);
    for (int i = 0; i < 2 * 256; i++) vpeak = fabsf(vbuf[i]) > vpeak ? fabsf(vbuf[i]) : vpeak;
    check(vpeak > 0.01f, "live voices are heard in the output");
    LOCK();
    voices_clear(&P);
    UNLOCK();

    /* recording pad hits */
    binds_default();
    frame();
    u0 = nundo;
    pads_rec = 1;
    int c0 = P.nclips;
    LOCK();
    P.playing = 1;
    P.loop = 0;
    P.playhead = 4 * beat_frames() + 37;
    UNLOCK();
    input_event(SRC_KEY, binds[0].key, 110, 1, -1);
    frame();
    LOCK();
    P.playhead = 6 * beat_frames() + 11;
    UNLOCK();
    input_event(SRC_KEY, binds[0].key, 110, 1, -1);
    frame();
    LOCK();
    P.playing = 0;
    UNLOCK();
    pads_rec = 0;
    frame();
    {
        int tt = P.nclips == c0 + 2 ? P.clips[c0].track : -1, open = tt >= 0 && P.clips[c0 + 1].track == tt;
        for (int c = 0; c < c0 && open; c++)    /* nothing else on that track from the take on */
            open = !(P.clips[c].track == tt && P.clips[c].start + P.clips[c].length > 4 * beat_frames());
        check(open && P.clips[c0].start == 4 * beat_frames() && P.clips[c0 + 1].start == 6 * beat_frames(),
              "a take lands on an open track, snapped to the grid");
        check(!take.on && take.hits == 2 && strstr(status_msg, "Recorded 2 hits"), "stopping ends the take and says where it went");
    }
    check(nundo == u0 + 1, "a recorded take is one undo step");
    {
        int nt = P.ntracks;
        undo_redo(0);                           /* back to the demo as it was, for the tests after */
        check(P.nclips == c0 && P.ntracks <= nt, "undo takes the take (and any track it added) away");
    }
    {                                           /* Rec while stopped: starts playing, the timer runs */
        double keep_view = view_start;
        long keep_ph = P.playhead;
        LOCK();
        P.playing = 0;
        P.playhead = 0;
        UNLOCK();
        int keep = MET.countin;
        MET.countin = 0;
        rec_toggle();
        frame();
        int started = pads_rec && P.playing && take.on;
        SDL_Delay(120);
        frame();
        double secs = (SDL_GetTicks() - take.since) / 1000.0;
        rec_toggle();
        LOCK();
        P.playing = 0;
        UNLOCK();
        frame();
        MET.countin = keep;
        check(started && secs > 0.1 && !take.on && !pads_rec, "Rec starts playing, its timer runs, and Rec again stops the take");
        LOCK();
        P.playhead = keep_ph;                   /* as it was, for the tests after */
        UNLOCK();
        view_start = keep_view;
        history_check();
    }

    /* effects: each one measured on rendered audio, in a separate test project */
    {
        Project *T = malloc(sizeof *T);
        project_init(T);
        GenParams saw = {WAVE_SAW, 69, 2.0, 0.5, 2, 2, 440};          /* 440 Hz saw, 1 s at 120 BPM */
        int ss = sample_generate(T, &saw);
        long sl = T->samples[ss].frames, half = sl / 2;
        T->clips[T->nclips++] = (Clip){ss, 0, 0, 0, sl, 0, 0, 0, 0, 0, 0};
        float *b0 = test_render(T, 0, sl, -1);
        double ref = test_rms(b0, 0, sl, 2);

        T->tracks[0].pan = -1;
        float *b1 = test_render(T, 0, sl, -1);
        check(test_rms(b1, 0, sl, 1) < 1e-4 && test_rms(b1, 0, sl, 0) > ref * 1.3, "pan hard left silences the right");
        T->tracks[0].pan = 0;

        T->tracks[0].filter = FILT_LOW;
        T->tracks[0].cutoff = 150;
        T->tracks[0].reso = 0;
        float *b2 = test_render(T, 0, sl, -1);
        check(test_rms(b2, half, sl, 2) < ref * 0.4, "a low-pass well below the note takes most of it away");
        T->tracks[0].filter = FILT_HIGH;
        T->tracks[0].cutoff = 4000;
        float *b3 = test_render(T, 0, sl, -1);
        check(test_rms(b3, half, sl, 2) < ref * 0.5 && test_rms(b3, half, sl, 2) > ref * 0.02,
              "a high-pass above the note leaves only its harmonics");
        T->tracks[0].filter = FILT_LOW;
        T->tracks[0].cutoff = 440;
        T->tracks[0].reso = 0;
        float *b4a = test_render(T, 0, sl, -1);
        T->tracks[0].reso = 0.95f;
        float *b4b = test_render(T, 0, sl, -1);
        check(test_rms(b4b, half, sl, 2) > test_rms(b4a, half, sl, 2) * 1.5, "resonance boosts the cutoff frequency");
        T->tracks[0].filter = FILT_OFF;

        T->tracks[0].eq[0] = 12;
        GenParams low = {WAVE_SINE, 33, 2.0, 0.3, 2, 2, 440};          /* 55 Hz */
        int ls = sample_generate(T, &low);
        T->clips[0].sample = ls;
        T->clips[0].length = T->samples[ls].frames;
        float *b5 = test_render(T, 0, sl, -1);
        T->tracks[0].eq[0] = 0;
        float *b5r = test_render(T, 0, sl, -1);
        check(test_rms(b5, half, sl, 2) > test_rms(b5r, half, sl, 2) * 2.5, "the low EQ band boosts a bass note");
        T->clips[0].sample = ss;
        T->clips[0].length = sl;

        T->clips[0].gain_db = -6.0206f;
        float *b6 = test_render(T, 0, sl, -1);
        check(fabs(test_rms(b6, half, sl, 2) / test_rms(b0, half, sl, 2) - 0.5) < 0.01, "clip gain -6 dB halves it");
        T->clips[0].gain_db = 0;
        T->clips[0].fade_in = sl / 4;
        T->clips[0].fade_out = sl / 4;
        float *b7 = test_render(T, 0, sl, -1);
        check(test_rms(b7, 0, sl / 40, 2) < ref * 0.06 && test_rms(b7, sl - sl / 40, sl, 2) < ref * 0.06 &&
                  test_rms(b7, sl / 3, 2 * sl / 3, 2) > ref * 0.9, "fades start and end at silence");
        T->clips[0].fade_in = T->clips[0].fade_out = 0;

        T->tracks[0].drive = 1;
        T->clips[0].gain_db = -20;
        float *b8 = test_render(T, 0, sl, -1);
        T->tracks[0].drive = 0;
        float *b8r = test_render(T, 0, sl, -1);
        check(test_rms(b8, half, sl, 2) > test_rms(b8r, half, sl, 2) * 2, "drive makes a quiet sound hotter");
        T->clips[0].gain_db = 0;

        long beat = (long)llround(60.0 / T->bpm * SR), end = sl + 3 * SR;
        T->clips[0].length = beat / 4;                                 /* a short blip, then silence */
        T->tracks[0].delay_send = 1;
        T->fx.delay_div = 5;                                           /* 1/4 = one beat */
        T->fx.delay_feedback = 0.5f;
        float *b9 = test_render(T, 0, end, -1);
        check(test_rms(b9, beat + beat / 16, beat + beat / 5, 2) > 0.02 && test_rms(b9, beat / 2, beat - 100, 2) < 1e-3,
              "the delay repeats the sound one beat later, silent in between");
        T->tracks[0].delay_send = 0;
        T->tracks[0].reverb_send = 1;
        float *b10 = test_render(T, 0, end, -1);
        check(test_rms(b10, beat / 2, beat, 2) > 1e-3, "the reverb rings on after the sound stops");
        check(project_has_tails(T), "a reverb send counts as a tail for export");
        T->tracks[0].reverb_send = 0;
        T->clips[0].length = sl;

        GenParams kick = {WAVE_KICK, 36, 0.5, 0.9, 0, 0, 440};
        int ks = sample_generate(T, &kick);
        T->clips[T->nclips++] = (Clip){ks, 1, 0, 0, T->samples[ks].frames, 0, 0, 0, 0, 0, 0};
        T->fx.sc_source = 1;
        T->tracks[0].duck = 1;
        float *b11 = test_render(T, 0, sl, 0);                         /* track 0 alone: its stem */
        T->tracks[0].duck = 0;
        float *b11r = test_render(T, 0, sl, 0);
        check(test_rms(b11, 0, SR / 20, 2) < test_rms(b11r, 0, SR / 20, 2) * 0.4 &&
                  test_rms(b11, sl - SR / 10, sl, 2) > test_rms(b11r, sl - SR / 10, sl, 2) * 0.8,
              "the sidechain ducks the track under the kick, then lets it back up");

        int up = sample_variant(T, ss, 12, 0), rv = sample_variant(T, ss, 0, 1), kup = sample_variant(T, ks, 12, 0);
        int zc0 = 0, zc1 = 0;
        for (long i = 1; i < sl; i++) {
            zc0 += (T->samples[ss].data[2 * i - 2] < 0) != (T->samples[ss].data[2 * i] < 0);
            zc1 += (T->samples[up].data[2 * i - 2] < 0) != (T->samples[up].data[2 * i] < 0);
        }
        check(up >= 0 && T->samples[up].frames == sl && abs(zc1 - 2 * zc0) < zc0 / 20,
              "a synth note an octave up is made again: same length, twice the frequency");
        check(kup >= 0 && labs(T->samples[kup].frames - T->samples[ks].frames / 2) <= 2,
              "a recorded sound an octave up is resampled: half the length");
        check(rv >= 0 && T->samples[rv].data[0] == T->samples[ss].data[2 * (sl - 1)], "reverse plays it backwards");
        check(sample_variant(T, up, 12, 0) == up && sample_base(T, up) == ss, "variants are reused, not duplicated");

        T->tracks[0].filter = FILT_BAND;
        T->tracks[0].eq[2] = -3;
        T->fx.pingpong = 1;
        T->clips[0].gain_db = -2.5f;
        T->clips[0].fade_in = 100;
        T->clips[T->nclips++] = (Clip){rv, 2, 0, 0, T->samples[rv].frames, 0, 0, 0, 0, 0, 0};
        char fpath[1200];
        snprintf(fpath, sizeof fpath, "%s.fx", tpath);
        project_save(T, fpath, msg, sizeof msg);
        Project *U = malloc(sizeof *U);
        int lr2 = project_load(U, fpath, msg, sizeof msg);
        check(lr2 == 0 && !memcmp(U->tracks, T->tracks, sizeof T->tracks) && !memcmp(&U->fx, &T->fx, sizeof T->fx) &&
                  !memcmp(U->clips, T->clips, (size_t)T->nclips * sizeof(Clip)),
              "track effects, master effects and clip gain/fades are saved exactly");
        check(lr2 == 0 && U->samples[rv].pitched && U->samples[rv].reversed && U->samples[rv].frames == sl,
              "a reversed clip's sound comes back reversed");
        if (lr2 == 0) project_free(U);
        free(U);
        remove(fpath);

        float *bufs[] = {b0, b1, b2, b3, b4a, b4b, b5, b5r, b6, b7, b8, b8r, b9, b10, b11, b11r};
        for (size_t i = 0; i < sizeof bufs / sizeof *bufs; i++) free(bufs[i]);
        project_free(T);
        free(T);
    }

    /* analysis: a 1 kHz sine at full scale peaks at 0 dB in the right bin */
    {
        static float sine[VIS_FFT], db[VIS_FFT / 2];
        double hz = 1000.0;                            /* need not fall exactly on a bin */
        for (int i = 0; i < VIS_FFT; i++) sine[i] = (float)sin(2 * VIS_PI * hz * i / SR);
        spectrum_db(sine, VIS_FFT, db);
        int best = 1;
        for (int k = 1; k < VIS_FFT / 2; k++)
            if (db[k] > db[best]) best = k;
        double peak_hz = best * (double)SR / VIS_FFT;
        check(fabs(peak_hz - 1000) < (double)SR / VIS_FFT && db[best] > -1.5 && db[best] < 0.5,
              "the analyzer shows a full-scale 1 kHz sine at 1 kHz, 0 dB");
        check(db[best * 3] < db[best] - 60, "and nothing at its third harmonic");

        Project *T = malloc(sizeof *T);
        project_init(T);
        GenParams g = {WAVE_SINE, 69, 1.0, 0.5, 2, 2, 440};
        int si = sample_generate(T, &g);
        T->clips[T->nclips++] = (Clip){si, 2, 0, 0, T->samples[si].frames, 0, 0, 0, 0, 0, 0};
        Mixer *m = mixer_new();
        float buf[2 * 2048] = {0}, lv[MAX_TRACKS][2];
        mixer_run(m, T, 4096, 1, buf, 2048, -1, 0);
        mixer_levels(m, lv);
        check(fabsf(lv[2][0] - 0.5f * T->tracks[2].volume) < 0.02f && lv[0][0] == 0,
              "track meters read each track's level after its fader");
        mixer_levels(m, lv);
        check(lv[2][0] == 0, "and reset once read");
        mixer_free(m);
        project_free(T);
        free(T);
        Meter mt = {0};
        meter_feed(&mt, 1.3f, 0.016f);
        check(mt.clipped && mt.peak > 1.29f, "a meter lights its clip warning above 0 dB");
    }

    /* the pitch wheel and mod wheel arrive as continuous MIDI controls, not pad presses */
    midi_in(MIDI_BEND, 16383, 0, NULL);
    check(P.bend > 0.99f, "the pitch wheel bends live sounds");
    midi_in(MIDI_BEND, 8192, 0, NULL);
    midi_in(MIDI_CCV + 1, 127, 0, NULL);
    check(P.modwheel > 0.99f && P.bend == 0, "the mod wheel is read, and the wheel springs back to centre");
    midi_in(MIDI_CCV + 1, 0, 0, NULL);

    /* clip edits through the UI helpers */
    frame();
    int ci = P.nclips - 1;
    long len0 = P.clips[ci].length, fr0 = P.samples[P.clips[ci].sample].frames;
    u0 = nundo;
    clip_variant(ci, 12, 0);
    frame();
    long want = llround(len0 * (double)P.samples[P.clips[ci].sample].frames / fr0);
    check(clip_semis(&P.clips[ci]) == 12 && labs(P.clips[ci].length - want) <= 2 && nundo == u0 + 1,
          "transposing a clip keeps the same stretch of sound, as one undo step");
    undo_redo(0);
    check(clip_semis(&P.clips[ci]) == 0 && P.clips[ci].length == len0, "and undo brings the original back");

    /* multiselect: box, shift-click, group moves, delete, duplicate, copy and paste */
    {
        theory_open = pads_open = mixer_open = vis_open = 0;
        LOCK();
        P.nclips = 0;
        P.playing = 0;
        UNLOCK();
        sel_clear();
        view_start = 0;
        snap_i = 3;
        frame();
        long bt = beat_frames();
        clip_add(0, 0, 1 * bt, 0, bt);
        clip_add(0, 1, 2 * bt, 0, bt);
        clip_add(0, 2, 6 * bt, 0, bt);
        frame();
        int bx0 = (int)frame_to_x(0) + 3, bx1 = (int)frame_to_x(4 * bt);
        sim_drag(bx0, lane_y(0) - L.lane_h / 2 + 3, bx1, lane_y(1) + 10, 0);
        check(sel_count() == 2 && csel[0] && csel[1] && !csel[2], "dragging a box over empty space selects the clips in it");
        SDL_Rect r3 = clip_rect(&P.clips[2]);
        sim(r3.x + r3.w / 2, r3.y + r3.h / 2, 1, 0, 0, KMOD_SHIFT);
        sim(r3.x + r3.w / 2, r3.y + r3.h / 2, 0, 1, 0, KMOD_SHIFT);
        check(sel_count() == 3 && csel[2], "Shift+click adds a clip to the selection");
        sim(r3.x + r3.w / 2, r3.y + r3.h / 2, 1, 0, 0, KMOD_SHIFT);
        sim(r3.x + r3.w / 2, r3.y + r3.h / 2, 0, 1, 0, KMOD_SHIFT);
        check(sel_count() == 2 && !csel[2], "and Shift+click again takes it out");
        frame();
        int u1 = nundo;
        SDL_Rect r1 = clip_rect(&P.clips[0]);
        int bpx = (int)(60.0 / P.bpm * pps);
        sim_drag(r1.x + r1.w / 2, r1.y + r1.h / 2, r1.x + r1.w / 2 + 2 * bpx, r1.y + r1.h / 2 + L.lane_h, 0);
        check(P.clips[0].start == 3 * bt && P.clips[1].start == 4 * bt && P.clips[0].track == 1 && P.clips[1].track == 2 &&
                  P.clips[2].start == 6 * bt, "dragging one selected clip moves them all, the others stay");
        check(nundo == u1 + 1, "a group move is one undo step");
        SDL_Keysym kk = {0};
        kk.sym = SDLK_LEFT;
        for (int i = 0; i < 6; i++) key(kk);
        check(P.clips[0].start == 0 && P.clips[1].start == bt, "nudging stops when the first clip reaches the start");
        kk.sym = SDLK_d;
        kk.mod = KMOD_CTRL;
        key(kk);
        check(P.nclips == 5 && sel_count() == 2 && csel[3] && csel[4] && P.clips[3].start == 2 * bt,
              "Ctrl+D copies the selection to just after itself and selects the copies");
        kk.sym = SDLK_c;
        key(kk);
        LOCK();
        P.playhead = 10 * bt + 5;
        UNLOCK();
        kk.sym = SDLK_v;
        key(kk);
        check(P.nclips == 7 && P.clips[5].start == 10 * bt && P.clips[6].start == 11 * bt,
              "Ctrl+C then Ctrl+V pastes the selection at the playhead");
        kk.sym = SDLK_DELETE;
        kk.mod = 0;
        key(kk);
        check(P.nclips == 5 && sel_count() == 0, "Delete removes every selected clip");
        kk.sym = SDLK_a;
        kk.mod = KMOD_CTRL;
        key(kk);
        check(sel_count() == 5, "Ctrl+A selects everything");
        SDL_Rect rr = clip_rect(&P.clips[2]);
        sim(rr.x + rr.w / 2, rr.y + rr.h / 2, 1, 0, 0, 0);
        sim(rr.x + rr.w / 2, rr.y + rr.h / 2, 0, 1, 0, 0);
        check(sel_count() == 1 && selected == 2, "a plain click on one of them selects just that one");

        /* scrollbars: the Theory panel's Keys page is taller than the window */
        open_panel(RI_THEORY);
        theory_tab = 0;
        th_scroll = 0;
        frame();
        frame();
        SDL_Rect pane = {L.pane_x, L.top, L.pane_w, H - L.top - L.status};
        int sx = pane.x + pane.w - 7;
        check(th_content_h > pane.h, "the Theory panel's Keys page is taller than the window here");
        sim(sx, pane.y + pane.h - 20, 1, 0, 0, 0);
        sim(sx, pane.y + pane.h - 20, 0, 1, 0, 0);
        check(th_scroll > 0, "clicking the scrollbar below its handle scrolls down a page");
        th_scroll = 0;
        frame();
        sim(sx, pane.y + 20, 1, 0, 0, 0);
        for (int i = 1; i <= 6; i++) sim(sx, pane.y + 20 + i * 15, 0, 0, 0, 0);
        sim(sx, pane.y + 110, 0, 1, 0, 0);
        check(th_scroll > 0 && drag.kind == D_NONE, "dragging the handle scrolls");
        kk.sym = SDLK_PAGEDOWN;
        kk.mod = 0;
        th_scroll = 0;
        key(kk);
        frame();
        check(th_scroll > 0, "Page Down scrolls the open panel");

        /* the rail and panels */
        open_panel(RI_MIXER);
        frame();
        check(mixer_open && !theory_open && L.pane_w == PANE_W, "opening a panel closes the one that was open");
        int tl_with = L.tl_w;
        kk.sym = SDLK_b;
        kk.mod = KMOD_CTRL;
        key(kk);
        frame();
        check(!mixer_open && L.tl_w > tl_with, "Ctrl+B hides the panel and the timeline takes the room");
        key(kk);
        check(mixer_open, "and brings the same panel back");
        sim(RAIL_W / 2, L.top + 6 + RI_VISUAL * 62 + 20, 1, 0, 0, 0);
        sim(RAIL_W / 2, L.top + 6 + RI_VISUAL * 62 + 20, 0, 1, 0, 0);
        check(vis_open && !mixer_open, "clicking a rail button opens its panel");
        sim(RAIL_W / 2, L.top + 6 + RI_VISUAL * 62 + 20, 1, 0, 0, 0);
        sim(RAIL_W / 2, L.top + 6 + RI_VISUAL * 62 + 20, 0, 1, 0, 0);
        check(!vis_open, "and clicking it again hides it");
        Col bg_dark = BG;
        theme_apply(1);
        check(BG.r > 200 && TEXT.r < 60, "the light theme is light, with dark text");
        theme_apply(0);
        check(BG.r == bg_dark.r, "and the dark theme comes back exactly");
        int shaped_ok = 1;
        for (int k = THEME_XP; k <= THEME_BLENDER; k++) {     /* each shaped theme draws a whole frame */
            theme_apply(k);
            frame();
            shaped_ok &= ST.shaped && theme_kind == k && (theme_light == (k != THEME_BLENDER));
        }
        check(shaped_ok, "Windows XP, Frutiger Aero and Blender themes each draw a frame in their own style");
        theme_apply(0);
        check(!ST.shaped && BG.r == bg_dark.r, "and switching back leaves no trace of them");
        open_panel(RI_SOUNDS);
    }

    /* the time scale, the metronome, count-in and navigation keys */
    {
        char tb[24];
        fmt_clock(65.25, 1, tb, sizeof tb);
        check(!strcmp(tb, "1:05.25"), "the playhead clock reads 1:05.25");
        fmt_clock(90, 0, tb, sizeof tb);
        char tc[24];
        fmt_clock(2.5, 0, tc, sizeof tc);
        check(!strcmp(tb, "1:30") && !strcmp(tc, "0:02.5"), "the time scale labels read 1:30 and 0:02.5");

        typeof(MET) saved_met = MET;
        MET = (typeof(MET)){1, CLICK_CLICK, 1, 1, 0, 1, 1.0f};
        metronome_prepare();
        long bf2 = beat_frames();
        float *mb = calloc((size_t)bf2 * 4 * 2, sizeof(float));
        LOCK();
        click_pos = -1;
        metronome_mix(mb, 0, (int)(2 * bf2), 0);
        UNLOCK();
        double at0 = test_rms(mb, 0, SR / 50, 2), at1 = test_rms(mb, bf2, bf2 + SR / 50, 2);
        double between = test_rms(mb, bf2 / 2, bf2 / 2 + SR / 50, 2);
        check(at0 > 0.01 && at1 > 0.01 && between < 1e-6, "the metronome clicks on each beat and is silent between");
        check(at0 > at1 * 1.2, "the first beat of the bar is louder");
        memset(mb, 0, sizeof(float) * (size_t)bf2 * 8);
        MET.sub = 2;
        LOCK();
        click_pos = -1;
        metronome_mix(mb, 0, (int)bf2, 0);
        UNLOCK();
        check(test_rms(mb, bf2 / 2, bf2 / 2 + SR / 50, 2) > 0.005, "8ths add a tick halfway through the beat");
        free(mb);

        MET.sub = 1;
        MET.countin = 1;
        MET.countin_rec_only = 0;
        int was_paused = !dev || SDL_GetAudioDeviceStatus(dev) == SDL_AUDIO_PAUSED;
        if (dev) SDL_PauseAudioDevice(dev, 1);  /* only this test drives the audio, or it races */
        LOCK();
        P.playing = 0;
        P.loop = 0;
        P.playhead = 0;
        UNLOCK();
        toggle_play();
        check(P.playing && countin_left == 4 * bf2, "with a count-in, play starts with a bar of clicks");
        float *cb = calloc((size_t)bf2 * 2, sizeof(float));
        LOCK();
        audio_cb(NULL, (Uint8 *)cb, (int)(bf2 * 2 * sizeof(float)));
        long ph_during = P.playhead;
        for (int i = 0; i < 3; i++) audio_cb(NULL, (Uint8 *)cb, (int)(bf2 * 2 * sizeof(float)));
        audio_cb(NULL, (Uint8 *)cb, (int)(bf2 * 2 * sizeof(float)));
        long ph_after = P.playhead;
        UNLOCK();
        free(cb);
        if (dev && !was_paused) SDL_PauseAudioDevice(dev, 0);   /* as it was */
        check(ph_during == 0 && ph_after == bf2, "the song waits for the count-in, then plays from where it was");
        toggle_play();
        MET = saved_met;
        metronome_prepare();

        LOCK();
        P.nclips = 0;
        UNLOCK();
        sel_clear();
        clip_add(0, 0, 2 * bf2, 0, bf2);
        clip_add(0, 1, 9 * bf2, 0, 3 * bf2);
        playhead_to(0);
        SDL_Keysym nk = {0};
        nk.sym = SDLK_RIGHT;
        nk.mod = KMOD_CTRL;
        key(nk);
        check(P.playhead == 4 * bf2, "Ctrl+Right moves the playhead to the next bar");
        nk.mod = KMOD_CTRL | KMOD_SHIFT;
        key(nk);
        check(P.playhead == 5 * bf2, "Ctrl+Shift+Right moves it one beat");
        nk.sym = SDLK_LEFT;
        nk.mod = KMOD_CTRL;
        key(nk);
        key(nk);
        check(P.playhead == 0, "Ctrl+Left goes back to the bar, then the bar before");
        nk.mod = KMOD_ALT;
        nk.sym = SDLK_RIGHT;
        key(nk);
        long e1 = P.playhead;
        key(nk);
        check(e1 == 2 * bf2 && P.playhead == 3 * bf2, "Alt+Right jumps to each clip edge in turn");
        nk.mod = 0;
        nk.sym = SDLK_END;
        key(nk);
        check(P.playhead == 12 * bf2, "End goes to the end of the song");
        frame();                                     /* the layout as it is now */
        nk.sym = SDLK_z;
        key(nk);
        frame();
        check(frame_to_x(12 * bf2) <= L.tl_x + L.tl_w && view_start == 0, "Z zooms so the whole song fits");
        pps = 400;
        nk.sym = SDLK_f;
        key(nk);
        frame();
        double phx = frame_to_x(P.playhead);
        check(phx > L.tl_x + L.tl_w / 4 && phx < L.tl_x + 3 * L.tl_w / 4, "F brings the playhead into view");
        pps = 110;
        Uint32 tnow = SDL_GetTicks();
        taps[0] = tnow - 1500;
        taps[1] = tnow - 1000;
        taps[2] = tnow - 500;
        ntaps = 3;
        tap_tempo();
        check(fabs(P.bpm - 120) <= 2, "tapping every half second sets 120 BPM");
    }

    /* Theory: + puts sounds in the library */
    {
        open_panel(RI_THEORY);
        theory_tab = 2;                          /* Chords */
        th_scroll = 0;
        th_root = 0;
        th_len = 2;
        lib_badge = 0;
        frame();
        int ns0 = P.nsamples;
        SDL_Rect pane = {L.pane_x, L.top, L.pane_w, H - L.top - L.status};
        /* the first chord row's + is at the right edge, below the tabs, strip and headings */
        int found = -1;
        for (int yy = pane.y + 60; yy < pane.y + pane.h && found < 0; yy += 2) {
            sim(pane.x + pane.w - 14 - 13, yy, 1, 0, 0, 0);
            sim(pane.x + pane.w - 14 - 13, yy, 0, 1, 0, 0);
            if (P.nsamples > ns0) found = yy;
        }
        check(found > 0 && !strncmp(P.samples[P.nsamples - 1].name, "C ", 2) &&
                  P.samples[P.nsamples - 1].nchord == 3, "+ on the Major row adds a C major chord to the library");
        check(lib_badge == 1, "and the Sounds button counts it");
        int ns1 = P.nsamples;
        sim(pane.x + pane.w - 14 - 13, found, 1, 0, 0, 0);
        sim(pane.x + pane.w - 14 - 13, found, 0, 1, 0, 0);
        check(P.nsamples == ns1, "adding the same chord again doesn't make a copy");
        th_len = 0;
        int note = 69;
        int s1 = add_to_library(&note, 1, "A4");
        check(s1 >= 0 && labs(P.samples[s1].frames - beat_frames()) <= 2, "the length setting sets how long added sounds are");
        int ns2 = P.nsamples;
        gn_split = 1;
        add_beat_to_library(0);
        check(P.nsamples == ns2 + 4 && P.samples[ns2].is_beat, "+ Library on a beat adds one loop per drum");
        gn_split = 0;
        open_panel(RI_SOUNDS);
        frame();
        check(lib_badge == 0, "opening Sounds clears the count");
    }

    /* typing the tempo: a click on BPM opens it for typing */
    {
        frame();
        double bpm0 = P.bpm;
        int bx = bpm_rect.x + bpm_rect.w / 2;
        sim(bx, 24, 1, 0, 0, 0);
        sim(bx, 24, 0, 1, 0, 0);
        check(numedit.active && numedit.v == &P.bpm && P.bpm == bpm0, "clicking BPM opens it for typing");
        numedit_text("98.5");
        SDL_Keysym ek = {0};
        ek.sym = SDLK_RETURN;
        numedit_key(ek);
        frame();
        check(!numedit.active && P.bpm == 98.5, "typing 98.5 and Enter sets the tempo");
        sim(bx, 24, 1, 0, 0, 0);
        sim(bx, 24, 0, 1, 0, 0);
        numedit_text("500");
        numedit_key(ek);
        frame();
        check(P.bpm == 300, "a tempo above 300 is held at 300");
        sim(bx, 24, 1, 0, 0, 0);
        sim(bx, 24, 0, 1, 0, 0);
        numedit_text("60");
        ek.sym = SDLK_ESCAPE;
        numedit_key(ek);
        frame();
        check(P.bpm == 300, "Esc leaves it as it was");
        sim(bx, 24, 1, 0, 0, 0);
        sim(bx, 24, 0, 1, 0, 0);
        numedit_text("110");
        sim(W / 2, H / 2, 1, 0, 0, 0);
        sim(W / 2, H / 2, 0, 1, 0, 0);
        check(P.bpm == 110 && !numedit.active, "clicking elsewhere sets what was typed");
        sim(bx, 24, 1, 0, 0, 0);
        for (int i = 1; i <= 5; i++) sim(bx + i * 6, 24, 0, 0, 0, 0);
        sim(bx + 30, 24, 0, 1, 0, 0);
        check(!numedit.active && P.bpm != 110, "dragging still changes it without typing");
        P.bpm = bpm0;
    }

    /* drop-down lists */
    {
        show_panel(RI_SOUNDS);
        sounds_sub = 4;                          /* Create > Beats */
        sounds_scroll = 0;
        gn_sel = 0;
        frame();
        frame();
        SDL_Rect pane = {L.pane_x, L.top, L.pane_w, H - L.top - L.status};
        check(sounds_content_h <= pane.h, "the Beats page fits without scrolling");
        int fx0 = L.pane_x + 14, fy = L.top + 10 + 40 + 15;          /* the Style field, under the tabs */
        sim(fx0 + 60, fy, 1, 0, 0, 0);
        sim(fx0 + 60, fy, 0, 1, 0, 0);
        check(dd.open && dd.sel == &gn_sel && dd.n == NGENRES && dd.cols == 2, "the style list opens in two columns");
        SDL_Rect it = dd_item(20);
        sim(it.x + 10, it.y + 10, 1, 0, 0, 0);
        sim(it.x + 10, it.y + 10, 0, 1, 0, 0);
        check(!dd.open && gn_sel == 20, "picking a style from the list selects it and closes the list");
        sim(fx0 + 60, fy, 1, 0, 0, 0);
        sim(fx0 + 60, fy, 0, 1, 0, 0);
        sim(W - 20, H - 60, 1, 0, 0, 0);
        sim(W - 20, H - 60, 0, 1, 0, 0);
        check(!dd.open && gn_sel == 20, "clicking elsewhere closes it without changing anything");
        gn_sel = 0;

        snap_i = 4;
        frame();
        SDL_Rect sn = {0, 0, 0, 0};
        for (int xx = 300; xx < 760 && !sn.w; xx += 2)      /* find the Snap field in the top bar */
            if (dropdown_probe_snap(xx)) sn = (SDL_Rect){xx, 9, 1, 30};
        sim(sn.x + 10, 24, 1, 0, 0, 0);
        sim(sn.x + 10, 24, 0, 1, 0, 0);
        check(snap_i == 5, "clicking Snap still steps to the next value");
        sim(snap_arrow_x(), 24, 1, 0, 0, 0);
        sim(snap_arrow_x(), 24, 0, 1, 0, 0);
        check(dd.open && dd.sel == &snap_i, "its arrow opens the list");
        it = dd_item(3);
        sim(it.x + 10, it.y + 10, 1, 0, 0, 0);
        sim(it.x + 10, it.y + 10, 0, 1, 0, 0);
        check(snap_i == 3, "and a value can be picked directly");
        snap_i = 3;
        open_panel(RI_SOUNDS);
    }

    /* the example songs open, sound right, and open as untitled copies */
    {
        examples_scan();
        check(nexamples == 16, "the sixteen example songs are found next to the program");
        int good = 0, harm = 0, chip_keys = 0, scenes = 0;
        for (int i = 0; i < nexamples; i++) {
            Project *q = malloc(sizeof *q);
            int old_rate = g_rate;
            if (project_load(q, example_path[i], msg, sizeof msg) == 0) {
                harm += q->nchords >= 3 || strstr(example_path[i], "experimental");
                scenes += q->nscenes >= 3 && q->nscenes == q->nsections;
                if (strstr(example_path[i], "chiptune")) {
                    int kr, ks, r0, s0, r1, s1;
                    key_at_bar(q, 25, &kr, &ks);
                    key_at_bar(q, 14, &r0, &s0);
                    key_at_bar(q, 38, &r1, &s1);
                    chip_keys = q->nkeys == 3 && kr == 9 && r0 == 2 && r1 == 0 && ks == 0;
                }
                long end = project_end(q), n = 2 * SR, at = end / 2;
                float *b = calloc((size_t)n * 2, sizeof(float));
                Mixer *m = mixer_new();
                for (long pos = 0; pos < n; pos += 1024)
                    mixer_run(m, q, at + pos, 1, b + 2 * pos, (int)(n - pos < 1024 ? n - pos : 1024), -1, 0);
                double r = test_rms(b, 0, n, 2);
                if (q->nclips > 20 && end > 50L * SR && r > 0.02 && strstr(msg, "missing") == NULL) good++;
                else fprintf(stderr, "example %s: %d clips, %.1f s, rms %.3f: %s\n", example_title[i], q->nclips,
                             (double)end / SR, r, msg);
                mixer_free(m);
                free(b);
                project_free(q);
            }
            free(q);
            g_rate = old_rate;
        }
        check(good == nexamples && good > 0, "each example opens, runs about a minute and is playing sound mid-song");
        check(harm == nexamples, "the example songs have their chords on the chord track");
        check(scenes == nexamples, "the example songs come with their session filled in: a scene for each section");
        check(chip_keys, "Chiptune changes key for the second level and the boss, and comes home for the win");
        if (nexamples) {
            int pick = 0;                       /* the one the tests after this were written against */
            for (int i = 0; i < nexamples; i++)
                if (strstr(example_title[i], "Disco")) pick = i;   /* (4/4 throughout, and 120 BPM: whole frames a beat) */
            snprintf(act_path, sizeof act_path, "%s", example_path[pick]);
            run_action(ACT_OPEN_EXAMPLE);
            check(proj_path[0] == 0 && !is_dirty() && P.nclips > 20, "an example opens as an untitled copy, nothing to save yet");
        }
    }

    /* tuning in the top bar, beside BPM: both open for typing and both still drag */
    {
        frame();
        double bpm0 = P.bpm, a40 = P.a4;
        int ax = a4_rect.x + a4_rect.w / 2, bx = bpm_rect.x + bpm_rect.w / 2;
        sim(ax, 24, 1, 0, 0, 0);
        sim(ax, 24, 0, 1, 0, 0);
        check(numedit.active && numedit.v == &P.a4, "clicking A4 opens the tuning for typing");
        numedit_text("432");
        SDL_Keysym ek = {0};
        ek.sym = SDLK_RETURN;
        numedit_key(ek);
        frame();
        check(P.a4 == 432 && P.bpm == bpm0, "typing 432 sets the tuning and leaves the tempo alone");
        sim(bx, 24, 1, 0, 0, 0);
        for (int i = 1; i <= 5; i++) sim(bx + i * 8, 24, 0, 0, 0, 0);
        sim(bx + 40, 24, 0, 1, 0, 0);
        check(P.bpm > bpm0 && P.a4 == 432 && !numedit.active, "dragging BPM still changes the tempo, not the tuning");
        P.bpm = bpm0;
        P.a4 = a40;
        frame();
    }

    /* pad presets fill all 16 pads, and undo puts the old ones back */
    {
        frame();
        Pad before[NPADS];
        memcpy(before, P.pads, sizeof before);
        int u0 = nundo;
        gn_sel = 0;
        load_pad_preset(0);
        frame();
        int filled = 0, kicks = 0;
        for (int k = 0; k < NPADS; k++) {
            filled += P.pads[k].sample >= 0;
            kicks += P.pads[k].sample >= 0 && P.samples[P.pads[k].sample].is_beat == 2 && P.samples[P.pads[k].sample].beat_mask == 0;
        }
        check(filled == 16 && kicks >= 4 && P.pads[7].volume < 0.5f, "Drum kit fills 16 pads: kicks, toms from the kick, a ghost snare");
        check(nundo == u0 + 1, "loading a preset is one undo step");
        undo_redo(0);
        check(!memcmp(P.pads, before, sizeof before), "and undo puts the old pads back");
        th_root = 9;
        th_scale = 1;
        load_pad_preset(2);
        frame();
        check(P.pads[0].mode == PAD_HOLD && P.samples[P.pads[0].sample].nchord == 3 &&
                  !strncmp(P.samples[P.pads[0].sample].name, "Am", 2), "Chords of the key in A minor starts on Am, held");
        load_pad_preset(3);
        check(P.samples[P.pads[0].sample].chord[0] == 69 && P.samples[P.pads[7].sample].chord[0] == 81,
              "Scale notes climb the scale: A4 on pad 1, A5 on pad 8");
        load_pad_preset(5);
        check(P.pads[0].mode == PAD_LOOP && P.samples[P.pads[0].sample].is_beat == 1, "Beat loops start and stop on a press");
        load_pad_preset(0);
        int ns = P.nsamples;
        load_pad_preset(0);
        check(P.nsamples == ns, "loading a preset again reuses its sounds");
        frame();
    }

    /* removing a sound takes its clips too, and undo brings both back */
    {
        frame();
        GenParams g = {WAVE_SINE, 72, 1.0, 0.5, 5, 50, 440};
        int si = sample_generate(&P, &g);
        clip_add(si, 0, 0, 0, P.samples[si].frames);
        clip_add(si, 1, beat_frames(), 0, P.samples[si].frames);
        frame();
        int n0 = P.nclips, u0 = nundo;
        remove_sound(si);
        frame();
        check(P.samples[si].removed && P.nclips == n0 - 2 && nundo == u0 + 1, "removing a sound removes its 2 clips, as one step");
        show_panel(RI_SOUNDS);
        sounds_sub = 0;
        lib_scroll = 1000;                        /* the newest sounds are at the end */
        memset(lib_rects, 0, sizeof lib_rects);
        frame();
        int shown_other = 0;
        for (int k = 0; k < P.nsamples; k++) shown_other |= k != si && lib_rects[k].w > 0;
        check(lib_rects[si].w == 0 && shown_other, "a removed sound leaves the library list");
        undo_redo(0);
        check(!P.samples[si].removed && P.nclips == n0, "undo brings the sound and its clips back");
        undo_redo(1);
        check(P.samples[si].removed && P.nclips == n0 - 2, "and redo removes them again");
        undo_redo(0);
        frame();
    }

    /* New sound: Play hears it without adding it to the library */
    {
        show_panel(RI_SOUNDS);
        sounds_sub = 3;
        frame();
        int ns = P.nsamples;
        SDL_Rect pane = {L.pane_x, L.top, L.pane_w, H - L.top - L.status};
        int found = 0;
        for (int yy = pane.y + 100; yy < pane.y + pane.h && !found; yy += 4) {  /* find the Play button, below the tabs */
            sim(pane.x + 12 + 40, yy, 1, 0, 0, 0);
            sim(pane.x + 12 + 40, yy, 0, 1, 0, 0);
            if (scratch && scratch == heard_buf) found = yy;
        }
        check(found && P.nsamples == ns, "Play in New sound plays it without adding it to the library");
        sim(pane.x + 12 + 40, found, 1, 0, 0, 0);
        sim(pane.x + 12 + 40, found, 0, 1, 0, 0);
        check(!scratch, "and pressing it again stops it");
        sounds_sub = 0;
    }

    /* the transport at the bottom */
    {
        frame();
        LOCK();
        P.playing = 0;
        P.loop = 0;
        P.playhead = 0;
        UNLOCK();
        SDL_Rect tr = transport_rect;
        int bw = 33, cy = tr.y + tr.h / 2;
        long bar = 4 * beat_frames();
        sim(tr.x + 4 * bw + 15, cy, 1, 0, 0, 0);             /* forward a bar */
        sim(tr.x + 4 * bw + 15, cy, 0, 1, 0, 0);
        check(P.playhead == bar, "the forward button moves the playhead a bar on");
        sim(tr.x + 2 * bw + 15, cy, 1, 0, 0, 0);             /* play */
        sim(tr.x + 2 * bw + 15, cy, 0, 1, 0, 0);
        int playing = P.playing;
        LOCK();
        P.playhead = 3 * bar;                                  /* as if it had played on */
        UNLOCK();
        sim(tr.x + 3 * bw + 15, cy, 1, 0, 0, 0);             /* stop */
        sim(tr.x + 3 * bw + 15, cy, 0, 1, 0, 0);
        check(playing && !P.playing && P.playhead == bar, "Play plays; Stop stops and goes back to where it began");
        sim(tr.x + 3 * bw + 15, cy, 1, 0, 0, 0);
        sim(tr.x + 3 * bw + 15, cy, 0, 1, 0, 0);
        check(P.playhead == 0, "Stop again goes to the start");
        sim(tr.x + 5 * bw + 15, cy, 1, 0, 0, 0);             /* to the end */
        sim(tr.x + 5 * bw + 15, cy, 0, 1, 0, 0);
        check(P.playhead == project_end(&P), "the end button goes to the end of the song");
        sim(tr.x + 6 * bw + 15, cy, 1, 0, 0, 0);             /* loop */
        sim(tr.x + 6 * bw + 15, cy, 0, 1, 0, 0);
        check(P.loop, "the loop button turns looping on");
        P.loop = 0;
        sim(tr.x + 7 * bw + 15, cy, 1, 0, 0, 0);             /* record */
        sim(tr.x + 7 * bw + 15, cy, 0, 1, 0, 0);
        check(pads_rec && P.playing, "the record button arms recording and starts playing");
        pads_rec = 0;
        LOCK();
        P.playing = 0;
        UNLOCK();
        countin_left = 0;
        frame();
        sim(tr.x + 15, cy, 1, 0, 0, 0);                       /* to the start */
        sim(tr.x + 15, cy, 0, 1, 0, 0);
        check(P.playhead == 0, "the start button goes to the beginning");
    }

    /* note clips and the piano roll */
    {
        frame();
        int t = -1;
        for (int k = 0; k < P.ntracks && t < 0; k++) {
            int used = 0;
            for (int i = 0; i < P.nclips; i++) used |= P.clips[i].track == k;
            if (!used) t = k;
        }
        if (t < 0 && P.ntracks < MAX_TRACKS) {
            LOCK();
            t = project_add_track(&P, NULL);
            UNLOCK();
        }
        if (t < 0) t = P.ntracks - 1;
        P.tracks[t].mute = P.tracks[t].solo = 0;
        P.tracks[t].volume = 1;
        int inst0 = P.inst_sample;
        P.inst_sample = -1;
        long bf = beat_frames(), bar = 4 * bf, at = (project_end(&P) / bar + 2) * bar;
        int c = new_note_clip(t, at + bf);
        P.inst_sample = inst0;
        check(c >= 0 && P.clips[c].pattern > 0 && P.clips[c].start == at && P.clips[c].length == bar && roll_clip == c,
              "a new note clip is one bar, on the bar, and opens in the piano roll");
        int pat = P.clips[c].pattern, root = P.clips[c].root;
        note_add(pat, root, 0, bf, 0.8f);
        int hi = note_add(pat, root + 12, 2 * bf, bf, 0.8f);
        float *b = test_render(&P, at, bar, t);
        long m = SR / 100;
        double r1 = test_rms(b, m, bf - m, 2), gap = test_rms(b, bf + SR / 20, 2 * bf - m, 2), r2 = test_rms(b, 2 * bf + m, 3 * bf - m, 2);
        int z1 = 0, z2 = 0;
        for (long i = m; i < bf - m; i++) z1 += (b[2 * i] < 0) != (b[2 * i + 2] < 0);
        for (long i = 2 * bf + m; i < 3 * bf - m; i++) z2 += (b[2 * i] < 0) != (b[2 * i + 2] < 0);
        free(b);
        check(r1 > 0.01 && r2 > 0.01 && gap < r1 * 0.02, "notes sound where they are and nowhere else");
        check(z1 > 20 && z2 > 1.85 * z1 && z2 < 2.15 * z1, "a note an octave up plays at twice the pitch");
        P.notes[hi].vel = 0.4f;
        b = test_render(&P, at, bar, t);
        double r2b = test_rms(b, 2 * bf + m, 3 * bf - m, 2);
        free(b);
        check(fabs(r2b / r2 - 0.5) < 0.03, "velocity scales a note's loudness");

        /* editing with the mouse */
        frame();
        int nn = P.nnotes;
        int ny = roll_grid.y + (roll_top - (root + 7)) * roll_kh + roll_kh / 2;
        int nx = (int)frame_to_x(at + 3 * bf + bf / 4);
        sim(nx, ny, 1, 0, 0, 0);
        sim(nx, ny, 0, 1, 0, 0);
        check(P.nnotes == nn + 1 && P.notes[nn].note == root + 7 && P.notes[nn].pattern == pat &&
                  P.notes[nn].start >= 3 * bf && P.notes[nn].start < 4 * bf, "clicking the grid adds a note there");
        SDL_Rect nr = note_rect(nn);
        sim_drag(nr.x + 3, nr.y + nr.h / 2, nr.x + 3, nr.y + nr.h / 2 - 2 * roll_kh, 0);
        check(P.notes[nn].note == root + 9, "dragging a note up two rows raises it two semitones");
        nr = note_rect(nn);
        long len0 = P.notes[nn].length;
        int beat_px = (int)(bf / (double)SR * pps);
        sim_drag(nr.x + nr.w - 2, nr.y + nr.h / 2, nr.x + nr.w - 2 + beat_px, nr.y + nr.h / 2, 0);
        check(P.notes[nn].length >= len0 + bf - snap_frames(), "dragging a note's end stretches it");
        check(P.clips[c].length == 2 * bar, "the clip grows by a bar to take in a note past its end");
        nr = note_rect(nn);
        sim(nr.x + 3, nr.y + 3, 0, 0, 1, 0);
        check(P.nnotes == nn, "right-clicking a note deletes it");
        undo_redo(0);
        check(P.nnotes == nn + 1 && P.notes[nn].note == root + 9, "and undo brings it back");
        SDL_Keysym kk = {0};
        kk.sym = SDLK_a;
        kk.mod = KMOD_CTRL;
        key(kk);
        check(nsel_count() == 3, "Ctrl+A selects every note in the clip");
        kk.sym = SDLK_UP;
        kk.mod = 0;
        key(kk);
        check(P.notes[nn].note == root + 10 && P.notes[nn - 2].note == root + 1, "Up moves the selected notes a semitone");
        kk.sym = SDLK_DELETE;
        key(kk);
        check(nsel_count() == 0 && pattern_end(&P, pat) == 0, "Delete removes the selected notes");
        frame();
        undo_redo(0);
        check(pattern_end(&P, pat) > 0, "undo puts them back");
        kk.sym = SDLK_ESCAPE;
        key(kk);
        key(kk);
        check(roll_clip < 0, "Esc clears the selection, then closes the roll");

        /* double-clicks on the timeline */
        view_start = (double)at / SR - 0.5;
        frame();
        SDL_Rect cr = clip_rect(&P.clips[c]);
        ldouble = 1;
        sim(cr.x + cr.w / 2, cr.y + cr.h / 2, 1, 0, 0, 0);
        sim(cr.x + cr.w / 2, cr.y + cr.h / 2, 0, 1, 0, 0);
        ldouble = 0;
        check(roll_clip == c, "double-clicking a note clip opens it in the piano roll");
        roll_close();
        view_start = (double)(at + 3 * bar) / SR - 0.5;
        frame();
        int n0 = P.nclips, ex = (int)frame_to_x(at + 3 * bar + bf);
        ldouble = 1;
        sim(ex, lane_y(t), 1, 0, 0, 0);
        sim(ex, lane_y(t), 0, 1, 0, 0);
        ldouble = 0;
        check(P.nclips == n0 + 1 && P.clips[n0].pattern > 0 && P.clips[n0].pattern != pat && P.clips[n0].start == at + 3 * bar &&
                  roll_clip == n0, "double-clicking an empty spot makes a new note clip there");
        roll_close();

        /* copies share their notes until made unique */
        sel_only(c);
        duplicate_selection(4 * bar);
        int cp = P.nclips - 1;
        check(P.clips[cp].pattern == pat, "a copy of a note clip shares its notes");
        roll_open(cp);
        roll_make_unique();
        int own = 0, orig = 0;
        for (int i = 0; i < P.nnotes; i++) {
            own += P.notes[i].pattern == P.clips[cp].pattern;
            orig += P.notes[i].pattern == pat;
        }
        check(P.clips[cp].pattern != pat && own == orig && own == 3, "make unique gives the copy its own notes");
        roll_close();
        frame();

        /* automation: what it does to the sound */
        b = test_render(&P, at, bf, t);
        double base = test_rms(b, m, bf - m, 2);
        free(b);
        auto_add(t, AUTO_VOLUME, at, 0);
        auto_add(t, AUTO_VOLUME, at + bf, 1 / 1.5f);
        b = test_render(&P, at, bf, t);
        double q1 = test_rms(b, m, bf / 4, 2), q4 = test_rms(b, 3 * bf / 4, bf - m, 2);
        free(b);
        check(q1 < q4 * 0.4 && q4 < base * 1.05, "a volume ramp fades the track in");
        auto_clear(t, AUTO_VOLUME);
        auto_add(t, AUTO_PAN, at, 0);
        b = test_render(&P, at, bf, t);
        double left = test_rms(b, m, bf - m, 0), right = test_rms(b, m, bf - m, 1);
        free(b);
        check(left > 0.01 && right < left * 0.01, "pan automation at the bottom puts it hard left");
        auto_clear(t, AUTO_PAN);
        frame();

        /* automation lanes with the mouse */
        auto_open[t] = 1;
        auto_param[t] = AUTO_CUTOFF;
        view_start = (double)at / SR - 0.5;
        frame();
        SDL_Rect ar = auto_rect(t);
        check(ar.h >= 58 && ar.y == track_y(t) + L.lane_h && auto_lane_at(ar.y + 5) == t &&
                  (t + 1 >= P.ntracks || track_y(t + 1) == ar.y + ar.h), "an open lane sits under its track and moves the rest down");
        int ax = (int)frame_to_x(at + bf);
        sim(ax, ar.y + 6, 1, 0, 0, 0);
        sim(ax, ar.y + 6, 0, 1, 0, 0);
        int first, np = auto_lane(&P, t, AUTO_CUTOFF, &first);
        check(np == 1 && P.autos[first].v > 0.97f && auto_to_value(AUTO_CUTOFF, P.autos[first].v) > 15000,
              "clicking the lane adds a point; at the top the cutoff is wide open");
        sim_drag(ax, ar.y + 6, ax + beat_px, ar.y + ar.h - 6, 0);
        np = auto_lane(&P, t, AUTO_CUTOFF, &first);
        check(np == 1 && P.autos[first].v < 0.03f && P.autos[first].at > at + bf, "dragging a point moves it in time and value");
        Track eff;
        auto_apply(&P, t, at, &eff);
        check(eff.filter == FILT_LOW && eff.cutoff < 25, "cutoff automation turns the filter on for that track");
        int px = (int)frame_to_x(P.autos[first].at), py = auto_v_to_y(ar, P.autos[first].v);
        sim(px, py, 0, 0, 1, 0);
        check(auto_lane(&P, t, AUTO_CUTOFF, NULL) == 0, "right-clicking a point removes it");
        undo_redo(0);
        check(auto_lane(&P, t, AUTO_CUTOFF, NULL) == 1, "undo brings the point back");

        /* it all saves and opens again */
        char tp[1100], msg2[256];
        const char *td = getenv("TMPDIR") ? getenv("TMPDIR") : getenv("TEMP") ? getenv("TEMP") : "/tmp";
        snprintf(tp, sizeof tp, "%s/tinydaw-roll-%u.tdaw", td, (unsigned)SDL_GetTicks() ^ (unsigned)time(NULL));
        int ok = project_save(&P, tp, msg2, sizeof msg2) == 0;
        Project *q = malloc(sizeof *q);
        ok = ok && project_load(q, tp, msg2, sizeof msg2) == 0;
        if (ok) {
            ok = q->nclips == P.nclips && !memcmp(q->clips, P.clips, (size_t)P.nclips * sizeof(Clip)) &&
                 q->nnotes == P.nnotes && q->nautos == P.nautos;
            for (int i = 0; ok && i < P.nnotes; i++)
                ok = q->notes[i].pattern == P.notes[i].pattern && q->notes[i].note == P.notes[i].note &&
                     q->notes[i].start == P.notes[i].start && q->notes[i].length == P.notes[i].length &&
                     fabsf(q->notes[i].vel - P.notes[i].vel) < 1e-5f;
            for (int i = 0; ok && i < P.nautos; i++)
                ok = q->autos[i].track == P.autos[i].track && q->autos[i].param == P.autos[i].param &&
                     q->autos[i].at == P.autos[i].at && fabsf(q->autos[i].v - P.autos[i].v) < 1e-5f;
            project_free(q);
        }
        free(q);
        remove(tp);
        check(ok, "note clips, notes and automation save and open again exactly");
        auto_open[t] = 0;
        frame();
    }

    /* the full-screen show: every scene draws, arrows step through them, cycling moves on */
    {
        show_open = 1;
        show_scene = 0;
        show_cycle = 0;
        SDL_Keysym sk = {0};
        int drew = 0;
        for (int i = 0; i <= SCENE_COUNT; i++) {
            frame();
            frame();
            drew += show_scene == 0 ? show_tex != NULL : scene_tex != NULL && scene_tw > 0;
            sk.sym = SDLK_RIGHT;
            key(sk);
        }
        check(drew == SCENE_COUNT + 1 && show_scene == 0, "the show steps through all 17 scenes and wraps back to the blobs");
        sk.sym = SDLK_LEFT;
        key(sk);
        check(show_scene == SCENE_COUNT, "Left goes back, round to the last scene");
        sk.sym = SDLK_TAB;
        key(sk);
        check(show_cycle == 1, "Tab turns cycling on");
        show_go(3);
        show_switch_at = show_wall - 1;          /* as if its time were up */
        frame();
        check(show_scene == 4 && show_fade_from == 3 && show_fade_left > 0, "cycling moves on by itself, crossfading");
        show_cycle = 0;
        show_scene = 0;
        show_open = 0;
        float probe[64 * 36 * 3];
        int lit = 0;
        for (int i = 0; i < SCENE_COUNT; i++) {
            char cap[64];
            double sum = 0;
            for (int f = 0; f < 8; f++) scene_render(i, 12 + f * 0.05, 0.05, 1, probe, 64, 36, cap, sizeof cap);
            for (int k = 0; k < 64 * 36 * 3; k++) sum += probe[k] == probe[k] ? probe[k] : 1e9;
            lit += sum / (64 * 36 * 3) > 0.002 && sum / (64 * 36 * 3) < 1.0;
        }
        check(lit == SCENE_COUNT, "every geometry and shader scene draws something, with no bad values");
    }

    /* ---- Sounds > Generator: the synth */
    {
        int bad = 0, quiet = 0, dc = 0;
        for (int i = 0; i < NSYNTH_PRESETS; i++) {
            SynthSound y;
            synth_from_preset(&y, i);
            long n;
            float *d = render_synth(&y, &n);
            if (!d) {
                bad++;
                continue;
            }
            double pk = 0, sum = 0, mean = 0;
            for (long k = 0; k < 2 * n; k++) {
                if (!isfinite(d[k])) bad++;
                pk = fmax(pk, fabs(d[k]));
                sum += d[k] * d[k];
                mean += d[k];
            }
            quiet += sqrt(sum / (2 * n)) < 0.02 || fabs(pk - 0.64) > 0.02;
            dc += fabs(mean / (2 * n)) > 0.03;
            free(d);
        }
        check(!bad && !quiet && !dc, "every Generator preset renders clean, audible, level-matched audio with no DC");

        int bass = 0;
        for (int i = 0; i < NSYNTH_PRESETS; i++)
            if (!strcmp(SYNTH_PRESETS[i].name, "Deep bass")) bass = i;
        double edge[2];
        for (int b = 0; b < 2; b++) {           /* brightness: more high end */
            SynthSound y;
            synth_from_preset(&y, bass);
            y.macro[MAC_BRIGHT] = b;
            long n;
            float *d = render_synth(&y, &n);
            double kk = exp(-2 * 3.14159265358979 * 1000.0 / SR), lp = 0, e = 0, a = 0;   /* the share above 1 kHz */
            for (long k = 0; k < n; k++) {
                double x = d[2 * k];
                lp = x + (lp - x) * kk;
                e += (x - lp) * (x - lp);
                a += x * x;
            }
            edge[b] = e / a;
            free(d);
        }
        check(edge[1] > edge[0] * 2, "Brightness opens up the high end");

        int sub = 0;
        for (int i = 0; i < NSYNTH_PRESETS; i++)
            if (!strcmp(SYNTH_PRESETS[i].name, "Rolling sub")) sub = i;
        int zc[2];
        for (int o = 0; o < 2; o++) {           /* the note: an octave up crosses zero twice as often */
            SynthSound y;
            synth_from_preset(&y, sub);
            y.note += 12 * o;
            long n;
            float *d = render_synth(&y, &n);
            zc[o] = 0;
            for (long k = 1; k < n; k++) zc[o] += (d[2 * k] >= 0) != (d[2 * k - 2] >= 0);
            free(d);
        }
        check(abs(zc[1] - 2 * zc[0]) < zc[0] / 20, "the Note control sets the pitch");

        Project *T = malloc(sizeof *T);
        project_init(T);
        SynthSound y;
        synth_from_preset(&y, bass);
        y.macro[MAC_GRIT] = 0.8;
        y.note = 40;
        int si = sample_generate_synth(T, &y, "My bass");
        check(si >= 0 && T->samples[si].synth && sample_root(&T->samples[si]) == 40,
              "a Generator sound plays unpitched at its note in note clips");
        int vi = sample_variant(T, si, 12, 0);
        y.note = 52;
        long vn;
        float *want = render_synth(&y, &vn);
        int same = vi >= 0 && want && T->samples[vi].frames == vn;
        for (long k = 0; same && k < 2 * vn; k++) same = T->samples[vi].data[k] == want[k];
        free(want);
        check(same, "a Generator sound is pitched by making it again at the new note");
        char msg[1200];
        int saved = project_save(T, tpath, msg, sizeof msg) == 0;
        Project *U = malloc(sizeof *U);
        int loaded = saved && project_load(U, tpath, msg, sizeof msg) == 0;
        check(loaded && U->samples[si].synth && !strcmp(U->samples[si].name, "My bass") &&
                  !memcmp(&U->samples[si].syn.patch, &T->samples[si].syn.patch, sizeof(SynthPatch)) &&
                  U->samples[si].syn.macro[MAC_GRIT] == 0.8 && U->samples[si].syn.note == 40 &&
                  U->samples[si].frames == T->samples[si].frames &&
                  !memcmp(U->samples[si].data, T->samples[si].data, (size_t)T->samples[si].frames * 2 * sizeof(float)),
              "a Generator sound saves as its recipe and comes back exactly");
        long before = T->samples[si].frames;
        int rate_ok = project_set_rate(T, 96000, msg, sizeof msg) == 0 && T->samples[si].frames == before * 2;
        project_set_rate(T, 48000, msg, sizeof msg);
        check(rate_ok, "a Generator sound is made again at a new project rate");
        if (loaded) project_free(U);
        free(U);
        project_free(T);
        free(T);
        remove(tpath);
    }
    {
        show_panel(RI_SOUNDS);
        sounds_sub = 1;
        sounds_scroll = 0;
        gx_genre = gx_kind = gx_preset = -1;
        frame();
        check(L.pane_w == PANE_W, "the Generator gets the wide panel");
        int dub = -1, bassk = -1, wob = -1, kick = -1;
        for (int i = 0; i < NSYNTH_GENRES; i++)
            if (!strcmp(SYNTH_GENRES[i], "Dubstep")) dub = i;
        for (int i = 0; i < NSYNTH_KINDS; i++)
            if (!strcmp(SYNTH_KINDS[i], "Bass")) bassk = i;
        for (int i = 0; i < NSYNTH_PRESETS; i++) {
            if (!strcmp(SYNTH_PRESETS[i].name, "Wobble bass")) wob = i;
            if (!strcmp(SYNTH_PRESETS[i].name, "Floor kick")) kick = i;
        }
        SDL_Rect r = gx_genre_r[dub + 1];
        sim(r.x + 5, r.y + 5, 1, 1, 0, 0);
        r = gx_kind_r[bassk + 1];
        sim(r.x + 5, r.y + 5, 1, 1, 0, 0);
        frame();
        int only = gx_row_r[wob].w > 0 && gx_row_r[kick].w == 0;
        for (int i = 0; i < NSYNTH_PRESETS && i < 128; i++)
            if (gx_row_r[i].w && (strcmp(SYNTH_PRESETS[i].genre, "Dubstep") || strcmp(SYNTH_PRESETS[i].kind, "Bass"))) only = 0;
        check(gx_genre == dub && gx_kind == bassk && only, "picking a style and a kind lists just those sounds");
        r = gx_row_r[wob];
        sim(r.x + 20, r.y + 5, 1, 1, 0, 0);
        frame();
        check(gx_preset == wob && scratch && scratch == gx_heard, "clicking a sound picks it and plays it");
        float *heard = gx_heard;
        r = gx_slide_r[MAC_BRIGHT];
        sim_drag(r.x + r.w / 2, r.y + r.h / 2, r.x - 30, r.y + r.h / 2, 0);
        frame();
        check(GX.macro[MAC_BRIGHT] == 0 && gx_heard != heard, "dragging a slider changes the sound and plays it again");
        r = gx_slide_r[MAC_BRIGHT];
        ldouble = 1;
        sim(r.x + 3, r.y + r.h / 2, 1, 1, 0, 0);
        ldouble = 0;
        check(GX.macro[MAC_BRIGHT] == 0.5, "double-clicking a slider puts it back in the middle");
        int ns = P.nsamples;
        r = gx_lib_r;
        sim(r.x + 5, r.y + 5, 1, 1, 0, 0);
        int hidden_layers = 1;
        for (int i = ns + 1; i < P.nsamples; i++) hidden_layers &= P.samples[i].is_layer && P.samples[i].hidden;
        check(P.nsamples == ns + 6 && P.samples[ns].synth && !strcmp(P.samples[ns].name, "Wobble bass") &&
                  P.samples[ns].nvel == 3 && P.samples[ns].nrr == 2 && hidden_layers,
              "+ Library keeps the sound, with 3 velocity layers and 2 round-robins kept out of the list");
        int nc = P.nclips;
        P.playhead = 8 * (long)SR;
        frame();
        r = gx_place_r;
        sim(r.x + 5, r.y + 5, 1, 1, 0, 0);
        int placed = P.nclips == nc + 1 && P.clips[nc].start == snap(8 * (long)SR) && P.samples[P.clips[nc].sample].synth;
        for (int c = 0; c < nc && placed; c++)              /* on a track that was free there */
            placed = !(P.clips[c].track == P.clips[nc].track && P.clips[c].start < P.clips[nc].start + P.clips[nc].length &&
                       P.clips[c].start + P.clips[c].length > P.clips[nc].start);
        check(placed, "Put it at the playhead places it on a free track");
        GX.note = 38;
        frame();
        r = gx_inst_r;
        int ns_inst = P.nsamples;
        sim(r.x + 5, r.y + 5, 1, 1, 0, 0);
        check(P.inst_sample == ns_inst && P.inst_root == 38, "Play notes with it makes it the instrument at its note");
        r = gx_kind_r[0];                       /* the sound being shaped stays when the filter changes */
        sim(r.x + 5, r.y + 5, 1, 1, 0, 0);
        gx_kind = -1;
        for (int i = 0; i < NSYNTH_KINDS; i++)
            if (!strcmp(SYNTH_KINDS[i], "Drums")) gx_kind = i;
        frame();
        check(gx_row_r[wob].w > 0 && gx_preset == wob, "the sound you're shaping stays listed when you change the filter");
        play_scratch(NULL, 0);
        gx_genre = gx_kind = gx_preset = -1;
        sounds_sub = 0;
        history_check();
    }

    /* ---- vowels and the Singer */
    {
        double share[2];
        for (int k = 0; k < 2; k++) {           /* "aah" has more up around 1 kHz than "ooh" */
            SynthSound y;
            synth_from_preset(&y, 0);
            memset(&y.patch, 0, sizeof y.patch);
            y.patch.wave = SW_SAW;
            y.patch.sustain = 1;
            y.patch.vowel = k ? 5 : 1;
            y.note = 48;
            y.seconds = 0.5;
            long n;
            float *d = render_synth(&y, &n);
            double kk = exp(-2 * 3.14159265358979 * 900.0 / SR), lp = 0, e = 0, a = 0;
            for (long i = 0; i < n; i++) {
                double x = d[2 * i];
                lp = x + (lp - x) * kk;
                e += (x - lp) * (x - lp);
                a += x * x;
            }
            share[k] = e / a;
            free(d);
        }
        check(share[0] > share[1] * 2, "the vowel controls change the voice (aah is brighter than ooh)");

        char syl[16][40];
        int rest[16];
        int n = vocal_syllables("Hap-py birth-day, to you. Ça va", syl, rest, 16);
        check(n == 8 && !strcmp(syl[0], "Hap") && !strcmp(syl[3], "day") && rest[4] == 1 && rest[6] == 2 &&
                  !strcmp(syl[6], "Ça") && rest[1] == 0,
              "words split into syllables at spaces and hyphens, with rests at commas and full stops");

        sv_defaults();
        int keep_root = th_root, keep_scale = th_scale;
        th_root = 0;
        th_scale = 0;
        SV.first = 48;
        SV.tune = TUNE_UPDOWN;
        snprintf(SV.words, sizeof SV.words, "one two three four five six");
        sv_make_notes();
        check(SV.nnotes == 6 && SV.notes[0].note == 48 && SV.notes[1].note == 50 && SV.notes[2].note == 52 &&
                  SV.notes[3].note == 53 && SV.notes[4].note == 55 && SV.notes[5].note == 53 &&
                  SV.notes[5].len > SV.notes[4].len,
              "a tune walks the key's scale, and the last word is held");
        th_root = keep_root;
        th_scale = keep_scale;
    }
    if (speech_available(NULL, 0)) {
        VocalSpec v;
        memset(&v, 0, sizeof v);
        snprintf(v.words, sizeof v.words, "la");
        v.nnotes = 1;
        v.notes[0] = (VocalNote){57, 0, 2};      /* A3, 220 Hz, held a second at 120 BPM */
        v.bpm = 120;
        v.volume = 0.8;
        char err[200] = "";
        long n;
        float *d = render_vocal(&v, &n, err, sizeof err);
        double best = 0;
        int lag = 0;
        if (d) {                                 /* the pitch in the middle, by autocorrelation */
            long mid = n / 2;
            for (int L = SR / 400; L < SR / 100; L++) {
                double c = 0, e1 = 0, e2 = 0;
                for (long i = mid; i < mid + 4096; i++) {
                    c += d[2 * i] * d[2 * (i + L)];
                    e1 += d[2 * i] * d[2 * i];
                    e2 += d[2 * (i + L)] * d[2 * (i + L)];
                }
                double r = c / sqrt(e1 * e2 + 1e-12);
                if (r > best) {
                    best = r;
                    lag = L;
                }
            }
        }
        double hz = lag ? (double)SR / lag : 0;
        check(d && fabs(hz - 220) < 220 * 0.03 && n >= SR, "a sung note is in tune and as long as the note");
        free(d);

        int bad = 0;
        snprintf(v.words, sizeof v.words, "hel-lo world");
        v.nnotes = 3;
        v.notes[0] = (VocalNote){60, 0, 0.5};
        v.notes[1] = (VocalNote){62, 0.5, 0.5};
        v.notes[2] = (VocalNote){64, 1, 1};
        for (int st = 0; st < VS_STYLES; st++)
            for (int hi = 0; hi < 2; hi++) {
                v.style = st;
                v.high = hi;
                d = render_vocal(&v, &n, err, sizeof err);
                double e = 0;
                for (long i = 0; d && i < 2 * n; i++) {
                    if (!isfinite(d[i])) bad++;
                    e += d[i] * d[i];
                }
                if (!d || sqrt(e / (2 * n)) < 0.02) bad++;
                free(d);
            }
        check(!bad, "every voice sings, low and high, with clean audio");

        int off = 0;                             /* the clear singers: in tune on every note */
        for (int sg = 0; sg < NCLEAR_SINGERS; sg++) {
            VocalSpec c;
            memset(&c, 0, sizeof c);
            c.style = VS_CLEAR;
            c.singer = sg;
            c.bpm = 120;
            c.volume = 0.8;
            snprintf(c.words, sizeof c.words, "sing to me");
            int base = CLEAR_SINGERS[sg].high ? 64 : 50;
            for (int k = 0; k < 3; k++) c.notes[k] = (VocalNote){base + 2 * k, k * 1.0, 0.95};
            c.nnotes = 3;
            float *cd = render_vocal(&c, &n, err, sizeof err);
            for (int k = 0; cd && k < 3; k++) {
                double want = 440 * pow(2, (c.notes[k].note - 69) / 12.0);
                int T = (int)lround(SR / want);
                long m = (long)((k + 0.5) * 0.5 * SR);
                double cc = 0, e1 = 0, e2 = 0, ch = 0, e3 = 0;   /* alike a period later, not half a period */
                for (long q = m; q < m + 2048; q++) {
                    cc += cd[2 * q] * cd[2 * (q + T)];
                    ch += cd[2 * q] * cd[2 * (q + T / 2)];
                    e1 += cd[2 * q] * cd[2 * q];
                    e2 += cd[2 * (q + T)] * cd[2 * (q + T)];
                    e3 += cd[2 * (q + T / 2)] * cd[2 * (q + T / 2)];
                }
                double rt = cc / sqrt(e1 * e2 + 1e-12), rh = ch / sqrt(e1 * e3 + 1e-12);
                if (!(rt > 0.85 && rh < rt - 0.2)) off++;
            }
            if (!cd) off += 3;
            free(cd);
        }
        check(!off, "every Clear singer sings each note in tune");

        Project *T = malloc(sizeof *T);
        project_init(T);
        v.style = VS_CHOIR;
        int si = sample_generate_vocal(T, &v, "Sung: hello world", err, sizeof err);
        char msg[1200];
        int loaded = 0;
        Project *U = malloc(sizeof *U);
        if (si >= 0 && project_save(T, tpath, msg, sizeof msg) == 0) loaded = project_load(U, tpath, msg, sizeof msg) == 0;
        check(loaded && U->samples[si].vocal && !strcmp(U->samples[si].voc.words, "hel-lo world") &&
                  U->samples[si].voc.nnotes == 3 && U->samples[si].voc.notes[2].note == 64 &&
                  U->samples[si].voc.style == VS_CHOIR && U->samples[si].frames == T->samples[si].frames &&
                  !memcmp(U->samples[si].data, T->samples[si].data, (size_t)T->samples[si].frames * 2 * sizeof(float)),
              "a sung line saves as words and notes and sings the same when opened");
        long before = T->samples[si].frames;
        int rate_ok = project_set_rate(T, 96000, msg, sizeof msg) == 0 && labs(T->samples[si].frames - 2 * before) <= 2;
        project_set_rate(T, 48000, msg, sizeof msg);
        check(rate_ok, "a sung line is sung again at a new project rate");
        if (loaded) project_free(U);
        free(U);
        project_free(T);
        free(T);
        remove(tpath);

        /* the Singer tab, by mouse and keyboard */
        show_panel(RI_SOUNDS);
        sounds_sub = 2;
        sounds_scroll = 0;
        sv_defaults();
        sv_espeak = -1;
        frame();
        SDL_Rect r = sv_words_r;
        sim(r.x + 5, r.y + 5, 1, 1, 0, 0);
        tf_insert("sing a song");
        tf_key((SDL_Keysym){.sym = SDLK_RETURN});
        frame();
        check(!strcmp(SV.words, "sing a song"), "typing words into the Singer");
        {                                       /* down to Sing it, as you would */
            SDL_Rect pr = panel_rect();
            int over = sv_sing_r.y + sv_sing_r.h + 10 - (pr.y + pr.h);
            if (over > 0) {
                sounds_scroll += over;
                frame();
            }
        }
        r = sv_sing_r;
        sim(r.x + 5, r.y + 5, 1, 1, 0, 0);
        check(scratch && scratch == sv_heard, "Sing it sings them");
        {                                       /* scroll down to the buttons, as you would */
            SDL_Rect pr = panel_rect();
            int over = sv_place_r.y + sv_place_r.h + 10 - (pr.y + pr.h);
            if (over > 0) sounds_scroll += over;
            frame();
        }
        int ns = P.nsamples;
        r = sv_lib_r;
        sim(r.x + 5, r.y + 5, 1, 1, 0, 0);
        check(P.nsamples == ns + 1 && P.samples[ns].vocal && !strcmp(P.samples[ns].name, "Sung: sing a song"),
              "+ Library keeps the sung line");
        int nc = P.nclips;
        P.playhead = 4 * (long)SR;
        r = sv_place_r;
        sim(r.x + 5, r.y + 5, 1, 1, 0, 0);
        check(P.nclips == nc + 1 && P.samples[P.clips[nc].sample].vocal && P.clips[nc].start == snap(4 * (long)SR),
              "Put it at the playhead places the sung line");
        int c = new_note_clip(P.ntracks - 1, 12 * beat_frames());   /* a melody to sing on */
        roll_close();
        int pat = P.clips[c].pattern;
        long bf = beat_frames();
        note_add(pat, 67, P.clips[c].offset, bf, 0.8f);
        note_add(pat, 69, P.clips[c].offset + bf, bf, 0.8f);
        note_add(pat, 71, P.clips[c].offset + 2 * bf, 2 * bf, 0.8f);
        selected = c;
        sounds_scroll = 0;                      /* back up to the tunes */
        frame();
        r = sv_tune_r[TUNE_CLIP];
        sim(r.x + 5, r.y + 5, 1, 1, 0, 0);
        frame();
        check(SV.tune == TUNE_CLIP && SV.nnotes == 3 && SV.notes[1].note == 69 && fabs(SV.notes[2].start - 2) < 1e-6 &&
                  fabs(SV.notes[2].len - 2) < 1e-6,
              "From a note clip sings on that clip's notes");
        nc = P.nclips;
        {
            SDL_Rect pr = panel_rect();
            int over = sv_place_r.y + sv_place_r.h + 10 - (pr.y + pr.h);
            if (over > 0) sounds_scroll += over;
            frame();
        }
        r = sv_place_r;
        sim(r.x + 5, r.y + 5, 1, 1, 0, 0);
        check(P.nclips == nc + 1 && P.clips[nc].start == P.clips[c].start && P.clips[nc].track != P.clips[c].track,
              "Put it with the clip lines it up under the melody");
        play_scratch(NULL, 0);
        sounds_sub = 0;
        history_check();
    } else {
        check(1, "(eSpeak NG isn't installed here: the singing tests were skipped)");
    }

    /* New sound's Note box: drag it, scroll it, or click and type a note */
    {
        show_panel(RI_SOUNDS);
        sounds_sub = 3;
        sounds_scroll = 0;
        G.wave = WAVE_SAW;
        G.note = 60;
        frame();
        SDL_Rect r = ns_note_r;
        sim_drag(r.x + 20, r.y + 10, r.x + 20 + 40, r.y + 10, 0);
        check(G.note == 65, "dragging New sound's Note box changes the note");
        sim(r.x + 20, r.y + 10, 1, 0, 0, 0);
        sim(r.x + 20, r.y + 10, 0, 1, 0, 0);
        check(numedit.active && numedit.iv == &G.note && G.note == 65, "clicking the Note box opens it for typing");
        numedit_text("F#3");
        numedit_key((SDL_Keysym){.sym = SDLK_RETURN});
        frame();
        check(G.note == 54 && !numedit.active, "typing F#3 sets the note");
        sim(r.x + 20, r.y + 10, 1, 0, 0, 0);
        sim(r.x + 20, r.y + 10, 0, 1, 0, 0);
        numedit_text("bb2");
        numedit_key((SDL_Keysym){.sym = SDLK_RETURN});
        frame();
        int flat = G.note == 46;
        sim(r.x + 20, r.y + 10, 1, 0, 0, 0);
        sim(r.x + 20, r.y + 10, 0, 1, 0, 0);
        numedit_text("72");
        numedit_key((SDL_Keysym){.sym = SDLK_RETURN});
        frame();
        int num = G.note == 72;
        sim(r.x + 20, r.y + 10, 1, 0, 0, 0);
        sim(r.x + 20, r.y + 10, 0, 1, 0, 0);
        numedit_text("#5");
        numedit_key((SDL_Keysym){.sym = SDLK_RETURN});
        frame();
        check(flat && num && G.note == 72, "Bb2 and 72 work too, and something that isn't a note changes nothing");
        mx = r.x + 20;
        my = r.y + 10;
        wheel_y = 2;
        lpress = lrel = rpress = 0;
        frame();
        check(G.note == 74, "the mouse wheel steps the note");
        G.note = 45;
        sounds_sub = 0;
    }

    /* ---- copying sounds between the library, pads and the instrument */
    {
        show_panel(RI_PADS);
        frame();
        int a = -1, b = -1;
        for (int i = 0; i < P.nsamples; i++)
            if (!P.samples[i].pitched && !P.samples[i].removed) {
                if (a < 0) a = i;
                else if (b < 0) b = i;
            }
        LOCK();
        for (int i = 0; i < NPADS; i++) P.pads[i] = (Pad){-1, PAD_ONESHOT, 0.8f, 0, 0, 0};
        UNLOCK();
        history_check();
        sound_copy(CM_LIB, a);                  /* library to pad, by the right-click menu */
        SDL_Rect r = pad_rects[5];
        sim(r.x + 10, r.y + 10, 0, 0, 1, 0);
        int opened = cm.open && cm.kind == CM_PAD && cm.idx == 5 && cm.enabled[1];
        sim(cm.box.x + 14, cm.box.y + 4 + 1 * (font_h + 12) + 6, 1, 1, 0, 0);
        check(opened && !cm.open && P.pads[5].sample == a, "right-click a pad, Paste: it plays the copied library sound");
        LOCK();
        P.pads[5].pitch = 7;
        P.pads[5].mode = PAD_LOOP;
        UNLOCK();
        frame();
        mx = pad_rects[5].x + 10;               /* pad to pad with Ctrl+C / Ctrl+V: its settings too */
        my = pad_rects[5].y + 10;
        key((SDL_Keysym){.sym = SDLK_c, .mod = KMOD_CTRL});
        mx = pad_rects[9].x + 10;
        my = pad_rects[9].y + 10;
        key((SDL_Keysym){.sym = SDLK_v, .mod = KMOD_CTRL});
        check(P.pads[9].sample == a && P.pads[9].pitch == 7 && P.pads[9].mode == PAD_LOOP,
              "Ctrl+C on a pad and Ctrl+V on another copies the sound and the pad's settings");
        frame();
        history_check();
        show_panel(RI_SOUNDS);                  /* a pitched pad into the library: its own sound */
        sounds_sub = 0;
        frame();
        int ns = P.nsamples, nvis0 = 0, nvis1 = 0;
        for (int i = 0; i < P.nsamples; i++) nvis0 += (!P.samples[i].pitched || P.samples[i].listed) && !P.samples[i].removed;
        SDL_Rect pr = panel_rect();
        mx = pr.x + pr.w / 2;
        my = pr.y + pr.h - 80;
        key((SDL_Keysym){.sym = SDLK_v, .mod = KMOD_CTRL});
        int pasted = -1;
        for (int i = 0; i < P.nsamples; i++) {
            nvis1 += (!P.samples[i].pitched || P.samples[i].listed) && !P.samples[i].removed;
            if (P.samples[i].listed) pasted = i;
        }
        check(pasted >= 0 && P.samples[pasted].src == a && P.samples[pasted].semis == 7 && nvis1 == nvis0 + 1 &&
                  P.nsamples <= ns + 1,
              "pasting a pitched pad into the library adds that pitched sound to it");
        frame();
        history_check();
        undo_redo(0);
        int gone = !P.samples[pasted].listed;
        undo_redo(1);
        check(gone && P.samples[pasted].listed, "undo takes the pasted sound out of the library, redo puts it back");
        remove_sound(b);                        /* a removed sound comes back when pasted */
        frame();
        history_check();
        sclip = (typeof(sclip)){1, b, 0, {0}};
        sound_paste(CM_LIB, -1);
        check(!P.samples[b].removed, "pasting a removed sound brings it back into the library");
        lib_scroll = 0;
        frame();
        r = lib_rects[a];                       /* right-click a library sound: the menu */
        sim(r.x + 10, r.y + 5, 0, 0, 1, 0);
        int lib_menu = cm.open && cm.kind == CM_LIB && cm.idx == a && cm.act[cm.n - 1] == CMA_REMOVE;
        sim(cm.box.x - 50, cm.box.y - 50, 1, 1, 0, 0);    /* a click elsewhere closes it, doing nothing */
        check(lib_menu && !cm.open && !P.samples[a].removed, "right-clicking a library sound opens Copy, Paste, Remove");
        history_check();
    }

    /* ---- a tempo change: selected clips follow it */
    {
        double sr = SR;
        int ok_len = 0, ok_pitch = 0;
        long n = SR;                             /* time-stretch keeps the pitch */
        float *sine = malloc((size_t)n * 2 * sizeof(float));
        for (long i = 0; i < n; i++) sine[2 * i] = sine[2 * i + 1] = (float)(0.5 * sin(2 * 3.14159265358979 * 440 * i / sr));
        long sn;
        float *st = time_stretch(sine, n, 1.5, &sn);
        if (st) {
            ok_len = labs(sn - (long)(1.5 * n)) < 4;
            int zc = 0;
            double e = 0, emin = 1e9;
            for (long i = 1; i < sn; i++) zc += (st[2 * i] >= 0) != (st[2 * i - 2] >= 0);
            for (long b = SR / 10; b + SR / 50 < sn - SR / 10; b += SR / 50) {   /* no dips between frames */
                e = 0;
                for (long i = b; i < b + SR / 50; i++) e += st[2 * i] * st[2 * i];
                emin = fmin(emin, e / (SR / 50));
            }
            ok_pitch = fabs(zc / (sn / sr) / 2 - 440) < 6 && emin > 0.08;
        }
        free(st);
        check(ok_len && ok_pitch, "time-stretching makes it longer at the same pitch, without dips");

        new_project();
        P.bpm = 120;
        long beat = beat_frames();
        int raw = sample_add(&P, "recorded", sine, n);   /* audio with no recipe: stretched */
        sine = NULL;
        BeatSpec bs = genre_spec(0);
        int loop = sample_generate_beat(&P, &bs, 0x3F, "house beat");
        SynthSound y;
        synth_from_preset(&y, 0);
        int shot = sample_generate_synth(&P, &y, "Deep bass");
        int c_raw = clip_add(raw, 0, 4 * beat, 0, n);
        int c_loop = clip_add(loop, 1, 8 * beat, 0, P.samples[loop].frames);
        int c_shot = clip_add(shot, 2, 2 * beat, 0, beat);
        int c_keep = clip_add(shot, 3, 6 * beat, 0, beat);
        int c_note = clip_add(shot, 4, 4 * beat, 0, 4 * beat);
        int pat = pattern_new_id(&P);
        P.clips[c_note].pattern = pat;
        P.clips[c_note].root = 36;
        note_add(pat, 40, beat, beat, 0.8f);
        frame();
        history_check();
        sel_clear();
        csel[c_raw] = csel[c_loop] = csel[c_shot] = csel[c_note] = 1;
        sel_fix_primary();
        int u0 = nundo;
        double before = P.bpm;
        P.bpm = 100;                             /* as if typed into the BPM box */
        tempo_touch(before);
        tempo_at = SDL_GetTicks() - 1000;
        frame();
        if (drag.kind == D_NONE) history_check();
        double r = 1.2;
        const Sample *rs = &P.samples[P.clips[c_raw].sample], *ls = &P.samples[P.clips[c_loop].sample];
        check(P.clips[c_raw].start == (long)llround(4 * beat * r) && rs->pitched && fabs(rs->stretch - r) < 1e-9 &&
                  labs(rs->frames - (long)(n * r)) < 4 && labs(P.clips[c_raw].length - (long)llround(n * r)) <= 1,
              "selected audio keeps its bar and is time-stretched to the new tempo");
        check(P.clips[c_loop].start == (long)llround(8 * beat * r) && ls->is_beat == 1 && ls->gen_bpm == 100 && ls->hidden &&
                  labs(ls->frames - (long)llround(P.samples[loop].frames * r)) < 8,
              "a selected beat loop is made again at the new tempo");
        int ni = -1;
        for (int i = 0; i < P.nnotes; i++)
            if (P.notes[i].pattern == P.clips[c_note].pattern) ni = i;
        check(ni >= 0 && P.notes[ni].start == (long)llround(beat * r) && P.notes[ni].length == (long)llround(beat * r) &&
                  P.clips[c_note].length == (long)llround(4 * beat * r),
              "a selected note clip's notes move to the new tempo");
        check(P.clips[c_shot].sample == shot && P.clips[c_shot].start == (long)llround(2 * beat * r) &&
                  P.clips[c_keep].start == 6 * beat,
              "one-shots just move, and clips not selected keep their times");
        undo_redo(0);
        check(nundo == u0 && P.bpm == 120 && P.clips[c_raw].sample == raw && P.clips[c_loop].start == 8 * beat &&
                  P.notes[ni].start == beat,
              "one undo puts the tempo and the clips back");
        undo_redo(1);
        char msg[1200];
        int saved = project_save(&P, tpath, msg, sizeof msg) == 0;
        Project *U = malloc(sizeof *U);
        int loaded = saved && project_load(U, tpath, msg, sizeof msg) == 0;
        int si = P.clips[c_raw].sample, li = P.clips[c_loop].sample;
        check(loaded && fabs(U->samples[si].stretch - r) < 1e-9 && U->samples[si].frames == P.samples[si].frames &&
                  U->samples[li].hidden && U->samples[li].gen_bpm == 100,
              "stretched and remade sounds save as recipes and open the same");
        if (loaded) project_free(U);
        free(U);
        remove(tpath);
        sel_clear();
    }

    /* ---- chords from Theory into the piano roll; clips dropped on the library */
    {
        new_project();
        P.bpm = 120;
        long bar = 4 * beat_frames();
        int c = new_note_clip(0, 2 * bar);       /* opens the roll on an empty one-bar clip */
        P.playhead = 0;                          /* outside the clip: chords go after the last note */
        frame();
        history_check();
        int cmaj[3] = {48, 52, 55}, u0 = nundo;
        roll_put_chord(cmaj, 3, 4, "C");
        int first = 0, sel = 0;
        for (int i = 0; i < P.nnotes; i++)
            if (P.notes[i].pattern == roll_pattern) {
                first += P.notes[i].start == P.clips[c].offset && P.notes[i].length == bar;
                sel += nsel[i];
            }
        check(first == 3 && sel == 3 && P.playhead == P.clips[c].start + bar,
              "Roll puts a chord at the start of an empty note clip, selected, with the playhead after it");
        frame();
        history_check();
        const Progression *pr = &PROGRESSIONS[0];
        int ns[8][6], cnt[8], m = pr->n < 8 ? pr->n : 8;
        for (int k = 0; k < m; k++) cnt[k] = chord_notes(pr->root[k] % 12, pr->type[k], 48, ns[k]);
        roll_put_chords((const int (*)[6])ns, cnt, m, 4, pr->numerals);
        long last = 0;
        for (int i = 0; i < P.nnotes; i++)
            if (P.notes[i].pattern == roll_pattern && P.notes[i].start > last) last = P.notes[i].start;
        check(last == P.clips[c].offset + (long)m * bar && P.clips[c].length == (long)(m + 1) * bar,
              "a progression follows on, one chord a bar, and the clip grows to hold it");
        frame();
        history_check();
        undo_redo(0);
        undo_redo(0);
        int left = 0;
        for (int i = 0; i < P.nnotes; i++) left += P.notes[i].pattern == roll_pattern;
        check(nundo == u0 && left == 0, "each Roll is one undo step");
        roll_close();

        /* a clip dropped on the library: a new sound, with its track's effects, saved as a file */
        new_project();
        long n = SR / 2;
        float *saw = malloc((size_t)n * 2 * sizeof(float));
        for (long i = 0; i < n; i++) saw[2 * i] = saw[2 * i + 1] = (float)(0.5 * (2 * fmod(i * 220.0 / SR, 1.0) - 1));
        int raw = sample_add(&P, "buzz", saw, n);
        int cl = clip_add(raw, 0, beat_frames(), 0, n);
        P.tracks[0].filter = FILT_LOW;           /* an effect that's easy to measure */
        P.tracks[0].cutoff = 300;
        P.tracks[0].mute = 1;                    /* muted tracks still bounce */
        show_panel(RI_SOUNDS);
        sounds_sub = 0;
        view_start = 0;
        frame();
        SDL_Rect r = clip_rect(&P.clips[cl]), pr2 = panel_rect();
        long start0 = P.clips[cl].start;
        int nsamp = P.nsamples;
        sim_drag(r.x + r.w / 2, r.y + r.h / 2, pr2.x + pr2.w / 2, pr2.y + pr2.h / 2, 0);
        int got = P.nsamples == nsamp + 1 ? nsamp : -1;
        double hf_raw = 0, hf_new = 0, e_raw = 0, e_new = 0;   /* the share above 2 kHz */
        if (got >= 0) {
            const Sample *b = &P.samples[got], *o = &P.samples[raw];
            double kk = exp(-2 * 3.14159265358979 * 2000.0 / SR), l1 = 0, l2 = 0;
            for (long i = 0; i < n && i < b->frames; i++) {
                double x1 = o->data[2 * i], x2 = b->data[2 * i];
                l1 = x1 + (l1 - x1) * kk;
                l2 = x2 + (l2 - x2) * kk;
                hf_raw += (x1 - l1) * (x1 - l1);
                e_raw += x1 * x1;
                hf_new += (x2 - l2) * (x2 - l2);
                e_new += x2 * x2;
            }
        }
        check(got >= 0 && P.clips[cl].start == start0 && P.nclips == 1 && file_exists(P.samples[got].path) &&
                  labs(P.samples[got].frames - n) < SR / 100 && e_new > 0 && hf_new / e_new < 0.2 * hf_raw / e_raw,
              "dropping a clip on the library makes a new sound with its effects, saved as a file; the clip stays");
        if (got >= 0) remove(P.samples[got].path);
        new_project();
        make_demo();                             /* the tests after this expect the demo song */
        sounds_sub = 0;
    }

    /* ---- the clip editor window */
    {
        new_project();
        long n = 2 * SR;
        float *tone = malloc((size_t)n * 2 * sizeof(float));
        for (long i = 0; i < n; i++) tone[2 * i] = tone[2 * i + 1] = (float)(0.25 * sin(2 * 3.14159265358979 * 330 * i / (double)SR));
        int sm = sample_add(&P, "tone", tone, n);
        long bar = 4 * beat_frames();
        int c = clip_add(sm, 1, bar, 0, n);
        view_start = 0;
        frame();
        history_check();
        SDL_Rect r = clip_rect(&P.clips[c]);       /* double-click an audio clip: its editor opens */
        ldouble = 1;
        sim(r.x + r.w / 2, r.y + r.h / 2, 1, 1, 0, 0);
        ldouble = 0;
        check(ewin && ed_clip == c, "double-clicking an audio clip opens it in its own editor window");
        if (ewin) {
            ed_sim(-100, -100, 0, 0);
            Clip o = P.clips[c];
            int ex = ed_frame_to_x(o.offset + o.length), sx = ed_frame_to_x(o.offset), cy = ed_wave.y + ed_wave.h / 2;
            ed_sim(ex, cy, 1, 0);                  /* drag the end in */
            ed_sim(ex - 100, cy, 0, 0);
            ed_sim(ex - 100, cy, 0, 1);
            long len1 = P.clips[c].length;
            check(len1 < o.length - SR / 10 && P.clips[c].start == o.start && drag.kind == D_NONE,
                  "dragging the end trims the clip");
            ed_sim(sx, cy, 1, 0);                  /* and the start: the end stays where it was */
            ed_sim(sx + 60, cy, 0, 0);
            ed_sim(sx + 60, cy, 0, 1);
            Clip t = P.clips[c];
            check(t.offset > 0 && t.start == o.start + t.offset && t.start + t.length == o.start + len1,
                  "dragging the start trims it from the front, keeping the end");
            int fx = ed_frame_to_x(t.offset);
            ed_sim(fx, ed_wave.y, 1, 0);           /* the fade-in square */
            ed_sim(fx + 40, ed_wave.y, 0, 0);
            ed_sim(fx + 40, ed_wave.y, 0, 1);
            check(P.clips[c].fade_in > SR / 20, "dragging the square on top fades it in");
            SDL_Rect b = ed_btn[1];
            ed_sim(b.x + 5, b.y + 5, 1, 1);        /* Normalize: the loudest moment at -1 dB */
            check(fabs(P.clips[c].gain_db - (-1 - 20 * log10(0.25))) < 0.2, "Normalize sets the gain so the peak is -1 dB");
            frame();
            history_check();
            undo_redo(0);
            int undone = fabs(P.clips[c].gain_db) < 0.01;
            undo_redo(1);
            check(undone, "editor changes undo like any other");
            LOCK();
            P.playhead = P.clips[c].start + P.clips[c].length / 2;
            UNLOCK();
            Clip before = P.clips[c];
            int nc = P.nclips;
            ed_sim(-100, -100, 0, 0);
            b = ed_btn[3];
            ed_sim(b.x + 5, b.y + 5, 1, 1);        /* Split at the playhead */
            check(P.nclips == nc + 1 && P.clips[c].length + P.clips[nc].length == before.length &&
                      P.clips[nc].start == before.start + P.clips[c].length &&
                      P.clips[nc].offset == before.offset + P.clips[c].length,
                  "Split at the playhead makes two clips that play the same as one");
            ed_sim(-100, -100, 0, 0);
            b = ed_btn[5];
            ed_sim(b.x + 5, b.y + 5, 1, 1);        /* Delete: the clip goes, and so does the window */
            check(!ewin && P.nclips == nc, "Delete removes the clip and closes the editor");
        }
        editor_close();
        new_project();
        make_demo();
    }

    /* ---- MIDI keys record notes into a note clip; quantize and humanize */
    {
        new_project();
        P.bpm = 120;
        binds_default();
        int c4 = 60;
        P.inst_sample = chord_sample(&c4, 1, "C4", 4);
        P.inst_root = 60;
        frame();
        history_check();
        long beat = beat_frames(), bar = 4 * beat;
        int was_paused = !dev || SDL_GetAudioDeviceStatus(dev) == SDL_AUDIO_PAUSED;
        if (dev) SDL_PauseAudioDevice(dev, 1);  /* the playhead moves only when the test moves it */
        for (int pass = 0; pass < 2; pass++) {   /* quantized, then as played */
            rec_quantize = pass == 0;
            int nc = P.nclips, nn = P.nnotes, u0 = nundo;
            pads_rec = 1;
            LOCK();
            P.playing = 1;
            P.loop = 0;
            P.playhead = 2 * bar + beat + SR / 50;   /* 20 ms late */
            UNLOCK();
            frame();
            input_event(SRC_MIDI, 64, 100, 1, 0);
            frame();
            LOCK();
            P.playhead += beat / 2;
            UNLOCK();
            input_event(SRC_MIDI, 64, 0, 0, 0);
            frame();
            LOCK();
            P.playhead = 2 * bar + 2 * beat - SR / 100;   /* 10 ms early */
            UNLOCK();
            input_event(SRC_MIDI, 67, 50, 1, 0);
            frame();
            LOCK();
            P.playing = 0;                          /* stopped while 67 is still held */
            UNLOCK();
            pads_rec = 0;
            frame();
            int c = nc < P.nclips ? nc : -1, a = -1, b = -1;
            for (int i = nn; i < P.nnotes; i++) {
                if (P.notes[i].note == 64) a = i;
                if (P.notes[i].note == 67) b = i;
            }
            if (pass == 0) {
                check(c >= 0 && P.clips[c].pattern > 0 && P.clips[c].start == 2 * bar && P.nnotes == nn + 2 && a >= 0 &&
                          b >= 0 && P.notes[a].start == beat && P.notes[b].start == 2 * beat &&
                          fabs(P.notes[a].vel - 100 / 127.0) < 1e-3 && labs(P.notes[a].length - beat / 2) < SR / 100 &&
                          P.notes[b].length > 0,
                      "MIDI keys record notes into a note clip, quantized to the grid, with their velocities");
                check(nundo == u0 + 1 && P.clips[c].sample == sample_base(&P, P.inst_sample),
                      "the take is one undo step, and the clip plays the instrument");
                undo_redo(0);
                check(P.nclips == nc && P.nnotes == nn, "undo takes the recorded notes away");
            } else {
                check(c >= 0 && a >= 0 && P.notes[a].start == beat + SR / 50 && b >= 0 && P.notes[b].start == 2 * beat - SR / 100,
                      "with quantize off, notes keep the timing they were played with");
            }
            frame();
            history_check();
        }
        if (dev && !was_paused) SDL_PauseAudioDevice(dev, 0);
        rec_quantize = 1;
        int pat = pattern_new_id(&P);            /* humanize: small, varied nudges */
        for (int k = 0; k < 32; k++) note_add(pat, 60, k * beat, beat / 2, 0.8f);
        Note before[32];
        int first = P.nnotes - 32;
        memcpy(before, &P.notes[first], sizeof before);
        notes_humanize(pat, NULL, 12);
        int moved = 0, within = 1, vel = 0;
        for (int k = 0; k < 32; k++) {
            long d = P.notes[first + k].start - before[k].start;
            moved += d != 0;
            within &= labs(d) <= (long)(0.012 * SR) + 1 && fabsf(P.notes[first + k].vel - 0.8f) <= 0.8f * 0.12f + 1e-4f;
            vel += P.notes[first + k].vel != 0.8f;
        }
        check(moved > 20 && vel > 20 && within, "Humanize nudges timing (up to 12 ms) and velocity (up to 12%)");
        int q = notes_quantize(pat, 0, NULL, 1.0, 1.0), on = 0;
        for (int k = 0; k < 32; k++) on += P.notes[first + k].start == k * beat;
        check(q == 32 && on == 32, "Quantize puts them back on the grid");
        new_project();
        make_demo();
    }

    /* ---- MIDI files */
    {
        static const unsigned char smf[] = {      /* type 0, 96 ticks a beat: two channels in one track */
            'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 96,
            'M', 'T', 'r', 'k', 0, 0, 0, 44,
            0x00, 0xFF, 0x51, 0x03, 0x09, 0x27, 0xC0,  /* 600000 us a beat: 100 BPM */
            0x00, 0xFF, 0x58, 0x04, 3, 2, 24, 8,       /* 3/4 */
            0x00, 0x90, 60, 100,                       /* C4 on */
            0x60, 64, 90,                              /* a beat later, E4 on (running status) */
            0x00, 60, 0,                               /* C4 off (velocity 0) */
            0x00, 0x99, 36, 120,                       /* a kick on channel 10 */
            0x30, 0x89, 36, 0,                         /* half a beat later, its off */
            0x30, 0x80, 64, 0,                         /* E4 off */
            0x00, 0xFF, 0x2F, 0x00};
        char mpath[1100];
        const char *tmpd = getenv("TMPDIR") ? getenv("TMPDIR") : getenv("TEMP") ? getenv("TEMP") : "/tmp";
        snprintf(mpath, sizeof mpath, "%s/tinydaw-selftest-%u.mid", tmpd, (unsigned)SDL_GetTicks());
        FILE *mf = fopen(mpath, "wb");
        if (mf) {
            fwrite(smf, 1, sizeof smf, mf);
            fclose(mf);
        }
        static MfFile rf;
        char err[300];
        int ok = mf_read(mpath, &rf, err, sizeof err) == 0;
        const MfTrack *mel = NULL, *drum = NULL;
        for (int k = 0; ok && k < rf.ntracks; k++) {
            if (rf.tracks[k].channel == 0) mel = &rf.tracks[k];
            if (rf.tracks[k].channel == 9) drum = &rf.tracks[k];
        }
        check(ok && fabs(rf.bpm - 100) < 1e-6 && rf.num == 3 && rf.den == 4 && rf.ntracks == 2 && mel && mel->n == 2 &&
                  drum && drum->n == 1 && fabs(drum->notes[0].start - 1) < 1e-9 && fabs(drum->notes[0].len - 0.5) < 1e-9,
              "a MIDI file is read: tempo, time signature, running status, and each channel as its own part");
        int c4 = -1, e4 = -1;
        for (int i = 0; mel && i < mel->n; i++) {
            if (mel->notes[i].note == 60) c4 = i;
            if (mel->notes[i].note == 64) e4 = i;
        }
        check(c4 >= 0 && e4 >= 0 && fabs(mel->notes[c4].len - 1) < 1e-9 && mel->notes[c4].vel == 100 &&
                  fabs(mel->notes[e4].start - 1) < 1e-9 && fabs(mel->notes[e4].len - 1) < 1e-9,
              "note-on with velocity 0 ends a note, and notes keep their timing and velocity");
        mf_free(&rf);

        new_project();                            /* into an empty song: it takes the file's tempo */
        LOCK();
        P.playhead = 0;
        UNLOCK();
        int nc = P.nclips;
        midi_import(mpath);
        long beat = beat_frames();
        int pc = 0, kick = 0;
        for (int c = nc; c < P.nclips; c++)
            if (P.clips[c].pattern > 0) pc++;
        for (int i = 0; i < P.nnotes; i++) kick += P.notes[i].note == 36 && P.notes[i].start == beat && P.notes[i].length == beat / 2;
        check(fabs(P.bpm - 100) < 1e-6 && pc == 2 && kick == 1 && P.clips[nc].track != P.clips[nc + 1].track,
              "importing makes a note clip per part, on its own track, at the file's tempo");
        char opath[1100];
        snprintf(opath, sizeof opath, "%s/tinydaw-selftest-out-%u.mid", tmpd, (unsigned)SDL_GetTicks());
        midi_export(opath);                       /* and back out: the same notes */
        ok = mf_read(opath, &rf, err, sizeof err) == 0;
        int same = ok && rf.ntracks == 2 && fabs(rf.bpm - 100) < 1e-3;
        for (int k = 0; same && k < rf.ntracks; k++)
            for (int i = 0; i < rf.tracks[k].n; i++) {
                const MfNote *n = &rf.tracks[k].notes[i];
                int found = 0;
                for (int j = 0; j < P.nnotes && !found; j++)
                    found = P.notes[j].note == n->note && labs(P.notes[j].start - (long)llround(n->start * beat)) <= SR / 200 &&
                            abs((int)lroundf(P.notes[j].vel * 127) - n->vel) <= 1;
                same &= found;
            }
        check(same, "exporting MIDI writes the note clips back out, note for note");
        mf_free(&rf);
        remove(mpath);
        remove(opath);
        new_project();
        make_demo();
    }

    /* ---- time signatures and tempo changes */
    {
        new_project();
        P.bpm = 120;                             /* 4/4 at 120: a bar is 2 s */
        P.changes[0] = (MeterChange){2, 60, 3, 4};   /* from bar 3: 3/4 at 60, a bar is 3 s */
        P.changes[1] = (MeterChange){4, 120, 6, 8};  /* from bar 5: 6/8 at 120, a bar is 1.5 s */
        P.nchanges = 2;
        long s1 = SR;
        check(meter_bar_frame(&P, 2) == 4 * s1 && meter_bar_frame(&P, 3) == 7 * s1 && meter_bar_frame(&P, 4) == 10 * s1 &&
                  meter_bar_frame(&P, 5) == 11 * s1 + s1 / 2 && fabs(meter_bars(&P, 7 * s1) - 3) < 1e-9 &&
                  fabs(meter_beats(&P, 7 * s1) - 11) < 1e-9 && meter_beats_frame(&P, 11) == 7 * s1,
              "the tempo map turns bars, beats and frames into each other across changes");
        int num, den, bar;
        long bs, bl;
        meter_at(&P, 8 * s1, NULL, &num, &den, &bar, &bs, &bl);
        check(num == 3 && den == 4 && bar == 3 && bs == 7 * s1 && bl == 3 * s1 && meter_bpm_at(&P, 8 * s1) == 60,
              "what's in force at a moment: its time signature, bar, and tempo");
        snap_i = 1;                              /* snapping to bars, and to quarters from each bar line */
        long sb = snap(8 * s1 + s1 / 3);
        snap_i = 3;
        long sq = snap(10 * s1 + s1 / 4 + 10);   /* 6/8 at 120: quarters are 0.5 s from 10 s */
        check(sb == 7 * s1 && sq == 10 * s1 + s1 / 2, "snapping follows the bars and beats of each section");
        int kinds[16], nk = 0;
        long pos = 4 * s1;                       /* the clicks in a 3/4 bar: an accent and two beats */
        MET.sub = 1;
        MET.accent = 1;
        for (int i = 0; i < 4; i++) {
            int kd;
            long nx = next_click(pos, 0, &kd);
            kinds[nk++] = kd;
            if (i == 0 && nx != 4 * s1) kinds[0] = 9;
            pos = nx + 1;
        }
        check(kinds[0] == 0 && kinds[1] == 1 && kinds[2] == 1 && kinds[3] == 0, "the metronome counts three in a 3/4 bar");
        pos = 10 * s1;
        nk = 0;
        for (int i = 0; i < 7; i++) {
            int kd;
            long nx = next_click(pos, 0, &kd);
            kinds[nk++] = kd;
            pos = nx + 1;
        }
        check(kinds[0] == 0 && kinds[5] == 1 && kinds[6] == 0, "and six eighths in a 6/8 bar");
        LOCK();
        P.playhead = 6 * s1;
        UNLOCK();
        long st[3];
        for (int i = 0; i < 3; i++) {
            key((SDL_Keysym){.sym = SDLK_RIGHT, .mod = KMOD_CTRL});
            st[i] = P.playhead;
        }
        check(st[0] == 7 * s1 && st[1] == 10 * s1 && st[2] == 11 * s1 + s1 / 2, "Ctrl+Right steps bar to bar, whatever their length");
        frame();
        history_check();
        LOCK();
        P.playhead = 13 * s1;                    /* bar 7 (in 6/8): a new change there */
        UNLOCK();
        int k = change_add_here();
        check(k == 2 && P.nchanges == 3 && P.changes[2].bar == 6 && P.changes[2].num == 6 && P.changes[2].bpm == 120,
              "a change added at the playhead starts out as what's in force there");
        frame();
        history_check();
        undo_redo(0);
        check(P.nchanges == 2, "and undo takes it out");
        char msg[1200];
        P.ts_num = 3;
        P.ts_den = 4;
        int saved = project_save(&P, tpath, msg, sizeof msg) == 0;
        Project *U = malloc(sizeof *U);
        int loaded = saved && project_load(U, tpath, msg, sizeof msg) == 0;
        check(loaded && U->ts_num == 3 && U->ts_den == 4 && U->nchanges == 2 && U->changes[1].bar == 4 &&
                  U->changes[1].num == 6 && U->changes[1].den == 8 && U->changes[0].bpm == 60,
              "time signatures and tempo changes are saved with the project");
        if (loaded) project_free(U);
        free(U);
        remove(tpath);
        P.ts_num = P.ts_den = 4;
        int pat = pattern_new_id(&P);            /* and in MIDI files, both ways */
        int c4 = 60, sm = chord_sample(&c4, 1, "C4", 4);
        int cc = clip_add(sm, 0, 0, 0, meter_bar_frame(&P, 6));
        P.clips[cc].pattern = pat;
        note_add(pat, 60, 0, s1 / 2, 0.8f);
        note_add(pat, 64, meter_bar_frame(&P, 3), s1 / 2, 0.8f);   /* in the 3/4 part */
        note_add(pat, 67, meter_bar_frame(&P, 5), s1 / 4, 0.8f);   /* in the 6/8 part */
        const char *tmpd = getenv("TMPDIR") ? getenv("TMPDIR") : getenv("TEMP") ? getenv("TEMP") : "/tmp";
        char mp[1100];
        snprintf(mp, sizeof mp, "%s/tinydaw-selftest-meter-%u.mid", tmpd, (unsigned)SDL_GetTicks());
        midi_export(mp);
        new_project();
        P.playhead = 0;
        midi_import(mp);
        int g4 = -1, e4 = -1;
        for (int i = 0; i < P.nnotes; i++) {
            if (P.notes[i].note == 67) g4 = i;
            if (P.notes[i].note == 64) e4 = i;
        }
        check(P.nchanges == 2 && P.changes[0].bar == 2 && P.changes[0].num == 3 && P.changes[1].num == 6 &&
                  P.changes[1].den == 8 && e4 >= 0 && labs(P.notes[e4].start - 7 * s1) < SR / 200 && g4 >= 0 &&
                  labs(P.notes[g4].start - (11 * s1 + s1 / 2)) < SR / 200,
              "a MIDI file carries the tempo map out and back in, and notes land where they were");
        remove(mp);
        snap_i = 4;
        new_project();
        make_demo();
    }

    /* ---- loudness, the master compressor and limiter, loudness on export */
    {
        long n = 10L * SR;
        float *tone = malloc((size_t)n * 2 * sizeof(float));
        for (long i = 0; i < n; i++) tone[2 * i] = tone[2 * i + 1] = (float)(0.1 * sin(2 * 3.14159265358979 * 997 * i / (double)SR));
        double l1 = loudness_of(tone, n);
        check(fabs(l1 - (-20)) < 0.2, "a 997 Hz tone at -20 dBFS measures -20 LUFS");
        for (long i = n / 2; i < n; i++) tone[2 * i] = tone[2 * i + 1] = 0;   /* half of it silent */
        double l2 = loudness_of(tone, n);
        for (long i = 0; i < n; i++) tone[2 * i] = tone[2 * i + 1] = (float)(0.1 * sin(2 * 3.14159265358979 * 20 * i / (double)SR));
        double l3 = loudness_of(tone, n);
        check(fabs(l2 - (-20)) < 0.2 && l3 < -30, "silence doesn't pull the song's loudness down, and deep rumble counts for less");

        Project *T = malloc(sizeof *T);          /* the compressor and limiter, on a loud tone */
        project_init(T);
        Mixer *mm = mixer_new();
        for (long i = 0; i < n; i++) tone[2 * i] = tone[2 * i + 1] = (float)(1.8 * sin(2 * 3.14159265358979 * 220 * i / (double)SR));
        T->fx.limit_on = 1;
        T->fx.limit_ceiling = -1;
        long blk = 512;
        for (long pos = 0; pos < n; pos += blk) master_dynamics(mm, T, tone + 2 * pos, (int)(n - pos < blk ? n - pos : blk));
        float pk = 0;
        for (long i = 0; i < 2 * n; i++) pk = fmaxf(pk, fabsf(tone[i]));
        check(pk <= powf(10, -1 / 20.0f) + 1e-5f && pk > 0.85f, "the limiter never lets the sound past its ceiling");
        mixer_free(mm);
        mm = mixer_new();
        T->fx.limit_on = 0;
        T->fx.comp_on = 1;
        T->fx.comp_thresh = -18;
        T->fx.comp_ratio = 4;
        T->fx.comp_makeup = 0;
        for (long i = 0; i < n; i++) tone[2 * i] = tone[2 * i + 1] = (float)(0.5 * sin(2 * 3.14159265358979 * 220 * i / (double)SR));
        for (long pos = 0; pos < n; pos += blk) master_dynamics(mm, T, tone + 2 * pos, (int)(n - pos < blk ? n - pos : blk));
        pk = 0;
        for (long i = n / 2; i < n; i++) pk = fmaxf(pk, fabsf(tone[2 * i]));
        double want = 20 * log10(0.5) - (20 * log10(0.5) + 18) * 0.75;   /* -6 dB in, 12 dB over: 9 dB off */
        check(fabs(20 * log10(pk) - want) < 1.0, "the compressor turns what's over the threshold down by its ratio");
        mixer_free(mm);
        project_free(T);
        free(T);
        free(tone);

        new_project();                            /* a whole export, to -14 LUFS */
        make_demo();
        char opath[1100];
        const char *tmpd = getenv("TMPDIR") ? getenv("TMPDIR") : getenv("TEMP") ? getenv("TEMP") : "/tmp";
        snprintf(opath, sizeof opath, "%s/tinydaw-selftest-loud-%u", tmpd, (unsigned)SDL_GetTicks());
        ExportOptions eo;
        export_defaults(&eo);
        eo.format = EXP_WAV;
        eo.bits = 32;
        eo.loudness = 1;
        char msg[1500];
        int ok = project_export(&P, &eo, opath, msg, sizeof msg) == 0;
        char wav[1200];
        snprintf(wav, sizeof wav, "%s.wav", opath);
        Project *U = malloc(sizeof *U);
        project_init(U);
        char err[200];
        int si = ok ? sample_import(U, wav, err, sizeof err) : -1;
        double lu = si >= 0 ? loudness_of(U->samples[si].data, U->samples[si].frames) : 0;
        float upk = 0;
        for (long i = 0; si >= 0 && i < 2 * U->samples[si].frames; i++) upk = fmaxf(upk, fabsf(U->samples[si].data[i]));
        check(ok && si >= 0 && fabs(lu - (-14)) < 0.6 && upk <= powf(10, -1 / 20.0f) + 1e-4f,
              "exporting at -14 LUFS comes out at -14 LUFS, with peaks under -1 dB");
        project_free(U);
        free(U);
        remove(wav);
    }

    /* ---- mixing: faders in dB, per-track compressor and width, the console window */
    {
        new_project();
        make_demo();
        show_panel(RI_MIXER);
        mixer_sub = 0;
        frame();
        history_check();
        SDL_Rect r = lv_fader[1];
        int ux = r.x + (int)(r.w * sqrtf(1 / 1.5f));    /* drag it to the 0 dB mark */
        sim_drag(r.x + 5, r.y + r.h / 2, ux, r.y + r.h / 2, 0);
        check(fabsf(20 * log10f(P.tracks[1].volume)) < 0.3f, "dragging a fader to its 0 dB mark sets 0 dB");
        frame();
        history_check();
        undo_redo(0);
        check(fabsf(P.tracks[1].volume - 0.8f) < 1e-6f, "a fader move undoes");
        frame();
        r = lv_fader[2];
        ldouble = 1;
        sim(r.x + 10, r.y + r.h / 2, 1, 1, 0, 0);
        ldouble = 0;
        float v0 = P.tracks[2].volume;
        mx = r.x + 10;
        my = r.y + r.h / 2;
        wheel_y = 2;
        lpress = lrel = rpress = 0;
        frame();
        check(v0 == 1.0f && fabsf(20 * log10f(P.tracks[2].volume) - 1.0f) < 0.01f,
              "double-click puts a fader at 0 dB, and each wheel notch is half a dB");
        auto_add(3, AUTO_VOLUME, 0, 0.2f);       /* an automated track's fader won't move */
        float a0 = P.tracks[3].volume;
        frame();
        r = lv_fader[3];
        sim_drag(r.x + 5, r.y + r.h / 2, r.x + r.w - 5, r.y + r.h / 2, 0);
        check(P.tracks[3].volume == a0, "a fader whose volume is automated stays where the automation has it");

        Project *T = malloc(sizeof *T);          /* the track compressor and width, in rendered sound */
        project_init(T);
        long n = SR;
        float *d = malloc((size_t)n * 2 * sizeof(float));
        for (long i = 0; i < n; i++) {
            d[2 * i] = (float)(0.7 * sin(2 * 3.14159265358979 * 220 * i / SR));
            d[2 * i + 1] = (float)(0.7 * sin(2 * 3.14159265358979 * 330 * i / SR));
        }
        int sm = sample_add(T, "two tones", d, n);
        T->clips[0] = (Clip){sm, 0, 0, 0, n, 0, 0, 0, 0, 0, 0};
        T->nclips = 1;
        T->tracks[0].volume = 1;
        float *b0 = test_render(T, 0, n, 0);
        T->tracks[0].comp_on = 1;
        T->tracks[0].comp_thresh = -20;
        T->tracks[0].comp_ratio = 4;
        float *b1 = test_render(T, 0, n, 0);
        T->tracks[0].comp_on = 0;
        T->tracks[0].width = 0;
        float *b2 = test_render(T, 0, n, 0);
        double r0 = test_rms(b0, n / 2, n, 2), r1 = test_rms(b1, n / 2, n, 2), side = 0;
        for (long i = n / 2; i < n; i++) side += fabs(b2[2 * i] - b2[2 * i + 1]);
        check(r1 < r0 * 0.5, "a track's compressor turns a loud track down");
        check(side / (n / 2) < 1e-4, "width 0 makes a track mono");
        free(b0);
        free(b1);
        free(b2);
        T->tracks[0].width = 1.5f;
        T->tracks[0].comp_on = 1;
        char msg[1200];
        Project *U = malloc(sizeof *U);
        int ok = project_save(T, tpath, msg, sizeof msg) == 0 && project_load(U, tpath, msg, sizeof msg) == 0;
        check(ok && U->tracks[0].width == 1.5f && U->tracks[0].comp_on && U->tracks[0].comp_thresh == -20 &&
                  U->tracks[0].comp_ratio == 4,
              "width and compressor settings are saved with the project");
        if (ok) project_free(U);
        free(U);
        project_free(T);
        free(T);
        remove(tpath);

        frame();                                 /* the console, in its own window */
        sim(lv_fader[0].x, lv_fader[0].y - 80, 0, 0, 0, 0);
        mixwin_open();
        check(pops[POP_MIXER].win != NULL, "the mixing console opens in its own window");
        if (pops[POP_MIXER].win) {
            PopIn *in = &pops[POP_MIXER].in;
            in->mx = in->my = -100;
            mixwin_frame();
            SDL_Rect f = cs_fader[0];
            in->mx = f.x + f.w / 2;              /* drag track 1's fader to the bottom: silence */
            in->my = f.y + f.h / 2;
            in->lpress = in->ldown = 1;
            mixwin_frame();
            in->my = f.y + f.h + 20;
            mixwin_frame();
            in->lrel = 1;
            in->ldown = 0;
            mixwin_frame();
            check(P.tracks[0].volume == 0 && drag.kind == D_NONE, "a console fader drags, in the console window");
            SDL_Rect m = cs_mute[1];
            in->mx = m.x + 5;
            in->my = m.y + 5;
            in->lpress = 1;
            in->ldown = 1;
            mixwin_frame();
            in->lrel = 1;
            in->ldown = 0;
            mixwin_frame();
            check(P.tracks[1].mute == 1, "and its M mutes the track");
            pop_close(POP_MIXER);
        }
        mixer_sub = 0;
        new_project();
        make_demo();
    }

    /* ---- the interface's size, and tooltips */
    {
        frame();
        int w0 = W, h0 = H, tw0 = text_w("Play from the playhead"), fh0 = font_h;
        ui_set(150);
        frame();
        int ow, oh;
        SDL_GetWindowSizeInPixels(win, &ow, &oh);
        int tw1 = text_w("Play from the playhead");
        check(fabsf(ui_px - 1.5f) < 0.01f && ow == (int)(w0 * 1.5f) && W == w0 && H == h0 && font_pt == 19,
              "at 150% the window grows and the layout keeps its size in its own units");
        check(abs(tw1 - tw0) <= tw0 / 12 && abs(font_h - fh0) <= 1, "text measures the same at 150%, so nothing shifts or overlaps");
        ui_set(100);
        frame();
        SDL_GetWindowSizeInPixels(win, &ow, &oh);
        check(fabsf(ui_px - 1) < 0.01f && ow == w0 && W == w0, "and back at 100%");
        key((SDL_Keysym){.sym = SDLK_EQUALS, .mod = KMOD_CTRL});
        float up = ui_user;
        key((SDL_Keysym){.sym = SDLK_MINUS, .mod = KMOD_CTRL});
        check(fabsf(up - 1.25f) < 0.01f && fabsf(ui_user - 1) < 0.01f, "Ctrl+= and Ctrl+- step the size by a quarter");
        ui_choice = 0;
        ui_apply();
        ui_set(0);

        tips_on = 1;                             /* hold the mouse over the record button: its tooltip */
        frame();
        SDL_Rect tr = transport_rect;
        int bx = tr.x + 7 * 33 + 10, by = tr.y + tr.h / 2;
        tip_shown[0] = 0;
        sim(bx, by, 0, 0, 0, 0);
        int early = tip_shown[0] != 0;
        tip_seen.since = SDL_GetTicks() - 1000;
        sim(bx, by, 0, 0, 0, 0);
        check(!early && strstr(tip_shown, "Record") != NULL, "holding the mouse still over a button shows what it does, after a moment");
        tip_shown[0] = 0;
        sim(bx, by, 1, 0, 0, 0);                 /* a click puts it away */
        sim(bx, by, 0, 1, 0, 0);
        tip_shown[0] = 0;
        sim(bx, by, 0, 0, 0, 0);
        check(tip_shown[0] == 0, "and clicking puts it away");
        if (pads_rec) rec_toggle();
        LOCK();
        P.playing = 0;
        UNLOCK();
        countin_left = 0;
        tips_on = 0;
        tip_shown[0] = 0;
        sim(bx - 33, by, 0, 0, 0, 0);
        tip_seen.since = SDL_GetTicks() - 1000;
        sim(bx - 33, by, 0, 0, 0, 0);
        check(tip_shown[0] == 0, "with tooltips off, none show");
        tips_on = 1;
        frame();
        history_check();
    }

    /* ---- recording the microphone (a synthetic one, fed straight to the capture callback) */
    {
        new_project();
        make_demo();
        int was_paused = !dev || SDL_GetAudioDeviceStatus(dev) == SDL_AUDIO_PAUSED;
        if (dev) SDL_PauseAudioDevice(dev, 1);  /* the test moves the playhead itself */
        close_input();
        cap_fake = 1;
        if (!cap_ring) cap_ring = calloc((size_t)CAP_RING * 2, sizeof(float));
        cap_channels = 1;                        /* a mono microphone */
        cap_buffer = 512;
        out_buffer = 512;
        rec_offset_ms = -1;
        in_gain_db = 6;
        memset(track_arm, 0, sizeof track_arm);
        track_arm[2] = 1;
        frame();
        history_check();
        int u0 = nundo, nc = P.nclips, ns = P.nsamples, keep_countin = MET.countin;
        MET.countin = 0;
        long bar = 4 * beat_frames(), at = 2 * bar;
        LOCK();
        P.loop = 0;
        P.playhead = at;
        UNLOCK();
        rec_toggle();                            /* Rec: plays, and the armed track records */
        frame();
        int recording = mic.on;
        float chunk[512];
        long fed = 0;
        for (int k = 0; k < 94; k++) {           /* about a second of a 440 Hz tone at -12 dB */
            for (int i = 0; i < 512; i++, fed++) chunk[i] = 0.25f * (float)sin(2 * 3.14159265358979 * 440 * fed / SR);
            cap_cb(NULL, (Uint8 *)chunk, (int)sizeof chunk);
            LOCK();
            P.playhead += 512;
            UNLOCK();
            if (k % 4 == 3) frame();
        }
        rec_toggle();                            /* Rec again: the take ends */
        LOCK();
        P.playing = 0;
        UNLOCK();
        frame();
        int c = P.nclips > nc ? P.nclips - 1 : -1;
        long want = at - (long)llround((1024.0 / SR) * SR);   /* one input and one output buffer earlier */
        double rms = 0;
        const Sample *sm = c >= 0 ? &P.samples[P.clips[c].sample] : NULL;
        for (long i = SR / 10; sm && i < sm->frames - SR / 10; i++) rms += sm->data[2 * i] * sm->data[2 * i];
        if (sm) rms = sqrt(rms / (sm->frames - SR / 5));
        check(recording && c >= 0 && P.clips[c].track == 2 && labs(P.clips[c].start - want) <= 1 && P.nsamples == ns + 1 &&
                  labs(sm->frames - fed) <= 1,
              "Rec with a track armed records the microphone onto it, moved earlier by the buffers' delay");
        check(sm && fabs(rms - 0.5 / sqrt(2)) < 0.02 && sm->data[2 * 1000] == sm->data[2 * 1000 + 1] && file_exists(sm->path),
              "the take is what was sung, with the input gain, a mono mic on both sides, saved as a file");
        check(nundo == u0 + 1, "a microphone take is one undo step");
        char path[1024];
        snprintf(path, sizeof path, "%s", sm ? sm->path : "");
        undo_redo(0);
        check(P.nclips == nc, "and undo takes it away");
        remove(path);
        memset(track_arm, 0, sizeof track_arm);   /* nothing armed: Rec doesn't record the microphone */
        frame();
        nc = P.nclips;
        rec_toggle();
        frame();
        for (int k = 0; k < 20; k++) cap_cb(NULL, (Uint8 *)chunk, (int)sizeof chunk);
        frame();
        int on = mic.on;
        rec_toggle();
        LOCK();
        P.playing = 0;
        UNLOCK();
        frame();
        check(!on && P.nclips == nc, "with no track armed, Rec leaves the microphone alone");
        MET.countin = keep_countin;
        cap_fake = 0;
        cap_channels = 2;
        in_gain_db = 0;
        if (dev && !was_paused) SDL_PauseAudioDevice(dev, 0);
        new_project();
        make_demo();
    }

    /* ---- the Drums panel: step patterns */
    {
        new_project();
        frame();
        history_check();
        float *imp = calloc(2 * 400, sizeof(float));   /* a one-sample tick, easy to find in the output */
        imp[0] = imp[1] = 1;
        int smp = sample_add(&P, "tick", imp, 400), t = 1;
        sel_track = t;
        drum_cur = 0;
        show_panel(RI_DRUMS);
        frame();
        SDL_Rect a = dr_add;
        drag = (typeof(drag)){.kind = D_LIB, .sample = smp};
        sim(a.x + 10, a.y + 5, 0, 1, 0, 0);
        DrumPat *d = drum_find(&P, drum_cur);
        check(d && d->nrows == 1 && d->sample[0] == smp && d->track == t && d->steps == 16,
              "dropping a sound on the Drums panel starts a pattern on the track with it");
        frame();
        history_check();
        int u0 = nundo;
        SDL_Rect c0 = dr_cell[0][0], c4 = dr_cell[0][4];
        sim(c0.x + 2, c0.y + c0.h / 2, 1, 0, 0, 0);
        sim(c0.x + 2, c0.y + c0.h / 2, 0, 1, 0, 0);
        sim(c4.x + 2, c4.y + c4.h / 2, 1, 0, 0, 0);
        sim(c4.x + 2, c4.y + c4.h / 2, 0, 1, 0, 0);
        d = drum_find(&P, drum_cur);
        check(d && d->vel[0][0] == 100 && d->vel[0][4] == 100 && d->vel[0][1] == 0 && nundo == u0 + 2,
              "clicking steps adds hits, each one an undo step");
        SDL_Rect c8 = dr_cell[0][8], c11 = dr_cell[0][11];
        sim_drag(c8.x + 2, c8.y + c8.h / 2, c11.x + 2, c11.y + c11.h / 2, 0);
        d = drum_find(&P, drum_cur);
        check(d && d->vel[0][8] && d->vel[0][9] && d->vel[0][10] && d->vel[0][11] && !d->vel[0][12],
              "dragging along a row paints hits");
        sim_drag(c8.x + 2, c8.y + c8.h / 2, c11.x + 2, c11.y + c11.h / 2, 0);
        d = drum_find(&P, drum_cur);
        check(d && !d->vel[0][8] && !d->vel[0][11] && d->vel[0][4], "and starting on a hit paints them away");
        drum_mode = 1;                          /* velocity: low in the step = soft */
        frame();
        sim(c4.x + 2, c4.y + c4.h - 3, 1, 0, 0, 0);
        sim(c4.x + 2, c4.y + c4.h - 3, 0, 1, 0, 0);
        int soft = d->vel[0][4];
        drum_mode = 2;                          /* chance: half way = half the time */
        frame();
        sim(c0.x + 2, c0.y + c0.h / 2, 1, 0, 0, 0);
        sim(c0.x + 2, c0.y + c0.h / 2, 0, 1, 0, 0);
        int half = d->prob[0][0];
        drum_mode = 0;
        check(soft > 0 && soft < 40 && half > 35 && half < 65 && d->vel[0][0] == 100,
              "Velocity and Chance set a hit's strength and how often it plays");
        int plays = 0, again = 1;
        for (long k = 0; k < 1600; k += 16) {
            int pl = drum_plays(d, 0, k, 12345);
            plays += pl;
            again &= pl == drum_plays(d, 0, k, 12345);
        }
        check(plays > 100 * (half - 20) / 100 && plays < 100 * (half + 20) / 100 && again,
              "a step with a chance plays about that often, the same way every time");
        LOCK();                                 /* playback: the hits land on their steps */
        d->prob[0][0] = 100;
        d->vel[0][4] = 127;
        P.tracks[t].volume = 1;
        P.playhead = 0;
        UNLOCK();
        int c = drum_place(d, 2);
        Mixer *m = mixer_new();
        long bar = meter_bar_frame(&P, 1), n = 2 * bar + SR / 10;
        float *buf = calloc((size_t)n * 2, sizeof(float));
        for (long f = 0; f < n; f += 512) mixer_run(m, &P, f, 1, buf + 2 * f, (int)(n - f < 512 ? n - f : 512), t, 0);
        long s4 = meter_beats_frame(&P, 1.0), s2 = meter_beats_frame(&P, 0.5);
        float e0 = fabsf(buf[0]) + fabsf(buf[2]), e4 = 0, e2 = 0, e4b = 0;
        for (long f = s4 - 2; f <= s4 + 2; f++) e4 += fabsf(buf[2 * f]);
        for (long f = s2 - 2; f <= s2 + 2; f++) e2 += fabsf(buf[2 * f]);
        for (long f = bar + s4 - 2; f <= bar + s4 + 2; f++) e4b += fabsf(buf[2 * f]);
        check(c >= 0 && P.clips[c].pattern == -d->id && P.clips[c].length == 2 * bar && e0 > 0.3f && e4 > 0.3f &&
                  e2 < 0.01f && e4b > 0.3f,
              "a drum clip plays its hits on their steps, and repeats the pattern");
        free(buf);
        mixer_free(m);
        check(fabs(drum_step_beat(&(DrumPat){.swing = 0.5f, .steps = 16}, 1) - 0.375) < 1e-6,
              "swing puts the off-beat 16ths later");
        int nd = P.ndrums;
        DrumPat *st = drum_from_style(t, 0);    /* House, from Theory */
        check(st && P.ndrums == nd + 1 && st->nrows >= 3 && st->vel[0][0] > 0, "New from style makes the style's kit and beat");
        char tp[1100], msg2[256];
        const char *td = getenv("TMPDIR") ? getenv("TMPDIR") : getenv("TEMP") ? getenv("TEMP") : "/tmp";
        snprintf(tp, sizeof tp, "%s/tinydaw-drums-%u.tdaw", td, (unsigned)SDL_GetTicks() ^ (unsigned)time(NULL));
        LOCK();
        P.samples[smp].removed = 0;
        UNLOCK();
        int ok = project_save(&P, tp, msg2, sizeof msg2) == 0;
        Project *q = malloc(sizeof *q);
        ok = ok && project_load(q, tp, msg2, sizeof msg2) == 0;
        if (ok) {
            const DrumPat *qa = drum_find_c(q, st->id);
            ok = q->ndrums == P.ndrums && qa && qa->nrows == st->nrows && qa->steps == st->steps &&
                 !memcmp(qa->vel, st->vel, sizeof st->vel) && !strcmp(qa->name, st->name) && fabsf(qa->swing - st->swing) < 1e-5f;
            int qc = 0;
            for (int i = 0; i < q->nclips; i++) qc += q->clips[i].pattern < 0;
            ok = ok && qc == 1;
            project_free(q);
        }
        free(q);
        remove(tp);
        check(ok, "drum patterns and their clips save and open again");
        frame();
        history_check();
        int id = st->id;
        drum_delete(id);
        frame();
        history_check();
        check(!drum_find(&P, id), "Delete pattern takes it away");
        undo_redo(0);
        check(drum_find(&P, id) != NULL, "and undo brings it back");
        show_panel(RI_SOUNDS);
        new_project();
        make_demo();
        frame();
        history_check();
    }

    /* ---- saved and pinned sounds (in a folder of the test's own) */
    {
        const char *td = getenv("TMPDIR") ? getenv("TMPDIR") : getenv("TEMP") ? getenv("TEMP") : "/tmp";
        char root[700], sd[800];
        snprintf(root, sizeof root, "%s/tinydaw-saved-%u", td, (unsigned)SDL_GetTicks() ^ (unsigned)time(NULL));
        snprintf(sd, sizeof sd, "%s/Saved sounds", root);
        SDL_setenv("TINYDAW_SAVED_DIR", sd, 1);
        saved_enabled = 1;
        pins_read = 0;
        new_project();
        make_demo();
        show_panel(RI_SOUNDS);
        sounds_sub = 0;
        frame();
        history_check();
        int a = -1, b = -1;
        for (int i = 0; i < P.nsamples; i++)
            if (lib_rects[i].w) {
                if (a < 0) a = i;
                else if (b < 0) b = i;
            }
        char an[96], bn[96];
        snprintf(an, sizeof an, "%s", P.samples[a].name);
        snprintf(bn, sizeof bn, "%s", P.samples[b].name);
#define CM_PICK(k) do { SDL_Rect mi = {cm.box.x + 4, cm.box.y + 4 + (k) * (font_h + 12), cm.box.w - 8, font_h + 12}; \
                        sim(mi.x + 10, mi.y + 4, 1, 0, 0, 0); sim(mi.x + 10, mi.y + 4, 0, 1, 0, 0); } while (0)
        SDL_Rect r = lib_rects[b];
        sim(r.x + 30, r.y + 5, 0, 0, 1, 0);      /* right-click: Pin */
        int five = cm.open && cm.n == 6 && cm.act[2] == CMA_EDIT && cm.act[3] == CMA_PIN && cm.act[4] == CMA_SAVE &&
                   cm.act[5] == CMA_REMOVE;
        CM_PICK(3);
        frame();
        check(five && P.samples[b].pinned && P.samples[b].saved && file_exists(P.samples[b].path) && in_saved_dir(P.samples[b].path),
              "right-click > Pin keeps a sound and saves a copy of it");
        check(lib_rects[b].y < lib_rects[a].y, "a pinned sound is at the top of the library");
        r = lib_rects[a];
        sim(r.x + 30, r.y + 5, 0, 0, 1, 0);      /* right-click: Save */
        CM_PICK(4);
        check(P.samples[a].saved && !P.samples[a].pinned && file_exists(P.samples[a].path), "Save keeps a copy without pinning");
        new_project();                           /* a later session */
        frame();
        int qa = -1, qb = -1;
        for (int i = 0; i < P.nsamples; i++) {
            if (!strcmp(P.samples[i].name, an)) qa = i;
            if (!strcmp(P.samples[i].name, bn)) qb = i;
        }
        check(qa >= 0 && qb >= 0 && P.samples[qa].saved && !P.samples[qa].pinned && P.samples[qb].pinned && !is_dirty(),
              "saved and pinned sounds are in a new project's library, still pinned");
        char moved[1024];
        snprintf(moved, sizeof moved, "%s", qb >= 0 ? P.samples[qb].path : "");
        r = lib_rects[qb];
        sim(r.x + 30, r.y + 5, 0, 0, 1, 0);      /* right-click: Forget the saved copy */
        CM_PICK(4);
        int gone = qb >= 0 && !P.samples[qb].saved && !P.samples[qb].pinned && !file_exists(moved) && file_exists(P.samples[qb].path);
        char unsaved_file[1024];
        snprintf(unsaved_file, sizeof unsaved_file, "%s", P.samples[qb].path);
        new_project();
        int still = 0;
        for (int i = 0; i < P.nsamples; i++) still |= !strcmp(P.samples[i].name, bn);
        check(gone && !still, "Forget the saved copy takes it out of new projects, keeping the file elsewhere");
        for (int i = 0; i < P.nsamples; i++)     /* tidy up */
            if (P.samples[i].saved) {
                remove(P.samples[i].path);
                P.samples[i].saved = 0;
            }
        remove(unsaved_file);
        char f[1100];
        snprintf(f, sizeof f, "%s/pinned.txt", sd);
        remove(f);
        rmdir(sd);
        snprintf(f, sizeof f, "%s/Unsaved sounds", root);
        rmdir(f);
        rmdir(root);
#undef CM_PICK
        saved_enabled = 0;
        SDL_setenv("TINYDAW_SAVED_DIR", "", 1);
        new_project();
        make_demo();
        frame();
        history_check();
    }

    /* ---- a library sound in the clip editor */
    {
        new_project();
        make_demo();
        show_panel(RI_SOUNDS);
        sounds_sub = 0;
        frame();
        history_check();
        int a = -1;
        for (int i = 0; i < P.nsamples && a < 0; i++)
            if (lib_rects[i].w && P.samples[i].frames > SR / 2) a = i;
        long af = P.samples[a].frames;
        int ns0 = P.nsamples, nc0 = P.nclips;
        SDL_Rect r = lib_rects[a];
        sim(r.x + 30, r.y + 5, 0, 0, 1, 0);
        SDL_Rect mi = {cm.box.x + 4, cm.box.y + 4 + 2 * (font_h + 12), cm.box.w - 8, font_h + 12};
        int is_edit = cm.act[2] == CMA_EDIT;
        sim(mi.x + 10, mi.y + 4, 1, 0, 0, 0);
        sim(mi.x + 10, mi.y + 4, 0, 1, 0, 0);
        check(is_edit && ewin && ed_clip == ED_LIB && ed_lib.sample == a && ed_lib.length == af,
              "right-click > Open in the clip editor opens a library sound in the editor");
        ed_lib.offset = SR / 10;                 /* keep a part, quieter, faded out */
        ed_lib.length = SR / 4;
        ed_lib.gain_db = -6;
        ed_lib.fade_out = SR / 20;
        ed_sim(0, 0, 0, 0);
        SDL_Rect sb = ed_btn[2];
        ed_sim(sb.x + 5, sb.y + 5, 1, 0);
        ed_sim(sb.x + 5, sb.y + 5, 0, 1);
        int ns = P.nsamples - 1;
        float want = P.samples[a].data[2 * (SR / 10 + 100)] * powf(10, -6.0f / 20);
        check(P.nsamples == ns0 + 1 && P.samples[ns].frames == SR / 4 && fabsf(P.samples[ns].data[200] - want) < 1e-3f &&
                  fabsf(P.samples[ns].data[2 * (SR / 4 - 1)]) < 1e-3f && P.samples[a].frames == af && P.nclips == nc0 &&
                  file_exists(P.samples[ns].path),
              "Save as a new sound keeps the edited part as a sound of its own; the original and the song stay as they were");
        remove(P.samples[ns].path);
        editor_close();
        frame();
        history_check();
    }

    /* ---- velocity layers and round-robins */
    {
        new_project();
        frame();
        long n = SR / 2;                          /* white noise: easy to tell darker from brighter */
        float *nz = malloc((size_t)n * 2 * sizeof(float));
        unsigned seed = 1;
        for (long i = 0; i < n; i++) {
            seed = seed * 1664525u + 1013904223u;
            nz[2 * i] = nz[2 * i + 1] = ((seed >> 9) / 8388608.0f - 1) * 0.5f;
        }
        int s0 = sample_add(&P, "noise", nz, n), ns = P.nsamples;
        int ok = sample_make_layers(&P, s0, 3, 2) == 0;
        Sample *o = &P.samples[s0];
        check(ok && P.nsamples == ns + 5 && o->nvel == 3 && o->nrr == 2 && o->layer[2][0] == s0 &&
                  sample_pick(&P, s0, 1.0f, 0) == s0 && sample_pick(&P, s0, 1.0f, 1) != s0 &&
                  sample_pick(&P, s0, 0.1f, 0) == o->layer[0][0] && sample_pick(&P, s0, 0.5f, 0) == o->layer[1][0],
              "a sound gets 3 layers and 2 round-robins; hits pick by velocity and take the round-robins in turn");
        double hf[3];                             /* how bright each layer is: its first difference's energy */
        for (int l = 0; l < 3; l++) {
            const Sample *ls = &P.samples[o->layer[l][0]];
            double e = 0, d = 0;
            for (long i = 1; i < ls->frames; i++) {
                double v = ls->data[2 * i], dv = v - ls->data[2 * i - 2];
                e += v * v;
                d += dv * dv;
            }
            hf[l] = d / (e + 1e-12);
        }
        check(hf[0] < hf[1] && hf[1] < hf[2], "softer layers are darker");
        const Sample *rr1 = &P.samples[o->layer[2][1]];
        check(rr1->frames != o->frames || memcmp(rr1->data, o->data, (size_t)n * 2 * sizeof(float)),
              "a round-robin is a touch different from the sound itself");
        check(sample_make_layers(&P, s0, 1, 1) == 0 && o->nvel == 0 && sample_pick(&P, s0, 0.1f, 1) == s0,
              "1 layer and 1 round-robin goes back to the sound alone");
        sample_make_layers(&P, s0, 3, 2);
        /* a drum pattern plays them: two hits at full velocity aren't the same sound */
        DrumPat *d = drum_new(0, "rr");
        LOCK();
        d->nrows = 1;
        d->sample[0] = s0;
        d->vel[0][0] = d->vel[0][8] = 127;
        P.tracks[0].volume = 1;
        P.playhead = 0;
        UNLOCK();
        int c = drum_place(d, 1);
        Mixer *m = mixer_new();
        long len = meter_bar_frame(&P, 1);
        float *buf = calloc((size_t)len * 2, sizeof(float));
        for (long f = 0; f < len; f += 512) mixer_run(m, &P, f, 1, buf + 2 * f, (int)(len - f < 512 ? len - f : 512), 0, 0);
        long h2 = meter_beats_frame(&P, 2.0);
        double diff = 0, e1 = 0;
        for (long i = 0; i < SR / 10; i++) {
            diff += fabs(buf[2 * i] - buf[2 * (h2 + i)]);
            e1 += fabs(buf[2 * i]);
        }
        check(c >= 0 && e1 > 1 && diff > e1 * 0.05, "a drum pattern takes the round-robins in turn");
        free(buf);
        mixer_free(m);
        char tp[1100], msg2[256];
        const char *td = getenv("TMPDIR") ? getenv("TMPDIR") : getenv("TEMP") ? getenv("TEMP") : "/tmp";
        snprintf(tp, sizeof tp, "%s/tinydaw-layers-%u.tdaw", td, (unsigned)SDL_GetTicks() ^ (unsigned)time(NULL));
        int ks = hit_sample(&(BeatSpec){.kick_hz = 50, .kick_decay = 0.4, .snare_hz = 200, .hat_decay = 0.05,
                                        .open_decay = 0.3, .volume = 0.8, .steps = 16}, 0, "kick");
        check(ks >= 0 && P.samples[ks].nvel == 3 && P.samples[ks].nrr == 2, "drum kits come with layers");
        ok = project_save(&P, tp, msg2, sizeof msg2) == 0;
        Project *q = malloc(sizeof *q);
        ok = ok && project_load(q, tp, msg2, sizeof msg2) == 0;
        if (ok) {
            const Sample *qk = &q->samples[ks];
            ok = qk->nvel == 3 && qk->nrr == 2 && !memcmp(qk->layer, P.samples[ks].layer, sizeof qk->layer) &&
                 q->samples[qk->layer[0][1]].is_layer && q->samples[qk->layer[0][1]].frames == P.samples[P.samples[ks].layer[0][1]].frames;
            project_free(q);
        }
        free(q);
        remove(tp);
        check(ok, "layers save and open again, made the same way");
        new_project();
        make_demo();
        frame();
        history_check();
    }

    /* ---- the arranger: named sections, rearranging the song */
    {
        new_project();
        make_demo();
        LOCK();
        P.nclips = 0;
        P.nautos = 0;
        P.nchanges = 0;
        P.bpm = 120;
        P.ts_num = 4;
        P.ts_den = 4;
        UNLOCK();
        view_start = 0;
        pps = 15;
        frame();
        history_check();
        int s0 = 0;
        long bar = meter_bar_frame(&P, 1);
        int ci = sec_add(0, 2, "Intro"), vi = sec_add(2, 2, "Verse"), ki = sec_add(4, 2, "Chorus");
        int A = clip_add(s0, 0, 0, 0, P.samples[s0].frames < bar / 2 ? P.samples[s0].frames : bar / 2);
        int B = clip_add(s0, 1, 2 * bar + bar / 2, 0, 100);
        int C = clip_add(s0, 2, 4 * bar, 0, 100);
        (void)A, (void)B, (void)C;
        LOCK();
        P.changes[P.nchanges++] = (MeterChange){4, 140, 4, 4};   /* the chorus is faster */
        P.changes[P.nchanges++] = (MeterChange){6, 120, 4, 4};
        P.autos[P.nautos++] = (AutoPoint){0, AUTO_VOLUME, meter_bar_frame(&P, 4.5), 0.3f};
        UNLOCK();
        frame();
        history_check();
        int u0 = nundo;
        check(ci == 0 && vi == 1 && ki == 2 && P.nsections == 3, "sections are added in bar order");
        SDL_Rect kr = sec_rect_[2];
        sim(kr.x + 10, kr.y + 5, 1, 0, 0, 0);
        sim(kr.x + 10, kr.y + 5, 0, 1, 0, 0);
        check(P.playhead == meter_bar_frame(&P, 4), "clicking a section puts the playhead at its start");
        key((SDL_Keysym){.sym = SDLK_PAGEUP, .mod = KMOD_CTRL});
        int prev_ok = P.playhead == meter_bar_frame(&P, 2);
        key((SDL_Keysym){.sym = SDLK_PAGEDOWN, .mod = KMOD_CTRL});
        check(prev_ok && P.playhead == meter_bar_frame(&P, 4), "Ctrl+PageUp and Ctrl+PageDown jump between sections");
        frame();
        kr = sec_rect_[2];
        int x0 = (int)frame_to_x(0);
        sim_drag(kr.x + 10, kr.y + 5, x0 + 10, kr.y + 5, 0);   /* the chorus to the front */
        frame();
        history_check();
        int order = P.nsections == 3 && !strcmp(P.sections[0].name, "Chorus") && P.sections[0].bar == 0 &&
                    !strcmp(P.sections[1].name, "Intro") && P.sections[1].bar == 2 && !strcmp(P.sections[2].name, "Verse") &&
                    P.sections[2].bar == 4;
        int c0 = -1, c1 = -1, c2 = -1;
        for (int i = 0; i < P.nclips; i++) {
            if (P.clips[i].track == 0) c0 = i;
            if (P.clips[i].track == 1) c1 = i;
            if (P.clips[i].track == 2) c2 = i;
        }
        check(order && c2 >= 0 && P.clips[c2].start == 0 && c0 >= 0 && P.clips[c0].start == meter_bar_frame(&P, 2) &&
                  c1 >= 0 && labs(P.clips[c1].start - (meter_bar_frame(&P, 4) + (meter_bar_frame(&P, 5) - meter_bar_frame(&P, 4)) / 2)) <= 1,
              "dragging a section moves it and everything in it; what was between moves along");
        check(P.bpm == 140 && P.nchanges >= 1 && P.changes[0].bar == 2 && P.changes[0].bpm == 120 && P.nautos == 1 &&
                  fabs(meter_bars(&P, P.autos[0].at) - 0.5) < 1e-3,
              "the section takes its tempo and its automation with it");
        check(nundo == u0 + 1, "a move is one undo step");
        undo_redo(0);
        check(P.nsections == 3 && !strcmp(P.sections[2].name, "Chorus") && P.bpm == 120 && P.changes[0].bar == 4,
              "undo puts the song back as it was");
        int vb = 1;
        sec_action(CMA_SDUP, vb);                /* copy the verse in after itself */
        frame();
        history_check();
        int verses = 0, b_copies = 0;
        for (int i = 0; i < P.nsections; i++) verses += !strcmp(P.sections[i].name, "Verse");
        for (int i = 0; i < P.nclips; i++) b_copies += P.clips[i].track == 1;
        int chorus_at = -1;
        for (int i = 0; i < P.nsections; i++)
            if (!strcmp(P.sections[i].name, "Chorus")) chorus_at = P.sections[i].bar;
        check(verses == 2 && b_copies == 2 && chorus_at == 6, "Copy it in after itself repeats a section and moves the rest along");
        sec_action(CMA_SCUT, 2);                  /* and take the copy out again */
        frame();
        history_check();
        verses = b_copies = 0;
        for (int i = 0; i < P.nsections; i++) verses += !strcmp(P.sections[i].name, "Verse");
        for (int i = 0; i < P.nclips; i++) b_copies += P.clips[i].track == 1;
        check(verses == 1 && b_copies == 1 && P.sections[2].bar == 4, "Remove it and its bars takes a section out and closes the gap");
        int n0 = P.nsections;
        int xs = (int)frame_to_x(meter_bar_frame(&P, 8)), xe = (int)frame_to_x(meter_bar_frame(&P, 10));
        sim_drag(xs + 2, kr.y + 5, xe, kr.y + 5, 0);   /* drag along the empty strip: a new one */
        check(P.nsections == n0 + 1 && P.sections[n0].bar == 8 && P.sections[n0].bars == 2, "dragging along the strip marks a new section");
        char tp[1100], msg2[256];
        const char *td = getenv("TMPDIR") ? getenv("TMPDIR") : getenv("TEMP") ? getenv("TEMP") : "/tmp";
        snprintf(tp, sizeof tp, "%s/tinydaw-sections-%u.tdaw", td, (unsigned)SDL_GetTicks() ^ (unsigned)time(NULL));
        int ok = project_save(&P, tp, msg2, sizeof msg2) == 0;
        Project *q = malloc(sizeof *q);
        ok = ok && project_load(q, tp, msg2, sizeof msg2) == 0;
        if (ok) {
            ok = q->nsections == P.nsections && !memcmp(q->sections, P.sections, (size_t)P.nsections * sizeof(Section));
            project_free(q);
        }
        free(q);
        remove(tp);
        check(ok, "sections save and open again");
        new_project();
        make_demo();
        frame();
        history_check();
    }

    /* ---- the chord track and the song's key */
    {
        int ty, tb;
        float w[12] = {0};
        w[0] = w[4] = w[7] = 1;
        check(chord_guess(w, 0, &ty, &tb) == 0 && ty == CH_MAJ && tb == -1, "C, E and G are named C");
        memset(w, 0, sizeof w);
        w[9] = w[0] = w[4] = 1;
        check(chord_guess(w, 0, &ty, &tb) == 9 && ty == CH_MIN && tb == 0, "A, C and E over a low C are named Am/C");
        memset(w, 0, sizeof w);
        w[7] = w[11] = w[2] = w[5] = 1;
        check(chord_guess(w, 7, &ty, &tb) == 7 && ty == CH_DOM7, "G, B, D and F are named G7");
        memset(w, 0, sizeof w);
        check(chord_guess(w, -1, &ty, &tb) == -1, "silence names no chord");
        SongChord cc = {0, 4, 0, CH_MAJ, -1}, gc = {4, 4, 7, CH_MAJ, -1};
        int v1[8], v2[8], n1 = chord_voice(&cc, NULL, 0, 52, 72, v1), n2 = chord_voice(&gc, v1, n1, 52, 72, v2), moved = 0, fits = 1;
        for (int k = 0; k < n2; k++) {
            int d = 99;
            for (int j = 0; j < n1; j++) d = abs(v2[k] - v1[j]) < d ? abs(v2[k] - v1[j]) : d;
            moved += d;
            fits &= v2[k] >= 52 && v2[k] <= 72 && (v2[k] % 12 == 7 || v2[k] % 12 == 11 || v2[k] % 12 == 2);
        }
        check(n1 == 3 && n2 == 3 && fits && moved <= 4, "C to G voiced in range, each voice moving a step or less");

        new_project();
        make_demo();
        LOCK();
        P.nclips = 0;
        P.nautos = 0;
        P.nchanges = 0;
        P.bpm = 120;
        P.ts_num = 4;
        P.ts_den = 4;
        UNLOCK();
        view_start = 0;
        pps = 15;
        show_panel(RI_SOUNDS);
        frame();
        history_check();
        check(P.nchords == 0 && P.key_root == 0 && P.key_scale == 0, "a new song has no chords, in C major");
        int sy = L.top + L.ruler + L.arr + L.chd / 2;
        sim((int)frame_to_x(meter_bar_frame(&P, 1)) + 1, sy, 1, 0, 0, 0);   /* click the strip in bar 2 */
        sim((int)frame_to_x(meter_bar_frame(&P, 1)) + 1, sy, 0, 1, 0, 0);
        check(P.nchords == 1 && P.chords[0].at == 4 && P.chords[0].len == 4 && P.chords[0].root == 0 &&
                  P.chords[0].type == CH_MAJ && chp_open, "clicking the chord strip adds the key's I for the bar, picker open");
        key((SDL_Keysym){.sym = SDLK_ESCAPE});
        check(!chp_open, "Esc closes the chord picker");
        sim((int)frame_to_x(meter_bar_frame(&P, 2)) + 1, sy, 1, 0, 0, 0);
        sim((int)frame_to_x(meter_bar_frame(&P, 2)) + 1, sy, 0, 1, 0, 0);
        key((SDL_Keysym){.sym = SDLK_ESCAPE});
        check(P.nchords == 2 && P.chords[1].at == 8 && P.chords[1].root == 7 && P.chords[1].type == CH_MAJ,
              "the next chord suggested after I is V");
        playhead_to(meter_bar_frame(&P, 2));
        chd_add_here();
        key((SDL_Keysym){.sym = SDLK_ESCAPE});
        check(P.nchords == 3 && P.chords[2].at == 12 && P.chords[2].root == 9 && P.chords[2].type == CH_MIN,
              "+ in the header adds a chord after the one at the playhead: vi after V");
        frame();
        SDL_Rect ar = chd_rect_[2];
        sim_drag(ar.x + ar.w - 2, sy, (int)frame_to_x(meter_beats_frame(&P, 14)), sy, 0);
        check(fabs(P.chords[2].len - 2) < 1e-9, "dragging a chord's end resizes it");
        frame();
        history_check();
        int u0 = nundo;
        SDL_Rect cr = chd_rect_[0];
        int dx = (int)frame_to_x(meter_bar_frame(&P, 4)) - (int)frame_to_x(meter_bar_frame(&P, 1));
        sim_drag(cr.x + 4, sy, cr.x + 4 + dx, sy, 0);
        frame();
        history_check();
        int at16 = chord_at(&P, 16.5);
        check(chord_at(&P, 4.5) < 0 && at16 >= 0 && P.chords[at16].root == 0 && nundo == u0 + 1,
              "dragging a chord moves it, in one undo step");
        undo_redo(0);
        check(chord_at(&P, 4.5) >= 0 && chord_at(&P, 16.5) < 0, "undo puts the chord back");
        frame();
        cr = chd_rect_[chord_at(&P, 4.5)];
        sim(cr.x + 4, sy, 1, 0, 0, 0);           /* click it: selected */
        sim(cr.x + 4, sy, 0, 1, 0, 0);
        int n0 = P.nchords;
        key((SDL_Keysym){.sym = SDLK_DELETE});
        check(P.nchords == n0 - 1 && chord_at(&P, 4.5) < 0, "Delete with the pointer on the strip removes the selected chord");
        undo_redo(0);
        frame();
        history_check();

        /* keys: a change at bar 3 to D major, which Theory follows and edits */
        int k = key_change_add(&P, 2);
        key_set(&P, 2, 2, 0);
        int kr, ks;
        key_at_bar(&P, 1, &kr, &ks);
        int before_ok = kr == 0 && ks == 0;
        key_at_bar(&P, 5, &kr, &ks);
        check(k == 0 && before_ok && kr == 2 && ks == 0, "a key change holds from its bar on");
        char lab[32];
        chord_numeral(&P, &P.chords[1], lab, sizeof lab);
        check(!strcmp(lab, "IV"), "G in D major is IV");
        chord_label(&P, &(SongChord){0, 1, 9, CH_MAJ, 1}, lab, sizeof lab);
        check(!strcmp(lab, "A/C#"), "slash chords are named with their bass note");
        playhead_to(meter_bar_frame(&P, 3));
        frame();
        check(th_root == 2 && th_scale == 0, "Theory shows the key where the playhead is");
        th_root = 4;                              /* changed in Theory: E */
        frame();
        check(P.nkeys == 1 && P.keys[0].root == 4 && P.key_root == 0, "changing the key in Theory changes that stretch's key");
        playhead_to(0);
        frame();
        check(th_root == 0, "and Theory follows the playhead back");
        th_root = 0;
        frame();

        /* rearranging the song carries the chords and keys along */
        song_edit((SongEdit){SE_MOVE, 2, 3, 0});  /* bar 3 (G, the key change) to the front */
        frame();
        history_check();
        int g0 = chord_at(&P, 0.5), c1 = chord_at(&P, 8.5);
        key_at_bar(&P, 0, &kr, &ks);
        int k0 = kr;
        key_at_bar(&P, 1, &kr, &ks);
        check(g0 >= 0 && P.chords[g0].root == 7 && c1 >= 0 && P.chords[c1].root == 0 && k0 == 4 && kr == 0,
              "moving a section takes its chords and its key with it");
        undo_redo(0);

        /* saving */
        char tp[1100], msg2[256];
        const char *td = getenv("TMPDIR") ? getenv("TMPDIR") : getenv("TEMP") ? getenv("TEMP") : "/tmp";
        snprintf(tp, sizeof tp, "%s/tinydaw-chords-%u.tdaw", td, (unsigned)SDL_GetTicks() ^ (unsigned)time(NULL));
        int ok = project_save(&P, tp, msg2, sizeof msg2) == 0;
        Project *q = malloc(sizeof *q);
        ok = ok && project_load(q, tp, msg2, sizeof msg2) == 0;
        if (ok) {
            ok = q->nchords == P.nchords && q->nkeys == P.nkeys && q->key_root == P.key_root && q->key_scale == P.key_scale &&
                 !memcmp(q->keys, P.keys, (size_t)P.nkeys * sizeof(KeyChange));
            for (int i = 0; ok && i < P.nchords; i++)
                ok = fabs(q->chords[i].at - P.chords[i].at) < 1e-6 && fabs(q->chords[i].len - P.chords[i].len) < 1e-6 &&
                     q->chords[i].root == P.chords[i].root && q->chords[i].type == P.chords[i].type && q->chords[i].bass == P.chords[i].bass;
            project_free(q);
        }
        free(q);
        remove(tp);
        check(ok, "chords and key changes save and open again");

        /* a part from the chords */
        int nc0 = P.nclips, nn0 = P.nnotes, pc = chord_part();
        int notes_ok = pc >= 0 && P.clips[pc].pattern > 0 && P.clips[pc].start == meter_bar_frame(&P, 1), cnt = 0;
        for (int i = nn0; i < P.nnotes; i++) {
            const Note *n = &P.notes[i];
            if (n->pattern != P.clips[pc].pattern) continue;
            cnt++;
            int ci2 = chord_at(&P, meter_beats(&P, P.clips[pc].start + n->start) + 0.01);
            const ChordType *ct = &CHORDS[P.chords[ci2 >= 0 ? ci2 : 0].type];
            int in = 0;
            for (int j = 0; j < ct->n; j++) in |= (P.chords[ci2].root + ct->iv[j]) % 12 == n->note % 12;
            notes_ok &= ci2 >= 0 && in && n->note >= 52 && n->note <= 72;
        }
        check(P.nclips == nc0 + 1 && notes_ok && cnt == 9, "Make a part writes each chord's notes, in range, into a new note clip");

        /* chords found in a note clip: a bar of Am, then a bar of F over A */
        P.nchords = 0;
        LOCK();
        int pat = P.clips[pc].pattern;
        int w2 = 0;
        for (int i = 0; i < P.nnotes; i++)
            if (P.notes[i].pattern != pat) P.notes[w2++] = P.notes[i];
        P.nnotes = w2;
        long b4 = meter_bar_frame(&P, 2) - meter_bar_frame(&P, 1);
        int am[3] = {57, 60, 64}, fa[3] = {57, 60, 65};
        for (int j = 0; j < 3; j++) {
            P.notes[P.nnotes++] = (Note){pat, am[j], 0, b4, 0.8f};
            P.notes[P.nnotes++] = (Note){pat, fa[j], b4, b4, 0.8f};
        }
        P.clips[pc].length = 2 * b4;
        UNLOCK();
        selected = pc;
        int found = chords_from_clip(pc);
        int a0 = chord_at(&P, 4.5), f0 = chord_at(&P, 8.5);
        check(found == 2 && a0 >= 0 && P.chords[a0].root == 9 && P.chords[a0].type == CH_MIN && fabs(P.chords[a0].len - 4) < 1e-9 &&
                  f0 >= 0 && P.chords[f0].root == 5 && P.chords[f0].type == CH_MAJ && P.chords[f0].bass == 9,
              "chords are found in a note clip, a bar each when they hold: Am, then F/A");
        chp_idx = a0;
        chp_open = 1;
        frame();
        frame();
        check(chp_open && chp_box.w > 0, "the chord picker opens over a chord");
        key((SDL_Keysym){.sym = SDLK_ESCAPE});

        /* Theory > Keys > Place puts the progression on the chord track too */
        P.nchords = 0;
        P.nkeys = 0;
        th_root = 9;
        playhead_to(meter_bar_frame(&P, 4));
        place_progression(&PROGRESSIONS[1]);      /* I - V - vi - IV in A: A E F#m D */
        check(P.nchords == 4 && P.chords[0].at == 16 && P.chords[0].root == 9 && P.chords[1].root == 4 && P.chords[2].root == 6 &&
                  P.chords[2].type == CH_MIN && P.chords[3].root == 2, "Place fills the chord track from the playhead's bar");
        new_project();
        make_demo();
        frame();
        history_check();
    }

    /* ---- Create > Parts: chords, bass and melody written from the chord track, and drum fills */
    {
        new_project();
        make_demo();
        LOCK();
        P.nclips = 0;
        P.nnotes = 0;
        P.nautos = 0;
        P.nchanges = 0;
        P.ndrums = 0;
        P.bpm = 120;
        P.ts_num = P.ts_den = 4;
        UNLOCK();
        static const int R[4] = {0, 7, 9, 5}, T[4] = {CH_MAJ, CH_MAJ, CH_MIN, CH_MAJ};   /* C G Am F, twice */
        for (int b = 0; b < 8; b++) chord_put(&P, b * 4, 4, R[b % 4], T[b % 4], -1);
        static PartNote pn[4096], pn2[4096];
        static const int LO[3] = {52, 28, 60}, HI[3] = {72, 55, 81};
        int all_ok = 1, total = 0;
        for (int k = 0; k < PART_KINDS; k++)
            for (int st = 0; st < PART_NSTYLES[k]; st++) {
                PartSpec ps = {k, st, 0, 32, LO[k], HI[k], 0.5, 3};
                int n = part_write(&P, &ps, pn, 4096);
                total += n;
                int ok = n > 0;
                for (int i = 0; i < n; i++) {
                    const PartNote *q = &pn[i];
                    int ci = chord_at(&P, q->at + 1e-6), pc = q->note % 12;
                    int in_chord = 0;
                    if (ci >= 0)
                        for (int j = 0; j < CHORDS[P.chords[ci].type].n; j++) in_chord |= (P.chords[ci].root + CHORDS[P.chords[ci].type].iv[j]) % 12 == pc;
                    int in_key = pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11;
                    int walk_step = k == PART_BASS && st == 4;   /* walking bass leads in chromatically */
                    ok &= q->note >= LO[k] && q->note <= HI[k] && q->at >= 0 && q->at < 32 && q->len >= 0.05 &&
                          (k == PART_MELODY ? in_key : walk_step ? 1 : in_chord);
                }
                if (!ok) fprintf(stderr, "part %d style %s: %d notes, out of range or off the chords\n", k, PART_STYLES[k][st], n);
                all_ok &= ok;
            }
        check(all_ok && total > 300, "every style of chords, bass and melody writes notes in range, on the chords (melodies: in the key)");
        PartSpec ms = {PART_MELODY, 0, 0, 32, 60, 81, 0.6, 5};
        int n1 = part_write(&P, &ms, pn, 4096), n2 = part_write(&P, &ms, pn2, 4096);
        int same = n1 == n2 && !memcmp(pn, pn2, sizeof(PartNote) * (size_t)n1);
        ms.seed = 6;
        n2 = part_write(&P, &ms, pn2, 4096);
        check(same && (n1 != n2 || memcmp(pn, pn2, sizeof(PartNote) * (size_t)n1)), "the same seed writes the same melody, the next a different one");
        int strong_ok = 1, end_ok = 0;
        for (int i = 0; i < n1; i++) {
            double inbar = fmod(pn[i].at, 4);
            int ci = chord_at(&P, pn[i].at + 1e-6), pc = pn[i].note % 12, tone = 0;
            for (int j = 0; j < CHORDS[P.chords[ci].type].n; j++) tone |= (P.chords[ci].root + CHORDS[P.chords[ci].type].iv[j]) % 12 == pc;
            if (fabs(inbar) < 1e-6 || fabs(inbar - 2) < 1e-6) strong_ok &= tone;
            if (pn[i].at >= 12 && pn[i].at < 16) end_ok = pc == 5 || pc == 9;   /* the phrase's last note, over F */
        }
        check(strong_ok && end_ok, "melodies land on chord tones on the strong beats and end a phrase on the root or third");
        PartSpec ws = {PART_BASS, 4, 0, 32, 28, 55, 0.5, 1};
        int nw = part_write(&P, &ws, pn, 4096), lead_ok = nw == 32;
        for (int i = 0; i < nw; i++)
            if (fabs(fmod(pn[i].at, 4) - 3) < 1e-6 && pn[i].at < 28) {
                int next = R[((int)(pn[i].at / 4) + 1) % 4], d = ((pn[i].note - next) % 12 + 12) % 12;
                lead_ok &= d == 1 || d == 11 || d == 2 || d == 10;
            }
        check(lead_ok, "a walking bass moves in quarters and leads into each new chord from a step away");
        LOCK();
        P.key_root = 7;                          /* G major: G C D Em, and the melody uses F# */
        UNLOCK();
        P.nchords = 0;
        static const int R2[4] = {7, 0, 2, 4}, T2[4] = {CH_MAJ, CH_MAJ, CH_MAJ, CH_MIN};
        for (int b = 0; b < 8; b++) chord_put(&P, b * 4, 4, R2[b % 4], T2[b % 4], -1);
        PartSpec gs = {PART_MELODY, 1, 0, 32, 60, 81, 0.8, 2};
        int ng = part_write(&P, &gs, pn, 4096), no_f = ng > 0;
        for (int i = 0; i < ng; i++) no_f &= pn[i].note % 12 != 5;
        check(no_f, "a melody follows the song's key (no F natural in G major)");
        P.key_root = 0;
        P.nchords = 0;
        for (int b = 0; b < 8; b++) chord_put(&P, b * 4, 4, R[b % 4], T[b % 4], -1);

        /* from the panel: Make, then Try another in the same clip */
        frame();
        history_check();
        int c0 = P.nclips;
        pt_where = 0;
        pt_style[PART_MELODY] = 0;
        pt_sound[PART_MELODY] = 1;
        int mc = part_make(PART_MELODY, 0);
        int nmel = 0;
        for (int i = 0; i < P.nnotes; i++) nmel += mc >= 0 && P.notes[i].pattern == P.clips[mc].pattern;
        check(mc >= 0 && P.nclips == c0 + 1 && P.clips[mc].start == 0 && P.clips[mc].length == meter_bar_frame(&P, 8) && nmel > 8 &&
                  !strcmp(P.samples[P.clips[mc].sample].name, SYNTH_PRESETS[pt_pre[PART_MELODY][0]].name),
              "Make writes the melody into a new note clip over the chords, on a Generator sound");
        frame();
        history_check();
        int u0 = nundo;
        pt_seed[PART_MELODY]++;
        int mc2 = part_make(PART_MELODY, 1);
        frame();
        history_check();
        check(mc2 == mc && P.nclips == c0 + 1 && nundo == u0 + 1, "Try another writes it again in the same clip, in one undo step");
        pt_where = 2;                            /* the section at the playhead */
        sec_add(4, 4, "Verse");
        playhead_to(meter_bar_frame(&P, 5));
        int bc = part_make(PART_BASS, 0);
        check(bc >= 0 && P.clips[bc].start == meter_bar_frame(&P, 4) && P.clips[bc].length == meter_bar_frame(&P, 8) - meter_bar_frame(&P, 4),
              "This section writes the part over the section the playhead is in");
        pt_where = 0;

        /* fills: before each section, in a drum clip's pattern */
        int ks = -1, ss = -1, hs = -1;
        for (int i = 0; i < P.nsamples; i++) {
            if (ks < 0 && strstr(P.samples[i].name, "kick")) ks = i;
            if (ss < 0 && strstr(P.samples[i].name, "snare")) ss = i;
            if (hs < 0 && strstr(P.samples[i].name, "hat")) hs = i;
        }
        DrumPat d;
        memset(&d, 0, sizeof d);
        d.id = 1;
        d.track = 0;
        snprintf(d.name, sizeof d.name, "Beat");
        d.steps = 16;
        d.nrows = 3;
        d.sample[0] = ks, d.sample[1] = ss, d.sample[2] = hs;
        for (int r = 0; r < 3; r++) {
            d.row_vol[r] = 1;
            for (int k = 0; k < 16; k++) d.prob[r][k] = 100;
        }
        d.vel[0][0] = d.vel[0][8] = 120;
        d.vel[1][4] = d.vel[1][12] = 110;
        for (int k = 0; k < 16; k += 2) d.vel[2][k] = 80;
        LOCK();
        for (int c = P.nclips - 1; c >= 0; c--)
            if (P.clips[c].track == 0) P.clips[c] = P.clips[--P.nclips];
        P.drums[P.ndrums++] = d;
        UNLOCK();
        int dc = clip_add(ks, 0, 0, 0, meter_bar_frame(&P, 8));
        P.clips[dc].pattern = -1;
        frame();
        history_check();
        fl_where = 0;
        fl_style = FILL_ROLL;
        fl_busy = 0.5;
        fl_track = 0;
        int made = fills_make(0);
        int on0 = 0, fill_clip = -1;
        for (int c = 0; c < P.nclips; c++)
            if (P.clips[c].track == 0) {
                on0++;
                if (P.clips[c].start == meter_bar_frame(&P, 3)) fill_clip = c;
            }
        const DrumPat *fp = fill_clip >= 0 ? drum_find_c(&P, -P.clips[fill_clip].pattern) : NULL;
        int roll = fp && !strcmp(fp->name, "Fill, bar 4") && fp->vel[2][0] == 80 && fp->vel[2][6] == 80 && fp->vel[2][8] == 0 &&
                   fp->vel[1][12] && fp->vel[1][13] && fp->vel[1][14] && fp->vel[1][15] > fp->vel[1][12] && !fp->vel[1][9];
        check(made == 1 && on0 == 3 && fill_clip >= 0 && P.clips[fill_clip].length == meter_bar_frame(&P, 4) - meter_bar_frame(&P, 3) && roll,
              "a fill goes in the bar before the section: the pattern's first half as it was, then a snare roll rising to the end");
        int nd = P.ndrums;
        fl_seed++;
        fl_style = FILL_DROP;
        fills_make(1);
        fp = drum_find_c(&P, -P.clips[fill_clip].pattern);
        check(P.ndrums == nd && fp && !fp->vel[1][12] && fp->vel[1][15] && !fp->vel[0][8], "Try another writes the fills again in place");
        show_panel(RI_SOUNDS);
        sounds_sub = 5;
        for (int k = 0; k < 4; k++) {
            pt_kind = k;
            frame();
        }
        check(sounds_content_h > 200, "the Parts tab draws each kind of part");
        pt_kind = 0;
        sounds_sub = 0;
        new_project();
        make_demo();
        frame();
        history_check();
    }

    /* ---- pitch curves on any clip (Mixer > Clip > Pitch bend) */
    {
        Bend tb;
        memset(&tb, 0, sizeof tb);
        tb.n = 2;
        tb.at[0] = 1, tb.semis[0] = 0, tb.at[1] = 2, tb.semis[1] = 12;
        check(bend_at(&tb, 0.5) == 0 && fabs(bend_at(&tb, 1.5) - 6) < 1e-5 && bend_at(&tb, 3) == 12,
              "a pitch curve runs in straight lines between its points and holds past the ends");
        new_project();
        LOCK();
        P.nclips = P.nnotes = P.nautos = P.nchanges = P.ndrums = 0;
        P.bpm = 120;
        P.ts_num = P.ts_den = 4;
        UNLOCK();
        int sine = gen(WAVE_SINE, 57, 4, 0.5, 0, 0);    /* A3, 220 Hz, two seconds */
        long sec = SR;
#define ZC(buf, from, to) ({ int z_ = 0; for (long i_ = (from) + 1; i_ < (to); i_++) z_ += ((buf)[2 * i_] >= 0) != ((buf)[2 * i_ - 2] >= 0); z_; })
        /* a note clip, straight and then bent up an octave */
        int nc = clip_add(sine, 0, 0, 0, 2 * sec);
        LOCK();
        P.clips[nc].pattern = pattern_new_id(&P);
        P.clips[nc].root = 57;
        P.notes[P.nnotes++] = (Note){P.clips[nc].pattern, 57, 0, (long)(1.5 * sec), 0.8f};
        UNLOCK();
        float *r0 = test_render(&P, 0, 2 * sec, 0);
        int z0 = ZC(r0, sec / 4, (3 * sec) / 4);
        int bid = bend_new(&P);
        Bend *bb = bend_find(&P, bid);
        bb->n = 1;
        bb->at[0] = 0, bb->semis[0] = 12;
        P.clips[nc].bend = bid;
        float *r1 = test_render(&P, 0, 2 * sec, 0);
        int z1 = ZC(r1, sec / 4, (3 * sec) / 4);
        check(z0 > 200 && z0 < 240 && fabs((double)z1 / z0 - 2) < 0.06, "a note clip bent up 12 semitones sounds an octave higher");
        free(r0);
        free(r1);
        bb->n = 2;                               /* a slide from straight to an octave up */
        bb->at[0] = 0.25f, bb->semis[0] = 0, bb->at[1] = 0.75f, bb->semis[1] = 12;
        float *r2 = test_render(&P, 0, 2 * sec, 0);
        int za = ZC(r2, 0, sec / 4), zb = ZC(r2, sec, (5 * sec) / 4);
        check(fabs((double)zb / za - 2) < 0.08, "a curve slides the pitch from one place to another");
        free(r2);
        /* an audio clip: tape, and keeping the timing */
        LOCK();
        P.nclips = 0;
        P.nnotes = 0;
        UNLOCK();
        int ac = clip_add(sine, 0, 0, 0, P.samples[sine].frames);
        int ab = bend_new(&P);
        Bend *abp = bend_find(&P, ab);
        abp->n = 1;
        abp->at[0] = 0, abp->semis[0] = 12;
        abp->keep_time = 0;
        P.clips[ac].bend = ab;
        float *t1 = test_render(&P, 0, 2 * sec, 0);
        int zt = ZC(t1, sec / 4, sec / 2);
        double late_tape = test_rms(t1, (long)(1.3 * sec), (long)(1.7 * sec), 0);
        free(t1);
        check(zt > 200 && zt < 240 && late_tape < 1e-4, "Tape: bent up an octave, an audio clip plays twice as fast and ends early");
        abp = bend_find(&P, ab);
        abp->keep_time = 1;
        float *t2 = test_render(&P, 0, 2 * sec, 0);
        int zk = ZC(t2, sec / 4, sec / 2);
        double late_keep = test_rms(t2, (long)(1.3 * sec), (long)(1.7 * sec), 0);
        free(t2);
        check(zk > 200 && zk < 240 && late_keep > 0.1, "Keep timing: an octave up, and still playing to its end");
        /* a drum hit follows the curve too */
        DrumPat d;
        memset(&d, 0, sizeof d);
        d.id = 1;
        d.steps = 16;
        d.nrows = 1;
        d.sample[0] = sine;
        d.row_vol[0] = 1;
        d.vel[0][0] = 127;
        for (int k = 0; k < 16; k++) d.prob[0][k] = 100;
        LOCK();
        P.nclips = 0;
        P.drums[P.ndrums++] = d;
        UNLOCK();
        int dc = clip_add(sine, 0, 0, 0, 2 * sec);
        P.clips[dc].pattern = -1;
        float *d0 = test_render(&P, 0, sec, 0);
        int zd0 = ZC(d0, sec / 4, sec / 2);
        free(d0);
        int db = bend_new(&P);
        Bend *dbp = bend_find(&P, db);
        dbp->n = 1;
        dbp->at[0] = 0, dbp->semis[0] = -12;
        P.clips[dc].bend = db;
        float *d1 = test_render(&P, 0, sec, 0);
        int zd1 = ZC(d1, sec / 4, sec / 2);
        free(d1);
        check(fabs((double)zd0 / zd1 - 2) < 0.08, "a drum clip's hits follow its curve (down an octave: half the frequency)");
#undef ZC
        /* saving */
        char tp[1100], msg2[256];
        const char *td = getenv("TMPDIR") ? getenv("TMPDIR") : getenv("TEMP") ? getenv("TEMP") : "/tmp";
        snprintf(tp, sizeof tp, "%s/tinydaw-bend-%u.tdaw", td, (unsigned)SDL_GetTicks() ^ (unsigned)time(NULL));
        LOCK();
        P.clips[P.nclips] = P.clips[dc];         /* the audio clip again, keeping its timing */
        P.clips[P.nclips].pattern = 0;
        P.clips[P.nclips].bend = ab;
        P.clips[P.nclips].start = 2 * sec;
        P.clips[P.nclips].length = P.samples[sine].frames;
        P.nclips++;
        UNLOCK();
        int ok = project_save(&P, tp, msg2, sizeof msg2) == 0;
        Project *q = malloc(sizeof *q);
        ok = ok && project_load(q, tp, msg2, sizeof msg2) == 0;
        if (ok) {
            const Bend *qa = bend_find_c(q, q->clips[1].bend), *qd = bend_find_c(q, q->clips[0].bend);
            ok = q->nclips == 2 && qa && qd && qa->keep_time == 1 && qa->n == 1 && qa->semis[0] == 12 && qd->semis[0] == -12 &&
                 q->clips[1].pattern == 0 && q->clips[0].pattern == -1;
            project_free(q);
        }
        free(q);
        remove(tp);
        check(ok, "pitch curves save and open again with their clips");

        /* the editor: draw, copies get their own curve, presets, undo */
        LOCK();
        P.nclips = 0;
        P.nnotes = 0;
        UNLOCK();
        int e1 = clip_add(sine, 0, 0, 0, P.samples[sine].frames), e2 = clip_add(sine, 1, 0, 0, P.samples[sine].frames);
        sel_only(e1);
        for (int i = 0; i < RI_COUNT; i++) *RAIL_FLAG[i] = 0;
        show_panel(RI_MIXER);
        mixer_sub = 1;
        mixer_scroll = 0;
        frame();
        frame();
        history_check();
        int u0 = nundo;
        SDL_Rect cr = cb_box;
        int yhi = cr.y + cr.h / 2 - (int)(5.0 / 12 * (cr.h / 2 - 4));   /* about +5 semitones */
        sim_drag(cr.x + 10, yhi, cr.x + cr.w - 10, yhi, KMOD_SHIFT);
        frame();
        history_check();
        const Bend *eb = bend_find_c(&P, P.clips[e1].bend);
        int whole = eb && eb->n > 3;
        for (int i = 0; eb && i < eb->n; i++) whole &= eb->semis[i] == roundf(eb->semis[i]) && fabsf(eb->semis[i] - 5) <= 1;
        check(whole && P.clips[e2].bend == 0 && nundo == u0 + 1,
              "drawing on the curve bends the selected clip, Shift in whole semitones, in one undo step");
        LOCK();
        P.clips[e2].bend = P.clips[e1].bend;      /* a copy, sharing the curve */
        UNLOCK();
        int shared = P.clips[e1].bend;
        sim_drag(cr.x + 10, cr.y + cr.h / 2, cr.x + 40, cr.y + cr.h / 2, 0);
        check(P.clips[e1].bend != shared && P.clips[e2].bend == shared, "editing one copy's curve leaves the other copy's alone");
        LOCK();
        P.clips[e1].pattern = pattern_new_id(&P);
        P.clips[e1].root = 57;
        for (int k = 0; k < 3; k++) P.notes[P.nnotes++] = (Note){P.clips[e1].pattern, 57 + k, k * sec / 2, sec / 4, 0.8f};
        UNLOCK();
        cb_preset(CBP_SCOOP);
        eb = bend_find_c(&P, P.clips[e1].bend);
        check(eb && eb->n >= 8 && fabsf(bend_at(eb, 0.5) + 1.5f) < 1e-4 && fabsf(bend_at(eb, 0.0)) > 1, "Scoops on a note clip scoop up into each note");
        frame();
        history_check();
        cb_preset(CBP_FLAT);
        check(P.clips[e1].bend == 0, "Flat takes the bend away");
        undo_redo(0);
        check(P.clips[e1].bend != 0, "and undo brings it back");
        sel_clear();
        new_project();
        make_demo();
        frame();
        history_check();
    }

    /* ---- typing: the caret moves with the arrow keys and a click, and typing goes where it is */
    {
        char t[64] = "Intro";
        tf_end(1);
        tf.buf = t;
        tf.cap = sizeof t;
        tf.path = 0;
        tf.caret = (size_t)-1;
        tf_drawn = 1;
        snprintf(tf.orig, sizeof tf.orig, "%s", t);
        SDL_Rect fr = {200, 200, 300, 28};
        textfield(fr, t, sizeof t, 0, "");
        tf_key((SDL_Keysym){.sym = SDLK_LEFT});
        tf_key((SDL_Keysym){.sym = SDLK_LEFT});
        tf_insert("X");
        int mid = !strcmp(t, "IntXro");
        tf_key((SDL_Keysym){.sym = SDLK_HOME});
        tf_insert("Y");
        tf_key((SDL_Keysym){.sym = SDLK_DELETE});
        int home = !strcmp(t, "YntXro");
        tf_key((SDL_Keysym){.sym = SDLK_END});
        tf_key((SDL_Keysym){.sym = SDLK_BACKSPACE});
        check(mid && home && !strcmp(t, "YntXr"), "in a text field, the arrows, Home and End move the caret; typing, Backspace and Delete work there");
        snprintf(t, sizeof t, "hello wide world");
        tf.caret = strlen(t);
        tf_key((SDL_Keysym){.sym = SDLK_LEFT, .mod = KMOD_CTRL});
        int w1 = tf.caret == 11;
        tf_key((SDL_Keysym){.sym = SDLK_LEFT, .mod = KMOD_CTRL});
        check(w1 && tf.caret == 6, "Ctrl+Left jumps a word at a time");
        mx = fr.x + 10 + prefix_w(t, 3) + 1;     /* click just after "hel" */
        my = fr.y + 10;
        lpress = 1;
        textfield(fr, t, sizeof t, 0, "");
        check(tf.caret == 3 && !lpress, "clicking in the text puts the caret there");
        snprintf(t, sizeof t, "caf\xc3\xa9");       /* a two-byte letter: the caret steps over it whole */
        tf.caret = strlen(t);
        tf_key((SDL_Keysym){.sym = SDLK_LEFT});
        check(tf.caret == 3, "the caret moves a whole letter at a time, accents included");
        tf_end(1);
        lpress = 0;
        double a4 = P.a4;
        numedit_begin(&P.a4, 415, 466);
        numedit_text("4320");
        numedit_key((SDL_Keysym){.sym = SDLK_LEFT});
        numedit_key((SDL_Keysym){.sym = SDLK_BACKSPACE});
        numedit_text("4");
        int nb = !strcmp(numedit.buf, "4340");
        numedit_key((SDL_Keysym){.sym = SDLK_END});
        numedit_key((SDL_Keysym){.sym = SDLK_BACKSPACE});
        check(nb && !strcmp(numedit.buf, "434"), "typing a value: the caret moves and edits in the middle too");
        numedit_key((SDL_Keysym){.sym = SDLK_RETURN});
        frame();
        frame();
        P.a4 = a4;
    }

    /* ---- macros and effect presets */
    {
        new_project();
        make_demo();
        frame();
        history_check();
        Track tt = P.tracks[0];
        memset(tt.macro, 0, sizeof tt.macro);
        tt.filter = FILT_OFF;
        macro_set(&tt.macro[0], "Test", 0.5f);
        macro_link(&tt.macro[0], TP_DRIVE, 0, 1);
        macro_link(&tt.macro[0], TP_CUTOFF, 100, 10000);
        macros_apply(&tt, NULL);
        check(fabsf(tt.drive - 0.5f) < 1e-5f && fabsf(tt.cutoff - 1000) < 1 && tt.filter == FILT_LOW,
              "a macro sets what it's linked to (cutoff on a log scale), turning the filter on");
        LOCK();
        P.tracks[0].macro[0] = tt.macro[0];
        P.tracks[0].macro[1] = (Macro){"Gone", 0, 1, {{TP_VOLUME, 1, 0}}};
        P.tracks[0].macro[1].value = 1;          /* turned right up: volume 0 */
        UNLOCK();
        float *silent = test_render(&P, 0, 4 * beat_frames(), 0);
        double r0 = test_rms(silent, 0, 4 * beat_frames(), 2);
        free(silent);
        LOCK();
        P.tracks[0].macro[1].value = 0;
        UNLOCK();
        float *loud = test_render(&P, 0, 4 * beat_frames(), 0);
        double r1 = test_rms(loud, 0, 4 * beat_frames(), 2);
        free(loud);
        check(r0 < 1e-5 && r1 > 0.01, "a macro linked to the volume silences the track at one end and not the other");
        Track eff;
        auto_add(0, AUTO_MACRO1, 0, 1.0f);
        auto_apply(&P, 0, 100, &eff);
        check(fabsf(eff.drive - 1) < 1e-5f, "automating a macro turns everything it's linked to");
        char tp[1100], msg2[256];
        const char *td = getenv("TMPDIR") ? getenv("TMPDIR") : getenv("TEMP") ? getenv("TEMP") : "/tmp";
        snprintf(tp, sizeof tp, "%s/tinydaw-macros-%u.tdaw", td, (unsigned)SDL_GetTicks() ^ (unsigned)time(NULL));
        int ok = project_save(&P, tp, msg2, sizeof msg2) == 0;
        Project *q = malloc(sizeof *q);
        ok = ok && project_load(q, tp, msg2, sizeof msg2) == 0;
        if (ok) {
            const Macro *a = &q->tracks[0].macro[0], *b = &q->tracks[0].macro[1];
            ok = !strcmp(a->name, "Test") && a->nlinks == 2 && a->link[1].param == TP_CUTOFF && a->link[1].hi == 10000 &&
                 !strcmp(b->name, "Gone") && b->nlinks == 1 && b->link[0].param == TP_VOLUME;
            project_free(q);
        }
        free(q);
        remove(tp);
        check(ok, "macros save and open again with their links");
        frame();
        history_check();

        /* presets: built in, and your own */
        char pd[1100];
        snprintf(pd, sizeof pd, "%s/tinydaw-presets-%u", td, (unsigned)SDL_GetTicks() ^ (unsigned)time(NULL));
        SDL_setenv("TINYDAW_PRESETS_DIR", pd, 1);
        plist[PK_TRACK].n = 0;
        preset_list(PK_TRACK);
        int warm = -1;
        for (int i = 0; i < plist[PK_TRACK].n; i++)
            if (!strcmp(plist[PK_TRACK].ptr[i], "Warm tape")) warm = i;
        float vol = P.tracks[1].volume;
        int u0 = nundo;
        preset_apply(PK_TRACK, warm, &P.tracks[1]);
        frame();
        history_check();
        check(warm > 0 && P.tracks[1].filter == FILT_LOW && fabsf(P.tracks[1].drive - 0.2f) < 1e-5f && P.tracks[1].comp_on &&
                  !strcmp(P.tracks[1].macro[0].name, "Warmth") && P.tracks[1].volume == vol && nundo == u0 + 1,
              "a built-in preset sets the track's chain and macros, keeps its level, in one undo step");
        LOCK();
        P.tracks[1].eq[1] = 4.5f;
        P.tracks[1].macro[2] = (Macro){"Mine", 0.25f, 1, {{TP_WIDTH, 0.5f, 1.9f}}};
        UNLOCK();
        int wrote = preset_write(PK_TRACK, "My chain", &P.tracks[1], &P.fx) == 0;
        plist[PK_TRACK].n = 0;
        preset_list(PK_TRACK);
        int mine = -1;
        for (int i = 0; i < plist[PK_TRACK].n; i++)
            if (!strcmp(plist[PK_TRACK].ptr[i], "Saved: My chain")) mine = i;
        preset_apply(PK_TRACK, mine, &P.tracks[2]);
        const Track *a = &P.tracks[1], *b = &P.tracks[2];
        check(wrote && mine > 0 && b->filter == a->filter && b->eq[1] == 4.5f && b->drive == a->drive &&
                  !memcmp(b->macro, a->macro, sizeof a->macro) && strcmp(b->name, a->name),
              "a saved preset lists as yours and puts the same chain and macros on another track");
        plist[PK_DELAY].n = 0;
        preset_list(PK_DELAY);
        preset_apply(PK_DELAY, 3, NULL);         /* Dotted 8th */
        plist[PK_REVERB].n = 0;
        preset_list(PK_REVERB);
        preset_apply(PK_REVERB, 3, NULL);        /* Hall */
        check(P.fx.delay_div == 4 && P.fx.pingpong && fabsf(P.fx.reverb_size - 0.85f) < 1e-5f,
              "the delay and reverb have presets too (a dotted 8th, a hall)");
        char f1[1300];
        snprintf(f1, sizeof f1, "%s/Track/My chain.preset", pd);
        remove(f1);
        snprintf(f1, sizeof f1, "%s/Track", pd);
        rmdir(f1);
        rmdir(pd);
        SDL_setenv("TINYDAW_PRESETS_DIR", "", 1);
        plist[PK_TRACK].n = plist[PK_DELAY].n = plist[PK_REVERB].n = 0;
        for (int i = 0; i < RI_COUNT; i++) *RAIL_FLAG[i] = 0;
        show_panel(RI_MIXER);
        mixer_sub = 2;
        sel_track = 2;
        macro_open = 2;
        mixer_scroll = 0;
        frame();
        int dirty0 = nundo;
        frame();
        history_check();
        check(mixer_content_h > 600 && nundo == dirty0, "the Track page shows the macros, and looking changes nothing");
        mixer_sub = 3;
        frame();
        macro_open = -1;
        mixer_sub = 0;
        new_project();
        make_demo();
        frame();
        history_check();
    }

    /* ---- the session view: slots launched live beside the timeline, and captured onto it */
    {
        new_project();
        LOCK();
        P.nclips = P.nnotes = P.nautos = P.nchanges = P.ndrums = 0;
        P.bpm = 120;
        P.ts_num = P.ts_den = 4;
        P.playing = 0;
        UNLOCK();
        long bar = meter_bar_frame(&P, 1);
        int lo = gen(WAVE_SINE, 57, 4, 0.5, 0, 0), hi = gen(WAVE_SINE, 69, 4, 0.5, 0, 0);   /* 220 Hz, 440 Hz */
        clip_add(lo, 0, 0, 0, bar);              /* the timeline: A3 in bar 1, A4 in bar 2 */
        clip_add(hi, 0, bar, 0, bar);
        sec_add(0, 1, "Low");
        sec_add(1, 1, "High");
        frame();
        history_check();
#define ZC(buf, from, to) ({ int z_ = 0; for (long i_ = (from) + 1; i_ < (to); i_++) z_ += ((buf)[2 * i_] >= 0) != ((buf)[2 * i_ - 2] >= 0); z_; })
        Mixer *m = mixer_new();
        long n = bar / 2;
        float *b = calloc((size_t)n * 2, sizeof(float));
        mixer_run(m, &P, bar / 4, 1, b, (int)n, -1, 1);
        int z_tl = ZC(b, 0, n);
        check(sess_from_sections() == 2 && P.nscenes == 2 && P.slots[0][1].kind == SLOT_RANGE && P.slots[0][1].bar == 1 &&
                  !strcmp(P.scene_names[1], "High"), "From sections makes a scene a section, each track looping its part");
        LOCK();
        P.sess_play[0] = 1;                      /* track 1 plays the High scene's slot, from the start */
        P.sess_at[0] = 0;
        UNLOCK();
        memset(b, 0, sizeof(float) * 2 * (size_t)n);
        mixer_run(m, &P, bar / 4, 1, b, (int)n, -1, 1);
        int z_sl = ZC(b, 0, n);
        check(fabs((double)z_sl / z_tl - 2) < 0.08, "a launched slot looping the timeline's bar 2 plays it instead of bar 1");
        float *ex = test_render(&P, bar / 4, n, -1);
        int z_ex = ZC(ex, 0, n);
        free(ex);
        check(abs(z_ex - z_tl) <= 2, "exporting plays the timeline, whatever's launched");
        LOCK();                                  /* queued for bar 2: the timeline until then, the slot after */
        P.sess_play[0] = SESS_TIMELINE;
        P.sess_q[0] = 0;                         /* the Low slot (A3) */
        P.sess_q_at[0] = bar;
        UNLOCK();
        float *bb = calloc((size_t)bar * 4, sizeof(float));
        mixer_run(m, &P, 0, 1, bb, (int)(2 * bar), -1, 1);
        int z_after = ZC(bb, bar + bar / 4, bar + bar * 3 / 4);
        check(P.sess_play[0] == 0 && P.sess_at[0] == bar && P.sess_q[0] == SESS_NONE && abs(z_after - z_tl) <= 2,
              "a queued launch switches exactly on its bar: then bar 2 plays the Low slot's A3");
        free(bb);
        LOCK();
        P.sess_play[0] = SESS_STOPPED;
        UNLOCK();
        memset(b, 0, sizeof(float) * 2 * (size_t)n);
        mixer_run(m, &P, bar / 4, 1, b, (int)n, -1, 1);
        double r_stop = test_rms(b, 0, n, 2);
        LOCK();
        P.sess_play[0] = SESS_TIMELINE;
        UNLOCK();
        memset(b, 0, sizeof(float) * 2 * (size_t)n);
        mixer_run(m, &P, bar / 4, 1, b, (int)n, -1, 1);
        check(r_stop < 1e-6 && abs(ZC(b, 0, n) - z_tl) <= 2, "a stopped track is silent; back on the timeline it plays it again");
        /* a clip in a slot loops */
        sel_only(1);                              /* the A4 clip */
        sess_menu_t = 1;
        sess_menu_s = 0;
        sess_action(SMA_PUT);
        check(P.slots[1][0].kind == SLOT_CLIP && P.slots[1][0].clip.sample == hi && fabs(P.slots[1][0].beats - 4) < 1e-9,
              "Put the selected clip here fills a slot with it, a bar round");
        LOCK();
        P.sess_play[1] = 0;
        P.sess_at[1] = 0;
        UNLOCK();
        memset(b, 0, sizeof(float) * 2 * (size_t)n);
        mixer_run(m, &P, 3 * bar + bar / 4, 1, b, (int)n, -1, 1);   /* its fourth time round */
        int z_loop = ZC(b, 0, n);
        check(fabs((double)z_loop / z_tl - 2) < 0.08, "a clip slot plays round and round");
        LOCK();
        P.sess_play[1] = SESS_TIMELINE;
        UNLOCK();
        free(b);
        mixer_free(m);
#undef ZC
        /* the grid: Tab, launching a scene by its name, capture */
        frame();
        history_check();
        for (int i = 0; i < RI_COUNT; i++) *RAIL_FLAG[i] = 0;
        key((SDL_Keysym){.sym = SDLK_TAB});
        frame();
        check(session_view && sess_scene_r[1].w > 0 && sess_cell_[0][1].w > 0, "Tab shows the session view: scenes across, tracks down");
        sess_quant = 0;
        LOCK();
        P.playhead = bar / 2;
        UNLOCK();
        SDL_Rect sr = sess_scene_r[1];
        sim(sr.x + sr.w / 2, sr.y + sr.h / 2, 1, 0, 0, 0);
        sim(sr.x + sr.w / 2, sr.y + sr.h / 2, 0, 1, 0, 0);
        check(P.playing && P.sess_q[0] == 1 && P.sess_q_at[0] == bar, "clicking a scene's name launches it on the next bar");
        LOCK();
        P.playing = 0;
        P.sess_q[0] = SESS_NONE;
        P.sess_play[0] = 1;                      /* as if it had launched at bar 2 and played two bars */
        P.sess_at[0] = bar;
        P.playhead = bar;
        P.playing = 1;
        UNLOCK();
        frame();
        LOCK();
        P.playhead = 3 * bar;
        UNLOCK();
        frame();
        LOCK();
        P.playing = 0;
        UNLOCK();
        frame();
        history_check();
        int u0 = nundo, nb = P.nclips;
        int cap = sess_capture();
        frame();
        history_check();
        int on0 = 0, all_hi = 1;
        for (int c = 0; c < P.nclips; c++)
            if (P.clips[c].track == 0 && P.clips[c].start >= bar && P.clips[c].start < 3 * bar) {
                on0++;
                all_hi &= P.clips[c].sample == hi;
            }
        check(cap >= 2 && on0 == 2 && all_hi && nundo == u0 + 1,
              "Capture writes what played onto the timeline where it played (bars 2 and 3: the High loop twice), in one undo step");
        undo_redo(0);
        check(P.nclips == nb, "and undo takes it off again");
        char tp[1100], msg2[256];
        const char *td = getenv("TMPDIR") ? getenv("TMPDIR") : getenv("TEMP") ? getenv("TEMP") : "/tmp";
        snprintf(tp, sizeof tp, "%s/tinydaw-session-%u.tdaw", td, (unsigned)SDL_GetTicks() ^ (unsigned)time(NULL));
        int ok = project_save(&P, tp, msg2, sizeof msg2) == 0;
        Project *q = malloc(sizeof *q);
        ok = ok && project_load(q, tp, msg2, sizeof msg2) == 0;
        if (ok) {
            ok = q->nscenes == 2 && !strcmp(q->scene_names[1], "High") && q->slots[0][1].kind == SLOT_RANGE &&
                 q->slots[0][1].bar == 1 && q->slots[1][0].kind == SLOT_CLIP && q->slots[1][0].clip.sample == hi &&
                 q->sess_play[0] == SESS_TIMELINE;
            project_free(q);
        }
        free(q);
        remove(tp);
        check(ok, "scenes and slots save and open again (and open with every track on the timeline)");
        session_view = 0;
        new_project();
        make_demo();
        frame();
        history_check();
    }

    /* ---- the project browser and recent projects */
    {
        examples_scan();
        int dk = -1;
        for (int i = 0; i < nexamples; i++)
            if (strstr(example_title[i], "Disco")) dk = i;
        ProjectInfo *pi = malloc(sizeof *pi);
        int peeked = dk >= 0 && project_peek(example_path[dk], pi) == 0;
        check(peeked && !strcmp(pi->title, "Disco: Mirror Ball") && pi->bpm == 120 && pi->ntracks == 8 && pi->npeek > 10 &&
                  pi->npsec == 5 && pi->has_key && pi->key_root == 9 && pi->seconds > 50,
              "a quick look at a project file: its title, tempo, key, tracks, sections and first clips");
        char td2[900], pa[1200], pb[1200], rf[1200];
        const char *td = getenv("TMPDIR") ? getenv("TMPDIR") : getenv("TEMP") ? getenv("TEMP") : "/tmp";
        snprintf(td2, sizeof td2, "%s/tinydaw-browse-%u", td, (unsigned)SDL_GetTicks() ^ (unsigned)time(NULL));
        ensure_dir(td2);
        snprintf(pa, sizeof pa, "%s/alpha.tdaw", td2);
        snprintf(pb, sizeof pb, "%s/beta.tdaw", td2);
        snprintf(rf, sizeof rf, "%s/recent.txt", td2);
        new_project();
        make_demo();
        save_to(pa);                             /* (not in the recent list: the test's file isn't set yet) */
        snprintf(P.tags, sizeof P.tags, "demo");
        save_to(pb);
        FILE *fa = fopen(pa, "rb");
        char before[200000];
        size_t nbefore = fa ? fread(before, 1, sizeof before - 1, fa) : 0;
        if (fa) fclose(fa);
        before[nbefore] = 0;
        int st = project_set_tags(pa, "lo-fi, sketch") == 0;
        fa = fopen(pa, "rb");
        char after[200000];
        size_t nafter = fa ? fread(after, 1, sizeof after - 1, fa) : 0;
        if (fa) fclose(fa);
        after[nafter] = 0;
        int ntag = 0;
        for (char *q = after; (q = strstr(q, "\ntags ")); q++) ntag++;
        Project *q = malloc(sizeof *q);
        char msg2[256];
        int ld = project_load(q, pa, msg2, sizeof msg2) == 0;
        check(st && ntag == 1 && nafter == nbefore + strlen("tags lo-fi, sketch\n") && ld && !strcmp(q->tags, "lo-fi, sketch"),
              "tags go into a project file as one line, the rest left as it was, and open with it");
        if (ld) project_free(q);
        project_set_tags(pa, "lo-fi");
        ld = project_load(q, pa, msg2, sizeof msg2) == 0;
        check(ld && !strcmp(q->tags, "lo-fi"), "changing the tags replaces the line");
        if (ld) project_free(q);
        free(q);
        SDL_setenv("TINYDAW_RECENT_FILE", rf, 1);
        recent_read = 0;
        nrecent = 0;
        recent_add(pa);
        recent_add(pb);
        recent_add(pa);
        recent_load();
        check(nrecent == 2 && !strcmp(recent[0], pa) && !strcmp(recent[1], pb), "recent projects: newest first, each once, kept on disk");
        /* the browser */
        SDL_setenv("TINYDAW_PROJECTS_DIR", td2, 1);
        browse_tab = 1;
        key((SDL_Keysym){.sym = SDLK_o, .mod = KMOD_CTRL});
        frame();
        check(browse_open && nbitems == 2, "Ctrl+O opens the project browser, with your projects in it");
        snprintf(browse_q, sizeof browse_q, "beta");
        frame();
        int one = browse_card_r[0].w > 0;
        snprintf(browse_q, sizeof browse_q, "zzz");
        frame();
        int none = 1;
        for (int i = 0; i < nbitems; i++) none &= !browse_matches(&bcache[bitems[i]]);
        browse_q[0] = 0;
        snprintf(browse_tag, sizeof browse_tag, "LO-FI");
        int by_tag = 0;
        for (int i = 0; i < nbitems; i++) by_tag += browse_matches(&bcache[bitems[i]]);
        browse_tag[0] = 0;
        check(one && none && by_tag == 1, "searching by name and filtering by a tag narrow the list");
        new_project();
        frame();
        history_check();
        int which = -1;
        for (int i = 0; i < nbitems; i++)
            if (!strcmp(bcache[bitems[i]].path, pb)) which = i;
        frame();
        SDL_Rect cr = browse_card_r[which >= 0 ? which : 0];
        sim(cr.x + cr.w / 2, cr.y + 30, 1, 0, 0, 0);
        sim(cr.x + cr.w / 2, cr.y + 30, 0, 1, 0, 0);
        check(!browse_open && !strcmp(path_base(proj_path), "beta.tdaw") && !strcmp(P.tags, "demo"),
              "clicking a project's card opens it");
        browse_show();
        frame();
        key((SDL_Keysym){.sym = SDLK_ESCAPE});
        check(!browse_open, "Esc closes the browser");
        remove(pa);
        remove(pb);
        remove(rf);
        rmdir(td2);
        SDL_setenv("TINYDAW_RECENT_FILE", "", 1);
        SDL_setenv("TINYDAW_PROJECTS_DIR", "", 1);
        recent_read = 0;
        nrecent = 0;
        browse_tab = 0;
        free(pi);
        new_project();
        make_demo();
        frame();
        history_check();
    }

    /* ---- the session with the loop on, and outside 4/4 */
    {
        new_project();
        LOCK();
        P.nclips = P.nnotes = P.nautos = P.nchanges = P.ndrums = 0;
        P.bpm = 120;
        P.ts_num = P.ts_den = 4;
        P.playing = 0;
        UNLOCK();
        long bar = meter_bar_frame(&P, 1);
        int hi = gen(WAVE_SINE, 69, 4, 0.5, 0, 0);
        clip_add(hi, 1, 0, 0, bar);
        sess_add_scene(NULL);
        slot_fill_clip(0, 0, &P.clips[0], "A4");
        frame();
        history_check();
        sess_forget();
        LOCK();                                  /* looping bars 1-2, the slot launched at the start */
        P.loop = 1;
        P.loop_start = 0;
        P.loop_end = 2 * bar;
        P.sess_play[0] = 0;
        P.sess_at[0] = 0;
        P.playhead = 0;
        P.playing = 1;
        UNLOCK();
        frame();
        LOCK();
        P.playhead = bar * 19 / 10;
        UNLOCK();
        frame();
        LOCK();
        P.playhead = bar * 3 / 10;               /* gone round (the UI catches it a little after the loop's start) */
        UNLOCK();
        frame();
        LOCK();
        P.playhead = bar * 3 / 2;
        UNLOCK();
        frame();
        LOCK();
        P.playing = 0;
        UNLOCK();
        frame();
        history_check();
        sess_capture();
        long cov = 0;
        int overlap = 0, n0 = 0, aligned = 1;
        for (int a = 0; a < P.nclips; a++) {
            const Clip *x = &P.clips[a];
            if (x->track != 0) continue;
            n0++;
            cov += x->length;
            /* in step with the launch: each piece plays its pass from where that pass is */
            aligned &= x->offset == (x->start % bar);
            for (int b = a + 1; b < P.nclips; b++) {
                const Clip *y = &P.clips[b];
                if (y->track == 0 && x->start < y->start + y->length && y->start < x->start + x->length) overlap = 1;
            }
        }
        check(n0 >= 2 && !overlap && labs(cov - 2 * bar) < SR / 50 && aligned,
              "captured while looping: one copy where passes overlap (the newest), in step with the launch, no drift");
        undo_redo(0);
        /* a launch queued in the loop's last bar waits for the loop to come round */
        LOCK();
        P.sess_play[0] = SESS_TIMELINE;
        P.playhead = bar * 3 / 2;
        P.playing = 1;
        UNLOCK();
        sess_quant = 0;
        sess_queue(0, 0);
        int queued_ok = P.sess_q[0] == 0 && P.sess_q_at[0] == 0 && P.sess_q_wrap[0];
        Mixer *m = mixer_new();
        float *b = calloc(2048 * 2, sizeof(float));
        mixer_run(m, &P, bar * 3 / 2, 1, b, 1024, -1, 1);
        int waited = P.sess_q[0] == 0 && P.sess_play[0] == SESS_TIMELINE;
        mixer_run(m, &P, 0, 1, b, 1024, -1, 1);   /* round the loop */
        check(queued_ok && waited && P.sess_play[0] == 0 && P.sess_at[0] == 0 && P.sess_q[0] == SESS_NONE,
              "a launch in the loop's last bar waits for the loop's start, then plays from there");
        free(b);
        mixer_free(m);
        LOCK();
        P.playing = 0;
        P.loop = 0;
        P.sess_play[0] = SESS_TIMELINE;
        UNLOCK();
        /* slot lengths in whole bars of the time signature where the clip starts */
        static const int TSN[3] = {3, 7, 6}, TSD[3] = {4, 8, 8}, BARS[3] = {2, 1, 2};
        static const double WANT[3] = {6, 3.5, 6};
        int rounding = 1;
        for (int k = 0; k < 3; k++) {
            LOCK();
            P.ts_num = TSN[k];
            P.ts_den = TSD[k];
            UNLOCK();
            Clip c = P.clips[0];
            c.start = 0;
            c.length = meter_bar_frame(&P, BARS[k]);
            slot_fill_clip(0, 0, &c, "x");
            rounding &= fabs(P.slots[0][0].beats - WANT[k]) < 1e-9;
        }
        check(rounding, "a clip put in a slot keeps whole bars outside 4/4 (2 bars of 3/4, a bar of 7/8, 2 bars of 6/8)");
        new_project();
        make_demo();
        frame();
        history_check();
    }

    /* ---- the session: what plays (both, session only, timeline only), and copy, paste, import, drag */
    {
        new_project();
        LOCK();
        P.nclips = P.nnotes = P.nautos = P.nchanges = P.ndrums = 0;
        P.bpm = 120;
        P.ts_num = P.ts_den = 4;
        P.playing = 0;
        UNLOCK();
        long bar = meter_bar_frame(&P, 1);
        int lo = gen(WAVE_SINE, 57, 4, 0.5, 0, 0), hi = gen(WAVE_SINE, 69, 4, 0.5, 0, 0);
        clip_add(lo, 0, 0, 0, bar);              /* the timeline: A3 on track 1, A4 on track 2 */
        clip_add(hi, 1, 0, 0, bar);
        sess_add_scene(NULL);
        Clip c = P.clips[1];
        slot_fill_clip(0, 0, &c, "A4");          /* track 1's slot plays A4 */
        LOCK();
        P.sess_play[0] = 0;
        P.sess_at[0] = 0;
        UNLOCK();
        Mixer *m = mixer_new();
        long n = bar / 2;
        float *b = calloc((size_t)n * 2, sizeof(float));
        double rms[SESS_MODES][2];
        for (int md = 0; md < SESS_MODES; md++) {
            P.sess_mode = md;
            for (int t = 0; t < 2; t++) {
                memset(b, 0, sizeof(float) * 2 * (size_t)n);
                mixer_run(m, &P, bar / 4, 1, b, (int)n, t, 1);
                rms[md][t] = test_rms(b, 0, n, 2);
            }
        }
        P.sess_mode = SESS_BOTH;
        check(rms[SESS_BOTH][0] > 0.01 && rms[SESS_BOTH][1] > 0.01 && rms[SESS_ONLY_SESSION][0] > 0.01 &&
                  rms[SESS_ONLY_SESSION][1] < 1e-6 && rms[SESS_ONLY_TIMELINE][0] > 0.01 && rms[SESS_ONLY_TIMELINE][1] > 0.01,
              "what plays: both (each track the slot or the timeline), the session alone, or the timeline alone");
        free(b);
        mixer_free(m);
        LOCK();
        P.sess_play[0] = SESS_TIMELINE;
        UNLOCK();
        frame();
        history_check();
        /* copy and paste */
        slot_copy(0, 0);
        sess_add_scene(NULL);
        slot_paste(1, 1);
        int same = P.slots[1][1].kind == SLOT_CLIP && P.slots[1][1].clip.sample == hi && fabs(P.slots[1][1].beats - 4) < 1e-9;
        int nb = P.nclips;
        LOCK();
        P.playhead = 2 * bar;
        UNLOCK();
        key((SDL_Keysym){.sym = SDLK_v, .mod = KMOD_CTRL});   /* (the pointer's on the timeline: the copy goes there) */
        int on_tl = P.nclips == nb + 1 && P.clips[nb].sample == hi && P.clips[nb].start == 2 * bar && P.clips[nb].track == 0;
        check(same && on_tl, "a copied slot pastes into another slot, and onto the timeline with Ctrl+V");
        sound_copy(CM_LIB, lo);
        slot_paste(1, 0);
        check(P.slots[1][0].kind == SLOT_CLIP && P.slots[1][0].clip.sample == lo && P.slots[1][0].clip.pattern == 0,
              "a sound copied from the library pastes into a slot");
        frame();
        history_check();
        for (int i = 0; i < RI_COUNT; i++) *RAIL_FLAG[i] = 0;
        session_view = 1;
        frame();
        SDL_Rect a = sess_cell_[1][0], z = sess_cell_[0][1];
        sim(a.x + a.w / 2, a.y + a.h / 2, 0, 0, 0, 0);
        key((SDL_Keysym){.sym = SDLK_c, .mod = KMOD_CTRL});
        sim(z.x + z.w / 2, z.y + z.h / 2, 0, 0, 0, 0);
        key((SDL_Keysym){.sym = SDLK_v, .mod = KMOD_CTRL});
        check(P.slots[0][1].kind == SLOT_CLIP && P.slots[0][1].clip.sample == lo, "Ctrl+C and Ctrl+V with the pointer over slots");
        frame();
        sim_drag(z.x + z.w / 2, z.y + z.h / 2, a.x + a.w / 2, a.y + a.h / 2, 0);   /* drag onto a filled slot: moved there */
        int moved = P.slots[0][1].kind == SLOT_EMPTY && P.slots[1][0].kind == SLOT_CLIP && P.slots[1][0].clip.sample == lo;
        frame();
        a = sess_cell_[1][0];
        z = sess_cell_[0][1];
        sim_drag(a.x + a.w / 2, a.y + a.h / 2, z.x + z.w / 2, z.y + z.h / 2, KMOD_CTRL);
        check(moved && P.slots[1][0].kind == SLOT_CLIP && P.slots[0][1].kind == SLOT_CLIP && P.slots[0][1].clip.sample == lo,
              "dragging a slot moves it; with Ctrl it copies it");
        sess_menu_t = 0;
        sess_menu_s = 1;
        nb = P.nclips;
        LOCK();
        P.playhead = 3 * bar;
        UNLOCK();
        sess_action(SMA_TO_TIMELINE);
        check(P.nclips == nb + 1 && P.clips[nb].start == 3 * bar && P.clips[nb].sample == lo, "a slot goes onto the timeline at the playhead");
        char wp[1100], err[200];
        const char *td = getenv("TMPDIR") ? getenv("TMPDIR") : getenv("TEMP") ? getenv("TEMP") : "/tmp";
        snprintf(wp, sizeof wp, "%s/tinydaw-slot-%u.wav", td, (unsigned)SDL_GetTicks() ^ (unsigned)time(NULL));
        int wrote = sample_write_wav(&P.samples[hi], wp, err, sizeof err) == 0;
        int ns = P.nsamples;
        slot_import_path(1, 1, wp);
        remove(wp);
        check(wrote && P.nsamples == ns + 1 && P.slots[1][1].kind == SLOT_CLIP && P.slots[1][1].clip.sample == ns,
              "a sound file imported into a slot goes into the library and the slot");
        frame();
        history_check();
        session_view = 0;
        LOCK();
        P.sess_play[0] = 0;
        P.sess_at[0] = 0;
        UNLOCK();
        frame();
        frame();
        LOCK();
        P.sess_play[0] = SESS_TIMELINE;
        UNLOCK();
        new_project();
        make_demo();
        frame();
        history_check();
    }

    /* ---- slot launch modes: loop, toggle, hold, once */
    {
        new_project();
        LOCK();
        P.nclips = P.nnotes = P.nautos = P.nchanges = P.ndrums = 0;
        P.bpm = 120;
        P.ts_num = P.ts_den = 4;
        P.playing = 0;
        P.loop = 0;
        UNLOCK();
        long bar = meter_bar_frame(&P, 1);
        int hi = gen(WAVE_SINE, 69, 4, 0.5, 0, 0);
        clip_add(hi, 0, 0, 0, bar);
        sess_add_scene(NULL);
        sess_add_scene(NULL);
        slot_fill_clip(0, 0, &P.clips[0], "A4");
        slot_fill_clip(0, 1, &P.clips[0], "A4 again");
        frame();
        history_check();
        for (int i = 0; i < RI_COUNT; i++) *RAIL_FLAG[i] = 0;
        session_view = 1;
        sess_quant = 2;                          /* (launches at once, for the test) */
        frame();
        SDL_Rect r = sess_cell_[0][0];
        SDL_Rect mb = {r.x + r.w - 54, r.y + 3, 50, font_h + 4};
        int seen = 1;
        for (int k = 1; k <= LAUNCH_MODES; k++) {
            sim(mb.x + 10, mb.y + 4, 1, 0, 0, 0);
            sim(mb.x + 10, mb.y + 4, 0, 1, 0, 0);
            seen &= P.slots[0][0].mode == k % LAUNCH_MODES;
        }
        check(seen && P.sess_q[0] == SESS_NONE, "the mode button on a slot goes round loop, toggle, hold, once (without launching it)");
        /* hold */
        LOCK();
        P.slots[0][0].mode = LAUNCH_HOLD;
        P.playhead = bar / 2;
        UNLOCK();
        sim(r.x + 20, r.y + r.h - 8, 1, 0, 0, 0);
        int held = P.sess_q[0] == 0 || P.sess_play[0] == 0;   /* (queued, or the audio thread already switched) */
        LOCK();
        P.sess_play[0] = 0;                      /* (as the audio thread would) */
        P.sess_q[0] = SESS_NONE;
        P.playhead = bar;
        UNLOCK();
        sim(r.x + 20, r.y + r.h - 8, 0, 1, 0, 0);
        check(held && ((P.sess_q[0] == SESS_TIMELINE && P.sess_q_at[0] >= bar && P.sess_q_at[0] < bar + SR / 10) ||
                       P.sess_play[0] == SESS_TIMELINE),
              "hold: pressed, it launches; let go, it stops then and there");
        /* toggle */
        LOCK();
        P.slots[0][0].mode = LAUNCH_TOGGLE;
        P.sess_play[0] = 0;
        P.sess_q[0] = SESS_NONE;
        UNLOCK();
        sim(r.x + 20, r.y + r.h - 8, 1, 0, 0, 0);
        sim(r.x + 20, r.y + r.h - 8, 0, 1, 0, 0);
        int stops = P.sess_q[0] == SESS_TIMELINE;
        LOCK();
        P.sess_play[0] = SESS_TIMELINE;
        P.sess_q[0] = SESS_NONE;
        UNLOCK();
        sim(r.x + 20, r.y + r.h - 8, 1, 0, 0, 0);
        sim(r.x + 20, r.y + r.h - 8, 0, 1, 0, 0);
        check(stops && P.sess_q[0] == 0, "toggle: a click on the playing slot stops it, the next starts it");
        /* once */
        LOCK();
        P.slots[0][0].mode = LAUNCH_ONCE;
        P.sess_q[0] = 0;
        P.sess_q_at[0] = bar;
        P.sess_q_wrap[0] = 0;
        UNLOCK();
        Mixer *m = mixer_new();
        float *b = calloc(4096 * 2, sizeof(float));
        mixer_run(m, &P, bar - 100, 1, b, 512, -1, 1);
        int once_q = P.sess_play[0] == 0 && P.sess_q[0] == SESS_TIMELINE && P.sess_q_at[0] == 2 * bar;
        mixer_run(m, &P, 2 * bar - 100, 1, b, 512, -1, 1);
        check(once_q && P.sess_play[0] == SESS_TIMELINE, "once: one pass, then the track goes back to the timeline");
        LOCK();                                  /* once, launched in a loop's last bar: its end comes round the loop */
        P.loop = 1;
        P.loop_start = 0;
        P.loop_end = 2 * bar;
        P.sess_q[0] = 0;
        P.sess_q_at[0] = bar + bar / 2;
        P.sess_q_wrap[0] = 0;
        UNLOCK();
        mixer_run(m, &P, bar + bar / 2 - 100, 1, b, 512, -1, 1);
        check(P.sess_q[0] == SESS_TIMELINE && P.sess_q_wrap[0] && P.sess_q_at[0] == bar / 2,
              "once in a loop: its pass ends round the loop, and it waits for that");
        free(b);
        mixer_free(m);
        LOCK();
        P.loop = 0;
        P.sess_play[0] = SESS_TIMELINE;
        P.sess_q[0] = SESS_NONE;
        P.slots[0][1].mode = LAUNCH_HOLD;
        UNLOCK();
        frame();
        SDL_Rect sh = sess_scene_r[1];           /* holding a scene's name holds its hold slots */
        sim(sh.x + sh.w / 2, sh.y + sh.h / 2, 1, 0, 0, 0);
        int sheld = P.sess_q[0] == 1 || P.sess_play[0] == 1;
        sim(sh.x + sh.w / 2, sh.y + sh.h / 2, 0, 1, 0, 0);
        check(sheld && (P.sess_q[0] == SESS_TIMELINE || (P.sess_q[0] == SESS_NONE && P.sess_play[0] == SESS_TIMELINE)), "holding a scene's name holds its hold slots; letting go stops them");
        char tp[1100], msg2[256];
        const char *td = getenv("TMPDIR") ? getenv("TMPDIR") : getenv("TEMP") ? getenv("TEMP") : "/tmp";
        snprintf(tp, sizeof tp, "%s/tinydaw-modes-%u.tdaw", td, (unsigned)SDL_GetTicks() ^ (unsigned)time(NULL));
        int ok = project_save(&P, tp, msg2, sizeof msg2) == 0;
        Project *q = malloc(sizeof *q);
        ok = ok && project_load(q, tp, msg2, sizeof msg2) == 0;
        if (ok) {
            ok = q->slots[0][0].mode == LAUNCH_ONCE && q->slots[0][1].mode == LAUNCH_HOLD;
            project_free(q);
        }
        free(q);
        remove(tp);
        check(ok, "slots' launch modes save and open again");
        session_view = 0;
        sess_quant = 0;
        LOCK();
        P.playing = 0;
        UNLOCK();
        new_project();
        make_demo();
        frame();
        history_check();
    }

    /* ---- buses and the effect rack */
    {
        new_project();
        check(P.nbuses == 2 && !strcmp(P.buses[BUS_DELAY].name, "Delay") && !strcmp(P.buses[BUS_REVERB].name, "Reverb") &&
                  P.buses[0].volume == 1 && P.buses[1].volume == 1,
              "a new song has two buses, Delay and Reverb, at 0 dB");
        LOCK();
        P.nclips = P.nnotes = P.nautos = P.nchanges = P.ndrums = 0;
        P.bpm = 120;
        P.playing = 0;
        for (int t = 0; t < MAX_TRACKS; t++) P.tracks[t].delay_send = P.tracks[t].reverb_send = 0;
        UNLOCK();
        int sn = gen(WAVE_SAW, 57, 4, 0.5, 0, 0);
        long len = P.samples[sn].frames, a = len / 4, e = len * 3 / 4;
        clip_add(sn, 0, 0, 0, len);
        float *dry = test_render(&P, 0, len, -1);
        double rd = test_rms(dry, a, e, 2);
        /* every kind of effect: plays something finite, and switched off, changes nothing */
        int finite = 1, changes = 0, off_same = 1;
        for (int k = 0; k < FXR_KINDS; k++) {
            LOCK();
            rack_fx_init(&P.tracks[0].rack.fx[0], k);
            if (k == FXR_EQ) P.tracks[0].rack.fx[0].p[2] = 9;
            if (k == FXR_COMP) P.tracks[0].rack.fx[0].p[0] = -30;
            if (k == FXR_GATE) P.tracks[0].rack.fx[0].p[0] = -3;
            P.tracks[0].rack.n = 1;
            UNLOCK();
            float *w = test_render(&P, 0, len, -1);
            double diff = 0;
            for (long i = 0; i < 2 * len; i++) {
                finite &= isfinite(w[i]) && fabsf(w[i]) < 8;
                diff += fabs(w[i] - dry[i]);
            }
            if (diff / (2.0 * len) > 1e-3) changes++;
            else printf("    (%s changed nothing)\n", RACK_KINDS[k].name);
            free(w);
            LOCK();
            P.tracks[0].rack.fx[0].on = 0;
            UNLOCK();
            w = test_render(&P, 0, len, -1);
            off_same &= !memcmp(w, dry, sizeof(float) * 2 * len);
            free(w);
        }
        check(finite, "each of the ten rack effects plays something sensible");
        check(changes == FXR_KINDS, "each of the ten rack effects changes the sound");
        check(off_same, "a rack effect switched off leaves the sound exactly as it was");
        LOCK();                                  /* a high-pass at 5 kHz takes most of a 220 Hz saw away */
        rack_fx_init(&P.tracks[0].rack.fx[0], FXR_FILTER);
        P.tracks[0].rack.fx[0].p[0] = 1;
        P.tracks[0].rack.fx[0].p[1] = 5000;
        UNLOCK();
        float *w = test_render(&P, 0, len, -1);
        check(test_rms(w, a, e, 2) < rd * 0.3, "the rack's filter, as a high-pass, takes the low end away");
        free(w);
        LOCK();                                  /* order matters: crusher then filter isn't filter then crusher */
        rack_fx_init(&P.tracks[0].rack.fx[1], FXR_CRUSH);
        P.tracks[0].rack.fx[1].p[0] = 3;
        P.tracks[0].rack.n = 2;
        UNLOCK();
        float *w1 = test_render(&P, 0, len, -1);
        LOCK();
        RackFx sw = P.tracks[0].rack.fx[0];
        P.tracks[0].rack.fx[0] = P.tracks[0].rack.fx[1];
        P.tracks[0].rack.fx[1] = sw;
        UNLOCK();
        float *w2 = test_render(&P, 0, len, -1);
        double od = 0;
        for (long i = 2 * a; i < 2 * e; i++) od += fabs(w1[i] - w2[i]);
        check(od / (2.0 * (e - a)) > 0.01 * rd, "the rack plays its effects in order");
        free(w1);
        free(w2);
        LOCK();
        P.tracks[0].rack.n = 0;
        UNLOCK();
        /* a group bus */
        int gb = bus_add("Drums");
        LOCK();
        P.tracks[0].out = gb + 1;
        UNLOCK();
        w = test_render(&P, 0, len, -1);
        check(gb == 2 && !memcmp(w, dry, sizeof(float) * 2 * len), "a track played into an empty bus at 0 dB sounds the same");
        free(w);
        LOCK();
        P.buses[gb].volume = 0.5f;
        UNLOCK();
        w = test_render(&P, 0, len, -1);
        check(fabs(test_rms(w, a, e, 2) - rd * 0.5) < rd * 0.02, "the bus's fader sets the level of what plays into it");
        free(w);
        LOCK();
        P.buses[gb].volume = 1;
        P.buses[gb].mute = 1;
        UNLOCK();
        w = test_render(&P, 0, len, -1);
        check(test_rms(w, a, e, 2) < 1e-6, "muting the bus silences the tracks played into it");
        free(w);
        LOCK();
        P.buses[gb].mute = 0;
        P.buses[gb].pan = -1;
        UNLOCK();
        w = test_render(&P, 0, len, -1);
        check(test_rms(w, a, e, 1) < 1e-6 && test_rms(w, a, e, 0) > rd, "the bus's pan moves the group");
        free(w);
        LOCK();
        P.buses[gb].pan = 0;
        rack_fx_init(&P.buses[gb].rack.fx[0], FXR_FILTER);
        P.buses[gb].rack.fx[0].p[0] = 1;
        P.buses[gb].rack.fx[0].p[1] = 5000;
        P.buses[gb].rack.n = 1;
        UNLOCK();
        w = test_render(&P, 0, len, -1);
        check(test_rms(w, a, e, 2) < rd * 0.3, "a bus's rack works on everything played into it");
        free(w);
        /* a send to it, with the track still going to the master */
        LOCK();
        P.buses[gb].rack.n = 0;
        P.tracks[0].out = 0;
        P.tracks[0].send[gb] = 1;
        UNLOCK();
        w = test_render(&P, 0, len, -1);
        check(fabs(test_rms(w, a, e, 2) - 2 * rd) < rd * 0.02, "a send to a bus adds the track again through it");
        free(w);
        LOCK();
        P.tracks[0].send[gb] = 0;
        P.tracks[0].delay_send = 0.6f;
        UNLOCK();
        float *wd = test_render(&P, 0, len * 2, -1);
        LOCK();
        P.buses[BUS_DELAY].mute = 1;
        UNLOCK();
        float *wm = test_render(&P, 0, len * 2, -1);
        check(test_rms(wd, len + 100, len * 2, 2) > 1e-3 && test_rms(wm, len + 100, len * 2, 2) < 1e-6,
              "the delay is bus 1: its echoes play after the clip, and muting the bus stops them");
        free(wd);
        free(wm);
        /* saving and opening */
        LOCK();
        P.buses[BUS_DELAY].mute = 0;
        P.buses[BUS_REVERB].volume = 0.7f;
        P.tracks[1].out = gb + 1;
        P.tracks[2].send[gb] = 0.25f;
        rack_fx_init(&P.tracks[2].rack.fx[0], FXR_PHASER);
        rack_fx_init(&P.tracks[2].rack.fx[1], FXR_DELAY);
        P.tracks[2].rack.fx[1].on = 0;
        P.tracks[2].rack.n = 2;
        rack_fx_init(&P.buses[gb].rack.fx[0], FXR_COMP);
        P.buses[gb].rack.n = 1;
        P.buses[gb].send_r = 0.3f;
        UNLOCK();
        char tp[1100], msg2[256];
        const char *td = getenv("TMPDIR") ? getenv("TMPDIR") : getenv("TEMP") ? getenv("TEMP") : "/tmp";
        snprintf(tp, sizeof tp, "%s/tinydaw-buses-%u.tdaw", td, (unsigned)SDL_GetTicks() ^ (unsigned)time(NULL));
        int ok = project_save(&P, tp, msg2, sizeof msg2) == 0;
        Project *q = malloc(sizeof *q);
        ok = ok && project_load(q, tp, msg2, sizeof msg2) == 0;
        if (ok) {
            ok = q->nbuses == 3 && !strcmp(q->buses[gb].name, "Drums") && fabsf(q->buses[BUS_REVERB].volume - 0.7f) < 1e-6f &&
                 q->tracks[1].out == gb + 1 && fabsf(q->tracks[2].send[gb] - 0.25f) < 1e-6f && q->tracks[2].rack.n == 2 &&
                 q->tracks[2].rack.fx[0].kind == FXR_PHASER && !q->tracks[2].rack.fx[1].on &&
                 q->buses[gb].rack.n == 1 && q->buses[gb].rack.fx[0].kind == FXR_COMP && fabsf(q->buses[gb].send_r - 0.3f) < 1e-6f;
            project_free(q);
        }
        free(q);
        remove(tp);
        check(ok, "buses, outputs, sends and racks save and open again");
        /* macros turn rack knobs */
        LOCK();
        Macro *mc = &P.tracks[2].macro[0];
        memset(mc, 0, sizeof *mc);
        mc->nlinks = 1;
        mc->link[0] = (MacroLink){TP_RACK + 0 * FXR_PARAMS + 0, 0.1f, 4};   /* the phaser's rate, on a log scale */
        mc->value = 0.5f;
        UNLOCK();
        macros_apply(&P.tracks[2], NULL);
        check(fabsf(P.tracks[2].rack.fx[0].p[0] - 0.6324555f) < 1e-3f, "a macro turns a knob in the rack (on its scale)");
        Track pt;
        memset(&pt, 0, sizeof pt);
        builtin_track_preset(10, &pt);
        check(pt.rack.n == 2 && pt.rack.fx[0].kind == FXR_CHORUS && pt.rack.fx[1].kind == FXR_REVERB && pt.macro[0].nlinks == 2,
              "the Dreamy preset brings a chorus and a reverb in its rack, and a macro for both");
        /* the interface: the Buses tab and the Track page draw, a new bus is one undo step */
        frame();
        history_check();
        int u0 = nundo;
        for (int i = 0; i < RI_COUNT; i++) *RAIL_FLAG[i] = 0;
        show_panel(RI_MIXER);
        mixer_sub = 4;
        mixer_scroll = 0;
        frame();
        frame();
        int nb0 = P.nbuses;
        bus_add(NULL);
        frame();
        history_check();
        check(P.nbuses == nb0 + 1 && nundo == u0 + 1, "a new bus is one undo step");
        undo_redo(0);
        frame();
        check(P.nbuses == nb0, "undo takes the new bus away again");
        sel_track = 2;
        mixer_sub = 2;
        frame();
        frame();
        check(1, "the Track page draws a track's output, sends and rack");
        mixer_sub = 0;
        frame();
        for (int i = 0; i < RI_COUNT; i++) *RAIL_FLAG[i] = 0;
        free(dry);
        new_project();
        make_demo();
        frame();
        history_check();
    }

    /* ---- the Singer's pitch curve */
    {
        VocalSpec t;
        memset(&t, 0, sizeof t);
        t.nbend = 3;
        t.bend_beat[0] = 1, t.bend_semis[0] = 0;
        t.bend_beat[1] = 2, t.bend_semis[1] = 2;
        t.bend_beat[2] = 4, t.bend_semis[2] = -2;
        check(vocal_bend_at(&t, 0.5) == 0 && fabs(vocal_bend_at(&t, 1.5) - 1) < 1e-9 && fabs(vocal_bend_at(&t, 3) - 0) < 1e-9 &&
                  vocal_bend_at(&t, 9) == -2,
              "the pitch curve runs in straight lines between its points and holds past the ends");
        show_panel(RI_SOUNDS);
        sounds_sub = 2;
        sv_defaults();
        snprintf(SV.words, sizeof SV.words, "la la la la");
        SV.tune = TUNE_UPDOWN;
        frame();
        sounds_scroll = 100000;                  /* down to it */
        frame();
        frame();
        SDL_Rect r = sv_curve_r;
        int on = r.w > 0 && r.y + r.h < H && r.y > L.top;
        sv_make_notes();
        double nb = SV.notes[1].start + SV.notes[1].len / 2, endb = SV.notes[3].start + SV.notes[3].len + 0.25;
        int px = r.x + (int)(nb / endb * r.w);
        sim(px - 8, r.y + 10, 1, 0, 0, 0);        /* draw high over the second note */
        sim(px + 8, r.y + 10, 0, 0, 0, 0);
        sim(px + 8, r.y + 10, 0, 1, 0, 0);
        double up = vocal_bend_at(&SV, nb);
        check(on && SV.nbend >= 2 && up > 1, "drawing above a note bends it up");
        sim(px, r.y + 10, 0, 0, 1, 0);            /* right-click rubs it out */
        int left = 0;
        for (int i = 0; i < SV.nbend; i++) left += fabs(SV.bend_beat[i] - nb) < 0.05;
        check(left == 0, "right-click rubs out the bend there");
        SDL_Rect sb = sv_bend_r[0];
        sim(sb.x + 5, sb.y + 5, 1, 1, 0, 0);       /* Scoops */
        double at_start = vocal_bend_at(&SV, SV.notes[2].start), later = vocal_bend_at(&SV, SV.notes[2].start + 0.2);
        check(at_start < -1 && fabs(later) < 1e-6, "Scoops start each note low and bring it up to pitch");
        sb = sv_bend_r[3];
        sim(sb.x + 5, sb.y + 5, 1, 1, 0, 0);       /* Flat */
        check(SV.nbend == 0, "Flat takes the bends away");
        if (speech_available(NULL, 0)) {          /* and it really is sung bent */
            char err[200];
            long n0, n1;
            sv_make_notes();
            float *a = render_vocal(&SV, &n0, err, sizeof err);
            sv_bend_preset(BEND_FALL, SV.nnotes < 4 ? SV.nnotes : 4);
            float *b = render_vocal(&SV, &n1, err, sizeof err);
            double d = 0, e = 0;
            for (long i = 0; a && b && i < n0 && i < n1; i++) {
                d += fabs(a[2 * i] - b[2 * i]);
                e += fabs(a[2 * i]);
            }
            check(a && b && n0 == n1 && d > e * 0.05, "a line with bends is sung differently, at the same length");
            free(a);
            free(b);
            char tp[1100], msg2[256];
            const char *td = getenv("TMPDIR") ? getenv("TMPDIR") : getenv("TEMP") ? getenv("TEMP") : "/tmp";
            snprintf(tp, sizeof tp, "%s/tinydaw-bend-%u.tdaw", td, (unsigned)SDL_GetTicks() ^ (unsigned)time(NULL));
            int idx = sv_keep(), ok = idx >= 0 && P.samples[idx].voc.nbend == SV.nbend;
            ok = ok && project_save(&P, tp, msg2, sizeof msg2) == 0;
            Project *q = malloc(sizeof *q);
            ok = ok && project_load(q, tp, msg2, sizeof msg2) == 0;
            if (ok) {
                const VocalSpec *qv = &q->samples[idx].voc;
                ok = qv->nbend == SV.nbend;
                for (int i = 0; ok && i < qv->nbend; i++)
                    ok = fabsf(qv->bend_beat[i] - SV.bend_beat[i]) < 1e-5f && fabsf(qv->bend_semis[i] - SV.bend_semis[i]) < 1e-5f;
                project_free(q);
            }
            free(q);
            remove(tp);
            check(ok, "a sung line keeps its pitch curve in the project");
        }
        sounds_scroll = 0;
        sv_defaults();
        frame();
        history_check();
    }

    /* ---- Export: choosing the folder and file name */
    {
        char keep_proj[1024];
        snprintf(keep_proj, sizeof keep_proj, "%s", proj_path);
        snprintf(proj_path, sizeof proj_path, "/somewhere/My Song.tdaw");
        open_export();
        check(export_open && !strcmp(export_name, "My Song") && export_dir[0], "Export names the file after the project");
        export_take_path("/a/b/take two.flac");
        check(!strcmp(export_dir, "/a/b") && !strcmp(export_name, "take two") && EXO.format == EXP_FLAC,
              "a file chosen in the system dialog sets the folder, the name and the format");
        const char *tmpd = getenv("TMPDIR") ? getenv("TMPDIR") : getenv("TEMP") ? getenv("TEMP") : "/tmp";
        snprintf(export_dir, sizeof export_dir, "%s", tmpd);
        EXO.format = EXP_WAV;
        frame();
        SDL_Rect r = export_name_r;
        sim(r.x + 5, r.y + 5, 1, 1, 0, 0);
        check(tf.buf == export_name, "clicking the file name lets you type it");
        export_name[0] = 0;
        tf_insert("mix/1.mp3");
        tf_key((SDL_Keysym){.sym = SDLK_RETURN});
        frame();
        check(!tf.buf && !strcmp(export_name, "mix1") && EXO.format == EXP_MP3,
              "typing a name keeps slashes out, and .mp3 picks MP3");
        sim(r.x + 5, r.y + 5, 1, 1, 0, 0);
        tf_insert("zzz");
        tf_key((SDL_Keysym){.sym = SDLK_ESCAPE});
        check(!strcmp(export_name, "mix1"), "Esc puts the name back");
        snprintf(export_dir, sizeof export_dir, "%s/no-such-dir-%u", tmpd, (unsigned)SDL_GetTicks());
        frame();
        r = export_go_r;
        sim(r.x + 5, r.y + 5, 1, 1, 0, 0);
        check(export_open && !export_pending, "Export waits for a folder that exists");
        snprintf(export_dir, sizeof export_dir, "%s", tmpd);
        snprintf(export_name, sizeof export_name, "tinydaw-selftest-export-%u", (unsigned)SDL_GetTicks());
        EXO.format = EXP_WAV;
        EXO.loop_only = 0;
        EXO.stems = 0;
        frame();
        sim(r.x + 5, r.y + 5, 1, 1, 0, 0);
        frame();
        frame();
        char out[1400];
        snprintf(out, sizeof out, "%s/%s.wav", tmpd, export_name);
        check(!export_open && file_exists(out), "Export writes to the chosen folder and name");
        remove(out);
        snprintf(proj_path, sizeof proj_path, "%s", keep_proj);
    }

    /* a controller, through SDL's own events (a virtual gamepad stands in for hardware) */
    int vj = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 4, 12, 1);
    if (vj >= 0) {
        SDL_Joystick *vh = SDL_JoystickOpen(vj);
        int got_added = 0, got_hit = 0;
        binds_default();
        P.pads[0] = (Pad){0, PAD_HOLD, 0.8f, 0, 0, 0};
        P.pads[12] = (Pad){0, PAD_HOLD, 0.8f, 0, 0, 0};
        LOCK();
        voices_clear(&P);
        UNLOCK();
        for (int step = 0; step < 4; step++) {
            if (step == 1) SDL_JoystickSetVirtualButton(vh, 0, 1);
            if (step == 2) SDL_JoystickSetVirtualHat(vh, 0, SDL_HAT_UP);
            SDL_JoystickUpdate();
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_JOYDEVICEADDED) {
                    joy_added(e.jdevice.which);
                    got_added = 1;
                } else {
                    joy_event(&e);
                }
            }
            if (step == 1) got_hit = voice_playing(&P, 0);
        }
        check(got_added && njoys > 0, "a plugged-in controller is picked up");
        check(got_hit, "a controller button plays its pad");
        check(voice_playing(&P, 12), "a d-pad direction plays its pad");
        {                                       /* the right stick bends pitch */
            int k = joy_index(SDL_JoystickInstanceID(vh));
            int keep = joy_bend;
            float b[3];
            joy_bend = JB_RIGHT_Y;
            for (int step = 0; step < 3; step++) {
                SDL_JoystickSetVirtualAxis(vh, k >= 0 ? stick_axis[k][3] : 3, step == 0 ? -32767 : step == 1 ? 0 : -32767);
                if (step == 2) joy_bend = JB_OFF;
                SDL_JoystickUpdate();
                SDL_Event e;
                while (SDL_PollEvent(&e))
                    if (e.type != SDL_JOYDEVICEADDED) joy_event(&e);
                b[step] = P.bend;
            }
            joy_bend = keep;
            check(b[0] > 0.99f && b[1] == 0, "pushing the right stick up bends the pitch up, and it springs back");
            check(b[2] == 0, "with pitch bend off the stick doesn't bend");
            P.bend = 0;
        }
        SDL_JoystickClose(vh);
        SDL_JoystickDetachVirtual(vj);
    } else {
        check(0, "SDL virtual joystick for the controller test");
    }

    printf("%d failed\n", failures);
    return failures ? 1 : 0;
}
