# Working on tinydaw

Major changes since 0.8.0, and the reasoning behind them, are logged in [notes.md](../notes.md).

## Layout of the code

**The interface** is drawn fresh every frame with SDL's 2D renderer. Its modules share one internal
header, `src/ui.h` (types, globals, every function's prototype), and each defines its own part:

- `src/main.c`: fonts and `main()`, the command line
- `src/ui_core.c`: colours, text, input, layout, project edits, the metronome and the widgets
- `src/history.c`: undo and redo (snapshots compared after each frame)
- `src/files.c`: opening and saving, the file dialogs, recent projects and the project browser
- `src/live.c`: pads and live input, the library, the Generator, the Singer and tones
- `src/visual.c`: the visualizer and the full-screen show
- `src/panels.c`: the rail and its panels, the mixer pane, automation lanes and note clips
- `src/roll.c`: the piano roll
- `src/arrange.c`: the chord track, Create > Parts, and the timeline
- `src/session.c`: the session view
- `src/edit_input.c`: dragging and the keyboard
- `src/audio_io.c`: audio devices, the microphone and the demo
- `src/theory_ui.c`, `src/drums_ui.c`, `src/editor.c`, `src/console.c`: the Theory pane and genre beats,
  the Drums panel, the clip editor window, and the faders, strips and mixing console
- `src/selftest.c`: the self-test (see Tests)

**The engine** (no interface; it doesn't include `ui.h`):

- `src/audio.c`: sounds, synthesis, import, sample-rate changes, export
- `src/fx.c`: the mixer and effects: a state-variable filter, RBJ biquad EQ, tanh drive, a tempo-synced
  delay and a Freeverb-style reverb; the effect rack (`RACK_KINDS`, `rack_run`: ten kinds, each with
  its own state per track or bus slot, made again when the kind changes) and the buses
- `src/analysis.c`: the FFT behind the analyzer and the show
- `src/fx.c` also plays note clips, evaluates automation, runs the master compressor and
  look-ahead limiter, and holds the tempo map (frames, beats and bars, either way)
- `src/scenes.c`: the show's geometry and shader scenes. Lines are drawn by stamping soft points
  along them and blurring once for the glow; per-pixel scenes run on every core
- `src/project.c`: the `.tdaw` format
- `src/midi.c`: MIDI input (ALSA on Linux, winmm on Windows)
- `src/theory.c`: scales, chords, progressions and the beat styles
- `src/harmony.c`: the chord track and the song's key: which chord and key are where, voicing chords
  so each voice moves as little as it can, and naming them (theory.c's `chord_guess` names the chord
  a set of notes makes)
- `src/parts.c`: parts written from the chord track (chord parts, basslines, melodies) and drum fills;
  deterministic for a seed, so Try another is the next seed
- `src/bend.c`: pitch curves on clips: the warp (how far into a sound a bent clip is) that notes, drum hits
  and tape-style audio play through, and the WSOLA pitch shifter for audio that keeps its timing
- `src/voice.c`: the Singer: runs eSpeak NG for each syllable, then either re-pitches it with
  PSOLA (Clear) or sings it through a 28-band vocoder, stretching the vowel to the note's length
- `src/synth.c`: the Generator's synth (unison oscillators, FM, a resonant filter with its own
  envelope and LFO, drive and crushing) and its genre presets
- `src/midifile.c`: reading and writing Standard MIDI Files
- `src/loudness.c`: ITU-R BS.1770 loudness (K-weighting, gating)
- `src/compat.h`: the few things that differ on Windows

## How some of it works
- The audio callback runs on SDL's audio thread, and every edit takes the audio device lock.
- Undo compares the project with a snapshot after every frame, so no edit can forget to record itself.
- Sounds are mixed in 32-bit float at the project rate. Changing the rate rebuilds synthesized sounds
  and reloads imported ones at the new rate, rather than resampling them.
- Routing: each track runs its strip, then its rack, then goes to the master or into a bus (`out`),
  with sends to the delay bus (0), the reverb bus (1) and any others (`send[b]`). Group buses (2 and
  up) run first, then the delay and reverb, which run their effect and then their rack, fader and pan.
  A bus at 0 dB, centred, unmuted and with an empty rack adds exactly as before buses existed, so old
  songs render bit for bit the same. Rack knobs are track parameters `TP_RACK + slot * 6 + knob`, so
  macros can turn them (`track_param_info` gives their names and ranges).

## Tests

The self-test lives in `src/selftest.c`, on its own. New tests go there, in a block of their own with a
comment saying what it covers.

`make` builds the files side by side (one object each, in `build/`), so an edit rebuilds only its own
file: a few seconds for most, about 30 for `selftest.c`.

```sh
./tinydaw --selftest                      # 497 checks, driving the real interface with simulated input
./tinydaw --demo --screenshot ui.bmp      # draw the window off-screen
TINYDAW_DEBUG_INPUT=1 ./tinydaw           # print every key, controller and MIDI input
TINYDAW_CHECK_TEXT=1 ./tinydaw            # report any text that overlaps other text or outgrows its button
TINYDAW_SCALE=1.5 ./tinydaw               # the automatic interface size, overridden
TINYDAW_SHOT_MIXER=0 TINYDAW_SHOT_MIXSUB=4 TINYDAW_SHOT_RACK=1 ./tinydaw --demo --screenshot b.bmp
                                          # a Mixer page (4: Buses), with a bus and a rack to show
```

The self-test covers editing, undo, saving and reopening sound for sound, pads, MIDI and controller
input, every effect measured on rendered audio, the Generator and Singer (pitch included), tempo
changes, the clip editor window, the themes and the example songs.

**Windows builds** are cross-compiled on Linux with llvm-mingw against MSYS2's libraries:
`make dist-windows`, see [win/README.md](../win/README.md). The version lives in `VERSION`, and the
program, saved projects, the Windows file properties and the zip name all use it.

**Built with** SDL2 and SDL2_ttf (zlib licence), libsndfile and alsa-lib (LGPL-2.1), libsamplerate
(BSD-2) and the DejaVu fonts. The Windows zip includes their licences, and eSpeak NG (GPL-3) as a
separate program in its own folder.
