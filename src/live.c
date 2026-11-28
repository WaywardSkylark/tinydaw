/* tinydaw: pads, live input, the library, the Generator, the Singer, tones. */
#include "ui.h"

Bind binds[NPADS];
int learn_pad = -1, learned = -1, binds_changed;
int inst_midi = 1;               /* MIDI notes not on a pad play the instrument */
Uint32 pad_flash[NPADS], key_flash, joy_flash[8], midi_flash[MIDI_MAX_DEVICES];
int midi_ok;
int mouse_pad = -1;
SDL_Joystick *joys[MAX_JOYS];
int njoys, joy_gc[MAX_JOYS];
Sint16 axis_rest[MAX_JOYS][16];
Uint8 axis_on[MAX_JOYS][16], hat_bits[MAX_JOYS][4];

const char *const JB_NAMES[JB_COUNT] = {"Off", "Left stick up/down", "Right stick up/down",
                                               "Left stick left/right", "Right stick left/right"};

int joy_bend = JB_RIGHT_Y;
int stick_axis[MAX_JOYS][4];
SDL_GameController *joy_ctl[MAX_JOYS];
RecEv recq[512];
int nrecq;

void binds_default(void)
{
    static const SDL_Scancode keys[NPADS] = {
        SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4,
        SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_E, SDL_SCANCODE_R,
        SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_F,
        SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_C, SDL_SCANCODE_V};
    for (int i = 0; i < NPADS; i++)
        binds[i] = (Bind){keys[i], i < 12 ? i : 100 + (i - 12), 36 + i};   /* buttons, then hat 1 directions */
}

const char *binds_file(void)
{
    static char path[1024];
    if (!path[0]) {
        char dir[900];
        user_dir("config", dir, sizeof dir);
        ensure_dir(dir);
        snprintf(path, sizeof path, "%s/pads.conf", dir);
    }
    return path;
}

void binds_load(void)
{
    binds_default();
    FILE *f = fopen(binds_file(), "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        int i, k, j, m, v;
        if (sscanf(line, "pad %d key %d joy %d midi %d", &i, &k, &j, &m) == 4 && i >= 0 && i < NPADS)
            binds[i] = (Bind){k, j, m};
        else if (sscanf(line, "joy_bend %d", &v) == 1 && v >= 0 && v < JB_COUNT)
            joy_bend = v;
        else if (sscanf(line, "instrument_midi %d", &v) == 1)
            inst_midi = v != 0;
    }
    fclose(f);
}

void binds_save(void)
{
    FILE *f = fopen(binds_file(), "w");
    if (!f) return;
    fprintf(f, "# tinydaw pad inputs: SDL scancode, controller input, MIDI note (-1 = none)\n");
    for (int i = 0; i < NPADS; i++)
        fprintf(f, "pad %d key %d joy %d midi %d\n", i, binds[i].key, binds[i].joy, binds[i].midi);
    fprintf(f, "instrument_midi %d\n", inst_midi);
    fprintf(f, "joy_bend %d\n", joy_bend);
    fclose(f);
}

void joy_label(int code, char *buf, size_t n)
{
    static const char *dirs[4] = {"up", "right", "down", "left"};
    if (code < 0) snprintf(buf, n, "-");
    else if (code < 100) snprintf(buf, n, "button %d", code);
    else if (code < 200) snprintf(buf, n, "hat%s %s", (code - 100) / 4 ? "2" : "", dirs[(code - 100) % 4]);
    else snprintf(buf, n, "axis %d%c", (code - 200) / 2, (code - 200) % 2 ? '-' : '+');
}

void midi_label(int code, char *buf, size_t n)
{
    if (code < 0) snprintf(buf, n, "-");
    else if (code >= MIDI_CC) snprintf(buf, n, "CC %d", code - MIDI_CC);
    else note_name(code, buf, n);
}

void bind_label(int pad, char *buf, size_t n)   /* short, for the pad itself */
{
    static const char *arrows[4] = {"up", "rt", "dn", "lt"};
    char a[32], b[32];
    int j = binds[pad].joy;
    if (j < 100) snprintf(a, sizeof a, "btn %d", j);
    else if (j < 200) snprintf(a, sizeof a, "hat %s", arrows[(j - 100) % 4]);
    else snprintf(a, sizeof a, "ax%d%c", (j - 200) / 2, (j - 200) % 2 ? '-' : '+');
    midi_label(binds[pad].midi, b, sizeof b);
    snprintf(buf, n, "%s  %s", binds[pad].joy >= 0 ? a : "", binds[pad].midi >= 0 ? b : "");
}

void rec_push_vel(int pad, int note, int down, int vel)
{
    if (!pads_rec || !P.playing || countin_left > 0 || nrecq >= (int)(sizeof recq / sizeof *recq)) return;
    recq[nrecq++] = (RecEv){pad, note, down, vel, P.playhead};
}

void rec_push(int pad, int note, int down) { rec_push_vel(pad, note, down, 100); }

unsigned live_rr;                /* round-robin count for live hits */

void pad_hit(int pad, int vel, int down)
{
    Pad *pd = &P.pads[pad];
    if (down) pad_flash[pad] = SDL_GetTicks();
    if (pd->sample < 0 || pd->sample >= P.nsamples) return;
    int smp = sample_pick(&P, pd->sample, vel / 127.0f, live_rr++);   /* its layer for this velocity */
    float gain = pd->volume * (float)vel / 127.0f;
    double step = pow(2.0, pd->pitch / 12.0);
    switch (pd->mode) {
    case PAD_ONESHOT:
        if (!down) return;
        voice_stop(&P, pad, -1);            /* choke: a new hit cuts the last one off */
        voice_start(&P, smp, step, gain, 0, pad, -1);
        rec_push(pad, -1, 1);
        break;
    case PAD_HOLD:
        voice_stop(&P, pad, -1);
        if (down) voice_start(&P, smp, step, gain, 0, pad, -1);
        rec_push(pad, -1, down);
        break;
    case PAD_LOOP:                          /* press to start, press again to stop */
        if (!down) return;
        if (voice_playing(&P, pad)) {
            voice_stop(&P, pad, -1);
            rec_push(pad, -1, 0);
        } else {
            voice_start(&P, pd->sample, step, gain, 1, pad, -1);
            rec_push(pad, -1, 1);
        }
        break;
    }
}

void inst_note(int note, int vel, int down)
{
    voice_stop(&P, -1, note);
    if (!down) {
        rec_push(-1, note, 0);
        return;
    }
    voice_start(&P, sample_pick(&P, P.inst_sample, vel / 127.0f, live_rr++), pow(2.0, (note - P.inst_root) / 12.0),
                0.9f * (float)vel / 127.0f, 0, -1, note);
    rec_push_vel(-1, note, 1, vel);
}

/* Every live input ends up here, from the UI thread or the MIDI thread. */
void input_event(int src, int code, int vel, int down, int device)
{
    static int debug = -1;
    if (debug < 0) debug = getenv("TINYDAW_DEBUG_INPUT") != NULL;
    if (debug) fprintf(stderr, "input %s code %d vel %d %s device %d\n",
                       (const char *[]){"key", "controller", "midi", "mouse"}[src], code, vel, down ? "down" : "up", device);
    LOCK();
    Uint32 now = SDL_GetTicks();
    if (src == SRC_KEY) key_flash = now;
    else if (src == SRC_JOY && device >= 0 && device < MAX_JOYS) joy_flash[device] = now;
    else if (src == SRC_MIDI && device >= 0 && device < MIDI_MAX_DEVICES) midi_flash[device] = now;

    if (learn_pad >= 0 && down && src != SRC_MOUSE) {    /* bind this input to the pad */
        for (int i = 0; i < NPADS; i++) {               /* one input triggers one pad */
            if (src == SRC_KEY && binds[i].key == code) binds[i].key = -1;
            if (src == SRC_JOY && binds[i].joy == code) binds[i].joy = -1;
            if (src == SRC_MIDI && binds[i].midi == code) binds[i].midi = -1;
        }
        if (src == SRC_KEY) binds[learn_pad].key = code;
        if (src == SRC_JOY) binds[learn_pad].joy = code;
        if (src == SRC_MIDI) binds[learn_pad].midi = code;
        learned = learn_pad;
        learn_pad = -1;
        binds_changed = 1;
        UNLOCK();
        return;
    }
    int pad = -1;
    for (int i = 0; i < NPADS && pad < 0; i++) {
        if ((src == SRC_KEY && binds[i].key == code) || (src == SRC_JOY && binds[i].joy == code) ||
            (src == SRC_MIDI && binds[i].midi == code) || (src == SRC_MOUSE && i == code))
            pad = i;
    }
    if (pad >= 0) pad_hit(pad, vel, down);
    else if (src == SRC_MIDI && code < 128 && inst_midi && P.inst_sample >= 0) inst_note(code, vel, down);
    UNLOCK();
}

void midi_in(int code, int velocity, int device, void *ud)
{
    (void)ud;
    if (code == MIDI_BEND || code >= MIDI_CCV) {    /* continuous controls: not pads */
        LOCK();
        if (code == MIDI_BEND) {
            float b = (velocity - 8192) / 8192.0f;
            P.bend = b < -1 ? -1 : b > 1 ? 1 : b;
        } else if (code == MIDI_CCV + 1) {
            P.modwheel = velocity / 127.0f;
        }
        if (device >= 0 && device < MIDI_MAX_DEVICES) midi_flash[device] = SDL_GetTicks();
        UNLOCK();
        return;
    }
    input_event(SRC_MIDI, code, velocity ? velocity : 0, velocity > 0, device);
}

/* ---- controllers */

int joy_index(SDL_JoystickID id)
{
    for (int i = 0; i < njoys; i++)
        if (SDL_JoystickInstanceID(joys[i]) == id) return i;
    return -1;
}

void joy_added(int device_index)
{
    if (njoys >= MAX_JOYS) return;
    SDL_Joystick *j = SDL_JoystickOpen(device_index);
    if (!j) return;
    if (joy_index(SDL_JoystickInstanceID(j)) >= 0) return;   /* already open */
    int k = njoys++;
    joys[k] = j;
    joy_gc[k] = SDL_IsGameController(device_index);
    memset(axis_on[k], 0, sizeof axis_on[k]);
    memset(hat_bits[k], 0, sizeof hat_bits[k]);
    static const int GUESS[4] = {0, 1, 2, 3};   /* the usual layout when SDL has no mapping */
    static const SDL_GameControllerAxis GC[4] = {SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY,
                                                 SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY};
    joy_ctl[k] = joy_gc[k] ? SDL_GameControllerOpen(device_index) : NULL;
    for (int i = 0; i < 4; i++) {
        stick_axis[k][i] = GUESS[i];
        if (joy_ctl[k]) {
            SDL_GameControllerButtonBind b = SDL_GameControllerGetBindForAxis(joy_ctl[k], GC[i]);
            if (b.bindType == SDL_CONTROLLER_BINDTYPE_AXIS) stick_axis[k][i] = b.value.axis;
        }
    }
    for (int a = 0; a < 16 && a < SDL_JoystickNumAxes(j); a++) {
        Sint16 rest = 0;
        if (!SDL_JoystickGetAxisInitialState(j, a, &rest)) rest = 0;
        axis_rest[k][a] = rest;
    }
    status("Controller connected: %s", SDL_JoystickName(j));
}

void joy_removed(SDL_JoystickID id)
{
    int k = joy_index(id);
    if (k < 0) return;
    status("Controller disconnected: %s", SDL_JoystickName(joys[k]));
    if (joy_ctl[k]) SDL_GameControllerClose(joy_ctl[k]);
    SDL_JoystickClose(joys[k]);
    for (int i = k; i < njoys - 1; i++) {
        joys[i] = joys[i + 1];
        joy_ctl[i] = joy_ctl[i + 1];
        memcpy(stick_axis[i], stick_axis[i + 1], sizeof stick_axis[i]);
        joy_gc[i] = joy_gc[i + 1];
        memcpy(axis_rest[i], axis_rest[i + 1], sizeof axis_rest[i]);
        memcpy(axis_on[i], axis_on[i + 1], sizeof axis_on[i]);
        memcpy(hat_bits[i], hat_bits[i + 1], sizeof hat_bits[i]);
    }
    njoys--;
}

/* Controller events as numbered inputs: buttons 0-99, hat directions 100+, axis pushes 200+. */
void joy_event(const SDL_Event *e)
{
    switch (e->type) {
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP: {
        int k = joy_index(e->jbutton.which);
        if (k >= 0 && e->jbutton.button < 100)
            input_event(SRC_JOY, e->jbutton.button, 127, e->type == SDL_JOYBUTTONDOWN, k);
        break;
    }
    case SDL_JOYHATMOTION: {
        int k = joy_index(e->jhat.which), h = e->jhat.hat;
        if (k < 0 || h >= 2) break;
        static const Uint8 bit[4] = {SDL_HAT_UP, SDL_HAT_RIGHT, SDL_HAT_DOWN, SDL_HAT_LEFT};
        for (int d = 0; d < 4; d++) {
            int was = hat_bits[k][h] & bit[d], is = e->jhat.value & bit[d];
            if (!was != !is) input_event(SRC_JOY, 100 + h * 4 + d, 127, is != 0, k);
        }
        hat_bits[k][h] = e->jhat.value;
        break;
    }
    case SDL_JOYAXISMOTION: {                /* sticks and triggers act as buttons past 60% */
        int k = joy_index(e->jaxis.which), a = e->jaxis.axis;
        if (k < 0 || a >= 16) break;
        if (joy_bend != JB_OFF && a == stick_axis[k][(joy_bend == JB_LEFT_Y ? 1 : joy_bend == JB_RIGHT_Y ? 3
                                                     : joy_bend == JB_LEFT_X ? 0 : 2)]) {
            /* the bend stick: up (or right) bends up, with a small dead zone, back to centre on release */
            double v = e->jaxis.value / 32767.0;
            if (joy_bend == JB_LEFT_Y || joy_bend == JB_RIGHT_Y) v = -v;
            double m = fabs(v) < 0.12 ? 0 : (fabs(v) - 0.12) / 0.88;
            float b = (float)(v < 0 ? -m : m);
            LOCK();
            P.bend = b < -1 ? -1 : b > 1 ? 1 : b;
            UNLOCK();
            break;
        }
        int d = e->jaxis.value - axis_rest[k][a];
        int range = axis_rest[k][a] < -16000 || axis_rest[k][a] > 16000 ? 65535 : 32767;
        int on_pos = d > range * 0.6, on_neg = d < -range * 0.6;
        int off_pos = d < range * 0.4, off_neg = d > -range * 0.4;
        if (!(axis_on[k][a] & 1) && on_pos) { axis_on[k][a] |= 1; input_event(SRC_JOY, 200 + a * 2, 127, 1, k); }
        else if ((axis_on[k][a] & 1) && off_pos) { axis_on[k][a] &= ~1; input_event(SRC_JOY, 200 + a * 2, 127, 0, k); }
        if (!(axis_on[k][a] & 2) && on_neg) { axis_on[k][a] |= 2; input_event(SRC_JOY, 201 + a * 2, 127, 1, k); }
        else if ((axis_on[k][a] & 2) && off_neg) { axis_on[k][a] &= ~2; input_event(SRC_JOY, 201 + a * 2, 127, 0, k); }
        break;
    }
    }
}

Held held_pad[NPADS];

void rec_close(Held *h, long end)
{
    if (!h->on) return;
    h->on = 0;
    long len = end - h->start;
    if (len <= 0) len = P.loop_end > h->start ? P.loop_end - h->start : SR / 10;   /* wrapped round a loop */
    long frames = P.samples[h->sample].frames;
    if (h->loop) {                          /* a looping pad: repeat the sound to fill the time */
        for (long at = 0; at < len; at += frames)
            clip_add(h->sample, h->track, h->start + at, 0, len - at < frames ? len - at : frames);
    } else {
        long l = len < frames ? len : frames;
        clip_add(h->sample, h->track, h->start, 0, l < SR / 100 ? SR / 100 : l);
    }
}

/* ---- takes: a recording from when it starts to when it stops lands on an open track (one with
 * nothing on it from the take's start on), or on a new track when there's none */
T_take take;

int rec_quantize = 1;            /* recorded notes snap to the grid when the take ends */

/* Filled shapes as triangles, not rows of lines: solid and smooth at any interface size. */
void poly_fill(const float *xy, int n, Col c)   /* a convex polygon, n points */
{
    SDL_Vertex v[72];
    int idx[210], ni = 0;
    if (n > 72) n = 72;
    for (int i = 0; i < n; i++) {
        v[i].position = (SDL_FPoint){xy[2 * i], xy[2 * i + 1]};
        v[i].color = (SDL_Color){c.r, c.g, c.b, c.a};
        v[i].tex_coord = (SDL_FPoint){0, 0};
    }
    for (int i = 1; i + 1 < n; i++) {
        idx[ni++] = 0;
        idx[ni++] = i;
        idx[ni++] = i + 1;
    }
    SDL_RenderGeometry(ren, NULL, v, n, idx, ni);
}

void disc_small(int cx, int cy, int r, Col c)
{
    float xy[2 * 40], fr = r + 0.5f;
    for (int i = 0; i < 40; i++) {
        float a = (float)i / 40 * 6.2831853f;
        xy[2 * i] = cx + 0.5f + fr * cosf(a);
        xy[2 * i + 1] = cy + 0.5f + fr * sinf(a);
    }
    poly_fill(xy, 40, c);
}

int take_track_for(int from_track)
{
    if (take.track >= 0) return take.track;
    for (int t = 0; t < P.ntracks; t++) {
        int busy = 0;
        for (int c = 0; c < P.nclips && !busy; c++)
            busy = P.clips[c].track == t && P.clips[c].start + P.clips[c].length > take.start;
        if (!busy) {
            int empty = 1;                      /* an unused track takes on the sound being played */
            for (int c = 0; c < P.nclips && empty; c++) empty = P.clips[c].track != t;
            if (empty && from_track >= 0 && from_track < P.ntracks && from_track != t) {
                LOCK();
                char name[32];
                snprintf(name, sizeof name, "%s", P.tracks[t].name);
                P.tracks[t] = P.tracks[from_track];
                snprintf(P.tracks[t].name, sizeof P.tracks[t].name, "%s", name);
                P.tracks[t].mute = P.tracks[t].solo = 0;
                UNLOCK();
            }
            return take.track = t;
        }
    }
    if (P.ntracks < MAX_TRACKS) {               /* none free: a new one, sounding the same */
        LOCK();
        int t = project_add_track(&P, NULL);
        if (t >= 0 && from_track >= 0 && from_track < P.ntracks) {
            P.tracks[t] = P.tracks[from_track];
            P.tracks[t].mute = P.tracks[t].solo = 0;
        }
        if (t >= 0) snprintf(P.tracks[t].name, sizeof P.tracks[t].name, "Take %d", take.number);
        UNLOCK();
        if (t >= 0) return take.track = t;
    }
    return from_track >= 0 && from_track < P.ntracks ? from_track : 0;   /* full: where it was playing */
}

/* Notes played on the instrument go into one note clip per take, on the take's track. */
int take_note_clip(long at)
{
    if (take.note_clip >= 0 && take.note_clip < P.nclips) return take.note_clip;
    int s = sample_base(&P, P.inst_sample);
    if (s < 0 || s >= P.nsamples) return -1;
    long start = bar_floor(at);
    int t = take_track_for(P.inst_track < P.ntracks ? P.inst_track : 0);
    int c = clip_add(s, t, start, 0, bar_len_at(start));
    if (c < 0) return -1;
    LOCK();
    P.clips[c].pattern = pattern_new_id(&P);
    P.clips[c].root = P.inst_root;
    UNLOCK();
    return take.note_clip = c;
}

void take_note_fit(void)                 /* the clip grows by bars to hold every note */
{
    Clip *c = &P.clips[take.note_clip];
    long end = c->start - c->offset + pattern_end(&P, c->pattern), len = bar_ceil(end) - c->start;
    if (len > c->length) {
        LOCK();
        c->length = len;
        UNLOCK();
    }
}

void take_note_on(int note, int vel, long frame)
{
    int c = take_note_clip(frame);
    if (c < 0) return;
    Clip *cl = &P.clips[c];
    if (frame < cl->start) {                     /* the loop came round to before the clip: it starts earlier */
        long ns = bar_floor(frame), d = cl->start - ns;
        LOCK();
        for (int i = 0; i < P.nnotes; i++)
            if (P.notes[i].pattern == cl->pattern) P.notes[i].start += d;
        cl->start = ns;
        cl->length += d;
        UNLOCK();
    }
    if (take.held[note] >= 0) take_note_off(note, frame);
    int i = note_add(cl->pattern, note, frame - cl->start + cl->offset, SR / 8, vel / 127.0f);
    take.held[note] = i;
    take.notes++;
    take_note_fit();
}

void take_note_off(int note, long frame)
{
    int i = take.held[note];
    take.held[note] = -1;
    if (i < 0 || i >= P.nnotes || take.note_clip < 0 || take.note_clip >= P.nclips) return;
    Clip *cl = &P.clips[take.note_clip];
    long abs = cl->start - cl->offset + P.notes[i].start, len = frame - abs;
    if (len <= 0) len = P.loop_end > abs ? P.loop_end - abs : SR / 10;   /* held round the end of a loop */
    LOCK();
    P.notes[i].length = len < SR / 100 ? SR / 100 : len;
    UNLOCK();
    take_note_fit();
}

void take_notes_finish(void)
{
    if (take.note_clip < 0 || take.note_clip >= P.nclips) return;
    for (int k = 0; k < 128; k++)
        if (take.held[k] >= 0) take_note_off(k, P.playhead);
    Clip *cl = &P.clips[take.note_clip];
    if (rec_quantize) notes_quantize(cl->pattern, cl->start - cl->offset, NULL, quant_grid(), 1.0);
    take_note_fit();
}

/* each frame: a take starts once play and Rec are both on (after any count-in), and ends when
 * either stops */
void take_check(void)
{
    int now = pads_rec && P.playing && countin_left == 0;
    if (now && !take.on) {
        take.on = 1;
        take.track = -1;
        take.hits = 0;
        take.number++;
        take.start = P.playhead;
        take.since = SDL_GetTicks();
        take.note_clip = -1;
        take.notes = 0;
        for (int i = 0; i < 128; i++) take.held[i] = -1;
        mic_take_start();                       /* and the microphone, if a track is armed for it */
    } else if (!now && take.on) {
        take.on = 0;
        double secs = (SDL_GetTicks() - take.since) / 1000.0;
        rec_drain();                            /* what was played up to the end belongs to this take */
        take_notes_finish();
        int mc = mic_take_end();
        if (mc >= 0) {
            char pos[48];
            pos_text(P.clips[mc].start, pos, sizeof pos);
            status("Recorded %.1f s from the microphone onto %s, from %s%s", (double)P.clips[mc].length / SR,
                   P.tracks[P.clips[mc].track].name, pos, take.hits || take.notes ? " (and what you played)" : "");
            return;
        }
        if (take.notes && !take.hits)
            status("Recorded %d note%s in %.1f s into a note clip on track %d (%s)%s", take.notes,
                   take.notes == 1 ? "" : "s", secs, take.track + 1, P.tracks[take.track].name,
                   rec_quantize ? ", quantized" : "");
        else if (take.hits)
            status("Recorded %d hit%s%s in %.1f s onto track %d (%s)", take.hits, take.hits == 1 ? "" : "s",
                   take.notes ? " and some notes" : "", secs, take.track + 1, P.tracks[take.track].name);
        else
            status("Recording stopped: nothing was played (%.1f s)", secs);
    }
}

/* Rec: arms recording, and starts playing (after the count-in, if there is one) when stopped. */
void rec_toggle(void)
{
    pads_rec = !pads_rec;
    if (pads_rec && !P.playing) toggle_play();
    if (pads_rec)
        status(countin_left > 0 ? "Recording after the count-in: play the pads or a MIDI keyboard"
                                : "Recording: play the pads or a MIDI keyboard; Stop or Rec ends the take");
    else if (!take.on)
        status("Recording off");
}

/* the take's own clock beside the transport: red while recording */
void rec_timer(SDL_Rect r)
{
    char buf[32];
    if (take.on) {
        double t = (SDL_GetTicks() - take.since) / 1000.0;
        snprintf(buf, sizeof buf, "REC %d:%04.1f", (int)(t / 60), fmod(t, 60));
        fill(r, rgb(190, 45, 40));
        if ((SDL_GetTicks() / 500) % 2 == 0) disc_small(r.x + 10, r.y + r.h / 2, 4, rgb(255, 255, 255));
        text_center((SDL_Rect){r.x + 14, r.y, r.w - 14, r.h}, buf, rgb(255, 255, 255));
    } else if (pads_rec) {
        outline(r, rgb(200, 60, 55));
        text_center(r, P.playing && countin_left > 0 ? "count-in..." : "REC armed", rgb(220, 70, 60));
    }
}

void rec_drain(void)
{
    RecEv evs[512];
    int n;
    LOCK();
    n = nrecq;
    memcpy(evs, recq, (size_t)n * sizeof *evs);
    nrecq = 0;
    UNLOCK();
    for (int i = 0; i < n; i++) {
        RecEv *e = &evs[i];
        if (e->pad >= 0) {
            Pad *pd = &P.pads[e->pad];
            Held *h = &held_pad[e->pad];
            if (!e->down) {
                rec_close(h, e->frame);
                continue;
            }
            if (pd->sample < 0) continue;
            LOCK();
            int s = sample_pitched(&P, pd->sample, pd->pitch);
            UNLOCK();
            if (s < 0) continue;
            int track = take_track_for(pd->track < P.ntracks ? pd->track : P.ntracks - 1);
            take.hits++;
            rec_close(h, e->frame);
            if (pd->mode == PAD_ONESHOT) clip_add(s, track, snap(e->frame), 0, P.samples[s].frames);
            else *h = (Held){1, snap(e->frame), s, track, pd->mode == PAD_LOOP};
        } else if (e->note >= 0 && e->note < 128) {   /* the instrument: notes in a note clip */
            if (!e->down) take_note_off(e->note, e->frame);
            else if (P.inst_sample >= 0) take_note_on(e->note, e->vel, e->frame);
        }
    }
    if (!P.playing || !pads_rec) {          /* stopped: finish anything still held */
        for (int i = 0; i < NPADS; i++) rec_close(&held_pad[i], P.playhead);
    }
}

/* ---- the Pads pane */

int pad_sel, pads_scroll, pads_content_h;

SDL_Rect pad_rects[NPADS], inst_drop;
const char *MODE_NAMES[PAD_MODES] = {"One-shot", "Hold", "Loop"};

void pad_assign(int pad, int sample)
{
    LOCK();
    P.pads[pad].sample = sample;
    UNLOCK();
    pad_sel = pad;
    status("Pad %d plays \"%s\"", pad + 1, P.samples[sample].name);
}

/* Library drops onto a pad or the instrument slot. Returns 1 when it was one. */
int pads_drop(int sample)
{
    if (!pads_open) return 0;
    for (int i = 0; i < NPADS; i++) {
        if (hit(pad_rects[i])) {
            pad_assign(i, sample);
            return 1;
        }
    }
    if (hit(inst_drop)) {
        LOCK();
        P.inst_sample = sample;
        UNLOCK();
        status("MIDI keys play \"%s\" (unpitched at %s)", P.samples[sample].name, "the root note");
        return 1;
    }
    return 0;
}

void track_picker(int x, int y, const char *label, int *track)
{
    text(x, y + 6, label, DIM);
    int lx = x + text_w(label) + 8;
    track_dropdown((SDL_Rect){lx, y, 160, 26}, "", track);
}

void device_row(int x, int y, int w, const char *name, Uint32 flash, Col c)
{
    Uint32 age = SDL_GetTicks() - flash;
    Col dot = flash && age < 200 ? HEAD : shade(c, 0.9f);
    fill((SDL_Rect){x, y + 4, 9, 9}, dot);
    text_clip((SDL_Rect){x + 16, y, w - 16, font_h + 2}, x + 16, y, name, TEXT);
}

int devices_section(int x, int y, int w)
{
    char buf[160];
    text(x, y, "INPUT DEVICES", DIM);
    y += font_h + 8;
    device_row(x, y, w, "Computer keyboard (plays pads while the Pads panel is open)", key_flash, ACCENT);
    y += font_h + 6;
    for (int i = 0; i < njoys; i++) {
        snprintf(buf, sizeof buf, "%s%s", SDL_JoystickName(joys[i]),
                 joy_gc[i] ? " (gamepad)" : " (controller)");
        device_row(x, y, w, buf, joy_flash[i], TRACK_COL[5]);
        y += font_h + 6;
    }
    char names[MIDI_MAX_DEVICES][64];
    int nm = midi_ok ? midi_devices(names, MIDI_MAX_DEVICES) : 0;
    for (int i = 0; i < nm; i++) {
        snprintf(buf, sizeof buf, "MIDI: %.63s", names[i]);
        device_row(x, y, w, buf, midi_flash[i], TRACK_COL[3]);
        y += font_h + 6;
    }
    if (!njoys) {
        text(x + 16, y, "No controllers: plug in a gamepad or USB controller", DIM);
        y += font_h + 6;
    }
    if (!nm) {
        snprintf(buf, sizeof buf, midi_ok ? "No MIDI devices: plug in a keyboard or pad controller" : "MIDI off: %s",
                 midi_error());
        text_clip((SDL_Rect){x + 16, y, w - 16, font_h + 2}, x + 16, y, buf, DIM);
        y += font_h + 6;
    }
    return y;
}

const char *const PAD_PRESETS[PAD_PRESET_COUNT] = {"Drum kit", "Drums + chords", "Chords of the key",
                                                          "Scale notes", "Bass notes", "Beat loops"};

int gn_sel;

int pads_content(int x, int y, int w)
{
    char buf[160], b2[64];
    Uint32 now = SDL_GetTicks();

    static const char *const SUBS[] = {"Pads", "Instrument"};
    y = subtabs(x, y, w, SUBS, 2, &pads_sub);
    if (pads_sub == 0) {
    char names[MIDI_MAX_DEVICES][64];
    int nm = midi_ok ? midi_devices(names, MIDI_MAX_DEVICES) : 0;
    snprintf(buf, sizeof buf, "Playing from: keyboard%s%s%s%s   (Settings > Input)", njoys ? ", " : "",
             njoys ? (njoys == 1 ? "1 controller" : "controllers") : "", nm ? ", " : "", nm ? "MIDI" : "");
    text_clip((SDL_Rect){x, y, w, font_h + 2}, x, y, buf, DIM);
    y += font_h + 10;
    y += 6;
    if (button((SDL_Rect){x, y, 76, 28}, pads_rec ? "● Rec" : "Rec", pads_rec)) rec_toggle();
    snprintf(buf, sizeof buf, "pads record hits (snap %s), MIDI keys record notes", SNAP_NAMES[snap_i]);
    text_clip((SDL_Rect){x + 86, y, w - 86, 28}, x + 86, y + (28 - font_h) / 2, buf, pads_rec ? HEAD : DIM);
    y += 34;
    if (button((SDL_Rect){x, y, w, 26}, rec_quantize ? "Quantize recorded notes: on (to the Snap grid)"
                                                     : "Quantize recorded notes: off (as played)", rec_quantize))
        rec_quantize = !rec_quantize;
    y += 36;

    {                                           /* preset layouts: fill all 16 pads at once */
        static int preset;
        if (dropdown((SDL_Rect){x, y, w - 76, 30}, "Preset: ", PAD_PRESETS, PAD_PRESET_COUNT, &preset, 0))
            load_pad_preset(preset);
        if (button((SDL_Rect){x + w - 70, y, 70, 30}, "Load", 0)) load_pad_preset(preset);   /* again, e.g. after a key change */
        y += 34;
        snprintf(buf, sizeof buf, "Uses the key in Theory (%s %s), the Beats style (%s)", pitch_name(th_root, key_uses_flats(th_root, th_scale)),
                 SCALES[th_scale].name, GENRES[gn_sel].name);
        text_clip((SDL_Rect){x, y, w, font_h + 2}, x, y, buf, DIM);
        text_clip((SDL_Rect){x, y + font_h + 2, w, font_h + 2}, x, y + font_h + 2,
                  "and the Tones wave. Ctrl+Z puts the old pads back.", DIM);
        y += 2 * font_h + 12;
    }
    text(x, y + 6, "PADS", DIM);
    if (button((SDL_Rect){x + w - 246, y, 150, 26}, "Fill from library", 0)) {
        int s = 0, filled = 0;
        LOCK();
        for (int i = 0; i < NPADS && s < P.nsamples; i++)
            if (P.pads[i].sample < 0) {
                while (s < P.nsamples && (P.samples[s].pitched || P.samples[s].removed || P.samples[s].hidden)) s++;   /* skip clip variants */
                if (s >= P.nsamples) break;
                P.pads[i].sample = s++;
                filled++;
            }
        UNLOCK();
        status(filled ? "Put %d library sounds on the empty pads" : "No empty pads, or the library is empty", filled);
    }
    if (button((SDL_Rect){x + w - 90, y, 90, 26}, "Clear all", 0)) {
        LOCK();
        voices_clear(&P);
        for (int i = 0; i < NPADS; i++) P.pads[i].sample = -1;
        UNLOCK();
    }
    y += 34;

    int gap = 6, pw = (w - 3 * gap) / 4, ph = 2 * font_h + 34;
    for (int i = 0; i < NPADS; i++) {
        SDL_Rect r = {x + (i % 4) * (pw + gap), y + (i / 4) * (ph + gap), pw, ph};
        pad_rects[i] = r;
        if (P.pads[i].sample >= 0 && P.pads[i].sample < P.nsamples)
            tip(r, "Pad %d: %s. Click or press its key to play; right-click to copy, paste or clear", i + 1,
                P.samples[P.pads[i].sample].name);
        else
            tip(r, "Pad %d is empty: drag a library sound onto it, or right-click to paste one", i + 1);
        Pad *pd = &P.pads[i];
        int has = pd->sample >= 0 && pd->sample < P.nsamples;
        Uint32 age = now - pad_flash[i];
        float glow = pad_flash[i] && age < 250 ? 1.0f - age / 250.0f : 0;
        Col base = has ? wash(TRACK_COL[pd->track % MAX_TRACKS], 0.34f + 0.5f * glow) : tone(PANEL2, 1 + glow);
        if (has && voice_playing(&P, i) && pd->mode == PAD_LOOP) base = wash(TRACK_COL[pd->track % MAX_TRACKS], 0.6f);
        int hov = hit(r) && drag.kind == D_NONE;
        int drop = drag.kind == D_LIB && hit(r);
        fill(r, hov ? tone(base, 1.2f) : base);
        outline(r, drop ? HEAD : i == pad_sel ? ACCENT : LINE);
        if (i == pad_sel) outline((SDL_Rect){r.x + 1, r.y + 1, r.w - 2, r.h - 2}, ACCENT);
        if (learn_pad == i && (now / 300) % 2) outline((SDL_Rect){r.x + 2, r.y + 2, r.w - 4, r.h - 4}, HEAD);
        const char *kname = binds[i].key >= 0 ? SDL_GetScancodeName(binds[i].key) : "";
        text(r.x + 6, r.y + 4, kname, HEAD);
        snprintf(b2, sizeof b2, "%d", i + 1);
        text(r.x + r.w - 6 - text_w(b2), r.y + 4, b2, DIM);
        const char *nm2 = has ? P.samples[pd->sample].name : "empty";
        text_clip((SDL_Rect){r.x + 4, r.y, r.w - 8, r.h}, r.x + 6, r.y + 8 + font_h, nm2, has ? TEXT : DIM);
        bind_label(i, buf, sizeof buf);
        text_clip((SDL_Rect){r.x + 4, r.y, r.w - 8, r.h}, r.x + 6, r.y + r.h - font_h - 5, buf, DIM);
        if (hov && lpress) {                /* click: select and play */
            pad_sel = i;
            mouse_pad = i;
            input_event(SRC_MOUSE, i, 110, 1, -1);
            lpress = 0;
        }
        if (hov && rpress) {                /* right-click: copy, paste, clear */
            rpress = 0;
            pad_sel = i;
            cm_open(CM_PAD, i);
        }
    }
    y += 4 * (ph + gap) + 8;

    /* the selected pad */
    Pad *pd = &P.pads[pad_sel];
    snprintf(buf, sizeof buf, "PAD %d", pad_sel + 1);
    text(x, y, buf, DIM);
    const char *snd = pd->sample >= 0 ? P.samples[pd->sample].name : "(empty: drag a library sound onto a pad)";
    text_clip((SDL_Rect){x + 60, y, w - 60, font_h + 2}, x + 60, y, snd, pd->sample >= 0 ? TEXT : DIM);
    y += font_h + 10;
    for (int m = 0; m < PAD_MODES; m++)
        if (button((SDL_Rect){x + m * 96, y, 90, 26}, MODE_NAMES[m], pd->mode == m)) {
            LOCK();
            voice_stop(&P, pad_sel, -1);
            pd->mode = m;
            UNLOCK();
        }
    text(x + 3 * 96 + 6, y + 6, pd->mode == PAD_ONESHOT ? "plays to the end" : pd->mode == PAD_HOLD ?
         "plays while held" : "press to start/stop", DIM);
    y += 34;
    static double vol, pitch;
    if (!(drag.kind == D_NUM && drag.num == &vol)) vol = pd->volume;
    if (!(drag.kind == D_NUM && drag.num == &pitch)) pitch = pd->pitch;
    int hw = (w - 8) / 2;
    number((SDL_Rect){x, y, hw, 26}, "Volume", &vol, 0, 1.5, 0.005, "%.2f");
    number((SDL_Rect){x + hw + 8, y, hw, 26}, "Pitch", &pitch, -24, 24, 0.1, "%+.0f st");
    if ((float)vol != pd->volume || (int)lround(pitch) != pd->pitch) {
        LOCK();
        pd->volume = (float)vol;
        pd->pitch = (int)lround(pitch);
        UNLOCK();
    }
    y += 34;
    double *pp = fproxy(&pd->pan);
    char pt[24];
    pan_text((float)*pp, pt, sizeof pt);
    number_text((SDL_Rect){x, y, hw, 26}, "Pan", pp, -1, 1, 0.005, pt);
    fcommit(&pd->pan, pp);
    text(x + hw + 12, y + 6, "uses its track's effects", DIM);
    y += 34;
    track_picker(x, y, "Track", &pd->track);
    if (button((SDL_Rect){x + w - 220, y, 110, 26}, learn_pad == pad_sel ? "Press input..." : "Learn input",
               learn_pad == pad_sel))
        learn_pad = learn_pad == pad_sel ? -1 : pad_sel;
    if (button((SDL_Rect){x + w - 104, y, 104, 26}, "Clear inputs", 0)) {
        LOCK();
        binds[pad_sel] = (Bind){-1, -1, -1};
        UNLOCK();
        binds_changed = 1;
    }
    y += 34;
    char jb[32], mb[32];
    joy_label(binds[pad_sel].joy, jb, sizeof jb);
    midi_label(binds[pad_sel].midi, mb, sizeof mb);
    snprintf(buf, sizeof buf, "Inputs:  key %s   controller %s   MIDI %s",
             binds[pad_sel].key >= 0 ? SDL_GetScancodeName(binds[pad_sel].key) : "-", jb, mb);
    text_clip((SDL_Rect){x, y, w, font_h + 2}, x, y, buf, learn_pad >= 0 ? HEAD : TEXT);
    y += font_h + 4;
    text(x, y, learn_pad >= 0 ? "Press a key, button or MIDI note (Esc cancels)"
                              : "Learn input, then press a key, button or MIDI note", DIM);
    y += font_h + 18;

    } else {
    /* the instrument */
    int hw = (w - 8) / 2;
    text(x, y, "INSTRUMENT", DIM);
    y += font_h + 4;
    text(x, y, "MIDI notes that aren't on a pad play this sound, pitched", DIM);
    y += font_h + 8;
    inst_drop = (SDL_Rect){x, y, w, 30};
    if (hit(inst_drop) && rpress && drag.kind == D_NONE) {
        rpress = 0;
        cm_open(CM_INST, 0);
    }
    int idrop = drag.kind == D_LIB && hit(inst_drop);
    fill(inst_drop, idrop ? PANEL3 : PANEL2);
    outline(inst_drop, idrop ? HEAD : LINE);
    snprintf(buf, sizeof buf, "%s", P.inst_sample >= 0 ? P.samples[P.inst_sample].name : "drop a library sound here");
    text_clip((SDL_Rect){x + 4, y, w - 150, 30}, x + 10, y + (30 - font_h) / 2, buf, P.inst_sample >= 0 ? TEXT : DIM);
    if (button((SDL_Rect){x + w - 140, y + 2, 136, 26}, "Generator sound", 0)) {
        GenParams g = G;
        g.a4 = P.a4;
        if (g.wave >= WAVE_NOISE) g.wave = WAVE_TRIANGLE;
        g.note = P.inst_root;
        if (g.release_ms < 80) g.release_ms = 80;
        int s = sample_generate(&P, &g);
        if (s >= 0) {
            LOCK();
            P.inst_sample = s;
            UNLOCK();
            status("MIDI keys play the generator's %s sound", WAVE_NAMES[g.wave]);
        }
    }
    y += 38;
    note_number((SDL_Rect){x, y, hw, 26}, "Root note", &P.inst_root);
    if (button((SDL_Rect){x + hw + 8, y, hw, 26}, inst_midi ? "MIDI keys: on" : "MIDI keys: off", inst_midi)) {
        inst_midi = !inst_midi;
        binds_changed = 1;
    }
    y += 34;
    track_picker(x, y, "Track", &P.inst_track);
    if (button((SDL_Rect){x + w - 150, y, 150, 26}, "No MIDI on pads", 0)) {
        LOCK();
        for (int i = 0; i < NPADS; i++) binds[i].midi = -1;
        UNLOCK();
        binds_changed = 1;
        status("MIDI notes no longer trigger pads: every key plays the instrument");
    }
    y += 34;
    text(x, y, "Pads use MIDI notes 36-51 (C2-D#3) by default, like", DIM);
    text(x, y + font_h + 2, "most pad controllers. Playing a piano keyboard? Press", DIM);
    text(x, y + 2 * font_h + 4, "No MIDI on pads so every key plays the instrument.", DIM);
    y += 3 * font_h + 12;
    }
    return y;
}

void pads_pane(void)
{
    SDL_Rect pane = {L.pane_x, L.top, L.pane_w, H - L.top - L.status};
    fill(pane, PANEL);
    line(pane.x + pane.w - 1, pane.y, pane.x + pane.w - 1, pane.y + pane.h, LINE);
    int inside = hit(pane), saved_press = lpress;
    if (!inside) lpress = 0;
    int max_scroll = pads_content_h - pane.h > 0 ? pads_content_h - pane.h : 0;
    pads_scroll = pads_scroll < 0 ? 0 : pads_scroll > max_scroll ? max_scroll : pads_scroll;
    SDL_RenderSetClipRect(ren, &pane);
    int y0 = pane.y + 12 - pads_scroll;
    int y = pads_content(pane.x + 14, y0, pane.w - 28);
    pads_content_h = y - y0 + 12;
    (void)max_scroll;
    scrollbar(pane, &pads_scroll, pads_content_h);
    SDL_RenderSetClipRect(ren, NULL);
    if (!inside) lpress = saved_press;
}

/* Keys go to the pads while the pane is open, unless Ctrl/Alt is held or a dialog is up. */
int pads_take_key(SDL_Keysym k)
{
    return (pads_open || show_open) && !(k.mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI)) && !menu_open && !confirm_act &&
           !export_open;
}

/* ---- File menu and the "save changes?" question */

SDL_Rect file_btn, met_btn, snap_rect, bpm_rect, a4_rect, ts_rect, change_flag[MAX_METER];

int met_open;

int menu_item(SDL_Rect r, const char *label, const char *shortcut, int enabled)
{
    int hov = enabled && hit(r);
    if (hov) fill(r, PANEL3);
    text(r.x + 12, r.y + (r.h - font_h) / 2, label, enabled ? TEXT : tone(DIM, 0.7f));
    if (shortcut) text(r.x + r.w - 12 - text_w(shortcut), r.y + (r.h - font_h) / 2, shortcut, DIM);
    if (hov && lpress) {
        lpress = 0;
        return 1;
    }
    return 0;
}

char example_path[16][1024], example_title[16][96];
int nexamples, example_pick;

void examples_scan(void)
{
    nexamples = 0;
    char dir[1100] = "examples";
    char *base = SDL_GetBasePath();
    if (base) {
        snprintf(dir, sizeof dir, "%sexamples", base);
        SDL_free(base);
    }
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && nexamples < 16) {
        size_t n = strlen(e->d_name);
        if (n < 6 || strcmp(e->d_name + n - 5, ".tdaw")) continue;
        snprintf(example_path[nexamples], sizeof example_path[0], "%.900s/%.100s", dir, e->d_name);
        snprintf(example_title[nexamples], sizeof example_title[0], "%.*s", (int)(n - 5), e->d_name);
        FILE *f = fopen(example_path[nexamples], "r");      /* its title line, if it has one */
        char line_[200];
        for (int k = 0; f && k < 4 && fgets(line_, sizeof line_, f); k++)
            if (!strncmp(line_, "title ", 6)) {
                line_[strcspn(line_, "\r\n")] = 0;
                snprintf(example_title[nexamples], sizeof example_title[0], "%.90s", line_ + 6);
            }
        if (f) fclose(f);
        nexamples++;
    }
    closedir(d);
    for (int i = 0; i < nexamples; i++)                        /* in title order */
        for (int j = i + 1; j < nexamples; j++)
            if (strcmp(example_title[j], example_title[i]) < 0) {
                char t[1024];
                memcpy(t, example_path[i], sizeof t);
                memcpy(example_path[i], example_path[j], sizeof t);
                memcpy(example_path[j], t, sizeof t);
                char u[96];
                memcpy(u, example_title[i], sizeof u);
                memcpy(example_title[i], example_title[j], sizeof u);
                memcpy(example_title[j], u, sizeof u);
            }
}

char recent[RECENT_MAX][1024];
int nrecent, recent_read, recent_pick = -1, recent_map_[RECENT_MAX];

const char *recent_file(void)
{
    static char path[1024];
    const char *e = SDL_getenv("TINYDAW_RECENT_FILE");
    if (e && *e) return e;
    if (!path[0]) {
        char dir[900];
        user_dir("config", dir, sizeof dir);
        snprintf(path, sizeof path, "%s/recent.txt", dir);
    }
    return path;
}

void recent_load(void)
{
    recent_read = 1;
    nrecent = 0;
    if (headless && !SDL_getenv("TINYDAW_RECENT_FILE")) return;
    FILE *f = fopen(recent_file(), "r");
    char line_[1100];
    while (f && nrecent < RECENT_MAX && fgets(line_, sizeof line_, f)) {
        line_[strcspn(line_, "\r\n")] = 0;
        if (line_[0]) snprintf(recent[nrecent++], sizeof recent[0], "%.1023s", line_);
    }
    if (f) fclose(f);
}

void recent_add(const char *path)        /* to the front of the list (opened or saved) */
{
    if (!recent_read) recent_load();
    if (!path || !path[0] || (headless && !SDL_getenv("TINYDAW_RECENT_FILE"))) return;
    char keep[1024];
    snprintf(keep, sizeof keep, "%s", path);
    int w = 0;
    for (int i = 0; i < nrecent; i++)
        if (strcmp(recent[i], keep)) memmove(recent[w++], recent[i], sizeof recent[0]);
    nrecent = w < RECENT_MAX - 1 ? w : RECENT_MAX - 1;
    memmove(recent[1], recent[0], (size_t)nrecent * sizeof recent[0]);
    snprintf(recent[0], sizeof recent[0], "%s", keep);
    nrecent++;
    FILE *f = fopen(recent_file(), "w");
    if (!f) return;
    for (int i = 0; i < nrecent; i++) fprintf(f, "%s\n", recent[i]);
    fclose(f);
}

long file_mtime(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (long)st.st_mtime : -1;
}

BrowseItem *bcache;                      /* every project looked at so far, read again when it changes */
int nbcache, capbcache;
int bitems[512], nbitems;                /* the tab's projects, as indexes into bcache */
int browse_open, browse_tab, browse_scroll, browse_content_h, browse_editing = -1;
char browse_q[64], browse_tag[48], browse_tags_buf[128];
SDL_Rect browse_card_r[512];

int browse_item(const char *path)        /* the cached look at a project (read now if new or changed) */
{
    long mt = file_mtime(path);
    if (mt < 0) return -1;
    for (int i = 0; i < nbcache; i++)
        if (!strcmp(bcache[i].path, path)) {
            if (bcache[i].mtime != mt) {
                bcache[i].mtime = mt;
                bcache[i].ok = project_peek(path, &bcache[i].info) == 0;
            }
            return bcache[i].ok ? i : -1;
        }
    if (nbcache >= capbcache) {
        int nc = capbcache ? capbcache * 2 : 32;
        BrowseItem *nb = realloc(bcache, sizeof(BrowseItem) * (size_t)nc);
        if (!nb) return -1;
        bcache = nb;
        capbcache = nc;
    }
    BrowseItem *b = &bcache[nbcache];
    snprintf(b->path, sizeof b->path, "%s", path);
    b->mtime = mt;
    b->ok = project_peek(path, &b->info) == 0;
    return b->ok ? nbcache++ : -1;
}

void browse_walk(const char *dir, int depth)   /* every .tdaw in a folder and the ones under it */
{
    DIR *d = opendir(dir);
    struct dirent *e;
    while (d && (e = readdir(d)) && nbitems < 512) {
        if (e->d_name[0] == '.') continue;
        char p[1100];
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        size_t n = strlen(e->d_name);
        if (n > 5 && !strcmp(e->d_name + n - 5, ".tdaw")) {
            int i = browse_item(p);
            if (i >= 0) bitems[nbitems++] = i;
        } else if (depth > 0) {
            struct stat st;
            if (stat(p, &st) == 0 && S_ISDIR(st.st_mode)) browse_walk(p, depth - 1);
        }
    }
    if (d) closedir(d);
}

int browse_cmp_mtime(const void *a, const void *b)
{
    long x = bcache[*(const int *)a].mtime, y = bcache[*(const int *)b].mtime;
    return x < y ? 1 : x > y ? -1 : 0;
}

void browse_projects_dir(char *buf, size_t n)
{
    const char *e = SDL_getenv("TINYDAW_PROJECTS_DIR");
    if (e && *e) {
        snprintf(buf, n, "%s", e);
        return;
    }
    char m[900];
    user_dir("music", m, sizeof m);
    snprintf(buf, n, "%s/tinydaw", m);
}

void browse_scan(void)
{
    nbitems = 0;
    if (browse_tab == 0) {                       /* recent: in the order they were last used */
        if (!recent_read) recent_load();
        for (int i = 0; i < nrecent && nbitems < 512; i++) {
            int k = browse_item(recent[i]);
            if (k >= 0) bitems[nbitems++] = k;
        }
    } else if (browse_tab == 1) {                /* yours: newest first */
        char d[1000];
        browse_projects_dir(d, sizeof d);
        browse_walk(d, 2);
        qsort(bitems, (size_t)nbitems, sizeof(int), browse_cmp_mtime);
    } else {
        examples_scan();
        for (int i = 0; i < nexamples && nbitems < 512; i++) {
            int k = browse_item(example_path[i]);
            if (k >= 0) bitems[nbitems++] = k;
        }
    }
}

void browse_show(void)
{
    if (!recent_read) recent_load();
    browse_open = 1;
    browse_scroll = 0;
    browse_editing = -1;
    if (!nrecent && browse_tab == 0) browse_tab = 1;
    browse_scan();
}

void browse_name(const BrowseItem *b, char *buf, size_t n)   /* its title, or its file name */
{
    if (b->info.title[0]) {
        snprintf(buf, n, "%s", b->info.title);
        return;
    }
    const char *base = path_base(b->path);
    size_t len = strlen(base);
    snprintf(buf, n, "%.*s", (int)(len > 5 ? len - 5 : len), base);
}

int has_tag(const char *tags, const char *tag)   /* comma-separated, any case */
{
    char tmp[128];
    snprintf(tmp, sizeof tmp, "%s", tags);
    for (char *t = strtok(tmp, ","); t; t = strtok(NULL, ",")) {
        while (*t == ' ') t++;
        size_t n = strlen(t);
        while (n && t[n - 1] == ' ') t[--n] = 0;
        if (!strcasecmp(t, tag)) return 1;
    }
    return 0;
}

int browse_matches(const BrowseItem *b)
{
    if (browse_tag[0] && !has_tag(b->info.tags, browse_tag)) return 0;
    if (!browse_q[0]) return 1;
    char name[128], hay[400];
    browse_name(b, name, sizeof name);
    snprintf(hay, sizeof hay, "%.127s %.120s %.127s", name, path_base(b->path), b->info.tags);
    for (char *q = hay; *q; q++) *q = (char)tolower((unsigned char)*q);
    char nd[64];
    snprintf(nd, sizeof nd, "%s", browse_q);
    for (char *q = nd; *q; q++) *q = (char)tolower((unsigned char)*q);
    return strstr(hay, nd) != NULL;
}

void browse_thumb(SDL_Rect r, const ProjectInfo *in)   /* its first 16 bars: sections, then each track's clips */
{
    fill(r, PANEL2);
    double fpb = in->ts_num * 4.0 / (in->ts_den ? in->ts_den : 4) * 60.0 / (in->bpm > 0 ? in->bpm : 120) * in->rate;
    double span = 16 * fpb;
    for (int b = 1; b < 16; b++) {
        int x = r.x + (int)(b * fpb / span * r.w);
        line(x, r.y, x, r.y + r.h - 1, b % 4 ? tone(PANEL2, 1.08f) : tone(PANEL2, 1.25f));
    }
    for (int i = 0; i < in->npsec; i++) {
        int x0 = r.x + (int)(in->sections[i].bar * fpb / span * r.w), x1 = r.x + (int)((in->sections[i].bar + in->sections[i].bars) * fpb / span * r.w);
        if (x0 >= r.x + r.w) continue;
        if (x1 > r.x + r.w) x1 = r.x + r.w;
        fill((SDL_Rect){x0, r.y, x1 - x0 - 1, 4}, sec_col(in->sections[i].name));
    }
    int rows = in->ntracks > 0 ? (in->ntracks < 12 ? in->ntracks : 12) : 1, top = r.y + 6, rh = (r.h - 7) / rows;
    for (int i = 0; i < in->npeek; i++) {
        int t = in->clips[i].track;
        if (t >= rows) continue;
        int x0 = r.x + (int)(in->clips[i].start / span * r.w), x1 = r.x + (int)((in->clips[i].start + in->clips[i].length) / span * r.w);
        if (x0 >= r.x + r.w) continue;
        if (x1 > r.x + r.w) x1 = r.x + r.w;
        if (x1 - x0 < 2) x1 = x0 + 2;
        fill((SDL_Rect){x0, top + t * rh, x1 - x0, rh > 2 ? rh - 1 : 1}, TRACK_COL[t % 16]);
    }
    outline(r, LINE);
}

void browse_when(long mt, char *buf, size_t n)   /* "today 14:02", "yesterday", "3 Nov 2026" */
{
    time_t now = time(NULL), t = (time_t)mt;
    struct tm a = *localtime(&now), b = *localtime(&t);
    if (a.tm_year == b.tm_year && a.tm_yday == b.tm_yday) strftime(buf, n, "today %H:%M", &b);
    else if (a.tm_year == b.tm_year && a.tm_yday - b.tm_yday == 1) snprintf(buf, n, "yesterday");
    else strftime(buf, n, "%d %b %Y", &b);
}

void browse_open_item(int k)
{
    const BrowseItem *b = &bcache[k];
    browse_open = 0;
    if (browse_tab == 2) {                        /* an example: an untitled copy, as the menu does */
        snprintf(act_path, sizeof act_path, "%s", b->path);
        request(ACT_OPEN_EXAMPLE, b->path);
    } else {
        request(ACT_OPEN_PATH, b->path);
    }
}

void browse_tags_commit(int k)
{
    BrowseItem *b = &bcache[k];
    char clean[128];                             /* "a, b ,c" as "a, b, c" */
    int w = 0;
    char tmp[128];
    snprintf(tmp, sizeof tmp, "%s", browse_tags_buf);
    clean[0] = 0;
    for (char *t = strtok(tmp, ","); t; t = strtok(NULL, ",")) {
        while (*t == ' ') t++;
        size_t n = strlen(t);
        while (n && t[n - 1] == ' ') t[--n] = 0;
        if (!n) continue;
        w += snprintf(clean + w, sizeof clean - (size_t)w, w ? ", %s" : "%s", t);
        if (w >= (int)sizeof clean - 1) break;
    }
    char *full = full_path(b->path);
    int open_now = proj_path[0] && full && !strcmp(full, proj_path);
    free(full);
    if (project_set_tags(b->path, clean) != 0) {
        status("Couldn't write the tags into %s", path_base(b->path));
        return;
    }
    if (open_now) snprintf(P.tags, sizeof P.tags, "%s", clean);   /* (and the open copy, so a save keeps them) */
    snprintf(b->info.tags, sizeof b->info.tags, "%s", clean);
    b->mtime = file_mtime(b->path);
    status(clean[0] ? "Tags on %s: %s" : "No tags on %s now", path_base(b->path), clean);
}

void browse_draw(void)
{
    SDL_Rect all = {0, 0, W, H};
    fill(all, BG);
    int x = 20, y = 14, w = W - 40;
    text(x, y + 6, "PROJECTS", DIM);
    static const char *const TABS[3] = {"Recent", "My projects", "Examples"};
    int tab = browse_tab;
    subtabs(x + 100, y, 420, TABS, 3, &tab);
    if (tab != browse_tab) {
        browse_tab = tab;
        browse_scroll = 0;
        browse_editing = -1;
        browse_tag[0] = 0;
        tf_end(1);
        browse_scan();
    }
    SDL_Rect sq = {x + 540, y, w - 540 - 260, 30};
    if (sq.w > 120) textfield(sq, browse_q, sizeof browse_q, 0, "search by name or tag");
    if (button((SDL_Rect){x + w - 250, y, 150, 30}, "Another file...", 0)) {
        browse_open = 0;
        request(ACT_OPEN, NULL);
    }
    tip((SDL_Rect){x + w - 250, y, 150, 30}, "Open a project from anywhere with your system's file dialog");
    if (button((SDL_Rect){x + w - 92, y, 92, 30}, "Close", 0)) browse_open = 0;
    y += 44;

    /* the tags in this tab, to filter by */
    char seen[40][48];
    int nseen = 0, tx = x;
    for (int i = 0; i < nbitems && nseen < 40; i++) {
        char tmp[128];
        snprintf(tmp, sizeof tmp, "%s", bcache[bitems[i]].info.tags);
        for (char *t = strtok(tmp, ","); t && nseen < 40; t = strtok(NULL, ",")) {
            while (*t == ' ') t++;
            size_t n = strlen(t);
            while (n && t[n - 1] == ' ') t[--n] = 0;
            int dup = !n;
            for (int k = 0; k < nseen && !dup; k++) dup = !strcasecmp(seen[k], t);
            if (!dup) snprintf(seen[nseen++], sizeof seen[0], "%.47s", t);
        }
    }
    if (nseen) {
        text(tx, y + 5, "Tags:", DIM);
        tx += text_w("Tags:") + 10;
        for (int k = 0; k < nseen; k++) {
            int cw = text_w(seen[k]) + 16;
            if (tx + cw > x + w) break;
            int on = !strcasecmp(browse_tag, seen[k]);
            if (button((SDL_Rect){tx, y, cw, 24}, seen[k], on)) {
                if (on) browse_tag[0] = 0;
                else snprintf(browse_tag, sizeof browse_tag, "%.47s", seen[k]);
                browse_scroll = 0;
            }
            tx += cw + 6;
        }
        y += 32;
    }

    /* the cards */
    SDL_Rect area = {x, y, w, H - y - 10};
    int cw = 300, ch = 172, gap = 14, cols = (w + gap) / (cw + gap);
    cols = cols < 1 ? 1 : cols;
    cw = (w - (cols - 1) * gap) / cols;
    int shown[512], ns = 0;
    for (int i = 0; i < nbitems; i++)
        if (browse_matches(&bcache[bitems[i]])) shown[ns++] = bitems[i];
    browse_content_h = ((ns + cols - 1) / cols) * (ch + gap);
    int maxs = browse_content_h - area.h > 0 ? browse_content_h - area.h : 0;
    if (hit(area) && wheel_y) {
        browse_scroll -= wheel_y * 60;
        wheel_y = 0;
    }
    browse_scroll = browse_scroll < 0 ? 0 : browse_scroll > maxs ? maxs : browse_scroll;
    if (!ns) {
        const char *why = browse_tab == 0 ? "Nothing opened or saved lately: projects you open or save show up here."
                        : browse_tab == 1 ? "No projects yet in Music/tinydaw: save one (Ctrl+S) and it shows up here."
                                          : "No examples found next to the program.";
        text(x, y + 10, browse_q[0] || browse_tag[0] ? "Nothing matches." : why, DIM);
    }
    SDL_RenderSetClipRect(ren, &area);
    char buf[200], nm[128], when[48];
    char *open_full = proj_path[0] ? full_path(proj_path) : NULL;
    for (int i = 0; i < ns; i++) {
        int k = shown[i];
        const BrowseItem *b = &bcache[k];
        const ProjectInfo *in = &b->info;
        SDL_Rect r = {x + (i % cols) * (cw + gap), y + (i / cols) * (ch + gap) - browse_scroll, cw, ch};
        browse_card_r[i] = r;
        if (r.y + r.h < area.y || r.y > area.y + area.h) continue;
        int hov = hit(r) && hit(area) && browse_editing != k;
        fill(r, hov ? PANEL3 : PANEL);
        outline(r, hov ? ACCENT : LINE);
        browse_thumb((SDL_Rect){r.x + 8, r.y + 8, r.w - 16, 66}, in);
        browse_name(b, nm, sizeof nm);
        int is_open = open_full && !strcmp(open_full, b->path);
        int ty = r.y + 82;
        text_clip((SDL_Rect){r.x + 10, ty, r.w - 20, font_h + 2}, r.x + 10, ty, nm, is_open ? ACCENT : HEAD);
        ty += font_h + 6;
        char key[40] = "";
        if (in->has_key && in->key_scale >= 0 && in->key_scale < NSCALES)
            snprintf(key, sizeof key, " · %s %s", pitch_name(in->key_root, key_uses_flats(in->key_root, in->key_scale)),
                     SCALES[in->key_scale].name);
        snprintf(buf, sizeof buf, "%.0f BPM%s · %d:%02d · %d track%s%s", in->bpm, key, (int)in->seconds / 60,
                 (int)in->seconds % 60, in->ntracks, in->ntracks == 1 ? "" : "s", in->nscenes ? " · session" : "");
        text_clip((SDL_Rect){r.x + 10, ty, r.w - 20, font_h + 2}, r.x + 10, ty, buf, TEXT);
        ty += font_h + 5;
        browse_when(b->mtime, when, sizeof when);
        snprintf(buf, sizeof buf, "%s%s", is_open ? "open now · " : "", when);
        text_clip((SDL_Rect){r.x + 10, ty, r.w - 20, font_h + 2}, r.x + 10, ty, buf, DIM);
        ty += font_h + 7;
        SDL_Rect tb = {r.x + r.w - 62, ty - 2, 54, 22};   /* tags, and the button that edits them */
        if (browse_editing == k) {
            SDL_Rect fr = {r.x + 8, ty - 2, r.w - 16, 24};
            textfield(fr, browse_tags_buf, sizeof browse_tags_buf, 0, "tags, with commas");
            if (tf.buf != browse_tags_buf) {      /* Enter or a click away keeps them; Esc leaves them */
                if (strcmp(browse_tags_buf, b->info.tags)) browse_tags_commit(k);
                browse_editing = -1;
            }
            continue;
        }
        int cx = r.x + 10;
        if (in->tags[0]) {
            char tmp[128];
            snprintf(tmp, sizeof tmp, "%s", in->tags);
            for (char *t = strtok(tmp, ","); t; t = strtok(NULL, ",")) {
                while (*t == ' ') t++;
                int tw2 = text_w(t) + 12;
                if (cx + tw2 > tb.x - 4) break;
                fill((SDL_Rect){cx, ty - 1, tw2, font_h + 4}, wash(ACCENT, 0.35f));
                text(cx + 6, ty + 1, t, TEXT);
                cx += tw2 + 4;
            }
        } else if (browse_tab != 2) {
            text(cx, ty + 1, "no tags", tone(DIM, 0.8f));
        }
        if (browse_tab != 2) {                    /* (the examples' tags stay as they come) */
            if (button(tb, "Tags", 0)) {
                browse_editing = k;
                snprintf(browse_tags_buf, sizeof browse_tags_buf, "%s", in->tags);
                tf_end(1);
                tf.buf = browse_tags_buf;
                tf.cap = sizeof browse_tags_buf;
                tf.path = 0;
                tf.caret = (size_t)-1;
                tf_drawn = 1;
                tf.r = (SDL_Rect){r.x + 8, ty - 2, r.w - 16, 24};
                snprintf(tf.orig, sizeof tf.orig, "%s", browse_tags_buf);
                SDL_StartTextInput();
            }
            tip(tb, "Words to find it by, with commas between them (they're kept in the project file)");
        }
        if (hov && lpress) {
            lpress = 0;
            free(open_full);
            open_full = NULL;
            SDL_RenderSetClipRect(ren, NULL);
            browse_open_item(k);
            return;
        }
        if (hov) tip(r, "%s: click to open it%s", b->path, browse_tab == 2 ? " (as an untitled copy)" : "");
    }
    free(open_full);
    SDL_RenderSetClipRect(ren, NULL);
    lpress = rpress = 0;                         /* nothing under the browser takes clicks */
}

void recent_menu(SDL_Rect anchor)        /* the newest few, as a drop-down */
{
    if (!recent_read) recent_load();
    int n = 0;
    for (int i = 0; i < nrecent && n < 10; i++)
        if (file_mtime(recent[i]) >= 0) {
            recent_map_[n] = i;
            snprintf(dd.items[n++], sizeof dd.items[0], "%.46s", path_base(recent[i]));
        }
    if (!n) {
        status("Nothing opened or saved lately");
        return;
    }
    dd.open = 1;
    dd.sel = &recent_pick;
    recent_pick = -1;
    dd.n = n;
    dd.anchor = anchor;
}

void examples_menu(SDL_Rect anchor)   /* the list of examples, as a drop-down */
{
    examples_scan();
    if (!nexamples) {
        status("No examples found: they belong in an examples folder next to the program");
        return;
    }
    dd.open = 1;
    dd.sel = &example_pick;
    example_pick = -1;
    dd.n = nexamples;
    for (int i = 0; i < nexamples; i++) snprintf(dd.items[i], sizeof dd.items[i], "%.46s", example_title[i]);
    dd.anchor = anchor;
}

void file_menu(void)
{
    int w = 270, ih = 30;
    static const struct { const char *label, *keys; } ITEMS[] = {
        {"New", "Ctrl+N"}, {"Browse projects...", "Ctrl+O"}, {"Open a recent project", NULL}, {"Open a file...", NULL},
        {"Save", "Ctrl+S"}, {"Save As...", "Ctrl+Shift+S"},
        {"Recover unsaved work", NULL}, {NULL, NULL}, {"Export audio...", "Ctrl+E"}, {NULL, NULL},
        {"Settings", "Ctrl+,"}, {"Full-screen show", "F11"}, {NULL, NULL}, {"Quit", "Ctrl+Q"},
        {NULL, NULL}, {"Open an example song", NULL}, {"Import a MIDI file...", NULL}, {"Export MIDI...", NULL},
        {"Mixing console", NULL}};
    int n = sizeof ITEMS / sizeof *ITEMS, hgt = 8;
    for (int i = 0; i < n; i++) hgt += ITEMS[i].label ? ih : 9;
    SDL_Rect m = {file_btn.x, file_btn.y + file_btn.h + 2, w, hgt + font_h + 12};
    fill(m, PANEL);
    outline(m, LINE);
    int y = m.y + 4, act = -1;
    for (int i = 0; i < n; i++) {
        if (!ITEMS[i].label) {
            line(m.x + 8, y + 4, m.x + w - 8, y + 4, LINE);
            y += 9;
            continue;
        }
        int enabled = i != 6 || autosave_exists();
        if (menu_item((SDL_Rect){m.x + 1, y, w - 2, ih}, ITEMS[i].label, ITEMS[i].keys, enabled)) act = i;
        y += ih;
    }
    line(m.x + 8, y + 4, m.x + w - 8, y + 4, LINE);
    text(m.x + 12, y + 9, "tinydaw " TINYDAW_VERSION, DIM);
    if (act >= 0 || lpress || rpress) menu_open = 0;   /* any click closes it */
    lpress = rpress = 0;
    switch (act) {
    case 0: request(ACT_NEW, NULL); break;
    case 1: browse_show(); break;
    case 2: recent_menu((SDL_Rect){m.x + m.w - 4, m.y + 4 + 2 * ih, 4, 4}); break;
    case 3: request(ACT_OPEN, NULL); break;
    case 4: save_project(0); break;
    case 5: save_project(1); break;
    case 6: request(ACT_RECOVER, NULL); break;
    case 8: open_export(); break;
    case 10: if (!settings_open) open_panel(RI_SETTINGS); break;
    case 11: show_toggle(); break;
    case 13: request(ACT_QUIT, NULL); break;
    case 15: examples_menu((SDL_Rect){m.x + m.w - 4, y - 4 * ih - font_h, 4, 4}); break;
    case 16: midi_file_dialog(0); break;
    case 17: midi_file_dialog(1); break;
    case 18: mixwin_open(); break;
    }
}

/* the change in force at frame f (index into P.changes), or -1 for the song's start */
int change_at(long f)
{
    int bar = (int)floor(meter_bars(&P, f) + 1e-9), k = -1;
    for (int i = 0; i < P.nchanges; i++)
        if (P.changes[i].bar <= bar) k = i;
    return k;
}

/* A change at the playhead's bar, starting with what's in force there. Returns its index. */
int change_add_here(void)
{
    int bar = (int)floor(meter_bars(&P, P.playhead) + 1e-9);
    if (bar < 1) {
        status("Bar 1's tempo and time signature are the song's own: set them with BPM and the box beside it");
        return -1;
    }
    for (int i = 0; i < P.nchanges; i++)
        if (P.changes[i].bar == bar) {
            status("Bar %d already has a change: BPM and the time signature box edit it", bar + 1);
            return i;
        }
    if (P.nchanges >= MAX_METER) {
        status("That's as many changes as a song can have (%d)", MAX_METER);
        return -1;
    }
    double bpm;
    int num, den;
    meter_at(&P, P.playhead, &bpm, &num, &den, NULL, NULL, NULL);
    int k = P.nchanges;
    while (k > 0 && P.changes[k - 1].bar > bar) k--;
    LOCK();
    memmove(&P.changes[k + 1], &P.changes[k], (size_t)(P.nchanges - k) * sizeof(MeterChange));
    P.changes[k] = (MeterChange){bar, bpm, num, den};
    P.nchanges++;
    P.playhead = meter_bar_frame(&P, bar);
    UNLOCK();
    status("A change at bar %d: set its tempo with BPM and its time signature in the box beside it", bar + 1);
    return k;
}

void change_remove(int k)
{
    if (k < 0 || k >= P.nchanges) return;
    int bar = P.changes[k].bar;
    LOCK();
    memmove(&P.changes[k], &P.changes[k + 1], (size_t)(P.nchanges - k - 1) * sizeof(MeterChange));
    P.nchanges--;
    UNLOCK();
    status("Removed the change at bar %d", bar + 1);
}

double se_map(const SongEdit *e, double x, int *gone)
{
    double len = e->b - e->a;
    *gone = 0;
    switch (e->mode) {
    case SE_MOVE:
        if (e->t <= e->a) {
            if (x >= e->a && x < e->b) return x - (e->a - e->t);
            if (x >= e->t && x < e->a) return x + len;
        } else {
            if (x >= e->a && x < e->b) return x + (e->t - e->b);
            if (x >= e->b && x < e->t) return x - len;
        }
        return x;
    case SE_COPY: return x >= e->b ? x + len : x;
    default:
        if (x >= e->a && x < e->b) *gone = 1;
        return x >= e->b ? x - len : x;
    }
}

void se_split_at(long f)            /* clips crossing frame f become two */
{
    int n = P.nclips;
    for (int c = 0; c < n && P.nclips < MAX_CLIPS; c++) {
        Clip *cl = &P.clips[c];
        if (cl->start >= f || cl->start + cl->length <= f) continue;
        long cut = f - cl->start;
        Clip r = *cl;
        r.start = f;
        r.offset = cl->offset + cut;
        r.length = cl->length - cut;
        r.fade_in = 0;
        if (r.fade_out > r.length) r.fade_out = 0;
        cl->length = cut;
        cl->fade_out = 0;
        if (cl->fade_in > cl->length) cl->fade_in = 0;
        P.clips[P.nclips] = r;
        csel[P.nclips] = 0;
        P.nclips++;
    }
}

/* where a frame is, as a whole bar and how far into it (0..1) */
void se_where(long f, int *bar, double *into)
{
    double x = meter_bars(&P, f);
    int b = (int)floor(x + 1e-9);
    long b0 = meter_bar_frame(&P, b), b1 = meter_bar_frame(&P, b + 1);
    *bar = b;
    *into = b1 > b0 ? (double)(f - b0) / (b1 - b0) : 0;
}

long se_frame(int bar, double into)
{
    long b0 = meter_bar_frame(&P, bar), b1 = meter_bar_frame(&P, bar + 1);
    return b0 + (long)llround(into * (b1 - b0));
}

void song_edit(SongEdit e)
{
    if (e.b <= e.a) return;
    int len = e.b - e.a;
    LOCK();
    /* 1. the tempo and time signature written out where the edit's edges are, so each part keeps its own */
    MeterChange m[MAX_METER + 8];
    int nm = 0;
    m[nm++] = (MeterChange){0, P.bpm, P.ts_num, P.ts_den};
    for (int i = 0; i < P.nchanges; i++) m[nm++] = P.changes[i];
    int edges[3] = {e.a, e.b, e.mode == SE_MOVE ? e.t : 0};
    for (int k = 0; k < 3; k++) {
        int bar = edges[k], have = 0;
        if (bar <= 0) continue;
        for (int i = 0; i < nm; i++) have |= m[i].bar == bar;
        if (have || nm >= MAX_METER + 8) continue;
        double bpm;
        int num, den;
        meter_at(&P, meter_bar_frame(&P, bar), &bpm, &num, &den, NULL, NULL, NULL);
        m[nm++] = (MeterChange){bar, bpm, num, den};
    }
    /* 2. clips split at the edges, then where everything is, in bars */
    se_split_at(meter_bar_frame(&P, e.a));
    se_split_at(meter_bar_frame(&P, e.b));
    if (e.mode == SE_MOVE) se_split_at(meter_bar_frame(&P, e.t));
    int *cb = malloc(sizeof(int) * (size_t)(P.nclips + 1)), *ab = malloc(sizeof(int) * (size_t)(P.nautos + 1));
    double *ci = malloc(sizeof(double) * (size_t)(P.nclips + 1)), *ai = malloc(sizeof(double) * (size_t)(P.nautos + 1));
    if (!cb || !ab || !ci || !ai) {
        UNLOCK();
        free(cb), free(ab), free(ci), free(ai);
        status("Out of memory");
        return;
    }
    for (int c = 0; c < P.nclips; c++) se_where(P.clips[c].start, &cb[c], &ci[c]);
    for (int i = 0; i < P.nautos; i++) se_where(P.autos[i].at, &ab[i], &ai[i]);
    /* chords split at the edges too, and where each starts, in bars; and the key written out at the
     * edges, like the tempo, so each part keeps its own */
    for (int k = 0; k < 3; k++)
        if (k < 2 || e.mode == SE_MOVE) chord_split(&P, meter_beats(&P, meter_bar_frame(&P, edges[k] > 0 ? edges[k] : 0)));
    static int chb[MAX_CHORDS];
    static double chi[MAX_CHORDS];
    for (int i = 0; i < P.nchords; i++) se_where(meter_beats_frame(&P, P.chords[i].at), &chb[i], &chi[i]);
    KeyChange km[MAX_KEYS + 4];
    int nkm = 0;
    km[nkm++] = (KeyChange){0, P.key_root, P.key_scale};
    for (int i = 0; i < P.nkeys; i++) km[nkm++] = P.keys[i];
    for (int k = 0; k < 3; k++) {
        int bar = edges[k], have = 0;
        if (bar <= 0 || (k == 2 && e.mode != SE_MOVE)) continue;
        for (int i = 0; i < nkm; i++) have |= km[i].bar == bar;
        if (have || nkm >= MAX_KEYS + 4) continue;
        KeyChange kc = {bar, 0, 0};
        key_at_bar(&P, bar, &kc.root, &kc.scale);
        km[nkm++] = kc;
    }
    /* 3. the tempo map, remapped */
    MeterChange nmap[2 * (MAX_METER + 8)];
    int nn = 0, gone;
    for (int i = 0; i < nm; i++) {
        double nb = se_map(&e, m[i].bar, &gone);
        if (!gone) nmap[nn++] = (MeterChange){(int)nb, m[i].bpm, m[i].num, m[i].den};
        if (e.mode == SE_COPY && m[i].bar >= e.a && m[i].bar < e.b) nmap[nn++] = (MeterChange){m[i].bar + len, m[i].bpm, m[i].num, m[i].den};
    }
    for (int i = 1; i < nn; i++)                 /* in bar order */
        for (int j = i; j > 0 && nmap[j - 1].bar > nmap[j].bar; j--) {
            MeterChange t = nmap[j];
            nmap[j] = nmap[j - 1];
            nmap[j - 1] = t;
        }
    if (nn && nmap[0].bar == 0) {
        P.bpm = nmap[0].bpm;
        P.ts_num = nmap[0].num;
        P.ts_den = nmap[0].den;
    }
    P.nchanges = 0;
    double pb = P.bpm;
    int pn = P.ts_num, pd = P.ts_den;
    for (int i = 0; i < nn; i++) {               /* only real changes, once a bar */
        if (nmap[i].bar <= 0 || (i + 1 < nn && nmap[i + 1].bar == nmap[i].bar)) continue;
        if (nmap[i].bpm == pb && nmap[i].num == pn && nmap[i].den == pd) continue;
        if (P.nchanges >= MAX_METER) break;
        P.changes[P.nchanges++] = nmap[i];
        pb = nmap[i].bpm;
        pn = nmap[i].num;
        pd = nmap[i].den;
    }
    /* 4. clips and automation to their new places */
    int n0 = P.nclips, w = 0;
    for (int c = 0; c < n0; c++) {
        Clip cl = P.clips[c];
        if (e.mode == SE_COPY && cb[c] >= e.a && cb[c] < e.b && P.nclips < MAX_CLIPS) {   /* the copy */
            Clip cp = cl;
            cp.start = se_frame(cb[c] + len, ci[c]);
            P.clips[P.nclips] = cp;
            csel[P.nclips] = 0;
            cb[P.nclips] = -1;
            P.nclips++;
        }
        double nb = se_map(&e, cb[c], &gone);
        if (gone) continue;
        cl.start = se_frame((int)nb, ci[c]);
        P.clips[w] = cl;
        csel[w] = 0;
        w++;
    }
    for (int c = n0; c < P.nclips; c++) {       /* the copies, after what was kept */
        P.clips[w] = P.clips[c];
        csel[w] = 0;
        w++;
    }
    P.nclips = w;
    int na0 = P.nautos, wa = 0;
    AutoPoint extra[MAX_AUTO];
    int nx = 0;
    for (int i = 0; i < na0; i++) {
        AutoPoint ap = P.autos[i];
        if (e.mode == SE_COPY && ab[i] >= e.a && ab[i] < e.b && nx < MAX_AUTO) {
            extra[nx] = ap;
            extra[nx++].at = se_frame(ab[i] + len, ai[i]);
        }
        double nb = se_map(&e, ab[i], &gone);
        if (gone) continue;
        ap.at = se_frame((int)nb, ai[i]);
        P.autos[wa++] = ap;
    }
    for (int i = 0; i < nx && wa < MAX_AUTO; i++) P.autos[wa++] = extra[i];
    P.nautos = wa;
    auto_sort(&P);
    /* 5. sections */
    int ws = 0, ns0 = P.nsections;
    Section copy = {0};
    int has_copy = 0;
    for (int i = 0; i < ns0; i++) {
        Section sc = P.sections[i];
        if (e.mode == SE_COPY && sc.bar == e.a && sc.bars == len) {
            copy = sc;
            copy.bar = e.b;
            has_copy = 1;
        }
        double nb = se_map(&e, sc.bar, &gone);
        if (gone) continue;
        sc.bar = (int)nb;
        P.sections[ws++] = sc;
    }
    P.nsections = ws;
    if (has_copy && P.nsections < MAX_SECTIONS) P.sections[P.nsections++] = copy;
    for (int i = 1; i < P.nsections; i++)
        for (int j = i; j > 0 && P.sections[j - 1].bar > P.sections[j].bar; j--) {
            Section t = P.sections[j];
            P.sections[j] = P.sections[j - 1];
            P.sections[j - 1] = t;
        }
    /* 6. chords, by bar and how far into it, under the new tempo map */
    {
        static SongChord out[MAX_CHORDS];
        int no = 0;
        for (int i = 0; i < P.nchords; i++) {
            SongChord c = P.chords[i];
            if (e.mode == SE_COPY && chb[i] >= e.a && chb[i] < e.b && no < MAX_CHORDS) {
                SongChord cp = c;
                cp.at = meter_beats(&P, se_frame(chb[i] + len, chi[i]));
                out[no++] = cp;
            }
            double nb = se_map(&e, chb[i], &gone);
            if (gone || no >= MAX_CHORDS) continue;
            c.at = meter_beats(&P, se_frame((int)nb, chi[i]));
            out[no++] = c;
        }
        memcpy(P.chords, out, (size_t)no * sizeof(SongChord));
        P.nchords = no;
        chords_sort(&P);
        for (int i = 0; i + 1 < P.nchords; i++)   /* rounding never makes two overlap */
            if (P.chords[i].at + P.chords[i].len > P.chords[i + 1].at) P.chords[i].len = P.chords[i + 1].at - P.chords[i].at;
        for (int i = P.nchords - 1; i >= 0; i--)
            if (P.chords[i].len <= 1e-6) chord_remove(&P, i);
    }
    /* 7. the key map, remapped like the tempo map */
    {
        KeyChange nk[2 * (MAX_KEYS + 4)];
        int nn2 = 0;
        for (int i = 0; i < nkm; i++) {
            double nb = se_map(&e, km[i].bar, &gone);
            if (!gone) nk[nn2++] = (KeyChange){(int)nb, km[i].root, km[i].scale};
            if (e.mode == SE_COPY && km[i].bar >= e.a && km[i].bar < e.b) nk[nn2++] = (KeyChange){km[i].bar + len, km[i].root, km[i].scale};
        }
        for (int i = 1; i < nn2; i++)
            for (int j = i; j > 0 && nk[j - 1].bar > nk[j].bar; j--) {
                KeyChange t = nk[j];
                nk[j] = nk[j - 1];
                nk[j - 1] = t;
            }
        int r0 = P.key_root, s0 = P.key_scale;
        for (int i = 0; i < nn2 && nk[i].bar == 0; i++) r0 = nk[i].root, s0 = nk[i].scale;
        P.key_root = r0;
        P.key_scale = s0;
        P.nkeys = 0;
        int pr = r0, ps = s0;
        for (int i = 0; i < nn2; i++) {           /* only real changes, once a bar */
            if (nk[i].bar <= 0 || (i + 1 < nn2 && nk[i + 1].bar == nk[i].bar)) continue;
            if (nk[i].root == pr && nk[i].scale == ps) continue;
            if (P.nkeys >= MAX_KEYS) break;
            P.keys[P.nkeys++] = nk[i];
            pr = nk[i].root;
            ps = nk[i].scale;
        }
    }
    UNLOCK();
    selected = -1;
    free(cb);
    free(ab);
    free(ci);
    free(ai);
}

/* The metronome's options and the tempo, under the arrow next to Click. */
void metronome_menu(void)
{
    int w = 380, h = 404 + 44 + (P.nchanges ? 30 : 0);
    SDL_Rect m = {met_btn.x + met_btn.w - w, met_btn.y + met_btn.h + 2, w, h};
    if (m.x < 4) m.x = 4;
    fill(m, PANEL);
    outline(m, LINE);
    int x = m.x + 14, y = m.y + 12, iw = w - 28, changed = 0;
    char buf[64];
    text(x, y, "METRONOME", DIM);
    y += font_h + 8;
    if (button((SDL_Rect){x, y, 110, 28}, MET.on ? "Click: on" : "Click: off", MET.on)) MET.on = !MET.on, changed = 1;
    double vol = MET.volume;
    number((SDL_Rect){x + 118, y, iw - 118, 28}, "Volume", &vol, 0, 1.5, 0.005, "%.2f");
    if ((float)vol != MET.volume) MET.volume = (float)vol, changed = 1;
    y += 36;
    text(x, y + 7, "Sound", DIM);
    int bw = (iw - 80 - 8) / 3;
    for (int i = 0; i < CLICK_SOUNDS; i++)
        if (button((SDL_Rect){x + 80 + i * (bw + 4), y, bw, 28}, CLICK_NAMES[i], MET.sound == i)) {
            MET.sound = i;
            metronome_prepare();
            changed = 1;
        }
    y += 34;
    text(x, y + 7, "Ticks", DIM);
    static const char *const SUBN[] = {"Beats", "8ths", "16ths"};
    static const int SUBV[] = {1, 2, 4};
    for (int i = 0; i < 3; i++)
        if (button((SDL_Rect){x + 80 + i * (bw + 4), y, bw, 28}, SUBN[i], MET.sub == SUBV[i])) MET.sub = SUBV[i], changed = 1;
    y += 34;
    if (button((SDL_Rect){x, y, iw, 28}, MET.accent ? "Louder first beat of each bar: on" : "Louder first beat of each bar: off",
               MET.accent))
        MET.accent = !MET.accent, changed = 1;
    y += 34;
    text(x, y + 7, "Count-in", DIM);
    static const char *const CIN[] = {"Off", "1 bar", "2 bars"};
    for (int i = 0; i < 3; i++)
        if (button((SDL_Rect){x + 80 + i * (bw + 4), y, bw, 28}, CIN[i], MET.countin == i)) MET.countin = i, changed = 1;
    y += 34;
    if (button((SDL_Rect){x + 80, y, iw - 80, 28}, MET.countin_rec_only ? "Only when recording (Rec)" : "Every time play starts",
               0))
        MET.countin_rec_only = !MET.countin_rec_only, changed = 1;
    y += 42;
    text(x, y, "TEMPO", DIM);
    y += font_h + 8;
    double tempo_before = P.bpm;
    number((SDL_Rect){x, y, 120, 28}, "BPM", &P.bpm, 40, 300, 0.25, "%.1f");
    static const char *const TB[] = {"-1", "+1", "/2", "x2"};
    for (int i = 0; i < 4; i++)
        if (button((SDL_Rect){x + 128 + i * 46, y, 42, 28}, TB[i], 0)) {
            double v = i == 0 ? P.bpm - 1 : i == 1 ? P.bpm + 1 : i == 2 ? P.bpm / 2 : P.bpm * 2;
            P.bpm = v < 40 ? 40 : v > 300 ? 300 : round(v * 4) / 4;
        }
    tempo_touch(tempo_before);
    y += 36;
    if (button((SDL_Rect){x, y, 150, 28}, "Tap tempo (T)", 0)) tap_tempo();
    {
        double bpm;
        long bl;
        meter_at(&P, P.playhead, &bpm, NULL, NULL, NULL, NULL, &bl);
        snprintf(buf, sizeof buf, "beat %.0f ms, bar %.2f s", 60000.0 / bpm, (double)bl / SR);
    }
    text_clip((SDL_Rect){x + 158, y, iw - 158, 28}, x + 158, y + 7, buf, DIM);
    y += 34;
    {                                               /* tempo and time signature changes */
        int here = change_at(P.playhead), at_bar = here >= 0 && P.changes[here].bar ==
                                                   (int)floor(meter_bars(&P, P.playhead) + 1e-9);
        if (button((SDL_Rect){x, y, 220, 28}, "+ Change tempo / time here", 0)) change_add_here();
        if (button2((SDL_Rect){x + 226, y, iw - 226, 28}, "Remove it", 0, at_bar)) change_remove(here);
        y += 34;
        if (P.nchanges) {
            snprintf(buf, sizeof buf, "%d change%s: they show above the bar numbers", P.nchanges, P.nchanges == 1 ? "" : "s");
            text(x, y + 4, buf, DIM);
            y += 30;
        }
    }
    text(x, y + 2, "M toggles the click. BPM and the box beside it edit", DIM);
    text(x, y + font_h + 4, "the part of the song the playhead is in.", DIM);
    if (changed) settings_save();
    if ((lpress || rpress) && !hit(m) && !hit(met_btn)) met_open = 0;   /* a click outside closes it */
    if (hit(m)) lpress = rpress = 0;
}

void confirm_dialog(void)
{
    fill((SDL_Rect){0, 0, W, H}, alpha(DARK, 170));
    int w = 520, h = 150;
    SDL_Rect d = {(W - w) / 2, (H - h) / 2, w, h};
    fill(d, PANEL);
    outline(d, LINE);
    char buf[1200];
    snprintf(buf, sizeof buf, "Save changes to %s?", proj_path[0] ? base_name(proj_path) : "this project");
    text(d.x + 20, d.y + 20, buf, TEXT);
    const char *what = confirm_act == ACT_QUIT ? "quit" : confirm_act == ACT_NEW ? "start a new project"
                     : "open another project";
    snprintf(buf, sizeof buf, "Your changes will be lost if you %s without saving.", what);
    text(d.x + 20, d.y + 24 + font_h + 6, buf, DIM);
    int by = d.y + h - 50;
    int act = confirm_act;
    if (button((SDL_Rect){d.x + 20, by, 120, 32}, "Don't save", 0)) {
        if (act == ACT_QUIT) remove(autosave_file());   /* you chose to throw it away */
        run_action(act);
    }
    if (button((SDL_Rect){d.x + w - 250, by, 110, 32}, "Cancel", 0)) confirm_act = ACT_NONE;
    if (button((SDL_Rect){d.x + w - 130, by, 110, 32}, "Save", 1)) {
        confirm_act = ACT_NONE;
        if (proj_path[0]) {
            if (save_to(proj_path) == 0) run_action(act);
        } else {
            after_save_act = act;
            file_dialog(1);
        }
    }
}

void waiting_overlay(const char *msg)
{
    fill((SDL_Rect){0, 0, W, H}, alpha(DARK, 150));
    SDL_Rect r = {(W - 360) / 2, (H - 60) / 2, 360, 60};
    fill(r, PANEL);
    outline(r, ACCENT);
    text_center(r, msg, TEXT);
}

void clip_variant(int ci, int semis, int reverse)
{
    if (ci == ED_LIB) clip_variant_c(&ed_lib, semis, reverse);
    else if (ci >= 0 && ci < P.nclips) clip_variant_c(&P.clips[ci], semis, reverse);
}

void clip_variant_c(Clip *c, int semis, int reverse)
{
    const Sample *old = &P.samples[c->sample];
    int old_rev = old->pitched ? old->reversed : 0;
    int base = sample_base(&P, c->sample);
    int ns = sample_variant(&P, base, semis, reverse);    /* made outside the lock: can take a moment */
    if (ns < 0) {
        status("Library is full");
        return;
    }
    if (ns == c->sample) return;
    if (c->pattern > 0) {                       /* a note clip: its notes play the shifted sound */
        LOCK();
        c->sample = ns;
        UNLOCK();
        return;
    }
    double ratio = (double)P.samples[ns].frames / old->frames;
    long nf = P.samples[ns].frames;
    long off = llround(c->offset * ratio), len = llround(c->length * ratio);
    if (!old_rev != !reverse) off = nf - off - len;         /* the same stretch of sound, mirrored */
    if (off < 0) off = 0;
    if (off > nf - 1) off = nf - 1;
    if (off + len > nf) len = nf - off;
    if (len < 1) len = 1;
    LOCK();
    c->sample = ns;
    c->offset = off;
    c->length = len;
    c->fade_in = llround(c->fade_in * ratio);
    c->fade_out = llround(c->fade_out * ratio);
    if (c->fade_in + c->fade_out > len) c->fade_in = c->fade_out = 0;
    UNLOCK();
}

int clip_semis(const Clip *c) { return P.samples[c->sample].pitched ? P.samples[c->sample].semis : 0; }

int clip_rev(const Clip *c) { return P.samples[c->sample].pitched ? P.samples[c->sample].reversed : 0; }

int track_has_fx(const Track *t)
{
    return t->filter || t->drive > 0 || t->eq[0] || t->eq[1] || t->eq[2] || t->delay_send > 0 ||
           t->reverb_send > 0 || t->duck > 0 || t->pan != 0;
}

/* ---- the Mixer pane */

int mixer_scroll, mixer_content_h;

int section(int x, int y, int w, const char *title)
{
    line(x, y + font_h / 2, x + w, y + font_h / 2, LINE);
    int tw = text_w(title) + 12;
    fill((SDL_Rect){x, y, tw, font_h + 2}, PANEL);
    text(x, y, title, DIM);
    return y + font_h + 10;
}

/* ---- Mixer > Clip > Pitch bend: a curve drawn over the selected clip(s), for any kind of clip */

SDL_Rect cb_box;                         /* where the curve is drawn (for the self-test) */

double cb_last = -1;                     /* the last point of a stroke, seconds from the origin */

/* The selected clip's own curve, ready to change: made if it has none, copied if other clips share it
 * (so editing one copy leaves the others), and given to every selected clip. */
Bend *cb_edit(void)
{
    if (selected < 0 || selected >= P.nclips) return NULL;
    Clip *c = &P.clips[selected];
    int shared = 0;
    for (int i = 0; i < P.nclips && c->bend; i++) shared |= i != selected && !csel[i] && P.clips[i].bend == c->bend;
    if (!c->bend || shared || !bend_find(&P, c->bend)) {
        const Bend *old = bend_find_c(&P, c->bend);
        Bend copy;
        int had = old != NULL;
        if (had) copy = *old;
        int id = bend_new(&P);
        if (!id) {
            status("Too many pitch curves in this song (%d)", MAX_BENDS);
            return NULL;
        }
        Bend *b = bend_find(&P, id);
        if (had) {
            copy.id = id;
            *b = copy;
        }
        LOCK();
        for (int i = 0; i < P.nclips; i++)
            if (i == selected || csel[i]) P.clips[i].bend = id;
        UNLOCK();
    } else {
        for (int i = 0; i < P.nclips; i++)
            if (csel[i]) P.clips[i].bend = c->bend;
    }
    return bend_find(&P, P.clips[selected].bend);
}

void cb_erase(Bend *b, double a, double z)
{
    if (a > z) {
        double t = a;
        a = z;
        z = t;
    }
    int w = 0;
    for (int i = 0; i < b->n; i++)
        if (b->at[i] < a || b->at[i] > z) {
            b->at[w] = b->at[i];
            b->semis[w++] = b->semis[i];
        }
    b->n = w;
}

void cb_put(Bend *b, double at, double semis)
{
    if (b->n >= BEND_POINTS || at < 0) return;
    int k = b->n;
    while (k > 0 && b->at[k - 1] > at) k--;
    memmove(&b->at[k + 1], &b->at[k], (size_t)(b->n - k) * sizeof(float));
    memmove(&b->semis[k + 1], &b->semis[k], (size_t)(b->n - k) * sizeof(float));
    b->at[k] = (float)at;
    b->semis[k] = (float)(semis < -24 ? -24 : semis > 24 ? 24 : semis);
    b->n++;
}

void cb_preset(int kind)
{
    if (kind == CBP_FLAT) {                      /* straight again: the clip lets go of its curve */
        LOCK();
        for (int i = 0; i < P.nclips; i++)
            if (i == selected || csel[i]) P.clips[i].bend = 0;
        UNLOCK();
        status("No pitch bend: the clip plays straight");
        return;
    }
    Bend *b = cb_edit();
    if (!b) return;
    const Clip *c = &P.clips[selected];
    double a = (double)c->offset / SR, z = (double)(c->offset + c->length) / SR;   /* the clip, in curve time */
    b->n = 0;
    if ((kind == CBP_SCOOP || kind == CBP_FALL) && c->pattern > 0) {   /* a note clip: each note */
        for (int i = 0; i < P.nnotes && b->n < BEND_POINTS - 3; i++) {
            const Note *nt = &P.notes[i];
            if (nt->pattern != c->pattern) continue;
            double s0 = (double)nt->start / SR, e0 = (double)(nt->start + nt->length) / SR;
            if (e0 < a || s0 > z) continue;
            int later = 0;                       /* a note straight after: no fall-off into it */
            for (int k = 0; k < P.nnotes && !later; k++)
                later = P.notes[k].pattern == c->pattern && P.notes[k].start > nt->start &&
                        (double)P.notes[k].start / SR - e0 < 0.1;
            if (kind == CBP_SCOOP) {
                double d = fmin(0.12, (e0 - s0) / 3);
                cb_erase(b, s0 - 0.002, s0 + d);
                cb_put(b, s0 - 0.001, 0);
                cb_put(b, s0, -1.5);
                cb_put(b, s0 + d, 0);
            } else if (!later) {
                double d = fmin(0.3, (e0 - s0) / 2);
                cb_erase(b, e0 - d, e0 + 0.002);
                cb_put(b, e0 - d, 0);
                cb_put(b, e0 - 0.001, -4);
                cb_put(b, e0 + 0.001, 0);
            }
        }
    } else if (kind == CBP_SCOOP) {             /* anything else: the clip's start, or its end */
        cb_put(b, a, -1.5);
        cb_put(b, a + fmin(0.12, (z - a) / 3), 0);
    } else if (kind == CBP_FALL) {
        cb_put(b, z - fmin(0.35, (z - a) / 2), 0);
        cb_put(b, z, -4);
    } else if (kind == CBP_VIB) {               /* 5.5 a second, a third of a semitone either way, easing in */
        double t = a, half = 1 / 11.0;
        int k = 0;
        cb_put(b, a, 0);
        for (t = a + 0.3; t < z && b->n < BEND_POINTS; t += half, k++) {
            double depth = 0.35 * fmin(1, (t - a - 0.3) / 0.5 + 0.2);
            cb_put(b, t, k % 2 ? -depth : depth);
        }
    } else if (kind == CBP_RISE || kind == CBP_DIVE) {
        cb_put(b, a, 0);
        cb_put(b, z, kind == CBP_RISE ? 12 : -12);
    }
    static const char *const SAY[CBP_COUNT] = {"", "Scooped up into", "Falling off at the end of", "Vibrato on",
                                               "Rising an octave over", "Diving an octave over"};
    status("%s %s. Draw over the curve to change it; Space to hear it", SAY[kind],
           c->pattern > 0 && (kind == CBP_SCOOP || kind == CBP_FALL) ? "each note" : "the clip");
}

int clip_bend_editor(int x, int y, int w)
{
    if (selected < 0 || selected >= P.nclips) return y;
    const Clip *c = &P.clips[selected];
    const Bend *b = bend_find_c(&P, c->bend);
    double a = (double)c->offset / SR, z = (double)(c->offset + c->length) / SR;
    if (z - a < 1e-3) z = a + 1e-3;
    int range = 12;
    for (int i = 0; b && i < b->n; i++)
        while (fabsf(b->semis[i]) > range && range < 24) range += 12;
    SDL_Rect r = {x, y, w, 120};
    cb_box = r;
    fill(r, PANEL2);
#define CX(t) (r.x + (int)(((t) - a) / (z - a) * r.w))
#define CY(st) (r.y + r.h / 2 - (int)((st) / (double)range * (r.h / 2 - 4)))
    for (int st = -range; st <= range; st++)     /* semitones; octaves stronger */
        if (st % 12 == 0 || range == 12)
            line(r.x, CY(st), r.x + r.w - 1, CY(st), st == 0 ? tone(PANEL2, 1.7f) : st % 12 == 0 ? tone(PANEL2, 1.4f) : tone(PANEL2, 1.12f));
    if (c->pattern > 0)                          /* a note clip: where its notes start */
        for (int i = 0; i < P.nnotes; i++)
            if (P.notes[i].pattern == c->pattern) {
                double s0 = (double)P.notes[i].start / SR, e0 = (double)(P.notes[i].start + P.notes[i].length) / SR;
                if (e0 < a || s0 > z) continue;
                int x0 = CX(s0 > a ? s0 : a), x1 = CX(e0 < z ? e0 : z);
                fill((SDL_Rect){x0, r.y + r.h - 5, x1 - x0 > 1 ? x1 - x0 - 1 : 1, 3}, tone(PANEL3, 1.4f));
            }
    int px0 = -1, py0 = 0;
    for (int px = 0; px < r.w; px++) {           /* the curve */
        double t = a + (z - a) * px / r.w;
        int yy = CY(b ? bend_at(b, t) : 0);
        yy = yy < r.y ? r.y : yy > r.y + r.h - 1 ? r.y + r.h - 1 : yy;
        if (px0 >= 0) {
            line(r.x + px0, py0, r.x + px, yy, ACCENT);
            line(r.x + px0, py0 + 1, r.x + px, yy + 1, ACCENT);
        }
        px0 = px;
        py0 = yy;
    }
    for (int i = 0; b && i < b->n; i++) {
        int bx = CX(b->at[i]), by = CY(b->semis[i]);
        if (bx >= r.x && bx < r.x + r.w) fill((SDL_Rect){bx - 2, by - 2, 5, 5}, HEAD);
    }
    char lab[16];
    snprintf(lab, sizeof lab, "+%d", range);
    text(r.x + 4, r.y + 2, lab, DIM);
    snprintf(lab, sizeof lab, "-%d", range);
    text(r.x + 4, r.y + r.h - font_h - 4, lab, DIM);
    outline(r, LINE);
    tip(r, "Draw the pitch over the clip, +/-%d semitones (Shift: whole semitones). Right-click rubs out a bend, "
           "Ctrl+drag rubs out along the way. Works on audio, note and drum clips", range);
    if (hit(r) && drag.kind == D_NONE && rpress) {
        rpress = 0;
        Bend *e = c->bend ? cb_edit() : NULL;
        double t = a + (z - a) * (mx - r.x) / r.w;
        if (e) cb_erase(e, t - (z - a) / 40, t + (z - a) / 40);
    }
    if (hit(r) && drag.kind == D_NONE && lpress) {
        if (cb_edit()) {
            drag = (typeof(drag)){.kind = (mods & KMOD_CTRL) ? D_CBENDX : D_CBEND, .sx = mx, .sy = my};
            cb_last = -1;
        }
        lpress = 0;
    }
    if ((drag.kind == D_CBEND || drag.kind == D_CBENDX) && ldown) {
        Bend *e = bend_find(&P, P.clips[selected].bend);
        double t = a + (z - a) * (mx - r.x) / r.w;
        t = t < a ? a : t > z ? z : t;
        double from = cb_last < 0 ? t : cb_last;
        if (e && drag.kind == D_CBENDX) {
            cb_erase(e, from - 0.01, t + 0.01);
        } else if (e) {
            double st = (double)(r.y + r.h / 2 - my) / (r.h / 2 - 4) * range;
            st = (mods & KMOD_SHIFT) ? round(st) : round(st * 20) / 20;
            if (t > from) cb_erase(e, from + 1e-6, t);   /* what the stroke passed over */
            else if (t < from) cb_erase(e, t, from - 1e-6);
            else cb_erase(e, t, t);
            if (e->n >= BEND_POINTS) status("A curve can have %d points", BEND_POINTS);
            cb_put(e, t, st);
        }
        cb_last = t;
    }
#undef CX
#undef CY
    y += r.h + 8;
    static const char *const PRE[CBP_COUNT] = {"Flat", "Scoops", "Fall-offs", "Vibrato", "Rise", "Dive"};
    static const char *const PTIP[CBP_COUNT] = {
        "No bend: the clip plays straight", "Up into each note from below (a note clip), or into the clip",
        "Dropping away at the end of each phrase (a note clip), or of the clip", "A gentle vibrato that eases in",
        "Up an octave over the clip", "Down an octave over the clip"};
    int bw = (w - 2 * 4) / 3;
    for (int i = 0; i < CBP_COUNT; i++) {
        SDL_Rect bb = {x + (i % 3) * (bw + 4), y + (i / 3) * 30, i % 3 == 2 ? w - 2 * (bw + 4) : bw, 26};
        tip(bb, "%s", PTIP[i]);
        if (button(bb, PRE[i], 0)) cb_preset(i);
    }
    y += 64;
    b = bend_find_c(&P, P.clips[selected].bend);
    if (!c->pattern) {                           /* audio: how the bend is made */
        int keep = b ? b->keep_time : 1;
        text(x, y + 6, "Audio", DIM);
        int hw2 = (w - 70 - 4) / 2;
        SDL_Rect k1 = {x + 70, y, hw2, 26}, k2 = {x + 70 + hw2 + 4, y, hw2, 26};
        tip(k1, "The pitch moves and the clip stays in time (made by pitch-shifting, rendered when you let go)");
        tip(k2, "Like a tape or turntable: bending up also runs faster, bending down slower");
        int pick = -1;
        if (button(k1, "Keep timing", keep)) pick = 1;
        if (button(k2, "Tape", !keep)) pick = 0;
        if (pick >= 0 && pick != keep) {
            Bend *e = cb_edit();
            if (e) {
                LOCK();
                e->keep_time = pick;
                UNLOCK();
                status(pick ? "Keeps its timing: the pitch alone moves" : "Tape: the speed moves with the pitch");
            }
        }
        y += 34;
    }
    return y;
}

const char *const PK_DIR[PK_KINDS] = {"Track", "Delay", "Reverb"};

const char *const TRACK_PRESET_NAMES[BUILTIN_TRACK] = {"Clean", "Warm tape", "Telephone", "Wide pad", "Pumping",
                                                               "Lo-fi crush", "Filter sweep", "Vocal", "Punchy drums", "Dub send",
                                                               "Dreamy", "Bitcrushed"};

const char *const DELAY_PRESET_NAMES[BUILTIN_DELAY] = {"Slapback", "Eighth echo", "Dotted 8th", "Triplet bounce",
                                                               "Dub quarter", "Long half"};

const char *const REVERB_PRESET_NAMES[BUILTIN_REVERB] = {"Room", "Plate", "Hall", "Cathedral", "Dark chamber"};

void preset_dir(int kind, char *buf, size_t n)
{
    const char *e = SDL_getenv("TINYDAW_PRESETS_DIR");
    char base[900];
    if (e && *e) snprintf(base, sizeof base, "%s", e);
    else {
        char m[800];
        user_dir("music", m, sizeof m);
        snprintf(base, sizeof base, "%s/tinydaw/Presets", m);
    }
    snprintf(buf, n, "%s/%s", base, PK_DIR[kind]);
}

void macro_set(Macro *m, const char *name, float value)
{
    memset(m, 0, sizeof *m);
    snprintf(m->name, sizeof m->name, "%s", name);
    m->value = value;
}

void macro_link(Macro *m, int param, float lo, float hi)
{
    if (m->nlinks < MACRO_LINKS) m->link[m->nlinks++] = (MacroLink){param, lo, hi};
}

/* a track's chain, everything but its name, level, pan, mute and solo, set to a built-in preset */
void builtin_track_preset(int i, Track *t)
{
    t->filter = FILT_OFF;
    t->cutoff = 1200;
    t->reso = 0.2f;
    t->eq[0] = t->eq[1] = t->eq[2] = 0;
    t->drive = t->delay_send = t->reverb_send = t->duck = 0;
    t->width = 1;
    t->comp_on = 0;
    t->comp_thresh = -18, t->comp_ratio = 3, t->comp_attack = 10, t->comp_release = 150, t->comp_makeup = 0;
    memset(t->macro, 0, sizeof t->macro);
    memset(&t->rack, 0, sizeof t->rack);
    Macro *m = t->macro;
    switch (i) {
    case 1:                                      /* warm tape */
        t->filter = FILT_LOW, t->cutoff = 9000, t->eq[0] = 2, t->eq[2] = -2, t->drive = 0.2f;
        t->comp_on = 1, t->comp_thresh = -18, t->comp_ratio = 2.5f;
        macro_set(&m[0], "Warmth", 0.3f);
        macro_link(&m[0], TP_DRIVE, 0.05f, 0.6f);
        macro_link(&m[0], TP_CUTOFF, 14000, 3000);
        break;
    case 2:                                      /* telephone */
        t->filter = FILT_BAND, t->cutoff = 1500, t->reso = 0.4f, t->eq[0] = -12, t->eq[2] = -8, t->drive = 0.35f;
        macro_set(&m[0], "Ring", 0.5f);
        macro_link(&m[0], TP_CUTOFF, 700, 3200);
        break;
    case 3:                                      /* wide pad */
        t->width = 1.8f, t->reverb_send = 0.5f, t->delay_send = 0.2f, t->eq[0] = -3;
        macro_set(&m[0], "Space", 0.5f);
        macro_link(&m[0], TP_REVERB, 0.2f, 0.85f);
        macro_link(&m[0], TP_WIDTH, 1.0f, 2.0f);
        break;
    case 4:                                      /* pumping */
        t->duck = 0.8f, t->comp_on = 1, t->comp_thresh = -20, t->comp_ratio = 4;
        macro_set(&m[0], "Pump", 0.8f);
        macro_link(&m[0], TP_DUCK, 0, 1);
        break;
    case 5:                                      /* lo-fi crush */
        t->filter = FILT_LOW, t->cutoff = 3500, t->drive = 0.6f, t->eq[2] = -6;
        macro_set(&m[0], "Dust", 0.5f);
        macro_link(&m[0], TP_DRIVE, 0.2f, 0.9f);
        macro_link(&m[0], TP_CUTOFF, 8000, 1800);
        break;
    case 6:                                      /* filter sweep */
        t->filter = FILT_LOW, t->cutoff = 2000, t->reso = 0.5f;
        macro_set(&m[0], "Sweep", 0.6f);
        macro_link(&m[0], TP_CUTOFF, 200, 16000);
        macro_set(&m[1], "Res", 0.4f);
        macro_link(&m[1], TP_RESO, 0.1f, 0.85f);
        break;
    case 7:                                      /* vocal */
        t->filter = FILT_HIGH, t->cutoff = 120, t->eq[1] = 1, t->eq[2] = 2, t->comp_on = 1, t->comp_thresh = -20;
        t->delay_send = 0.15f, t->reverb_send = 0.25f;
        macro_set(&m[0], "Air", 0.3f);
        macro_link(&m[0], TP_EQ_HIGH, 0, 6);
        macro_link(&m[0], TP_REVERB, 0.1f, 0.5f);
        break;
    case 8:                                      /* punchy drums */
        t->eq[0] = 3, t->eq[2] = 2, t->drive = 0.15f, t->comp_on = 1, t->comp_thresh = -16, t->comp_ratio = 4;
        t->comp_attack = 15;
        macro_set(&m[0], "Smash", 0.3f);
        macro_link(&m[0], TP_COMP, -10, -32);
        macro_link(&m[0], TP_DRIVE, 0, 0.5f);
        break;
    case 10:                                     /* dreamy: chorus into reverb, in the rack */
        t->eq[2] = 1;
        rack_fx_init(&t->rack.fx[0], FXR_CHORUS);
        rack_fx_init(&t->rack.fx[1], FXR_REVERB);
        t->rack.fx[1].p[0] = 0.8f, t->rack.fx[1].p[2] = 0.35f;
        t->rack.n = 2;
        macro_set(&m[0], "Dream", 0.5f);
        macro_link(&m[0], TP_RACK + 0 * FXR_PARAMS + 1, 0.1f, 0.9f);   /* the chorus's depth */
        macro_link(&m[0], TP_RACK + 1 * FXR_PARAMS + 2, 0.1f, 0.6f);   /* the reverb's mix */
        break;
    case 11:                                     /* bitcrushed: fewer bits, then a filter to tame it */
        rack_fx_init(&t->rack.fx[0], FXR_CRUSH);
        rack_fx_init(&t->rack.fx[1], FXR_FILTER);
        t->rack.fx[1].p[1] = 6000;
        t->rack.n = 2;
        macro_set(&m[0], "Crunch", 0.5f);
        macro_link(&m[0], TP_RACK + 0 * FXR_PARAMS + 0, 14, 4);         /* the crusher's bits */
        break;
    case 9:                                      /* dub send */
        t->delay_send = 0.6f, t->reverb_send = 0.3f, t->filter = FILT_HIGH, t->cutoff = 200;
        macro_set(&m[0], "Echo", 0.6f);
        macro_link(&m[0], TP_DELAY, 0, 1);
        break;
    }
}

void builtin_fx_preset(int kind, int i, MasterFx *fx)
{
    if (kind == PK_DELAY) {
        static const struct { int div; float fb, tone, level; int ping; } D[BUILTIN_DELAY] = {
            {0, 0.1f, 0.6f, 0.6f, 0}, {2, 0.35f, 0.5f, 0.55f, 0}, {4, 0.4f, 0.5f, 0.55f, 1},
            {1, 0.35f, 0.55f, 0.5f, 1}, {5, 0.65f, 0.3f, 0.6f, 1}, {7, 0.5f, 0.4f, 0.5f, 0}};
        fx->delay_div = D[i].div, fx->delay_feedback = D[i].fb, fx->delay_tone = D[i].tone;
        fx->delay_level = D[i].level, fx->pingpong = D[i].ping;
    } else {
        static const float R[BUILTIN_REVERB][3] = {{0.35f, 0.5f, 0.5f}, {0.6f, 0.3f, 0.6f}, {0.85f, 0.4f, 0.7f},
                                                   {0.97f, 0.2f, 0.75f}, {0.7f, 0.8f, 0.65f}};
        fx->reverb_size = R[i][0], fx->reverb_damp = R[i][1], fx->reverb_level = R[i][2];
    }
}

/* your own presets: key value lines */
int preset_write(int kind, const char *name, const Track *t, const MasterFx *fx)
{
    char d[1024], path[1300];
    preset_dir(kind, d, sizeof d);
    ensure_dir(d);
    snprintf(path, sizeof path, "%s/%s.preset", d, name);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "tinydaw-preset 1 %s\n", PK_DIR[kind]);
    if (kind == PK_TRACK) {
        fprintf(f, "chain %d %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n", t->filter, t->cutoff, t->reso, t->eq[0],
                t->eq[1], t->eq[2], t->drive, t->delay_send, t->reverb_send, t->duck, t->width);
        fprintf(f, "comp %d %.9g %.9g %.9g %.9g %.9g\n", t->comp_on, t->comp_thresh, t->comp_ratio, t->comp_attack,
                t->comp_release, t->comp_makeup);
        for (int k = 0; k < t->rack.n; k++) {     /* the rack, in order */
            const RackFx *x = &t->rack.fx[k];
            fprintf(f, "rack %d %d", x->kind, x->on);
            for (int j = 0; j < FXR_PARAMS; j++) fprintf(f, " %.9g", x->p[j]);
            fprintf(f, "\n");
        }
        for (int m = 0; m < TRACK_MACROS; m++) {
            const Macro *mc = &t->macro[m];
            if (!mc->name[0] && !mc->nlinks) continue;
            fprintf(f, "macro %d %.6g %s\n", m, mc->value, mc->name);
            for (int k = 0; k < mc->nlinks; k++)
                fprintf(f, "mlink %d %d %.9g %.9g\n", m, mc->link[k].param, mc->link[k].lo, mc->link[k].hi);
        }
    } else if (kind == PK_DELAY) {
        fprintf(f, "delay %d %.9g %.9g %.9g %d\n", fx->delay_div, fx->delay_feedback, fx->delay_tone, fx->delay_level,
                fx->pingpong);
    } else {
        fprintf(f, "reverb %.9g %.9g %.9g\n", fx->reverb_size, fx->reverb_damp, fx->reverb_level);
    }
    return fclose(f) == 0 ? 0 : -1;
}

int preset_read(int kind, const char *name, Track *t, MasterFx *fx)
{
    char d[1024], path[1300], line_[300];
    preset_dir(kind, d, sizeof d);
    snprintf(path, sizeof path, "%s/%s.preset", d, name);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (kind == PK_TRACK) {
        memset(t->macro, 0, sizeof t->macro);
        memset(&t->rack, 0, sizeof t->rack);
    }
    while (fgets(line_, sizeof line_, f)) {
        int m, nm = 0;
        float v;
        MacroLink l;
        if (kind == PK_TRACK)
            if (sscanf(line_, "chain %d %f %f %f %f %f %f %f %f %f %f", &t->filter, &t->cutoff, &t->reso, &t->eq[0], &t->eq[1],
                       &t->eq[2], &t->drive, &t->delay_send, &t->reverb_send, &t->duck, &t->width) == 11) {
                if (t->filter < 0 || t->filter >= FILT_TYPES) t->filter = FILT_OFF;
                continue;
            }
        if (kind == PK_TRACK && sscanf(line_, "comp %d %f %f %f %f %f", &t->comp_on, &t->comp_thresh, &t->comp_ratio,
                                       &t->comp_attack, &t->comp_release, &t->comp_makeup) == 6)
            continue;
        if (kind == PK_TRACK && sscanf(line_, "macro %d %f %n", &m, &v, &nm) >= 2 && m >= 0 && m < TRACK_MACROS) {
            Macro *mc = &t->macro[m];
            mc->value = v < 0 ? 0 : v > 1 ? 1 : v;
            if (nm > 0) snprintf(mc->name, sizeof mc->name, "%.15s", line_ + nm);
            mc->name[strcspn(mc->name, "\r\n")] = 0;
            continue;
        }
        RackFx rx;
        memset(&rx, 0, sizeof rx);
        if (kind == PK_TRACK && sscanf(line_, "rack %d %d %f %f %f %f %f %f", &rx.kind, &rx.on, &rx.p[0], &rx.p[1], &rx.p[2],
                                       &rx.p[3], &rx.p[4], &rx.p[5]) >= 2 && rx.kind >= 0 && rx.kind < FXR_KINDS) {
            if (t->rack.n < RACK_SLOTS) t->rack.fx[t->rack.n++] = rx;
            continue;
        }
        if (kind == PK_TRACK && sscanf(line_, "mlink %d %d %f %f", &m, &l.param, &l.lo, &l.hi) == 4 && m >= 0 &&
            m < TRACK_MACROS && track_param_valid(l.param) && t->macro[m].nlinks < MACRO_LINKS) {
            t->macro[m].link[t->macro[m].nlinks++] = l;
            continue;
        }
        if (kind == PK_DELAY && sscanf(line_, "delay %d %f %f %f %d", &fx->delay_div, &fx->delay_feedback, &fx->delay_tone,
                                       &fx->delay_level, &fx->pingpong) == 5) {
            if (fx->delay_div < 0 || fx->delay_div >= DELAY_DIVS) fx->delay_div = 2;
            continue;
        }
        if (kind == PK_REVERB)
            sscanf(line_, "reverb %f %f %f", &fx->reverb_size, &fx->reverb_damp, &fx->reverb_level);
    }
    fclose(f);
    return 0;
}

/* the list a preset dropdown shows: "Choose a preset", the built-ins, then yours ("Saved: name") */
T_plist plist[PK_KINDS];

void preset_list(int kind)
{
    if (plist[kind].n && SDL_GetTicks() - plist[kind].at < 2000) return;   /* read again every couple of seconds */
    plist[kind].at = SDL_GetTicks();
    const char *const *bn = kind == PK_TRACK ? TRACK_PRESET_NAMES : kind == PK_DELAY ? DELAY_PRESET_NAMES : REVERB_PRESET_NAMES;
    int nb = kind == PK_TRACK ? BUILTIN_TRACK : kind == PK_DELAY ? BUILTIN_DELAY : BUILTIN_REVERB, n = 0;
    plist[kind].ptr[n++] = "Choose a preset...";
    for (int i = 0; i < nb; i++) plist[kind].ptr[n++] = bn[i];
    plist[kind].nbuilt = nb;
    char d[1024];
    preset_dir(kind, d, sizeof d);
    DIR *dir = headless && !SDL_getenv("TINYDAW_PRESETS_DIR") ? NULL : opendir(d);
    int k = 0;
    struct dirent *e;
    while (dir && (e = readdir(dir)) && k < 64) {
        size_t len = strlen(e->d_name);
        if (len < 8 || strcmp(e->d_name + len - 7, ".preset")) continue;
        snprintf(plist[kind].names[k], sizeof plist[kind].names[k], "Saved: %.*s", (int)(len - 7 < 38 ? len - 7 : 38), e->d_name);
        plist[kind].ptr[n++] = plist[kind].names[k++];
    }
    if (dir) closedir(dir);
    plist[kind].n = n;
}

/* applies item `i` of the list to the selected track or the shared effects */
void preset_apply(int kind, int i, Track *t)
{
    if (i <= 0 || i >= plist[kind].n) return;
    const char *label = plist[kind].ptr[i];
    Track tmp = t ? *t : P.tracks[0];
    MasterFx fx = P.fx;
    int ok = 0;
    if (i <= plist[kind].nbuilt) {
        if (kind == PK_TRACK) builtin_track_preset(i - 1, &tmp);
        else builtin_fx_preset(kind, i - 1, &fx);
        ok = 1;
    } else {
        ok = preset_read(kind, label + 7, &tmp, &fx) == 0;
    }
    if (!ok) {
        status("Couldn't read the preset \"%s\"", label);
        return;
    }
    LOCK();
    if (kind == PK_TRACK && t) {                 /* the chain; the track keeps its name, level, pan, mute and solo */
        Track keep = *t;
        *t = tmp;
        snprintf(t->name, sizeof t->name, "%s", keep.name);
        t->volume = keep.volume, t->pan = keep.pan, t->mute = keep.mute, t->solo = keep.solo;
    } else {
        P.fx = fx;
    }
    UNLOCK();
    status("%s: %s. Ctrl+Z puts it back", kind == PK_TRACK ? t->name : kind == PK_DELAY ? "Delay" : "Reverb", label);
}

/* a preset row: the list, and Save (which asks for a name in the same place) */
int preset_row(int x, int y, int w, int kind, Track *t)
{
    static int pick[PK_KINDS], naming = -1;
    static char name[40];
    preset_list(kind);
    if (naming == kind) {
        SDL_Rect fr = {x, y, w - 160, 26};
        textfield(fr, name, sizeof name, 0, "name the preset");
        if (!tf.buf && name[0] == 0) naming = -1;
        if (button((SDL_Rect){x + w - 154, y, 74, 26}, "Save", 1) || (!tf.buf && name[0])) {
            if (name[0]) {
                if (preset_write(kind, name, t, &P.fx) == 0) status("Saved the preset \"%s\": every project has it now", name);
                else status("Couldn't save the preset \"%s\"", name);
                plist[kind].n = 0;
            }
            tf_end(1);
            naming = -1;
        }
        if (button((SDL_Rect){x + w - 74, y, 74, 26}, "Cancel", 0)) {
            tf_end(0);
            naming = -1;
        }
        return y + 34;
    }
    pick[kind] = 0;
    if (dropdown((SDL_Rect){x, y, w - 86, 26}, kind == PK_TRACK ? "Preset: " : "", plist[kind].ptr, plist[kind].n, &pick[kind], 0))
        preset_apply(kind, pick[kind], t);
    tip((SDL_Rect){x, y, w - 86, 26}, kind == PK_TRACK ? "Set the whole effect chain from a preset (the track keeps its level and pan)"
                                                      : "Set it from a preset");
    if (button((SDL_Rect){x + w - 80, y, 80, 26}, "Save as", 0)) {
        naming = kind;
        name[0] = 0;
        tf_end(1);
        tf.buf = name;
        tf.cap = sizeof name;
        tf.path = 0;
        tf.caret = 0;
        tf_drawn = 1;
        tf.orig[0] = 0;
        SDL_StartTextInput();
    }
    tip((SDL_Rect){x + w - 80, y, 80, 26}, "Keep these settings as a preset of your own, in every project");
    return y + 34;
}

/* ---- macros: four knobs a track can have, each turning up to four of its settings at once */
int macro_open = -1;                     /* the macro whose links are showing */

/* what a macro can link to on track t: its strip's settings, then every knob in its rack */
int macro_choices(const Track *t, int *ids, const char **names, int max)
{
    static char buf[TP_COUNT + RACK_SLOTS * FXR_PARAMS][48];
    int n = 0;
    for (int p = 0; p < TP_COUNT && n < max; p++, n++) {
        ids[n] = p;
        names[n] = TRACK_PARAMS[p].name;
    }
    for (int s = 0; s < t->rack.n; s++)
        for (int k = 0; k < RACK_KINDS[t->rack.fx[s].kind].np && n < max; k++, n++) {
            float lo, hi;
            int lg;
            ids[n] = TP_RACK + s * FXR_PARAMS + k;
            track_param_info(t, ids[n], buf[n], sizeof buf[n], &lo, &hi, &lg, NULL);
            names[n] = buf[n];
        }
    return n;
}

int macro_linked(const Track *t, int tp)
{
    for (int m = 0; m < TRACK_MACROS; m++)
        for (int k = 0; k < t->macro[m].nlinks; k++)
            if (t->macro[m].link[k].param == tp) return 1;
    return 0;
}

int macros_section(int x, int y, int w, Track *t)
{
    char buf[96];
    static int ids[TP_COUNT + RACK_SLOTS * FXR_PARAMS];
    static const char *names[TP_COUNT + RACK_SLOTS * FXR_PARAMS];
    int nch = macro_choices(t, ids, names, TP_COUNT + RACK_SLOTS * FXR_PARAMS);
    for (int m = 0; m < TRACK_MACROS; m++) {
        Macro *mc = &t->macro[m];
        int nw = 110, bw = 86;
        SDL_Rect nr = {x, y, nw, 26};
        char ph[16];
        snprintf(ph, sizeof ph, "Macro %d", m + 1);
        textfield(nr, mc->name, sizeof mc->name, 0, ph);
        double v = mc->value;
        SDL_Rect sr = {x + nw + 8, y, w - nw - 8 - bw - 8, 26};
        if (mc->nlinks) {
            slider(sr, &v);
            if ((float)v != mc->value) {
                LOCK();
                mc->value = (float)v;
                UNLOCK();
            }
        } else {
            text_clip(sr, sr.x + 6, y + 6, "not linked to anything yet", DIM);
        }
        int has_auto = auto_lane(&P, sel_track, AUTO_MACRO1 + m, NULL);
        tip(sr, "%s: turns %d setting%s at once%s", mc->name[0] ? mc->name : ph, mc->nlinks, mc->nlinks == 1 ? "" : "s",
            has_auto ? " (automated: the track's A lane sets it)" : "");
        snprintf(buf, sizeof buf, macro_open == m ? "Done" : "Links (%d)", mc->nlinks);
        if (button((SDL_Rect){x + w - bw, y, bw, 26}, buf, macro_open == m)) macro_open = macro_open == m ? -1 : m;
        y += 32;
        if (macro_open != m) continue;
        for (int k = 0; k < mc->nlinks; k++) {    /* each link: what it turns, from where to where */
            MacroLink *l = &mc->link[k];
            int lw = (w - 30 - 8) / 3;
            static int psel[TRACK_MACROS][MACRO_LINKS];
            psel[m][k] = 0;
            for (int c = 0; c < nch; c++)
                if (ids[c] == l->param) psel[m][k] = c;
            if (dropdown((SDL_Rect){x + 16, y, lw, 26}, "", names, nch, &psel[m][k], 0)) {
                float lo, hi;
                int lg;
                track_param_info(t, ids[psel[m][k]], NULL, 0, &lo, &hi, &lg, NULL);
                LOCK();
                l->param = ids[psel[m][k]];
                l->lo = lo + (hi - lo) * (lg ? 0.01f : 0.25f);
                l->hi = lg ? hi * 0.6f : lo + (hi - lo) * 0.75f;
                UNLOCK();
            }
            float lo, hi;
            int lg;
            const char *fmt;
            track_param_info(t, l->param, NULL, 0, &lo, &hi, &lg, &fmt);
            double step = (hi - lo) / (lg ? 2000 : 400);
            fnumber((SDL_Rect){x + 16 + lw + 4, y, lw, 26}, "From", &l->lo, lo, hi, step, fmt);
            fnumber((SDL_Rect){x + 16 + 2 * (lw + 4), y, lw, 26}, "To", &l->hi, lo, hi, step, fmt);
            if (button((SDL_Rect){x + w - 22, y, 22, 26}, "x", 0)) {
                LOCK();
                memmove(&mc->link[k], &mc->link[k + 1], (size_t)(mc->nlinks - k - 1) * sizeof(MacroLink));
                mc->nlinks--;
                UNLOCK();
                break;
            }
            y += 30;
        }
        if (mc->nlinks < MACRO_LINKS && button((SDL_Rect){x + 16, y, 120, 26}, "+ Link", 0)) {
            int p = TP_CUTOFF;                     /* the first setting no macro turns yet */
            for (int q = TP_COUNT - 1; q >= 0; q--)
                if (!macro_linked(t, q) && q != TP_VOLUME && q != TP_PAN) p = q;
            const TrackParamInfo *pi = &TRACK_PARAMS[p];
            float cur = *track_param(t, p);
            LOCK();
            mc->link[mc->nlinks++] = (MacroLink){p, cur, pi->log ? (cur * 4 < pi->hi ? cur * 4 : pi->hi)
                                                                  : (cur + (pi->hi - pi->lo) * 0.5f < pi->hi ? cur + (pi->hi - pi->lo) * 0.5f : pi->hi)};
            if (mc->nlinks == 1) mc->value = 0;
            UNLOCK();
        }
        text(x + 144, y + 6, "Each link turns a setting (or a rack knob) from one value to another", DIM);
        y += 34;
    }
    char lst[200] = "";
    for (int c = 0; c < nch; c++)
        if (macro_linked(t, ids[c])) {
            if (lst[0]) strncat(lst, ", ", sizeof lst - strlen(lst) - 1);
            strncat(lst, names[c], sizeof lst - strlen(lst) - 1);
        }
    if (lst[0]) {
        snprintf(buf, sizeof buf, "Set by the macros: %.70s", lst);
        text_clip((SDL_Rect){x, y, w, font_h + 4}, x, y, buf, ACCENT);
        y += font_h + 10;
    }
    return y;
}

/* ---- the effect rack: up to eight effects in order, each with on/off, its knobs, up/down and remove */

int rack_editor(int x, int y, int w, Rack *rk, int key)
{
    char buf[96];
    for (int s = 0; s < rk->n; s++) {
        RackFx *f = &rk->fx[s];
        const RackKind *kd = &RACK_KINDS[f->kind];
        SDL_Rect hd = {x, y, w, 26};
        fill(hd, f->on ? tone(PANEL2, 1.1f) : PANEL2);
        if (button((SDL_Rect){x, y, 44, 26}, f->on ? "On" : "Off", f->on)) {
            LOCK();
            f->on = !f->on;
            UNLOCK();
        }
        tip((SDL_Rect){x, y, 44, 26}, "Turn it off to hear the sound without it");
        snprintf(buf, sizeof buf, "%d  %s", s + 1, kd->name);
        text(x + 52, y + 6, buf, f->on ? HEAD : DIM);
        tip((SDL_Rect){x + 50, y, w - 50 - 3 * 28, 26}, "%s", kd->about);
        int bx = x + w - 3 * 28;
        if (button2((SDL_Rect){bx, y, 26, 26}, "^", 0, s > 0)) {          /* earlier */
            LOCK();
            RackFx t = rk->fx[s - 1];
            rk->fx[s - 1] = rk->fx[s];
            rk->fx[s] = t;
            UNLOCK();
        }
        tip((SDL_Rect){bx, y, 26, 26}, "Earlier in the rack (the order matters: drive then filter isn't filter then drive)");
        if (button2((SDL_Rect){bx + 28, y, 26, 26}, "v", 0, s + 1 < rk->n)) {   /* later */
            LOCK();
            RackFx t = rk->fx[s + 1];
            rk->fx[s + 1] = rk->fx[s];
            rk->fx[s] = t;
            UNLOCK();
        }
        tip((SDL_Rect){bx + 28, y, 26, 26}, "Later in the rack");
        if (button((SDL_Rect){bx + 56, y, 26, 26}, "x", 0)) {
            LOCK();
            memmove(&rk->fx[s], &rk->fx[s + 1], (size_t)(rk->n - s - 1) * sizeof(RackFx));
            rk->n--;
            UNLOCK();
            status("Took the %s out of the rack. Ctrl+Z puts it back", kd->name);
            break;
        }
        tip((SDL_Rect){bx + 56, y, 26, 26}, "Take it out of the rack");
        y += 30;
        int per = 3, kw = (w - 16 - (per - 1) * 6) / per;
        for (int k = 0; k < kd->np; k++) {
            SDL_Rect kr = {x + 16 + (k % per) * (kw + 6), y + (k / per) * 30, kw, 26};
            if (kd->choices[k]) {                  /* a choice: click to go round them */
                int nchoice = 0;
                while (kd->choices[k][nchoice]) nchoice++;
                int cur = (int)lroundf(f->p[k]);
                cur = cur < 0 ? 0 : cur >= nchoice ? nchoice - 1 : cur;
                snprintf(buf, sizeof buf, "%s: %s", kd->pname[k], kd->choices[k][cur]);
                if (button(kr, buf, 0)) {
                    LOCK();
                    f->p[k] = (float)((cur + 1) % nchoice);
                    UNLOCK();
                }
            } else if (kd->log[k]) {               /* dragged on a log scale */
                static double lpos[MAX_TRACKS + MAX_BUSES + 1][RACK_SLOTS][FXR_PARAMS];
                double *lp = &lpos[key][s][k];
                if (!(drag.kind == D_NUM && drag.num == lp)) *lp = log(f->p[k] / kd->lo[k]) / log(kd->hi[k] / kd->lo[k]);
                double val = kd->lo[k] * pow(kd->hi[k] / kd->lo[k], *lp);
                char shown[32];
                snprintf(shown, sizeof shown, kd->fmt[k], val);
                number_text(kr, kd->pname[k], lp, 0, 1, 0.002, shown);
                float nv = (float)(kd->lo[k] * pow(kd->hi[k] / kd->lo[k], *lp));
                if (fabsf(nv - f->p[k]) > 1e-4f * fabsf(nv) + 1e-6f) {
                    LOCK();
                    f->p[k] = nv;
                    UNLOCK();
                }
            } else {
                fnumber(kr, kd->pname[k], &f->p[k], kd->lo[k], kd->hi[k], (kd->hi[k] - kd->lo[k]) / 400, kd->fmt[k]);
            }
        }
        y += ((kd->np + per - 1) / per) * 30 + 6;
    }
    if (rk->n < RACK_SLOTS) {                      /* + an effect */
        static const char *knames[FXR_KINDS + 1];
        knames[0] = "+ Add an effect...";
        for (int k = 0; k < FXR_KINDS; k++) knames[k + 1] = RACK_KINDS[k].name;
        static int pick[MAX_TRACKS + MAX_BUSES + 1];
        pick[key] = 0;
        if (dropdown((SDL_Rect){x, y, w < 260 ? w : 260, 26}, "", knames, FXR_KINDS + 1, &pick[key], 0) && pick[key] > 0) {
            LOCK();
            rack_fx_init(&rk->fx[rk->n++], pick[key] - 1);
            UNLOCK();
            status("Added a %s to the rack, after what's there. Its arrows move it", RACK_KINDS[pick[key] - 1].name);
        }
        tip((SDL_Rect){x, y, w < 260 ? w : 260, 26}, "Add an effect at the end of the rack (up to %d)", RACK_SLOTS);
        y += 34;
    } else {
        text(x, y + 4, "The rack is full (8 effects)", DIM);
        y += font_h + 12;
    }
    return y;
}

/* where a track plays (the master or a bus), and its sends to the buses beyond the delay and reverb */
int output_section(int x, int y, int w, Track *t)
{
    static char names[MAX_BUSES + 1][48];
    static const char *ptr[MAX_BUSES + 1];
    snprintf(names[0], sizeof names[0], "Master");
    ptr[0] = names[0];
    for (int b = 0; b < P.nbuses; b++) {
        snprintf(names[b + 1], sizeof names[0], "Bus: %s", P.buses[b].name);
        ptr[b + 1] = names[b + 1];
    }
    static int pick;
    pick = t->out <= P.nbuses ? t->out : 0;
    int hw = (w - 8) / 2;
    if (dropdown((SDL_Rect){x, y, hw, 26}, "Output: ", ptr, P.nbuses + 1, &pick, 0)) {
        LOCK();
        t->out = pick;
        UNLOCK();
        status(pick ? "This track now plays into the %s bus (its fader, pan and rack), not straight to the master"
                    : "This track plays straight to the master", pick ? P.buses[pick - 1].name : "");
    }
    tip((SDL_Rect){x, y, hw, 26}, "Where the track plays: the master, or a bus that sums several tracks (Mixer > Buses)");
    y += 32;
    int k = 0, sw = (w - 8) / 2;
    for (int b = 2; b < P.nbuses; b++, k++) {
        char lab[48];
        snprintf(lab, sizeof lab, "To %.20s", P.buses[b].name);
        fnumber((SDL_Rect){x + (k % 2) * (sw + 8), y + (k / 2) * 30, sw, 26}, lab, &t->send[b], 0, 1, 0.004, "%.2f");
    }
    if (k) y += ((k + 1) / 2) * 30;
    return y + 6;
}

/* Mixer > Buses: the delay and reverb returns and your own buses, each with a fader, pan, mute and rack */
int sel_bus;                                    /* the bus the Buses tab shows */

int bus_add(const char *name)                   /* a new bus (index), or -1 when there are MAX_BUSES */
{
    if (P.nbuses >= MAX_BUSES) return -1;
    LOCK();
    Bus *nb = &P.buses[P.nbuses];
    memset(nb, 0, sizeof *nb);
    nb->volume = 1;
    if (name) snprintf(nb->name, sizeof nb->name, "%.31s", name);
    else snprintf(nb->name, sizeof nb->name, "Bus %d", P.nbuses - 1);
    int b = P.nbuses++;
    UNLOCK();
    return b;
}

int buses_tab(int x, int y, int w)
{
    char buf[120];
    if (sel_bus >= P.nbuses) sel_bus = P.nbuses - 1;
    y = section(x, y, w, "BUSES");
    for (int b = 0; b < P.nbuses; b++) {
        Bus *bs = &P.buses[b];
        SDL_Rect row = {x, y, w, 28};
        int on = b == sel_bus;
        fill(row, on ? PANEL3 : PANEL2);
        if (on) outline(row, ACCENT);
        int users = 0;
        for (int t = 0; t < P.ntracks; t++)
            users += P.tracks[t].out == b + 1 || (b == BUS_DELAY ? P.tracks[t].delay_send > 0 : b == BUS_REVERB ? P.tracks[t].reverb_send > 0 : P.tracks[t].send[b] > 0);
        snprintf(buf, sizeof buf, "%s%s", bs->name, b < 2 ? " (the shared effect)" : "");
        text_clip((SDL_Rect){x + 8, y, w / 2, 28}, x + 8, y + 7, buf, on ? HEAD : TEXT);
        snprintf(buf, sizeof buf, "%d track%s, %d effect%s%s", users, users == 1 ? "" : "s", bs->rack.n, bs->rack.n == 1 ? "" : "s",
                 bs->mute ? ", muted" : "");
        text_clip((SDL_Rect){x + w / 2, y, w / 2 - 8, 28}, x + w / 2, y + 7, buf, DIM);
        if (hit(row) && lpress && drag.kind == D_NONE) {
            lpress = 0;
            sel_bus = b;
        }
        y += 30;
    }
    if (button2((SDL_Rect){x, y + 2, 140, 28}, "+ New bus", 0, P.nbuses < MAX_BUSES)) {
        int nb = bus_add(NULL);
        if (nb >= 0) sel_bus = nb;
        status("A new bus: set a track's Output to it (Mixer > Track) to group tracks, or send to it");
    }
    tip((SDL_Rect){x, y + 2, 140, 28}, "A bus sums the tracks played into it (a group) or sent to it, through its own rack");
    y += 40;
    Bus *bs = &P.buses[sel_bus];
    snprintf(buf, sizeof buf, "%s", sel_bus == BUS_DELAY ? "THE DELAY BUS" : sel_bus == BUS_REVERB ? "THE REVERB BUS" : "THIS BUS");
    y = section(x, y, w, buf);
    if (sel_bus >= 2) {
        textfield((SDL_Rect){x, y, w - 120, 26}, bs->name, sizeof bs->name, 0, "name");
        if (button((SDL_Rect){x + w - 110, y, 110, 26}, "Remove bus", 0)) {
            int gone = sel_bus;
            LOCK();
            for (int t = 0; t < MAX_TRACKS; t++) {      /* its tracks go to the master; sends move down with the buses */
                Track *tr = &P.tracks[t];
                if (tr->out == gone + 1) tr->out = 0;
                else if (tr->out > gone + 1) tr->out--;
                memmove(&tr->send[gone], &tr->send[gone + 1], (size_t)(MAX_BUSES - gone - 1) * sizeof(float));
                tr->send[MAX_BUSES - 1] = 0;
            }
            memmove(&P.buses[gone], &P.buses[gone + 1], (size_t)(P.nbuses - gone - 1) * sizeof(Bus));
            P.nbuses--;
            UNLOCK();
            sel_bus = gone - 1;
            status("Removed the bus: its tracks play to the master again. Ctrl+Z brings it back");
            return y + 30;
        }
        y += 32;
    } else {
        y = wrapped(x, y, w, sel_bus == BUS_DELAY
                              ? "The delay every track's Delay send feeds. Its echoes are set under Effects; anything in its rack "
                                "comes after them."
                              : "The reverb every track's Reverb send feeds. Its room is set under Effects; anything in its rack "
                                "comes after it.", DIM) + 6;
    }
    int tw = (w - 16) / 3;
    fnumber((SDL_Rect){x, y, tw, 26}, "Level", &bs->volume, 0, 1.5, 0.004, "%.2f");
    double *pd = fproxy(&bs->pan);
    char pb[24];
    pan_text((float)*pd, pb, sizeof pb);
    number_text((SDL_Rect){x + tw + 8, y, tw, 26}, "Pan", pd, -1, 1, 0.005, pb);
    fcommit(&bs->pan, pd);
    if (button((SDL_Rect){x + 2 * (tw + 8), y, tw, 26}, bs->mute ? "Muted" : "Mute", bs->mute)) {
        LOCK();
        bs->mute = !bs->mute;
        UNLOCK();
    }
    y += 32;
    if (sel_bus >= 2) {
        int hw = (w - 8) / 2;
        fnumber((SDL_Rect){x, y, hw, 26}, "To the delay", &bs->send_d, 0, 1, 0.004, "%.2f");
        fnumber((SDL_Rect){x + hw + 8, y, hw, 26}, "To the reverb", &bs->send_r, 0, 1, 0.004, "%.2f");
        y += 32;
    }
    y = section(x, y + 4, w, "ITS EFFECT RACK");
    y = rack_editor(x, y, w, &bs->rack, MAX_TRACKS + sel_bus);
    return y;
}

int mixer_content(int x, int y, int w)
{
    char buf[160], b2[48];
    int hw = (w - 8) / 2, tw = (w - 16) / 3, rh = 34;

    static const char *const SUBS[] = {"Levels", "Clip", "Track", "Effects", "Buses", "Live"};
    y = subtabs(x, y, w, SUBS, 6, &mixer_sub);
    if (mixer_sub == 0) y = levels_tab(x, y, w);
    if (mixer_sub == 1) {
    /* the selected clips: the one clicked last shows its values; a change applies to all of them */
    y = section(x, y, w, "CLIP");
    if (selected >= 0 && selected < P.nclips) {
        Clip *c = &P.clips[selected];
        int many = sel_count();
        if (many > 1) snprintf(buf, sizeof buf, "%d clips selected: changes apply to all", many);
        else snprintf(buf, sizeof buf, "%s", P.samples[sample_base(&P, c->sample)].name);
        text_clip((SDL_Rect){x, y, w, font_h + 2}, x, y, buf, many > 1 ? HEAD : TEXT);
        y += font_h + 8;
        float g0 = c->gain_db;
        fnumber((SDL_Rect){x, y, hw, 26}, "Gain", &c->gain_db, -36, 12, 0.05, "%+.1f dB");
        if (c->gain_db != g0 && many > 1) {     /* move every selected clip's gain by the same amount */
            float d = c->gain_db - g0;
            LOCK();
            for (int i = 0; i < P.nclips; i++)
                if (csel[i] && i != selected) {
                    float v = P.clips[i].gain_db + d;
                    P.clips[i].gain_db = v < -36 ? -36 : v > 12 ? 12 : v;
                }
            UNLOCK();
        }
        static double semis;
        if (!(drag.kind == D_NUM && drag.num == &semis)) semis = clip_semis(c);
        number((SDL_Rect){x + hw + 8, y, hw, 26}, "Pitch", &semis, -24, 24, 0.08, "%+.0f st");
        if (drag.kind != D_NUM && (int)lround(semis) != clip_semis(c))
            for (int i = 0; i < P.nclips; i++)
                if (csel[i]) clip_variant(i, (int)lround(semis), clip_rev(&P.clips[i]));
        y += rh;
        c = &P.clips[selected];
        double fin = (double)c->fade_in / SR * 1000, fout = (double)c->fade_out / SR * 1000;
        double maxms = (double)c->length / SR * 1000;
        static double din, dout;
        if (!(drag.kind == D_NUM && drag.num == &din)) din = fin;
        if (!(drag.kind == D_NUM && drag.num == &dout)) dout = fout;
        number((SDL_Rect){x, y, tw, 26}, "In", &din, 0, maxms, 2, "%.0f ms");
        number((SDL_Rect){x + tw + 8, y, tw, 26}, "Out", &dout, 0, maxms, 2, "%.0f ms");
        if (din != fin || dout != fout) {
            LOCK();
            for (int i = 0; i < P.nclips; i++) {
                if (!csel[i]) continue;
                Clip *ci = &P.clips[i];
                long a = (long)llround(din / 1000 * SR), b = (long)llround(dout / 1000 * SR);
                if (a > ci->length) a = ci->length;
                if (a + b > ci->length) b = ci->length - a;
                if (din != fin) ci->fade_in = a;
                if (dout != fout) ci->fade_out = b < 0 ? 0 : b;
            }
            UNLOCK();
        }
        if (button((SDL_Rect){x + 2 * (tw + 8), y, tw, 26}, "Reverse", clip_rev(c))) {
            int rev = !clip_rev(c);
            for (int i = 0; i < P.nclips; i++)
                if (csel[i]) clip_variant(i, clip_semis(&P.clips[i]), rev);
        }
        y += rh;
        text(x, y, "Drag the squares on a clip's top corners to fade it", DIM);
        y += font_h + 14;
        y = section(x, y, w, "PITCH BEND");
        y = clip_bend_editor(x, y, w);
    } else {
        text(x, y, "Click a clip, or drag a box around several", DIM);
        y += font_h + 14;
    }

    }
    if (mixer_sub == 2) {
    /* the selected track */
    if (sel_track >= P.ntracks) sel_track = P.ntracks - 1;
    Track *t = &P.tracks[sel_track];
    y = section(x, y, w, "TRACK");
    fill((SDL_Rect){x, y + 4, 4, 18}, TRACK_COL[sel_track]);
    track_dropdown((SDL_Rect){x + 10, y, w - 10, 26}, "Track ", &sel_track);
    t = &P.tracks[sel_track];
    y += rh;
    y = preset_row(x, y, w, PK_TRACK, t);
    fnumber((SDL_Rect){x, y, hw, 26}, "Volume", &t->volume, 0, 1.5, 0.004, "%.2f");
    double *pd = fproxy(&t->pan);
    pan_text((float)*pd, b2, sizeof b2);
    number_text((SDL_Rect){x + hw + 8, y, hw, 26}, "Pan", pd, -1, 1, 0.005, b2);
    fcommit(&t->pan, pd);
    y += rh;
    {                                           /* automation overrides these while it has points */
        char au[120] = "";
        for (int p = 0; p < AUTO_PARAMS; p++)
            if (auto_lane(&P, sel_track, p, NULL)) {
                if (au[0]) strncat(au, ", ", sizeof au - strlen(au) - 1);
                strncat(au, AUTO_NAMES[p], sizeof au - strlen(au) - 1);
            }
        if (au[0]) {
            char line_[200];
            snprintf(line_, sizeof line_, "Automated: %s (the track's A lane sets them)", au);
            text_clip((SDL_Rect){x, y, w, font_h + 4}, x, y, line_, ACCENT);
            y += font_h + 10;
        }
    }
    static const char *FNAMES[FILT_TYPES] = {"Off", "Low-pass", "High-pass", "Band-pass"};
    text(x, y + 6, "Filter", DIM);
    int fw = (w - 60 - 12) / 4;
    for (int k = 0; k < FILT_TYPES; k++)
        if (button((SDL_Rect){x + 60 + k * (fw + 4), y, fw, 26}, FNAMES[k], t->filter == k)) {
            LOCK();
            t->filter = k;
            UNLOCK();
        }
    y += rh;
    double *cp = fproxy(&t->cutoff);
    static double cpos;                         /* cutoff is dragged on a log scale */
    if (!(drag.kind == D_NUM && drag.num == &cpos)) cpos = log(*cp / 20.0) / log(1000.0);
    double hz = 20 * pow(1000.0, cpos);
    if (hz >= 1000) snprintf(b2, sizeof b2, "%.2f kHz", hz / 1000);
    else snprintf(b2, sizeof b2, "%.0f Hz", hz);
    number_text((SDL_Rect){x, y, hw, 26}, "Cutoff", &cpos, 0, 1, 0.002, b2);
    *cp = 20 * pow(1000.0, cpos);
    fcommit(&t->cutoff, cp);
    fnumber((SDL_Rect){x + hw + 8, y, hw, 26}, "Resonance", &t->reso, 0, 1, 0.004, "%.2f");
    y += rh;
    static const char *EQN[3] = {"Low", "Mid", "High"};
    for (int k = 0; k < 3; k++)
        fnumber((SDL_Rect){x + k * (tw + 8), y, tw, 26}, EQN[k], &t->eq[k], -15, 15, 0.05, "%+.1f dB");
    y += rh;
    fnumber((SDL_Rect){x, y, hw, 26}, "Drive", &t->drive, 0, 1, 0.004, "%.2f");
    snprintf(buf, sizeof buf, P.fx.sc_source >= 0 ? "Duck (by %d)" : "Duck", P.fx.sc_source + 1);
    double before = t->duck;
    fnumber((SDL_Rect){x + hw + 8, y, hw, 26}, buf, &t->duck, 0, 1, 0.004, "%.2f");
    if (before == 0 && t->duck > 0 && P.fx.sc_source < 0) {     /* pick a sidechain source: the kick */
        int src = sel_track == 0 ? 1 : 0;
        for (int k = 0; k < P.ntracks; k++)
            if (k != sel_track && (strstr(P.tracks[k].name, "Kick") || strstr(P.tracks[k].name, "kick"))) src = k;
        LOCK();
        P.fx.sc_source = src < P.ntracks ? src : -1;
        UNLOCK();
        status("Track %d now ducks this one: change it under Master effects", P.fx.sc_source + 1);
    }
    y += rh;
    fnumber((SDL_Rect){x, y, hw, 26}, "Delay send", &t->delay_send, 0, 1, 0.004, "%.2f");
    fnumber((SDL_Rect){x + hw + 8, y, hw, 26}, "Reverb send", &t->reverb_send, 0, 1, 0.004, "%.2f");
    y += rh + 8;
    y = section(x, y, w, "WIDTH AND COMPRESSOR");
    fnumber((SDL_Rect){x, y, hw, 26}, "Stereo width", &t->width, 0, 2, 0.004, "%.2f");
    if (button((SDL_Rect){x + hw + 8, y, hw, 26}, t->comp_on ? "Compressor: on" : "Compressor: off", t->comp_on)) {
        LOCK();
        t->comp_on = !t->comp_on;
        UNLOCK();
    }
    y += rh;
    fnumber((SDL_Rect){x, y, tw, 26}, "Threshold", &t->comp_thresh, -48, 0, 0.1, "%.0f dB");
    fnumber((SDL_Rect){x + tw + 8, y, tw, 26}, "Ratio", &t->comp_ratio, 1, 20, 0.02, "%.1f:1");
    fnumber((SDL_Rect){x + 2 * (tw + 8), y, tw, 26}, "Makeup", &t->comp_makeup, 0, 24, 0.05, "%+.1f dB");
    y += rh;
    fnumber((SDL_Rect){x, y, tw, 26}, "Attack", &t->comp_attack, 0.5, 200, 0.2, "%.0f ms");
    fnumber((SDL_Rect){x + tw + 8, y, tw, 26}, "Release", &t->comp_release, 20, 2000, 2, "%.0f ms");
    {
        LOCK();
        float gr = mixer_track_gr(MX, sel_track);
        UNLOCK();
        snprintf(buf, sizeof buf, "-%.1f dB", gr);
        text(x + 2 * (tw + 8) + 6, y + 6, t->comp_on ? buf : "", gr > 0.1f ? HEAD : DIM);
    }
    y += rh;
    text(x, y, "Width 0 is mono, 1 as it is, 2 twice as wide.", DIM);
    y += font_h + 14;
    y = section(x, y, w, "OUTPUT AND SENDS");
    y = output_section(x, y, w, t);
    y = section(x, y, w, "EFFECT RACK");
    y = wrapped(x, y, w, "Effects after the strip above, in order: add, turn off, reorder or take out. Macros can "
                         "turn their knobs.", DIM) + 4;
    y = rack_editor(x, y, w, &t->rack, sel_track);
    y += 4;
    y = section(x, y, w, "MACROS");
    y = macros_section(x, y, w, t);
    y += 8;

    }
    MasterFx *fx = &P.fx;
    if (mixer_sub == 3) {
    /* shared effects */
    y = section(x, y, w, "DELAY");
    y = preset_row(x, y, w, PK_DELAY, NULL);
    int dw = (w - 7 * 4) / 8;
    for (int k = 0; k < DELAY_DIVS; k++)
        if (button((SDL_Rect){x + k * (dw + 4), y, dw, 26}, DELAY_NAMES[k], fx->delay_div == k)) {
            LOCK();
            fx->delay_div = k;
            UNLOCK();
        }
    y += rh;
    fnumber((SDL_Rect){x, y, tw, 26}, "Feedback", &fx->delay_feedback, 0, 0.95, 0.004, "%.2f");
    fnumber((SDL_Rect){x + tw + 8, y, tw, 26}, "Tone", &fx->delay_tone, 0, 1, 0.004, "%.2f");
    fnumber((SDL_Rect){x + 2 * (tw + 8), y, tw, 26}, "Level", &fx->delay_level, 0, 1.5, 0.004, "%.2f");
    y += rh;
    if (button((SDL_Rect){x, y, 120, 26}, "Ping-pong", fx->pingpong)) {
        LOCK();
        fx->pingpong = !fx->pingpong;
        UNLOCK();
    }
    snprintf(buf, sizeof buf, "%.0f ms at %.0f BPM", DELAY_BEATS[fx->delay_div] * 60000.0 / P.bpm, P.bpm);
    text(x + 132, y + 6, buf, DIM);
    y += rh + 8;
    y = section(x, y, w, "REVERB");
    y = preset_row(x, y, w, PK_REVERB, NULL);
    fnumber((SDL_Rect){x, y, tw, 26}, "Size", &fx->reverb_size, 0, 1, 0.004, "%.2f");
    fnumber((SDL_Rect){x + tw + 8, y, tw, 26}, "Damping", &fx->reverb_damp, 0, 1, 0.004, "%.2f");
    fnumber((SDL_Rect){x + 2 * (tw + 8), y, tw, 26}, "Level", &fx->reverb_level, 0, 1.5, 0.004, "%.2f");
    y += rh + 8;
    y = section(x, y, w, "SIDECHAIN");
    {                                           /* off, or which track ducks the others */
        static char names[MAX_TRACKS + 1][48];
        static const char *ptrs[MAX_TRACKS + 1];
        static int pick;
        snprintf(names[0], sizeof names[0], "off");
        ptrs[0] = names[0];
        for (int k = 0; k < P.ntracks; k++) {
            snprintf(names[k + 1], sizeof names[k + 1], "%d  %s", k + 1, P.tracks[k].name);
            ptrs[k + 1] = names[k + 1];
        }
        if (!(dd.open && dd.sel == &pick)) pick = fx->sc_source + 1;
        if (dropdown((SDL_Rect){x, y, hw, 26}, "Source: ", ptrs, P.ntracks + 1, &pick, 0)) {
            LOCK();
            fx->sc_source = pick - 1;
            UNLOCK();
        }
    }
    fnumber((SDL_Rect){x + hw + 8, y, hw, 26}, "Release", &fx->sc_release, 20, 1000, 1, "%.0f ms");
    y += rh;
    text(x, y, "Tracks with Duck above 0 dip whenever the source hits", DIM);
    y += font_h + 16;

    y = section(x, y, w, "MASTER: LOUDNESS AND PEAKS");
    {                                           /* one click to a finished sound, then the details */
        static const char *const PRE[3] = {"Off", "Glue", "Loud"};
        int cur = !fx->comp_on && !fx->limit_on ? 0 : fx->comp_on && fx->comp_ratio <= 2.6f && fx->comp_thresh >= -19 ? 1
                : fx->comp_on ? 2 : -1;
        int bw = (w - 8) / 3;
        for (int i = 0; i < 3; i++)
            if (button((SDL_Rect){x + i * (bw + 4), y, bw, 28}, PRE[i], cur == i)) {
                LOCK();
                if (i == 0) fx->comp_on = fx->limit_on = 0;
                else if (i == 1) {                /* gentle: holds the mix together, catches peaks */
                    fx->comp_on = fx->limit_on = 1;
                    fx->comp_thresh = -18, fx->comp_ratio = 2.5f, fx->comp_attack = 15, fx->comp_release = 180;
                    fx->comp_makeup = 3, fx->limit_ceiling = -1;
                } else {                          /* loud: pushes it up hard against the limiter */
                    fx->comp_on = fx->limit_on = 1;
                    fx->comp_thresh = -24, fx->comp_ratio = 4, fx->comp_attack = 8, fx->comp_release = 120;
                    fx->comp_makeup = 8, fx->limit_ceiling = -1;
                }
                UNLOCK();
            }
        y += 34;
        static const char *const ABOUT[3] = {"Off: the mix as it is", "Glue: gentle compression, peaks held under -1 dB",
                                             "Loud: hard compression into the limiter, like a release"};
        text(x, y, cur >= 0 ? ABOUT[cur] : "Your own settings", DIM);
        y += font_h + 10;
        if (button((SDL_Rect){x, y, hw, 26}, fx->comp_on ? "Compressor: on" : "Compressor: off", fx->comp_on)) {
            LOCK();
            fx->comp_on = !fx->comp_on;
            UNLOCK();
        }
        if (button((SDL_Rect){x + hw + 8, y, hw, 26}, fx->limit_on ? "Limiter: on" : "Limiter: off", fx->limit_on)) {
            LOCK();
            fx->limit_on = !fx->limit_on;
            UNLOCK();
        }
        y += rh;
        fnumber((SDL_Rect){x, y, tw, 26}, "Threshold", &fx->comp_thresh, -48, 0, 0.1, "%.0f dB");
        fnumber((SDL_Rect){x + tw + 8, y, tw, 26}, "Ratio", &fx->comp_ratio, 1, 20, 0.02, "%.1f:1");
        fnumber((SDL_Rect){x + 2 * (tw + 8), y, tw, 26}, "Makeup", &fx->comp_makeup, 0, 24, 0.05, "%+.1f dB");
        y += rh;
        fnumber((SDL_Rect){x, y, tw, 26}, "Attack", &fx->comp_attack, 0.5, 200, 0.2, "%.0f ms");
        fnumber((SDL_Rect){x + tw + 8, y, tw, 26}, "Release", &fx->comp_release, 20, 2000, 2, "%.0f ms");
        fnumber((SDL_Rect){x + 2 * (tw + 8), y, tw, 26}, "Ceiling", &fx->limit_ceiling, -12, 0, 0.02, "%.1f dB");
        y += rh;
        float cgr, lgr;                           /* how hard they're working */
        LOCK();
        mixer_dynamics_meter(MX, &cgr, &lgr);
        UNLOCK();
        snprintf(buf, sizeof buf, "Turning down: compressor %.1f dB, limiter %.1f dB", cgr, lgr);
        text(x, y, buf, cgr > 0.1f || lgr > 0.1f ? HEAD : DIM);
        y += font_h + 8;
        y = loudness_readout(x, y, w);
        y += 8;
    }

    }
    if (mixer_sub == 4) y = buses_tab(x, y, w);
    if (mixer_sub == 5) {
    y = section(x, y, w, "LIVE CONTROLS");
    fnumber((SDL_Rect){x, y, hw, 26}, "Bend range", &fx->bend_range, 1, 24, 0.05, "%.0f st");
    SDL_Rect meter = {x + hw + 8, y + 4, hw, 18};
    fill(meter, PANEL2);
    int mid = meter.x + meter.w / 2;
    int bx = mid + (int)(P.bend * meter.w / 2);
    fill((SDL_Rect){bx < mid ? bx : mid, meter.y, abs(bx - mid) + 1, meter.h}, ACCENT);
    snprintf(buf, sizeof buf, "bend %+.1f st", P.bend * fx->bend_range);
    text_center(meter, buf, TEXT);
    line(mid, meter.y - 3, mid, meter.y, DIM);
    line(mid, meter.y + meter.h, mid, meter.y + meter.h + 3, DIM);
    y += rh;
    SDL_Rect mw = {x + hw + 8, y + 4, hw, 18};
    fill(mw, PANEL2);
    fill((SDL_Rect){mw.x, mw.y, (int)(mw.w * P.modwheel), mw.h}, TRACK_COL[3]);
    text_center(mw, "mod wheel", TEXT);
    snprintf(buf, sizeof buf, "opens track %d's filter", P.inst_track + 1);
    text(x, y + 6, buf, DIM);
    y += rh;
    text(x, y, "The pitch wheel (or a controller stick: Settings > Input)", DIM);
    text(x, y + font_h + 2, "bends pads and the instrument live.", DIM);
    y += font_h + 2;
    text(x, y + font_h + 2, "Set the instrument's track (Pads pane) to a filter,", DIM);
    text(x, y + 2 * font_h + 4, "then push the mod wheel up to open it.", DIM);
    y += 3 * font_h + 12;
    }
    return y;
}

void mixer_pane(void)
{
    SDL_Rect pane = {L.pane_x, L.top, L.pane_w, H - L.top - L.status};
    fill(pane, PANEL);
    line(pane.x + pane.w - 1, pane.y, pane.x + pane.w - 1, pane.y + pane.h, LINE);
    int inside = hit(pane), saved_press = lpress;
    if (!inside) lpress = 0;
    int max_scroll = mixer_content_h - pane.h > 0 ? mixer_content_h - pane.h : 0;
    mixer_scroll = mixer_scroll < 0 ? 0 : mixer_scroll > max_scroll ? max_scroll : mixer_scroll;
    SDL_RenderSetClipRect(ren, &pane);
    int y0 = pane.y + 12 - mixer_scroll;
    int y = mixer_content(pane.x + 14, y0, pane.w - 28);
    mixer_content_h = y - y0 + 12;
    (void)max_scroll;
    scrollbar(pane, &mixer_scroll, mixer_content_h);
    SDL_RenderSetClipRect(ren, NULL);
    if (!inside) lpress = saved_press;
}
