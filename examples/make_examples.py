#!/usr/bin/env python3
"""Writes tinydaw's example songs (examples/*.tdaw).

The songs are made only from what tinydaw can make itself: Generator sounds with velocity layers
and round-robins, drum patterns (velocity and chance on every step), note clips, sung lines with
pitch curves, plain tones, pitched and reversed copies, automation, tempo and time signature
changes, named sections, and the mixer's effects and dynamics. A project stores recipes, so the files are small
and every sound is rebuilt when a song opens.

usage: python3 -I examples/make_examples.py        (then: ./tinydaw examples/<song>.tdaw)
"""
import math
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
RATE = 48000
VERSION = open(os.path.join(ROOT, "VERSION")).read().strip()
SINE, SQUARE, SAW, TRI, NOISE, KICK, SNARE, HAT = range(8)
KICK_L, SNARE_L, CLAP_L, HAT_L, OPEN_L, PERC_L = (1 << i for i in range(6))


def load_genres():
    """The genre table from src/theory.c: name -> its numbers and step patterns."""
    src = open(os.path.join(ROOT, "src", "theory.c")).read()
    body = src[src.index("const Genre GENRES[]"):src.index("const int NGENRES")]
    out = {}
    for m in re.finditer(r'\{"([^"]+)",\s*"[^"]*",\s*([^{]+)\{([^}]*)\}\}', body):
        nums = [float(v) for v in m.group(2).replace("\n", " ").split(",") if v.strip()]
        lanes = re.findall(r'"([^"]*)"', m.group(3))
        keys = ["bpm", "lo", "hi", "swing", "swing8", "root", "scale", "a4", "kick_decay", "snare_hz",
                "hat_decay", "open_decay", "steps"]
        g = dict(zip(keys, nums))
        g["lanes"] = lanes
        out[m.group(1)] = g
    return out


GENRES = load_genres()


class Song:
    def __init__(self, title, bpm, a4=440.0):
        self.title, self.bpm, self.a4 = title, bpm, a4
        self.samples, self.clips, self.tracks, self.pads = [], [], [], []
        self.fx = dict(div=4, fb=0.35, tone=0.5, dlevel=0.8, ping=0, size=0.6, damp=0.4, rlevel=0.8,
                       sc=-1, release=180, bend=2)
        self.loop = (0, 0, 0)
        self.sections = []                          # (bar, bars, name): the arranger strip
        self.bf = round(60.0 / bpm * RATE)          # frames per beat, as tinydaw rounds it

    def section(self, bar, bars, name):
        self.sections.append((bar, bars, name))

    # ---- sounds
    def _frames(self, beats):
        return max(16, int(beats * 60.0 / self.bpm * RATE))

    def gen(self, name, wave, note, beats, vol, att=5, rel=60):
        self.samples.append(dict(kind="gen", name=name, frames=self._frames(beats),
                                 gen=(wave, note, beats, vol, att, rel)))
        return len(self.samples) - 1

    def chord(self, name, notes, wave, beats, vol, att=5, rel=120):
        self.samples.append(dict(kind="chord", name=name, frames=self._frames(beats),
                                 gen=(wave, notes[0], beats, vol, att, rel), notes=notes))
        return len(self.samples) - 1

    def beat(self, name, genre, mask, root=None, volume=0.85):
        g = GENRES[genre]
        root = g["root"] if root is None else root
        f = self.a4 * 2 ** ((root - 9) / 12)        # the kick tuned to the key, between 40 and 80 Hz
        while f >= 80:
            f /= 2
        while f < 40:
            f *= 2
        steps = int(g["steps"])
        frames = round(steps * (60.0 / self.bpm / 4 * RATE))
        self.samples.append(dict(kind="beat", name=name, frames=frames, genre=g, mask=mask, kick_hz=f,
                                 volume=volume, steps=steps))
        return len(self.samples) - 1

    def variant(self, name, src, semis=0, reverse=0):
        s = self.samples[src]
        synth = s["kind"] in ("gen", "chord") and s["gen"][0] < KICK
        frames = s["frames"] if synth or semis == 0 else int(s["frames"] * 2 ** (-semis / 12))
        self.samples.append(dict(kind="variant", name=name, frames=frames, src=src, semis=semis, rev=reverse))
        return len(self.samples) - 1

    # ---- tracks and clips
    def track(self, name, vol=0.8, pan=0.0, filt=0, cutoff=1200, reso=0.2, eq=(0, 0, 0), drive=0.0, delay=0.0,
              reverb=0.0, duck=0.0):
        self.tracks.append(dict(name=name, vol=vol, pan=pan, filt=filt, cutoff=cutoff, reso=reso, eq=eq,
                                drive=drive, delay=delay, reverb=reverb, duck=duck))
        return len(self.tracks) - 1

    def clip(self, sample, track, at, beats=None, offset=0.0, gain=0.0, fade_in=0.0, fade_out=0.0):
        """at, beats, offset, fades in beats; the clip is cut to the sound's length."""
        frames = self.samples[sample]["frames"]
        start, off = round(at * self.bf), round(offset * self.bf)
        length = frames - off if beats is None else min(round(beats * self.bf), frames - off)
        fi, fo = round(fade_in * self.bf), round(fade_out * self.bf)
        if fi + fo > length:
            fi = fo = length // 2
        self.clips.append((sample, track, start, off, length, gain, fi, fo))

    def every_bar(self, sample, track, bars, gain=0.0):
        for b in bars:
            self.clip(sample, track, b * 4, gain=gain)

    # ---- the file
    def write(self, path):
        t = lambda v: ("%.9g" % v)
        out = ["tinydaw-project 1", "saved-by tinydaw %s (examples/make_examples.py)" % VERSION,
               "title %s" % self.title,
               "rate %d" % RATE, "bpm %s" % t(self.bpm), "a4 %s" % t(self.a4), "master 0.8",
               "loop %d %d %d" % self.loop, "playhead 0", "instrument -1 60 0"]
        f = self.fx
        out.append("fx %d %s %s %s %d %s %s %s %d %s %s" % (f["div"], t(f["fb"]), t(f["tone"]), t(f["dlevel"]),
                   f["ping"], t(f["size"]), t(f["damp"]), t(f["rlevel"]), f["sc"], t(f["release"]), t(f["bend"])))
        for tr in self.tracks:
            out.append("track %s 0 0 %s" % (t(tr["vol"]), tr["name"]))
            out.append("trackfx %s %d %s %s %s %s %s %s %s %s %s" % (
                t(tr["pan"]), tr["filt"], t(tr["cutoff"]), t(tr["reso"]), t(tr["eq"][0]), t(tr["eq"][1]),
                t(tr["eq"][2]), t(tr["drive"]), t(tr["delay"]), t(tr["reverb"]), t(tr["duck"])))
        for s in self.samples:
            out.append("sample %s %d %s" % (s["kind"], s["frames"], s["name"]))
            if s["kind"] in ("gen", "chord"):
                w, n, b, v, a, r = s["gen"]
                out.append("gen %d %d %s %s %s %s %s %s" % (w, n, t(b), t(v), t(a), t(r), t(self.a4), t(self.bpm)))
                if s["kind"] == "chord":
                    out.append("chord %d %s" % (len(s["notes"]), " ".join(str(x) for x in s["notes"])))
            elif s["kind"] == "beat":
                g = s["genre"]
                out.append("gen 0 0 0 0 0 0 0 %s" % t(self.bpm))
                out.append("beat %d %s %d %s %s %s %s %s %s %d" % (
                    s["steps"], t(g["swing"]), g["swing8"], t(s["kick_hz"]), t(g["kick_decay"]), t(g["snare_hz"]),
                    t(g["hat_decay"]), t(g["open_decay"]), t(s["volume"]), s["mask"]))
                for i, lane in enumerate(g["lanes"]):
                    out.append("lane %d %s" % (i, lane or "-"))
            elif s["kind"] == "variant":
                out.append("variant %d %d %d" % (s["src"], s["semis"], s["rev"]))
            out.append("end")
        for b, n, name in sorted(self.sections):
            out.append("section %d %d %s" % (b, n, name))
        for c in sorted(self.clips, key=lambda c: (c[2], c[1])):
            out.append("clip %d %d %d %d %d %s %d %d" % (c[0], c[1], c[2], c[3], c[4], t(c[5]), c[6], c[7]))
        for i, p in enumerate(self.pads[:16]):
            out.append("pad %d %d %d %s 0 %d 0" % (i, p[0], p[1], t(p[2]), p[3]))
        with open(path, "w") as fh:
            fh.write("\n".join(out) + "\n")
        secs = max(c[2] + c[4] for c in self.clips) / RATE
        print("%-40s %3d sounds %4d clips %5.1f s" % (os.path.basename(path).replace(".tmp", ""), len(self.samples), len(self.clips), secs))


# ---------------------------------------------------------------- the newer tools
# Generator sounds (src/synth.c's presets, or a patch of our own), sung lines, note clips (the
# piano roll), automation, time signature and tempo changes, and the master's dynamics.

SW = dict(SW_SAW=0, SW_SQUARE=1, SW_TRI=2, SW_SINE=3)
SF = dict(SF_NONE=0, SF_LOW=1, SF_HIGH=2, SF_BAND=3)
AUTO_VOLUME, AUTO_PAN, AUTO_CUTOFF, AUTO_DELAY, AUTO_REVERB = range(5)
VS_ROBOT, VS_TALKBOX, VS_CHOIR, VS_WHISPER, VS_CLEAR = range(5)
AIRA, LUMI, KAI, REN = range(4)                  # the Clear singers (Aria, Lumi, Kai, Ren)


def load_presets():
    """The Generator's presets from src/synth.c: name -> (patch fields, note, seconds)."""
    src = open(os.path.join(ROOT, "src", "synth.c")).read()
    out = {}
    for m in re.finditer(r'PRESET\("([^"]+)",\s*"[^"]+",\s*"[^"]+",\s*"(?:[^"\\]|\\.)*",\s*(-?\d+),\s*([\d.]+),(.*?)\),\n', src, re.S):
        fields = {}
        for k, v in re.findall(r'\.(\w+)\s*=\s*([-\w.]+)', m.group(4)):
            fields[k] = SW.get(v, SF.get(v, None)) if v in SW or v in SF else float(v)
        out[m.group(1)] = (fields, int(m.group(2)), float(m.group(3)))
    return out


PRESETS = load_presets()
INT_FIELDS = ("wave", "noosc", "wave2", "voices", "bursts", "filter", "vowel", "vowel2")


def lufs_cutoff(hz):
    """A cutoff in Hz as an automation value (20 Hz .. 20 kHz, evenly by ear)."""
    return math.log(max(hz, 20) / 20) / math.log(1000)


class Song2(Song):
    """A song with a tempo map, Generator and sung sounds, note clips and automation."""

    def __init__(self, title, bpm, num=4, den=4):
        Song.__init__(self, title, bpm)
        self.num, self.den = num, den
        self.changes, self.notes, self.autos = [], [], []
        self.patterns = 0
        self.dynamics = None
        self.instrument = (-1, 60, 0)
        self.drumpats = []
        self.key0, self.keychanges, self.harm = (0, 0), [], []
        self.bends, self.cli = [], []                   # pitch curves; parts and fills for tinydaw to write
        self._last = None                               # the last clip: (index, beat it starts at, offset frames)

    # ---- velocity layers and round-robins (made by tinydaw from the sound, kept out of the list)
    def layers(self, sample, nvel=3, nrr=2):
        owner = self.samples[sample]
        table = [[sample] * 3 for _ in range(4)]
        for lv in range(nvel):
            for rr in range(nrr):
                if lv == nvel - 1 and rr == 0:
                    continue
                self.samples.append(dict(kind="layer", name="%s v%d r%d" % (owner["name"], lv + 1, rr + 1),
                                         frames=owner["frames"], src=sample, lvl=lv, n=nvel, rr=rr))
                table[lv][rr] = len(self.samples) - 1
        owner["layers"] = (nvel, nrr, table)
        return sample

    # ---- drum patterns (the Drums panel) and the drum clips that repeat them
    VEL = {".": 0, " ": 0, "g": 35, "o": 62, "x": 100, "X": 127}

    def drums(self, track, name, steps, rows, swing=0.0):
        """rows: (sound, hits[, chance[, volume]]). hits: one character a step (. rest, g ghost, o soft,
        x hit, X accent) or a list of velocities; chance: a digit d = d*10 % (anything else: always)."""
        out = []
        for row in rows:
            snd, hits = row[0], row[1]
            chance = row[2] if len(row) > 2 and row[2] else ""
            vol = row[3] if len(row) > 3 else 1.0
            vel = [self.VEL[c] for c in hits] if isinstance(hits, str) else list(hits)
            vel = (vel * steps)[:steps]
            pr = [int(c) * 10 if c.isdigit() and c != "0" else 100 for c in chance.ljust(steps, ".")][:steps]
            out.append((snd, vol, vel, pr))
        self.drumpats.append(dict(id=len(self.drumpats) + 1, track=track, name=name, steps=steps, rows=out,
                                  swing=swing))
        return len(self.drumpats)

    def drumclip(self, pid, track, at, beats):
        snd = self.drumpats[pid - 1]["rows"][0][0]
        start = self.frame(at)
        self.clips.append((snd, track, start, 0, self.frame(at + beats) - start, 0.0, 0, 0, -pid, 60))
        self._last = (len(self.clips) - 1, at, 0)

    # ---- the chord track and the key
    SCALE_NAMES = ["Major", "Minor", "Harm. minor", "Mel. minor", "Dorian", "Phrygian", "Lydian", "Mixolydian",
                   "Locrian", "Pent. major", "Pent. minor", "Blues"]
    SUFFIXES = ["", "m", "dim", "aug", "sus2", "sus4", "maj7", "7", "m7", "m7b5", "dim7", "m(maj7)", "+maj7", "6",
                "m6", "add9", "9", "maj9", "m9"]
    PCS = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}

    def _pc(self, name):
        return (self.PCS[name[0]] + (1 if name[1:] == "#" else -1 if name[1:] == "b" else 0)) % 12

    def key(self, name, bar=0):
        """'A Minor' from bar `bar` on (0: the song's key)."""
        note, scale = name.split(" ", 1)
        k = (self._pc(note), self.SCALE_NAMES.index(scale))
        if bar == 0:
            self.key0 = k
        else:
            self.keychanges.append((bar, k[0], k[1]))

    def harmony(self, bar, name, bars=1):
        """A chord on the chord track: 'Am7', 'G/B', for `bars` bars from bar `bar`."""
        m = re.match(r"^([A-G][#b]?)(.*?)(?:/([A-G][#b]?))?$", name)
        root, suffix, bass = m.group(1), m.group(2), m.group(3)
        at = self.bar(bar)
        self.harm.append((at, self.bar(bar + bars) - at, self._pc(root), self.SUFFIXES.index(suffix),
                          self._pc(bass) if bass else -1))

    # ---- pitch curves, and parts tinydaw writes from the chord track
    def bend(self, points, keep=1):
        """A pitch curve on the clip just added: (beat from the clip's start, semitones) points. keep=0 bends
        an audio clip like tape (speed with pitch)."""
        idx, at, off = self._last
        start = self.frame(at)
        pts = [((self.frame(at + b) - start + off) / RATE, st) for b, st in sorted(points)]
        self.bends.append((len(self.bends) + 1, keep, pts))
        c = self.clips[idx]
        self.clips[idx] = (c + (0, 0) if len(c) == 8 else c) + (len(self.bends),)

    def part(self, spec):
        """A part tinydaw writes from the chord track: "bass,style=Walking,seed=3,sound=...,bars=5-13,track=Bass"."""
        self.cli += ["--part", spec]

    def fills(self, spec):
        self.cli += ["--fills", spec]

    # ---- the tempo map: quarter notes ("beats") and bars, to frames
    def _sections(self):
        out, q, f, bar = [], 0.0, 0.0, 0
        bpm, num, den = self.bpm, self.num, self.den
        for b, nb, nn, nd in [(0, bpm, num, den)] + self.changes:
            if out:
                pb, pq, pf, pqf, pbarq = out[-1][0], out[-1][1], out[-1][2], out[-1][3], out[-1][4]
                q = pq + (b - pb) * pbarq
                f = pf + (b - pb) * pbarq * pqf
            out.append((b, q, f, 60.0 / nb * RATE, nn * 4.0 / nd))
        return out

    def change(self, bar, bpm, num, den):
        self.changes.append((bar, bpm, num, den))

    def bar(self, b):
        """The beat (quarter note) where bar b (0-based) starts."""
        sec = [s for s in self._sections() if s[0] <= b][-1]
        return sec[1] + (b - sec[0]) * sec[4]

    def frame(self, q):
        sec = [s for s in self._sections() if s[1] <= q + 1e-9][-1]
        return int(round(sec[2] + (q - sec[1]) * sec[3]))

    # ---- sounds
    def synth(self, name, preset=None, patch=None, note=None, seconds=None, volume=0.8, macros=(0.5,) * 5):
        fields, pnote, psec = PRESETS[preset] if preset else ({}, 60, 1.0)
        fields = dict(fields)
        fields.update(patch or {})
        note = pnote if note is None else note
        seconds = psec if seconds is None else seconds
        self.samples.append(dict(kind="synth", name=name, frames=int(seconds * RATE), patch=fields, note=note,
                                 seconds=seconds, volume=volume, macros=macros))
        return len(self.samples) - 1

    def vocal(self, name, words, notes, style=VS_CLEAR, singer=KAI, high=0, vibrato=0.45, volume=0.8, bends=()):
        """notes: (MIDI note, start, length) in beats from the sound's start, one per syllable.
        bends: the pitch curve, (beat, semitones) points (see shape())."""
        end = max(st + ln for _, st, ln in notes)
        self.samples.append(dict(kind="vocal", name=name, frames=int((end * 60.0 / self.bpm + 0.25) * RATE),
                                 words=words, vnotes=notes, style=style, singer=singer, high=high,
                                 vibrato=vibrato, volume=volume, bends=sorted(bends)))
        return len(self.samples) - 1

    def variant2(self, name, src, semis=0, reverse=0):
        self.samples.append(dict(kind="variant", name=name, frames=self.samples[src]["frames"], src=src, semis=semis,
                                 rev=reverse))
        return len(self.samples) - 1

    # ---- clips, in beats on the tempo map
    def clip(self, sample, track, at, beats=None, offset=0.0, gain=0.0, fade_in=0.0, fade_out=0.0):
        frames = self.samples[sample]["frames"]
        start = self.frame(at)
        off = self.frame(offset) if offset else 0
        length = frames - off if beats is None else min(self.frame(at + beats) - start, frames - off)
        fi = self.frame(at + fade_in) - start if fade_in else 0
        fo = self.frame(at + beats) - self.frame(at + beats - fade_out) if fade_out and beats else (
            int(fade_out * 60.0 / self.bpm * RATE) if fade_out else 0)
        if fi + fo > length:
            fi = fo = length // 2
        self.clips.append((sample, track, start, off, length, gain, fi, fo))
        self._last = (len(self.clips) - 1, at, off)

    def noteclip(self, sample, track, at, beats, notes, root=None):
        """A note clip at `at` for `beats`, playing `sample` pitched to each (note, start, length, vel)."""
        self.patterns += 1
        pat = self.patterns
        root = self.samples[sample].get("note", 60) if root is None else root
        start = self.frame(at)
        for n, st, ln, vel in notes:
            a = self.frame(at + st) - start
            self.notes.append((pat, n, a, max(48, self.frame(at + st + ln) - self.frame(at + st)), vel))
        self.clips.append((sample, track, start, 0, self.frame(at + beats) - start, 0.0, 0, 0, pat, root))
        self._last = (len(self.clips) - 1, at, 0)

    def auto(self, track, param, at, value):
        self.autos.append((track, param, self.frame(at), value))

    # ---- the file
    def write(self, path):
        Song.write(self, path + ".tmp")
        lines = open(path + ".tmp").read().splitlines()
        os.remove(path + ".tmp")
        t = lambda v: ("%.9g" % v)
        head, rest = [], []
        for ln in lines:                                 # the base writes samples it doesn't know as bare
            (rest if rest or ln.startswith("track ") else head).append(ln)
        head = [ln for ln in head if not ln.startswith("instrument ")]
        head.append("instrument %d %d %d" % self.instrument)
        if (self.num, self.den) != (4, 4):
            head.append("meter %d %d" % (self.num, self.den))
        for b, bpm, num, den in self.changes:
            head.append("change %d %s %d %d" % (b, t(bpm), num, den))
        if self.key0 != (0, 0):
            head.append("key %d %d" % self.key0)
        for kc in self.keychanges:
            head.append("keychange %d %d %d" % kc)
        for h in sorted(self.harm):
            head.append("harmony %s %s %d %d %d" % (t(h[0]), t(h[1]), h[2], h[3], h[4]))
        if self.dynamics:
            head.append("dynamics %d %s %s %s %s %s %d %s" % tuple(
                t(v) if isinstance(v, float) else v for v in self.dynamics))
        out, i = head, 0
        rest = [ln for ln in rest if not ln.startswith("clip ")]
        k = 0
        while k < len(rest):                             # sounds: put in the new kinds' own lines
            ln = rest[k]
            if ln.startswith("sample "):
                s = self.samples[i]
                i += 1
                if s["kind"] == "layer":
                    out.append("sample layer %d %s" % (s["frames"], s["name"]))
                    out.append("layer %d %d %d %d" % (s["src"], s["lvl"], s["n"], s["rr"]))
                    out.append("hidden")
                    out.append("end")
                    while rest[k] != "end":
                        k += 1
                    k += 1
                    continue
                if s["kind"] == "synth":
                    out.append("sample synth %d %s" % (s["frames"], s["name"]))
                    m = s["macros"]
                    out.append("synth %d %s %s 440 %s" % (s["note"], t(s["seconds"]), t(s["volume"]),
                                                          " ".join(t(x) for x in m)))
                    out.append("patch" + "".join(" %s=%s" % (key, int(v) if key in INT_FIELDS else t(v))
                                                 for key, v in sorted(s["patch"].items()) if v))
                    out += self._layer_line(s)
                    out.append("end")
                    while rest[k] != "end":
                        k += 1
                    k += 1
                    continue
                if s["kind"] == "vocal":
                    out.append("sample vocal %d %s" % (s["frames"], s["name"]))
                    out.append("vocal %d %d 0 %s %s 440 %s 6 60 0.5 %d" % (
                        s["style"], s["high"], t(s["vibrato"]), t(s["volume"]), t(self.bpm), s["singer"]))
                    out.append("words %s" % s["words"])
                    for n, st, ln_ in s["vnotes"]:
                        out.append("vnote %d %s %s" % (n, t(st), t(ln_)))
                    for b, st in s["bends"]:
                        out.append("vbend %s %s" % (t(b), t(st)))
                    out += self._layer_line(s)
                    out.append("end")
                    while rest[k] != "end":
                        k += 1
                    k += 1
                    continue
            if ln == "end" and i > 0 and self.samples[i - 1]["kind"] not in ("synth", "vocal", "layer"):
                out += self._layer_line(self.samples[i - 1])    # (a sound the base wrote)
            if ln.startswith("pad ") and not any(o.startswith("clip ") for o in out):
                for d in self.drumpats:                  # drum patterns, before the clips that play them
                    out.append("drum %d %d %d %d %s %s" % (d["id"], d["track"], d["steps"], len(d["rows"]),
                                                           t(d["swing"]), d["name"]))
                    for r, (snd, vol, vel, pr) in enumerate(d["rows"]):
                        out.append("drow %d %d %s %s" % (r, snd, t(vol), " ".join("%d:%d" % vp for vp in zip(vel, pr))))
                for bid, keep, pts in self.bends:            # pitch curves, before the clips that use them
                    out.append("bend %d %d" % (bid, keep))
                    for at, st in pts:
                        out.append("bpt %s %s" % (t(at), t(st)))
                for c in sorted(self.clips, key=lambda c: (c[2], c[1])):
                    extra = " %d %d" % (c[8], c[9]) if len(c) > 8 else ""
                    extra += " %d" % c[10] if len(c) > 10 else ""
                    out.append("clip %d %d %d %d %d %s %d %d%s" % (c[0], c[1], c[2], c[3], c[4], t(c[5]), c[6], c[7], extra))
                for n in self.notes:
                    out.append("note %d %d %d %d %s" % (n[0], n[1], n[2], n[3], t(n[4])))
                for a in sorted(self.autos, key=lambda a: (a[0], a[1], a[2])):
                    out.append("auto %d %d %d %s" % (a[0], a[1], a[2], t(a[3])))
            out.append(ln)
            k += 1
        with open(path, "w") as fh:
            fh.write("\n".join(out) + "\n")


    def _layer_line(self, s):
        if "layers" not in s:
            return []
        nv, nr, table = s["layers"]
        return ["layers %d %d %s" % (nv, nr, " ".join(str(table[l][r]) for l in range(4) for r in range(3)))]


def shape(notes, scoop=1.2, slide=True, fall=3.0):
    """A sung line's pitch curve, as the Singer's buttons make it: a scoop up into each phrase's first
    note, glides between joined notes, and a fall at each phrase's end. notes: (note, start, length)."""
    pts = []
    for i, (n, st, ln) in enumerate(notes):
        end = st + ln
        prev = notes[i - 1] if i else None
        nxt = notes[i + 1] if i + 1 < len(notes) else None
        if scoop and (not prev or st - (prev[1] + prev[2]) > 0.2):
            d = min(0.15, ln / 3)
            pts += [(st - 0.002, 0.0), (st, -scoop), (st + d, 0.0)]
        if slide and nxt and nxt[1] - end < 0.3 and nxt[0] != n and ln > 0.3:
            d = min(0.2, ln / 3)
            pts += [(end - d, 0.0), (end - 0.002, float(nxt[0] - n))]
        if fall and (not nxt or nxt[1] - end > 0.2) and ln >= 0.75:
            d = min(0.35, ln / 3)
            pts += [(end - d, 0.0), (end - 0.002, -fall), (end + 0.002, 0.0)]
    pts.sort()
    out = []
    for b, v in pts:                                 # one point a beat
        if out and abs(out[-1][0] - b) < 1e-6:
            continue
        out.append((round(b, 4), v))
    return out


def roll(s, sound, track, at, beats, step, gain0=-14.0, gain1=0.0, pitches=None):
    """A drum roll: `sound` every `step` beats for `beats`, getting louder (and higher, with pitches)."""
    n = int(round(beats / step))
    for i in range(n):
        snd = pitches[min(len(pitches) - 1, i * len(pitches) // n)] if pitches else sound
        s.clip(snd, track, at + i * step, beats=step, gain=gain0 + (gain1 - gain0) * i / max(1, n - 1))


# ---------------------------------------------------------------- the songs
# Every drum part is a pattern in the Drums panel (velocity and a chance on each step, swing), the
# drums and keys have velocity layers and round-robins, the sung lines have pitch curves, and each
# song's parts are named on the arranger strip.

def kit(s, layered):
    for x in layered:
        s.layers(x, 3, 2)


def split_beat(s, t_kick, t_drums, name, steps, kick_row, rows, swing=0.0):
    """A beat as two patterns: the kick on its own track (the sidechain listens to it), the rest on the
    drum track. Returns them as a pair for place()."""
    return (s.drums(t_kick, name + " kick", steps, [kick_row], swing), s.drums(t_drums, name, steps, rows, swing),
            t_kick, t_drums)


def place(s, beat, at, beats):
    k, d, tk, td = beat
    s.drumclip(k, tk, at, beats)
    s.drumclip(d, td, at, beats)


# ---------------------------------------------------------------- 1. house

def house():
    s = Song2("House: Late Train", 124)
    # A minor: Am7 - Fmaj7 - Cmaj7 - G6, a bar each
    chords = [("Am7", [57, 60, 64, 67], 45), ("Fmaj7", [53, 57, 60, 64], 41),
              ("Cmaj7", [55, 59, 60, 64], 48), ("G6", [55, 59, 62, 64], 43)]
    kick = s.synth("Floor kick", "Floor kick")
    clap = s.synth("Clap", "Clap", macros=(0.55, 0.5, 0.6, 0.45, 0.5))
    hat = s.synth("Closed hat", "Trap hat", seconds=0.12, macros=(0.55, 0.45, 0.5, 0.35, 0.5))
    ohat = s.synth("Open hat", "Open hat", seconds=0.35)
    rim = s.synth("Rim", "Rimshot")
    bass = s.synth("Deep bass", "Deep bass", seconds=0.6, macros=(0.5, 0.4, 0.55, 0.5, 0.5))
    stab = s.synth("Piano stab", "Piano stab", seconds=0.8)
    pluck = s.synth("Pluck", "Pop pluck", seconds=0.9, macros=(0.6, 0.5, 0.5, 0.4, 0.5))
    kit(s, (kick, clap, hat, ohat, rim, bass, stab, pluck))
    pad = {n: s.chord("%s pad" % n, [x - 12 for x in notes] + [notes[-1]], TRI, 4, 0.30, 450, 900)
           for n, notes, _ in chords}
    riser = s.synth("Riser", "Riser", seconds=4)
    impact = s.synth("Impact", "Impact")
    hook_notes = [(76, 0, 1), (74, 1, 1.5), (72, 3, 0.5), (74, 3.5, 0.5), (76, 4, 1), (69, 5, 2.5),
                  (72, 8, 0.5), (74, 8.5, 0.5), (76, 9, 1.5), (79, 11, 0.5), (76, 11.5, 0.5), (74, 12, 0.5), (72, 12.5, 3)]
    hook = s.vocal("Sung: late train", "Late train, car-ry me home. Ci-ty lights, I'm not a-lone.",
                   [(n, st, ln * 0.95) for n, st, ln in hook_notes], singer=LUMI, high=1, vibrato=0.4,
                   bends=shape([(n, st, ln * 0.95) for n, st, ln in hook_notes], scoop=1.0, fall=2.5))

    t_kick = s.track("Kick", 0.9)
    t_drums = s.track("Drums", 0.7, pan=0.1, eq=(0, 0, 1), reverb=0.12)
    t_bass = s.track("Bass", 0.75, filt=1, cutoff=900, reso=0.3, drive=0.2, duck=0.7)
    t_stab = s.track("Stabs", 0.65, pan=-0.15, delay=0.3, reverb=0.3, duck=0.5)
    t_pad = s.track("Pad", 0.55, pan=0.2, filt=1, cutoff=3200, reverb=0.55, duck=0.35)
    t_lead = s.track("Pluck", 0.5, pan=0.1, delay=0.4, reverb=0.3, duck=0.3)
    t_vox = s.track("Vocal", 0.8, delay=0.2, reverb=0.35, duck=0.15)
    t_fx = s.track("FX", 0.55, reverb=0.4)
    s.fx.update(div=4, fb=0.38, tone=0.55, dlevel=0.7, ping=1, size=0.7, damp=0.4, rlevel=0.8, sc=t_kick, release=150)
    s.dynamics = (1, -18.0, 2.5, 15.0, 180.0, 3.0, 1, -1.0)

    intro = s.drums(t_drums, "Intro hats", 16, [(hat, "x..gx..gx..gx.gg", "...6...6...6..66"),
                                                 (ohat, "..o...o...o...o.")], swing=0.04)
    groove = split_beat(s, t_kick, t_drums, "Groove", 16, (kick, "X...x...X...x..."),
                        [(clap, "....X.......X..."), (hat, "x..gx..gx..gx.gg", "...6...6...6..66"),
                         (ohat, "..X...x...X...x.")], swing=0.04)
    build = split_beat(s, t_kick, t_drums, "Build", 32, (kick, "X...x...X...x...X...x...X...x.x."),
                       [(clap, "....X.......X.......X...g.o.oxxX", "........................5.7....."),
                        (hat, "x.gxx.gxx.gxx.gxx.gxx.gxxgxxxxXX", "..5...5...5...5...5...5........"),
                        (ohat, "..X...x...X...x...X...x.........")], swing=0.04)
    drop = split_beat(s, t_kick, t_drums, "Drop", 32, (kick, "X...x...X...x...X...x...X...x..."),
                      [(clap, "....X.......X.......X.......X..g", "...............................4"),
                       (hat, "x..gx.ggx..gx.ggx..gx.ggx..gxggg", "...6..55...6..55...6..55...6.555"),
                       (ohat, "..X...x...X...x...X...x...X...X."),
                       (rim, "...x..g....x..g....x..g...x..g.o", "...5..4....7..4....5..4...7..45.")], swing=0.04)
    s.drumclip(intro, t_drums, 0, 16)
    place(s, groove, s.bar(4), 32)
    place(s, build, s.bar(12), 16)
    place(s, drop, s.bar(20), 48)
    s.clip(riser, t_fx, s.bar(18), beats=8, fade_in=4)
    s.clip(impact, t_fx, s.bar(20))
    s.clip(impact, t_fx, s.bar(12), gain=-6)

    for bar in range(32):
        name, notes, root = chords[bar % 4]
        if 4 <= bar < 16 or 20 <= bar < 32:           # off-beat bass, harder on the and of one and three
            s.noteclip(bass, t_bass, s.bar(bar), 4, [(root, off, 0.4, v) for off, v in
                                                     ((0.5, 0.95), (1.5, 0.7), (2.5, 0.9), (3.5, 0.6))])
        if 12 <= bar < 16 or 20 <= bar < 32:           # stabs: accented, then lighter answers
            s.noteclip(stab, t_stab, s.bar(bar), 4, [(n, off, 0.3, v) for off, v in
                                                     ((0.75, 0.9), (1.75, 0.5), (2.5, 0.75), (3.5, 0.45)) for n in notes])
        s.clip(pad[name], t_pad, s.bar(bar), fade_in=2 if bar == 0 else 0, fade_out=4 if bar == 31 else 0)
    phrase = [(0, 76, 1), (1, 74, 0.5), (1.5, 72, 0.5), (2, 69, 1.5), (4, 72, 0.5), (4.5, 74, 0.5), (5, 76, 1),
              (6, 79, 1), (7, 76, 1), (8, 81, 1.5), (9.5, 79, 0.5), (10, 76, 1), (11, 74, 0.5), (11.5, 72, 0.5),
              (12, 74, 1), (13, 72, 1), (14, 69, 2)]
    for start in (24, 28):                          # the pluck answers the hook, softly then strongly
        s.noteclip(pluck, t_lead, s.bar(start), 16, [(n, at, ln, (0.55 if start == 24 else 0.8) + 0.15 * (k % 2 == 0))
                                                      for k, (at, n, ln) in enumerate(phrase)])
    s.clip(hook, t_vox, s.bar(16))                  # the hook alone over the break, then in the drop
    s.clip(hook, t_vox, s.bar(24))
    s.pads = [(kick, 0, 0.9, t_kick), (clap, 0, 0.8, t_drums), (hat, 0, 0.7, t_drums), (ohat, 0, 0.7, t_drums),
              (rim, 0, 0.8, t_drums)] + [(pad[n], 1, 0.7, t_pad) for n, _, _ in chords] + \
             [(hook, 0, 0.8, t_vox), (riser, 0, 0.8, t_fx), (impact, 0, 0.9, t_fx)]
    s.instrument = (stab, 60, t_stab)
    s.loop = (1, s.frame(s.bar(20)), s.frame(s.bar(24)))
    s.section(0, 4, "Intro")
    s.section(4, 8, "Groove")
    s.section(12, 4, "Build")
    s.section(16, 4, "Break")
    s.section(20, 12, "Drop")
    s.key("A Minor")
    for bar in range(32):
        s.harmony(bar, ["Am7", "Fmaj7", "Cmaj7", "G6"][bar % 4])
    return s






# ---------------------------------------------------------------- 4. breakcore

def breakcore():
    s = Song2("Breakcore: Static Bloom", 86)
    # A half-time intro at 86, the break at 172, four bars of 7/8, then a faster run to the end at 186.
    s.change(4, 172, 4, 4)
    s.change(20, 172, 7, 8)
    s.change(24, 186, 4, 4)
    kick = s.synth("Break kick", "Boom bap kick", macros=(0.6, 0.35, 0.6, 0.6, 0.5))
    snare = s.synth("Break snare", "Break snare", macros=(0.65, 0.4, 0.5, 0.65, 0.5))
    hat = s.synth("Hat", "Trap hat", macros=(0.6, 0.4, 0.5, 0.5, 0.5))
    ohat = s.synth("Open hat", "Open hat")
    kit(s, (kick, snare, hat, ohat))
    stutter = [snare] + [s.variant2("Snare +%d" % k, snare, k) for k in (3, 5, 7, 12)]
    pad = s.synth("Bloom pad", "Shimmer pad", seconds=6, macros=(0.45, 0.6, 0.7, 0.4, 0.55))
    reese = s.synth("Reese", "Reese", seconds=2, macros=(0.5, 0.45, 0.65, 0.7, 0.6))
    bell = s.synth("Glass bell", "Glass bell", seconds=3)
    kit(s, (reese, bell))
    chop = s.synth("Vocal chop", "Vocal chop")
    zap = s.synth("Laser zap", "Laser zap")
    riser = s.synth("Riser", "Riser", seconds=5)
    impact = s.synth("Impact", "Impact")
    robo_notes = [(63, 0, 0.5), (63, 0.5, 0.5), (70, 1, 1), (66, 2.5, 0.5), (68, 3, 1.5)]
    robo = s.vocal("Sung: static bloom", "Stat-ic, bloom a-gain", robo_notes, style=VS_ROBOT, high=1, vibrato=0.1,
                   bends=[(0.0, 0.0), (0.45, 0.0), (0.5, 7.0), (0.95, 0.0), (1.0, -3.0), (1.3, 0.0), (2.4, 0.0),
                          (2.9, 2.0), (3.0, 0.0), (4.0, 0.0), (4.5, 12.0)])

    t_kick = s.track("Kick", 0.85, drive=0.25)
    t_snare = s.track("Drums", 0.75, eq=(0, 1, 2), reverb=0.12)
    t_hat = t_snare
    t_pad = s.track("Pad", 0.62, filt=1, cutoff=900, reverb=0.5, duck=0.6)
    t_bass = s.track("Reese", 0.6, filt=1, cutoff=900, reso=0.3, drive=0.3, duck=0.7)
    t_bell = s.track("Bell", 0.45, pan=-0.2, delay=0.4, reverb=0.4)
    t_vox = s.track("Voices", 0.55, pan=0.15, delay=0.3, reverb=0.25)
    t_fx = s.track("FX", 0.6, reverb=0.35)
    s.fx.update(div=3, fb=0.45, tone=0.55, dlevel=0.6, ping=1, size=0.8, damp=0.35, rlevel=0.8, sc=t_kick, release=120)
    s.dynamics = (1, -22.0, 4.0, 6.0, 110.0, 7.0, 1, -1.0)   # Loud

    chords = [[51, 58, 61, 65], [47, 54, 58, 63], [54, 58, 61, 65], [49, 56, 58, 61]]
    roots = [27, 23, 30, 25]
    s.noteclip(pad, t_pad, 0, s.bar(4), [(n, i * 4, 4, 0.7) for i in range(4) for n in chords[i]])
    s.auto(t_pad, AUTO_CUTOFF, 0, lufs_cutoff(300))
    s.auto(t_pad, AUTO_CUTOFF, s.bar(4), lufs_cutoff(6000))
    s.auto(t_pad, AUTO_CUTOFF, s.bar(20), lufs_cutoff(6000))
    s.auto(t_pad, AUTO_CUTOFF, s.bar(24), lufs_cutoff(1800))
    s.noteclip(bell, t_bell, 0, s.bar(4), [(75, 0, 1, 0.75), (73, 2, 1, 0.45), (70, 4, 2, 0.7), (68, 8, 1, 0.4),
                                            (70, 10, 2, 0.6), (73, 12, 3, 0.85)])
    s.clip(riser, t_fx, s.bar(3), beats=4, fade_in=2)

    # the break: four amen-style patterns (two bars each), ghost snares that come and go
    def pat(name, k, sn, hats, chance=""):
        return split_beat(s, t_kick, t_snare, name, 32, (kick, k),
                          [(snare, sn, chance), (hat, hats, "..5...5...5...5...5...5...5...5.")])
    hat8 = "x.g.o.g.x.g.o.g.x.g.o.g.x.g.o.g."
    hat16 = "xgogxgogxgogxgogxgogxgogxgogxgox"
    a = pat("Break A", "X.x.......XX....X.x.......x.....", "....X..g.g....X.....X..g.X....X.", hat8, "........5..............6........")
    b2 = pat("Break B", "X.x.....X.x.....X...x...X.x.....", "....X....gX...X.....X..g.g..X..X", hat8, ".........6.............5.6......")
    c = pat("Break C", "X.x.......XX....X.x.......x...x.", "....X..g.g....X.....X..g.X..gXgX", hat16, "........5..............6....4.5.")
    for i, b in enumerate(range(4, 20, 2)):
        place(s, (a, b2)[i % 2], s.bar(b), s.bar(b + 2) - s.bar(b))
        if b % 4 == 2:                              # a stutter roll, rising, over the last beat of every fourth bar
            roll(s, snare, t_snare, s.bar(b + 2) - 1, 1, 0.125, -12, 0, stutter)
    for b in range(20, 24):                         # 7/8: the pattern is cut at each bar's end
        place(s, a if b % 2 == 0 else b2, s.bar(b), s.bar(b + 1) - s.bar(b))
    for b in range(24, 36, 2):
        place(s, c if b >= 28 else a, s.bar(b), s.bar(b + 2) - s.bar(b))
        s.clip(ohat, t_hat, s.bar(b) + 1.5, beats=0.5, gain=-4)
    roll(s, snare, t_snare, s.bar(36) - 1, 1, 0.125, -12, 0, stutter)
    s.clip(impact, t_fx, s.bar(4))
    s.clip(impact, t_fx, s.bar(24))
    for b in range(4, 36):
        i = (b - 4) % 4
        bars_q = s.bar(b + 1) - s.bar(b)
        s.noteclip(pad, t_pad, s.bar(b), bars_q, [(n, 0, bars_q, 0.55) for n in chords[i]])
        r = roots[i] + 12
        s.noteclip(reese, t_bass, s.bar(b), bars_q, [(r, 0, bars_q * 0.75, 0.9), (r + 12 if b % 2 else r, bars_q * 0.75,
                                                                                   bars_q * 0.25, 0.55)])
    s.auto(t_bass, AUTO_CUTOFF, s.bar(24), lufs_cutoff(500))
    for b in range(24, 36):
        s.auto(t_bass, AUTO_CUTOFF, s.bar(b) + 1, lufs_cutoff(2600 if b % 2 else 1400))
        s.auto(t_bass, AUTO_CUTOFF, s.bar(b) + 3, lufs_cutoff(500))
    for b in list(range(20, 24)):
        s.clip(chop, t_vox, s.bar(b) + 1.5, beats=0.5)
        s.clip(chop, t_vox, s.bar(b) + 2.25, beats=0.5, gain=-3)
    for b in (12, 16, 28, 32):
        s.clip(robo, t_vox, s.bar(b) + 1, gain=-2)
    for b in (8, 16, 26, 30, 34):
        s.clip(zap, t_fx, s.bar(b) + 3.5, gain=-6)
    s.clip(riser, t_fx, s.bar(22), beats=s.bar(24) - s.bar(22), fade_in=1)
    s.clip(pad, t_pad, s.bar(36), beats=6, fade_out=4)
    s.pads = [(kick, 0, 0.9, t_kick), (snare, 0, 0.9, t_snare), (hat, 0, 0.7, t_hat), (ohat, 0, 0.7, t_hat)] + \
             [(x, 0, 0.8, t_snare) for x in stutter[1:]] + \
             [(chop, 0, 0.8, t_vox), (robo, 0, 0.8, t_vox), (zap, 0, 0.8, t_fx), (impact, 0, 0.9, t_fx),
              (riser, 0, 0.8, t_fx), (bell, 0, 0.8, t_bell), (pad, 1, 0.7, t_pad)]
    s.instrument = (bell, 72, t_bell)
    s.section(0, 4, "Intro")
    s.section(4, 16, "Break")
    s.section(20, 4, "7/8")
    s.section(24, 12, "Finale")
    s.key("Eb Minor")
    for b in range(36):
        s.harmony(b, ["Ebsus2", "Bmaj7", "Gbmaj7", "Db6"][b % 4])
    return s




# ---------------------------------------------------------------- 6. trap

def trap():
    s = Song2("Trap: Midnight Hollow", 140)
    # F minor: Fm - Db - Ab - Eb. Half-time: the clap on beat 3. An intro opening up under a filter,
    # two drops with an 808 bassline and hat rolls, a break with a vocal tag, and a short outro.
    b808 = s.synth("808", "808", seconds=1.8, macros=(0.5, 0.5, 0.6, 0.6, 0.5))
    kick = s.synth("Kick", "Boom bap kick", macros=(0.55, 0.3, 0.5, 0.55, 0.5))
    clap = s.synth("Clap", "Clap", macros=(0.6, 0.5, 0.6, 0.5, 0.5))
    hat = s.synth("Hat", "Trap hat", macros=(0.6, 0.4, 0.5, 0.45, 0.5))
    ohat = s.synth("Open hat", "Open hat", seconds=0.4)
    snap = s.synth("Snap", "Snap")
    bell = s.synth("Dark bell", "Dark bell", seconds=1.6)
    kit(s, (b808, kick, clap, hat, ohat, snap, bell))
    hats = [hat] + [s.variant2("Hat +%d" % k, hat, k) for k in (2, 5, 7)]
    pad = s.synth("Night pad", "Silk pad", seconds=4, macros=(0.4, 0.6, 0.6, 0.4, 0.5))
    riser = s.synth("Riser", "Riser", seconds=4)
    impact = s.synth("Impact", "Impact")
    tag_notes = [(65, 0, 0.5), (63, 0.5, 1), (60, 2, 0.5), (58, 2.5, 1.5), (65, 5, 0.5), (68, 5.5, 1.5)]
    tag = s.vocal("Sung: tag", "Mid-night, hol-low. Let's go!", tag_notes, style=VS_TALKBOX, high=0, vibrato=0.2,
                  bends=shape(tag_notes, scoop=2.0, slide=True, fall=5.0))

    t_808 = s.track("808", 0.75, drive=0.25)
    t_kick = s.track("Kick", 0.8)
    t_clap = s.track("Drums", 0.7, pan=0.1, eq=(0, 0, 1), reverb=0.15)
    t_hats = t_clap
    t_bell = s.track("Bell", 0.55, pan=-0.15, filt=1, cutoff=4000, delay=0.35, reverb=0.4, duck=0.3)
    t_pad = s.track("Pad", 0.5, filt=1, cutoff=1200, reverb=0.5, duck=0.5)
    t_vox = s.track("Tag", 0.7, delay=0.4, reverb=0.35)
    t_fx = s.track("FX", 0.55, reverb=0.4)
    s.fx.update(div=4, fb=0.4, tone=0.5, dlevel=0.6, ping=1, size=0.7, damp=0.5, rlevel=0.7, sc=t_kick, release=200)
    s.dynamics = (1, -22.0, 4.0, 8.0, 120.0, 7.0, 1, -1.0)   # Loud

    chords = [[53, 56, 60], [49, 53, 56], [48, 51, 56], [51, 55, 58]]
    roots = [29, 25, 32, 27]
    melody = [(72, 0, 0.5), (75, 0.75, 0.5), (77, 1.5, 1), (75, 3, 0.5), (72, 4, 0.5), (70, 4.75, 0.5),
              (72, 5.5, 1.5), (68, 8, 0.5), (70, 8.75, 0.5), (72, 9.5, 1), (75, 11, 0.5), (77, 12, 1.5),
              (75, 13.75, 0.5), (72, 14.5, 1.5)]
    for b in range(30):
        s.noteclip(pad, t_pad, s.bar(b), 4, [(n, 0, 4, 0.6) for n in chords[b % 4]])
    s.auto(t_pad, AUTO_CUTOFF, 0, lufs_cutoff(250))
    s.auto(t_pad, AUTO_CUTOFF, s.bar(4), lufs_cutoff(2400))
    s.auto(t_bell, AUTO_CUTOFF, 0, lufs_cutoff(400))
    s.auto(t_bell, AUTO_CUTOFF, s.bar(4), lufs_cutoff(9000))
    for start in (0, 8, 16, 24):
        s.noteclip(bell, t_bell, s.bar(start), 16, [(n, st, ln, 0.55 + 0.35 * (ln >= 1)) for n, st, ln in melody])
    s.clip(riser, t_fx, s.bar(2), beats=8, fade_in=2)
    s.clip(riser, t_fx, s.bar(14), beats=8, fade_in=2)
    for b in (4, 16):
        s.clip(impact, t_fx, s.bar(b))
    # two-bar drum patterns: kick on the one and the "and" of two, the clap on three, hats in eighths
    # with sixteenth bursts that only sometimes happen
    a = split_beat(s, t_kick, t_clap, "Drop A", 32, (kick, "X......o........X......o.x......"),
                                      [(clap, "........X...............X......."),
                                       (hat, "x.o.x.o.x.oox.o.x.o.x.o.xoxox.xo", "..........55.............5.5.5.."),
                                       (ohat, "..............................x."),
                                       (snap, "............g...............g...", "............5...............6...")])
    b2 = split_beat(s, t_kick, t_clap, "Drop B", 32, (kick, "X......o.x......X......o...x.x.."),
                                       [(clap, "........X...............X.....g.", "..............................6."),
                                        (hat, "xoxox.o.x.o.x.o.x.oxoxo.xgxgxoxX", ".5.5......................5.5..."),
                                        (ohat, "......x.......................x.")])
    drops = list(range(4, 12, 2)) + list(range(16, 28, 2))
    for b in drops:
        place(s, a if b % 4 == 0 else b2, s.bar(b), 8)
        if b % 4 == 2:                              # a 32nd-note or triplet roll to end each phrase
            roll(s, hat, t_hats, s.bar(b + 1) + 3, 1, 0.125 if b % 8 == 2 else 1 / 6.0, -10, 0, hats)
    for b in drops:
        for bb in (b, b + 1):
            r = roots[bb % 4]
            notes = [(r, 0, 1.5, 0.95), (r, 1.75, 0.75, 0.6), (r + 7, 3, 0.5, 0.75)]
            if bb % 4 == 3:
                notes[-1] = (r + 12, 3, 1, 0.9)
            s.noteclip(b808, t_808, s.bar(bb), 4, notes, root=36)
    s.clip(tag, t_vox, s.bar(12))
    s.clip(tag, t_vox, s.bar(14), gain=-2)
    for b in range(12, 16):
        s.noteclip(b808, t_808, s.bar(b), 4, [(roots[b % 4], 0, 3.5, 0.5)], root=36)
    s.clip(tag, t_vox, s.bar(28))
    s.clip(pad, t_pad, s.bar(30), beats=4, fade_out=3)
    s.pads = [(kick, 0, 0.9, t_kick), (clap, 0, 0.9, t_clap)] + [(h, 0, 0.7, t_hats) for h in hats] + \
             [(ohat, 0, 0.7, t_hats), (snap, 0, 0.8, t_clap), (b808, 0, 0.9, t_808), (bell, 0, 0.8, t_bell),
              (pad, 1, 0.6, t_pad), (riser, 0, 0.8, t_fx), (impact, 0, 0.9, t_fx), (tag, 0, 0.8, t_vox)]
    s.instrument = (bell, 72, t_bell)
    s.loop = (1, s.frame(s.bar(4)), s.frame(s.bar(12)))
    s.section(0, 4, "Intro")
    s.section(4, 8, "Drop")
    s.section(12, 4, "Break")
    s.section(16, 12, "Drop")
    s.section(28, 2, "Outro")
    s.key("F Minor")
    for b in range(30):
        s.harmony(b, ["Fm", "Db", "Ab", "Eb"][b % 4])
    return s


# ---------------------------------------------------------------- 7. synthwave

def synthwave():
    s = Song2("Synthwave: Neon Coastline", 100)
    # F# minor: F#m - D - A - E, a bar each. Gated snare, a pulsing bass and sixteenth arps under a
    # retro lead, the pad and bass pumping with the kick, and a talkbox singing the chorus.
    prog = [([54, 57, 61], 30), ([50, 54, 57], 26), ([49, 52, 57], 33), ([52, 56, 59], 28)]
    kick = s.synth("Kick", "Floor kick", macros=(0.55, 0.45, 0.6, 0.4, 0.5))
    snare = s.synth("Gated snare", "Break snare", seconds=0.5, macros=(0.5, 0.7, 0.8, 0.4, 0.5))
    hat = s.synth("Hat", "Trap hat", seconds=0.1, macros=(0.5, 0.45, 0.5, 0.35, 0.5))
    tom = s.synth("Tom", "Drum machine tom")
    bass = s.synth("Synth bass", "Synth bass", seconds=0.4)
    arp = s.synth("Arp", "Arp pluck", seconds=0.5)
    lead = s.synth("Retro lead", "Retro lead", seconds=1.5)
    kit(s, (kick, snare, hat, tom, bass, arp, lead))
    pad = s.synth("Analog pad", "Analog pad", seconds=3)
    riser = s.synth("Riser", "Riser", seconds=4)
    impact = s.synth("Impact", "Impact")
    l1 = [(61, 0, 0.5), (61, 0.5, 0.5), (64, 1, 1), (61, 2, 1.5), (59, 4, 0.5), (61, 4.5, 1), (59, 5.5, 0.5),
          (57, 6, 0.5), (54, 6.5, 1.5)]
    l2 = [(61, 8, 0.5), (61, 8.5, 0.5), (64, 9, 1), (66, 10, 1.5), (64, 12, 1), (61, 13, 0.5), (59, 13.5, 0.5),
          (57, 14, 0.5), (54, 14.5, 1.5)]
    vn = [(n, st, ln * 0.95) for n, st, ln in l1 + l2]
    vox = s.vocal("Sung: neon coastline", "Ne-on coast-line, we drive through the night. "
                  "Ne-on coast-line, hold on to the light.", vn, style=VS_TALKBOX, vibrato=0.25,
                  bends=shape(vn, scoop=1.5, slide=True, fall=3.0))

    t_kick = s.track("Kick", 0.85)
    t_drums = s.track("Drums", 0.7, eq=(0, 1, 1), reverb=0.35)
    t_bass = s.track("Bass", 0.7, filt=1, cutoff=1400, drive=0.15, duck=0.7)
    t_arp = s.track("Arp", 0.5, pan=-0.25, delay=0.45, reverb=0.25, duck=0.4)
    t_pad = s.track("Pad", 0.5, pan=0.2, filt=1, cutoff=2500, reverb=0.5, duck=0.6)
    t_lead = s.track("Lead", 0.55, pan=0.1, delay=0.35, reverb=0.35)
    t_vox = s.track("Talkbox", 0.75, delay=0.25, reverb=0.3)
    t_fx = s.track("FX", 0.5, reverb=0.5)
    s.fx.update(div=3, fb=0.45, tone=0.5, dlevel=0.6, ping=1, size=0.8, damp=0.3, rlevel=0.8, sc=t_kick, release=200)
    s.dynamics = (1, -18.0, 2.5, 15.0, 180.0, 3.0, 1, -1.0)

    verse = split_beat(s, t_kick, t_drums, "Verse", 16, (kick, "X.......X..o...."),
                       [(snare, "....X.......X..."), (hat, "x.o.x.o.x.o.x.oo", "...............5")])
    chorus = split_beat(s, t_kick, t_drums, "Chorus", 16, (kick, "X...X...X...X..."),
                        [(snare, "....X.......X..."), (hat, "xoxoxoxoxoxoxoxo", ".5.5.5.5.5.5.5..")])
    fill = split_beat(s, t_kick, t_drums, "Fill", 16, (kick, "X.......X......."),
                      [(snare, "....X..........."), (tom, "........x.x.o.xX")])
    bridge = s.drums(t_drums, "Bridge", 16, [(hat, "o.g.o.g.o.g.o.gg", "..5...5...5...55"), (tom, "..........g...o.", "..........5.....")])
    s.drumclip(bridge, t_drums, s.bar(20), 16)
    for b0, b1, beat in ((4, 11, verse), (11, 12, fill), (12, 19, chorus), (19, 20, fill), (24, 27, chorus), (27, 28, fill)):
        place(s, beat, s.bar(b0), s.bar(b1) - s.bar(b0))
    for b in range(28):
        chord, root = prog[b % 4]
        s.noteclip(pad, t_pad, s.bar(b), 4, [(n, 0, 4, 0.6) for n in chord] + [(root + 24, 0, 4, 0.4)])
        tones = chord + [chord[0] + 12]
        order = [0, 1, 2, 3, 2, 1] * 3
        s.noteclip(arp, t_arp, s.bar(b), 4, [(tones[order[i]] + 12, i * 0.25, 0.22, 0.85 if i % 4 == 0 else 0.5)
                                             for i in range(16)])
        if b >= 4 and not 20 <= b < 22:
            s.noteclip(bass, t_bass, s.bar(b), 4, [(root + (12 if i % 4 == 3 else 0), i * 0.5, 0.4, 0.9 if i % 2 == 0 else 0.6)
                                                   for i in range(8)])
    s.auto(t_pad, AUTO_CUTOFF, 0, lufs_cutoff(500))
    s.auto(t_pad, AUTO_CUTOFF, s.bar(4), lufs_cutoff(2500))
    melody = [(73, 0, 1.5), (71, 1.5, 0.5), (69, 2, 1), (66, 3, 1), (69, 4, 2), (73, 6, 2), (74, 8, 1.5), (73, 9.5, 0.5),
              (71, 10, 2), (69, 12, 1), (68, 13, 1), (66, 14, 2)]
    for start in (12, 16, 24):
        s.noteclip(lead, t_lead, s.bar(start), 16, [(n, st, ln, 0.6 + 0.3 * (ln >= 1.5)) for n, st, ln in melody])
    for start in (12, 24):
        s.clip(vox, t_vox, s.bar(start + 4))
    s.clip(riser, t_fx, s.bar(10), beats=8, fade_in=4)
    s.clip(riser, t_fx, s.bar(22), beats=8, fade_in=4)
    for b in (12, 24, 28):
        s.clip(impact, t_fx, s.bar(b))
    s.pads = [(kick, 0, 0.9, t_kick), (snare, 0, 0.9, t_drums), (hat, 0, 0.7, t_drums), (tom, 0, 0.8, t_drums),
              (arp, 0, 0.7, t_arp), (lead, 1, 0.8, t_lead), (vox, 0, 0.8, t_vox), (impact, 0, 0.9, t_fx)]
    s.instrument = (lead, 69, t_lead)
    s.loop = (1, s.frame(s.bar(12)), s.frame(s.bar(20)))
    s.section(0, 4, "Intro")
    s.section(4, 8, "Verse")
    s.section(12, 8, "Chorus")
    s.section(20, 4, "Bridge")
    s.section(24, 4, "Chorus")
    s.key("F# Minor")
    for b in range(28):
        s.harmony(b, ["F#m", "D", "A", "E"][b % 4])
    return s


# ---------------------------------------------------------------- 8. techno

def techno():
    s = Song2("Techno: Concrete Hours", 130)
    # G minor. Four-on-the-floor, a rumbling bass, a stab every three sixteenths against the bar, an
    # acid line whose filter opens over the peak, a robot voice, and the tempo pushed to 133 for the peak.
    s.change(24, 133, 4, 4)
    kick = s.synth("Industrial kick", "Industrial kick")
    clap = s.synth("Clap", "Clap", macros=(0.45, 0.5, 0.6, 0.6, 0.5))
    hat = s.synth("Hat", "Trap hat", seconds=0.1, macros=(0.4, 0.4, 0.5, 0.6, 0.5))
    ohat = s.synth("Open hat", "Open hat", seconds=0.3, macros=(0.45, 0.5, 0.5, 0.5, 0.5))
    rim = s.synth("Rim", "Rimshot")
    rumble = s.synth("Rumble", "Rumble", seconds=0.5)
    stab = s.synth("Hypnotic stab", "Hypnotic stab", seconds=0.6)
    acid = s.synth("Acid line", "Acid line", seconds=0.4)
    kit(s, (kick, clap, hat, ohat, rim, rumble, stab, acid))
    laser = s.synth("Laser sweep", "Laser sweep", seconds=2)
    impact = s.synth("Impact", "Impact")
    rv = [(55, 0, 0.5), (55, 0.5, 0.5), (58, 1, 1.5), (55, 3, 0.75)]
    robot = s.vocal("Sung: keep moving", "Keep mov-ing on", rv, style=VS_ROBOT, vibrato=0.0,
                    bends=[(0.0, 0.0), (0.4, 0.0), (0.5, -5.0), (0.95, 0.0), (1.0, 0.0), (2.0, 0.0), (2.4, 3.0),
                           (3.0, 0.0), (3.4, 0.0), (3.7, -12.0)])

    t_kick = s.track("Kick", 0.9, drive=0.3)
    t_drums = s.track("Drums", 0.65, eq=(0, -1, 2), reverb=0.15)
    t_bass = s.track("Rumble", 0.7, filt=1, cutoff=300, reso=0.2, drive=0.4, duck=0.8, reverb=0.2)
    t_stab = s.track("Stab", 0.5, pan=0.2, filt=2, cutoff=400, delay=0.45, reverb=0.4, duck=0.4)
    t_acid = s.track("Acid", 0.5, pan=-0.15, filt=1, cutoff=600, reso=0.75, drive=0.5, delay=0.25, duck=0.5)
    t_vox = s.track("Robot", 0.6, delay=0.5, reverb=0.4)
    t_fx = s.track("FX", 0.5, reverb=0.6)
    s.fx.update(div=5, fb=0.5, tone=0.4, dlevel=0.6, ping=1, size=0.85, damp=0.3, rlevel=0.75, sc=t_kick, release=140)
    s.dynamics = (1, -22.0, 4.0, 6.0, 110.0, 7.0, 1, -1.0)

    floor = s.drums(t_kick, "Four to the floor", 16, [(kick, "X...X...X...X...")])
    intro = s.drums(t_drums, "Intro hats", 16, [(hat, "..x...x...x...x.", ""), (rim, "x..x..x..x..x..x", "5..4..6..4..5..3")])
    groove = s.drums(t_drums, "Groove", 32, [(clap, "....X.......X.......X.......X.g.", "..............................5."),
                                             (hat, "g.X.g.x.g.X.g.x.g.X.g.x.g.X.g.xg", "5...5...5...5...5...5...5...5..5"),
                                             (rim, "x..x..x..x..x..x..x..x..x..x..x.", "5..4..6..4..5..3..6..4..5..4..7.")])
    peak = s.drums(t_drums, "Peak", 32, [(clap, "....X.......X.......X.......X.oX"),
                                         (hat, "gxXxgxxxgxXxgxxxgxXxgxxxgxXxgxXX", "5.......5.......5.......5......."),
                                         (ohat, "..X...x...X...x...X...x...X...X."),
                                         (rim, "x..x..x..x..x..x..x..x..x..x..x.", "7..5..7..5..7..5..7..5..7..5..9.")])
    breakd = s.drums(t_drums, "Breakdown", 16, [(hat, "o.g.o.g.o.g.o.g.", "..4...4...4...4."), (rim, "x.....x.....x...", "6.....6.....6...")])
    s.drumclip(floor, t_kick, 0, s.bar(16))
    s.drumclip(floor, t_kick, s.bar(24), s.bar(32) - s.bar(24))
    s.drumclip(intro, t_drums, 0, s.bar(8))
    s.drumclip(groove, t_drums, s.bar(8), s.bar(16) - s.bar(8))
    s.drumclip(breakd, t_drums, s.bar(16), s.bar(24) - s.bar(16))
    s.drumclip(peak, t_drums, s.bar(24), s.bar(32) - s.bar(24))
    for b in list(range(8, 16)) + list(range(24, 32)):     # the rumble: the three sixteenths after each kick
        s.noteclip(rumble, t_bass, s.bar(b), 4, [(31, beat + k * 0.25, 0.22, v) for beat in range(4)
                                                 for k, v in ((1, 0.95), (2, 0.6), (3, 0.75))])
    stabs = [(n, i * 0.75, 0.3, 0.9 if i % 4 == 0 else 0.55) for i in range(int(16 / 0.75)) for n in (55, 58, 62)]
    for b in range(8, 32, 4):
        s.noteclip(stab, t_stab, s.bar(b), s.bar(b + 4) - s.bar(b), [x for x in stabs if x[1] < 16])
    s.auto(t_stab, AUTO_CUTOFF, s.bar(8), lufs_cutoff(300))
    s.auto(t_stab, AUTO_CUTOFF, s.bar(16), lufs_cutoff(1500))
    acid_seq = [43, 43, 55, 43, 46, 43, 53, 43, 43, 50, 43, 55, 46, 43, 58, 41]
    for b in range(16, 32, 2):
        s.noteclip(acid, t_acid, s.bar(b), s.bar(b + 2) - s.bar(b),
                   [(acid_seq[i % 16], i * 0.25, 0.22, 1.0 if i % 16 in (2, 6, 11, 14) else 0.5) for i in range(32)])
    s.auto(t_acid, AUTO_CUTOFF, s.bar(16), lufs_cutoff(250))
    s.auto(t_acid, AUTO_CUTOFF, s.bar(23), lufs_cutoff(1200))
    s.auto(t_acid, AUTO_CUTOFF, s.bar(24), lufs_cutoff(600))
    s.auto(t_acid, AUTO_CUTOFF, s.bar(30), lufs_cutoff(5000))
    s.auto(t_acid, AUTO_CUTOFF, s.bar(32), lufs_cutoff(800))
    for b in (12, 20, 26, 30):
        s.clip(robot, t_vox, s.bar(b) + 2)
    s.clip(laser, t_fx, s.bar(22), beats=8, fade_in=4)
    s.clip(impact, t_fx, s.bar(24))
    s.clip(impact, t_fx, s.bar(8), gain=-6)
    s.pads = [(kick, 0, 0.9, t_kick), (clap, 0, 0.8, t_drums), (hat, 0, 0.7, t_drums), (ohat, 0, 0.7, t_drums),
              (rim, 0, 0.8, t_drums), (rumble, 0, 0.9, t_bass), (stab, 0, 0.8, t_stab), (acid, 0, 0.8, t_acid),
              (robot, 0, 0.8, t_vox), (laser, 0, 0.8, t_fx), (impact, 0, 0.9, t_fx)]
    s.instrument = (acid, 43, t_acid)
    s.loop = (1, s.frame(s.bar(24)), s.frame(s.bar(28)))
    s.section(0, 8, "Intro")
    s.section(8, 8, "Groove")
    s.section(16, 8, "Breakdown")
    s.section(24, 8, "Peak")
    s.key("G Minor")
    for b in (8, 16, 24):
        s.harmony(b, "Gm", 8)
    return s


# ---------------------------------------------------------------- 9. pop

def pop():
    s = Song2("Pop: Paper Planes", 118)
    # C major: C - G - Am - F. Piano verses, a supersaw chorus, Aria singing the verses and Lumi the
    # chorus with Kai a third below, each line shaped with scoops, slides and fall-offs.
    prog = [([60, 64, 67], 36), ([59, 62, 67], 31), ([60, 64, 69], 33), ([60, 65, 69], 29)]
    kick = s.synth("Kick", "Floor kick", macros=(0.5, 0.5, 0.5, 0.35, 0.5))
    snap = s.synth("Snap", "Snap")
    clap = s.synth("Clap", "Clap")
    hat = s.synth("Hat", "Trap hat", seconds=0.1, macros=(0.55, 0.45, 0.5, 0.3, 0.5))
    piano = s.synth("Bright piano", "Bright piano", seconds=2)
    pluck = s.synth("Pluck", "Pop pluck", seconds=0.6)
    saw = s.synth("Supersaw", "Supersaw", seconds=2)
    bass = s.synth("Bass", "Round bass", seconds=0.8)
    kit(s, (kick, snap, clap, hat, piano, pluck, saw, bass))
    riser = s.synth("Riser", "Riser", seconds=4)
    impact = s.synth("Impact", "Impact")

    r8 = [(0, 0.5), (0.5, 0.5), (1, 0.5), (1.5, 0.5), (2, 0.5), (2.5, 0.5), (3, 1), (4, 1.5)]
    r7 = [(0, 0.5), (0.5, 0.5), (1, 1), (2, 0.5), (2.5, 0.5), (3, 1), (4, 2.5)]
    vlines = [([64, 64, 67, 67, 69, 67, 64, 62], r8), ([60, 62, 64, 64, 62, 60, 62], r7),
              ([64, 64, 67, 67, 69, 72, 71, 67], r8), ([69, 67, 64, 64, 62, 62, 60], r7)]
    vnotes = []
    for i, (ps, rh) in enumerate(vlines):
        vnotes += [(p, i * 8 + st, ln * 0.95) for p, (st, ln) in zip(ps, rh)]
    verse = s.vocal("Sung: verse", "Fold-ing up the morn-ing pa-per, write your name a-cross the wing. "
                    "Throw it from the win-dow wait-ing, watch it catch the wind and sing.", vnotes, singer=AIRA,
                    high=1, vibrato=0.4, bends=shape(vnotes, scoop=1.0, slide=True, fall=2.0))
    c7 = [(0, 0.5), (0.5, 0.5), (1, 1.5), (3, 0.5), (3.5, 0.5), (4, 0.5), (4.5, 3)]
    ca, cb = [72, 71, 72, 67, 69, 71, 72], [72, 71, 72, 76, 74, 72, 72]
    cnotes = []
    for i, ps in enumerate((ca, cb, ca, cb)):
        cnotes += [(p, i * 8 + st, ln * 0.95) for p, (st, ln) in zip(ps, c7)]
    C_MAJOR = [48, 50, 52, 53, 55, 57, 59, 60, 62, 64, 65, 67, 69, 71, 72, 74, 76, 77, 79]
    below = lambda n: C_MAJOR[C_MAJOR.index(n) - 2] - 12 if n in C_MAJOR else n - 15
    hnotes = [(below(n), st, ln) for n, st, ln in cnotes]
    words = "Pa-per planes, we're fly-ing high, pa-per planes a-cross the sky. " * 2
    chorus = s.vocal("Sung: chorus", words, cnotes, singer=LUMI, high=1, vibrato=0.45,
                     bends=shape(cnotes, scoop=1.2, slide=True, fall=0))
    harm = s.vocal("Sung: chorus low", words, hnotes, singer=KAI, vibrato=0.35, bends=shape(hnotes, scoop=0, slide=True, fall=0))

    t_kick = s.track("Kick", 0.85)
    t_drums = s.track("Drums", 0.65, eq=(0, 0, 1), reverb=0.2)
    t_bass = s.track("Bass", 0.7, filt=1, cutoff=1200, duck=0.5)
    t_piano = s.track("Piano", 0.6, pan=-0.15, eq=(-2, 0, 1), reverb=0.3)
    t_pluck = s.track("Pluck", 0.45, pan=0.3, delay=0.4, reverb=0.25, duck=0.3)
    t_saw = s.track("Supersaw", 0.45, filt=1, cutoff=6000, reverb=0.4, duck=0.6)
    t_vox = s.track("Vocal", 0.85, delay=0.15, reverb=0.3)
    t_harm = s.track("Harmony", 0.5, pan=0.2, reverb=0.4)
    t_fx = s.track("FX", 0.5, reverb=0.5)
    s.fx.update(div=3, fb=0.35, tone=0.6, dlevel=0.55, ping=1, size=0.7, damp=0.4, rlevel=0.75, sc=t_kick, release=170)
    s.dynamics = (1, -18.0, 2.5, 15.0, 180.0, 3.0, 1, -1.0)

    vbeat = split_beat(s, t_kick, t_drums, "Verse", 16, (kick, "X.......X.o....."),
                       [(snap, "....X.......X..."), (hat, "..x...x...x...xg", "...............5")])
    cbeat = split_beat(s, t_kick, t_drums, "Chorus", 32, (kick, "X...X...X...X...X...X...X...X.x."),
                       [(clap, "....X.......X.......X.......X..."), (snap, "....X.......X.......X.......X.o.", "..............................6."),
                        (hat, "x.x.x.x.x.x.x.xox.x.x.x.x.x.xoxo", "..............5...............5.")])
    for b0, b1, beat in ((4, 12, vbeat), (12, 20, cbeat), (24, 32, cbeat)):
        place(s, beat, s.bar(b0), s.bar(b1) - s.bar(b0))
    s.drumclip(s.drums(t_drums, "Break claps", 16, [(clap, "....x.......x...", "....7.......7...")]), t_drums, s.bar(20), s.bar(24) - s.bar(20))
    for b in range(32):
        chord, root = prog[b % 4]
        chor = 12 <= b < 20 or b >= 24
        comp = [(0, 1.5, 0.85), (1.5, 0.5, 0.45), (2, 1.5, 0.7), (3.5, 0.5, 0.4)] if not chor else \
               [(i * 0.5, 0.45, 0.75 if i % 2 == 0 else 0.45) for i in range(8)]
        s.noteclip(piano, t_piano, s.bar(b), 4, [(n, at, ln, v - 0.05 * k) for at, ln, v in comp
                                                 for k, n in enumerate(chord)])
        if b >= 4 and not 20 <= b < 24:
            pat = [(root, 0, 1.5, 0.9), (root, 1.5, 0.5, 0.55), (root + 12, 2, 1, 0.75), (root + 7, 3, 1, 0.6)]
            s.noteclip(bass, t_bass, s.bar(b), 4, pat)
        if chor:
            s.noteclip(saw, t_saw, s.bar(b), 4, [(n + 12, 0, 3.9, 0.7) for n in chord])
            s.noteclip(pluck, t_pluck, s.bar(b), 4, [((chord + [chord[0] + 12])[i % 4] + 12, i * 0.5, 0.4, 0.8 if i % 2 == 0 else 0.5)
                                                     for i in range(8)])
    s.clip(verse, t_vox, s.bar(4))
    for b in (12, 24):
        s.clip(chorus, t_vox, s.bar(b))
        s.clip(harm, t_harm, s.bar(b))
    s.clip(chorus, t_vox, s.bar(20), beats=16, gain=-2, fade_out=2)   # the chorus alone, over the break
    s.clip(riser, t_fx, s.bar(10), beats=8, fade_in=4)
    s.clip(riser, t_fx, s.bar(22), beats=8, fade_in=4)
    for b in (12, 24):
        s.clip(impact, t_fx, s.bar(b))
    s.pads = [(kick, 0, 0.9, t_kick), (snap, 0, 0.9, t_drums), (clap, 0, 0.8, t_drums), (hat, 0, 0.7, t_drums),
              (piano, 1, 0.8, t_piano), (pluck, 0, 0.7, t_pluck), (saw, 1, 0.7, t_saw), (bass, 0, 0.9, t_bass),
              (verse, 0, 0.8, t_vox), (chorus, 0, 0.8, t_vox), (harm, 0, 0.7, t_harm), (riser, 0, 0.8, t_fx)]
    s.instrument = (piano, 60, t_piano)
    s.loop = (1, s.frame(s.bar(12)), s.frame(s.bar(20)))
    s.section(0, 4, "Intro")
    s.section(4, 8, "Verse")
    s.section(12, 8, "Chorus")
    s.section(20, 4, "Break")
    s.section(24, 8, "Chorus")
    s.key("C Major")
    for b in range(32):
        s.harmony(b, ["C", "G", "Am", "F"][b % 4])
    return s


# ---------------------------------------------------------------- 10. chiptune

def chiptune():
    s = Song2("Chiptune: Pixel Quest", 150)
    # C major: C - Am - F - G. A title screen, two levels (the second a whole step up), a boss fight
    # in 3/4 at 170, and a victory jingle back at 150. Square-wave lead and fast arpeggios, a pulse
    # bass jumping octaves, noise drums.
    s.change(20, 170, 3, 4)
    s.change(36, 150, 4, 4)
    lead = s.synth("Square lead", "Square lead", seconds=1)
    arp = s.synth("Chip arp", "Square lead", seconds=0.3, volume=0.6, macros=(0.7, 0.2, 0.3, 0.4, 0.5))
    bass = s.synth("Pulse bass", "Pulse bass", seconds=0.4)
    kick = s.synth("Kick", "Lo-fi kick", macros=(0.6, 0.3, 0.4, 0.6, 0.5))
    noise = s.synth("Noise drum", "Noise drum")
    kit(s, (lead, arp, bass, kick, noise))
    hat = s.variant2("Noise hat", noise, 12)
    coin = s.synth("Coin", "Coin")
    zap = s.synth("Laser zap", "Laser zap")
    sv = [(72, 0, 0.5), (76, 0.5, 1.5), (79, 2.5, 0.5), (84, 3, 0.5), (79, 3.5, 2)]
    voice = s.vocal("Sung: press start", "Press start! Lev-el up!", sv, style=VS_ROBOT, high=1, vibrato=0.0,
                    bends=[(0.0, -12.0), (0.3, 0.0), (2.0, 0.0), (2.4, 12.0), (2.5, 0.0), (5.0, 0.0), (5.5, 12.0)])

    t_lead = s.track("Lead", 0.6, pan=0.1, delay=0.2)
    t_arp = s.track("Arp", 0.45, pan=-0.3)
    t_bass = s.track("Bass", 0.65)
    t_drums = s.track("Drums", 0.7, eq=(0, 0, 1))
    t_fx = s.track("FX", 0.5, reverb=0.3)
    t_vox = s.track("Voice", 0.6, delay=0.3, reverb=0.2)
    s.fx.update(div=4, fb=0.3, tone=0.6, dlevel=0.5, ping=1, size=0.4, damp=0.5, rlevel=0.5)
    s.dynamics = (1, -16.0, 2.0, 10.0, 150.0, 3.0, 1, -1.0)

    level = s.drums(t_drums, "Level", 16, [(kick, "X.......X.x....."), (noise, "....X.......X..o", "...............6"),
                                           (hat, "x.x.x.x.x.x.x.xx", "..............5.")])
    level2 = s.drums(t_drums, "Level 2", 16, [(kick, "X.....x.X.x....."), (noise, "....X..g....X.oX", ".......5........"),
                                              (hat, "xgxgxgxgxgxgxgxg", ".5.5.5.5.5.5.5.5")])
    boss = s.drums(t_drums, "Boss", 16, [(kick, "X.....X.x..."), (noise, "...X.....X.o"), (hat, "xxxxxxxxxxxx")])
    win = s.drums(t_drums, "Win", 16, [(kick, "X...X...X...X..."), (noise, "....X.......XoXX"), (hat, "x.x.x.x.x.x.x.x.")])
    s.drumclip(level, t_drums, s.bar(4), s.bar(12) - s.bar(4))
    s.drumclip(level2, t_drums, s.bar(12), s.bar(20) - s.bar(12))
    for b in range(20, 36):                         # 3/4: the pattern is cut at each bar's end
        s.drumclip(boss, t_drums, s.bar(b), s.bar(b + 1) - s.bar(b))
    s.drumclip(win, t_drums, s.bar(36), s.bar(42) - s.bar(36))

    prog = [([60, 64, 67], 36), ([57, 60, 64], 33), ([53, 57, 60], 29), ([55, 59, 62], 31)]
    for b in range(42):
        boss_bar = 20 <= b < 36
        up = 2 if 12 <= b < 20 else 0
        chord, root = prog[(b // 2) % 4] if boss_bar else prog[b % 4]
        if boss_bar:
            chord, root = [n - 3 for n in chord], root - 3   # the boss, a minor third down: darker
        length = s.bar(b + 1) - s.bar(b)
        steps = int(round(length * 4))
        tones = [n + up + 12 for n in chord] + [chord[0] + up + 24]
        s.noteclip(arp, t_arp, s.bar(b), length, [(tones[i % 4], i * 0.25, 0.2, 0.9 if i % 4 == 0 else 0.5)
                                                  for i in range(steps)])
        if b >= 4:
            s.noteclip(bass, t_bass, s.bar(b), length, [(root + up + (12 if i % 2 else 0), i * 0.5, 0.4, 0.9 if i % 2 == 0 else 0.6)
                                                        for i in range(int(length * 2))])
    tune = [(72, 0, 0.5), (74, 0.5, 0.5), (76, 1, 1), (79, 2, 1), (76, 3, 0.5), (74, 3.5, 0.5), (72, 4, 1.5),
            (69, 6, 0.5), (72, 6.5, 0.5), (74, 7, 1), (72, 8, 0.5), (74, 8.5, 0.5), (76, 9, 1), (79, 10, 1),
            (81, 11, 1), (79, 12, 1.5), (76, 13.5, 0.5), (74, 14, 2)]
    for start in (4, 8):
        s.noteclip(lead, t_lead, s.bar(start), 16, [(n, st, ln, 0.6 + 0.3 * (st % 2 == 0)) for n, st, ln in tune])
    for start in (12, 16):
        s.noteclip(lead, t_lead, s.bar(start), 16, [(n + 2, st, ln, 0.7 + 0.25 * (st % 2 == 0)) for n, st, ln in tune])
    boss_tune = [(69, 0, 0.5), (72, 0.5, 0.5), (76, 1, 0.5), (75, 1.5, 0.5), (76, 2, 1), (69, 3, 0.5), (72, 3.5, 0.5),
                 (77, 4, 0.5), (76, 4.5, 0.5), (74, 5, 1)]
    for b in range(20, 36, 2):
        s.noteclip(lead, t_lead, s.bar(b), s.bar(b + 2) - s.bar(b), [(n - 3, st, ln, 0.95 if st % 3 == 0 else 0.6)
                                                                    for n, st, ln in boss_tune])
    win_tune = [(72, 0, 0.5), (76, 0.5, 0.5), (79, 1, 0.5), (84, 1.5, 1.5), (83, 3, 0.5), (84, 3.5, 4.5)]
    s.noteclip(lead, t_lead, s.bar(36), 8, [(n, st, ln, 0.9) for n, st, ln in win_tune])
    s.noteclip(lead, t_lead, s.bar(38), 16, [(n, st, ln, 0.7) for n, st, ln in tune])
    s.clip(voice, t_vox, s.bar(1))
    s.clip(voice, t_vox, s.bar(36) + 2)
    for b in (4, 12, 20, 36):
        s.clip(coin, t_fx, s.bar(b))
    for b in range(22, 36, 4):
        s.clip(zap, t_fx, s.bar(b) + 2, gain=-4)
    s.pads = [(kick, 0, 0.9, t_drums), (noise, 0, 0.9, t_drums), (hat, 0, 0.7, t_drums), (lead, 1, 0.8, t_lead),
              (arp, 0, 0.7, t_arp), (bass, 0, 0.9, t_bass), (coin, 0, 0.8, t_fx), (zap, 0, 0.8, t_fx),
              (voice, 0, 0.8, t_vox)]
    s.instrument = (lead, 72, t_lead)
    s.loop = (1, s.frame(s.bar(20)), s.frame(s.bar(28)))
    s.section(0, 4, "Title")
    s.section(4, 8, "Level 1")
    s.section(12, 8, "Level 2")
    s.section(20, 16, "Boss (3/4)")
    s.section(36, 6, "Victory")
    s.key("C Major")                            # the second level a step up, the boss in A, home for the win
    s.key("D Major", 12)
    s.key("A Major", 20)
    s.key("C Major", 36)
    for b in list(range(12)) + list(range(36, 42)):
        s.harmony(b, ["C", "Am", "F", "G"][b % 4])
    for b in range(12, 20):
        s.harmony(b, ["D", "Bm", "G", "A"][b % 4])
    for b in range(20, 36, 2):
        s.harmony(b, ["D", "E", "A", "F#m"][(b // 2) % 4], 2)
    return s


# ---------------------------------------------------------------- the songs made with the chord track
# These lay down the sounds, the mix, the drums, the chords and key, the sections and any sung lines,
# then ask tinydaw (--part, --fills) to write the bass, chord and melody parts and the drum fills from
# the chord track, the way Create > Parts does. Pitch curves (s.bend) bend clips the way Mixer > Clip does.

def beats_every(s, pair_or_pid, track, b0, b1, every=1):
    """A drum pattern (or a kick/drums pair from split_beat) as clips `every` bars long over bars [b0, b1)."""
    for b in range(b0, b1, every):
        e = min(b + every, b1)
        if isinstance(pair_or_pid, tuple):
            place(s, pair_or_pid, s.bar(b), s.bar(e) - s.bar(b))
        else:
            s.drumclip(pair_or_pid, track, s.bar(b), s.bar(e) - s.bar(b))


def harmony_loop(s, b0, b1, names, bars=1):
    """Chords on the chord track, `names` round and round, each `bars` long, over bars [b0, b1)."""
    for i, b in enumerate(range(b0, b1, bars)):
        s.harmony(b, names[i % len(names)], min(bars, b1 - b))


# ---------------------------------------------------------------- 2. hip-hop

def hiphop():
    s = Song2("Hip-hop: Rooftop Static", 90)
    # C minor: Cm9 - Abmaj7 - Fm9 - G7, a bar each. Swung boom-bap, soul-key stabs and an upright
    # groove written from the chords, a flute answering in the hook, Kai on the hook, and the beat
    # slowing to a stop at the end (a tape-style bend on the drums).
    kick = s.synth("Kick", "Boom bap kick", macros=(0.5, 0.45, 0.6, 0.45, 0.5))
    snare = s.synth("Snare", "Dusty snare", seconds=0.4)
    hat = s.synth("Hat", "Trap hat", seconds=0.12, macros=(0.4, 0.5, 0.45, 0.35, 0.5))
    rim = s.synth("Rim", "Rimshot")
    kit(s, (kick, snare, hat, rim))
    crackle = s.synth("Vinyl", "Vinyl crackle", seconds=8)
    hook_notes = [(67, 0, 0.75), (70, 1, 0.5), (72, 1.5, 1.5), (70, 4, 0.75), (67, 5, 0.5), (65, 5.5, 1),
                  (63, 6.5, 1.25)]
    hook = s.vocal("Sung: rooftop", "Up on the roof, the sta-tic hums", hook_notes, singer=KAI, vibrato=0.3,
                   bends=shape(hook_notes, scoop=1.2, fall=2.5))

    t_drums = s.track("Drums", 0.8, eq=(1, 0, -1), reverb=0.12)
    t_keys = s.track("Keys", 0.6, filt=1, cutoff=3000, reso=0.1, drive=0.15, reverb=0.3)
    t_bass = s.track("Bass", 0.75, eq=(2, 0, -2))
    t_flute = s.track("Flute", 0.45, pan=0.3, delay=0.3, reverb=0.4)
    t_vox = s.track("Vocal", 0.85, delay=0.15, reverb=0.3)
    t_vinyl = s.track("Vinyl", 0.3, filt=2, cutoff=600)
    s.fx.update(div=4, fb=0.3, tone=0.4, dlevel=0.5, ping=1, size=0.55, damp=0.5, rlevel=0.6)
    s.dynamics = (1, -18.0, 2.5, 15.0, 180.0, 3.0, 1, -1.0)

    groove = s.drums(t_drums, "Boom bap", 16, [(kick, "X.....x...X..x..", ".........5....5."),
                                                (snare, "....X..g....X..g", ".......4.......5"),
                                                (hat, "x.x.x.x.x.x.x.xo", "..........5....."),
                                                (rim, "..........g.....", "..........4.....")], swing=0.12)
    intro = s.drums(t_drums, "Intro", 16, [(hat, "x.x.x.x.x.x.x.x."), (rim, "....g.......g...")], swing=0.12)
    beats_every(s, intro, t_drums, 0, 4)
    beats_every(s, groove, t_drums, 4, 27)
    s.drumclip(groove, t_drums, s.bar(27), 4)          # the last bar: the beat winds down like a tape stopping
    s.bend([(0, 0), (2, -3), (4, -24)], keep=0)
    for b in range(0, 28, 4):
        s.clip(crackle, t_vinyl, s.bar(b), beats=16)
    harmony_loop(s, 0, 28, ["Cm9", "Abmaj7", "Fm9", "G7"])
    s.key("C Minor")
    for b in (12, 20):
        s.clip(hook, t_vox, s.bar(b))
        s.clip(hook, t_vox, s.bar(b + 4))
    s.section(0, 4, "Intro")
    s.section(4, 8, "Verse")
    s.section(12, 8, "Hook")
    s.section(20, 4, "Verse")
    s.section(24, 4, "Outro")
    s.part("chords,style=Stabs,busy=0.35,seed=2,sound=Soul keys,track=Keys")
    s.part("bass,style=Groove,busy=0.6,seed=4,sound=Upright bass,bars=5-28,track=Bass")
    s.part("melody,style=Sparse,busy=0.45,seed=7,sound=Flute lead,bars=13-21,track=Flute,bend=scoops")
    s.fills("style=Drop-out,len=0.3,where=sections,track=Drums,seed=3")
    s.pads = [(kick, 0, 0.9, t_drums), (snare, 0, 0.9, t_drums), (hat, 0, 0.7, t_drums), (rim, 0, 0.8, t_drums),
              (hook, 0, 0.8, t_vox)]
    s.loop = (1, s.frame(s.bar(12)), s.frame(s.bar(20)))
    return s


# ---------------------------------------------------------------- 5. rock

def rock():
    s = Song2("Rock: Paper Lanterns", 140)
    # D major: D - A - Bm - G in the verse, G - D - A - Bm in the chorus, and the last chorus a step up
    # in E (a key change). Doubled crunch guitars, a picked bass written from the chords, tom fills
    # before each section, and Aria singing the chorus.
    guitar_patch = dict(wave=0, wave2=1, mix2=0.55, voices=2, detune=9, filter=1, cutoff=2600, reso=0.2,
                        fenv=0.6, fdecay=0.25, attack=0.003, decay=0.35, sustain=0.75, release=0.12, drive=0.95)
    lead_patch = dict(guitar_patch, voices=1, vibrato=22, lfo_hz=5.5, cutoff=3400, mix2=0.4, sustain=0.85)
    kick = s.synth("Rock kick", "Boom bap kick", macros=(0.6, 0.4, 0.6, 0.45, 0.5))
    snare = s.synth("Rock snare", "Break snare", seconds=0.35, macros=(0.55, 0.6, 0.6, 0.5, 0.5))
    hat = s.synth("Closed hat", "Trap hat", seconds=0.15, macros=(0.45, 0.55, 0.5, 0.4, 0.5))
    tom = s.synth("Tom", "Drum machine tom", macros=(0.45, 0.5, 0.6, 0.4, 0.5))
    crash = s.synth("Crash", "Open hat", seconds=1.6, volume=0.75, macros=(0.6, 0.95, 0.8, 0.6, 0.5))
    gtr = s.synth("Crunch guitar", patch=guitar_patch, note=52, seconds=2.2, volume=0.75)
    lead = s.synth("Lead guitar", patch=lead_patch, note=64, seconds=2.2, volume=0.75)
    kit(s, (kick, snare, hat, tom, gtr, lead))
    cn = [(74, 0, 1), (73, 1, 0.5), (71, 1.5, 1.5), (69, 4, 1), (71, 5, 0.5), (73, 5.5, 2)]
    cn2 = [(n + 2, st, ln) for n, st, ln in cn]
    chorus_v = s.vocal("Sung: lanterns", "Pa-per lan-terns, light the way", cn, singer=AIRA, high=1, vibrato=0.4,
                       bends=shape(cn, scoop=1.0, fall=2.0))
    chorus_up = s.vocal("Sung: lanterns (up)", "Pa-per lan-terns, light the way", cn2, singer=AIRA, high=1,
                        vibrato=0.4, bends=shape(cn2, scoop=1.0, fall=2.0))

    t_drums = s.track("Drums", 0.85, eq=(1, 0, 1), reverb=0.15)
    t_bass = s.track("Bass", 0.7, filt=1, cutoff=1600, drive=0.2)
    t_gl = s.track("Guitar L", 0.5, pan=-0.6, eq=(-2, 1, 0))
    t_gr = s.track("Guitar R", 0.5, pan=0.6, eq=(-2, 1, 0))
    t_lead = s.track("Lead guitar", 0.5, pan=0.1, delay=0.25, reverb=0.3)
    t_vox = s.track("Vocal", 0.85, delay=0.12, reverb=0.3)
    t_cym = s.track("Cymbals", 0.4, pan=-0.2, reverb=0.2)
    s.fx.update(div=4, fb=0.3, tone=0.5, dlevel=0.5, ping=0, size=0.65, damp=0.45, rlevel=0.7)
    s.dynamics = (1, -18.0, 2.5, 15.0, 180.0, 3.0, 1, -1.0)

    verse = s.drums(t_drums, "Verse beat", 16, [(kick, "X.....x.X......."), (snare, "....X.......X..g", "...............5"),
                                                 (hat, "X.x.x.x.X.x.x.x."), (tom, "................")])
    chorus = s.drums(t_drums, "Chorus beat", 16, [(kick, "X.....x.X.x....."), (snare, "....X.......X..."),
                                                   (hat, "X.X.X.X.X.X.X.X."), (tom, "................")])
    beats_every(s, verse, t_drums, 0, 12)
    beats_every(s, chorus, t_drums, 12, 20)
    beats_every(s, verse, t_drums, 20, 24)
    beats_every(s, chorus, t_drums, 24, 32)
    for b in (4, 12, 20, 24, 32):
        s.clip(crash, t_cym, s.bar(b))
    s.key("D Major")
    s.key("E Major", 24)
    harmony_loop(s, 0, 12, ["D", "A", "Bm", "G"])
    harmony_loop(s, 12, 20, ["G", "D", "A", "Bm"])
    harmony_loop(s, 20, 24, ["D", "A", "Bm", "G"])
    harmony_loop(s, 24, 32, ["A", "E", "B", "C#m"])
    s.harmony(32, "E", 1)
    roots = {"D": 38, "A": 45, "Bm": 47, "G": 43, "E": 40, "B": 47, "C#m": 37}
    for b, c in enumerate(["D", "A", "Bm", "G"] * 3 + ["G", "D", "A", "Bm"] * 2 + ["D", "A", "Bm", "G"] +
                          ["A", "E", "B", "C#m"] * 2 + ["E"]):
        r = roots[c] + 12
        palm = b < 12 or 20 <= b < 24
        notes = [(n, i * 0.5, 0.4, 0.85 if i % 2 == 0 else 0.6) for i in range(8) for n in ((r, r + 7) if palm else ())] if palm \
            else [(n, 0, 3.8, 0.85) for n in (r, r + 7, r + 12)]
        if b == 32:
            notes = [(n, 0, 3.8, 0.9) for n in (r, r + 7, r + 12)]
        for tr, shift in ((t_gl, 0), (t_gr, 0.01)):
            s.noteclip(gtr, tr, s.bar(b) + shift, 4, notes)
    for b in (12, 16, 24, 28):
        s.clip(chorus_v if b < 24 else chorus_up, t_vox, s.bar(b))
    s.noteclip(lead, t_lead, s.bar(20), 16, [(74, 0, 2, 0.8), (76, 2, 1, 0.7), (78, 3, 3, 0.85), (76, 6, 1, 0.6),
                                              (74, 7, 1, 0.6), (73, 8, 2, 0.8), (71, 10, 2, 0.7), (69, 12, 4, 0.9)])
    s.bend([(0, -1.5), (0.15, 0), (12, 0), (16, -2)])   # scooped in, sagging off at the end
    s.section(0, 4, "Intro")
    s.section(4, 8, "Verse")
    s.section(12, 8, "Chorus")
    s.section(20, 4, "Break")
    s.section(24, 8, "Chorus (up)")
    s.section(32, 1, "End")
    s.part("bass,style=Pulse,busy=0.7,seed=1,sound=Synth bass,bars=5-34,track=Bass,gain=-2")
    s.fills("style=Toms,len=0.5,where=sections,track=Drums,seed=5")
    s.pads = [(kick, 0, 0.9, t_drums), (snare, 0, 0.9, t_drums), (hat, 0, 0.7, t_drums), (tom, 0, 0.8, t_drums),
              (crash, 0, 0.8, t_cym), (chorus_v, 0, 0.8, t_vox)]
    s.instrument = (lead, 64, t_lead)
    s.loop = (1, s.frame(s.bar(12)), s.frame(s.bar(20)))
    return s


# ---------------------------------------------------------------- 3. experimental

def experimental():
    s = Song2("Experimental: Tidal Clockwork", 108, 5, 4)
    # Clockwork in 5/4, a tide in 7/8, a waltz in 6/8, then home in 4/4. D Dorian, then F# Lydian from the
    # 7/8 on and back. Glass bells arpeggiated up and down from the chords, a drone held under them, a
    # melody flowing over it with vibrato, ticking percussion that bends down as it fades, and wind rising.
    s.change(8, 108, 7, 8)
    s.change(16, 108, 6, 8)
    s.change(24, 108, 4, 4)
    tick = s.synth("Tick", "Rimshot", macros=(0.7, 0.3, 0.3, 0.3, 0.5))
    knock = s.synth("Knock", "Lo-fi kick", macros=(0.4, 0.5, 0.5, 0.3, 0.5))
    hat = s.synth("Shaker", "Trap hat", seconds=0.1, macros=(0.6, 0.6, 0.4, 0.2, 0.5))
    kit(s, (tick, knock, hat))
    wind = s.synth("Wind", "Wind", seconds=10)
    drone = s.synth("Drone", "Drone", seconds=12)
    t_perc = s.track("Clockwork", 0.6, pan=-0.15, reverb=0.3, delay=0.25)
    t_bell = s.track("Bells", 0.5, pan=0.25, delay=0.35, reverb=0.5)
    t_pad = s.track("Pad", 0.45, reverb=0.6)
    t_lead = s.track("Lead", 0.5, pan=0.05, delay=0.3, reverb=0.45)
    t_drone = s.track("Drone", 0.35, filt=1, cutoff=900)
    t_wind = s.track("Wind", 0.35, pan=0.3, reverb=0.5)
    s.fx.update(div=6, fb=0.5, tone=0.4, dlevel=0.6, ping=1, size=0.9, damp=0.25, rlevel=0.85)

    five = s.drums(t_perc, "Clock (5/4)", 32, [(tick, "x.x.x.x.x.x.x.x.x.x.", "..6...6...6...6....."),
                                                (knock, "X.....x...X.....x...", ".....................", 0.9),
                                                (hat, ".x.x.x.x.x.x.x.x.x.x", "5.5.5.5.5.5.5.5.5.5.")])
    seven = s.drums(t_perc, "Tide (7/8)", 16, [(tick, "x.x.xx.x.x.x.x", "..6..5..6..5.."),
                                                (knock, "X.....x.......", ""), (hat, ".xxx.xx.xxx.xx", "5..5..5..5..5.")])
    six = s.drums(t_perc, "Waltz (6/8)", 16, [(tick, "x.....x.....", ""), (knock, "X...........", ""),
                                               (hat, "..x.x...x.x.", "..6.6...6.6.")])
    four = s.drums(t_perc, "Home (4/4)", 16, [(tick, "x.x.x.x.x.x.x.x.", "..6...6...6...6."),
                                               (knock, "X.......X.......", ""), (hat, ".x.x.x.x.x.x.x.x", "5.5.5.5.5.5.5.5.")])
    for b in range(8):
        s.drumclip(five, t_perc, s.bar(b), s.bar(b + 1) - s.bar(b))
    for b in range(8, 16):
        s.drumclip(seven, t_perc, s.bar(b), s.bar(b + 1) - s.bar(b))
    for b in range(16, 24):
        s.drumclip(six, t_perc, s.bar(b), s.bar(b + 1) - s.bar(b))
    for b in range(24, 30):
        s.drumclip(four, t_perc, s.bar(b), s.bar(b + 1) - s.bar(b))
    s.drumclip(four, t_perc, s.bar(30), s.bar(32) - s.bar(30))   # the clock running down
    s.bend([(0, 0), (4, -2), (8, -12)])
    s.key("D Dorian")
    s.key("F# Lydian", 8)
    s.key("D Dorian", 24)
    harmony_loop(s, 0, 8, ["Dm9", "G6", "Cmaj7", "Am7"], 2)
    harmony_loop(s, 8, 16, ["F#maj7", "G#/F#", "D#m7", "C#sus4"], 2)
    harmony_loop(s, 16, 24, ["Emaj7", "F#", "D#m7", "G#m7"], 2)
    harmony_loop(s, 24, 32, ["Dm9", "G6", "Bbmaj7", "Asus4"], 2)
    for b in range(0, 32, 4):
        s.clip(drone, t_drone, s.bar(b), beats=s.bar(b + 4) - s.bar(b), fade_in=1, fade_out=1)
    for b in (6, 14, 22):
        s.clip(wind, t_wind, s.bar(b), beats=s.bar(b + 2) - s.bar(b), fade_in=2)
        s.bend([(0, -5), (s.bar(b + 2) - s.bar(b), 7)], keep=0)
    s.section(0, 8, "Clockwork (5/4)")
    s.section(8, 8, "Tide (7/8)")
    s.section(16, 8, "Waltz (6/8)")
    s.section(24, 8, "Home")
    s.part("chords,style=Arp up & down,busy=0.6,seed=1,sound=Glass bell,track=Bells,octave=1")
    s.part("chords,style=Held,seed=1,sound=Shimmer pad,track=Pad")
    s.part("melody,style=Flowing,busy=0.4,seed=9,sound=Flute lead,bars=9-31,track=Lead,bend=vibrato")
    s.pads = [(tick, 0, 0.8, t_perc), (knock, 0, 0.9, t_perc), (hat, 0, 0.7, t_perc), (wind, 0, 0.6, t_wind)]
    s.loop = (1, s.frame(s.bar(8)), s.frame(s.bar(16)))
    return s


# ---------------------------------------------------------------- 11. lo-fi

def lofi():
    s = Song2("Lo-fi: Window Rain", 76)
    # F major: Fmaj7 - Em7 - Dm9 - Cmaj7, a bar each. Lazy swung drums, held keys with a tape wobble (a
    # vibrato on their pitch curve), a round bass on the roots, a flute that barely speaks, rain at the
    # window and vinyl underneath.
    kick = s.synth("Kick", "Lo-fi kick")
    snare = s.synth("Snare", "Dusty snare", seconds=0.4, macros=(0.35, 0.6, 0.5, 0.5, 0.5))
    hat = s.synth("Hat", "Trap hat", seconds=0.1, macros=(0.3, 0.5, 0.4, 0.4, 0.5))
    kit(s, (kick, snare, hat))
    rain = s.synth("Rain", "Wind", seconds=10, macros=(0.75, 0.6, 0.6, 0.5, 0.5))
    crackle = s.synth("Vinyl", "Vinyl crackle", seconds=8)
    t_drums = s.track("Drums", 0.75, filt=1, cutoff=4500, eq=(1, 0, -3), reverb=0.15)
    t_keys = s.track("Keys", 0.6, filt=1, cutoff=2400, drive=0.1, reverb=0.35)
    t_bass = s.track("Bass", 0.7, eq=(2, -1, -3))
    t_flute = s.track("Flute", 0.4, pan=0.25, delay=0.35, reverb=0.5)
    t_rain = s.track("Rain", 0.3, filt=1, cutoff=3000, pan=-0.2)
    t_vinyl = s.track("Vinyl", 0.25, filt=2, cutoff=700)
    s.fx.update(div=4, fb=0.4, tone=0.3, dlevel=0.5, ping=1, size=0.7, damp=0.6, rlevel=0.7, sc=t_drums, release=220)
    s.dynamics = (1, -18.0, 2.5, 15.0, 180.0, 3.0, 1, -1.0)
    groove = s.drums(t_drums, "Lazy", 16, [(kick, "X......x..X.....", ".......5........"),
                                            (snare, "....X.......X..g", "...............4"),
                                            (hat, "x.xgx.x.x.xgx.xg", "...5.......5...5")], swing=0.18)
    sparse = s.drums(t_drums, "Sparse", 16, [(kick, "X.........X....."), (hat, "x...x...x...x...")], swing=0.18)
    beats_every(s, sparse, t_drums, 0, 4)
    beats_every(s, groove, t_drums, 4, 20)
    beats_every(s, sparse, t_drums, 20, 24)
    for b in range(0, 24, 4):
        s.clip(crackle, t_vinyl, s.bar(b), beats=16)
        s.clip(rain, t_rain, s.bar(b), beats=16, fade_in=1 if b == 0 else 0, fade_out=4 if b == 20 else 0)
    s.key("F Major")
    harmony_loop(s, 0, 24, ["Fmaj7", "Em7", "Dm9", "Cmaj7"])
    s.section(0, 4, "Rain")
    s.section(4, 8, "A")
    s.section(12, 8, "B")
    s.section(20, 4, "Fade")
    s.part("chords,style=Held,seed=1,sound=Lo-fi keys,track=Keys,bend=vibrato")
    s.part("bass,style=Roots,seed=1,sound=Round bass,bars=5-21,track=Bass")
    s.part("melody,style=Sparse,busy=0.35,seed=12,sound=Flute lead,bars=13-21,track=Flute,bend=scoops")
    s.fills("style=Drop-out,len=0.2,where=8,track=Drums,seed=2")
    s.pads = [(kick, 0, 0.9, t_drums), (snare, 0, 0.9, t_drums), (hat, 0, 0.7, t_drums), (rain, 0, 0.6, t_rain)]
    s.loop = (1, s.frame(s.bar(4)), s.frame(s.bar(12)))
    return s


# ---------------------------------------------------------------- 12. drum & bass

def dnb():
    s = Song2("Drum & Bass: Night Bus", 174)
    # F minor: Fm - Db - Bbm - C, two bars each. A filtered intro, then two-step breaks with ghost snares,
    # a rolling sub on the roots with a reese doubling it, a held pad, a bell hook, snare rolls into each
    # section, and the sub diving at the drop's end.
    kick = s.synth("Kick", "Floor kick", macros=(0.6, 0.4, 0.5, 0.45, 0.5))
    snare = s.synth("Snare", "Break snare", seconds=0.35)
    hat = s.synth("Hat", "Trap hat", seconds=0.08)
    kit(s, (kick, snare, hat))
    sub_drop = s.synth("Sub drop", "Sub drop", seconds=3)
    riser = s.synth("Riser", "Riser", seconds=4)
    t_kick = s.track("Kick", 0.85)
    t_drums = s.track("Drums", 0.7, eq=(0, 1, 2), reverb=0.15)
    t_sub = s.track("Sub", 0.75, duck=0.7)
    t_reese = s.track("Reese", 0.45, filt=1, cutoff=1500, reso=0.3, drive=0.4, duck=0.6)
    t_pad = s.track("Pad", 0.45, reverb=0.6, duck=0.5)
    t_bell = s.track("Bells", 0.45, pan=0.2, delay=0.4, reverb=0.45)
    t_fx = s.track("FX", 0.5, reverb=0.5)
    s.fx.update(div=3, fb=0.45, tone=0.45, dlevel=0.5, ping=1, size=0.8, damp=0.35, rlevel=0.75, sc=t_kick, release=120)
    s.dynamics = (1, -22.0, 4.0, 6.0, 110.0, 7.0, 1, -1.0)
    two = split_beat(s, t_kick, t_drums, "Two-step", 32, (kick, "X.........X.......X.....x.X....."),
                     [(snare, "....X..g.g..X.......X..g.g..X..g", "......5.5..............5.5.....4"),
                      (hat, "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", "..5...5...5...5...5...5...5...5.")])
    intro = s.drums(t_drums, "Intro hats", 16, [(hat, "x.x.x.x.x.x.x.x."), (snare, "............X...")])
    beats_every(s, intro, t_drums, 0, 8)
    beats_every(s, two, t_drums, 8, 24)
    beats_every(s, intro, t_drums, 24, 28)
    beats_every(s, two, t_drums, 28, 40)
    s.auto(t_pad, AUTO_CUTOFF, 0, lufs_cutoff(400))
    s.auto(t_pad, AUTO_CUTOFF, s.bar(8), lufs_cutoff(8000))
    s.clip(riser, t_fx, s.bar(6), beats=8, fade_in=4)
    s.clip(riser, t_fx, s.bar(26), beats=8, fade_in=4)
    s.clip(sub_drop, t_fx, s.bar(23))
    s.bend([(0, 0), (6, -12)], keep=0)
    s.key("F Minor")
    harmony_loop(s, 0, 40, ["Fm", "Db", "Bbm", "C"], 2)
    s.section(0, 8, "Intro")
    s.section(8, 16, "Drop")
    s.section(24, 4, "Break")
    s.section(28, 12, "Drop")
    s.part("chords,style=Held,seed=1,sound=Shimmer pad,track=Pad")
    s.part("bass,style=Roots,seed=1,sound=Rolling sub,bars=9-25,track=Sub")
    s.part("bass,style=Roots,seed=1,sound=Rolling sub,bars=29-41,track=Sub")
    s.part("bass,style=Off-beat,busy=0.6,seed=2,sound=Reese,bars=9-25,track=Reese,octave=1")
    s.part("bass,style=Off-beat,busy=0.6,seed=2,sound=Reese,bars=29-41,track=Reese,octave=1")
    s.part("melody,style=Hook,busy=0.5,seed=21,sound=Glass bell,bars=9-41,track=Bells,octave=1")
    s.fills("style=Snare roll,len=0.6,where=sections,track=Drums,seed=4")
    s.pads = [(kick, 0, 0.9, t_kick), (snare, 0, 0.9, t_drums), (hat, 0, 0.7, t_drums), (sub_drop, 0, 0.9, t_fx),
              (riser, 0, 0.8, t_fx)]
    s.loop = (1, s.frame(s.bar(8)), s.frame(s.bar(16)))
    return s


# ---------------------------------------------------------------- 13. jazz

def jazz():
    s = Song2("Jazz: Blue Lamp", 132)
    # Bb major: ii - V - I - vi (Cm7 F7 Bbmaj7 Gm7), then the turnaround, and the last chorus up a minor
    # third in Db. A swung ride, cross-stick and feathered kick, a walking upright bass, comping keys,
    # and a flowing flute line, all written from the changes.
    ride = s.synth("Ride", "Open hat", seconds=0.6, macros=(0.6, 0.6, 0.4, 0.3, 0.5))
    hat = s.synth("Hi-hat foot", "Trap hat", seconds=0.08, macros=(0.3, 0.4, 0.4, 0.3, 0.5))
    stick = s.synth("Cross-stick", "Rimshot")
    kick = s.synth("Kick", "Lo-fi kick", macros=(0.4, 0.5, 0.4, 0.3, 0.5))
    kit(s, (ride, hat, stick, kick))
    t_drums = s.track("Drums", 0.7, reverb=0.25)
    t_bass = s.track("Bass", 0.8, eq=(2, 0, -2))
    t_keys = s.track("Keys", 0.55, pan=-0.2, reverb=0.3)
    t_flute = s.track("Flute", 0.5, pan=0.25, reverb=0.35, delay=0.15)
    s.fx.update(div=4, fb=0.25, tone=0.5, dlevel=0.4, ping=0, size=0.6, damp=0.45, rlevel=0.6)
    swing = s.drums(t_drums, "Swing", 16, [(ride, "x...x.xx....x.xx", "......4.......4."),
                                            (hat, "....x.......x..."),
                                            (stick, "........g.......", "........4......."),
                                            (kick, "g.......g.......", "4.......4.......")], swing=0.33)
    brush = s.drums(t_drums, "Brushes", 16, [(ride, "x...x...x...x..."), (hat, "....x.......x...")], swing=0.33)
    beats_every(s, brush, t_drums, 0, 4)
    beats_every(s, swing, t_drums, 4, 36)
    s.key("Bb Major")
    s.key("Db Major", 28)
    head = ["Cm7", "F7", "Bbmaj7", "Gm7", "Cm7", "F7", "Bbmaj7", "Bbmaj7"]
    harmony_loop(s, 0, 4, ["Bbmaj7", "Gm7", "Cm7", "F7"])
    harmony_loop(s, 4, 28, head)
    harmony_loop(s, 28, 36, ["Ebm7", "Ab7", "Dbmaj7", "Bbm7", "Ebm7", "Ab7", "Dbmaj7", "Dbmaj7"])
    s.section(0, 4, "Intro")
    s.section(4, 8, "Head")
    s.section(12, 8, "Solo")
    s.section(20, 8, "Head")
    s.section(28, 8, "Out (Db)")
    s.part("bass,style=Walking,seed=3,sound=Upright bass,bars=5-37,track=Bass")
    s.part("chords,style=Stabs,busy=0.25,seed=5,sound=Smooth keys,track=Keys")
    s.part("melody,style=Flowing,busy=0.55,seed=31,sound=Flute lead,bars=5-37,track=Flute,bend=scoops")
    s.fills("style=Toms,len=0.3,where=8,track=Drums,seed=6")
    s.pads = [(ride, 0, 0.8, t_drums), (hat, 0, 0.7, t_drums), (stick, 0, 0.8, t_drums), (kick, 0, 0.9, t_drums)]
    s.loop = (1, s.frame(s.bar(4)), s.frame(s.bar(12)))
    return s


# ---------------------------------------------------------------- 14. ambient

def ambient():
    s = Song2("Ambient: Slow Tide", 70)
    # E major leaning Lydian: Emaj7 - C#m7 - Amaj7(#11 in the bells) - Bsus4, two bars each. A shimmer pad
    # held, glass bells rising and falling, a choir singing a sparse line, a soft pulse, and wind that
    # swells up and falls back (its pitch rising and settling with it).
    pulse = s.synth("Pulse", "Lo-fi kick", macros=(0.3, 0.7, 0.4, 0.2, 0.5))
    shaker = s.synth("Shaker", "Trap hat", seconds=0.12, macros=(0.5, 0.7, 0.3, 0.2, 0.5))
    kit(s, (pulse, shaker))
    wind = s.synth("Wind", "Wind", seconds=12)
    drone = s.synth("Drone", "Drone", seconds=12)
    t_pulse = s.track("Pulse", 0.45, reverb=0.4)
    t_pad = s.track("Pad", 0.55, reverb=0.7)
    t_bell = s.track("Bells", 0.45, pan=0.3, delay=0.45, reverb=0.6)
    t_choir = s.track("Choir", 0.45, pan=-0.2, reverb=0.7)
    t_drone = s.track("Drone", 0.35, filt=1, cutoff=700)
    t_wind = s.track("Wind", 0.35, reverb=0.6, pan=0.2)
    s.fx.update(div=6, fb=0.55, tone=0.35, dlevel=0.6, ping=1, size=0.95, damp=0.2, rlevel=0.9)
    tide = s.drums(t_pulse, "Tide", 16, [(pulse, "X.......x.......", "........6......."),
                                          (shaker, "..x...x...x...x.", "..7...5...7...5.")])
    beats_every(s, tide, t_pulse, 4, 20)
    for b in range(0, 24, 4):
        s.clip(drone, t_drone, s.bar(b), beats=16, fade_in=2, fade_out=2)
    for b in (2, 10, 18):
        s.clip(wind, t_wind, s.bar(b), beats=20, fade_in=6, fade_out=6)
        s.bend([(0, -3), (10, 2), (20, -1)], keep=1)
    s.key("E Lydian")
    harmony_loop(s, 0, 24, ["Emaj7", "C#m7", "Amaj7", "Bsus4"], 2)
    s.section(0, 4, "Low water")
    s.section(4, 16, "Tide")
    s.section(20, 4, "Ebb")
    s.part("chords,style=Held,seed=1,sound=Shimmer pad,track=Pad")
    s.part("chords,style=Arp up & down,busy=0.3,seed=1,sound=Glass bell,track=Bells,octave=1")
    s.part("melody,style=Sparse,busy=0.3,seed=40,sound=Choir aah,bars=5-21,track=Choir,bend=scoops")
    s.pads = [(pulse, 0, 0.8, t_pulse), (shaker, 0, 0.7, t_pulse), (wind, 0, 0.6, t_wind), (drone, 0, 0.6, t_drone)]
    s.loop = (1, s.frame(s.bar(4)), s.frame(s.bar(12)))
    return s


# ---------------------------------------------------------------- 15. disco

def disco():
    s = Song2("Disco: Mirror Ball", 120)
    # A minor: Am7 - D9 - Gmaj7 - Cmaj7, a bar each. Four on the floor with open hats on the off-beats, an
    # octave bass, chord stabs and held strings, a supersaw hook, builds into each section, and Lumi
    # singing the chorus.
    kick = s.synth("Kick", "Floor kick")
    clap = s.synth("Clap", "Clap")
    hat = s.synth("Hat", "Trap hat", seconds=0.1)
    ohat = s.synth("Open hat", "Open hat", seconds=0.35)
    kit(s, (kick, clap, hat, ohat))
    riser = s.synth("Riser", "Riser", seconds=4)
    impact = s.synth("Impact", "Impact")
    hn = [(76, 0, 0.5), (76, 0.5, 0.5), (74, 1, 0.5), (72, 1.5, 1), (69, 3, 1), (72, 4, 0.5), (74, 4.5, 1), (76, 5.5, 2)]
    hook = s.vocal("Sung: mirror ball", "Mir-ror ball, spin me round, turn the lights up", hn, singer=LUMI, high=1,
                   vibrato=0.35, bends=shape(hn, scoop=1.0, fall=3.0))
    t_kick = s.track("Kick", 0.85)
    t_drums = s.track("Drums", 0.7, eq=(0, 1, 2), reverb=0.15)
    t_bass = s.track("Bass", 0.7, duck=0.5, drive=0.15)
    t_stab = s.track("Stabs", 0.5, pan=-0.2, delay=0.25, reverb=0.25, duck=0.4)
    t_str = s.track("Strings", 0.4, pan=0.2, reverb=0.5, duck=0.5)
    t_lead = s.track("Lead", 0.45, pan=0.1, delay=0.3, reverb=0.35)
    t_vox = s.track("Vocal", 0.85, delay=0.2, reverb=0.35)
    t_fx = s.track("FX", 0.45, reverb=0.5)
    s.fx.update(div=3, fb=0.35, tone=0.55, dlevel=0.5, ping=1, size=0.7, damp=0.4, rlevel=0.7, sc=t_kick, release=160)
    s.dynamics = (1, -18.0, 2.5, 15.0, 180.0, 3.0, 1, -1.0)
    floor = split_beat(s, t_kick, t_drums, "Floor", 16, (kick, "X...X...X...X..."),
                       [(clap, "....X.......X..."), (hat, "x.x.x.x.x.x.x.x.", "...5...5...5...5"),
                        (ohat, "..x...x...x...x.")])
    intro = s.drums(t_drums, "Intro", 16, [(hat, "x.x.x.x.x.x.x.x."), (ohat, "..x...x...x...x.")])
    beats_every(s, intro, t_drums, 0, 4)
    beats_every(s, floor, t_drums, 4, 32)
    for b in (2, 10, 18):
        s.clip(riser, t_fx, s.bar(b), beats=8, fade_in=4)
    for b in (4, 12, 20):
        s.clip(impact, t_fx, s.bar(b))
    s.key("A Minor")
    harmony_loop(s, 0, 32, ["Am7", "D9", "Gmaj7", "Cmaj7"])
    for b in (12, 16, 24, 28):
        s.clip(hook, t_vox, s.bar(b))
    s.section(0, 4, "Intro")
    s.section(4, 8, "Verse")
    s.section(12, 8, "Chorus")
    s.section(20, 4, "Break")
    s.section(24, 8, "Chorus")
    s.part("bass,style=Octaves,busy=0.6,seed=1,sound=Synth bass,bars=5-33,track=Bass")
    s.part("chords,style=Stabs,busy=0.7,seed=3,sound=Chord stab,track=Stabs")
    s.part("chords,style=Held,seed=1,sound=Silk pad,bars=5-33,track=Strings")
    s.part("melody,style=Hook,busy=0.6,seed=17,sound=Supersaw,bars=5-13,track=Lead,octave=1,bend=scoops")
    s.fills("style=Build,len=0.6,where=sections,track=Drums,seed=8")
    s.pads = [(kick, 0, 0.9, t_kick), (clap, 0, 0.9, t_drums), (hat, 0, 0.7, t_drums), (ohat, 0, 0.7, t_drums),
              (riser, 0, 0.8, t_fx), (impact, 0, 0.9, t_fx), (hook, 0, 0.8, t_vox)]
    s.loop = (1, s.frame(s.bar(12)), s.frame(s.bar(20)))
    return s


# ---------------------------------------------------------------- 16. afrobeats

def afrobeats():
    s = Song2("Afrobeats: Golden Hour", 104)
    # G major: Gmaj7 - Em7 - Cmaj7 - D, a bar each. A rolling kick and rim pattern, shakers on the 16ths,
    # a log-drum groove bass, plucks arpeggiating the chords, a vocal-like lead hook, and tom fills.
    kick = s.synth("Kick", "Floor kick", macros=(0.45, 0.5, 0.5, 0.35, 0.5))
    rim = s.synth("Rim", "Rimshot")
    shaker = s.synth("Shaker", "Trap hat", seconds=0.08, macros=(0.6, 0.6, 0.4, 0.3, 0.5))
    tom = s.synth("Tom", "Drum machine tom")
    clap = s.synth("Clap", "Clap")
    kit(s, (kick, rim, shaker, tom, clap))
    t_drums = s.track("Drums", 0.75, eq=(1, 0, 1), reverb=0.15)
    t_bass = s.track("Log drum", 0.7, eq=(2, 1, -2), drive=0.15)
    t_pluck = s.track("Plucks", 0.45, pan=-0.25, delay=0.3, reverb=0.3)
    t_keys = s.track("Keys", 0.4, pan=0.2, reverb=0.4)
    t_lead = s.track("Lead", 0.5, pan=0.1, delay=0.3, reverb=0.35)
    s.fx.update(div=3, fb=0.4, tone=0.5, dlevel=0.5, ping=1, size=0.65, damp=0.4, rlevel=0.6)
    s.dynamics = (1, -18.0, 2.5, 15.0, 180.0, 3.0, 1, -1.0)
    groove = s.drums(t_drums, "Golden", 16, [(kick, "X..x..x...X..x..", ".............5.."),
                                              (rim, "...x..x....x..x.", "......6........5"),
                                              (shaker, "xxxxxxxxxxxxxxxx", "5.5.5.5.5.5.5.5."),
                                              (clap, "....x.......x...", ""), (tom, "................")])
    lite = s.drums(t_drums, "Light", 16, [(rim, "...x..x....x..x."), (shaker, "x.xxx.xxx.xxx.xx", "5.5.5.5.5.5.5.5.")])
    beats_every(s, lite, t_drums, 0, 4)
    beats_every(s, groove, t_drums, 4, 28)
    s.key("G Major")
    harmony_loop(s, 0, 28, ["Gmaj7", "Em7", "Cmaj7", "D"])
    s.section(0, 4, "Intro")
    s.section(4, 8, "Verse")
    s.section(12, 8, "Hook")
    s.section(20, 8, "Hook")
    s.part("bass,style=Groove,busy=0.7,seed=6,sound=Round bass,bars=5-29,track=Log drum")
    s.part("chords,style=Arp up,busy=0.4,seed=1,sound=Pop pluck,track=Plucks,octave=1")
    s.part("chords,style=Pulse,busy=0.3,seed=1,sound=Smooth keys,bars=13-29,track=Keys")
    s.part("melody,style=Hook,busy=0.55,seed=44,sound=Ee lead,bars=13-29,track=Lead,bend=scoops")
    s.fills("style=Toms,len=0.5,where=sections,track=Drums,seed=9")
    s.pads = [(kick, 0, 0.9, t_drums), (rim, 0, 0.8, t_drums), (shaker, 0, 0.7, t_drums), (tom, 0, 0.8, t_drums),
              (clap, 0, 0.9, t_drums)]
    s.loop = (1, s.frame(s.bar(12)), s.frame(s.bar(20)))
    return s


if __name__ == "__main__":
    import subprocess
    tinydaw = os.path.join(ROOT, "tinydaw.exe" if os.name == "nt" else "tinydaw")
    for name, song in (("house-late-train", house()), ("hiphop-rooftop-static", hiphop()),
                       ("experimental-tidal-clockwork", experimental()), ("breakcore-static-bloom", breakcore()),
                       ("rock-paper-lanterns", rock()), ("trap-midnight-hollow", trap()),
                       ("synthwave-neon-coastline", synthwave()), ("techno-concrete-hours", techno()),
                       ("pop-paper-planes", pop()), ("chiptune-pixel-quest", chiptune()),
                       ("lofi-window-rain", lofi()), ("dnb-night-bus", dnb()), ("jazz-blue-lamp", jazz()),
                       ("ambient-slow-tide", ambient()), ("disco-mirror-ball", disco()),
                       ("afrobeats-golden-hour", afrobeats())):
        path = os.path.join(HERE, name + ".tdaw")
        song.write(path)
        # tinydaw writes the parts and fills from the chord track, and a session scene for each section
        r = subprocess.run([tinydaw, path] + song.cli + ["--scenes", "--save", path], capture_output=True, text=True)
        if r.returncode:
            raise SystemExit("%s: %s" % (name, r.stderr.strip()))
        lines = open(path).read().split("\n")       # tinydaw's save has no title: put it back
        lines = [ln for ln in lines if not ln.startswith(("title ", "saved-by "))]
        lines[1:1] = ["saved-by tinydaw %s (examples/make_examples.py)" % VERSION, "title %s" % song.title]
        open(path, "w").write("\n".join(lines))
