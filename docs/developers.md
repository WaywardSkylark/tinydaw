# Working on tinydaw

## Layout of the code
- `src/main.c`: the interface, drawn fresh every frame with SDL's 2D renderer, and the self-tests
- `src/audio.c`: sounds, synthesis, import, sample-rate changes, export
- `src/fx.c`: the mixer and effects: a state-variable filter, RBJ biquad EQ, tanh drive, a tempo-synced
  delay and a Freeverb-style reverb
- `src/analysis.c`: the FFT behind the analyzer and the show
- `src/fx.c` also plays note clips, evaluates automation, runs the master compressor and
  look-ahead limiter, and holds the tempo map (frames, beats and bars, either way)
- `src/scenes.c`: the show's geometry and shader scenes. Lines are drawn by stamping soft points
  along them and blurring once for the glow; per-pixel scenes run on every core
- `src/project.c`: the `.tdaw` format
- `src/midi.c`: MIDI input (ALSA on Linux, winmm on Windows)
- `src/theory.c`: scales, chords, progressions and the beat styles
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

## Tests

```sh
./tinydaw --selftest                      # 300 checks, driving the real interface with simulated input
./tinydaw --demo --screenshot ui.bmp      # draw the window off-screen
TINYDAW_DEBUG_INPUT=1 ./tinydaw           # print every key, controller and MIDI input
TINYDAW_CHECK_TEXT=1 ./tinydaw            # report any text that overlaps other text or outgrows its button
TINYDAW_SCALE=1.5 ./tinydaw               # the automatic interface size, overridden
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
