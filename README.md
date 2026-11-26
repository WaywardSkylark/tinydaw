# tinydaw

A small timeline sequencer in C. Generate tones and drums, import audio files, drag them onto
8 tracks, arrange, loop, save and export. Play sounds live from the keyboard, game controllers
and MIDI devices with 16 pads, and record what you play onto the timeline.

**Libraries (all open source):** SDL2 (window, input, drawing, audio output; zlib licence),
SDL2_ttf (text; zlib), libsndfile (reading and writing audio files; LGPL-2.1), libsamplerate
(sample-rate conversion; BSD-2), alsa-lib (MIDI input; LGPL-2.1, optional). Font: DejaVu Sans Mono.

## Build and run

```sh
make
./tinydaw              # empty project
./tinydaw --demo       # a two-bar beat to play with
./tinydaw a.wav b.flac # start with files in the library
./tinydaw song.tdaw    # open a project
```

Arch/CachyOS packages: `sdl2-compat sdl2_ttf libsndfile libsamplerate alsa-lib ttf-dejavu`, plus `zenity`
for the Open/Save dialogs (Debian/Ubuntu: `libsdl2-dev libsdl2-ttf-dev libsndfile1-dev libsamplerate0-dev
libasound2-dev fonts-dejavu-core zenity`). Without alsa-lib it builds without MIDI. Set `TINYDAW_FONT=/path/to/font.ttf` to use another font.

## Using it

| Do | How |
|---|---|
| Make a sound | Pick a wave (sine, square, saw, tri, noise) or drum (kick, snare, hat), set note, length, volume, attack, release, press **+ Generate sound** |
| Change a number | Drag it left/right or up/down, or use the mouse wheel. Hold Shift for fine steps |
| Import audio | Drop WAV, FLAC, OGG, MP3 (or anything libsndfile reads) onto the window |
| Hear a sound | Click it in the library (click again to stop) |
| Place a sound | Drag it from the library onto a track |
| Move a clip | Drag it, in time and between tracks. Arrow keys nudge it |
| Copy a clip | Ctrl+drag, or Ctrl+D to duplicate it after itself |
| Trim a clip | Drag its left or right edge |
| Delete a clip | Right-click it, or select it and press Delete |
| Snap | **Snap** button cycles off / bar / 1/2 / 1/4 / 1/8 / 1/16. Hold Shift while dragging to ignore it |
| Play / stop | Space, or **Play**. Home jumps back to the start |
| Playhead | Click or drag in the ruler |
| Loop | Right-drag in the ruler to set a region, **Loop** or L to toggle |
| Zoom / scroll | Ctrl+wheel to zoom, wheel to scroll; +/- also zoom |
| Mute / solo / volume | M and S buttons and the bar under each track name |
| Rows | **+** next to TRACKS adds a row (up to 16); placing a beat adds and names its own rows |
| Tuning | **Tuning A4** in the generator (415–466 Hz) applies to new tones, chords and the note table |
| Export | **Export...** or Ctrl+E opens the export dialog (Enter exports, Esc cancels). Files go to `~/Music` (or the current folder) |
| Project sample rate | The **kHz** button cycles 44.1 / 48 / 88.2 / 96 / 176.4 / 192 kHz |
| Undo / redo | Ctrl+Z, Ctrl+Shift+Z or Ctrl+Y, or the **Undo** and **Redo** buttons (300 steps) |
| Save / open | **File** menu, Ctrl+S, Ctrl+Shift+S, Ctrl+O, Ctrl+N. Dropping a `.tdaw` file on the window opens it |

## Saving

Projects are `.tdaw` text files (default folder `~/Music/tinydaw`). They store recipes, not audio:
generated sounds are made again when the project opens, and imported sounds are read again from
their files, so a project is a few kilobytes. If an imported file has moved, its clips play
silence until it's back. Unsaved work is written to `~/.local/share/tinydaw/autosave.tdaw` every
30 seconds; after a crash, **File > Recover unsaved work** brings it back. Closing with unsaved
changes asks first.

Undo covers clips, tracks, tempo, tuning, the loop region and pad settings. A drag, a placed beat
or a recorded take is one step. Sounds added to the library stay there.

## Pads and live input

**Pads** in the top bar (or F2) opens the pads pane: 16 pads that play library sounds, and an
instrument that MIDI keyboards play chromatically.

| Do | How |
|---|---|
| Put a sound on a pad | Drag it from the library onto the pad, or **Fill from library** |
| Play a pad | Its key (1-4, Q-R, A-F, Z-V) while the pane is open, a controller button, a MIDI note, or click it |
| Pad behaviour | **One-shot** plays to the end, **Hold** plays while held, **Loop** starts and stops on each press |
| Rebind | Select a pad, **Learn input**, press any key, controller button/d-pad/stick/trigger or MIDI note/CC |
| Play an instrument | Drop a sound on **Instrument** (or **Generator sound**) and play a MIDI keyboard; Root note is where it plays unpitched |
| Record | **Rec**, then play the song: hits become clips on each pad's track, snapped to the grid |

Devices are picked up when plugged in, including while tinydaw is running: game controllers and
other USB controllers through SDL, MIDI keyboards and pad controllers through ALSA. MIDI notes
36-51 (C2-D#3) trigger the pads by default, which suits pad controllers; on a piano keyboard press
**No MIDI on pads** so every key plays the instrument. Bindings belong to your hardware, not a song,
so they're kept in `~/.config/tinydaw/pads.conf`; the sounds on the pads are saved in the project.
Controllers work even when the window is in the background.

## Theory pane

**Theory** in the top bar (or F1) opens a reference pane on the right. Sounds you hear from it
use the generator's current wave, volume and envelope (noise and drums fall back to triangle).

| Tab | What's there |
|---|---|
| Genres | 12 styles (house, techno, hip-hop, trap, drum & bass, rock, funk, reggaeton, dubstep, lo-fi, disco, jazz swing), each with a tempo and typical range, swing, a suggested key, tuning and its own drum kit. An editable step grid (kick, snare, clap, closed/open hat, perc; click to cycle hit, accent, ghost, roll). **Play** previews it, **Place on new row(s)** adds it at the bar nearest the playhead as one row or a row per drum, and the orange handle can be **dragged** onto any row and position. The kick is tuned to the key root chosen in Keys |
| Keys | Any root and 12 scales/modes: notes (click to hear), degree formula, steps, key signature. The chords of the key as triads or 7ths with Roman numerals (click: hear, Shift+click: add to the library). 12 common progressions spelled in the key, with **Play** and **Place** (one chord per bar from the bar nearest the playhead, on the chosen track) |
| Circle | Circle of fifths with relative minors and key signatures; click a key to select it |
| Chords | 19 chord types with symbol, formula and notes on the current root |
| Intervals | Semitones, names, just-intonation ratios, cents off in equal temperament; click to hear |
| Tempo | Straight, dotted and triplet times in ms and Hz at the project tempo, tap tempo, tempo markings |
| Notes | Frequency of every note C0–B8; click to set the generator's note |

Notes are spelled with one letter per scale degree, so Eb minor contains Cb and Gm is G Bb D.

## Export options

| Option | Choices |
|---|---|
| Format | WAV, AIFF, FLAC, OGG Vorbis, Opus, MP3 |
| Bit depth | 16-bit, 24-bit, 32-bit float (FLAC up to 24-bit; lossy formats use a 0–1 VBR quality instead) |
| Sample rate | the project rate, or 44.1–192 kHz, converted with libsamplerate's best-quality sinc. Opus is always 48 kHz and MP3 at most 48 kHz |
| Channels | stereo or mono |
| Range | whole song or the loop region |
| Output | the mix, or stems (one file per track, ignoring mute/solo) |
| Normalize | off, -1 dB, -0.1 dB peak (mixes only, so stems stay in balance) |
| Dither | TPDF dither for 16-bit output |

From the command line: `./tinydaw --demo --export out.flac --bits 24 --rate 96000` (see `--help`).

## High resolution

The engine mixes in 32-bit float at the project sample rate. Changing the project rate rebuilds
generated sounds from their settings and reloads imported sounds from their files at the new rate,
so a 192 kHz project really has content up to 96 kHz. Exporting a 48 kHz project at 96 kHz only
changes the file format, because no detail above 24 kHz exists to keep.

## Design

- `src/audio.c`: the engine. Sounds are stored as 48 kHz stereo float; imports are converted and
  resampled on load. Square and saw use PolyBLEP so they don't alias badly. Clips get 64-frame
  fades at their edges so trims don't click, and the output goes through a soft limiter.
- `src/main.c`: the UI, an immediate-mode layout redrawn every frame with SDL's 2D renderer. The
  audio callback runs on SDL's audio thread; every edit takes the audio device lock.
- `src/project.c`: the `.tdaw` format. `src/midi.c`: a thread on the ALSA sequencer that subscribes
  to every MIDI output port. Live pad voices are mixed in the audio callback on top of the song.
- Undo compares the project with the last snapshot after every frame, so no edit can forget to
  record itself.
- No effects, automation or MIDI output yet.

## Tests

```sh
./tinydaw --selftest                 # simulated mouse drags through the real input code
./tinydaw --demo --export demo.wav   # headless render of the demo
./tinydaw --beat trap --export t.wav # render a genre beat (4 bars, its tempo and key)
./tinydaw --demo --screenshot ui.bmp # offscreen render of the window
./tinydaw --demo --save demo.tdaw    # write a project without opening a window
TINYDAW_DEBUG_INPUT=1 ./tinydaw      # print every key, controller and MIDI input
```

The self-test covers undo, saving and reopening (sound for sound), pads, learning, the instrument,
recording, and a virtual SDL gamepad.
