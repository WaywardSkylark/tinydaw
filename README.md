# tinydaw

A small music maker for Windows and Linux. Make drums, synth sounds and chords, arrange them on a
timeline, mix them with effects and export a finished song. Play it live from your keyboard, a game
controller or a MIDI keyboard, and record what you play or sing.

![tinydaw playing the House example song](docs/screenshot.png)

- **Ready to play:** sixteen example songs, 30 drum beats and a Theory panel with scales, chords and
  progressions you can add in one click.
- **Makes its own sounds:** 62 synth sounds across 12 styles, shaped with plain controls like
  *brightness* and *grit*. It also loads WAV, FLAC, OGG and MP3 files.
- **Sings your words:** type a line, pick a tune and a voice, and it sings it, in 12 languages.
- **Everything you need to finish a song:** a chord track, a piano roll, a drum step sequencer, MIDI, automation, a full mixer with effects,
  and export to WAV, FLAC, MP3 and more.

## Get it

**Windows:** download the zip from the
[latest release](https://github.com/WaywardSkylark/tinydaw/releases/latest), unzip it and run
`tinydaw.exe`. You don't need to install anything.

**Linux:**

```sh
# Arch / CachyOS
sudo pacman -S sdl2-compat sdl2_ttf libsndfile libsamplerate alsa-lib ttf-dejavu zenity espeak-ng
# Debian / Ubuntu
sudo apt install libsdl2-dev libsdl2-ttf-dev libsndfile1-dev libsamplerate0-dev libasound2-dev fonts-dejavu-core zenity espeak-ng

make && ./tinydaw
```

## Your first minute

1. **Hear an example:** ☰ > **Open an example song**, then press **Space**. It opens as a copy,
   so change anything.
2. **Start your own:** ☰ > **New**. In **Create > Beats**, pick a **Style** and press
   **Place on new row(s)**.
3. **Add chords:** in **Theory > Keys**, press **Place** next to a progression. The chords go on
   the CHORDS strip above the tracks.
4. **Write parts from them:** **Create > Parts**, pick **Bass** or **Melody** and press **Make it**.
   **Try another** writes it again differently.
5. **Save** with Ctrl+S and **export** with Ctrl+E.

Hold the mouse over anything to see what it does. Settings > **Keys** lists every shortcut.

## Example songs

Sixteen of them: House, Hip-hop, Experimental, Breakcore, Rock, Trap, Synthwave, Techno, Pop,
Chiptune, Lo-fi, Drum & Bass, Jazz, Ambient, Disco and Afrobeats. Each is a complete song with its
chords, drum patterns and a mix, so you can open one to see how it's made.

## More

- [The manual](docs/manual.md) covers every panel, shortcut and option.
- [For developers](docs/developers.md) covers the code layout, the self-test and Windows builds.
