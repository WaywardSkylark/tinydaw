/* tinydaw: the visualizer and the full-screen show. */
#include "ui.h"

float vis_ring[2 * VIS_RING];
int vis_pos;                     /* written by the audio thread under its lock */
float vis_buf[2 * VIS_FFT], vis_mono[VIS_FFT], vis_db[VIS_FFT / 2], vis_smooth[VIS_FFT / 2];
Meter track_meter[MAX_TRACKS], master_meter[2], bus_meter[MAX_BUSES];
float low_energy, low_avg, high_energy, beat_pulse;   /* for the show */
Uint32 vis_last;

float to_db(float v) { return v > 1e-6f ? 20 * log10f(v) : -120; }

/* fast rise, ~24 dB/s fall, peak held 1.5 s, sticky clip light (click to clear) */
void meter_feed(Meter *m, float v, float dt)
{
    float fall = powf(10, -24 * dt / 20);
    m->level = v > m->level * fall ? v : m->level * fall;
    Uint32 now = SDL_GetTicks();
    if (v >= m->peak || now - m->peak_at > 1500) {
        m->peak = v;
        m->peak_at = now;
    }
    if (v > 1.0f) m->clipped = 1;
}

/* momentary, short-term and integrated LUFS, with a reset */
int loudness_readout(int x, int y, int w)
{
    char buf[160], a[16], b[16], c[16];
    double v[3] = {loud_momentary(&live_loud), loud_short(&live_loud), loud_integrated(&live_loud)};
    char *o[3] = {a, b, c};
    for (int i = 0; i < 3; i++) {
        if (isfinite(v[i])) snprintf(o[i], 16, "%.1f", v[i]);
        else snprintf(o[i], 16, "--");
    }
    snprintf(buf, sizeof buf, "Loudness  now %s   3 s %s   song %s LUFS", a, b, c);
    text_clip((SDL_Rect){x, y + 4, w - 70, font_h + 2}, x, y + 4, buf, TEXT);
    if (button((SDL_Rect){x + w - 64, y, 64, 24}, "Reset", 0)) loud_reset(&live_loud);
    y += 28;
    text(x, y, "Streaming services play songs at about -14 LUFS.", DIM);
    return y + font_h + 4;
}

void vis_update(void)
{
    Uint32 now = SDL_GetTicks();
    float dt = vis_last ? (now - vis_last) / 1000.0f : 0.016f;
    vis_last = now;
    float levels[MAX_TRACKS][2];
    LOCK();
    int pos = vis_pos;
    {                                           /* everything new since last time, to the loudness meter */
        static int last = -1;
        static float chunk[2 * 4096];
        if (last < 0) last = pos;
        int fresh = (pos - last + VIS_RING) % VIS_RING;
        if (fresh > 4096) fresh = 4096;
        for (int i = 0; i < fresh; i++) {
            int k = (pos - fresh + i + VIS_RING) % VIS_RING;
            chunk[2 * i] = vis_ring[2 * k];
            chunk[2 * i + 1] = vis_ring[2 * k + 1];
        }
        last = pos;
        if (P.playing && fresh) loud_feed(&live_loud, chunk, fresh);
    }
    for (int i = 0; i < VIS_FFT; i++) {
        int k = (pos - VIS_FFT + i + VIS_RING) % VIS_RING;
        vis_buf[2 * i] = vis_ring[2 * k];
        vis_buf[2 * i + 1] = vis_ring[2 * k + 1];
    }
    mixer_levels(MX, levels);
    UNLOCK();
    float pl = 0, pr = 0;
    for (int i = VIS_FFT - 1024; i < VIS_FFT; i++) {      /* the latest ~20 ms for the master meter */
        pl = fmaxf(pl, fabsf(vis_buf[2 * i]));
        pr = fmaxf(pr, fabsf(vis_buf[2 * i + 1]));
    }
    meter_feed(&master_meter[0], pl, dt);
    meter_feed(&master_meter[1], pr, dt);
    for (int t = 0; t < MAX_TRACKS; t++) meter_feed(&track_meter[t], fmaxf(levels[t][0], levels[t][1]), dt);
    float blev[MAX_BUSES][2];
    mixer_bus_levels(MX, blev);
    for (int b = 0; b < MAX_BUSES; b++) meter_feed(&bus_meter[b], fmaxf(blev[b][0], blev[b][1]), dt);
    if (!vis_open && !show_open) return;
    static int smooth_ready;
    if (!smooth_ready) {                                  /* start from silence, not full scale */
        for (int k = 0; k < VIS_FFT / 2; k++) vis_smooth[k] = -140;
        smooth_ready = 1;
    }
    for (int i = 0; i < VIS_FFT; i++) vis_mono[i] = 0.5f * (vis_buf[2 * i] + vis_buf[2 * i + 1]);
    spectrum_db(vis_mono, VIS_FFT, vis_db);
    float fall = 60 * dt;                                 /* bars fall 60 dB/s */
    double bin_hz = (double)SR / VIS_FFT, lo = 0, hi = 0;
    int nlo = 0, nhi = 0;
    for (int k = 1; k < VIS_FFT / 2; k++) {
        vis_smooth[k] = vis_db[k] > vis_smooth[k] - fall ? vis_db[k] : vis_smooth[k] - fall;
        double f = k * bin_hz;
        float lin = powf(10, vis_db[k] / 20);
        if (f >= 30 && f < 150) { lo += lin * lin; nlo++; }
        if (f >= 4000 && f < 16000) { hi += lin * lin; nhi++; }
    }
    low_energy = nlo ? sqrtf((float)(lo / nlo)) * 8 : 0;
    high_energy = nhi ? sqrtf((float)(hi / nhi)) * 30 : 0;
    low_avg += (low_energy - low_avg) * fminf(1, dt * 2);
    if (low_energy > low_avg * 1.35f && low_energy > 0.05f && beat_pulse < 0.3f) beat_pulse = 1;   /* a kick */
    beat_pulse *= expf(-dt * 6);
}

Col meter_col(float db)
{
    return db > -0.5f ? (Col){235, 70, 60, 255} : db > -6 ? (Col){240, 190, 70, 255} : (Col){110, 200, 120, 255};
}

/* A level meter, -60..0 dB, in r: vertical or horizontal. */
void meter_draw(SDL_Rect r, const Meter *m, int vertical)
{
    fill(r, ST.shaped && theme_light ? rgb(28, 44, 78) : tone(PANEL, 0.7f));   /* dark well on bright themes */
    float db = to_db(m->level), pk = to_db(m->peak);
    float k = (db + 60) / 60, kp = (pk + 60) / 60;
    k = k < 0 ? 0 : k > 1 ? 1 : k;
    kp = kp < 0 ? 0 : kp > 1 ? 1 : kp;
    if (vertical) {
        int h = (int)(r.h * k), hp = (int)(r.h * kp);
        fill((SDL_Rect){r.x, r.y + r.h - h, r.w, h}, meter_col(db));
        if (hp > 1) fill((SDL_Rect){r.x, r.y + r.h - hp, r.w, 2}, meter_col(pk));
        if (m->clipped) fill((SDL_Rect){r.x, r.y, r.w, 3}, (Col){235, 70, 60, 255});
    } else {
        int w = (int)(r.w * k), wp = (int)(r.w * kp);
        fill((SDL_Rect){r.x, r.y, w, r.h}, meter_col(db));
        if (wp > 1) fill((SDL_Rect){r.x + wp - 2, r.y, 2, r.h}, meter_col(pk));
        if (m->clipped) fill((SDL_Rect){r.x + r.w - 4, r.y, 4, r.h}, (Col){235, 70, 60, 255});
    }
}

double freq_x(double f, SDL_Rect r)   /* 20 Hz .. 20 kHz, logarithmic */
{
    return r.x + r.w * log(f / 20.0) / log(1000.0);
}

void draw_spectrum(SDL_Rect r)
{
    fill(r, tone(BG, 0.9f));
    static const int grid_hz[] = {50, 100, 200, 500, 1000, 2000, 5000, 10000};
    static const char *grid_lbl[] = {"50", "100", "200", "500", "1k", "2k", "5k", "10k"};
    for (int i = 0; i < 8; i++) {
        int x = (int)freq_x(grid_hz[i], r);
        line(x, r.y, x, r.y + r.h, tone(LINE, 0.7f));
        text(x + 3, r.y + r.h - font_h - 2, grid_lbl[i], tone(DIM, 0.8f));
    }
    for (int db = -12; db >= -84; db -= 24) {
        int y = r.y + (int)(r.h * (-db) / 96.0);
        line(r.x, y, r.x + r.w, y, tone(LINE, 0.7f));
        char b[16];
        snprintf(b, sizeof b, "%d", db);
        text(r.x + 3, y + 1, b, tone(DIM, 0.8f));
    }
    double bin_hz = (double)SR / VIS_FFT;
    int prev_y = -1;
    for (int x = r.x; x < r.x + r.w; x++) {      /* each pixel column: the loudest bin under it */
        double f0 = 20 * pow(1000.0, (double)(x - r.x) / r.w), f1 = 20 * pow(1000.0, (double)(x + 1 - r.x) / r.w);
        int k0 = (int)(f0 / bin_hz), k1 = (int)(f1 / bin_hz);
        if (k1 <= k0) k1 = k0 + 1;
        float best = -140;
        for (int k = k0; k < k1 && k < VIS_FFT / 2; k++) best = fmaxf(best, vis_smooth[k]);
        if (k0 < 1) best = -140;
        float t = (best + 96) / 96;
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        int y = r.y + r.h - (int)(r.h * t);
        Col c = tone(ACCENT, 0.45f + 0.55f * t);
        line(x, y, x, r.y + r.h, alpha(c, 150));
        if (prev_y >= 0) line(x - 1, prev_y, x, y, HEAD);
        prev_y = y;
    }
    outline(r, LINE);
}

void draw_scope(SDL_Rect r)
{
    fill(r, tone(BG, 0.9f));
    line(r.x, r.y + r.h / 2, r.x + r.w, r.y + r.h / 2, tone(LINE, 0.7f));
    int span = 2048, start = VIS_FFT - span - 1;
    for (int i = VIS_FFT - span - 1; i > VIS_FFT / 2; i--)   /* trigger on a rising zero crossing */
        if (vis_mono[i - 1] < 0 && vis_mono[i] >= 0) {
            start = i;
            break;
        }
    if (start + span > VIS_FFT) start = VIS_FFT - span;
    int py = -1;
    for (int x = 0; x < r.w; x++) {
        float v = vis_mono[start + x * span / r.w];
        v = v > 1 ? 1 : v < -1 ? -1 : v;
        int y = r.y + r.h / 2 - (int)(v * (r.h / 2 - 2));
        if (py >= 0) line(r.x + x - 1, py, r.x + x, y, TRACK_COL[4]);
        py = y;
    }
    outline(r, LINE);
}

void draw_stereo(SDL_Rect r)              /* goniometer: mono is vertical, wide is spread out */
{
    fill(r, tone(BG, 0.9f));
    int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    line(cx, r.y, cx, r.y + r.h, tone(LINE, 0.7f));
    line(r.x, cy, r.x + r.w, cy, tone(LINE, 0.7f));
    SDL_SetRenderDrawColor(ren, TRACK_COL[6].r, TRACK_COL[6].g, TRACK_COL[6].b, 160);
    double corr_num = 0, el = 0, er = 0;
    for (int i = VIS_FFT - 1536; i < VIS_FFT; i++) {
        float L = vis_buf[2 * i], R = vis_buf[2 * i + 1];
        corr_num += L * R;
        el += L * L;
        er += R * R;
        float sx = (R - L) * 0.7071f, sy = (L + R) * 0.7071f;
        int x = cx + (int)(sx * r.w * 0.45f), y = cy - (int)(sy * r.h * 0.45f);
        if (x > r.x && x < r.x + r.w && y > r.y && y < r.y + r.h) SDL_RenderDrawPoint(ren, x, y);
    }
    double corr = el > 1e-9 && er > 1e-9 ? corr_num / sqrt(el * er) : 1;
    char b[32];
    snprintf(b, sizeof b, "corr %+.2f", corr);
    text(r.x + 4, r.y + 2, b, corr < 0 ? (Col){235, 70, 60, 255} : DIM);
    outline(r, LINE);
}

int vis_content(int x, int y, int w)
{
    char buf[64];
    static const char *const SUBS[] = {"Analyzer", "Meters"};
    y = subtabs(x, y, w, SUBS, 2, &vis_sub);
    {                                           /* the show: which scene, cycling, how long each */
        static const char *names[SCENE_COUNT + 1];
        for (int k = 0; k <= SCENE_COUNT; k++) names[k] = show_name(k);
        if (dropdown((SDL_Rect){x, y, w - 140, 30}, "Show: ", names, SCENE_COUNT + 1, &show_scene, 0)) {
            show_started[show_scene] = show_clock;
            settings_save();
        }
        if (button((SDL_Rect){x + w - 132, y, 132, 30}, show_cycle ? "Cycle: on" : "Cycle: off", show_cycle)) {
            show_cycle = !show_cycle;
            settings_save();
        }
        y += 36;
        double each = show_each;
        number((SDL_Rect){x, y, w / 2 - 4, 28}, "Each scene", &show_each, 5, 600, 0.25, "%.0f s");
        if (show_each != each) settings_save();
        if (button((SDL_Rect){x + w / 2 + 4, y, w / 2 - 4, 28}, "Full-screen show  (F11)", 0)) show_toggle();
        y += 40;
    }
    if (vis_sub == 0) {
    text(x, y, "SPECTRUM", DIM);
    y += font_h + 6;
    draw_spectrum((SDL_Rect){x, y, w, 200});
    y += 212;
    text(x, y, "WAVEFORM", DIM);
    text(x + w / 2 + 6, y, "STEREO", DIM);
    y += font_h + 6;
    draw_scope((SDL_Rect){x, y, w / 2 - 6, 150});
    draw_stereo((SDL_Rect){x + w / 2 + 6, y, w / 2 - 6, 150});
    y += 162;
    text(x, y, "Stereo: a vertical line is mono, a wide cloud is", DIM);
    text(x, y + font_h + 2, "wide. Below zero, left and right cancel in mono.", DIM);
    y += 2 * font_h + 12;
    } else {
    text(x, y, "MASTER", DIM);
    y += font_h + 6;
    for (int c = 0; c < 2; c++) {
        SDL_Rect mr = {x + 20, y + c * 20, w - 110, 14};
        text(x, y + c * 20 - 1, c ? "R" : "L", DIM);
        meter_draw(mr, &master_meter[c], 0);
        snprintf(buf, sizeof buf, "%5.1f dB", fmaxf(-99, to_db(master_meter[c].peak)));
        text(x + w - 82, y + c * 20 - 1, buf, master_meter[c].clipped ? (Col){235, 70, 60, 255} : TEXT);
        if (hit(mr) && lpress) {
            master_meter[0].clipped = master_meter[1].clipped = 0;
            for (int t = 0; t < MAX_TRACKS; t++) track_meter[t].clipped = 0;
            lpress = 0;
        }
    }
    y += 46;
    y = loudness_readout(x, y, w);              /* LUFS, as streaming services measure it */
    y += 12;
    text(x, y, "TRACKS", DIM);
    y += font_h + 6;
    for (int t = 0; t < P.ntracks; t++) {
        fill((SDL_Rect){x, y + 3, 4, 10}, TRACK_COL[t]);
        text_clip((SDL_Rect){x + 8, y, 110, font_h + 2}, x + 10, y, P.tracks[t].name, TEXT);
        meter_draw((SDL_Rect){x + 124, y + 2, w - 124, 12}, &track_meter[t], 0);
        y += 18;
    }
    y += 10;
    text(x, y, "Red at the right of a meter means it went over 0 dB.", DIM);
    text(x, y + font_h + 2, "Click a master meter to clear the clip lights.", DIM);
    y += 2 * font_h + 14;
    }
    return y;
}

int vis_scroll, vis_content_h;

void vis_pane(void)
{
    SDL_Rect pane = {L.pane_x, L.top, L.pane_w, H - L.top - L.status};
    fill(pane, PANEL);
    line(pane.x + pane.w - 1, pane.y, pane.x + pane.w - 1, pane.y + pane.h, LINE);
    int inside = hit(pane), saved_press = lpress;
    if (!inside) lpress = 0;
    int max_scroll = vis_content_h - pane.h > 0 ? vis_content_h - pane.h : 0;
    vis_scroll = vis_scroll < 0 ? 0 : vis_scroll > max_scroll ? max_scroll : vis_scroll;
    SDL_RenderSetClipRect(ren, &pane);
    int y0 = pane.y + 12 - vis_scroll;
    int y = vis_content(pane.x + 14, y0, pane.w - 28);
    vis_content_h = y - y0 + 12;
    scrollbar(pane, &vis_scroll, vis_content_h);
    SDL_RenderSetClipRect(ren, NULL);
    if (!inside) lpress = saved_press;
}

/* ---- the full-screen show: metaballs, one per sounding track, in its colour, orbiting in time
 * with the song; they swell on the kick, a spectrum ring pulses around them, beats send out
 * ripples. Rendered at low resolution into a texture and smoothly scaled up. */

SDL_Texture *show_tex;

int show_w, show_h;
Uint32 *show_px;
float blob_size[MAX_TRACKS], show_phase, ripple_t[6];
int ripple_n;
double last_beat = -1;

void show_blobs(void)
{
    int tw = 640, th = 640 * H / (W > 0 ? W : 1);    /* twice the old resolution */
    if (th < 180) th = 180;
    if (!show_tex || tw != show_w || th != show_h) {
        if (show_tex) SDL_DestroyTexture(show_tex);
        free(show_px);
        show_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, tw, th);
        SDL_SetTextureScaleMode(show_tex, SDL_ScaleModeLinear);
        show_px = malloc((size_t)tw * th * sizeof(Uint32));
        show_w = tw;
        show_h = th;
        if (!show_tex || !show_px) return;
    }
    float dt = 1 / 60.0f;
    /* the song's position: blobs go round once every two bars while it plays */
    double beats = (double)P.playhead / SR * P.bpm / 60.0;
    if (P.playing) {
        show_phase = (float)(beats / 8.0 * 2 * VIS_PI);
        if (floor(beats) != floor(last_beat) && ripple_n < 6) ripple_t[ripple_n++] = 0;   /* each beat */
        last_beat = beats;
    } else {
        show_phase += dt * 0.15f;
    }
    float bf = P.playing ? (float)(beats - floor(beats)) : 1;
    float pulse = fmaxf(beat_pulse, P.playing ? expf(-bf * 7) * fminf(1, low_energy * 3) : 0);

    /* blobs: active tracks in their colours */
    float bx[MAX_TRACKS + 1], by[MAX_TRACKS + 1], bs[MAX_TRACKS + 1], br[MAX_TRACKS + 1], bg[MAX_TRACKS + 1], bb[MAX_TRACKS + 1];
    int n = 0;
    float aspect = (float)tw / th;
    for (int t = 0; t < P.ntracks; t++) {
        float lv = track_meter[t].level;
        float target = lv > 0.002f ? 0.06f + 0.16f * sqrtf(fminf(lv, 1.5f)) : 0;
        blob_size[t] += (target - blob_size[t]) * (target > blob_size[t] ? 0.5f : 0.06f);
        if (blob_size[t] < 0.004f) continue;
        float a = show_phase + t * 2 * (float)VIS_PI / P.ntracks;
        float rad = 0.38f + 0.12f * sinf(show_phase * 2 + t);
        bx[n] = cosf(a) * rad * fminf(aspect, 1.9f);
        by[n] = sinf(a) * rad;
        bs[n] = blob_size[t] * (1 + 0.6f * pulse);
        br[n] = TRACK_COL[t].r / 255.0f;
        bg[n] = TRACK_COL[t].g / 255.0f;
        bb[n] = TRACK_COL[t].b / 255.0f;
        n++;
    }
    /* a core in the middle that breathes with the bass */
    bx[n] = 0;
    by[n] = 0;
    bs[n] = 0.05f + 0.12f * fminf(1, low_energy) + 0.06f * pulse;
    br[n] = 1.0f;
    bg[n] = 0.82f;
    bb[n] = 0.45f;
    n++;

    float sparkle = fminf(1, high_energy);
    for (int py = 0; py < th; py++) {
        float y = 1 - 2.0f * py / th;
        for (int px = 0; px < tw; px++) {
            float x = (2.0f * px / tw - 1) * aspect;
            float field = 0, cr = 0, cg = 0, cb = 0;
            for (int i = 0; i < n; i++) {
                float dx = x - bx[i], dy = y - by[i];
                float f = bs[i] * bs[i] / (dx * dx + dy * dy + 1e-4f);
                field += f;
                cr += f * br[i];
                cg += f * bg[i];
                cb += f * bb[i];
            }
            float inv = field > 0 ? 1 / field : 0;
            float inside = field < 0.9f ? 0 : field > 1.1f ? 1 : (field - 0.9f) * 5;
            inside = inside * inside * (3 - 2 * inside);
            float core = fminf(1, (field - 1) * 0.15f);
            core = core < 0 ? 0 : core;
            float glow = fminf(field, 1) * fminf(field, 1) * fminf(field, 1) * (0.35f + 0.4f * pulse);
            float vign = 1 - 0.55f * (x * x / (aspect * aspect) + y * y);
            float r = (cr * inv) * (inside * (0.75f + 0.5f * core) + glow * (1 - inside)) + 0.03f * vign;
            float g = (cg * inv) * (inside * (0.75f + 0.5f * core) + glow * (1 - inside)) + 0.035f * vign;
            float b = (cb * inv) * (inside * (0.75f + 0.5f * core) + glow * (1 - inside)) + 0.06f * vign;
            if (sparkle > 0.05f && ((px * 7919 + py * 104729 + (int)(SDL_GetTicks() / 70) * 31) % 997) < sparkle * 3) {
                r += 0.6f * sparkle;                 /* hi-hats glitter */
                g += 0.6f * sparkle;
                b += 0.7f * sparkle;
            }
            Uint32 R = r > 1 ? 255 : r < 0 ? 0 : (Uint32)(r * 255);
            Uint32 G = g > 1 ? 255 : g < 0 ? 0 : (Uint32)(g * 255);
            Uint32 B = b > 1 ? 255 : b < 0 ? 0 : (Uint32)(b * 255);
            show_px[py * tw + px] = 0xFF000000u | R << 16 | G << 8 | B;
        }
    }
    SDL_UpdateTexture(show_tex, NULL, show_px, tw * (int)sizeof(Uint32));
    SDL_RenderCopy(ren, show_tex, NULL, NULL);

    /* the spectrum as a ring of bars around the middle */
    int cx = W / 2, cy = H / 2, r0 = (int)(H * (0.30f + 0.04f * pulse));
    double bin_hz = (double)SR / VIS_FFT;
    for (int i = 0; i < 96; i++) {
        double f0 = 40 * pow(400.0, i / 96.0), f1 = 40 * pow(400.0, (i + 1) / 96.0);
        float best = -140;
        for (int k = (int)(f0 / bin_hz); k <= (int)(f1 / bin_hz) && k < VIS_FFT / 2; k++) best = fmaxf(best, vis_smooth[k]);
        float t = (best + 80) / 80;
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        float a = (float)(i / 96.0 * 2 * VIS_PI) - (float)VIS_PI / 2 + show_phase * 0.25f;
        Col c = TRACK_COL[i * MAX_TRACKS / 96];
        int len = 4 + (int)(t * H * 0.16f);
        for (int side = -1; side <= 1; side += 2) {   /* mirrored left and right */
            float aa = side < 0 ? a : (float)VIS_PI - a;
            int x0 = cx + (int)(cosf(aa) * r0), y0 = cy + (int)(sinf(aa) * r0);
            int x1 = cx + (int)(cosf(aa) * (r0 + len)), y1 = cy + (int)(sinf(aa) * (r0 + len));
            for (int wdt = -1; wdt <= 1; wdt++) line(x0 + wdt, y0, x1 + wdt, y1, alpha(c, (Uint8)(90 + 150 * t)));
        }
    }
    /* beat ripples */
    for (int i = 0; i < ripple_n; i++) {
        ripple_t[i] += dt;
        if (ripple_t[i] > 1.2f) {
            ripple_t[i--] = ripple_t[--ripple_n];
            continue;
        }
        int rr = r0 + (int)(ripple_t[i] * H * 0.5f);
        ring(cx, cy, rr, alpha(HEAD, (Uint8)(120 * (1 - ripple_t[i] / 1.2f))));
    }
    show_overlay("Blobs", "a glowing blob for every track that's playing");
}

/* text with a dark backing, readable over any scene */
void show_text(int x, int y, const char *s, Col c)
{
    fill((SDL_Rect){x - 6, y - 2, text_w(s) + 12, font_h + 4}, (Col){0, 0, 0, 120});
    text(x, y, s, c);
}

/* what's playing, which scene, and the keys: on top of every scene */
void show_overlay(const char *name, const char *caption)
{
    char buf[200];
    double secs = (double)P.playhead / SR;
    char pos[48];
    pos_text(P.playhead, pos, sizeof pos);
    snprintf(buf, sizeof buf, "%.60s   %s   %.0f BPM   %02d:%02d", proj_path[0] ? base_name(proj_path) : "tinydaw", pos,
             meter_bpm_at(&P, P.playhead), (int)(secs / 60), (int)fmod(secs, 60.0));
    show_text(20, H - font_h - 16, buf, rgb(230, 230, 236));
    snprintf(buf, sizeof buf, "%d/%d  %s", show_scene + 1, scene_count() + 1, name);
    show_text(20, 16, buf, rgb(255, 214, 90));
    if (caption && caption[0]) show_text(20, 20 + font_h, caption, rgb(170, 175, 190));
    const char *hint = show_cycle ? "Left/Right scene  ·  Tab: stop cycling  ·  Space play  ·  Esc leave"
                                  : "Left/Right scene  ·  Tab: cycle scenes  ·  Space play  ·  Esc leave";
    show_text(W - text_w(hint) - 20, H - font_h - 16, hint, rgb(170, 175, 190));
}

/* ---- the other scenes (scenes.c), with cycling, crossfades and the music driving them */

SDL_Texture *scene_tex;

int scene_tw, scene_th;
float *scene_buf, *scene_buf2;
size_t scene_cap;
Uint32 *scene_px;
double show_switch_at, show_wall;
int show_fade_from = -1;
double show_fade_left;
Uint32 show_last_tick;

const char *show_name(int k) { return k == 0 ? "Blobs" : scene_name(k - 1); }

void show_go(int k)               /* to scene k, crossfading from the one before */
{
    int n = scene_count() + 1;
    k = ((k % n) + n) % n;
    if (k == show_scene) return;
    show_fade_from = show_scene != 0 && k != 0 ? show_scene : -1;   /* the blobs draw differently: cut */
    show_fade_left = SHOW_FADE;
    show_scene = k;
    show_started[k] = show_clock;            /* each visit draws itself afresh */
    if (k > 0) scene_reset(k - 1);
    show_switch_at = show_wall + show_each;
    settings_save();
}

void show_draw(void)
{
    Uint32 now = SDL_GetTicks();
    double dt = show_last_tick ? (now - show_last_tick) / 1000.0 : 1 / 60.0;
    if (dt > 0.25) dt = 0.25;
    show_last_tick = now;
    float lv = fminf(1.0f, master_meter[0].level), bass = fminf(1.5f, low_energy);
    double beats = (double)P.playhead / SR * P.bpm / 60.0;
    float bf = P.playing ? (float)(beats - floor(beats)) : 1;
    float beat = fmaxf(beat_pulse, P.playing ? expf(-bf * 7) * fminf(1, low_energy * 3) : 0);
    show_clock += dt * (1 + 0.9 * lv);       /* louder music draws faster */
    show_wall += dt;
    show_fade_left = fmax(0, show_fade_left - dt);
    if (show_cycle && show_wall >= show_switch_at) show_go(show_scene + 1);
    if (show_scene == 0) {
        show_blobs();
        return;
    }
    int si = show_scene - 1, rw = scene_width(si, W), rh = rw * H / (W > 0 ? W : 1);
    if (rh < 90) rh = 90;
    size_t need = (size_t)rw * rh * 3;
    if (need > scene_cap) {
        free(scene_buf);
        free(scene_buf2);
        free(scene_px);
        scene_buf = malloc(need * sizeof(float));
        scene_buf2 = malloc(need * sizeof(float));
        scene_px = malloc(need / 3 * sizeof(Uint32));
        scene_cap = scene_buf && scene_buf2 && scene_px ? need : 0;
        if (!scene_cap) return;
    }
    if (!scene_tex || rw != scene_tw || rh != scene_th) {
        if (scene_tex) SDL_DestroyTexture(scene_tex);
        scene_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, rw, rh);
        if (!scene_tex) return;
        SDL_SetTextureScaleMode(scene_tex, SDL_ScaleModeLinear);
        scene_tw = rw;
        scene_th = rh;
    }
    float zoom = 1 + 0.06f * bass + 0.05f * beat;            /* the bass breathes, beats pulse */
    float gain = 0.9f + 0.35f * lv + 0.4f * beat;
    char caption[96];
    scene_render(si, show_clock - show_started[show_scene], dt, zoom, scene_buf, rw, rh, caption, sizeof caption);
    if (show_fade_from > 0 && show_fade_left > 0) {           /* crossfade from the last scene */
        char dummy[96];
        scene_render(show_fade_from - 1, show_clock - show_started[show_fade_from], dt, zoom, scene_buf2, rw, rh, dummy,
                     sizeof dummy);
        float k = (float)(1 - show_fade_left / SHOW_FADE);
        k = k * k * (3 - 2 * k);
        for (size_t i = 0; i < need; i++) scene_buf[i] = scene_buf2[i] + (scene_buf[i] - scene_buf2[i]) * k;
    }
    for (size_t i = 0; i < need / 3; i++) {
        float r = scene_buf[3 * i] * gain, g = scene_buf[3 * i + 1] * gain, b = scene_buf[3 * i + 2] * gain;
        Uint32 R = r >= 1 ? 255 : r <= 0 ? 0 : (Uint32)(r * 255), G = g >= 1 ? 255 : g <= 0 ? 0 : (Uint32)(g * 255),
               B = b >= 1 ? 255 : b <= 0 ? 0 : (Uint32)(b * 255);
        scene_px[i] = 0xFF000000u | R << 16 | G << 8 | B;
    }
    SDL_UpdateTexture(scene_tex, NULL, scene_px, rw * (int)sizeof(Uint32));
    SDL_RenderCopy(ren, scene_tex, NULL, NULL);
    char cap2[200];
    snprintf(cap2, sizeof cap2, caption[0] ? "%s: %s" : "%s", scene_caption(si), caption);
    show_overlay(scene_name(si), cap2);
}

void show_toggle(void)
{
    show_open = !show_open;
    if (show_open) {                         /* start the current scene afresh, cycling from now */
        show_last_tick = 0;
        show_started[show_scene] = show_clock;
        show_switch_at = show_wall + show_each;
        show_fade_left = 0;
        if (show_scene > 0) scene_reset(show_scene - 1);
    }
    if (!headless) SDL_SetWindowFullscreen(win, show_open ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}
