# tinydaw

A small music maker for Windows and Linux. Make drums, synth sounds and chords, arrange them on a
timeline, mix them with effects and export a finished song. You can also play sounds live from
your computer keyboard, a game controller or a MIDI keyboard, and record what you play or sing.

![tinydaw playing the House example song](docs/screenshot.png)

- **Starts you off quickly:** six example songs, 30 ready-made drum beats, and a Theory panel
  with scales, chords and progressions you can add to your song with one click.
- **Makes its own sounds:** a Generator with 62 ready-made sounds across 12 styles (808s, wobble
  bass, supersaws, pads, keys, drum hits, risers), shaped with five plain controls like
  *brightness* and *grit*. Also plain tones, and WAV, FLAC, OGG and MP3 files.
- **A real mixer:** faders in dB with meters, per-track EQ, filter, drive, compressor, stereo width,
  pan and sidechain ducking, delay and reverb, a master compressor and limiter with a loudness meter
  (LUFS), and a mixing console you can pop out into its own window.
- **Sings your words:** type a line, pick a tune and a voice, and it sings it, in 12 languages.
- **Drum step sequencer:** patterns of any sounds over 16 or 32 steps, with velocity and a chance
  of playing on every step, swing, and a start from any of the 30 styles.
- **Piano roll, MIDI and automation:** play or write melodies (quantize and humanize them), import
  and export MIDI files, and draw volume, pan, filter and send changes over time.
- **Any time signature, changing tempo:** 3/4, 6/8 and more, with changes anywhere in the song.
- **Live play:** 16 pads and a MIDI instrument, played from the keyboard, a controller or MIDI, and
  recorded onto the timeline.
- **Exports** to WAV, AIFF, FLAC, OGG, Opus or MP3, as a full mix or as one file per track.
- **Looks how you like:** Dark, Light, follow-your-desktop, Windows XP, Frutiger Aero and Blender themes,
  sharp on high-resolution screens, at any size from 100% to 200%, with tooltips on everything.

## Get it

**Windows:** download `tinydaw-<version>-windows-x64.zip` from the
[latest release](https://github.com/WaywardSkylark/tinydaw/releases/latest), unzip it anywhere and
run `tinydaw.exe`. Nothing is installed.

**Linux:** install the libraries and build:

```sh
# Arch / CachyOS
sudo pacman -S sdl2-compat sdl2_ttf libsndfile libsamplerate alsa-lib ttf-dejavu zenity espeak-ng
# Debian / Ubuntu
sudo apt install libsdl2-dev libsdl2-ttf-dev libsndfile1-dev libsamplerate0-dev libasound2-dev fonts-dejavu-core zenity espeak-ng

make
./tinydaw
```

`zenity` (or `kdialog`) gives the Open, Save and Export dialogs, and `espeak-ng` lets the Singer
sing. Without alsa-lib tinydaw still builds, just without MIDI.

## Your first minute

1. **Hear what it can do:** ☰ > **Open an example song** (there are six), then press **Space**. Each example opens
   as an untitled copy, so you can change anything without spoiling it.
2. **Start your own:** ☰ > **New**. Open **Theory** on the left, pick a **Style** under Beats and
   press **Place on new row(s)**.
3. **Add harmony:** in Theory > **Keys**, pick a key, then press **Place** next to a chord
   progression (or **+** to put its chords in your library).
4. **Add your own sounds:** **Sounds > Generator**, pick a style (say *Trap*) and what the sound
   is for (say *Bass*), click a sound to hear it, then press **Put it at the playhead** or
   **+ Library** and drag it onto a track.
5. **Mix it:** open **Mixer**, click a track's name and try a filter, some reverb, or **Duck** to
   make it pump with the kick.
6. **Save** with Ctrl+S and **export** with Ctrl+E.

Settings > **Keys** lists every shortcut.

## Finding your way around

- **Top bar:**
  - the ☰ menu: new, open, save, examples, export, settings, full-screen show, quit
  - undo and redo
  - play and loop
  - tempo, snap and the metronome
  - where you are in the song, and the master level
- **The rail** down the left holds Sounds, Theory, Pads, Drums, Mixer, Visual and Settings. Each
  opens a panel with its own tabs. Click the open one again, or press **Ctrl+B**, to hide it and give
  the timeline the full width. **Ctrl+1** to **Ctrl+7** open them from the keyboard.
- **The timeline:** tracks down the side, bars and minutes:seconds along the top.
- **Tooltips:** hold the mouse still over anything for a moment and it says what it is and how to
  use it (turn them off in Settings > Look).
- **Size:** Settings > Look > **Interface size** (or Ctrl+= and Ctrl+-, Ctrl+0 for automatic) makes
  everything bigger or smaller together. Automatic matches your screen's scaling, and text stays
  sharp at every size.

## Arranging

| To | Do this |
|---|---|
| Play / stop | Space, or **Play**. Home goes to the start, End to the end |
| Transport | The buttons at the bottom centre: to the start, back a bar, play/pause, stop (back to where play began; again for the start), forward a bar, to the end, loop and record |
| Put a sound in the song | Drag it from the library onto a track |
| Hear a sound first | Click it in the library (click again to stop) |
| Remove a sound | The **×** on its row in the library, or right-click it > **Remove from the library**. Its clips go too; Ctrl+Z brings both back |
| Keep a sound for later | Right-click it in the library > **Save for future sessions**: a copy goes in Music/tinydaw/Saved sounds, and every project's library has it. **Pin to the top** keeps it first in the list (and saves it). **Forget the saved copy** moves the file out to Unsaved sounds |
| Copy a sound | Right-click a library sound, pad or the instrument slot > **Copy**, then right-click another > **Paste** (or Ctrl+C / Ctrl+V over them). Pad to pad copies its settings too |
| Make a sound of clips | Drag clips onto the library: they become one new sound with their tracks' effects, saved as a WAV in `Music/tinydaw/Sounds` |
| Move a clip | Drag it, in time and between tracks. Arrow keys nudge it |
| Trim / fade a clip | Drag its edges, or the small squares on its top corners |
| Edit a clip in detail | Double-click it: a window opens with a large waveform (trim, fade, zoom, click to hear), exact start, length and offset, gain, pitch, reverse, fades, and Normalize, Fit to whole bars, Split at the playhead, New sound with its effects and Delete |
| Make a clip louder or quieter | Alt + mouse wheel over it |
| Transpose / reverse a clip | `[` and `]` move it a semitone, Ctrl+R plays it backwards |
| Select several | Drag a box over empty timeline, Shift+click to add or remove, Ctrl+A for all, Esc for none. Moves, deletes, copies, transposes and gain changes then apply to all of them |
| Copy | Ctrl+drag, Ctrl+D (a copy straight after), or Ctrl+C / Ctrl+X then Ctrl+V (pastes at the playhead) |
| Delete | Delete, or right-click a clip |
| Undo / redo | Ctrl+Z, Ctrl+Shift+Z (300 steps) |
| Loop part of the song | Right-drag in the ruler, then **Loop** or L |
| Snap to the grid | **Snap** steps through off, bar, 1/2 … 1/16; its arrow opens the list. Hold Shift while dragging to ignore it |
| Jump around | Ctrl+←/→ a bar, Ctrl+Shift+←/→ a beat, Alt+←/→ to the next clip edge, F shows the playhead, Z zooms to fit the song |
| Zoom / scroll | Ctrl + wheel to zoom, wheel to scroll, + and - also zoom |
| Change a number | Click it and type, drag it, or use the wheel (Shift for fine steps). Note boxes take names like `C4`, `F#3` or `Bb2` |
| Tempo / time signature | **BPM** and the box beside it (4/4, 3/4, 6/8...) set them for the part of the song the playhead is in (BPM shows `BPM*` inside a change). The metronome menu adds a change at the playhead's bar; changes show as flags above the bar numbers (click to go there, right-click to remove). In a one-tempo song, selected clips follow a new tempo (audio is time-stretched, recipes are remade) |
| Tuning | **A4** beside BPM (415–466 Hz, e.g. 432). New sounds use it |
| Metronome | **Click** in the top bar, or M. Its arrow sets the sound, 8th/16th ticks, accent and a 1-2 bar count-in |
| Add a track | **+** next to TRACKS (up to 16). Each track has mute, solo and volume |
| Bring in audio | Drop WAV, FLAC, OGG or MP3 files on the window |

## Notes and automation

**Piano roll.** Double-click an empty spot on a track to make a note clip (one bar, using the
Instrument sound from Pads, or a saw if none is set), or double-click a note clip (or select it and
press Enter) to open it. The roll takes over the timeline until you press **Done** or Esc.

| To | Do this |
|---|---|
| Add a note | Click the grid. It snaps to the grid and is as long as the last note you drew |
| Move / stretch | Drag a note, or drag its right end. Shift turns snapping off |
| Delete | Right-click it, or select and press Delete |
| Select | Click, Shift+click, Shift+drag a box, Ctrl+A |
| Transpose / nudge | Up / Down (Shift: an octave), Left / Right |
| Copy | Ctrl+drag, or Ctrl+D for a copy straight after |
| Velocity | Drag across the bars in the Velocity strip at the bottom |
| Tidy / loosen timing | **Quantize** moves the selected notes (or all) onto the Snap grid; **Humanize** nudges them a little in time and velocity, like a player |
| Hear a note | Click a key on the keyboard |

The bar along the top picks the clip's sound, the note it plays unpitched at (**Root**) and its
length in bars; a note past the end makes the clip grow. While the roll is open, **Roll** buttons in
Theory > Keys and Chords put a chord (or a whole progression, a bar each) into it. Copies of a note clip share their notes, so
editing one edits all of them; **Make unique** gives a copy its own. `[` and `]` and the Mixer's
Pitch still transpose a whole note clip.

**Drums.** The Drums panel (Ctrl+4) holds each track's drum patterns: a row per sound, 16 or 32
steps of a 16th each. Start one with **New from style** (any of Theory's 30 beats, with its kit), or
drag a sound from the panel's list onto it; dropping a sound on a step puts a hit there, on a row's
name swaps that row's sound. Theory > Beats > **Edit in Drums** turns the beat you see into one.

| To | Do this |
|---|---|
| Add or remove hits | **Hits**: click a step, or drag along a row to paint |
| Set how hard | **Velocity**: drag up and down inside a hit |
| Make a hit occasional | **Chance**: drag up and down inside a hit (a dot marks a hit with a chance) |
| Feel | **Swing** moves the off-beat 16ths later |
| Use it | **Put on the timeline** makes a drum clip, 1 to 8 bars; drag its edge to repeat it longer |

Every drum clip of a pattern plays it, so editing the pattern changes them all; **Copy as new** gives
you one to change separately. Double-click a drum clip to open its pattern. A step's chance is decided
the same way every time, so playback and export match.

**Automation.** Press **A** on a track to open its lane underneath, pick what it automates (volume,
pan, filter cutoff, delay send or reverb send), then click to add points, drag them and right-click to
remove them. Between points the value moves in a straight line; before the first and after the last
it holds. A lane with points overrides that control, and an orange dot on **A** means a hidden lane
has points. Automating the cutoff turns the track's filter on as a low-pass if it was off.

## Making sounds

**Sounds > Generator** is the quickest way to a sound, with no synth knowledge needed:

1. **What kind of music?** House, Techno, Hip-hop, Trap, Lo-fi, Drum & bass, Dubstep, Synthwave,
   Pop, R&B, Ambient or Chiptune (or Any).
2. **What is the sound for?** Bass, Lead, Pad, Keys, Pluck, Vocals, Drums or FX (or Anything).
   Each says what it's for in plain words. Vocals are wordless voices: choirs, oohs and aahs, and
   synths that talk.
3. **Click a sound** to hear it. Its controls open underneath:

| Control | What it does |
|---|---|
| Brightness | dark to bright: muffled or crisp |
| Softness | snappy to smooth: a hard start and short tail, or a slow fade in and long tail |
| Thickness | thin to fat: more detuned copies, wider stereo, more sub |
| Grit | clean to dirty: saturation, then bit-crushing |
| Wobble | still to wobbly: the filter moving in a rhythm |
| Note, Length, Volume | the pitch it plays at, how long it lasts, how loud |

Each control starts in the middle, which is the sound as designed. You hear every change as you let
go, double-clicking a control puts it back, and **Back to how it was** resets the lot. **Surprise
me** picks a random sound that fits your choices, with a little variation. Then:

- **+ Library** keeps it, ready to drag onto a track.
- **Put it at the playhead** places it straight into the song, on the first free track.
- **Play notes with it** makes it the instrument, so double-clicking a track writes notes with it
  in the piano roll, and a MIDI keyboard plays it.

Generator sounds are saved as recipes, like everything else that tinydaw makes.

**Sounds > Singer** sings words you type:

1. **Type the words.** Each word gets a note; split longer words with `-` to give each part its
   own (`hap-py birth-day`). A comma leaves a short gap and a full stop a longer one. The line
   underneath shows exactly how it will be split.
2. **Pick a tune:** One note, Up and down, Rising, Falling, Bouncy or Call and answer, in the key
   set in Theory > Keys, starting on the note you choose, at a slow, medium or fast pace. Or
   **From a note clip**: write a melody in the piano roll, select its clip, and the words are sung
   on its notes, one word per note.
3. **Pick a voice.** **Clear** is a singer: pick Aria (high, bright), Lumi (high, soft), Kai
   (low, warm) or Ren (low, plain). **Robot**, **Talkbox**, **Choir** and **Whisper** are
   vocoder voices, low or high. Then one of 12 languages and how much vibrato.
4. **Sing it** to hear it, then **+ Library** or **Put it at the playhead** (**Put it with the
   clip** when singing a note clip's melody, so it lines up underneath).

The words are spoken by [eSpeak NG](https://github.com/espeak-ng/espeak-ng), a free speech
program: the Windows download includes it, and on Linux it's the `espeak-ng` package. Sung lines are
saved as words and notes, so they stay in a project (silent) where eSpeak NG is missing.

**Sounds > New sound** builds a plain tone instead: choose sine, square, saw, triangle, noise, kick,
snare or hat. Then set the note, length, volume and envelope (attack, release), and the tuning (A4,
415–466 Hz). **Play** lets you hear it before adding it to the library.

**Theory** has four tabs. Everything you can hear in them has a **+** that adds it to the library.
The strip at the top of each tab sets which wave and length those sounds use. Adding the same thing
twice doesn't make a copy, and the Sounds button counts what you've added.

| Tab | What's there |
|---|---|
| Beats | 30 styles: house, deep house, tech house, techno, trance, UK garage, breakbeat, drum & bass, jungle, dubstep, hip-hop, trap, UK drill, phonk, R&B, pop, rock, funk, disco, synthwave, electro, footwork, lo-fi, reggaeton, dancehall, reggae, afrobeats, amapiano, bossa nova and jazz swing. Each has its own tempo, swing, key and drum kit, and a step grid you can edit. **Play** previews it, **+ Library** adds it as a loop, **Place** puts it in the song, or drag the orange bar onto any track |
| Keys | Any key and 12 scales and modes: the notes, the chords of the key (triads or 7ths, with Roman numerals) and 12 common progressions to play, add or place |
| Chords | 19 chord types on the chosen root |
| Reference | The circle of fifths, intervals (with their just-intonation ratios), tempo maths (delay times in ms), and the frequency of every note |

## Mixing

**Mixer** (or F3) has five pages:

| Page | What's there |
|---|---|
| Levels | Every track's fader (in dB, with its meter), pan, mute and solo, then the master and the loudness. Drag a fader or use the wheel over it (half a dB a notch, Shift for a tenth); double-click puts it back to 0 dB. A fader whose volume is automated follows its lane. Click a name for that track's effects |
| Clip | Gain, pitch, fades and reverse for the selected clips |
| Track | Volume, pan, a filter (low-, high- or band-pass with cutoff and resonance), 3-band EQ, drive, how much it ducks, its sends to the delay and reverb, its stereo width (0 mono to 2 twice as wide) and its own compressor (threshold, ratio, attack, release, makeup, and how hard it's working) |
| Effects | The delay (in time with the tempo, 1/16 to 1/2 including dotted and triplet, feedback, tone, ping-pong), the reverb (size, damping), the sidechain (which track makes the others duck), and the master: **Off**, **Glue** or **Loud**, or set the compressor and the limiter's ceiling yourself, with a loudness meter |
| Live | How far pitch bend goes; the mod wheel opens the filter on the instrument's track |

**The mixing console** (the button at the top of Levels, or ☰ > Mixing console) opens in its own
window you can put anywhere, even another screen: a strip a track, side by side, with EQ, compressor,
width, sends, pan, a tall fader and meter, mute and solo, and a master strip with Off / Glue / Loud,
how hard the master is working, the loudness and the master fader. Esc closes it.

Tracks with effects show `fx` under their name, and every track has a level meter. What you export
sounds exactly like what you hear, including the delay and reverb dying away after the last note.

## Playing live: pads and MIDI

**Pads** (or F2) has 16 pads. Drag library sounds onto them, press **Fill from library**, or pick a
**Preset** to fill all 16 at once (Ctrl+Z puts the old pads back):

| Preset | What goes on the pads |
|---|---|
| Drum kit | The Beats style's kit: kick, snare, clap, hats and percussion, then toms, a ghost snare and tuned variations |
| Drums + chords | Eight drums, then the seven chords of the Theory key and the first one with a seventh |
| Chords of the key | The key's seven triads, its seven sevenths, then I up an octave and IV, held while you press |
| Scale notes | 16 notes of the Theory scale, climbing from the key note in the middle octave |
| Bass notes | The same, two octaves down, on the bass track |
| Beat loops | The Beats style's whole beat and its parts as loops, then other styles' beats |

- **Playing them:** keys 1-4, Q-R, A-F and Z-V (while the panel is open), a game-controller button,
  a MIDI note, or a click.
- **Modes:** **One-shot** plays to the end, **Hold** plays while you hold it, **Loop** starts and
  stops on each press.
- **Rebinding:** select a pad, press **Learn input**, then press any key, controller button, stick,
  trigger, MIDI note or knob.
- **Pitch bend:** a MIDI pitch wheel, or a controller stick (the right stick's up/down to start
  with; choose another or turn it off in Settings > Input). It springs back to centre; Mixer >
  Live sets how far it bends.
- **Instrument:** a MIDI keyboard plays a chosen sound at every pitch. On a piano keyboard, press
  **Stop MIDI notes triggering pads** in Settings > Input so every key plays the instrument.
- **Recording:** press **Rec** (it starts the song, after a count-in if one is set) and play along.
  A timer runs beside the transport; when you stop, the take is on an open track (a new one if
  none is free). Pad hits become clips; MIDI keys become notes in a note clip you can edit in the
  piano roll, quantized to the Snap grid unless you turn that off beside **Rec**. One Ctrl+Z undoes a take.
- **Microphone:** click a track's red dot to arm it, then press **Rec**. What the microphone hears
  becomes a clip on that track, from where you started, and is saved as a WAV in
  Music/tinydaw/Recordings. The armed dot shows the input level. Settings > Audio picks the input,
  its gain, **Hear yourself** (use headphones) and the offset, which moves takes earlier to make up
  for the audio system's delay (automatic by default). On Windows, Settings > Privacy > Microphone
  must allow apps to use it.

Controllers and MIDI devices are picked up when they're plugged in, even while tinydaw is running.
Pads play through their track's effects.

## Saving and exporting

Projects are small `.tdaw` files, saved in `Music/tinydaw` by default. They store how each sound was
made rather than the audio, so they open at full quality at any sample rate. Imported audio files
are linked, not copied; if one moves, its clips play silence until it's back.

Unsaved work is autosaved every 30 seconds; after a crash, ☰ > **Recover unsaved work** brings it
back. Projects also open by dropping them on the window or naming them on the command line.

**Export** (Ctrl+E):

| Option | Choices |
|---|---|
| Format | WAV, AIFF, FLAC, OGG Vorbis, Opus, MP3 |
| Bit depth | 16-bit, 24-bit, 32-bit float (FLAC up to 24-bit; lossy formats have a quality setting instead) |
| Sample rate | The project's, or 44.1–192 kHz (Opus is always 48 kHz, MP3 at most 48 kHz) |
| Channels | Stereo or mono |
| Range | The whole song or the loop |
| Output | The mix, or one file per track |
| Level | As mixed, peaks at -1 or -0.1 dB, or a loudness: -14 LUFS (what streaming services play at) or -9 LUFS (loud), with peaks kept under -1 dB |
| Dither | For 16-bit files |

**Folder** and **File name** say where it goes. Type in either, or press **Choose...** for your
system's save dialog. The file is named after the project, or the date and time if it hasn't been
saved yet. Typing a name ending in `.mp3`, `.flac` and so on also picks that format. The dialog
warns before replacing a file, and the folder you choose is kept until you close tinydaw.

**MIDI files:** ☰ > **Import a MIDI file** (or drop a `.mid` on the window) brings each part in as a
note clip on its own track, from the bar at the playhead; an empty song takes the file's tempo and
time signatures. ☰ > **Export MIDI** writes every note clip, with the tempo map.

## Watching it: the visualizer

**Visual** (or F4) has a spectrum analyzer, a waveform, a stereo view (with a warning when left and
right would cancel in mono) and level meters with clip warnings.

**F11** starts a full-screen show with 17 scenes. Pick one in **Visual** (the Show list), or use
←/→ inside the show. With cycling on (Tab), they crossfade from one to the next every 24 seconds,
or however long you set.

- **Blobs:** a glowing blob for every track that's playing, in its colour, circling in time with the
  song, with a ring of spectrum bars and a ripple on every beat.
- **Sacred geometry** (from geoshade): the Flower of Life, Metatron's Cube, the Sri Yantra, the
  Platonic solids, golden-angle phyllotaxis, star polygons, a torus of circles and spirographs, each
  drawing itself in.
- **Shaders** (from termshade): plasma, tunnel, fire, metaballs, a raymarched torus and sphere, a
  starfield, interference and Voronoi cells.

The music drives every scene: the bass makes the picture breathe, beats make it pulse, and louder
music draws it faster. Space plays and stops, the pad keys still work, and Esc leaves.

## The example songs

| Song | What's in it |
|---|---|
| House: Late Train | 124 BPM, A minor. Four-on-the-floor drums, an off-beat bass and chord stabs that pump with the kick, a pad, a lead with ping-pong delay, and a riser and snare roll into the drop |
| Hip-hop: Corner Store | 88 BPM, D minor. Swung boom-bap drums, an 808-style bass, soft keys (Dm9, Gm7, Bbmaj7, A7), a bell melody and tape hiss |
| Experimental: Glass Weather | 96 BPM, F Lydian. Resonant drones, glass bells falling five against four into long echoes, reversed bells, wind, and drums that loop every 3.5 beats so they drift against the bar |
| Breakcore: Static Bloom | A half-time intro at 86 BPM, then 172 with chopped breaks and rising stutter rolls, four bars of 7/8, and a faster run at 186. A shimmer pad and reese bass in note clips, a filter swept by automation, vocal chops, and the Loud master |
| Rock: Highway Lights | 128 BPM, E minor. Double-tracked crunch guitars (palm-muted in the verse, open in the chorus), a picked bass, a guitar solo, and sung verses and choruses: Kai sings, Aria harmonises |
| Trap: Midnight Hollow | 140 BPM, F minor, half-time. An 808 bassline and bell melody in note clips, hat rolls in sixteenths, thirty-seconds and triplets, an intro opening under a filter, and a talkbox vocal tag |

Each sets up the pads with its own sounds too. `examples/make_examples.py` writes them.

## Settings

- **Look:** Dark, Light, System (follows your desktop's dark/light setting), Windows XP, Frutiger
  Aero or Blender; tooltips on or off; and the interface size (Automatic, or 100% to 200%, as far as
  the screen has room for).
- **Audio:** output device; the microphone input, its gain, hearing yourself and the recording
  offset; the project's sample rate (44.1 to 192 kHz) and master volume.
- **Input:** connected controllers and MIDI devices, MIDI options, and which stick bends pitch.
- **Keys:** every shortcut.
- **About:** the version, and where projects, pad bindings and autosaves are kept.

Your theme, interface size, output and input devices, metronome and pad bindings are remembered: in `~/.config/tinydaw/` on
Linux and `%APPDATA%\tinydaw` on Windows.

## Command line

```sh
./tinydaw song.tdaw                       # open a project
./tinydaw a.wav b.flac                    # start with these in the library
./tinydaw --demo                          # a two-bar beat to play with
./tinydaw --beat trap --export trap.wav   # render a genre beat (4 bars, its tempo and key)
./tinydaw song.tdaw --export song.flac --bits 24 --rate 96000
./tinydaw --demo --save demo.tdaw         # write a project without opening a window
./tinydaw --version
```

`./tinydaw --help` lists every option.

## Limits

tinydaw has no MIDI output to other devices, and records one microphone at a time. Beat loops and
sung lines are made at one tempo, so they don't follow tempo changes inside them. The drum patterns
are approximations of each style, meant as a starting point, and the Singer sounds synthetic.

## For developers

**Layout of the code:**
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

**How some of it works:**
- The audio callback runs on SDL's audio thread, and every edit takes the audio device lock.
- Undo compares the project with a snapshot after every frame, so no edit can forget to record itself.
- Sounds are mixed in 32-bit float at the project rate. Changing the rate rebuilds synthesized sounds
  and reloads imported ones at the new rate, rather than resampling them.

**Tests:**

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
`make dist-windows`, see [win/README.md](win/README.md). The version lives in `VERSION`, and the
program, saved projects, the Windows file properties and the zip name all use it.

**Built with** SDL2 and SDL2_ttf (zlib licence), libsndfile and alsa-lib (LGPL-2.1), libsamplerate
(BSD-2) and the DejaVu fonts. The Windows zip includes their licences, and eSpeak NG (GPL-3) as a
separate program in its own folder.
