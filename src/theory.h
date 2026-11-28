/* tinydaw: music theory tables and helpers for the Theory pane. No UI, no audio. */
#ifndef THEORY_H
#define THEORY_H

#include <stddef.h>

typedef struct {
    const char *name;
    int n;                  /* notes per octave */
    int steps[7];           /* semitones above the root */
    int mode_offset;        /* semitones above its parent major key's root (Dorian = 2) */
    int minor;              /* minor-flavoured: the circle highlights the minor ring */
} Scale;

enum { CH_MAJ, CH_MIN, CH_DIM, CH_AUG, CH_SUS2, CH_SUS4, CH_MAJ7, CH_DOM7, CH_MIN7, CH_M7B5,
       CH_DIM7, CH_MMAJ7, CH_AUGMAJ7, CH_6, CH_M6, CH_ADD9, CH_9, CH_MAJ9, CH_MIN9, CH_COUNT };

typedef struct {
    const char *name, *suffix, *formula;
    int n;
    int iv[5];              /* semitones above the root */
} ChordType;

typedef struct {
    const char *numerals, *style;
    int n;
    int root[12];           /* semitones above the key root */
    int type[12];           /* CH_* */
} Progression;

typedef struct {
    const char *name, *abbr;
    int num, den;           /* just-intonation ratio */
    const char *feel;
} Interval;

typedef struct { const char *name; int lo, hi; } TempoMark;

/* A genre preset: tempo, feel, a suggested key and tuning, a drum kit and a groove.
 * Lanes: kick, snare, clap, closed hat, open hat, percussion; one char per 16th. */
typedef struct {
    const char *name, *blurb;
    int bpm, bpm_lo, bpm_hi;
    double swing;
    int swing8;
    int root, scale;
    double a4;
    double kick_decay, snare_hz, hat_decay, open_decay;
    int steps;
    const char *lanes[6];
} Genre;

extern const Genre GENRES[];
extern const int NGENRES;
extern const char *DRUM_LANE_NAMES[6];

extern const Scale SCALES[];
extern const int NSCALES;
extern const ChordType CHORDS[CH_COUNT];
extern const Progression PROGRESSIONS[];
extern const int NPROGRESSIONS;
extern const Interval INTERVALS[13];
extern const TempoMark TEMPO_MARKS[];
extern const int NTEMPO_MARKS;

const char *pitch_name(int pc, int flats);
int scale_parent_major(int root, int scale);
int key_uses_flats(int root, int scale);
void scale_degree_name(int scale, int k, char *buf, size_t n);
/* Note k of a scale, spelled with one letter per degree for 7-note scales (Eb minor has Cb). */
void scale_note_name(int root, int scale, int k, char *buf, size_t n);
void diatonic_chord_name(int root, int scale, int degree, int type, char *buf, size_t n);
/* The note `degree` letters and `semis` semitones above a root spelled with/without flats. */
void note_above(int root_pc, int flats, int degree, int semis, char *buf, size_t n);
int chord_notes(int root_pc, int type, int base_midi, int *out);
void chord_name(int root_pc, int type, int flats, char *buf, size_t n);
/* The chord's notes spelled by degree (Gm = G Bb D, not G A# D). */
void chord_tone_names(int root_pc, int type, int flats, char *buf, size_t n);
/* Chord built on scale degree `degree` (0-based) of a 7-note scale. Returns its root pitch
 * class and sets *type and the Roman numeral, or returns -1 for 5/6-note scales. */
int diatonic_chord(int root, int scale, int degree, int sevenths, int *type, char *numeral, size_t nlen);
/* The chord that best explains how much each pitch class sounds (w, e.g. length x velocity) with
 * `lowest` the lowest note's pitch class (-1 = don't know). Returns its root and sets *type and
 * *bass (a slash chord's bass, -1 = the root), or returns -1 when nothing sounds. */
int chord_guess(const float w[12], int lowest, int *type, int *bass);
/* Key signature of a major key: >0 sharps, <0 flats. */
int key_signature(int major_root);

#endif
