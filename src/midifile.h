/* tinydaw: reading and writing Standard MIDI Files. Notes are in beats (quarter notes). */
#ifndef MIDIFILE_H
#define MIDIFILE_H

#include <stddef.h>

#define MF_MAX_TRACKS 64
#define MF_MAX_METER 64

typedef struct { int note; double start, len; int vel; } MfNote;
typedef struct {
    char name[64];
    int channel;
    int n, cap;
    MfNote *notes;
} MfTrack;
typedef struct { double beat, bpm; int num, den; } MfMeter;   /* a tempo / time signature from `beat` on */
typedef struct {
    double bpm;                 /* the first tempo (0 when the file has none) */
    int num, den, has_meter;    /* the first time signature */
    MfMeter meter[MF_MAX_METER];   /* for writing: every change (nmeter 0 = just bpm, num/den) */
    int nmeter;
    MfTrack tracks[MF_MAX_TRACKS];
    int ntracks;
} MfFile;

int mf_read(const char *path, MfFile *f, char *err, size_t errlen);
int mf_write(const char *path, const MfFile *f, char *err, size_t errlen);
void mf_free(MfFile *f);

#endif
