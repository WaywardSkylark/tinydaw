/* tinydaw: shared types for the audio engine (audio.c) and the UI (main.c). */
#ifndef DAW_H
#define DAW_H

#include <stddef.h>

#ifndef TINYDAW_VERSION
#define TINYDAW_VERSION "dev"           /* the Makefile passes the VERSION file's contents */
#endif

/* The project sample rate. It can change at runtime (project_set_rate), so it is a
 * variable; SR is kept as a short name for it. */
extern int g_rate;
#define SR g_rate

#define MAX_SAMPLES 256
#define MAX_CLIPS 2048
#define MAX_NOTES 8192
#define MAX_AUTO 4096
#define MAX_TRACKS 16
#define DEFAULT_TRACKS 8
#define DRUM_LANES 6        /* kick, snare, clap, closed hat, open hat, percussion */
#define PEAK_BLOCK 256      /* frames per waveform-overview peak */

enum { WAVE_SINE, WAVE_SQUARE, WAVE_SAW, WAVE_TRIANGLE, WAVE_NOISE,
       WAVE_KICK, WAVE_SNARE, WAVE_HAT, WAVE_COUNT };

typedef struct {
    int wave;
    int note;               /* MIDI note number, 69 = A4 = 440 Hz */
    double beats;           /* length at the project tempo */
    double volume;          /* 0..1 */
    double attack_ms, release_ms;
    double a4;              /* tuning reference in Hz; 0 = 440 */
} GenParams;

/* A drum pattern and the kit that plays it. Lane strings use one character per 16th:
 * 'x' hit, 'X' accent, 'o' ghost (soft), 'r' roll (three quick hits), anything else rest. */
typedef struct {
    char lanes[DRUM_LANES][33];
    int steps;              /* 16 or 32 */
    double swing;           /* 0..0.5: how late the off-beats land */
    int swing8;             /* swing the off-beat 8ths (jazz) instead of the off-beat 16ths */
    double kick_hz, kick_decay, snare_hz, hat_decay, open_decay;
    double volume;
} BeatSpec;

/* The Generator's synth: a small subtractive/FM voice that the genre presets are made from.
 * Zero means "off" for every field, so a preset only names what it uses. */
enum { SW_SAW, SW_SQUARE, SW_TRI, SW_SINE, SW_COUNT };
enum { SF_NONE, SF_LOW, SF_HIGH, SF_BAND };
typedef struct {
    int wave, noosc;        /* main oscillator; noosc = noise only (hats, wind) */
    int wave2;              /* second oscillator, mix2 loud, semi2 semitones away */
    double semi2, mix2;
    int voices;             /* unison copies (1..7), spread detune cents and width (0..1) wide */
    double detune, width;
    double pw;              /* square pulse width (0 = 0.5) */
    double sub;             /* sine an octave down */
    double noise, noise_decay;      /* noise level; its own decay time (s), 0 = follows the envelope */
    int bursts;             /* the noise restarts this many times 11 ms apart (claps) */
    double fm, fm_ratio, fm_decay;  /* phase modulation: amount, modulator ratio, decay (s) */
    double pitch_drop, pitch_time;  /* starts pitch_drop semitones off and falls back over pitch_time s */
    double psweep;          /* semitones the pitch moves over the whole sound (risers) */
    double vibrato, lfo_hz; /* vibrato in cents; the LFO rate for vibrato and wobble */
    int filter;             /* SF_* */
    double cutoff, reso;    /* Hz, 0..1 */
    double fenv, fdecay;    /* the filter opens fenv octaves and closes over fdecay s */
    double fsweep;          /* octaves the cutoff moves over the whole sound */
    double wobble;          /* LFO depth on the cutoff, octaves */
    double attack, decay, sustain, release;   /* s, s, 0..1, s */
    double drive, crush;    /* saturation and bit/rate reduction, 0..1 */
    int vowel, vowel2;      /* a voice: formants of vowel 1..5 (a e i o u), 0 = none; vowel2 to move to */
    double vmorph;          /* vowel movement: Hz back and forth, or 0 = one glide over the sound */
    double fshift;          /* formants moved this many semitones (- = deeper voice, + = higher) */
} SynthPatch;

/* A Generator sound: the preset's patch plus the friendly controls (0..1, 0.5 = as designed). */
enum { MAC_BRIGHT, MAC_SOFT, MAC_THICK, MAC_GRIT, MAC_WOBBLE, MACROS };
typedef struct {
    SynthPatch patch;
    double macro[MACROS];
    int note;               /* MIDI note */
    double seconds, volume, a4;
} SynthSound;

typedef struct {
    const char *name, *genre, *kind, *about;
    SynthPatch patch;
    int note;
    double seconds;
} SynthPreset;
extern const SynthPreset SYNTH_PRESETS[];
extern const int NSYNTH_PRESETS;
extern const char *const SYNTH_GENRES[];
extern const int NSYNTH_GENRES;
extern const char *const SYNTH_KINDS[];
extern const int NSYNTH_KINDS;
extern const char *const MACRO_NAMES[MACROS][3];   /* name, low end, high end */

float *render_synth(const SynthSound *s, long *frames);   /* stereo at the project rate */
void synth_from_preset(SynthSound *s, int preset);
void synth_patch_text(const SynthPatch *p, char *buf, size_t n);   /* for project files */
void synth_patch_parse(const char *s, SynthPatch *p);

/* A sung line (the Singer): words sung on notes. Each syllable is spoken by eSpeak NG (a separate
 * free program) and played back through a vocoder at its note's pitch and length. */
#define VOCAL_NOTES 96
#define VOCAL_BENDS 256
typedef struct { int note; double start, len; } VocalNote;   /* MIDI note; start and length in beats */
enum { VS_ROBOT, VS_TALKBOX, VS_CHOIR, VS_WHISPER, VS_CLEAR, VS_STYLES };
typedef struct {
    char words[256];        /* as typed: a space or - starts the next note, , and . are rests */
    int nnotes;
    VocalNote notes[VOCAL_NOTES];
    int style, high, lang;  /* VS_*; a higher voice; index into VOCAL_LANGS */
    double vibrato, volume, a4, bpm;
    int tune, first;        /* how the notes were made, so the Singer can show it again */
    double step;
    int singer;             /* VS_CLEAR: which of CLEAR_SINGERS */
    int nbend;              /* the pitch curve: semitones off the notes, at beats from the start, */
    float bend_beat[VOCAL_BENDS], bend_semis[VOCAL_BENDS];   /* in beat order; straight lines between */
} VocalSpec;
typedef struct { const char *name, *about, *variant; int high; } ClearSinger;
extern const ClearSinger CLEAR_SINGERS[];
extern const int NCLEAR_SINGERS;
extern const char *const VOCAL_STYLES[VS_STYLES];
extern const char *const VOCAL_LANGS[][2];   /* name, eSpeak voice */
extern const int NVOCAL_LANGS;

/* A sound in the library: interleaved stereo float at the project rate, plus a min/max
 * overview for drawing. Generated sounds remember their recipe and imported ones their
 * file, so a rate change can rebuild them at full quality instead of resampling. */
typedef struct {
    char name[96];
    float *data;
    long frames;
    float *peak_min, *peak_max;
    long npeaks;
    int generated;
    GenParams gen;
    double gen_bpm;
    int chord[6];           /* for generated chords: the MIDI notes (nchord > 0) */
    int nchord;
    int is_beat;            /* generated drum loop: rebuilt from beat + beat_mask; 2 = one hit of drum
                             * lane beat_mask from that kit (pads) */
    int removed;            /* taken out of the library (kept so undo can bring it back) */
    BeatSpec beat;
    int beat_mask;          /* which lanes it contains */
    char path[1024];
    int pitched;            /* a variant of sample `src`: shifted `semis` semitones and/or reversed.
                             * Made for clip pitch/reverse and pad recording; hidden in the library */
    int src, semis, reversed;
    int listed;             /* a pitched copy shown in the library anyway (pasted there from a pad) */
    double stretch;         /* a variant time-stretched this many times longer (0 or 1 = not) */
    int hidden;             /* a copy remade at another tempo for clips: kept out of the library list */
    int synth;              /* made in the Generator: rebuilt from syn */
    SynthSound syn;
    int vocal;              /* made in the Singer: rebuilt from voc (needs eSpeak NG) */
    VocalSpec voc;
    int nvel, nrr;          /* velocity layers (soft to hard) and round-robins; 0 or 1 = just this sound */
    int layer[4][3];        /* the sound for each layer and round-robin (the hardest's first is this one) */
    int is_layer;           /* a layer of sound `src`: layer layer_lvl of layer_n, round-robin layer_rr */
    int layer_lvl, layer_n, layer_rr;
    int saved;              /* a copy is in the Saved sounds folder: it's in every project's library */
    int pinned;             /* kept at the top of the library (and saved) */
} Sample;

/* A placed region on the timeline that plays part of a sample. All positions in frames.
 * A note clip (pattern > 0) plays the notes of its pattern instead, with `sample` pitched to
 * each note (it sounds as is at note `root`); offset is then how far into the pattern it starts.
 * Copies of a note clip share its pattern, so editing one edits them all. */
typedef struct {
    int sample, track;
    long start;             /* timeline position */
    long offset;            /* first frame of the sample (or pattern) that plays */
    long length;            /* frames played */
    float gain_db;          /* clip volume, 0 = as is */
    long fade_in, fade_out; /* frames */
    int pattern;            /* 0 = an audio clip */
    int root;               /* note clips: the note at which the sound plays unpitched */
    int bend;               /* its pitch curve (a Bend's id), 0 = none */
} Clip;

/* A pitch curve drawn over a clip (Mixer > Clip): semitones, in straight lines between points, at
 * seconds from the clip's origin (start - offset: where its sound or pattern starts). Before the
 * first point and after the last the bend holds. Copies of a clip share it until one is edited. */
#define MAX_BENDS 256
#define BEND_POINTS 512
typedef struct {
    int id;                 /* > 0 */
    int keep_time;          /* audio clips: 1 = the pitch moves and the timing stays; 0 = tape (speed with pitch) */
    int n;
    float at[BEND_POINTS], semis[BEND_POINTS];   /* in time order */
} Bend;

/* A drum pattern (the Drums panel): rows of sounds over 16 or 32 sixteenth-note steps, each step with
 * a velocity and a chance of playing. A drum clip (Clip.pattern == -id) plays it over and over for as
 * long as the clip is; hits ring out past the clip's end. */
#define MAX_DRUMS 64
#define DRUM_ROWS 8
#define DRUM_STEPS 32
typedef struct {
    int id;                 /* > 0 */
    int track;              /* the track whose panel shows it */
    char name[24];
    int steps;              /* 16 or 32 */
    int nrows;
    int sample[DRUM_ROWS];  /* library sound of each row, -1 = none yet */
    float row_vol[DRUM_ROWS];
    unsigned char vel[DRUM_ROWS][DRUM_STEPS];    /* 0 = rest, 1..127 */
    unsigned char prob[DRUM_ROWS][DRUM_STEPS];   /* chance it plays, 1..100 % */
    float swing;            /* 0..0.5: how late the off-beat 16ths land, in 16ths */
} DrumPat;

/* A note in a pattern (piano roll). start is in frames from the start of the pattern. */
typedef struct {
    int pattern, note;
    long start, length;
    float vel;              /* 0..1 */
} Note;

/* Automation: points on a track's lane for one parameter, kept sorted by track, param, at.
 * v is 0..1 across the parameter's range (see auto_to_value). Between points the value moves
 * in a straight line; before the first and after the last it holds. */
enum { AUTO_VOLUME, AUTO_PAN, AUTO_CUTOFF, AUTO_DELAY, AUTO_REVERB, AUTO_MACRO1, AUTO_MACRO2, AUTO_MACRO3, AUTO_MACRO4,
       AUTO_PARAMS };
extern const char *AUTO_NAMES[AUTO_PARAMS];
typedef struct {
    int track, param;
    long at;
    float v;
} AutoPoint;

enum { FILT_OFF, FILT_LOW, FILT_HIGH, FILT_BAND, FILT_TYPES };

/* What a track's macro knobs can turn: each link moves one of these from `lo` to `hi` as the knob goes
 * from 0 to 1 (the cutoff on a log scale). A linked parameter follows its macro, not its own knob. */
enum { TP_VOLUME, TP_PAN, TP_CUTOFF, TP_RESO, TP_EQ_LOW, TP_EQ_MID, TP_EQ_HIGH, TP_DRIVE, TP_DELAY, TP_REVERB,
       TP_DUCK, TP_WIDTH, TP_COMP, TP_COUNT };
typedef struct { const char *name; float lo, hi; int log; const char *fmt; } TrackParamInfo;
extern const TrackParamInfo TRACK_PARAMS[TP_COUNT];
#define TRACK_MACROS 4
#define MACRO_LINKS 4
typedef struct { int param; float lo, hi; } MacroLink;          /* param: TP_* */
typedef struct {
    char name[16];          /* "" = unused */
    float value;            /* 0..1 */
    int nlinks;
    MacroLink link[MACRO_LINKS];
} Macro;

typedef struct {
    char name[32];
    float volume;
    int mute, solo;
    float pan;              /* -1 left .. 1 right */
    int filter;             /* FILT_* */
    float cutoff, reso;     /* Hz, 0..1 */
    float eq[3];            /* low, mid, high in dB */
    float drive;            /* 0..1 */
    float delay_send, reverb_send;   /* 0..1 */
    float duck;             /* 0..1: how far the sidechain source pushes this track down */
    float width;            /* stereo width: 0 mono, 1 as it is, 2 twice as wide */
    int comp_on;            /* the track's own compressor */
    float comp_thresh, comp_ratio, comp_attack, comp_release, comp_makeup;   /* dB, :1, ms, ms, dB */
    Macro macro[TRACK_MACROS];
} Track;

/* The shared effects every track can send to, plus the sidechain and live controls. */
#define DELAY_DIVS 8
extern const double DELAY_BEATS[DELAY_DIVS];
extern const char *DELAY_NAMES[DELAY_DIVS];
typedef struct {
    int delay_div;          /* index into DELAY_BEATS */
    float delay_feedback, delay_tone, delay_level;
    int pingpong;
    float reverb_size, reverb_damp, reverb_level;
    int sc_source;          /* track whose sound ducks the others, -1 = none */
    float sc_release;       /* ms */
    float bend_range;       /* semitones the pitch wheel bends live sounds */
    /* the master's dynamics, last before the output: a compressor that glues the mix together and
     * a limiter that never lets it go over the ceiling */
    int comp_on;
    float comp_thresh, comp_ratio, comp_attack, comp_release, comp_makeup;   /* dB, :1, ms, ms, dB */
    int limit_on;
    float limit_ceiling;    /* dBFS */
} MasterFx;

/* ---- pads: library sounds played live from keys, controllers and MIDI ---- */

#define NPADS 16
#define MAX_VOICES 48
enum { PAD_ONESHOT, PAD_HOLD, PAD_LOOP, PAD_MODES };

typedef struct {
    int sample;             /* library index, -1 = empty */
    int mode;               /* PAD_* */
    float volume;
    int pitch;              /* semitones */
    int track;              /* where it plays through and where recorded hits are placed */
    float pan;
} Pad;

/* A sound playing live (a pad hit or an instrument note), mixed on top of the song. */
typedef struct {
    int active;
    int sample;
    int pad, note;          /* who started it: pad index or -1, MIDI note or -1 */
    int track;              /* the track whose effects it plays through */
    float pan;
    double pos, step;       /* step != 1 plays it pitched */
    float gain;
    int loop;
    int fade;               /* frames left of the fade-out once stopped, 0 = not stopping */
} Voice;

/* A tempo and/or time signature change, from the start of bar `bar` (0-based, >= 1) on. The song's
 * first tempo and time signature are the project's bpm, ts_num and ts_den. Tempo is in quarter
 * notes a minute, whatever the time signature. */
#define MAX_METER 64
typedef struct { int bar; double bpm; int num, den; } MeterChange;

/* A named section of the song (the arranger strip): bars [bar, bar + bars), 0-based. */
#define MAX_SECTIONS 64
typedef struct { int bar, bars; char name[24]; } Section;

/* The chord track: the song's harmony, silent itself, read by whatever writes parts from it.
 * Positions are in quarter notes from the start of the song, so tempo edits never move them. */
#define MAX_CHORDS 512
typedef struct {
    double at, len;         /* quarter notes */
    int root, type;         /* pitch class 0..11, CH_* */
    int bass;               /* the bass note's pitch class for a slash chord (C/E), -1 = the root */
} SongChord;

/* A key change from the start of bar `bar` (>= 1) on. The song's first key is the project's
 * key_root and key_scale (an index into SCALES). */
#define MAX_KEYS 64
typedef struct { int bar, root, scale; } KeyChange;

/* The session view: a grid of slots, tracks down the side and scenes across, launched live beside the
 * timeline. A track plays the timeline until a slot is launched on it, then that slot, round and round,
 * until it's stopped or sent back to the timeline. A slot plays a clip of its own, or loops a stretch of
 * the track's own timeline (so a song's sections can become scenes). */
#define MAX_SCENES 16
enum { SLOT_EMPTY, SLOT_CLIP, SLOT_RANGE };
typedef struct {
    int kind;
    char name[24];
    double beats;           /* how long a pass is, in quarter notes */
    Clip clip;              /* SLOT_CLIP: what it plays (its start and track aren't used) */
    int bar;                /* SLOT_RANGE: the track's timeline from this bar (0-based), `beats` long */
} Slot;
enum { SESS_TIMELINE = -1, SESS_STOPPED = -2, SESS_NONE = -3 };

/* The project. The audio thread reads it; the UI changes it only while holding the
 * audio device lock (see LOCK/UNLOCK in main.c). */
typedef struct {
    Sample samples[MAX_SAMPLES];
    int nsamples;
    Clip clips[MAX_CLIPS];
    int nclips;
    Track tracks[MAX_TRACKS];
    int ntracks;            /* rows shown; grows up to MAX_TRACKS */
    double bpm;             /* the tempo at the start (see changes for later ones) */
    int ts_num, ts_den;     /* the time signature at the start: 4/4, 3/4, 6/8... */
    MeterChange changes[MAX_METER];   /* in bar order */
    int nchanges;
    double a4;              /* tuning reference for new sounds */
    float master;
    long playhead;
    int playing;
    int loop;
    long loop_start, loop_end;
    int preview;            /* library sample being auditioned, or -1 */
    long preview_pos;
    Pad pads[NPADS];
    int inst_sample;        /* the instrument MIDI keys play, -1 = none */
    int inst_root;          /* the note at which it plays unpitched */
    int inst_track;         /* the track it plays through and records to */
    MasterFx fx;
    Note notes[MAX_NOTES];
    int nnotes;
    AutoPoint autos[MAX_AUTO];
    int nautos;
    DrumPat drums[MAX_DRUMS];
    int ndrums;
    Section sections[MAX_SECTIONS];         /* in bar order, not overlapping */
    int nsections;
    int key_root, key_scale;                /* the key at the start */
    KeyChange keys[MAX_KEYS];               /* in bar order */
    int nkeys;
    SongChord chords[MAX_CHORDS];               /* in order, not overlapping */
    int nchords;
    Bend bends[MAX_BENDS];                  /* pitch curves, referred to by clips */
    int nbends;
    char tags[128];                         /* words to find it by in the project browser, comma-separated */
    Slot slots[MAX_TRACKS][MAX_SCENES];     /* the session view */
    int nscenes;
    char scene_names[MAX_SCENES][24];
    /* what each track is playing live (not saved): SESS_TIMELINE, SESS_STOPPED or a scene; since when;
     * and what it switches to at sess_q_at (SESS_NONE: nothing queued). The audio thread makes the switch. */
    int sess_play[MAX_TRACKS], sess_q[MAX_TRACKS];
    long sess_at[MAX_TRACKS], sess_q_at[MAX_TRACKS];
    Voice voices[MAX_VOICES];
    float bend;             /* live pitch wheel, -1..1 (not saved) */
    float modwheel;         /* live mod wheel 0..1: opens the instrument track's filter (not saved) */
} Project;

/* ---- export ---- */

enum { EXP_WAV, EXP_AIFF, EXP_FLAC, EXP_OGG, EXP_OPUS, EXP_MP3, EXP_COUNT };

typedef struct {
    const char *name, *ext;
    int sf_format;          /* major format (lossless) or major|codec (lossy) */
    int lossy;
    int max_rate;           /* 0 = no limit */
    int fixed_rate;         /* 0 = any */
    int max_bits;           /* lossless only: highest bit depth (32 = float) */
} ExportFormat;

typedef struct {
    int format;             /* EXP_* */
    int bits;               /* 16, 24 or 32 (32 = float) */
    int rate;               /* 0 = project rate */
    double quality;         /* lossy formats: 0..1 */
    int mono;
    int loop_only;
    int normalize;          /* 0 off, 1 = -1 dBFS, 2 = -0.1 dBFS (mixes only) */
    int dither;             /* TPDF dither for 16-bit */
    int stems;              /* one file per track instead of the mix */
    int loudness;           /* mixes only: 0 off, 1 = -14 LUFS (streaming), 2 = -9 LUFS (loud); peaks kept under -1 dB */
} ExportOptions;

extern const ExportFormat EXPORT_FORMATS[EXP_COUNT];
extern const int PROJECT_RATES[6];

void export_defaults(ExportOptions *o);
void export_effective(const ExportOptions *o, int *rate, int *bits);  /* after format limits */
int project_export(const Project *p, const ExportOptions *o, const char *base,
                   char *msg, size_t msglen);                         /* base = path without extension */

/* ---- project, samples, mixing ---- */

extern const char *WAVE_NAMES[WAVE_COUNT];

void project_init(Project *p);
long project_end(const Project *p);
int project_set_rate(Project *p, int rate, char *msg, size_t msglen);
void note_name(int note, char *buf, size_t n);

int sample_add(Project *p, const char *name, float *data, long frames); /* takes ownership */
int sample_generate(Project *p, const GenParams *g);
/* Several notes of the same sound, summed: chords and intervals. */
float *render_notes(const GenParams *g, double bpm, const int *notes, int n, long *frames);
int sample_generate_notes(Project *p, const GenParams *g, const int *notes, int n, const char *name);
/* One pass of a drum pattern (lanes in `mask`) as a seamless loop: tails wrap to the start. */
float *render_beat(const BeatSpec *b, int mask, double bpm, long *frames);
int sample_generate_beat(Project *p, const BeatSpec *b, int mask, const char *name);
/* One hit of one drum (lane 0-5: kick, snare, clap, closed hat, open hat, percussion) from a kit */
float *render_hit(const BeatSpec *b, int lane, long *frames);
int sample_generate_hit(Project *p, const BeatSpec *b, int lane, const char *name);
int sample_generate_synth(Project *p, const SynthSound *s, const char *name);

/* the Singer (voice.c) */
int speech_available(char *where, size_t n);            /* 1 when eSpeak NG can be found */
int vocal_syllables(const char *words, char syl[][40], int *rest_before, int max);
float *render_vocal(const VocalSpec *v, long *frames, char *err, size_t errlen);
double vocal_bend_at(const VocalSpec *v, double beat);   /* the pitch curve: semitones; 0 with no points */
/* Adds a sung line; if it can't be sung (no eSpeak NG), adds silence that keeps the recipe. */
int sample_generate_vocal(Project *p, const VocalSpec *v, const char *name, char *err, size_t errlen);
int project_add_track(Project *p, const char *name);   /* new row, or -1 when full */
int sample_import(Project *p, const char *path, char *err, size_t errlen);
/* Renders clips (indices into p->clips) together, alone, through their tracks' effects, writes
 * the result to `path` (a 32-bit float WAV) and adds that file to the library. */
int sample_bounce(Project *p, const int *clips, int n, const char *path, const char *name, char *err, size_t errlen);
/* Writes a recording (stereo, the project rate) to `path` as a WAV and adds that file to the library. */
int sample_record(Project *p, const float *buf, long frames, const char *path, const char *name, char *err, size_t errlen);
/* Writes a sound as a WAV (32-bit float, so nothing is lost). */
int sample_write_wav(const Sample *s, const char *path, char *err, size_t errlen);
void sample_minmax(const Sample *s, long f0, long f1, float *lo, float *hi);

/* A copy of sample `src` shifted by `semis` semitones and/or reversed; reuses an existing one.
 * Returns src itself for (0, 0). */
int sample_variant(Project *p, int src, int semis, int reverse);
int sample_variant_ex(Project *p, int src, int semis, int reverse, double stretch);
/* Longer by `factor`, at the same pitch (WSOLA). Stereo in and out. */
float *time_stretch(const float *in, long frames, double factor, long *out_frames);
/* The sound `s` remade to last `ratio` times as long, as a tempo change needs: recipes made at a
 * tempo are made again at the new one, audio is time-stretched. -1 for one-shots (a drum hit, a
 * Generator sound), which keep their length. */
int sample_at_tempo(Project *p, int s, double ratio);
int sample_pitched(Project *p, int src, int semis);
int sample_base(const Project *p, int s);       /* the original a variant was made from */
/* Velocity layers and round-robins: copies of a sound made softer (darker, a gentler start) for
 * lower layers and a touch different (a few cents, a little level) for each round-robin. */
#define MAX_VEL 4
#define MAX_RR 3
int sample_make_layers(Project *p, int s, int nvel, int nrr);   /* 1, 1 takes them away */
int sample_layer(Project *p, int src, int lvl, int n, int rr);  /* one layer, made or reused */
/* The sound to play for a hit of `s` at velocity vel (0..1), the rr-th hit in a row. */
int sample_pick(const Project *p, int s, float vel, unsigned rr);

/* ---- live voices (call with the audio device locked) ---- */
int voice_start(Project *p, int sample, double step, float gain, int loop, int pad, int note);
void voice_stop(Project *p, int pad, int note);  /* fades out voices from that pad or note */
int voice_playing(const Project *p, int pad);
void voices_clear(Project *p);

/* ---- the mixer (fx.c): clips and live voices through each track's effects ---- */
typedef struct Mixer Mixer;
/* The master's compressor and limiter over a stereo block (after the master volume). */
void master_dynamics(Mixer *m, const Project *p, float *buf, int n);
void mixer_dynamics_meter(const Mixer *m, float *comp_db, float *limit_db);   /* gain reduction now */
float mixer_track_gr(const Mixer *m, int track);   /* how far a track's compressor is turning it down, dB */
/* Brings a whole stereo buffer to `lufs`, then holds its peaks under `ceiling_db` (look-ahead
 * limiter, timing kept). Returns the loudness it measured first. */
double loudness_match(float *buf, long n, double lufs, double ceiling_db);

/* Loudness (ITU-R BS.1770 / EBU R128), loudness.c */
typedef struct {
    int rate, piece, count;
    double b1[3], a1[2], b2[3], a2[2], s1[2][2], s2[2][2], acc;
    double recent[30];
    int rpos, nrecent;
    double *blocks;
    int nblocks, cap;
} Loud;
void loud_init(Loud *l, int rate);
void loud_reset(Loud *l);
void loud_free(Loud *l);
void loud_feed(Loud *l, const float *stereo, long n);
double loud_momentary(const Loud *l);   /* LUFS over the last 400 ms */
double loud_short(const Loud *l);       /* the last 3 s */
double loud_integrated(const Loud *l);  /* the whole run, gated */
double loudness_of(const float *stereo, long n);   /* integrated LUFS of a buffer */
Mixer *mixer_new(void);             /* sized for the current project rate */
void mixer_free(Mixer *m);
int mixer_rate(const Mixer *m);
/* Adds [start, start + frames) to out. clips = 0 mixes only live voices (stopped transport);
 * voices = 0 leaves them out (export). only_track >= 0 renders that track alone (a stem),
 * ignoring mute/solo. */
void mixer_run(Mixer *m, Project *p, long start, int clips, float *out, int frames, int only_track, int voices);
int project_has_tails(const Project *p);        /* any delay or reverb in use */
/* Each track's loudest sample (left, right) since the last call, after its fader; then resets. */
void mixer_levels(Mixer *m, float out[][2]);

/* ---- automation and note clips (fx.c) ---- */
float auto_to_value(int param, float v);        /* 0..1 to the parameter's own units */
float value_to_auto(int param, float value);
void auto_sort(Project *p);                     /* after adding or moving points */
int auto_lane(const Project *p, int track, int param, int *first);   /* how many points; first index */
float auto_at(const Project *p, int track, int param, long at, int *has);   /* 0..1 at frame `at` */
void auto_apply(const Project *p, int track, long at, Track *out);  /* the track with its automation and macros */
float *track_param(Track *t, int tp);           /* where a TP_* parameter lives in a track */
void macros_apply(Track *t, const float *values);   /* the macros (at these values, or their own) set what they're linked to */
int sample_root(const Sample *s);              /* the note a sound plays at, for note clips */
long pattern_end(const Project *p, int pattern);

/* The tempo map: frames, beats (quarter notes from the start) and bars (from 0), any way round. */
double meter_beats(const Project *p, long frame);          /* quarter notes from the start */
long meter_beats_frame(const Project *p, double beats);
double meter_bars(const Project *p, long frame);           /* bars from the start (0 = the first) */
long meter_bar_frame(const Project *p, double bar);
/* What's in force at a frame: tempo, time signature, and the bar it's in (its index, start, length). */
void meter_at(const Project *p, long frame, double *bpm, int *num, int *den, int *bar, long *bar_start, long *bar_len);
double meter_bpm_at(const Project *p, long frame);   /* the end of its last note, in frames */
int pattern_new_id(const Project *p);
long slot_frames(const Project *p, const Slot *s, long at);   /* how long a pass of a slot is, launched at `at` */
void session_reset(Project *p);                  /* every track back on the timeline, nothing queued */
DrumPat *drum_find(Project *p, int id);
const DrumPat *drum_find_c(const Project *p, int id);
/* Whether step `k` (counted from the clip's origin, across repeats) of row `r` plays: its chance,
 * decided the same way every time (so playback and export agree). */
int drum_plays(const DrumPat *d, int r, long k, long seed);
double drum_step_beat(const DrumPat *d, long k);   /* where step k lands, in beats from the origin */

/* ---- the chord track and the song's key (harmony.c) ---- */
void key_at_bar(const Project *p, int bar, int *root, int *scale);   /* the key in force in bar `bar` */
void key_at_beat(const Project *p, double beat, int *root, int *scale);
/* Sets the key of the stretch `bar` is in (from its key change, or the start, to the next one). */
void key_set(Project *p, int bar, int root, int scale);
int key_change_add(Project *p, int bar);        /* a change at `bar` keeping the key as it is; its index or -1 */
void key_change_remove(Project *p, int i);
void keys_tidy(Project *p);                     /* bar order, one a bar */
int chord_at(const Project *p, double beat);    /* the chord sounding at a beat, or -1 */
/* Puts a chord over [at, at + len), cutting back or removing the chords it covers; its index or -1. */
int chord_put(Project *p, double at, double len, int root, int type, int bass);
void chord_remove(Project *p, int i);
void chords_sort(Project *p);
void chord_split(Project *p, double beat);      /* a chord sounding across `beat` becomes two there */
/* "Am", "C/E": spelled with sharps or flats as the key there wants. */
void chord_label(const Project *p, const SongChord *c, char *buf, size_t n);
/* The chord's Roman numeral in the key at its start: "vi", "V7", or "bVII" for a chord borrowed
 * from outside the key (degrees counted on the major scale of the key's root). */
void chord_numeral(const Project *p, const SongChord *c, char *buf, size_t n);
/* A voicing between lo and hi (MIDI notes) that moves as little as it can from prev (nprev notes,
 * 0 = none: close to the middle of the range). Returns how many notes went into out (up to 6). */
int chord_voice(const SongChord *c, const int *prev, int nprev, int lo, int hi, int *out);

/* ---- pitch curves on clips (bend.c) ---- */
typedef struct BendMap BendMap;
typedef struct BendWork BendWork;
float bend_at(const Bend *b, double sec);
int bend_used(const Project *p, int id);        /* by a clip or a session slot */      /* semitones at a time from the clip's origin */
const Bend *bend_find_c(const Project *p, int id);
Bend *bend_find(Project *p, int id);
int bend_new(Project *p);                       /* a new empty curve's id (keeping the timing), or 0 */
/* The curve's warp, for playback: NULL when there's none or it isn't built yet. bend_warp gives how many
 * frames into its sound a clip is, `frames` of timeline after its origin. */
const BendMap *bend_map(const Project *p, int id);
double bend_warp(const BendMap *m, double frames);
const float *bend_shifted(const Project *p, int id, int sample);   /* a keep-time render, or NULL */
BendWork *bends_build(const Project *p);        /* what changed, built without the audio lock (NULL: nothing) */
void bends_install(BendWork *w);                /* swaps it in: call holding the audio lock */

/* ---- parts written from the chord track (parts.c) ---- */
enum { PART_CHORDS, PART_BASS, PART_MELODY, PART_KINDS };
#define PART_MAX_STYLES 6
extern const char *const PART_STYLES[PART_KINDS][PART_MAX_STYLES];
extern const int PART_NSTYLES[PART_KINDS];
typedef struct { int note; double at, len; float vel; } PartNote;   /* at and len in beats from the start */
typedef struct {
    int kind, style;        /* PART_*, an index into PART_STYLES[kind] */
    double from, to;        /* the stretch of the song, in beats */
    int lo, hi;             /* the range, MIDI notes */
    double density;         /* 0 sparse .. 1 busy */
    unsigned seed;          /* the same seed writes the same part */
} PartSpec;
/* The part's notes over [from, to), following the chord track and the key there. Returns how many. */
int part_write(const Project *p, const PartSpec *s, PartNote *out, int max);
enum { FILL_ROLL, FILL_BUILD, FILL_TOMS, FILL_DROP, FILL_KINDS };
extern const char *const FILL_STYLES[FILL_KINDS];
/* One bar of `src` as it plays from step `from_step` (bar_steps 16ths long), with its end turned
 * into a fill. intensity 0..1 makes it a beat, half a bar or the whole bar long. */
void drum_fill(const Project *p, const DrumPat *src, int from_step, int bar_steps, int style, double intensity,
               unsigned seed, DrumPat *out);

/* ---- analysis (analysis.c) ---- */
#define VIS_FFT 4096
/* Magnitude spectrum in dB (full-scale sine = 0 dB) of n mono samples, n a power of two up to
 * VIS_FFT, Hann-windowed. Writes n / 2 bins; bin k is k * rate / n Hz. */
void spectrum_db(const float *mono, int n, float *db);

/* ---- project files (project.c) ---- */
void project_free(Project *p);                   /* frees sample memory */
int project_save(const Project *p, const char *path, char *msg, size_t msglen);
/* Loads into dst (freshly initialised). Sets the project rate to the file's. */
int project_load(Project *dst, const char *path, char *msg, size_t msglen);

/* What the project browser shows of a project, read quickly (no sounds are made). The clips are the
 * ones starting in its first 16 bars, by the opening tempo and time signature. */
#define PEEK_CLIPS 512
typedef struct {
    char title[96], tags[128];
    double bpm, seconds;
    int ts_num, ts_den, rate, key_root, key_scale, has_key;
    int ntracks, nclips, nsections, nchords, nscenes;
    struct { short track; long start, length; } clips[PEEK_CLIPS];
    int npeek;
    struct { int bar, bars; char name[24]; } sections[16];
    int npsec;
} ProjectInfo;
int project_peek(const char *path, ProjectInfo *out);
/* Rewrites only the tags line of a project file (the rest stays byte for byte). */
int project_set_tags(const char *path, const char *tags);

float soft_clip(float v);
/* Adds the output for [start, start + frames) into out (stereo). only_track < 0 mixes every
 * track honouring mute/solo; otherwise only that track, ignoring mute/solo (for stems). */


#endif
