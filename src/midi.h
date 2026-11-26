/* tinydaw: MIDI input from every connected MIDI device (ALSA sequencer). */
#ifndef MIDI_H
#define MIDI_H

#define MIDI_CC 128          /* codes 0-127 are notes, MIDI_CC + n is controller n */
#define MIDI_MAX_DEVICES 16

/* Called on the MIDI thread: code (note or MIDI_CC + controller), velocity 1-127 or 0 on
 * release, and which device it came from (an index into midi_devices). */
typedef void (*midi_fn)(int code, int velocity, int device, void *ud);

int midi_start(midi_fn fn, void *ud);   /* 0 = listening, -1 = no MIDI (message in midi_error) */
void midi_stop(void);
const char *midi_error(void);
/* Copies the connected devices' names; returns how many. */
int midi_devices(char names[][64], int max);

#endif
