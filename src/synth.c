/* tinydaw: the Generator's synth and its genre presets.
 *
 * One voice: up to seven detuned copies of a main oscillator, a second oscillator a set interval
 * away, a sub-octave sine and noise, through a resonant filter with its own envelope and LFO, then
 * saturation, an ADSR and optional bit/rate crushing. Pitch can fall from above (808s, kicks,
 * zaps) or sweep over the sound (risers). The result is normalised, so presets don't need level
 * matching and the Volume control means what it says.
 *
 * The five friendly controls (brightness, softness, thickness, grit, wobble) bend a preset's
 * patch rather than replace it: at 0.5 each one leaves the preset exactly as designed. */
#include "daw.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846

const char *const SYNTH_GENRES[] = {"House", "Techno", "Hip-hop", "Trap", "Lo-fi", "Drum & bass", "Dubstep",
                                    "Synthwave", "Pop", "R&B", "Ambient", "Chiptune"};
const int NSYNTH_GENRES = (int)(sizeof SYNTH_GENRES / sizeof SYNTH_GENRES[0]);
const char *const SYNTH_KINDS[] = {"Bass", "Lead", "Pad", "Keys", "Pluck", "Drums", "FX"};
const int NSYNTH_KINDS = (int)(sizeof SYNTH_KINDS / sizeof SYNTH_KINDS[0]);
const char *const MACRO_NAMES[MACROS][3] = {
    {"Brightness", "dark", "bright"}, {"Softness", "snappy", "smooth"}, {"Thickness", "thin", "fat"},
    {"Grit", "clean", "dirty"},       {"Wobble", "still", "wobbly"}};

#define PRESET(nm, ge, ki, ab, nt, sec, ...) {nm, ge, ki, ab, {__VA_ARGS__}, nt, sec}

const SynthPreset SYNTH_PRESETS[] = {
    /* ---- House */
    PRESET("Deep bass", "House", "Bass", "Warm, round low end that bounces under the kick", 36, 0.6,
           .wave = SW_SINE, .wave2 = SW_SAW, .mix2 = 0.3, .filter = SF_LOW, .cutoff = 500, .fenv = 1.6,
           .fdecay = 0.15, .attack = 0.004, .decay = 0.3, .sustain = 0.55, .release = 0.08, .drive = 0.2),
    PRESET("Piano stab", "House", "Keys", "Bright, percussive piano for house chords", 60, 1.4,
           .wave = SW_SINE, .wave2 = SW_SINE, .semi2 = 12, .mix2 = 0.25, .fm = 1.6, .fm_ratio = 1, .fm_decay = 0.35,
           .attack = 0.002, .decay = 0.9, .sustain = 0.15, .release = 0.3),
    PRESET("Chord stab", "House", "Pluck", "Short, wide synth hit that cuts through", 60, 0.5,
           .wave = SW_SAW, .voices = 5, .detune = 14, .width = 0.8, .filter = SF_LOW, .cutoff = 1100, .fenv = 2,
           .fdecay = 0.18, .attack = 0.002, .decay = 0.25, .sustain = 0, .release = 0.15),
    PRESET("Floor kick", "House", "Drums", "Punchy dance kick with a short click on top", 33, 0.45,
           .wave = SW_SINE, .pitch_drop = 30, .pitch_time = 0.035, .noise = 0.12, .noise_decay = 0.006,
           .attack = 0, .decay = 0.3, .sustain = 0, .release = 0.04, .drive = 0.2),
    PRESET("Open hat", "House", "Drums", "Airy off-beat hi-hat", 60, 0.35,
           .noosc = 1, .noise = 1, .filter = SF_HIGH, .cutoff = 7500, .reso = 0.2, .attack = 0, .decay = 0.28,
           .sustain = 0, .release = 0.05),

    /* ---- Techno */
    PRESET("Acid line", "Techno", "Bass", "Squelchy bass: turn up brightness to scream", 36,
           0.45, .wave = SW_SAW, .filter = SF_LOW, .cutoff = 450, .reso = 0.85, .fenv = 3, .fdecay = 0.18,
           .attack = 0.002, .decay = 0.3, .sustain = 0.5, .release = 0.05, .drive = 0.4),
    PRESET("Rumble", "Techno", "Bass", "Dark, distorted rumble to fill out the kick", 33, 0.8,
           .wave = SW_SINE, .noise = 0.05, .filter = SF_LOW, .cutoff = 280, .reso = 0.3, .attack = 0.002,
           .decay = 0.45, .sustain = 0.3, .release = 0.25, .drive = 0.65),
    PRESET("Hypnotic stab", "Techno", "Pluck", "Hollow, repeating stab for looping hooks", 48, 0.35,
           .wave = SW_SQUARE, .pw = 0.3, .filter = SF_LOW, .cutoff = 850, .reso = 0.6, .fenv = 2.5, .fdecay = 0.12,
           .attack = 0.001, .decay = 0.2, .sustain = 0, .release = 0.1),
    PRESET("Industrial kick", "Techno", "Drums", "Hard, saturated kick", 33, 0.5,
           .wave = SW_SINE, .pitch_drop = 36, .pitch_time = 0.04, .noise = 0.15, .noise_decay = 0.01,
           .attack = 0, .decay = 0.35, .sustain = 0, .release = 0.05, .drive = 0.8),
    PRESET("Laser sweep", "Techno", "FX", "Resonant sweep for builds and transitions", 48, 2.0,
           .wave = SW_SAW, .voices = 3, .detune = 10, .width = 0.6, .filter = SF_LOW, .cutoff = 200, .reso = 0.8,
           .fsweep = 5, .attack = 0.3, .decay = 0, .sustain = 1, .release = 0.3),

    /* ---- Hip-hop */
    PRESET("Boom bap kick", "Hip-hop", "Drums", "Thick, dusty kick for head-nod beats", 31, 0.4,
           .wave = SW_SINE, .pitch_drop = 28, .pitch_time = 0.03, .noise = 0.2, .noise_decay = 0.008,
           .filter = SF_LOW, .cutoff = 3500, .attack = 0, .decay = 0.26, .sustain = 0, .release = 0.05,
           .drive = 0.3),
    PRESET("Dusty snare", "Hip-hop", "Drums", "Crunchy snare with a bit of tone", 54, 0.3,
           .wave = SW_TRI, .pitch_drop = 7, .pitch_time = 0.02, .noise = 1.2, .noise_decay = 0.12,
           .filter = SF_BAND, .cutoff = 2400, .reso = 0.15, .attack = 0, .decay = 0.14, .sustain = 0,
           .release = 0.05, .crush = 0.3),
    PRESET("Soul keys", "Hip-hop", "Keys", "Mellow electric piano for samples-style chords", 60, 2.0,
           .wave = SW_SINE, .wave2 = SW_SINE, .semi2 = 12, .mix2 = 0.15, .fm = 0.9, .fm_ratio = 1, .fm_decay = 1.0,
           .vibrato = 5, .lfo_hz = 4.5, .attack = 0.004, .decay = 1.4, .sustain = 0.3, .release = 0.4,
           .drive = 0.15),
    PRESET("Upright bass", "Hip-hop", "Bass", "Soft, plucked jazz bass", 40, 0.8,
           .wave = SW_TRI, .wave2 = SW_SINE, .mix2 = 0.5, .filter = SF_LOW, .cutoff = 900, .fenv = 1.2,
           .fdecay = 0.08, .attack = 0.004, .decay = 0.45, .sustain = 0.35, .release = 0.12),

    /* ---- Trap */
    PRESET("808", "Trap", "Bass", "Long, booming 808 bass with a punchy start", 36, 1.8,
           .wave = SW_SINE, .pitch_drop = 12, .pitch_time = 0.05, .attack = 0.002, .decay = 1.4, .sustain = 0.35,
           .release = 0.3, .drive = 0.45),
    PRESET("Trap hat", "Trap", "Drums", "Tight, crisp hi-hat for fast rolls", 60, 0.12,
           .noosc = 1, .noise = 1, .filter = SF_HIGH, .cutoff = 8000, .reso = 0.25, .attack = 0, .decay = 0.045,
           .sustain = 0, .release = 0.02),
    PRESET("Clap", "Trap", "Drums", "Layered hand clap", 60, 0.45,
           .noosc = 1, .noise = 1, .noise_decay = 0.12, .bursts = 4, .filter = SF_BAND, .cutoff = 1400, .reso = 0.3,
           .attack = 0, .decay = 0.25, .sustain = 0, .release = 0.1),
    PRESET("Dark bell", "Trap", "Lead", "Eerie metallic bell for dark melodies", 72, 1.6,
           .wave = SW_SINE, .fm = 2.2, .fm_ratio = 3.5, .fm_decay = 0.7, .attack = 0.002, .decay = 1.2,
           .sustain = 0.1, .release = 0.4),
    PRESET("Flute lead", "Trap", "Lead", "Breathy lead for simple top-line melodies", 72, 1.2,
           .wave = SW_TRI, .noise = 0.06, .vibrato = 18, .lfo_hz = 5, .filter = SF_LOW, .cutoff = 3000,
           .attack = 0.06, .decay = 0.2, .sustain = 0.8, .release = 0.2),

    /* ---- Lo-fi */
    PRESET("Lo-fi keys", "Lo-fi", "Keys", "Warbly, worn-out electric piano", 60, 2.0,
           .wave = SW_SINE, .fm = 0.7, .fm_ratio = 1, .fm_decay = 0.8, .vibrato = 14, .lfo_hz = 3.2,
           .filter = SF_LOW, .cutoff = 2400, .attack = 0.008, .decay = 1.1, .sustain = 0.35, .release = 0.5,
           .crush = 0.35),
    PRESET("Tape pad", "Lo-fi", "Pad", "Soft, wobbling pad like an old cassette", 60, 4.0,
           .wave = SW_SAW, .voices = 3, .detune = 10, .width = 0.7, .vibrato = 10, .lfo_hz = 0.8, .filter = SF_LOW,
           .cutoff = 850, .attack = 0.6, .decay = 1, .sustain = 0.8, .release = 1.2, .crush = 0.25),
    PRESET("Lo-fi kick", "Lo-fi", "Drums", "Muffled, soft kick", 31, 0.35,
           .wave = SW_SINE, .pitch_drop = 22, .pitch_time = 0.035, .filter = SF_LOW, .cutoff = 1400,
           .attack = 0, .decay = 0.24, .sustain = 0, .release = 0.05, .crush = 0.4),
    PRESET("Vinyl crackle", "Lo-fi", "FX", "Quiet hiss and pops to lay under a beat", 60, 4.0,
           .noosc = 1, .noise = 1, .filter = SF_BAND, .cutoff = 3000, .reso = 0.1, .attack = 0.2, .decay = 0,
           .sustain = 1, .release = 0.3, .crush = 0.7),

    /* ---- Drum & bass */
    PRESET("Reese", "Drum & bass", "Bass", "Moody, phasing bass made of detuned saws", 33, 2.0,
           .wave = SW_SAW, .voices = 3, .detune = 18, .width = 0.5, .filter = SF_LOW, .cutoff = 650, .reso = 0.3,
           .wobble = 0.5, .lfo_hz = 0.3, .attack = 0.01, .decay = 0, .sustain = 1, .release = 0.12, .drive = 0.4),
    PRESET("Rolling sub", "Drum & bass", "Bass", "Clean sub bass you feel more than hear", 36, 1.0,
           .wave = SW_SINE, .attack = 0.005, .decay = 0, .sustain = 1, .release = 0.08),
    PRESET("Neuro growl", "Drum & bass", "Bass", "Aggressive, talking mid bass", 36, 1.2,
           .wave = SW_SAW, .wave2 = SW_SQUARE, .mix2 = 0.6, .fm = 0.8, .fm_ratio = 0.5, .filter = SF_LOW,
           .cutoff = 500, .reso = 0.6, .wobble = 3, .lfo_hz = 6, .attack = 0.005, .decay = 0, .sustain = 1,
           .release = 0.1, .drive = 0.8),
    PRESET("Break snare", "Drum & bass", "Drums", "Snappy, bright snare for fast breaks", 58, 0.25,
           .wave = SW_TRI, .pitch_drop = 5, .pitch_time = 0.02, .noise = 0.9, .noise_decay = 0.1, .attack = 0,
           .decay = 0.1, .sustain = 0, .release = 0.05, .drive = 0.3),

    /* ---- Dubstep */
    PRESET("Wobble bass", "Dubstep", "Bass", "The classic wub: a filter opening and closing", 31, 2.0,
           .wave = SW_SAW, .voices = 3, .detune = 12, .width = 0.5, .wave2 = SW_SQUARE, .semi2 = -12, .mix2 = 0.6,
           .filter = SF_LOW, .cutoff = 280, .reso = 0.7, .wobble = 4, .lfo_hz = 2.33, .attack = 0.005, .decay = 0,
           .sustain = 1, .release = 0.1, .drive = 0.6),
    PRESET("Screech", "Dubstep", "Lead", "Harsh, metallic lead for drops", 60, 1.0,
           .wave = SW_SQUARE, .pw = 0.15, .fm = 2, .fm_ratio = 2, .filter = SF_LOW, .cutoff = 2800, .reso = 0.6,
           .wobble = 2, .lfo_hz = 8, .attack = 0.005, .decay = 0, .sustain = 1, .release = 0.08, .drive = 0.7),
    PRESET("Sub drop", "Dubstep", "FX", "Deep falling boom to land a drop", 28, 2.5,
           .wave = SW_SINE, .pitch_drop = 24, .pitch_time = 0.6, .attack = 0.01, .decay = 2, .sustain = 0,
           .release = 0.4, .drive = 0.2),

    /* ---- Synthwave */
    PRESET("Retro lead", "Synthwave", "Lead", "Singing 80s lead with a gentle vibrato", 72, 1.4,
           .wave = SW_SAW, .voices = 2, .detune = 8, .width = 0.4, .wave2 = SW_SQUARE, .semi2 = 12, .mix2 = 0.3,
           .vibrato = 15, .lfo_hz = 5.5, .filter = SF_LOW, .cutoff = 3400, .reso = 0.2, .attack = 0.01,
           .decay = 0.3, .sustain = 0.7, .release = 0.4),
    PRESET("Analog pad", "Synthwave", "Pad", "Lush, slowly opening pad", 60, 4.0,
           .wave = SW_SAW, .voices = 5, .detune = 20, .width = 1, .filter = SF_LOW, .cutoff = 1300, .fenv = 0.8,
           .fdecay = 1.5, .attack = 0.8, .decay = 1, .sustain = 0.8, .release = 1.5),
    PRESET("Arp pluck", "Synthwave", "Pluck", "Bouncy pluck made for arpeggios", 60, 0.45,
           .wave = SW_SQUARE, .pw = 0.4, .voices = 2, .detune = 6, .width = 0.5, .filter = SF_LOW, .cutoff = 900,
           .reso = 0.4, .fenv = 3, .fdecay = 0.15, .attack = 0.001, .decay = 0.25, .sustain = 0, .release = 0.15),
    PRESET("Drum machine tom", "Synthwave", "Drums", "Big electronic tom for fills", 50, 0.6,
           .wave = SW_SINE, .pitch_drop = 7, .pitch_time = 0.1, .noise = 0.1, .noise_decay = 0.02, .attack = 0,
           .decay = 0.45, .sustain = 0, .release = 0.1),
    PRESET("Synth bass", "Synthwave", "Bass", "Driving, pulsing bass for 8th-note lines", 36, 0.35,
           .wave = SW_SAW, .sub = 0.4, .filter = SF_LOW, .cutoff = 700, .reso = 0.35, .fenv = 1.5, .fdecay = 0.1,
           .attack = 0.002, .decay = 0.2, .sustain = 0.4, .release = 0.06),

    /* ---- Pop */
    PRESET("Supersaw", "Pop", "Lead", "Huge, wide lead for big choruses", 60, 1.5,
           .wave = SW_SAW, .voices = 7, .detune = 25, .width = 1, .filter = SF_LOW, .cutoff = 6000, .attack = 0.01,
           .decay = 0.2, .sustain = 0.8, .release = 0.4),
    PRESET("Pop pluck", "Pop", "Pluck", "Clean, sparkly pluck for catchy hooks", 72, 0.6,
           .wave = SW_TRI, .wave2 = SW_SAW, .semi2 = 12, .mix2 = 0.3, .filter = SF_LOW, .cutoff = 2000, .fenv = 2,
           .fdecay = 0.1, .attack = 0.001, .decay = 0.3, .sustain = 0, .release = 0.2),
    PRESET("Bright piano", "Pop", "Keys", "Clear piano-like keys for ballads and chords", 60, 2.0,
           .wave = SW_SINE, .wave2 = SW_SINE, .semi2 = 12, .mix2 = 0.3, .fm = 2, .fm_ratio = 1, .fm_decay = 0.3,
           .attack = 0.001, .decay = 1.6, .sustain = 0, .release = 0.3),
    PRESET("Snap", "Pop", "Drums", "Finger snap for verses", 60, 0.2,
           .noosc = 1, .noise = 1, .noise_decay = 0.03, .filter = SF_BAND, .cutoff = 2200, .reso = 0.4, .attack = 0,
           .decay = 0.08, .sustain = 0, .release = 0.04),

    /* ---- R&B */
    PRESET("Smooth keys", "R&B", "Keys", "Silky electric piano for slow jams", 60, 2.5,
           .wave = SW_SINE, .fm = 0.5, .fm_ratio = 1, .fm_decay = 2, .vibrato = 6, .lfo_hz = 5, .attack = 0.01,
           .decay = 2, .sustain = 0.5, .release = 0.6),
    PRESET("Round bass", "R&B", "Bass", "Smooth bass that stays out of the vocal's way", 36, 0.8,
           .wave = SW_SINE, .wave2 = SW_TRI, .mix2 = 0.3, .filter = SF_LOW, .cutoff = 500, .attack = 0.02,
           .decay = 0.5, .sustain = 0.7, .release = 0.2),
    PRESET("Silk pad", "R&B", "Pad", "Soft, warm pad to sit behind chords", 60, 4.0,
           .wave = SW_TRI, .voices = 4, .detune = 12, .width = 0.9, .filter = SF_LOW, .cutoff = 2000, .attack = 1,
           .decay = 1, .sustain = 0.9, .release = 2),
    PRESET("Rimshot", "R&B", "Drums", "Woody click for laid-back grooves", 76, 0.15,
           .wave = SW_TRI, .noise = 0.4, .noise_decay = 0.015, .filter = SF_BAND, .cutoff = 1800, .reso = 0.3,
           .attack = 0, .decay = 0.04, .sustain = 0, .release = 0.02),

    /* ---- Ambient */
    PRESET("Shimmer pad", "Ambient", "Pad", "Glowing, airy pad that drifts", 60, 6.0,
           .wave = SW_SAW, .voices = 6, .detune = 15, .width = 1, .wave2 = SW_SINE, .semi2 = 19, .mix2 = 0.3,
           .filter = SF_LOW, .cutoff = 2400, .wobble = 0.5, .lfo_hz = 0.2, .attack = 1.5, .decay = 1, .sustain = 0.9,
           .release = 2.5),
    PRESET("Glass bell", "Ambient", "Keys", "Clear, ringing bell with a long tail", 84, 4.0,
           .wave = SW_SINE, .fm = 1.5, .fm_ratio = 2.76, .fm_decay = 2, .attack = 0.002, .decay = 3, .sustain = 0,
           .release = 1),
    PRESET("Wind", "Ambient", "FX", "Gusting noise, like wind through trees", 60, 5.0,
           .noosc = 1, .noise = 1, .filter = SF_BAND, .cutoff = 800, .reso = 0.7, .wobble = 1.5, .lfo_hz = 0.25,
           .attack = 1, .decay = 0, .sustain = 1, .release = 1.5),
    PRESET("Drone", "Ambient", "Pad", "Low, slowly moving drone to build on", 36, 8.0,
           .wave = SW_SAW, .voices = 3, .detune = 5, .width = 0.6, .wave2 = SW_SAW, .semi2 = 7, .mix2 = 0.5,
           .filter = SF_LOW, .cutoff = 400, .reso = 0.4, .wobble = 0.8, .lfo_hz = 0.1, .attack = 2, .decay = 0,
           .sustain = 1, .release = 2),

    /* ---- Chiptune */
    PRESET("Square lead", "Chiptune", "Lead", "Game-console lead with a quick vibrato", 72, 0.8,
           .wave = SW_SQUARE, .vibrato = 20, .lfo_hz = 6, .attack = 0, .decay = 0.1, .sustain = 0.8, .release = 0.05,
           .crush = 0.5),
    PRESET("Pulse bass", "Chiptune", "Bass", "Thin, buzzy 8-bit bass", 36, 0.5,
           .wave = SW_SQUARE, .pw = 0.25, .attack = 0, .decay = 0.2, .sustain = 0.6, .release = 0.03, .crush = 0.5),
    PRESET("Noise drum", "Chiptune", "Drums", "Crunchy 8-bit snare", 60, 0.2,
           .noosc = 1, .noise = 1, .filter = SF_HIGH, .cutoff = 1500, .attack = 0, .decay = 0.08, .sustain = 0,
           .release = 0.03, .crush = 0.7),
    PRESET("Coin", "Chiptune", "FX", "Bright pickup blip", 88, 0.4,
           .wave = SW_SQUARE, .pitch_drop = -5, .pitch_time = 0.03, .attack = 0, .decay = 0.35, .sustain = 0,
           .release = 0.05, .crush = 0.5),
    PRESET("Laser zap", "Chiptune", "FX", "Falling zap for hits and jumps", 60, 0.35,
           .wave = SW_SQUARE, .pitch_drop = 36, .pitch_time = 0.08, .attack = 0, .decay = 0.3, .sustain = 0,
           .release = 0.05, .crush = 0.3),

    /* ---- for everything */
    PRESET("Riser", "Pop", "FX", "Rising whoosh that builds into a drop", 48, 4.0,
           .wave = SW_SAW, .voices = 5, .detune = 20, .width = 1, .noise = 0.5, .psweep = 24, .filter = SF_LOW,
           .cutoff = 300, .fsweep = 4, .attack = 3, .decay = 0, .sustain = 1, .release = 0.1),
    PRESET("Impact", "Techno", "FX", "Heavy boom for the first beat of a section", 28, 3.0,
           .wave = SW_SINE, .pitch_drop = 24, .pitch_time = 0.15, .noise = 0.6, .noise_decay = 0.3, .filter = SF_LOW,
           .cutoff = 3000, .fsweep = -3, .attack = 0, .decay = 2.5, .sustain = 0, .release = 0.4, .drive = 0.5),
};
const int NSYNTH_PRESETS = (int)(sizeof SYNTH_PRESETS / sizeof SYNTH_PRESETS[0]);

void synth_from_preset(SynthSound *s, int preset)
{
    if (preset < 0 || preset >= NSYNTH_PRESETS) preset = 0;
    const SynthPreset *p = &SYNTH_PRESETS[preset];
    memset(s, 0, sizeof *s);
    s->patch = p->patch;
    for (int i = 0; i < MACROS; i++) s->macro[i] = 0.5;
    s->note = p->note;
    s->seconds = p->seconds;
    s->volume = 0.8;
}

static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

/* The preset's patch bent by the friendly controls. */
static void shape(const SynthSound *s, SynthPatch *q)
{
    *q = s->patch;
    double d;
    d = clampd(s->macro[MAC_BRIGHT], 0, 1) - 0.5;          /* brightness: the cutoff, +/- 2.5 octaves */
    if (q->filter != SF_NONE && q->cutoff > 0) {
        q->cutoff *= pow(2, d * 5);
    } else if (d < 0) {
        q->filter = SF_LOW;
        q->cutoff = 18000 * pow(2, d * 8);
    }
    if (d > 0 && !q->noosc && q->wave == SW_SINE) {   /* a sine has no high end to let through: add some */
        if (q->fm <= 0) {
            q->fm_ratio = 1;
            q->fm_decay = 0;
        }
        q->fm += d * 2.4;
    }
    d = clampd(s->macro[MAC_SOFT], 0, 1) - 0.5;            /* softness: slower start, longer tails */
    double k = pow(2, d * 2);
    q->attack = q->attack * pow(2, d * 4) + (d > 0 ? d * d * 1.6 : 0);
    q->release = q->release * pow(2, d * 3) + (d > 0 ? d * 0.4 : 0);
    q->decay *= k;
    q->noise_decay *= k;
    q->pitch_time *= k;
    q->fdecay *= k;
    d = clampd(s->macro[MAC_THICK], 0, 1) - 0.5;           /* thickness: more, wider copies and sub */
    if (d > 0) {
        if (q->voices <= 1 && d > 0.1) q->voices = 3;
        q->detune = q->detune * pow(2, d * 2) + d * 16;
        q->width = clampd(q->width + d * 1.2, 0, 1);
        q->sub = clampd(q->sub + d * 0.8, 0, 1);
    } else {
        if (d < -0.25 && q->voices > 1) q->voices = (q->voices + 1) / 2;
        q->detune *= 1 + 2 * d;
        q->width *= 1 + 2 * d;
        q->sub *= 1 + 2 * d;
    }
    d = clampd(s->macro[MAC_GRIT], 0, 1) - 0.5;            /* grit: saturation, then crushing */
    q->drive = clampd(q->drive + d * 1.4, 0, 1);
    if (d > 0.3) q->crush = clampd(q->crush + (d - 0.3) * 2.5, 0, 1);
    if (d < 0) q->crush *= 1 + 2 * d;
    d = clampd(s->macro[MAC_WOBBLE], 0, 1) - 0.5;          /* wobble: the filter LFO */
    if (d > 0) {
        if (q->filter == SF_NONE || q->cutoff <= 0) {
            q->filter = SF_LOW;
            q->cutoff = 900;
        }
        if (q->lfo_hz <= 0) q->lfo_hz = 2.5;
        q->wobble += d * 6;
    } else {
        q->wobble *= 1 + 2 * d;
        q->vibrato *= 1 + 2 * d;
    }
}

static double blep(double t, double dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt) {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

static double osc(int wave, double p, double dt, double pw)
{
    switch (wave) {
    case SW_SAW: return (2 * p - 1) - blep(p, dt);
    case SW_SQUARE: {
        double q = p + 1 - pw;
        q -= floor(q);
        return (p < pw ? 1.0 : -1.0) + blep(p, dt) - blep(q, dt) - (2 * pw - 1);   /* centred */
    }
    case SW_TRI: return 1 - 4 * fabs(p - 0.5);
    default: return sin(2 * PI * p);
    }
}

static uint32_t synth_rng;
static double white(void)
{
    synth_rng ^= synth_rng << 13;
    synth_rng ^= synth_rng >> 17;
    synth_rng ^= synth_rng << 5;
    return synth_rng / 2147483647.5 - 1.0;
}

float *render_synth(const SynthSound *s, long *frames)
{
    SynthPatch q;
    shape(s, &q);
    double secs = clampd(s->seconds, 0.05, 30);
    long n = (long)(secs * SR);
    float *d = calloc((size_t)n * 2, sizeof(float));
    if (!d) return NULL;
    synth_rng = 0x2545F491u;                     /* the same sound every time it's made */

    double base = (s->a4 > 0 ? s->a4 : 440.0) * pow(2.0, (s->note - 69) / 12.0);
    int nv = q.voices < 1 ? 1 : q.voices > 7 ? 7 : q.voices;
    double ph[7], ph2[7], ratio[7], gl[7], gr[7], phs = 0, phm = 0;
    for (int v = 0; v < nv; v++) {
        double spread = nv > 1 ? (double)v / (nv - 1) * 2 - 1 : 0;
        ratio[v] = pow(2.0, q.detune * spread / 1200.0);
        double pan = clampd(spread * q.width, -1, 1);
        gl[v] = sqrt(0.5 * (1 - pan)) / sqrt(nv);
        gr[v] = sqrt(0.5 * (1 + pan)) / sqrt(nv);
        ph[v] = nv > 1 ? fmod(v * 0.618034, 1.0) : 0;
        ph2[v] = fmod(v * 0.381966 + 0.25, 1.0);
    }
    double pw = q.pw > 0.02 && q.pw < 0.98 ? q.pw : 0.5, ratio2 = pow(2.0, q.semi2 / 12.0);

    long att = (long)(clampd(q.attack, 0, 20) * SR), rel = (long)(clampd(q.release, 0, 20) * SR);
    if (att > n * 0.9) att = (long)(n * 0.9);
    if (rel > n - att) rel = n - att;
    long gate = n - rel;
    double sus = clampd(q.sustain, 0, 1), gate_lvl = 0;
    double dtc = q.decay > 0 ? q.decay / 4 : 0;  /* decay: close to sustain by `decay` seconds */

    int filt = q.filter != SF_NONE && q.cutoff > 0;
    double kq = 2 - 1.96 * clampd(q.reso, 0, 1), a1 = 0, a2 = 0, a3 = 0;
    double s1[2] = {0, 0}, s2[2] = {0, 0};
    double drive_k = 1 + q.drive * 8, drive_n = tanh(drive_k);
    int hold = 1 + (int)(q.crush * q.crush * 14 * SR / 48000.0);
    double levels = pow(2.0, 14 - 10 * q.crush), held[2] = {0, 0};
    double noise_lvl = q.noise, peak = 0;
    double dc_k = exp(-2 * PI * 8.0 / SR), dc_in[2] = {0, 0}, dc_out[2] = {0, 0};   /* DC blocker at 8 Hz */
    int bursts = q.bursts < 1 ? 1 : q.bursts > 8 ? 8 : q.bursts;

    for (long i = 0; i < n; i++) {
        double t = (double)i / SR;
        double lfo = sin(2 * PI * q.lfo_hz * t);
        double semis = 0;
        if (q.pitch_drop != 0 && q.pitch_time > 0) semis += q.pitch_drop * exp(-t / q.pitch_time);
        semis += q.psweep * t / secs;
        if (q.vibrato > 0) semis += q.vibrato / 100.0 * lfo * fmin(1, t / 0.25);
        double f = base * pow(2.0, semis / 12.0);
        if (f > SR * 0.45) f = SR * 0.45;
        double dt = f / SR;

        double pm = 0;                           /* phase modulation, in cycles */
        if (q.fm > 0) {
            double idx = q.fm * (q.fm_decay > 0 ? exp(-t / q.fm_decay) : 1);
            phm += dt * (q.fm_ratio > 0 ? q.fm_ratio : 1);
            phm -= floor(phm);
            pm = idx * sin(2 * PI * phm) / (2 * PI);
        }
        double L = 0, R = 0;
        if (!q.noosc) {
            for (int v = 0; v < nv; v++) {
                double dv = dt * ratio[v], p = ph[v] + pm;
                p -= floor(p);
                double x = osc(q.wave, p, dv, pw);
                if (q.mix2 > 0) {
                    double p2 = ph2[v] + pm;
                    p2 -= floor(p2);
                    x += q.mix2 * osc(q.wave2, p2, dv * ratio2, pw);
                    ph2[v] += dv * ratio2;
                    ph2[v] -= floor(ph2[v]);
                }
                L += x * gl[v];
                R += x * gr[v];
                ph[v] += dv;
                ph[v] -= floor(ph[v]);
            }
        }
        double mono = 0;
        if (q.sub > 0) {
            mono += q.sub * sin(2 * PI * phs);
            phs += dt / 2;
            phs -= floor(phs);
        }
        if (noise_lvl > 0) {
            double tn = t;
            for (int b = bursts - 1; b > 0; b--)
                if (t >= b * 0.011) {
                    tn = t - b * 0.011;
                    break;
                }
            mono += noise_lvl * white() * (q.noise_decay > 0 ? exp(-tn / q.noise_decay) : 1);
        }
        L += mono;
        R += mono;

        if (filt) {
            if ((i & 7) == 0) {
                double oct = q.fsweep * t / secs + q.wobble * lfo;
                if (q.fenv != 0 && q.fdecay > 0) oct += q.fenv * exp(-t / (q.fdecay / 3));
                double fc = clampd(q.cutoff * pow(2.0, oct), 20, SR * 0.45);
                double g = tan(PI * fc / SR);
                a1 = 1 / (1 + g * (g + kq));
                a2 = g * a1;
                a3 = g * a2;
            }
            double io[2] = {L, R};
            for (int c = 0; c < 2; c++) {
                double v3 = io[c] - s2[c], v1 = a1 * s1[c] + a2 * v3, v2 = s2[c] + a2 * s1[c] + a3 * v3;
                s1[c] = 2 * v1 - s1[c];
                s2[c] = 2 * v2 - s2[c];
                io[c] = q.filter == SF_LOW ? v2 : q.filter == SF_HIGH ? io[c] - kq * v1 - v2 : v1 * kq;
            }
            L = io[0];
            R = io[1];
        }
        if (q.drive > 0) {
            L = tanh(L * drive_k) / drive_n;
            R = tanh(R * drive_k) / drive_n;
        }

        double env;                              /* attack, decay to sustain, release to silence */
        if (i < att) env = (double)i / att;
        else if (i < gate) env = dtc > 0 ? sus + (1 - sus) * exp(-(t - (double)att / SR) / dtc) : sus;
        else {
            double r = rel > 0 ? 1 - (double)(i - gate) / rel : 0;
            env = gate_lvl * r * r;
        }
        if (i < gate) gate_lvl = env;            /* where the release starts from */
        long left = n - 1 - i;                   /* never end on a click */
        if (left < 64) env *= left / 64.0;
        L *= env;
        R *= env;

        if (q.crush > 0) {
            if (i % hold == 0) {
                held[0] = round(L * levels) / levels;
                held[1] = round(R * levels) / levels;
            }
            L = held[0];
            R = held[1];
        }
        dc_out[0] = L - dc_in[0] + dc_k * dc_out[0];
        dc_in[0] = L;
        dc_out[1] = R - dc_in[1] + dc_k * dc_out[1];
        dc_in[1] = R;
        L = dc_out[0];
        R = dc_out[1];
        d[2 * i] = (float)L;
        d[2 * i + 1] = (float)R;
        if (fabs(L) > peak) peak = fabs(L);
        if (fabs(R) > peak) peak = fabs(R);
    }
    if (peak > 1e-9) {                           /* the same loudness for every preset */
        float g = (float)(0.8 * clampd(s->volume, 0, 1) / peak);
        for (long i = 0; i < 2 * n; i++) d[i] *= g;
    }
    *frames = n;
    return d;
}

int sample_generate_synth(Project *p, const SynthSound *s, const char *name)
{
    long n;
    float *d = render_synth(s, &n);
    if (!d) return -1;
    int idx = sample_add(p, name, d, n);
    if (idx >= 0) {
        p->samples[idx].synth = 1;
        p->samples[idx].syn = *s;
    }
    return idx;
}

/* ---- the patch as text, for project files: name=value pairs, zeros left out, so files stay
 * readable and later versions can add fields */

#define PATCH_INTS(X) X(wave) X(noosc) X(wave2) X(voices) X(bursts) X(filter)
#define PATCH_DOUBLES(X)                                                                                 \
    X(semi2) X(mix2) X(detune) X(width) X(pw) X(sub) X(noise) X(noise_decay) X(fm) X(fm_ratio) X(fm_decay) \
    X(pitch_drop) X(pitch_time) X(psweep) X(vibrato) X(lfo_hz) X(cutoff) X(reso) X(fenv) X(fdecay)       \
    X(fsweep) X(wobble) X(attack) X(decay) X(sustain) X(release) X(drive) X(crush)

void synth_patch_text(const SynthPatch *p, char *buf, size_t n)
{
    size_t k = 0;
    buf[0] = 0;
#define PUT_I(f)                                                                     \
    if (p->f && k < n) k += (size_t)snprintf(buf + k, n - k, " " #f "=%d", p->f);
#define PUT_D(f)                                                                     \
    if (p->f != 0 && k < n) k += (size_t)snprintf(buf + k, n - k, " " #f "=%.9g", p->f);
    PATCH_INTS(PUT_I)
    PATCH_DOUBLES(PUT_D)
#undef PUT_I
#undef PUT_D
}

void synth_patch_parse(const char *s, SynthPatch *p)
{
    memset(p, 0, sizeof *p);
    while (*s) {
        while (*s == ' ') s++;
        const char *eq = strchr(s, '=');
        if (!eq) break;
        size_t len = (size_t)(eq - s);
        char *end;
        double v = strtod(eq + 1, &end);
        if (end == eq + 1) break;
#define GET_I(f) if (len == sizeof #f - 1 && !strncmp(s, #f, len)) p->f = (int)v;
#define GET_D(f) if (len == sizeof #f - 1 && !strncmp(s, #f, len)) p->f = v;
        PATCH_INTS(GET_I)
        PATCH_DOUBLES(GET_D)
#undef GET_I
#undef GET_D
        s = end;
    }
    if (p->wave < 0 || p->wave >= SW_COUNT) p->wave = SW_SAW;
    if (p->wave2 < 0 || p->wave2 >= SW_COUNT) p->wave2 = SW_SAW;
    if (p->filter < SF_NONE || p->filter > SF_BAND) p->filter = SF_NONE;
}
