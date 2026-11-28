# tinydaw notes

A running log of major changes since 0.8.0: what changed, why, where it lives in the code, and
what was decided along the way. Newest first. It's here to give context to whoever picks the work up
next. The manual (`docs/manual.md`) says how things work for users; this says why they're built the
way they are.

## Unreleased (after 0.8.0)

### 2026-11-28: Effect presets and macro knobs

Asked for: presets for the delay, reverb and filter, tracks exposing a few macro knobs bound to effect
parameters, and the whole track chain saved as a named preset. The user also proposed a session view
/ clip launcher. That was agreed to come next, with **per-track** launching Ableton-style (a track
plays the timeline until a slot is launched on it; "back to timeline" returns it). Design so far: same
tracks and mixer; slots hold ordinary clips; a playing slot is rendered as a temporary repeated clip
so the existing engine plays it; launches quantized to the bar; capture writes recorded launches onto
the timeline as one undo step.

- **Macros:** `Track.macro[4]`, each with a name, a value 0..1 and up to 4 `MacroLink`s
  (TP_* parameter, lo, hi). `TRACK_PARAMS` (fx.c) lists what can be linked and its range; cutoff is
  log. `macros_apply` runs last in `auto_apply`, so a linked parameter follows its macro, over its
  own knob and automation. Linking the cutoff turns the filter on (low-pass), and linking the
  compressor turns it on. Four automation params, `AUTO_MACRO1..4`, automate the knobs; lanes show
  the macro's name.
- **Why not the Generator's five controls:** those change how a sound is synthesized (the sound is
  re-rendered), so they can't be live per-track knobs. Macros are the mixer's equivalent.
- **Presets:** built-ins in code (`builtin_track_preset`, `builtin_fx_preset`). Yours are saved as
  `<Music>/tinydaw/Presets/{Track,Delay,Reverb}/<name>.preset` (key/value lines;
  `TINYDAW_PRESETS_DIR` overrides the folder, which tests use). Applying a track preset keeps the
  name, volume, pan, mute and solo. Delay and reverb presets apply to `P.fx` (they're shared sends).
- **File format:** `macro i value name` and `mlink i param lo hi` lines after a track's line.
  Older builds ignore them.
- **Tests:** 8 checks: links (log cutoff, filter on), a volume-linked macro silencing the track,
  macro automation, save and load, built-in and saved presets, delay and reverb presets, and that
  just drawing the page changes nothing. (An early version filled in "Macro 1" names while drawing,
  which dirtied the project; it's a placeholder now.)
- **Not done:** the pop-out mixing console doesn't show macro knobs yet.

### 2026-11-27: Sixteen example songs, made with the new tools

Asked for: replace Hip-hop, Rock and Experimental with new songs using the new tools, and fill the
remaining example slots (the menu holds 16).

- New: Hip-hop: Rooftop Static, Rock: Paper Lanterns (key change up a step), Experimental: Tidal
  Clockwork (5/4, 7/8, 6/8, 4/4, two key changes), Lo-fi: Window Rain, Drum & Bass: Night Bus,
  Jazz: Blue Lamp (walking bass, key change up a minor third), Ambient: Slow Tide, Disco: Mirror
  Ball, Afrobeats: Golden Hour. The old Corner Store, Highway Lights and Glass Weather are gone.
- **How they're made:** `make_examples.py` writes the base (sounds, mix, drum patterns, chords and
  key, sections, sung lines, pitch curves via `Song2.bend`), then runs tinydaw on the file with
  `--part` and `--fills` to write the parts with the real generators, and `--save`s it in place.
  So `make` must have built `./tinydaw` before the script runs.
- **New command-line options:** `--part "kind,style=,busy=,octave=,seed=,sound=,bars=a-b,track=,gain=,bend="`
  and `--fills "style=,len=,where=sections|4|8,track=,seed="`, run in order after the project
  loads. `bend=` puts an editor preset (scoops, fall-offs, vibrato, rise, dive) on the part.
  Implemented by `cli_spec` in main.c through overrides (`pt_force_preset`, `pt_force_track`,
  `pt_from/pt_to`) on the same `part_make` and `fills_make` the panel uses.
- **Self-test changes:** it expects 16 examples, and an example needs more than 20 clips, not 40,
  because generated parts are a few long clips, not many short ones. The tests that open an example
  and keep working in it now use Disco: 4/4 throughout, and at 120 BPM a beat is a whole number of frames (the tests compare
  exact positions; Lo-fi at 76 BPM was a frame off); the new Experimental starts
  in 5/4 and broke 25 later checks.
- **Gotcha:** main.c takes about 100 s to compile at -O2. Editing it while a build runs leaves a
  binary newer than the source, which `make` then won't rebuild. Touch the file or remove
  `tinydaw` if a change seems missing.

### 2026-11-27: Pitch bend on any clip

Asked for: the Singer's drawn pitch curve should work on everything, as one of the Mixer tools. The
live wheel and stick bend was left as it is. For audio clips the user wanted both behaviours, as a
toggle.

- **Where:** Mixer > Clip > PITCH BEND (`clip_bend_editor` in main.c). Draw, Shift for semitones,
  right-click or Ctrl+drag to erase; presets Flat, Scoops, Fall-offs, Vibrato, Rise, Dive. On note
  clips, Scoops and Fall-offs go on each note.
- **Data:** `Bend` (id, keep_time, up to 512 points of seconds and semitones) in `Project.bends`;
  `Clip.bend` holds the id. Time is **seconds from the clip's origin** (start - offset), so moving,
  trimming, splitting and changing the sample rate need no changes to the curve. Copies share a
  curve, and editing clones it first (`cb_edit`, copy-on-write). Flat sets `Clip.bend = 0`.
- **Playback (`src/bend.c`, fx.c):** a warp table (`BendMap`), the integral of 2^(st/12) every 32
  frames, maps timeline frames since the origin to frames into the sound. Note clips:
  `pos = step * (W(f) - W(note start))`. Drum hits do the same from each hit. Tape audio uses
  `src = W(offset + rel)`. Keep-timing audio plays a pitch-shifted render (`shift_sound`: grains
  every ~10 ms, read at the bend's ratio, each placed where it best continues the last (WSOLA),
  coarse then fine search, skipped where the curve is flat). It takes about 1.9 s per 3 minutes of
  audio and is rendered when you let go of the curve.
- **Threads:** `bends_build` makes new tables without the audio lock; `bends_install` swaps them in
  under it. main.c's `bends_update()` runs every frame when nothing is being dragged, and before
  export, bounce and test renders. The audio thread checks a table's hash against the curve and
  plays the clip straight if they differ, so it never uses a stale table.
- **File format:** `bend id keep_time`, then `bpt seconds semitones` lines, before the clips. A
  clip line gains an 11th field, the bend id (audio clips then write `0 0` for pattern and root).
  Older builds ignore all of it.
- **Tests:** 12 checks measuring frequency with zero crossings: an octave up on notes, slides, tape
  running fast and ending early, keep-timing at the right pitch and length, drum hits, saving, the
  editor (Shift, copy-on-write, presets, one undo step).
- **Not done:** MIDI export doesn't write the curves as pitch-bend events, and the clip editor
  window doesn't show the curve.

### 2026-11-27: Create panel, and parts written from the chord track

**Menu reorganized.** Everything that makes sound or music now lives in one rail button, **Create**
(it was Sounds), so the tools are in one place:

- Create: Library | Synth (Presets = the Generator, Tones = the old New sound) | Voice (the Singer) |
  Beats (moved from Theory) | Parts (new).
- Theory is harmony and reference only: Keys | Chords | Reference.
- Theory > Keys > **Place** now fills the chord track only. It used to also drop chord audio clips
  on a track, which the Parts generators now do better. The "on track N" picker in Keys is gone.
- The chord strip's right-click "Make a chord part" uses Create > Parts > Chords' settings.
- Internally `sounds_sub` keeps its old numbers (0 library, 1 generator, 2 singer, 3 tones) and
  adds 4 beats and 5 parts, so the self-tests and screenshot hooks kept working. `theory_tab` 6
  (Beats) is mapped to Keys if anything still sets it.

**Parts** (`src/parts.c`, UI in `main.c`, "Create > Parts"). The generators read the chord track
and the key and write `PartNote`s (beats). The UI turns them into a note clip. With no UI or audio
in parts.c it can be tested directly.

- Chords: Held, Pulse, Stabs, Arp up, Arp up & down. Voiced with `chord_voice` so voices move as
  little as possible.
- Bass: Roots, Pulse, Octaves, Off-beat, Walking, Groove. Uses the slash bass when there is one.
  The root sits low in the range with room for an octave above, and stays near the previous root.
  Walking approaches the next root from a step away.
- Melody: Hook (1-bar cells), Flowing, Sparse (2-bar cells). Phrase form A A B A: cells with the
  same letter reuse the same random choices (seeded by letter), so the shape returns over different
  chords. Strong beats snap to chord tones, other notes move by scale step in the key at that beat,
  and the phrase's last note goes to the root or third. `near_prev` keeps leaps under an octave.
- Fills: `drum_fill` copies the bar of the source pattern as it plays there (from the clip's origin)
  and rewrites its end: Snare roll, Build, Toms, Drop-out, a beat, half a bar or a bar long. Rows
  are found by sample name (kick/808, snare/clap/rim, hat, tom/perc). The drum clip is split around
  the bar and the middle piece plays a new pattern named "Fill, bar N".
- Every generator is deterministic for its seed. **Try another** bumps the seed and rewrites the
  same clip (parts) or the same fill patterns, so it's one undo step and no clutter.
- Sounds: a Generator preset of a suitable kind (Pad/Keys/Pluck, Bass, Lead/Pluck/Keys), made into
  the library once and reused by name, or the Pads instrument. Held chords get a "(held)" variant
  long enough for the longest chord.
- Ranges (MIDI): chords 52-72, bass 28-55, melody 60-81, and the Octave buttons move them by 12.

**Gotcha found on the way.** Beats from `meter_beats()` come back a hair off whole numbers
(19.99999 for 20), because they go through frames. That put notes on the wrong side of chord and
bar edges and left 0-length notes. parts.c puts bar starts on a 1/960-beat grid (`tidy`), and
`emit` drops notes shorter than 0.05 beats. Anything new that walks bars by beats should do the same.

Tests: 9 new self-test checks (410 in all): every style stays in range and on the chords or in the
key, determinism, strong beats on chord tones, phrase endings, walking lead-ins, following the key,
Make / Try another / This section, and fills (placement, roll shape, Try another in place).

### 2026-11-27: Chord track and the song's key (commit ee8e4ea)

- A CHORDS strip under SECTIONS. Chords are stored in **beats** (quarter notes from the start),
  not frames, so tempo edits never move them. They're kept sorted and never overlap
  (`chord_put` makes room).
- The song's key is saved in the project, with key changes by bar (`KeyChange`). Theory's key
  (`th_root`/`th_scale`) is synced both ways with the key at the playhead (`key_sync`, every
  frame), like the BPM box with tempo changes.
- `song_edit` (moving, copying, cutting sections) carries chords (split at the edges) and key
  changes (written out at the edges, like the tempo map).
- File format: `key`, `keychange` and `harmony` lines. Not `chord`, which chord sounds already use
  inside their sample recipes. Older builds ignore the new lines.
- `src/harmony.c`: key and chord lookup, `chord_voice` (voice leading), labels and numerals.
  `theory.c`: `chord_guess` names the chord a set of notes makes (used for "chords from a note
  clip").
- The example songs carry their chords and keys, written by `examples/make_examples.py`
  (`Song2.key`, `Song2.harmony`).
- The type is `SongChord`, not `Chord`: Windows' wingdi.h already declares a `Chord()` function,
  and the Windows build broke on it. Check `make windows` when adding short, common type names.
- Decided: the chord track is silent, and parts are written from it. There's one key per stretch,
  with key changes along the song.

