# The tinydaw manual

Everything tinydaw does, panel by panel. New here? Start with [Your first minute](../README.md#your-first-minute) in the README.

## Contents

- [Finding your way around](#finding-your-way-around)
- [Arranging](#arranging)
- [Chords and keys](#chords-and-keys)
- [Notes and automation](#notes-and-automation)
- [Making sounds and parts](#making-sounds-and-parts)
- [Mixing](#mixing)
- [Playing live: pads and MIDI](#playing-live-pads-and-midi)
- [Saving and exporting](#saving-and-exporting)
- [Watching it: the visualizer](#watching-it-the-visualizer)
- [The example songs](#the-example-songs)
- [Settings](#settings)
- [Command line](#command-line)
- [Limits](#limits)

## Finding your way around

- **Top bar:**
  - the ☰ menu: new, open, save, examples, export, settings, full-screen show, quit
  - undo and redo
  - play and loop
  - tempo, snap and the metronome
  - where you are in the song, and the master level
- **The rail** down the left holds Create, Theory, Pads, Drums, Mixer, Visual and Settings. Each
  opens a panel with its own tabs. Click the open one again, or press **Ctrl+B**, to hide it and give
  the timeline the full width. **Ctrl+1** to **Ctrl+7** open them from the keyboard.
- **The timeline:** tracks down the side, bars and minutes:seconds along the top.
- **Tooltips:** hold the mouse still over anything for a moment and it says what it is and how to
  use it (turn them off in Settings > Look).
- **Size:** Settings > Look > **Interface size** (or Ctrl+= and Ctrl+-, Ctrl+0 for automatic) makes
  everything bigger or smaller together. Automatic matches your screen's scaling, and text stays
  sharp at every size.

## Arranging

**Sections.** The strip above the tracks names the parts of the song. Drag along it (or press **+**
beside SECTIONS) to mark one; new ones are named in the usual order (Intro, Verse, Chorus...), and
sections with the same name share a colour.

| To | Do this |
|---|---|
| Go to a section | Click it; **Ctrl+PageUp** / **Ctrl+PageDown** for the previous / next |
| Rearrange the song | Drag a section to another section's edge: everything in it (clips, automation, chords, tempo and key changes) moves, and what was between moves along. Clips crossing an edge are split there |
| Rename / resize | Double-click it to type a name; drag its right end |
| Repeat, loop or cut | Right-click: **Copy it in after itself**, **Loop it**, **Remove the marker** (the music stays) or **Remove it and its bars** |

| To | Do this |
|---|---|
| Play / stop | Space, or **Play**. Home goes to the start, End to the end |
| Transport | The buttons at the bottom centre: to the start, back a bar, play/pause, stop (back to where play began; again for the start), forward a bar, to the end, loop and record |
| Put a sound in the song | Drag it from the library onto a track |
| Hear a sound first | Click it in the library (click again to stop) |
| Remove a sound | The **×** on its row in the library, or right-click it > **Remove from the library**. Its clips go too; Ctrl+Z brings both back |
| Give a sound layers | Right-click it > **Open in the clip editor** > **Layers** 1-4 and **Round-robins** 1-3 (**Hear them** plays each). Generator sounds get 3 x 2 as they're made (set it under the sliders), drum kits too; the library shows it as "3x2". Drum patterns, note clips, pads and MIDI pick a layer by how hard each hit is |
| Edit a clip or a sound | Double-click an audio clip: the clip editor opens in its own window (trim, fades, gain, pitch, reverse, normalize, split). Right-click a library sound > **Open in the clip editor** to shape the sound itself, then **Save as a new sound** (the original stays) |
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
| Edit typed text | In a name, folder or a number being typed: click to put the caret there, ←/→ to move it (Ctrl: a word), Home / End, Backspace and Delete |
| Tempo / time signature | **BPM** and the box beside it (4/4, 3/4, 6/8...) set them for the part of the song the playhead is in (BPM shows `BPM*` inside a change). The metronome menu adds a change at the playhead's bar; changes show as flags above the bar numbers (click to go there, right-click to remove). In a one-tempo song, selected clips follow a new tempo (audio is time-stretched, recipes are remade) |
| Tuning | **A4** beside BPM (415–466 Hz, e.g. 432). New sounds use it |
| Metronome | **Click** in the top bar, or M. Its arrow sets the sound, 8th/16th ticks, accent and a 1-2 bar count-in |
| Add a track | **+** next to TRACKS (up to 16). Each track has mute, solo and volume |
| Bring in audio | Drop WAV, FLAC, OGG or MP3 files on the window |

## Chords and keys

**The chord track.** The CHORDS strip under the sections holds the song's chords: what the harmony
is, bar by bar. It makes no sound on its own; it's there to see, to play from, and to write parts
from. Each chord shows its name and its Roman numeral in the key (Am vi, G V), coloured by where it
sits in the key.

| To | Do this |
|---|---|
| Add a chord | Click the strip: a chord fills the rest of that bar. Drag along it to set the length yourself, or press **+** beside CHORDS for a bar at the playhead. It suggests the next chord the key usually goes to (I, then V, vi, IV) and opens the chord picker |
| Change a chord | Double-click it. The picker has the key's own chords (with **7ths** for sevenths), any root and any of the 19 chord types, and a bass note for slash chords (C/E). Each change plays as you click |
| Hear a chord | Click it |
| Move / resize | Drag it (it takes the place of any chord it lands on); drag its right end |
| Remove | Delete with the pointer on the strip, or right-click > **Remove it** |
| Put in a progression | Theory > Keys > **Place**: one chord a bar on the chord track, from the bar nearest the playhead |
| Find the chords in a part | Select a note clip, then right-click the strip > **Chords from the selected note clip**. It works half a bar at a time (a bar at a time in 3/4 and 6/8) and joins repeats |
| Hear the chords | Right-click the strip > **Make a chord part**, or use [Create > Parts](#parts-written-from-the-chords) for a chord part, a bassline, a melody or drum fills |

**The key.** Every song has a key, saved with it, and shown beside CHORDS. Theory always shows the
key where the playhead is, and changing it in Theory > Keys changes the key there. For a key change
part-way through (the last chorus up a step), right-click the chord strip > **A key change here**,
then pick the new key in Theory > Keys with the playhead after it. A key change is a flag on the
chord strip: click it to go there, right-click to remove it. Chords are named and numbered in the
key where they are.

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
name swaps that row's sound. Create > Beats > **Edit in Drums** turns the beat you see into one.

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

## Making sounds and parts

Everything that makes something new is in **Create** (Ctrl+1), beside your library:

| Tab | What it makes |
|---|---|
| Library | The sounds you have: click to hear, drag onto a track, right-click to pin, save or edit |
| Synth | **Presets**: the Generator's ready-made sounds. **Tones**: plain waves and simple drums |
| Voice | The Singer: words sung on a tune |
| Beats | Drum beats in 30 styles |
| Parts | A chord part, a bassline, a melody or drum fills, written from the chord track |

**Create > Synth > Presets** (the Generator) is the quickest way to a sound, with no synth knowledge needed:

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

**Create > Voice** (the Singer) sings words you type:

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
4. **Shape the pitch** (optional): the line's notes are drawn with the pitch as it will be sung
   over them. Draw on it to bend notes (Shift: whole semitones), right-click to rub a bend out, or
   start from **Scoops** (up into each note), **Slides** (gliding note to note) or **Fall-offs**
   (dropping away at phrase ends); **Flat** clears it. Bends change the pitch, never the timing.
5. **Sing it** to hear it, then **+ Library** or **Put it at the playhead** (**Put it with the
   clip** when singing a note clip's melody, so it lines up underneath).

The words are spoken by [eSpeak NG](https://github.com/espeak-ng/espeak-ng), a free speech
program: the Windows download includes it, and on Linux it's the `espeak-ng` package. Sung lines are
saved as words, notes and pitch curve, so they stay in a project (silent) where eSpeak NG is missing.

**Create > Synth > Tones** builds a plain tone instead: choose sine, square, saw, triangle, noise, kick,
snare or hat. Then set the note, length, volume and envelope (attack, release), and the tuning (A4,
415–466 Hz). **Play** lets you hear it before adding it to the library.

**Create > Beats** has 30 styles: house, deep house, tech house, techno, trance, UK garage,
breakbeat, drum & bass, jungle, dubstep, hip-hop, trap, UK drill, phonk, R&B, pop, rock, funk, disco,
synthwave, electro, footwork, lo-fi, reggaeton, dancehall, reggae, afrobeats, amapiano, bossa nova
and jazz swing. Each has its own tempo, swing, key and drum kit, and a step grid you can edit.
**Play** previews it, **+ Library** adds it as a loop, **Place** puts it in the song, or drag the
orange bar onto any track.

### Parts written from the chords

**Create > Parts** writes a part from the chord track, in the key wherever it is. Pick the part,
its style, how busy it is, its octave and its sound (any Generator preset that suits it, or the
instrument chosen in Pads), and where it goes: the whole chord track, the loop, or the section the
playhead is in. **Make it** puts it on a free track as a note clip you can edit in the piano roll;
**Try another** writes it again differently, in the same clip.

| Part | Styles |
|---|---|
| Chords | **Held** (pads), **Pulse** and **Stabs** (keys), **Arp up** and **Arp up & down** (plucks). Each chord is voiced close to the last, so the voices move as little as they can |
| Bass | **Roots**, **Pulse** (8ths), **Octaves**, **Off-beat**, **Walking** (quarters that lead into each new chord a step away) and **Groove** (a syncopated pattern with the fifth and octave). Slash chords put their bass note in the bass |
| Melody | **Hook** (short and catchy), **Flowing** (moves more) and **Sparse** (room to breathe, or to sing). Short cells come back (A A B A), the strong beats land on chord tones, and each phrase ends on the root or third |
| Fills | **Snare roll**, **Build**, **Toms** and **Drop-out**, in the bar before each section or every 4 or 8 bars, a beat, half a bar or a whole bar long. Each fill is made from that bar of your own drum pattern and becomes a pattern of its own you can open in Drums |

**Theory** has three tabs. Everything you can hear in them has a **+** that adds it to the library.
The strip at the top of each tab sets which wave and length those sounds use. Adding the same thing
twice doesn't make a copy, and the Create button counts what you've added.

| Tab | What's there |
|---|---|
| Keys | The song's key (where the playhead is) and 12 scales and modes: the notes, the chords of the key (triads or 7ths, with Roman numerals) and 12 common progressions to play, add, or **Place** on the chord track |
| Chords | 19 chord types on the chosen root |
| Reference | The circle of fifths, intervals (with their just-intonation ratios), tempo maths (delay times in ms), and the frequency of every note |

## Mixing

**Mixer** (or F3) has five pages:

| Page | What's there |
|---|---|
| Levels | Every track's fader (in dB, with its meter), pan, mute and solo, then the master and the loudness. Drag a fader or use the wheel over it (half a dB a notch, Shift for a tenth); double-click puts it back to 0 dB. A fader whose volume is automated follows its lane. Click a name for that track's effects |
| Clip | Gain, pitch, fades and reverse for the selected clips, and a pitch bend drawn over them |
| Track | Volume, pan, a filter (low-, high- or band-pass with cutoff and resonance), 3-band EQ, drive, how much it ducks, its sends to the delay and reverb, its stereo width (0 mono to 2 twice as wide) and its own compressor (threshold, ratio, attack, release, makeup, and how hard it's working) |
| Effects | The delay (in time with the tempo, 1/16 to 1/2 including dotted and triplet, feedback, tone, ping-pong), the reverb (size, damping), the sidechain (which track makes the others duck), and the master: **Off**, **Glue** or **Loud**, or set the compressor and the limiter's ceiling yourself, with a loudness meter |
| Live | How far pitch bend goes; the mod wheel opens the filter on the instrument's track |

**Presets.** The Track page's **Preset** list sets a track's whole chain at once (filter, EQ, drive,
sends, duck, width, compressor and macros) while it keeps its name, level and pan: Clean, Warm tape,
Telephone, Wide pad, Pumping, Lo-fi crush, Filter sweep, Vocal, Punchy drums and Dub send. **Save as**
keeps the chain as a preset of your own, listed as "Saved:" in every project (they're small files in
Music/tinydaw/Presets). The Effects page has the same for the shared delay (Slapback, Eighth echo,
Dotted 8th, Triplet bounce, Dub quarter, Long half) and reverb (Room, Plate, Hall, Cathedral, Dark
chamber).

**Macros.** Each track has four macro knobs, at the bottom of the Track page. A macro turns up to
four of the track's settings at once, each between two values you choose: **Links** opens it, **+
Link** adds a setting (then pick which, and its From and To), and the knob sweeps them all together.
A linked setting follows its macro instead of its own box. Name a macro by typing in its box. Macros
can be automated (the track's A lane lists them by name), and they're saved with presets, so a preset
like Warm tape comes with a "Warmth" knob.

**Pitch bend.** Mixer > Clip draws a pitch curve over the selected clip: any clip, whether audio,
notes or drums. Drag across the curve to draw the pitch, from 12 semitones down to 12 up (24 once you
need it); hold Shift for whole semitones, right-click or Ctrl+drag to rub a bend out. Or start from
**Scoops** or **Fall-offs** (on each note of a note clip, or the clip's start or end), **Vibrato**,
**Rise** or **Dive** (an octave over the clip); **Flat** takes the bend away. Notes and drum hits
follow the curve as they play, slides included. For an audio clip, choose **Keep timing** (the
pitch moves and the clip stays in time: it's pitch-shifted when you let go, which suits single
voices and instruments best) or **Tape** (the speed moves with the pitch, like a tape or turntable).
The curve belongs to the clip: it moves, trims and splits with it, and copies share it until you
bend one of them.

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
| House: Late Train | 124 BPM, A minor. Four-on-the-floor patterns (intro, groove, build, drop) with ghost hats that come and go, an off-beat bass and accented piano stabs that pump with the kick, a pad, a pluck answering the hook, and Lumi singing "Late train" with slides and fall-offs |
| Hip-hop: Rooftop Static | 90 BPM, C minor (Cm9, Abmaj7, Fm9, G7). Swung boom-bap with ghost notes, soul-key stabs and an upright groove written from the chords, a flute answering in the hook with scoops, Kai singing the hook, drop-out fills, and the beat winding down like a tape stopping |
| Experimental: Tidal Clockwork | 108 BPM. Clockwork in 5/4, a tide in 7/8, a waltz in 6/8, then home in 4/4, and the key moving from D Dorian to F# Lydian and back. Glass bells arpeggiated up and down from the chords, a shimmer pad, a flute line with vibrato, ticking percussion that bends down as it runs out, and wind rising in pitch |
| Breakcore: Static Bloom | A half-time intro at 86 BPM, then 172 with chopped two-bar break patterns and rising stutter rolls, four bars of 7/8, and a faster run at 186. A shimmer pad and reese bass, a filter swept by automation, a robot voice leaping an octave, and the Loud master |
| Rock: Paper Lanterns | 140 BPM, D major, and the last chorus a step up in E. Doubled crunch guitars, palm-muted in the verse, a picked bass written from the chords, a sagging guitar solo, tom fills into each section, and Aria singing the chorus |
| Trap: Midnight Hollow | 140 BPM, F minor, half-time. Two-bar drum patterns with hat bursts, triplet and thirty-second rolls, an 808 bassline and bell melody, an intro opening under a filter, and a talkbox tag that scoops and falls |
| Synthwave: Neon Coastline | 100 BPM, F# minor. A gated snare, a pulsing bass and sixteenth arps under a retro lead, a pad and bass pumping with the kick, a filter opening over the intro, and a talkbox singing the chorus |
| Techno: Concrete Hours | 130 BPM, G minor, pushed to 133 for the peak. Four-on-the-floor with a rumbling bass, a stab every three sixteenths against the bar, an acid line whose filter opens over the peak, and a robot voice |
| Pop: Paper Planes | 118 BPM, C major. Piano verses and a supersaw chorus over C - G - Am - F; Aria sings the verses and Lumi the chorus with Kai a third below, each line shaped with scoops, slides and fall-offs |
| Chiptune: Pixel Quest | 150 BPM, C major. A title screen, two levels (the second a whole step up), a boss fight in 3/4 at 170 and a victory jingle. Square-wave lead, fast arpeggios, a pulse bass jumping octaves and noise drums |
| Lo-fi: Window Rain | 76 BPM, F major (Fmaj7, Em7, Dm9, Cmaj7). Lazy swung drums, held keys with a tape wobble (a vibrato pitch curve), a round bass on the roots, a flute that barely speaks, rain and vinyl |
| Drum & Bass: Night Bus | 174 BPM, F minor. Two-step breaks with ghost snares, a rolling sub on the roots with a reese doubling it off the beat, a held pad opening under automation, a bell hook, snare rolls into each section, and the sub diving at the drop's end |
| Jazz: Blue Lamp | 132 BPM swung, Bb major: ii-V-I-vi with a turnaround, then the last chorus up a minor third in Db. Ride, cross-stick and feathered kick, a walking upright bass, comping keys and a flowing flute, all written from the changes |
| Ambient: Slow Tide | 70 BPM, E Lydian. A shimmer pad held, glass bells rising and falling, a choir singing a sparse line, a soft pulse, drones, and wind that swells and settles in pitch |
| Disco: Mirror Ball | 120 BPM, A minor (Am7, D9, Gmaj7, Cmaj7). Four on the floor with off-beat open hats, an octave bass, chord stabs and held strings, a supersaw hook, builds into each section, and Lumi singing the chorus |
| Afrobeats: Golden Hour | 104 BPM, G major. A rolling kick and rim pattern with shakers, a log-drum groove bass, plucks arpeggiating the chords, a vocal-like lead hook with scoops, and tom fills |

Every drum part is a pattern you can open in the Drums panel, the kits and keys have velocity
layers and round-robins, the sung lines have pitch curves, the parts are named on the arranger
strip, and the chords are on the chord track (Chiptune changes key three times). The newer songs (Hip-hop,
Rock, Experimental and the six after Chiptune) had their bass, chord and melody parts and their drum
fills written by Create > Parts, from the command line (`--part`, `--fills`). Each sets up the pads with its own sounds too. `examples/make_examples.py` writes them.

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
./tinydaw song.tdaw --part "bass,style=Walking,sound=Upright bass" --fills "style=Build" --save song.tdaw
                                          # write parts and fills from the chord track (as Create > Parts)
./tinydaw --version
```

`./tinydaw --help` lists every option.

## Limits

tinydaw has no MIDI output to other devices, and records one microphone at a time. Beat loops and
sung lines are made at one tempo, so they don't follow tempo changes inside them. The drum patterns
are approximations of each style, meant as a starting point, and the Singer sounds synthetic.
