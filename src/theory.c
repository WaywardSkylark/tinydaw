/* tinydaw: music theory tables and helpers. */
#include "theory.h"

#include <stdio.h>
#include <string.h>

static const int MAJOR[7] = {0, 2, 4, 5, 7, 9, 11};

const Scale SCALES[] = {
    {"Major",       7, {0, 2, 4, 5, 7, 9, 11}, 0,  0},
    {"Minor",       7, {0, 2, 3, 5, 7, 8, 10}, 9,  1},
    {"Harm. minor", 7, {0, 2, 3, 5, 7, 8, 11}, 9,  1},
    {"Mel. minor",  7, {0, 2, 3, 5, 7, 9, 11}, 9,  1},
    {"Dorian",      7, {0, 2, 3, 5, 7, 9, 10}, 2,  1},
    {"Phrygian",    7, {0, 1, 3, 5, 7, 8, 10}, 4,  1},
    {"Lydian",      7, {0, 2, 4, 6, 7, 9, 11}, 5,  0},
    {"Mixolydian",  7, {0, 2, 4, 5, 7, 9, 10}, 7,  0},
    {"Locrian",     7, {0, 1, 3, 5, 6, 8, 10}, 11, 1},
    {"Pent. major", 5, {0, 2, 4, 7, 9},        0,  0},
    {"Pent. minor", 5, {0, 3, 5, 7, 10},       9,  1},
    {"Blues",       6, {0, 3, 5, 6, 7, 10},    9,  1},
};
const int NSCALES = sizeof SCALES / sizeof *SCALES;

const ChordType CHORDS[CH_COUNT] = {
    [CH_MAJ]     = {"Major",              "",       "1 3 5",       3, {0, 4, 7}},
    [CH_MIN]     = {"Minor",              "m",      "1 b3 5",      3, {0, 3, 7}},
    [CH_DIM]     = {"Diminished",         "dim",    "1 b3 b5",     3, {0, 3, 6}},
    [CH_AUG]     = {"Augmented",          "aug",    "1 3 #5",      3, {0, 4, 8}},
    [CH_SUS2]    = {"Suspended 2nd",      "sus2",   "1 2 5",       3, {0, 2, 7}},
    [CH_SUS4]    = {"Suspended 4th",      "sus4",   "1 4 5",       3, {0, 5, 7}},
    [CH_MAJ7]    = {"Major 7th",          "maj7",   "1 3 5 7",     4, {0, 4, 7, 11}},
    [CH_DOM7]    = {"Dominant 7th",       "7",      "1 3 5 b7",    4, {0, 4, 7, 10}},
    [CH_MIN7]    = {"Minor 7th",          "m7",     "1 b3 5 b7",   4, {0, 3, 7, 10}},
    [CH_M7B5]    = {"Half-diminished",    "m7b5",   "1 b3 b5 b7",  4, {0, 3, 6, 10}},
    [CH_DIM7]    = {"Diminished 7th",     "dim7",   "1 b3 b5 bb7", 4, {0, 3, 6, 9}},
    [CH_MMAJ7]   = {"Minor-major 7th",    "m(maj7)", "1 b3 5 7",   4, {0, 3, 7, 11}},
    [CH_AUGMAJ7] = {"Augmented major 7th", "+maj7", "1 3 #5 7",    4, {0, 4, 8, 11}},
    [CH_6]       = {"Major 6th",          "6",      "1 3 5 6",     4, {0, 4, 7, 9}},
    [CH_M6]      = {"Minor 6th",          "m6",     "1 b3 5 6",    4, {0, 3, 7, 9}},
    [CH_ADD9]    = {"Add 9",              "add9",   "1 3 5 9",     4, {0, 4, 7, 14}},
    [CH_9]       = {"Dominant 9th",       "9",      "1 3 5 b7 9",  5, {0, 4, 7, 10, 14}},
    [CH_MAJ9]    = {"Major 9th",          "maj9",   "1 3 5 7 9",   5, {0, 4, 7, 11, 14}},
    [CH_MIN9]    = {"Minor 9th",          "m9",     "1 b3 5 b7 9", 5, {0, 3, 7, 10, 14}},
};

#define D7 CH_DOM7
const Progression PROGRESSIONS[] = {
    {"I - IV - V - I",          "Basic cadence",     4,  {0, 5, 7, 0},  {CH_MAJ, CH_MAJ, CH_MAJ, CH_MAJ}},
    {"I - V - vi - IV",         "Pop",               4,  {0, 7, 9, 5},  {CH_MAJ, CH_MAJ, CH_MIN, CH_MAJ}},
    {"I - vi - IV - V",         "50s / doo-wop",     4,  {0, 9, 5, 7},  {CH_MAJ, CH_MIN, CH_MAJ, CH_MAJ}},
    {"vi - IV - I - V",         "Pop, minor start",  4,  {9, 5, 0, 7},  {CH_MIN, CH_MAJ, CH_MAJ, CH_MAJ}},
    {"I - bVII - IV - I",       "Mixolydian rock",   4,  {0, 10, 5, 0}, {CH_MAJ, CH_MAJ, CH_MAJ, CH_MAJ}},
    {"ii7 - V7 - Imaj7",        "Jazz cadence",      3,  {2, 7, 0},     {CH_MIN7, CH_DOM7, CH_MAJ7}},
    {"Imaj7 - vi7 - ii7 - V7",  "Jazz turnaround",   4,  {0, 9, 2, 7},  {CH_MAJ7, CH_MIN7, CH_MIN7, CH_DOM7}},
    {"i - VI - III - VII",      "Minor pop",         4,  {0, 8, 3, 10}, {CH_MIN, CH_MAJ, CH_MAJ, CH_MAJ}},
    {"i - iv - v - i",          "Natural minor",     4,  {0, 5, 7, 0},  {CH_MIN, CH_MIN, CH_MIN, CH_MIN}},
    {"i - iv - V - i",          "Harmonic minor",    4,  {0, 5, 7, 0},  {CH_MIN, CH_MIN, CH_MAJ, CH_MIN}},
    {"i - VII - VI - V",        "Andalusian cadence", 4, {0, 10, 8, 7}, {CH_MIN, CH_MAJ, CH_MAJ, CH_MAJ}},
    {"12-bar blues",            "I7 IV7 V7",         12, {0, 0, 0, 0, 5, 5, 0, 0, 7, 5, 0, 7},
                                                         {D7, D7, D7, D7, D7, D7, D7, D7, D7, D7, D7, D7}},
};
#undef D7
const int NPROGRESSIONS = sizeof PROGRESSIONS / sizeof *PROGRESSIONS;

const Interval INTERVALS[13] = {
    {"Unison",      "P1", 1, 1,   "same note"},
    {"Minor 2nd",   "m2", 16, 15, "sharp dissonance"},
    {"Major 2nd",   "M2", 9, 8,   "mild dissonance"},
    {"Minor 3rd",   "m3", 6, 5,   "consonant, dark"},
    {"Major 3rd",   "M3", 5, 4,   "consonant, bright"},
    {"Perfect 4th", "P4", 4, 3,   "open"},
    {"Tritone",     "TT", 45, 32, "tense, unstable"},
    {"Perfect 5th", "P5", 3, 2,   "open, very stable"},
    {"Minor 6th",   "m6", 8, 5,   "consonant, dark"},
    {"Major 6th",   "M6", 5, 3,   "consonant, bright"},
    {"Minor 7th",   "m7", 9, 5,   "mild dissonance"},
    {"Major 7th",   "M7", 15, 8,  "sharp dissonance"},
    {"Octave",      "P8", 2, 1,   "same note, higher"},
};

const char *DRUM_LANE_NAMES[6] = {"Kick", "Snare", "Clap", "Hat", "Open", "Perc"};

/* Typical grooves for each style, written as 16th-note grids. Tempos are common ranges. */
const Genre GENRES[] = {
    {"House", "Four-on-the-floor kick, claps on 2 and 4, off-beat open hats",
     124, 120, 128, 0.05, 0, 9, 1, 440, 9, 190, 70, 9, 16,
     {"x...x...x...x...", "................", "....x.......x...",
      "o..oo..oo..oo..o", "..x...x...x...x.", "................"}},
    {"Techno", "Driving four-on-the-floor, deep kick, busy hats",
     132, 125, 140, 0, 0, 9, 1, 440, 7, 190, 80, 7, 16,
     {"x...x...x...x...", "................", "....x.......x...",
      "o.o.o.o.o.o.o.o.", "..x...x...x...x.", "......x.......x."}},
    {"Hip-hop", "Boom bap: swung 16ths, punchy kick, snare on 2 and 4",
     90, 85, 95, 0.22, 0, 2, 1, 440, 8, 180, 55, 8, 16,
     {"x......x..x.....", "....x.......x...", "................",
      "x.x.x.x.x.x.x.x.", "................", "..............o."}},
    {"Trap", "Half-time clap on 3, long 808 kick, hi-hat rolls",
     140, 130, 150, 0, 0, 0, 1, 440, 2.2, 200, 90, 10, 16,
     {"x......x..x.....", "................", "........x.......",
      "x.x.x.x.x.xrx.rr", "................", "................"}},
    {"Drum & bass", "Fast two-step: kick on 1 and the and-of-3, snare on 2 and 4",
     174, 170, 178, 0, 0, 9, 1, 440, 8, 210, 70, 9, 32,
     {"x.........x.......x.x...........", "....x.......x.......x.......x...", "................................",
      "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", "..............................x.", "...........o...............o...."}},
    {"Rock", "Straight 8th hats, backbeat snare on 2 and 4",
     112, 100, 140, 0, 0, 4, 1, 440, 9, 190, 50, 6, 16,
     {"x.......x.x.....", "....x.......x...", "................",
      "x.x.x.x.x.x.x.x.", "................", "................"}},
    {"Funk", "Syncopated kick, ghost-note snare, 16th hats",
     100, 95, 112, 0.12, 0, 4, 4, 440, 10, 200, 75, 8, 16,
     {"x.x...x...x..x..", "....x..o.o..x..o", "................",
      "xoxoxoxoxoxoxoxo", "................", "................"}},
    {"Reggaeton", "Dembow: steady kick under a 3-3-2 snare pattern",
     95, 88, 100, 0, 0, 9, 1, 440, 8, 220, 70, 8, 16,
     {"x...x...x...x...", "...x..x....x..x.", "................",
      "x.x.x.x.x.x.x.x.", "................", "................"}},
    {"Dubstep", "Half-time: heavy kick, snare and clap together on 3",
     140, 138, 142, 0, 0, 5, 1, 440, 6, 190, 70, 8, 16,
     {"x.........x.....", "........x.......", "........x.......",
      "x.x.x.x.x.x.x.x.", "................", "................"}},
    {"Lo-fi", "Lazy swing and soft hats; 432 Hz tuning is a popular choice",
     82, 70, 90, 0.28, 0, 5, 0, 432, 9, 170, 45, 8, 16,
     {"x......x.x......", "....x.......x...", "................",
      "x.o.x.o.x.o.x.ox", "................", "..........o....."}},
    {"Disco", "Four-on-the-floor, closed hat on the beat, open on the off-beat",
     118, 110, 125, 0, 0, 9, 1, 440, 9, 200, 70, 8, 16,
     {"x...x...x...x...", "....x.......x...", "................",
      "x...x...x...x...", "..x...x...x...x.", "................"}},
    {"Jazz swing", "Ride in triplet swing, hi-hat foot on 2 and 4, feathered kick",
     140, 100, 220, 0.33, 1, 10, 0, 440, 12, 230, 60, 4, 16,
     {"o...o...o...o...", "......o.......o.", "................",
      "....x.......x...", "x...x.x.x...x.x.", "................"}},
    /* more styles (kept after the first twelve, so --beat numbers stay the same) */
    {"Deep house", "Softer four-on-the-floor, rim and shaker, warm off-beat open hats",
     122, 118, 124, 0.08, 0, 5, 1, 440, 9, 190, 60, 10, 16,
     {"x...x...x...x...", "................", "....x.......x...",
      "o.o.o.o.o.o.o.o.", "..x...x...x...x.", ".......o......o."}},
    {"Tech house", "Punchy four-on-the-floor, clap on 2 and 4, busy percussion",
     126, 124, 128, 0, 0, 7, 1, 440, 8, 195, 85, 8, 16,
     {"x...x...x...x...", "................", "....x.......x...",
      "oxoxoxoxoxoxoxox", "..x...x...x...x.", "..o..o....o..o.."}},
    {"Trance", "Driving four-on-the-floor, 8th hats, open hat on every off-beat",
     138, 135, 142, 0, 0, 9, 1, 440, 7, 200, 80, 8, 16,
     {"x...x...x...x...", "................", "....x.......x...",
      "x.x.x.x.x.x.x.x.", "..x...x...x...x.", "................"}},
    {"UK garage", "2-step: skippy kick that dodges beat 3, swung 16th hats",
     132, 128, 136, 0.18, 0, 0, 1, 440, 8, 210, 70, 8, 16,
     {"x......x..x.....", "....x.......x...", "................",
      "x.xxx.xxx.xxx.xx", "................", "...o......o....."}},
    {"Breakbeat", "Broken kick against a straight backbeat, open hat lifts the end",
     130, 120, 140, 0, 0, 4, 1, 440, 8, 200, 70, 8, 16,
     {"x.....x...x.....", "....x.......x...", "................",
      "x.x.x.x.x.x.x.x.", ".............x..", "................"}},
    {"Jungle", "Fast chopped-break feel: shuffling snares and ghost notes",
     165, 160, 175, 0, 0, 2, 1, 440, 8, 230, 80, 8, 16,
     {"x.x.......x.x...", "....x..o.x..x..o", "................",
      "x.x.x.x.x.x.x.x.", "................", "................"}},
    {"UK drill", "Half-time snare on 3, sliding 808 kick, triplet-feel hats",
     142, 138, 146, 0, 0, 1, 1, 440, 2.5, 210, 90, 9, 16,
     {"x......x..x.....", "........x.....o.", "................",
      "x..x..x.x..x..x.", "................", "...o..........o."}},
    {"Afrobeats", "Bouncy kick, clap on 2 and 4, shaker and hats in 16ths",
     104, 98, 110, 0.1, 0, 7, 0, 440, 8, 210, 65, 8, 16,
     {"x.....x.x.....x.", "................", "....x.......x...",
      "x.xxx.xxx.xxx.xx", "................", "..o...o...o...o."}},
    {"Amapiano", "Long log-drum kick, laid-back clap, shakers and open hats",
     112, 110, 115, 0.08, 0, 5, 1, 440, 3, 200, 65, 9, 16,
     {"x..x..x...x..x..", "................", "....x.......x...",
      "o.o.o.o.o.o.o.o.", "..x...x...x...x.", "...o...o...o...o"}},
    {"Dancehall", "Riddim kick under a 3-3-2 snare, straight 8th hats",
     96, 90, 100, 0, 0, 2, 1, 440, 8, 220, 70, 8, 16,
     {"x..x..x.x..x..x.", "...x..x....x..x.", "................",
      "x.x.x.x.x.x.x.x.", "................", "................"}},
    {"Reggae", "One drop: kick and rimshot together on beat 3, swung hats",
     76, 70, 85, 0.2, 0, 9, 1, 440, 10, 220, 60, 6, 16,
     {"........x.......", "................", "................",
      "x.x.x.x.x.x.x.x.", "................", "........x......."}},
    {"Bossa nova", "Rim on a 3-2 clave, soft kick on 1 and the and of 2",
     132, 120, 140, 0, 0, 2, 0, 440, 12, 240, 60, 5, 16,
     {"x..x..x.x..x..x.", "................", "................",
      "x.x.x.x.x.x.x.x.", "................", "x..x..x...x..x.."}},
    {"Synthwave", "80s drum machine: big snare and clap on 2 and 4, steady 8ths",
     100, 85, 115, 0, 0, 9, 1, 440, 8, 170, 60, 7, 16,
     {"x.......x.......", "....x.......x...", "....x.......x...",
      "x.x.x.x.x.x.x.x.", "................", "................"}},
    {"Pop", "Simple and solid: kick on 1 and 3 with a push, backbeat, 8th hats",
     120, 100, 128, 0, 0, 0, 0, 440, 9, 200, 70, 8, 16,
     {"x.......x.x.....", "....x.......x...", "....x.......x...",
      "x.x.x.x.x.x.x.x.", "..............x.", "................"}},
    {"R&B", "Slow, swung and sparse: lazy kick, crisp snare, rolling hats",
     90, 70, 100, 0.18, 0, 10, 1, 440, 7, 190, 55, 7, 16,
     {"x......x..x.....", "....x.......x...", "................",
      "x.x.x.xxx.x.x.x.", "................", "...o.......o...."}},
    {"Phonk", "Cowbell riff over a boom-bap-style beat and long 808",
     130, 120, 140, 0, 0, 4, 1, 440, 3, 200, 80, 8, 16,
     {"x.....x...x.....", "....x.......x...", "................",
      "x.x.x.x.x.x.x.x.", "................", "x..x..x...x..x.."}},
    {"Footwork", "Juke: stuttering triplet-feel kicks and claps at 160",
     160, 155, 165, 0, 0, 9, 1, 440, 6, 210, 90, 9, 16,
     {"x..x..x...x..x..", "................", "....x..x....x...",
      "x.x.x.x.x.x.x.x.", "................", "..o...o...o...o."}},
    {"Electro", "Robotic syncopated kick, snare and clap together, 8th hats",
     128, 120, 135, 0, 0, 2, 1, 440, 6, 180, 90, 7, 16,
     {"x.....x...x...x.", "....x.......x...", "....x.......x...",
      "x.x.x.x.x.x.x.x.", "................", "................"}},
};
const int NGENRES = sizeof GENRES / sizeof *GENRES;

/* Commonly quoted ranges; sources and eras differ, so treat them as rough guides. */
const TempoMark TEMPO_MARKS[] = {
    {"Largo", 40, 60},      {"Larghetto", 60, 66}, {"Adagio", 66, 76},
    {"Andante", 76, 108},   {"Moderato", 108, 120}, {"Allegro", 120, 156},
    {"Vivace", 156, 176},   {"Presto", 168, 200},  {"Prestissimo", 200, 240},
};
const int NTEMPO_MARKS = sizeof TEMPO_MARKS / sizeof *TEMPO_MARKS;

const char *pitch_name(int pc, int flats)
{
    static const char *sharp[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const char *flat[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
    pc = ((pc % 12) + 12) % 12;
    return flats ? flat[pc] : sharp[pc];
}

int scale_parent_major(int root, int scale)
{
    return ((root - SCALES[scale].mode_offset) % 12 + 12) % 12;
}

int key_signature(int major_root)
{
    int pos = (major_root * 7) % 12;             /* steps clockwise round the circle of fifths */
    return pos <= 6 ? pos : pos - 12;            /* F# major = 6 sharps; Db and beyond use flats */
}

int key_uses_flats(int root, int scale)
{
    int parent = scale_parent_major(root, scale);
    if (parent == 6) {                           /* 6 sharps or 6 flats: F# major, but Eb minor */
        int r = ((root % 12) + 12) % 12;
        return r == 1 || r == 3 || r == 8 || r == 10;
    }
    return key_signature(parent) < 0;
}

void scale_note_name(int root, int scale, int k, char *buf, size_t n)
{
    static const int letter_pc[7] = {0, 2, 4, 5, 7, 9, 11};
    const Scale *s = &SCALES[scale];
    int flats = key_uses_flats(root, scale);
    int target = (root + s->steps[k]) % 12;
    /* the letter comes from the degree: k for 7-note scales, otherwise from the degree
     * name (pentatonic and blues: b3 -> 3rd letter, b5 -> 5th letter) */
    static const int degree_of_semitone[12] = {0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6};
    int degree = s->n == 7 ? k : degree_of_semitone[s->steps[k] % 12];
    int l0 = (int)(strchr("CDEFGAB", pitch_name(root, flats)[0]) - "CDEFGAB");
    int letter = (l0 + degree) % 7;
    int acc = ((target - letter_pc[letter] + 6) % 12 + 12) % 12 - 6;
    snprintf(buf, n, "%c%s", "CDEFGAB"[letter],
             acc == -2 ? "bb" : acc == -1 ? "b" : acc == 1 ? "#" : acc == 2 ? "##" : "");
}

void note_above(int root_pc, int flats, int degree, int semis, char *buf, size_t n)
{
    static const int letter_pc[7] = {0, 2, 4, 5, 7, 9, 11};
    int l0 = (int)(strchr("CDEFGAB", pitch_name(root_pc, flats)[0]) - "CDEFGAB");
    int letter = (l0 + degree) % 7, target = ((root_pc + semis) % 12 + 12) % 12;
    int acc = ((target - letter_pc[letter] + 6) % 12 + 12) % 12 - 6;
    snprintf(buf, n, "%c%s", "CDEFGAB"[letter],
             acc == -2 ? "bb" : acc == -1 ? "b" : acc == 1 ? "#" : acc == 2 ? "##" : "");
}

void diatonic_chord_name(int root, int scale, int degree, int type, char *buf, size_t n)
{
    char note[8];
    scale_note_name(root, scale, degree, note, sizeof note);
    snprintf(buf, n, "%s%s", note, CHORDS[type].suffix);
}

void scale_degree_name(int scale, int k, char *buf, size_t n)
{
    static const char *by_semitone[12] = {"1", "b2", "2", "b3", "3", "4", "b5", "5", "b6", "6", "b7", "7"};
    const Scale *s = &SCALES[scale];
    if (s->n != 7) {
        snprintf(buf, n, "%s", by_semitone[s->steps[k] % 12]);
        return;
    }
    int acc = s->steps[k] - MAJOR[k];
    snprintf(buf, n, "%s%d", acc == -2 ? "bb" : acc == -1 ? "b" : acc == 1 ? "#" : acc == 2 ? "##" : "", k + 1);
}

int chord_notes(int root_pc, int type, int base_midi, int *out)
{
    const ChordType *c = &CHORDS[type];
    for (int i = 0; i < c->n; i++) out[i] = base_midi + ((root_pc % 12) + 12) % 12 + c->iv[i];
    return c->n;
}

void chord_tone_names(int root_pc, int type, int flats, char *buf, size_t n)
{
    static const int letter_pc[7] = {0, 2, 4, 5, 7, 9, 11};
    const ChordType *c = &CHORDS[type];
    int l0 = (int)(strchr("CDEFGAB", pitch_name(root_pc, flats)[0]) - "CDEFGAB");
    const char *f = c->formula;
    int len = 0;
    buf[0] = 0;
    for (int i = 0; i < c->n && *f; i++) {      /* each formula token names a degree: "b3" -> 3rd */
        while (*f == ' ') f++;
        while (*f == 'b' || *f == '#') f++;
        int deg = 0;
        while (*f >= '0' && *f <= '9') deg = deg * 10 + (*f++ - '0');
        int letter = (l0 + deg - 1) % 7;
        int target = ((root_pc + c->iv[i]) % 12 + 12) % 12;
        int acc = ((target - letter_pc[letter] + 6) % 12 + 12) % 12 - 6;
        if (len < 0 || (size_t)len >= n) break;
        len += snprintf(buf + len, n - len, "%s%c%s", i ? " " : "", "CDEFGAB"[letter],
                        acc == -2 ? "bb" : acc == -1 ? "b" : acc == 1 ? "#" : acc == 2 ? "##" : "");
    }
}

void chord_name(int root_pc, int type, int flats, char *buf, size_t n)
{
    snprintf(buf, n, "%s%s", pitch_name(root_pc, flats), CHORDS[type].suffix);
}

int diatonic_chord(int root, int scale, int degree, int sevenths, int *type, char *numeral, size_t nlen)
{
    const Scale *s = &SCALES[scale];
    if (s->n != 7) return -1;
    int iv[4], k = sevenths ? 4 : 3;
    for (int i = 0; i < k; i++) {
        int d = degree + 2 * i;
        iv[i] = s->steps[d % 7] + 12 * (d / 7) - s->steps[degree];
    }
    *type = -1;
    for (int t = 0; t < CH_COUNT; t++) {
        if (CHORDS[t].n != k) continue;
        int same = 1;
        for (int i = 0; i < k; i++) same &= CHORDS[t].iv[i] == iv[i];
        if (same) {
            *type = t;
            break;
        }
    }
    if (*type < 0) return -1;

    static const char *upper[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
    static const char *lower[7] = {"i", "ii", "iii", "iv", "v", "vi", "vii"};
    int acc = s->steps[degree] - MAJOR[degree];
    const char *pre = acc < 0 ? "b" : acc > 0 ? "#" : "";
    const char *roman = iv[1] == 4 ? upper[degree] : lower[degree];
    const char *suf = "";
    switch (*type) {
    case CH_DIM: suf = "°"; break;          /* degree sign */
    case CH_AUG: suf = "+"; break;
    case CH_MAJ7: suf = "maj7"; break;
    case CH_DOM7: case CH_MIN7: suf = "7"; break;
    case CH_M7B5: suf = "ø7"; break;        /* o-slash: half-diminished */
    case CH_DIM7: suf = "°7"; break;
    case CH_MMAJ7: suf = "(maj7)"; break;
    case CH_AUGMAJ7: suf = "+maj7"; break;
    }
    snprintf(numeral, nlen, "%s%s%s", pre, roman, suf);
    return (root + s->steps[degree]) % 12;
}
