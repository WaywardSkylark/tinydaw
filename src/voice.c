/* tinydaw: the Singer. Words sung on notes.
 *
 * Each syllable is spoken by eSpeak NG, a free speech synthesizer that tinydaw runs as a separate
 * program (so it's never built into tinydaw). The speech is analysed into the loudness of 28 bands
 * over time, plus how hissy (unvoiced) each moment is. That is then played back through the same
 * bands on a new sound: a buzz at the note's pitch for the voiced parts and noise for the "s" and
 * "t" sounds. This is a vocoder: the words come out in tune, held as long as the note, with the
 * character of the buzz (robot, talkbox, choir or whisper).
 *
 * Syllables are stretched at their vowel, so consonants stay crisp when a note is held. Speech is
 * cached per syllable, so changing the tune or the voice doesn't run eSpeak again. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "daw.h"
#include "compat.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

#define PI 3.14159265358979323846

const char *const VOCAL_STYLES[VS_STYLES] = {"Robot", "Talkbox", "Choir", "Whisper", "Clear"};
const ClearSinger CLEAR_SINGERS[] = {
    {"Aria", "high, bright and clear", "f5", 1},
    {"Lumi", "high, soft and sweet", "Annie", 1},
    {"Kai", "low, warm and smooth", "klatt4", 0},
    {"Ren", "low, steady and plain", "m3", 0},
};
const int NCLEAR_SINGERS = (int)(sizeof CLEAR_SINGERS / sizeof CLEAR_SINGERS[0]);
const char *const VOCAL_LANGS[][2] = {
    {"English (US)", "en-us"}, {"English (UK)", "en-gb"}, {"Spanish", "es"}, {"French", "fr"},
    {"German", "de"}, {"Italian", "it"}, {"Portuguese", "pt-br"}, {"Dutch", "nl"}, {"Japanese", "ja"},
    {"Hindi", "hi"}, {"Swahili", "sw"}, {"Indonesian", "id"},
};
const int NVOCAL_LANGS = (int)(sizeof VOCAL_LANGS / sizeof VOCAL_LANGS[0]);

/* ---------------------------------------------------------------- finding and running eSpeak NG */

#ifdef _WIN32
static wchar_t espeak_exe[1024];

static int try_exe(const wchar_t *path)
{
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) return 0;
    wcsncpy(espeak_exe, path, 1023);
    return 1;
}

static int find_espeak(void)
{
    if (espeak_exe[0]) return 1;
    wchar_t buf[1024];
    const wchar_t *env = _wgetenv(L"TINYDAW_ESPEAK");
    if (env && try_exe(env)) return 1;
    DWORD n = GetModuleFileNameW(NULL, buf, 1000);   /* next to tinydaw.exe */
    if (n > 0 && n < 1000) {
        wchar_t *slash = wcsrchr(buf, L'\\');
        if (slash) {
            wcscpy(slash + 1, L"espeak-ng\\bin\\espeak-ng.exe");   /* the copy that comes with tinydaw */
            if (try_exe(buf)) return 1;
            wcscpy(slash + 1, L"espeak-ng\\espeak-ng.exe");
            if (try_exe(buf)) return 1;
        }
    }
    if (SearchPathW(NULL, L"espeak-ng.exe", NULL, 1024, buf, NULL) && try_exe(buf)) return 1;
    const wchar_t *dirs[] = {L"ProgramFiles", L"ProgramFiles(x86)", L"ProgramW6432"};
    for (int i = 0; i < 3; i++) {
        const wchar_t *pf = _wgetenv(dirs[i]);
        if (!pf) continue;
        _snwprintf(buf, 1024, L"%ls\\eSpeak NG\\espeak-ng.exe", pf);
        if (try_exe(buf)) return 1;
    }
    return 0;
}

int speech_available(char *where, size_t n)
{
    int ok = find_espeak();
    if (where && n) {
        where[0] = 0;
        if (ok) WideCharToMultiByte(CP_UTF8, 0, espeak_exe, -1, where, (int)n, NULL, NULL);
    }
    return ok;
}

static FILE *open_u8(const wchar_t *path, const wchar_t *mode) { return _wfopen(path, mode); }

/* Runs eSpeak NG on a text file into a WAV file, without a console window flashing up. */
static int run_espeak(const char *voice, int speed, int pitch, const wchar_t *txt, const wchar_t *wav)
{
    wchar_t cmd[3300], v[64], pt[32] = L"";
    MultiByteToWideChar(CP_UTF8, 0, voice, -1, v, 64);
    if (pitch >= 0) _snwprintf(pt, 32, L" -p %d -P 0", pitch);   /* one steady pitch */
    _snwprintf(cmd, 3300, L"\"%ls\" -v %ls -s %d%ls -z -b 1 -w \"%ls\" -f \"%ls\"", espeak_exe, v, speed, pt, wav, txt);
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    if (!CreateProcessW(espeak_exe, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return -1;
    WaitForSingleObject(pi.hProcess, 20000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code == 0 ? 0 : -1;
}
#else
static char espeak_exe[1200];

static int try_exe(const char *path)
{
    if (access(path, X_OK) != 0) return 0;
    snprintf(espeak_exe, sizeof espeak_exe, "%s", path);
    return 1;
}

static int find_espeak(void)
{
    if (espeak_exe[0] && access(espeak_exe, X_OK) == 0) return 1;
    const char *env = getenv("TINYDAW_ESPEAK");
    if (env && try_exe(env)) return 1;
    const char *path = getenv("PATH");
    char buf[1200];
    if (!path) path = "/usr/local/bin:/usr/bin:/bin";
    const char *names[] = {"espeak-ng", "espeak"};
    for (int k = 0; k < 2; k++) {
        for (const char *p = path; *p;) {
            size_t n = strcspn(p, ":");
            snprintf(buf, sizeof buf, "%.*s/%s", (int)n, p, names[k]);
            if (try_exe(buf)) return 1;
            p += n + (p[n] == ':');
        }
    }
    return 0;
}

int speech_available(char *where, size_t n)
{
    int ok = find_espeak();
    if (where && n) snprintf(where, n, "%s", ok ? espeak_exe : "");
    return ok;
}

static FILE *open_u8(const char *path, const char *mode) { return fopen(path, mode); }

static int run_espeak(const char *voice, int speed, int pitch, const char *txt, const char *wav)
{
    char sp[16], pt[16];
    snprintf(sp, sizeof sp, "%d", speed);
    snprintf(pt, sizeof pt, "%d", pitch);
    char *argv[] = {espeak_exe, "-v", (char *)voice, "-s", sp, "-z", "-b", "1", "-w", (char *)wav, "-f", (char *)txt,
                    "-p", pt, "-P", "0", NULL};   /* with a pitch: one steady pitch */
    if (pitch < 0) argv[12] = NULL;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    pid_t pid;
    int rc = posix_spawn(&pid, espeak_exe, &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) return -1;
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}
#endif

/* Reads the 16-bit PCM WAV eSpeak writes: mono samples as floats, and the rate. */
static float *read_wav(FILE *f, int *rate, long *n)
{
    unsigned char h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) return NULL;
    int channels = 1, bits = 16;
    *rate = 22050;
    for (;;) {
        unsigned char c[8];
        if (fread(c, 1, 8, f) != 8) return NULL;
        uint32_t len = c[4] | c[5] << 8 | c[6] << 16 | (uint32_t)c[7] << 24;
        if (!memcmp(c, "fmt ", 4)) {
            unsigned char fm[16];
            if (len < 16 || fread(fm, 1, 16, f) != 16) return NULL;
            channels = fm[2] | fm[3] << 8;
            *rate = (int)(fm[4] | fm[5] << 8 | fm[6] << 16 | (uint32_t)fm[7] << 24);
            bits = fm[14] | fm[15] << 8;
            if (len > 16) fseek(f, (long)(len - 16 + (len & 1)), SEEK_CUR);
        } else if (!memcmp(c, "data", 4)) {
            if (bits != 16 || channels < 1 || *rate < 4000) return NULL;
            if (len == 0 || len == 0xFFFFFFFFu) {     /* written to the end */
                long here = ftell(f);
                fseek(f, 0, SEEK_END);
                len = (uint32_t)(ftell(f) - here);
                fseek(f, here, SEEK_SET);
            }
            long frames = (long)(len / 2 / (uint32_t)channels);
            float *out = malloc((size_t)(frames > 0 ? frames : 1) * sizeof(float));
            int16_t *raw = malloc((size_t)(frames > 0 ? frames : 1) * (size_t)channels * 2);
            if (!out || !raw) {
                free(out);
                free(raw);
                return NULL;
            }
            frames = (long)fread(raw, (size_t)channels * 2, (size_t)frames, f);
            for (long i = 0; i < frames; i++) {
                const unsigned char *b = (const unsigned char *)&raw[i * channels];
                out[i] = (int16_t)(b[0] | b[1] << 8) / 32768.0f;   /* WAV is little-endian */
            }
            free(raw);
            *n = frames;
            return out;
        } else {
            fseek(f, (long)(len + (len & 1)), SEEK_CUR);
        }
    }
}

/* One piece of text spoken, as mono floats at *rate. Cached, since the same syllables come back
 * again and again while a line is being tried out. */
typedef struct {
    char text[48], voice[24];
    int pitch;
    float *data;
    long n;
    int rate;
    unsigned used;
} Spoken;
static Spoken cache[160];
static unsigned cache_clock;

static const Spoken *speak(const char *text, const char *voice, int pitch, char *err, size_t errlen)
{
    for (int i = 0; i < 160; i++)
        if (cache[i].data && !strcmp(cache[i].text, text) && !strcmp(cache[i].voice, voice) &&
            cache[i].pitch == pitch) {
            cache[i].used = ++cache_clock;
            return &cache[i];
        }
    if (!find_espeak()) {
        snprintf(err, errlen, "singing needs eSpeak NG, which isn't installed");
        return NULL;
    }
    static unsigned serial;
    serial++;
#ifdef _WIN32
    wchar_t dir[600], txt[700], wav[700];
    if (!GetTempPathW(600, dir)) wcscpy(dir, L".\\");
    _snwprintf(txt, 700, L"%lstinydaw-say-%lu-%u.txt", dir, (unsigned long)GetCurrentProcessId(), serial);
    _snwprintf(wav, 700, L"%lstinydaw-say-%lu-%u.wav", dir, (unsigned long)GetCurrentProcessId(), serial);
    FILE *t = open_u8(txt, L"wb");
#else
    const char *dir = getenv("TMPDIR");
    char txt[700], wav[700];
    if (!dir || !dir[0]) dir = "/tmp";
    snprintf(txt, sizeof txt, "%s/tinydaw-say-%d-%u.txt", dir, (int)getpid(), serial);
    snprintf(wav, sizeof wav, "%s/tinydaw-say-%d-%u.wav", dir, (int)getpid(), serial);
    FILE *t = open_u8(txt, "wb");
#endif
    if (!t) {
        snprintf(err, errlen, "can't write a temporary file");
        return NULL;
    }
    fputs(text, t);
    fclose(t);
    int rc = run_espeak(voice, 150, pitch, txt, wav);
    float *data = NULL;
    long n = 0;
    int rate = 22050;
    if (rc == 0) {
#ifdef _WIN32
        FILE *w = open_u8(wav, L"rb");
#else
        FILE *w = open_u8(wav, "rb");
#endif
        if (w) {
            data = read_wav(w, &rate, &n);
            fclose(w);
        }
    }
#ifdef _WIN32
    _wremove(txt);
    _wremove(wav);
#else
    remove(txt);
    remove(wav);
#endif
    if (!data || n < 64) {
        free(data);
        snprintf(err, errlen, "eSpeak NG couldn't say \"%s\"", text);
        return NULL;
    }
    float pk = 0;                                /* trim the silence around it */
    for (long i = 0; i < n; i++) pk = fmaxf(pk, fabsf(data[i]));
    long a = 0, b = n;
    while (a < n && fabsf(data[a]) < pk * 0.02f) a++;
    while (b > a && fabsf(data[b - 1]) < pk * 0.02f) b--;
    if (b - a < 64) {
        a = 0;
        b = n;
    }
    memmove(data, data + a, (size_t)(b - a) * sizeof(float));
    n = b - a;
    int slot = 0;                                /* the least recently used slot */
    for (int i = 1; i < 160; i++)
        if (cache[i].used < cache[slot].used) slot = i;
    free(cache[slot].data);
    Spoken *s = &cache[slot];
    snprintf(s->text, sizeof s->text, "%.47s", text);
    snprintf(s->voice, sizeof s->voice, "%.23s", voice);
    s->pitch = pitch;
    s->data = data;
    s->n = n;
    s->rate = rate;
    s->used = ++cache_clock;
    return s;
}

/* ---------------------------------------------------------------- words into syllables */

int vocal_syllables(const char *words, char syl[][40], int *rest_before, int max)
{
    int n = 0, rest = 0;
    size_t k = 0;
    char cur[40];
    for (const char *p = words;; p++) {
        unsigned char c = (unsigned char)*p;
        int keep = c >= 0x80 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                   c == '\'';
        if (keep) {
            if (k < sizeof cur - 1) cur[k++] = (char)c;
            continue;
        }
        if (k > 0) {                             /* a syllable ends */
            cur[k] = 0;
            if (n < max) {
                snprintf(syl[n], 40, "%s", cur);
                if (rest_before) rest_before[n] = rest;
                n++;
            }
            k = 0;
            rest = 0;
        }
        if (c == ',' || c == ';' || c == ':') rest = rest > 1 ? rest : 1;     /* a short breath */
        if (c == '.' || c == '!' || c == '?' || c == '\n') rest = 2;           /* the end of a line */
        if (!c) break;
    }
    return n;
}

/* ---------------------------------------------------------------- the vocoder */

#define NB 28
#define HOP_S 0.002                              /* analysis frames every 2 ms */

typedef struct {
    float *env;                                  /* frames * NB band levels */
    float *uv;                                   /* 0 voiced .. 1 hiss, per frame */
    long frames;
    long v0, v1;                                 /* the vowel: where to stretch */
} Analysis;

static double band_hz(int b) { return 100.0 * pow(75.0, (double)b / (NB - 1)); }   /* 100 Hz .. 7.5 kHz */

typedef struct { double a1, a2, a3, k, s1, s2; } Bp;

static void bp_set(Bp *f, double hz, double k, int rate)
{
    if (hz > rate * 0.45) hz = rate * 0.45;
    double g = tan(PI * hz / rate);
    f->k = k;
    f->a1 = 1 / (1 + g * (g + k));
    f->a2 = g * f->a1;
    f->a3 = g * f->a2;
    f->s1 = f->s2 = 0;
}

static inline double bp_run(Bp *f, double x)
{
    double v3 = x - f->s2, v1 = f->a1 * f->s1 + f->a2 * v3, v2 = f->s2 + f->a2 * f->s1 + f->a3 * v3;
    f->s1 = 2 * v1 - f->s1;
    f->s2 = 2 * v2 - f->s2;
    return v1 * f->k;
}

static const double BAND_K = 0.2;                /* about a third of an octave wide */

static int analyse(const Spoken *s, Analysis *a)
{
    int hop = (int)(s->rate * HOP_S);
    a->frames = s->n / hop + 1;
    a->env = calloc((size_t)a->frames * NB, sizeof(float));
    a->uv = calloc((size_t)a->frames, sizeof(float));
    if (!a->env || !a->uv) return -1;
    Bp f[NB];
    double lp[NB] = {0}, kl = exp(-2 * PI * 50.0 / s->rate), prev = 0;
    for (int b = 0; b < NB; b++) bp_set(&f[b], band_hz(b), BAND_K, s->rate);
    for (long i = 0; i < s->n; i++) {
        double x = s->data[i] - 0.9 * prev;      /* pre-emphasis: the upper bands count too */
        prev = s->data[i];
        for (int b = 0; b < NB; b++) {
            double y = bp_run(&f[b], x);
            lp[b] = y * y + (lp[b] - y * y) * kl;
        }
        if (i % hop == 0) {
            long fr = i / hop;
            double hi = 0, all = 1e-12;
            for (int b = 0; b < NB; b++) {
                double e = sqrt(lp[b]);
                a->env[fr * NB + b] = (float)e;
                all += e * e;
                if (band_hz(b) > 3000) hi += e * e;
            }
            double r = hi / all;                 /* hiss is mostly up high */
            a->uv[fr] = (float)fmin(1, fmax(0, (r - 0.3) / 0.35));
        }
    }
    double top = 0;                              /* the vowel: voiced and loud */
    for (long fr = 0; fr < a->frames; fr++) {
        double e = 0;
        for (int b = 0; b < NB; b++) e += a->env[fr * NB + b];
        top = fmax(top, e);
    }
    a->v0 = -1;
    a->v1 = -1;
    for (long fr = 0; fr < a->frames; fr++) {
        double e = 0;
        for (int b = 0; b < NB; b++) e += a->env[fr * NB + b];
        if (a->uv[fr] < 0.4 && e > top * 0.35) {
            if (a->v0 < 0) a->v0 = fr;
            a->v1 = fr;
        }
    }
    if (a->v0 < 0 || a->v1 - a->v0 < 4) {        /* no clear vowel: stretch the middle */
        a->v0 = a->frames / 3;
        a->v1 = a->frames * 2 / 3;
    }
    return 0;
}

static double white(uint32_t *r)
{
    *r ^= *r << 13;
    *r ^= *r >> 17;
    *r ^= *r << 5;
    return *r / 2147483647.5 - 1.0;
}

/* Sings one syllable into out (stereo, at the project rate) from frame `at`, `len` frames long. */
static void sing(const Analysis *a, const VocalSpec *v, int note, int prev_note, long at, long len, float *out,
                 long out_frames, uint32_t *rng)
{
    double base = (v->a4 > 0 ? v->a4 : 440.0);
    double f_to = base * pow(2.0, (note - 69) / 12.0), f_from = prev_note > 0 ? base * pow(2.0, (prev_note - 69) / 12.0) : f_to;
    double fshift = v->high ? 1.1 : 0.95;        /* the vocal tract: a little smaller or bigger */
    int stereo = v->style == VS_CHOIR;
    int nv = stereo ? 4 : 1;
    Bp fl[NB], fr[NB];
    double comp[NB];
    for (int b = 0; b < NB; b++) {
        bp_set(&fl[b], band_hz(b) * fshift, BAND_K, SR);
        bp_set(&fr[b], band_hz(b) * fshift, BAND_K, SR);
        comp[b] = pow(band_hz(b) / 200.0, 0.5);  /* a buzz has less up high than speech: even it out */
    }
    double ph[4] = {0, 0.31, 0.57, 0.83}, det[4] = {-9, -3, 4, 10}, pan[4] = {-0.8, 0.3, -0.3, 0.8};
    double src_len = (double)a->frames, onset = (double)a->v0, coda = src_len - a->v1;
    double t_len = len / (double)SR / HOP_S;     /* in analysis frames */
    int stretch = onset + coda < t_len * 0.8;
    long fade = SR / 125;                        /* 8 ms */
    double vib = v->vibrato * 0.6;               /* semitones at full */
    for (long i = 0; i < len && at + i < out_frames; i++) {
        if (at + i < 0) continue;
        double t = (double)i / SR, tf = t / HOP_S, src;
        if (stretch) {                           /* consonants at speed, the vowel held */
            double mid = t_len - onset - coda;
            src = tf < onset ? tf : tf < onset + mid ? onset + (tf - onset) * (a->v1 - a->v0) / mid
                                                     : a->v1 + (tf - onset - mid);
        } else {
            src = tf * src_len / t_len;
        }
        if (src > src_len - 1.001) src = src_len - 1.001;
        long s0 = (long)src;
        double fx = src - s0;
        double uv = a->uv[s0] * (1 - fx) + a->uv[s0 + 1 < a->frames ? s0 + 1 : s0] * fx;

        double glide = fmin(1, t / 0.04);
        double semis = 12 * log2(f_from / f_to) * (1 - glide);
        if (t > 0.2) semis += vib * sin(2 * PI * 5.5 * (t - 0.2)) * fmin(1, (t - 0.2) / 0.3);
        double f = f_to * pow(2.0, semis / 12.0);
        double cl = 0, cr = 0;
        for (int k = 0; k < nv; k++) {           /* the buzz */
            double dt = f * pow(2.0, det[k] * (stereo ? 1 : 0) / 1200.0) / SR;
            double p = ph[k], x;
            switch (v->style) {
            case VS_TALKBOX: x = (2 * p - 1) * 0.6 + (p < 0.25 ? 1 : -1) * 0.6; break;
            case VS_WHISPER: x = 0; break;
            default: x = 2 * p - 1;
            }
            ph[k] += dt;
            ph[k] -= floor(ph[k]);
            double g = stereo ? 0.5 : 1;
            cl += x * g * sqrt(0.5 * (1 - (stereo ? pan[k] : 0)));
            cr += x * g * sqrt(0.5 * (1 + (stereo ? pan[k] : 0)));
        }
        double hiss = white(rng);
        double voiced = v->style == VS_WHISPER ? 0 : 1 - uv, unv = v->style == VS_WHISPER ? 1.2 : uv * 1.6;
        double xl = cl * voiced + hiss * unv * 0.7, xr = cr * voiced + hiss * unv * 0.7;
        double yl = 0, yr = 0;
        const float *e0 = &a->env[s0 * NB], *e1 = &a->env[(s0 + 1 < a->frames ? s0 + 1 : s0) * NB];
        for (int b = 0; b < NB; b++) {
            double e = (e0[b] * (1 - fx) + e1[b] * fx) * comp[b];
            yl += bp_run(&fl[b], xl) * e;
            if (stereo) yr += bp_run(&fr[b], xr) * e;
        }
        if (!stereo) yr = yl;
        if (v->style == VS_TALKBOX) {
            yl = tanh(yl * 3) / 3;
            yr = tanh(yr * 3) / 3;
        }
        double env = 1;
        if (i < fade) env = (double)i / fade;
        if (len - i < fade) env = fmin(env, (double)(len - i) / fade);
        out[2 * (at + i)] += (float)(yl * env);
        out[2 * (at + i) + 1] += (float)(yr * env);
    }
}


/* ---------------------------------------------------------------- the clear voice (PSOLA)
 *
 * eSpeak sings the syllable on one steady pitch close to the note. Its voiced part is cut into
 * grains, one pitch period each around every glottal pulse, and the grains are laid down again
 * one period of the *note* apart: the pitch moves but the vowel's formants (its sound) don't.
 * Holding a note repeats grains from the vowel. Consonants are copied as they are. */

static double autocorr(const float *x, long n, long at, int win, int lag)
{
    if (at < 0 || at + win + lag >= n) return 0;
    double c = 0, e1 = 0, e2 = 0;
    for (int i = 0; i < win; i++) {
        c += x[at + i] * x[at + i + lag];
        e1 += x[at + i] * x[at + i];
        e2 += x[at + i + lag] * x[at + i + lag];
    }
    return c / sqrt(e1 * e2 + 1e-12);
}

/* The pitch period at `a`: the shortest lag that's nearly as alike as the best one, so a period
 * is never mistaken for two. 0 when there isn't one. */
static int period_at(const float *x, long n, long a, int win, int r, double *strength)
{
    static double c[1024];
    int lo = r / 500, hi = r / 55;
    if (hi > 1022) hi = 1022;
    double best = 0;
    for (int L = lo; L <= hi; L++) {
        c[L] = autocorr(x, n, a, win, L);
        best = fmax(best, c[L]);
    }
    if (strength) *strength = best;
    if (best < 0.3) return 0;
    for (int L = lo + 1; L < hi; L++)
        if (c[L] > 0.9 * best && c[L] >= c[L - 1] && c[L] >= c[L + 1]) return L;
    return 0;
}

/* What pitch each eSpeak -p gives for a singer, measured once (voices differ, and some wrap
 * around near the top), then the -p that comes closest to `hz`. */
static struct { char voice[24]; double hz[20]; } calib[8];

static int pitch_for(const char *voice, double hz)
{
    int k = -1;
    for (int i = 0; i < 8 && k < 0; i++)
        if (!strcmp(calib[i].voice, voice)) k = i;
    if (k < 0) {
        static int next;
        k = next;
        next = (next + 1) % 8;
        snprintf(calib[k].voice, sizeof calib[k].voice, "%.23s", voice);
        char err[100];
        for (int j = 0; j < 20; j++) {
            calib[k].hz[j] = 0;
            const Spoken *s = speak("aah", voice, j * 5, err, sizeof err);
            if (!s) continue;
            int win = s->rate / 40, L = period_at(s->data, s->n, s->n / 2 - win / 2, win, s->rate, NULL);
            calib[k].hz[j] = L ? (double)s->rate / L : 0;
        }
    }
    int best = 50 / 5;
    double bd = 1e9;
    for (int j = 0; j < 20; j++) {               /* nearest in pitch, a little lower rather than higher */
        double h = calib[k].hz[j];
        if (h <= 0) continue;
        double d = fabs(log2(hz / h)) + (h > hz ? 0.05 : 0);
        if (d < bd) {
            bd = d;
            best = j;
        }
    }
    return best * 5;
}

static void sing_clear(const Spoken *s, const VocalSpec *v, int note, int prev_note, long at, long len, float *out,
                       long out_frames)
{
    const float *x = s->data;
    long n = s->n;
    int r = s->rate, win = r / 40, hop = r / 200;
    long nfr = n / hop + 1;
    unsigned char *voiced = calloc((size_t)nfr, 1);
    int *lags = calloc((size_t)nfr, sizeof(int));
    if (!voiced || !lags) {
        free(voiced);
        free(lags);
        return;
    }
    double top = 0;
    for (long i = 0; i < n; i++) top = fmax(top, fabs(x[i]));
    int nl = 0;                                  /* the steady pitch it was sung at */
    for (long f = 0; f < nfr; f++) {
        long a = f * hop - win / 2;
        double rms = 0;
        for (int i = 0; i < win && a + i < n; i++)
            if (a + i >= 0) rms += x[a + i] * x[a + i];
        rms = sqrt(rms / win);
        int best = period_at(x, n, a, win, r, NULL), bc_ok = best > 0;
        double bc = bc_ok ? autocorr(x, n, a, win, best) : 0;
        if (bc > 0.6 && rms > top * 0.04) {
            voiced[f] = 1;
            lags[nl++] = best;
        }
    }
    double T0;
    if (nl) {                                    /* the median period, then refined below */
        for (int i = 1; i < nl; i++)
            for (int j = i; j > 0 && lags[j] < lags[j - 1]; j--) {
                int t = lags[j];
                lags[j] = lags[j - 1];
                lags[j - 1] = t;
            }
        T0 = lags[nl / 2];
        long vm = -1, vcount = 0;                /* refine it to a fraction of a sample, over the */
        for (long f = 0; f < nfr; f++)           /* middle of the voiced part */
            if (voiced[f] && ++vcount) vm = vm < 0 ? f : vm;
        if (vm >= 0) {
            long mid = (vm + vcount / 2) * hop;
            int L = (int)T0, w2 = (int)fmin(4 * win, (double)(n - mid - L - 2));
            if (w2 > 4 * L && mid > 0) {
                double a = autocorr(x, n, mid - w2 / 2, w2, L - 1), b = autocorr(x, n, mid - w2 / 2, w2, L),
                       c = autocorr(x, n, mid - w2 / 2, w2, L + 1);
                double den = a - 2 * b + c;
                if (den < 0) T0 = L + fmax(-0.5, fmin(0.5, 0.5 * (a - c) / den));
            }
        }
    } else {
        T0 = r / 150.0;
    }
    free(lags);
    int H = (int)lround(T0);

    long *marks = malloc((size_t)(n / (H > 0 ? H : 1) + 4) * sizeof(long));   /* the glottal pulses */
    int nm = 0;
    if (marks) {
        for (long i = 0; i < n;) {
            long f = i / hop;
            if (f >= nfr || !voiced[f]) {
                i += hop;
                continue;
            }
            long best = i;
            for (long k = i; k < i + H && k < n; k++)
                if (x[k] > x[best]) best = k;
            /* then exactly a period apart: the pitch is steady, and every grain is cut at the same
             * point in its cycle (peak-picking can hop between two peaks and add a low growl) */
            double m = (double)best;
            while ((long)m < n && (long)m / hop < nfr && voiced[(long)m / hop]) {
                marks[nm++] = (long)lround(m);
                m += T0;
            }
            i = (long)m + hop;
        }
    }

    long v0 = -1, v1 = -1;                       /* the vowel: voiced and loud */
    for (long f = 0; f < nfr; f++)
        if (voiced[f]) {
            if (v0 < 0) v0 = f * hop;
            v1 = f * hop;
        }
    if (v0 < 0 || v1 - v0 < 4 * hop) {
        v0 = n / 3;
        v1 = 2 * n / 3;
    }
    double len_r = (double)len * r / SR, onset = v0, coda = n - v1;
    int stretch = onset + coda < len_r * 0.8;
    double base = v->a4 > 0 ? v->a4 : 440.0;
    double f_to = base * pow(2.0, (note - 69) / 12.0), f_from = prev_note > 0 ? base * pow(2.0, (prev_note - 69) / 12.0) : f_to;
    double vib = v->vibrato * 0.6;

    long ylen = (long)len_r + 2 * H + 4;
    float *y = calloc((size_t)ylen, sizeof(float));
    if (y) {
        for (double t = 0; t < len_r;) {
            double src;
            if (stretch) {
                double mid = len_r - onset - coda;
                src = t < onset ? t : t < onset + mid ? onset + (t - onset) * (v1 - v0) / mid : v1 + (t - onset - mid);
            } else {
                src = t * n / len_r;
            }
            long sf = (long)src / hop;
            int vo = sf < nfr && voiced[sf] && nm > 0;
            long c = (long)src, c2 = c;
            double alpha = 0;
            if (vo) {                            /* the two pulses either side of this point */
                int lo = 0, hi = nm - 1;
                while (lo < hi) {
                    int mid = (lo + hi) / 2;
                    if (marks[mid] <= c) lo = mid + 1;
                    else hi = mid;
                }
                int k1 = lo > 0 ? lo - 1 : 0, k2 = lo;
                c = marks[k1];
                c2 = marks[k2];
                /* blended by how far between them: no pulse is ever repeated or skipped, which
                 * would roughen the sound with a growl an octave down */
                alpha = c2 > c && marks[k2] - marks[k1] < 2 * H ? fmin(1, fmax(0, (src - c) / (double)(c2 - c))) : 0;
                if (alpha == 0) c2 = c;
            }
            long o = (long)t;
            for (int k = -H; k <= H; k++) {      /* a two-period grain, Hann windowed */
                long oi = o + k + H;
                if (oi < 0 || oi >= ylen) continue;
                long s1 = c + k, s2 = c2 + k;
                double a1 = s1 >= 0 && s1 < n ? x[s1] : 0, a2 = s2 >= 0 && s2 < n ? x[s2] : 0;
                double w = 0.5 + 0.5 * cos(PI * k / H);
                y[oi] += (float)((a1 * (1 - alpha) + a2 * alpha) * w);
            }
            double sec = t / r, semis = 12 * log2(f_from / f_to) * (1 - fmin(1, sec / 0.06));
            if (sec > 0.18) semis += vib * sin(2 * PI * 5.5 * (sec - 0.18)) * fmin(1, (sec - 0.18) / 0.3);
            double T1 = r / (f_to * pow(2.0, semis / 12.0));
            t += vo ? T1 : T0;
        }
        long fade = SR / 200;                    /* to the project rate, cubic, with soft ends */
        for (long i = 0; i < len && at + i < out_frames; i++) {
            if (at + i < 0) continue;
            double pos = (double)i * r / SR + H;
            long k = (long)pos;
            double fr = pos - k;
            if (k < 1 || k + 2 >= ylen) continue;
            double p0 = y[k - 1], p1 = y[k], p2 = y[k + 1], p3 = y[k + 2];
            double val = p1 + 0.5 * fr * (p2 - p0 + fr * (2 * p0 - 5 * p1 + 4 * p2 - p3 + fr * (3 * (p1 - p2) + p3 - p0)));
            double env = 1;
            if (i < fade) env = (double)i / fade;
            if (len - i < fade) env = fmin(env, (double)(len - i) / fade);
            out[2 * (at + i)] += (float)(val * env);
            out[2 * (at + i) + 1] += (float)(val * env);
        }
        free(y);
    }
    free(marks);
    free(voiced);
}

float *render_vocal(const VocalSpec *v, long *frames, char *err, size_t errlen)
{
    char syl[VOCAL_NOTES][40];
    int n = vocal_syllables(v->words, syl, NULL, VOCAL_NOTES);
    if (n == 0) {
        snprintf(err, errlen, "no words to sing");
        return NULL;
    }
    if (v->nnotes == 0) {
        snprintf(err, errlen, "no notes to sing on");
        return NULL;
    }
    if (n > v->nnotes) n = v->nnotes;
    double bpm = v->bpm > 0 ? v->bpm : 120, spb = 60.0 / bpm;
    double end = 0;
    for (int i = 0; i < n; i++) end = fmax(end, v->notes[i].start + v->notes[i].len);
    long total = (long)((end * spb + 0.25) * SR);
    float *out = calloc((size_t)total * 2, sizeof(float));
    if (!out) {
        snprintf(err, errlen, "out of memory");
        return NULL;
    }
    int lang = v->lang >= 0 && v->lang < NVOCAL_LANGS ? v->lang : 0;
    char voice[32];
    int clear = v->style == VS_CLEAR;
    const ClearSinger *cs = &CLEAR_SINGERS[v->singer >= 0 && v->singer < NCLEAR_SINGERS ? v->singer : 0];
    if (clear) snprintf(voice, sizeof voice, "%s+%s", VOCAL_LANGS[lang][1], cs->variant);
    else snprintf(voice, sizeof voice, "%s+%s", VOCAL_LANGS[lang][1], v->high ? "f3" : "m3");
    uint32_t rng = 0x1234567u;
    for (int i = 0; i < n; i++) {
        const VocalNote *cn = &v->notes[i];
        double hz = (v->a4 > 0 ? v->a4 : 440.0) * pow(2.0, (cn->note - 69) / 12.0);
        const Spoken *s = speak(syl[i], voice, clear ? pitch_for(voice, hz) : -1, err, errlen);
        if (!s) {
            free(out);
            return NULL;
        }
        if (clear) {
            long cat = (long)(cn->start * spb * SR), clen = (long)(cn->len * spb * SR);
            double gap = cn->start - (v->notes[i - 1 >= 0 ? i - 1 : 0].start + v->notes[i - 1 >= 0 ? i - 1 : 0].len);
            int cprev = i > 0 && gap > -0.01 && gap < 0.15 ? v->notes[i - 1].note : 0;   /* joined notes glide */
            sing_clear(s, v, cn->note, cprev, cat, clen, out, total);
            continue;
        }
        Analysis a;
        memset(&a, 0, sizeof a);
        if (analyse(s, &a) != 0) {
            free(a.env);
            free(a.uv);
            free(out);
            snprintf(err, errlen, "out of memory");
            return NULL;
        }
        const VocalNote *nt = &v->notes[i];
        long at = (long)(nt->start * spb * SR), len = (long)(nt->len * spb * SR);
        int prev = i > 0 && fabs(v->notes[i - 1].start + v->notes[i - 1].len - nt->start) < 0.01 ? v->notes[i - 1].note : 0;
        sing(&a, v, nt->note, prev, at, len, out, total, &rng);
        free(a.env);
        free(a.uv);
    }
    float pk = 0;
    for (long i = 0; i < 2 * total; i++) pk = fmaxf(pk, fabsf(out[i]));
    if (pk > 1e-9f) {
        float g = (float)(0.8 * (v->volume > 0 ? fmin(1, v->volume) : 0.8) / pk);
        for (long i = 0; i < 2 * total; i++) out[i] *= g;
    }
    *frames = total;
    return out;
}

int sample_generate_vocal(Project *p, const VocalSpec *v, const char *name, char *err, size_t errlen)
{
    long n = 0;
    float *d = render_vocal(v, &n, err, errlen);
    if (!d) {                                    /* keep the recipe, as silence of the right length */
        double end = 0;
        for (int i = 0; i < v->nnotes; i++) end = fmax(end, v->notes[i].start + v->notes[i].len);
        n = (long)((end * 60.0 / (v->bpm > 0 ? v->bpm : 120) + 0.25) * SR);
        if (n < 16) n = 16;
        d = calloc((size_t)n * 2, sizeof(float));
        if (!d) return -1;
    }
    int idx = sample_add(p, name, d, n);
    if (idx >= 0) {
        p->samples[idx].vocal = 1;
        p->samples[idx].voc = *v;
    }
    return idx;
}
