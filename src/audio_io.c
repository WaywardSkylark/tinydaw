/* tinydaw: audio devices, the microphone and the demo. */
#include "ui.h"

void audio_cb(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    float *out = (float *)stream;
    int frames = len / (int)(2 * sizeof(float));
    memset(stream, 0, (size_t)len);
    int done = 0;
    if (P.playing && countin_left > 0) {        /* the count-in: clicks only, the song waits */
        int n = countin_left < frames ? (int)countin_left : frames;
        mixer_run(MX, &P, P.playhead, 0, out, n, -1, 1);
        metronome_mix_ex(out, countin_total - countin_left, n, 1, 1);
        countin_left -= n;
        done = n;
    }
    if (!P.playing) mixer_run(MX, &P, P.playhead, 0, out, frames, -1, 1);   /* pads, instrument, tails */
    if (P.playing) {
        while (done < frames) {
            int n = frames - done;
            int looping = P.loop && P.loop_end > P.loop_start && P.playhead < P.loop_end;
            if (looping && P.playhead + n > P.loop_end) n = (int)(P.loop_end - P.playhead);
            if (n <= 0) {
                P.playhead = P.loop_start;
                continue;
            }
            mixer_run(MX, &P, P.playhead, 1, out + 2 * done, n, -1, 1);
            metronome_mix(out + 2 * done, P.playhead, n, 0);
            P.playhead += n;
            done += n;
            if (looping && P.playhead >= P.loop_end) P.playhead = P.loop_start;
        }
    }
    if (P.preview >= 0) {
        const Sample *s = &P.samples[P.preview];
        for (int i = 0; i < frames && P.preview_pos < s->frames; i++, P.preview_pos++) {
            out[2 * i] += s->data[2 * P.preview_pos] * 0.9f;
            out[2 * i + 1] += s->data[2 * P.preview_pos + 1] * 0.9f;
        }
        if (P.preview_pos >= s->frames) P.preview = -1;
    }
    if (scratch) {
        for (int i = 0; i < frames && scratch_pos < scratch_len; i++, scratch_pos++) {
            out[2 * i] += scratch[2 * scratch_pos];
            out[2 * i + 1] += scratch[2 * scratch_pos + 1];
        }
    }
    for (int i = 0; i < 2 * frames; i++) out[i] *= P.master;
    master_dynamics(MX, &P, out, frames);         /* the master's compressor and limiter */
    if (monitor_on && cap_ring && cap_dev) {      /* hearing yourself: the newest of the microphone */
        unsigned w = (unsigned)SDL_AtomicGet(&cap_w);
        if (w - mon_r > (unsigned)(2 * frames + 1024)) mon_r = w - (unsigned)frames;   /* stay close behind */
        for (int i = 0; i < frames && mon_r != w; i++, mon_r++) {
            unsigned j = mon_r % CAP_RING;
            out[2 * i] += cap_ring[2 * j];
            out[2 * i + 1] += cap_ring[2 * j + 1];
        }
    }
    for (int i = 0; i < 2 * frames; i++) out[i] = soft_clip(out[i]);
    for (int i = 0; i < frames; i++) {          /* for the meters and the visualizer */
        vis_ring[2 * vis_pos] = out[2 * i];
        vis_ring[2 * vis_pos + 1] = out[2 * i + 1];
        vis_pos = (vis_pos + 1) % VIS_RING;
    }
}

/* (Re)opens the output at the project rate. SDL converts if the hardware runs at another rate. */
void open_audio(void)
{
    if (dev) {
        SDL_CloseAudioDevice(dev);
        dev = 0;
    }
    if (mixer_rate(MX) != SR) {         /* the device is closed: nothing is using the old one */
        mixer_free(MX);
        MX = mixer_new();
    }
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = SR;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = SR > 96000 ? 2048 : SR > 48000 ? 1024 : 512;   /* about 10 ms */
    want.callback = audio_cb;
    dev = SDL_OpenAudioDevice(audio_out[0] ? audio_out : NULL, 0, &want, &have, 0);
    if (!dev && audio_out[0]) {         /* that device has gone: use the default */
        status("Output \"%s\" isn't available: using the default", audio_out);
        dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    }
    if (dev) {
        out_buffer = have.samples;
        SDL_PauseAudioDevice(dev, 0);
    } else {
        status("No audio output: %s", SDL_GetError());
    }
}

/* ---------------------------------------------------------------- the microphone
 *
 * The input device runs only while it's needed: a track is armed to record it, or the Input
 * settings are showing. Its callback (SDL's capture thread) writes into a ring, noting for each
 * chunk where the song was; each frame the interface drains the ring into the take being recorded.
 * A take's start is that song position, less the round trip through the audio buffers (and any
 * offset set by hand), so what was played lands where it was heard. */

int armed_track(void)
{
    for (int t = 0; t < P.ntracks; t++)
        if (track_arm[t]) return t;
    return -1;
}

void cap_cb(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    const float *in = (const float *)stream;
    int ch = cap_channels < 1 ? 1 : cap_channels, frames = len / (int)(ch * sizeof(float));
    unsigned w = (unsigned)SDL_AtomicGet(&cap_w);
    int k = SDL_AtomicGet(&cap_nmark);         /* this chunk: where the song is */
    cap_marks[k % 64].w = w;
    cap_marks[k % 64].playhead = P.playhead;
    cap_marks[k % 64].playing = P.playing && countin_left == 0;
    SDL_AtomicSet(&cap_nmark, k + 1);
    float g = powf(10, in_gain_db / 20), pk = in_peak;
    for (int i = 0; i < frames; i++) {
        float l = in[i * ch] * g, r = ch > 1 ? in[i * ch + 1] * g : l;
        if (fabsf(l) > pk) pk = fabsf(l);
        if (fabsf(r) > pk) pk = fabsf(r);
        unsigned j = (w + (unsigned)i) % CAP_RING;
        cap_ring[2 * j] = l;
        cap_ring[2 * j + 1] = r;
    }
    in_peak = pk;
    SDL_AtomicSet(&cap_w, (int)(w + (unsigned)frames));
}

void close_input(void)
{
    if (cap_dev) SDL_CloseAudioDevice(cap_dev);
    cap_dev = 0;
}

int open_input(void)
{
    close_input();
    if (!cap_ring) cap_ring = calloc((size_t)CAP_RING * 2, sizeof(float));
    if (!cap_ring) return 0;
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = SR;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = SR > 96000 ? 2048 : SR > 48000 ? 1024 : 512;
    want.callback = cap_cb;
    cap_dev = SDL_OpenAudioDevice(audio_in[0] ? audio_in : NULL, 1, &want, &have, SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
    if (!cap_dev && audio_in[0]) {
        status("Input \"%s\" isn't available: using the default", audio_in);
        cap_dev = SDL_OpenAudioDevice(NULL, 1, &want, &have, SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
    }
    if (!cap_dev) {
#ifdef _WIN32
        status("No microphone: %s (Windows Settings > Privacy > Microphone must let apps use it)", SDL_GetError());
#else
        status("No microphone: %s", SDL_GetError());
#endif
        return 0;
    }
    cap_channels = have.channels;
    cap_buffer = have.samples;
    cap_r = mon_r = (unsigned)SDL_AtomicGet(&cap_w);
    SDL_PauseAudioDevice(cap_dev, 0);
    return 1;
}

/* the automatic offset: one input buffer and one output buffer, in ms */
double rec_offset_auto(void) { return (double)(cap_buffer + out_buffer) * 1000.0 / SR; }

double rec_offset(void) { return rec_offset_ms >= 0 ? rec_offset_ms : rec_offset_auto(); }

/* Each frame: the input open while it's wanted, its meter, and anything new into the take. */
void input_update(void)
{
    int want = armed_track() >= 0 || (settings_open && settings_sub == 1) || mic.on;
    if (want && !cap_dev && !cap_fake) {
        static Uint32 tried;                    /* don't hammer a missing device */
        if (!tried || SDL_GetTicks() - tried > 3000) {
            tried = SDL_GetTicks();
            open_input();
        }
    } else if (!want && cap_dev) {
        close_input();
    }
    in_meter = fmaxf(in_peak, in_meter * 0.85f);
    in_peak = 0;
    if (!cap_ring) return;
    unsigned w = (unsigned)SDL_AtomicGet(&cap_w), n = w - cap_r;
    if (n > CAP_RING) {                         /* fell behind: the oldest is gone */
        if (mic.on) mic.lost = 1;
        cap_r = w - CAP_RING;
        n = CAP_RING;
    }
    if (mic.on && n) {
        if (!mic.start_known) {                 /* where the first frame belongs in the song */
            int k = SDL_AtomicGet(&cap_nmark), best = -1;
            for (int i = 1; i <= 64 && i <= k; i++) {
                int m = (k - i) % 64;
                if (cap_marks[m].w <= cap_r && cap_marks[m].playing) {
                    best = m;
                    break;
                }
            }
            long ph = best >= 0 ? cap_marks[best].playhead + (long)(cap_r - cap_marks[best].w) : P.playhead;
            mic.start = ph - (long)llround(rec_offset() / 1000.0 * SR);
            mic.start_known = 1;
        }
        if (mic.n + (long)n > mic.cap) {
            long nc = mic.cap ? mic.cap * 2 : (long)SR * 30;
            while (nc < mic.n + (long)n) nc *= 2;
            float *nb = realloc(mic.buf, (size_t)nc * 2 * sizeof(float));
            if (!nb) {
                mic.lost = 1;
                cap_r = w;
                return;
            }
            mic.buf = nb;
            mic.cap = nc;
        }
        for (unsigned i = 0; i < n; i++) {
            unsigned j = (cap_r + i) % CAP_RING;
            mic.buf[2 * (mic.n + i)] = cap_ring[2 * j];
            mic.buf[2 * (mic.n + i) + 1] = cap_ring[2 * j + 1];
        }
        mic.n += n;
    }
    cap_r = w;
}

void mic_take_start(void)
{
    int t = armed_track();
    if (t < 0 || (!cap_dev && !cap_fake)) return;
    mic.on = 1;
    mic.track = t;
    mic.n = 0;
    mic.lost = 0;
    mic.start_known = 0;
    cap_r = (unsigned)SDL_AtomicGet(&cap_w);    /* from now on */
}

/* The take's sound: saved as a WAV in Music/tinydaw/Recordings and put on the armed track.
 * Returns the clip, or -1. */
int mic_take_end(void)
{
    if (!mic.on) return -1;
    input_update();                              /* the last of it */
    mic.on = 0;
    if (mic.n < SR / 20) return -1;              /* nothing worth keeping */
    long start = mic.start, skip = 0;
    if (start < 0) {                             /* began before the song: start at its start */
        skip = -start;
        start = 0;
    }
    if (skip >= mic.n) return -1;
    char dir[900], path[1200], stamp[32], name[96], err[300], base[96];
    user_dir("music", dir, sizeof dir);
    snprintf(dir + strlen(dir), sizeof dir - strlen(dir), "/tinydaw/Recordings");
    ensure_dir(dir);
    snprintf(base, sizeof base, "%.60s", proj_path[0] ? path_base(proj_path) : "untitled");
    char *dot = strrchr(base, '.');
    if (dot) *dot = 0;
    for (char *q = base; *q; q++)
        if (!isalnum((unsigned char)*q) && *q != '-') *q = '_';
    time_t now = time(NULL);
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&now));
    snprintf(path, sizeof path, "%s/%s-take-%s.wav", dir, base, stamp);
    snprintf(name, sizeof name, "Mic take %d (%s)", take.number, P.tracks[mic.track].name);
    int idx = sample_record(&P, mic.buf + 2 * skip, mic.n - skip, path, name, err, sizeof err);
    if (idx < 0) {
        status("Couldn't save the recording: %s", err);
        return -1;
    }
    int c = clip_add(idx, mic.track, start, 0, P.samples[idx].frames);
    return c;
}

void set_rate(int rate)
{
    char msg[300];
    double ratio = (double)rate / SR;
    LOCK();
    project_set_rate(&P, rate, msg, sizeof msg);
    UNLOCK();
    history_scale(ratio);
    open_audio();
    metronome_prepare();
    status("%s", msg);
}

/* ---------------------------------------------------------------- demo */

int gen(int wave, int note, double beats, double vol, double att, double rel)
{
    GenParams g = {wave, note, beats, vol, att, rel, P.a4};
    return sample_generate(&P, &g);
}

void make_demo(void)
{
    int kick = gen(WAVE_KICK, 36, 0.5, 0.75, 0, 0);
    int snare = gen(WAVE_SNARE, 60, 0.5, 0.5, 0, 0);
    int hat = gen(WAVE_HAT, 60, 0.25, 0.45, 0, 0);
    int bass[3] = {gen(WAVE_SAW, 33, 0.75, 0.35, 3, 60), gen(WAVE_SAW, 36, 0.75, 0.35, 3, 60),
                   gen(WAVE_SAW, 31, 0.75, 0.35, 3, 60)};
    int pad[3] = {gen(WAVE_TRIANGLE, 57, 4, 0.22, 400, 900), gen(WAVE_TRIANGLE, 60, 4, 0.22, 400, 900),
                  gen(WAVE_TRIANGLE, 64, 4, 0.22, 400, 900)};
    int lead = gen(WAVE_SQUARE, 76, 0.5, 0.16, 5, 120);
    long bf = beat_frames();
    for (int bar = 0; bar < 2; bar++) {
        long b0 = bar * 4 * bf;
        for (int b = 0; b < 4; b++) clip_add(kick, 0, b0 + b * bf, 0, P.samples[kick].frames);
        clip_add(snare, 1, b0 + bf, 0, P.samples[snare].frames);
        clip_add(snare, 1, b0 + 3 * bf, 0, P.samples[snare].frames);
        for (int h = 0; h < 8; h++) clip_add(hat, 2, b0 + h * bf / 2, 0, P.samples[hat].frames);
        static const double bass_at[4] = {0, 1.5, 2, 3};
        static const int bass_note[4] = {0, 0, 1, 2};
        for (int i = 0; i < 4; i++) {
            int s = bass[bass_note[i]];
            clip_add(s, 3, b0 + (long)(bass_at[i] * bf), 0, P.samples[s].frames);
        }
        for (int i = 0; i < 3; i++) clip_add(pad[i], 4 + i, b0, 0, P.samples[pad[i]].frames);
    }
    clip_add(lead, 7, 7 * bf, 0, P.samples[lead].frames);
    clip_add(lead, 7, (long)(7.5 * bf), 0, P.samples[lead].frames);
    P.loop_start = 0;
    P.loop_end = 8 * bf;
    P.loop = 1;
    status("Demo loaded: press Space to play");
}
