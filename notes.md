# tinydaw notes

A running log of major changes since 0.8.0: what changed, why, where it lives in the code, and
what was decided along the way. Newest first. It's here to give context to whoever picks the work up
next. The manual (`docs/manual.md`) says how things work for users; this says why they're built the
way they are.

## Unreleased (after 0.8.5)

### 2026-11-28: Buses and an effect rack

Asked for: buses and a flexible effect chain ("the biggest musical gap"). The user chose "strip +
effect rack" and "group + return buses".

- **Rack:** `Rack` (up to 8 `RackFx`: kind, on, 6 knobs) on every track and bus, run after the fixed
  strip. Ten kinds in fx.c's `RACK_KINDS` table (names, ranges, formats, log scales, choices): EQ,
  Filter (SVF), Drive, Compressor, Delay (tempo divisions, ping-pong), Reverb (its own Freeverb),
  Chorus, Phaser (6 allpasses), Bitcrusher, Gate. State lives in the Mixer per owner (track, or
  MAX_TRACKS + bus) and slot; it's reallocated when the kind in a slot changes.
- **Buses:** `Project.buses[8]`, `nbuses`. Bus 0 is the shared delay, bus 1 the reverb (their racks
  run after the built-in effect). `Track.out` (0 master, b+1 bus b) and `Track.send[b]` for b >= 2;
  buses 0/1 keep using `delay_send` and `reverb_send`. Group buses can send to the delay and reverb.
- **Bit-exact:** a bus at unity, centred, unmuted, with an empty rack takes the old code path, so the
  examples render identically (checked on five songs by comparing the WAV data with a pre-change
  build at 32-bit float).
- **Files:** `bus`, `busrack`, `trackout`, `tsend` and `rack` lines; old files get the two default
  buses from `project_init`. Presets save the rack; two new built-ins (Dreamy, Bitcrushed). Macros
  can link to rack knobs (`TP_RACK + slot*6 + knob`, `track_param_info`, `track_param_valid`).
- **UI:** Mixer > Track has Output, sends to user buses and the rack editor; new Mixer > Buses page
  (Live moved to index 5); bus rows in Levels and bus strips in the console; faders take
  `-2 - b` for bus b.
- **Tests:** 19 checks: every kind changes the sound and does nothing when off, order matters,
  group/fader/mute/pan/rack/sends, the delay bus, saving, macros on rack knobs, the preset, undo.
- **Found along the way:** eSpeak NG renders the syllable "a" (with `-z -b 1`) differently from run
  to run, so the Breakcore example's robot vocal isn't byte-identical between renders. Not ours;
  that song is left out of bit-exact comparisons.

### 2026-11-28: main.c split into modules; tests in their own file

`main.c` had grown to 22,000 lines. It took about 100 s to compile (one translation unit, one core)
and caused stale-build mix-ups.

- **How:** a script split it, not hand edits. It scans the top-level items (comments, strings,
  braces and preprocessor lines respected, and inside function bodies only the first branch of an
  `#if` is counted). Types, macros, includes and size-less constant tables go to `src/ui.h` in their
  original order. Every global gets an `extern` there and every function a prototype. Definitions
  move, minus `static`, to the module for their section. Anonymous-struct globals got named types
  (`T_<name>`). Globals declared twice in the old file (legal within one translation unit) are now
  defined once. Top-level `#if` blocks (the file dialogs) stay whole, and their header lines carry
  the same guards. 17 modules, see docs/developers.md.
- **Building:** one object per file in `build/`, `MAKEFLAGS += -j$(nproc)` by default; `selftest.o`
  is built at -O1 (one huge function). A clean build takes about 42 s; after an edit, a few seconds
  for most files and about 30 s for selftest.c. The Windows target still compiles everything in one
  command.
- **Rule from here on:** tests live in `src/selftest.c`, as their own blocks.
- Nothing changed in behaviour: the same 473 checks pass before and after.

### 2026-11-28: Slot launch modes: loop, toggle, hold, once

Asked for: toggle, loop and hold options in the session view. Once was added as the counterpart to
loop, so slots match the pads' One-shot, Hold and Loop modes.

- `Slot.mode` (LAUNCH_LOOP, TOGGLE, HOLD, ONCE), saved as a separate `slotmode t s m` line so older
  builds aren't confused. A button in the slot's corner cycles through the modes.
- **Toggle:** a click on the playing (or queued) slot queues the timeline. **Hold:** it launches on
  press and stops on release, right away (`sess_release_now`); holding a scene's name (`D_SCENE`)
  holds its Hold slots. **Once:** when the audio thread makes the switch, it queues the timeline at
  the end of one pass, wrapping round the loop when the pass ends past `loop_end`.
- When a slot stops by itself, its track goes back to the timeline (silent in "session only").
- **Tests:** 7 checks. The hold tests accept "queued or already switched", because the self-test's
  dummy audio device really runs once playback starts.

### 2026-11-28: Session and timeline together or apart; slot copy, paste, import; examples with scenes

Asked for: the session and the timeline playable on their own or together, with the user always aware
of which, and a visible option for it; easier import, copy and paste of sounds to and from the session;
the example songs' session view filled in as a demo.

- **What plays** (`P.sess_mode`, live and not saved): SESS_BOTH (a track plays its slot or the timeline),
  SESS_ONLY_SESSION (only launched slots; the timeline is silent), SESS_ONLY_TIMELINE (the session
  is set aside but launched slots stay set, for A/B). `add_clips` decides per mode, and range slots
  read their automation from the source bars only when the session is playing. It's a "Plays:"
  dropdown first in the session toolbar; the less important toolbar buttons drop off when it's narrow.
- **Awareness in the timeline view:** the view toggle reads "Session (N)" and lights up when the
  session has tracks (or "Session (off)" in timeline-only). `session_lane_marks()` covers each affected
  lane with what's happening there: playing a slot, stopped, silent in session-only, or about to
  switch.
- **One clipboard idea for slots:** `last_copy` records which clipboard was used last (a sound from
  the library or a pad, clips from the timeline, or a slot). Copying a slot fills all three: the slot
  board, the timeline's clip clipboard (`slot_clips()`: one pass, so Ctrl+V on the timeline pastes it)
  and the sound clipboard (its sound, for the library or pads). A paste into a slot takes whichever was
  copied last. Ctrl+C, Ctrl+X, Ctrl+V and Delete work with the pointer on a slot.
- **Import:** right-click > "Import a sound file here..." (file dialog mode 4, with audio filters on
  Linux and Windows), or drop files from the file manager onto a slot (`slot_drop_file`: the pointer
  position comes from the global mouse state, since SDL2's drop event has none; several files fill the
  empty slots after it, adding a scene if needed). `import_file` now returns the new sound's index.
- **Drag** a slot to another to move it (Ctrl copies). A click without moving still launches it, now
  on release (`D_SLOT`). "Put it on the timeline at the playhead" writes one pass as clips.
- Right-click menus can now have up to 10 items (they had 6).
- **Examples:** `--scenes` (after any `--part` and `--fills`) runs From sections; `make_examples.py`
  passes it for every song, so all 16 open with a scene for each section.
- **Tests:** 8 checks: the three modes (both tracks rendered alone in each), slot to slot and onto the
  timeline, a library sound into a slot, Ctrl+C/V over slots, drag move and Ctrl copy, one pass onto
  the timeline, importing a WAV into a slot, and that every example has its scenes.

### 2026-11-28: Session fixes: looping capture, launches at the loop's end, slots outside 4/4

Four bugs reported in the session view:

- **Capture doubled clips while looping.** Every wrap started a new segment over the same bars, and
  capture wrote them all, so the copies stacked and played twice as loud. Now each segment is cut back
  by later segments on the same track before anything is written: the newest pass wins.
- **Captured clips drifted after a wrap.** The reopened segment started wherever the next UI frame caught
  the playhead, and capture counted passes from there. Segments now carry `at` (the launch frame):
  after a wrap a segment reopens at `loop_start` with the same `at`, and capture counts passes from
  `at`, as `add_session` plays them. Each written piece is trimmed to what showed, with its offset
  moved to match.
- **A launch in the loop's last bar never fired.** `sess_boundary` gave `loop_end`, which the playhead
  never reaches (audio_cb wraps first). `sess_queue` now queues those at `loop_start` with
  `P.sess_q_wrap[t]` set. The audio thread fires a wrap-queued launch only in the block that holds
  its frame (the blocks before the wrap are past it, so the old rule, "q_at < block end", would have
  fired it at once).
- **Clip slots lost whole bars outside 4/4.** `slot_fill_clip` rounded to multiples of 4 quarter
  notes. It now rounds to the bar length (or, for less than a bar, the beat: 4/den) of the time
  signature at the clip's start.

Tests: 3 regression checks: a loop capture (no overlap, two bars covered, every piece's offset in step
with the launch), a launch queued in the last bar of a loop, and slot lengths in 3/4, 7/8 and 6/8.

### 2026-11-28: Recent projects and a project browser

Asked for: a way to find the dozens of .tdaw files in Music/tinydaw: a browser with a list,
thumbnails of the first bars, and tags.

- **Browser** (`browse_draw`, a full-window view; Ctrl+O now opens it, and the system dialog is
  "Another file..." in it or "Open a file..." in the menu): tabs Recent, My projects (Music/tinydaw
  walked two folders deep, newest first; `TINYDAW_PROJECTS_DIR` overrides it) and Examples (open as
  untitled copies, like the menu). It has search over name, file name and tags, tag chips to filter by,
  and a click to open (through `request()`, so unsaved changes are asked about). Esc closes it.
- **Cards** come from `project_peek()` (project.c): a quick line scan (title, tempo, meter, key,
  tags, track, clip, section, chord and scene counts, the clips in the first 16 bars, the length) that
  makes no sounds. They're cached by path and modification time in `bcache`. The thumbnail places clips
  by the opening tempo and meter (an approximation if the tempo changes early). A file with chords but
  no `key` line is C major.
- **Tags** live in the project (`Project.tags`, a `tags a, b` line), so they travel with the file, and
  they're part of undo and dirty. Editing a closed project's tags uses `project_set_tags()`, which
  rewrites only that line (a temp file then a rename). If it's the open project, `P.tags` follows so a
  later save keeps them. The examples' tags can't be edited.
- **Recent:** `recent.txt` in the config folder, newest first, 20 at most, added on every open and save
  (`TINYDAW_RECENT_FILE` overrides it; headless runs don't touch it). ☰ > Open a recent project lists
  the newest 10 that still exist.
- **Tests:** 8 checks: peek, tags written as one line (the rest byte for byte) and replaced, recent
  order, Ctrl+O, search, tag filter, clicking a card opening it, Esc.

## 0.8.5 (released 2026-11-28)

### 2026-11-28: The session view (clip launcher)

Asked for: an Ableton-style grid of scenes x tracks, launch a scene, jam, capture to the timeline.
Agreed: the same tracks and mixer, and **per-track** playback (a track plays the timeline until a slot
is launched on it).

- **Layout:** tracks stay as rows (their headers and mixer controls stay put); scenes are columns.
  That's Ableton's grid turned sideways to fit tinydaw. Tab or the "Session view" button (where
  "TRACKS" was) toggles it.
- **Slots** (`Slot`, `P.slots[track][scene]`): `SLOT_CLIP` holds a copy of any clip (sample,
  pattern, root, offset, gain, pitch curve); `SLOT_RANGE` loops a stretch of the track's own
  timeline (bar, beats). Range slots are what make "From sections" possible: every song becomes a
  playable session with no setup. A pass's length is `slot_frames` (range: by the tempo map; clip: beats
  at the tempo where it was launched).
- **Live state** (in Project, not saved): `sess_play[t]` (SESS_TIMELINE, SESS_STOPPED or a scene),
  `sess_at[t]`, and a queue (`sess_q`, `sess_q_at`). The UI queues for the next bar or beat; the
  **audio thread** makes the switch inside `mixer_run`, so it lands on the bar exactly.
- **Rendering (fx.c):** `add_clips` was split into `add_clip` (one clip) plus a loop. A playing slot
  is drawn as temporary clips, one per pass from the launch (`add_session`); a range slot copies the
  track's clips in that stretch, trimmed and moved, so notes, drums, bends, layers, macros and effects
  all just work. Passes are counted with floor division, so moving the playhead back before the launch
  keeps the loop in step. A range slot also reads the track's automation at its source time. **Only live
  playback uses the session** (`mixer_run`'s `voices` flag); export and offline renders play the timeline.
- **Capture:** `sess_track()` runs every frame and logs segments (track, a copy of the slot, from,
  to), closing and reopening them when the transport loops. `sess_capture` first builds every clip to
  write (a range slot is read from the timeline *before* it's replaced, in case it loops the bars
  being written over), then clears those stretches on those tracks (split and delete), then adds
  them. It's one undo step. Pieces under 20 ms are skipped.
- **Undo:** scenes, names and slots are in `State` (slots copied only when there are scenes).
- **Pitch curves:** `bend_used()` counts slot clips, so their curves are saved and get playback
  tables and keep-timing renders.
- **File format:** `scene i name` and `slot t s kind beats bar sample pattern root offset length gain
  bend name`. Older builds ignore them. Opening a song puts every track on the timeline.
- **Tests:** 12 checks: From sections; a range slot replacing the timeline; export ignoring the
  session; a queued launch switching exactly on its bar; stop and back to the timeline; a clip slot
  looping; Tab; clicking a scene launching on the next bar; capture landing where it played, as one
  undo step; save and load.
- **Not done:** moving sections doesn't update range slots' bars; there's no MIDI or pad launching
  of slots yet; Parts can't write into a slot yet; a slot's notes can't be edited in the piano roll
  in place (put the clip on the timeline, edit it, then put it back).

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

