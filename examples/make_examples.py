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
                for c in sorted(self.clips, key=lambda c: (c[2], c[1])):
                    extra = " %d %d" % (c[8], c[9]) if len(c) > 8 else ""
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
    return s


# ---------------------------------------------------------------- 2. hip-hop

def hiphop():
    s = Song2("Hip-hop: Corner Store", 88)
    # D minor, a bar each: Dm9 - Gm7 - Bbmaj7 - A7. Swung sixteenths, ghost notes that come and go.
    chords = [("Dm9", [50, 53, 57, 60, 64], 38), ("Gm7", [50, 55, 58, 62, 65], 31),
              ("Bbmaj7", [50, 53, 57, 58, 62], 34), ("A7", [49, 52, 55, 57, 61], 33)]
    kick = s.synth("Boom bap kick", "Boom bap kick")
    snare = s.synth("Dusty snare", "Dusty snare")
    hat = s.synth("Dusty hat", "Trap hat", seconds=0.14, macros=(0.35, 0.45, 0.45, 0.55, 0.5))
    rim = s.synth("Rim", "Rimshot", macros=(0.4, 0.5, 0.5, 0.5, 0.5))
    keys = s.synth("Soul keys", "Soul keys", seconds=2.5)
    bass = s.synth("Upright bass", "Upright bass", seconds=1.2)
    bell = s.synth("Glass bell", "Glass bell", seconds=2)
    kit(s, (kick, snare, hat, rim, keys, bass, bell))
    crackle = s.synth("Vinyl crackle", "Vinyl crackle", seconds=8)
    air = {n: s.chord("%s air" % n, [x + 12 for x in notes[2:]], SINE, 4, 0.12, 300, 900) for n, notes, _ in chords}
    hook_notes = [(57, 0, 0.5), (57, 0.5, 0.5), (60, 1, 1.5), (57, 3, 0.5), (55, 3.5, 1), (53, 4.5, 0.5), (50, 5, 2.5),
                  (57, 8, 0.5), (60, 8.5, 1), (62, 9.5, 1), (60, 11, 0.5), (57, 11.5, 0.5), (55, 12, 1), (53, 13, 0.5),
                  (50, 13.5, 2.5)]
    hn = [(n, st, ln * 0.95) for n, st, ln in hook_notes]
    hook = s.vocal("Sung: corner store", "Cor-ner store, the lights stay on. Late-night talk till the dark is gone.",
                   hn, singer=KAI, vibrato=0.3, bends=shape(hn, scoop=1.5, slide=True, fall=2.0))

    t_drums = s.track("Drums", 0.8, eq=(1, 0, 1), drive=0.2, reverb=0.08)
    t_bass = s.track("Bass", 0.7, filt=1, cutoff=900, drive=0.1)
    t_keys = s.track("Keys", 0.75, filt=1, cutoff=2600, reso=0.15, eq=(-3, 1, 0), drive=0.15, reverb=0.3)
    t_air = s.track("Air", 0.35, pan=0.25, reverb=0.5)
    t_bell = s.track("Bells", 0.45, pan=0.2, delay=0.38, reverb=0.35)
    t_vox = s.track("Vocal", 0.8, delay=0.15, reverb=0.25)
    t_vinyl = s.track("Vinyl", 0.45, filt=2, cutoff=2000, pan=-0.2)
    s.fx.update(div=4, fb=0.42, tone=0.35, dlevel=0.6, ping=0, size=0.55, damp=0.6, rlevel=0.7)
    s.dynamics = (1, -18.0, 2.5, 15.0, 180.0, 3.0, 1, -1.0)

    verse = s.drums(t_drums, "Boom bap", 16, [(kick, "X......o.xX....."), (snare, "....X..g....X..g", ".......4.......6"),
                                              (hat, "x.o.x.o.x.o.x.ox", "...............6"),
                                              (rim, "..........g...g.", "..........5...3.")], swing=0.2)
    hookd = s.drums(t_drums, "Boom bap B", 32, [(kick, "X......o.xX.....X.....x.x.X...o."),
                                                (snare, "....X..g....X..g....X..g....X.oX", ".......5.......6.......5........"),
                                                (hat, "x.o.x.o.x.o.x.oxx.o.x.o.x.o.xoxo", "...............6.............5.5"),
                                                (rim, "..g.......g.......g.......g.....", "..4.......6.......4.......6.....")],
                    swing=0.2)
    brk = s.drums(t_drums, "Hats alone", 16, [(hat, "o.g.o.g.o.g.o.go", "..5...5...5...55"), (rim, "....g.......g...", "....6.......6...")],
                  swing=0.2)
    s.drumclip(verse, t_drums, s.bar(4), 32)
    s.drumclip(hookd, t_drums, s.bar(12), 16)
    s.drumclip(brk, t_drums, s.bar(16), 8)
    s.drumclip(hookd, t_drums, s.bar(18), 16)

    for b in range(0, 24, 2):
        s.clip(crackle, t_vinyl, s.bar(b), beats=8, fade_in=1 if b == 0 else 0, fade_out=3 if b == 22 else 0)
    for bar in range(24):
        name, notes, root = chords[bar % 4]
        comp = [(0, 1.5, 0.8), (1.75, 0.5, 0.45), (2.5, 1.4, 0.65)] if bar % 2 == 0 else \
               [(0, 1.0, 0.75), (1.5, 0.5, 0.4), (2.25, 0.5, 0.55), (3, 1, 0.6)]
        s.noteclip(keys, t_keys, s.bar(bar), 4, [(n, at, ln, v - 0.04 * i) for at, ln, v in comp
                                                 for i, n in enumerate(notes)])
        if 12 <= bar < 22:
            s.clip(air[name], t_air, s.bar(bar), fade_in=1)
        if 4 <= bar < 22 and not 16 <= bar < 18:
            s.noteclip(bass, t_bass, s.bar(bar), 4, [(root, 0, 1.25, 0.9), (root + 7, 2.75, 0.5, 0.55),
                                                     (root, 3.5, 0.5, 0.7)])
    melody = [(0, 81, 1), (1.5, 79, 0.5), (2, 77, 1), (4, 74, 2), (8, 77, 1), (9, 79, 1), (10.5, 81, 0.5),
              (11, 84, 1), (12, 81, 2)]
    for start in (12, 18):
        s.noteclip(bell, t_bell, s.bar(start), 16, [(n, at, ln, 0.5 + 0.1 * (k % 3)) for k, (at, n, ln) in enumerate(melody)])
        s.clip(hook, t_vox, s.bar(start))
    s.pads = [(kick, 0, 0.9, t_drums), (snare, 0, 0.9, t_drums), (hat, 0, 0.7, t_drums), (rim, 0, 0.8, t_drums),
              (bell, 0, 0.7, t_bell), (hook, 0, 0.8, t_vox), (crackle, 2, 0.6, t_vinyl)]
    s.instrument = (keys, 60, t_keys)
    s.loop = (1, s.frame(s.bar(12)), s.frame(s.bar(16)))
    s.section(0, 4, "Intro")
    s.section(4, 8, "Verse")
    s.section(12, 4, "Hook")
    s.section(16, 2, "Break")
    s.section(18, 4, "Hook")
    s.section(22, 2, "Outro")
    return s


# ---------------------------------------------------------------- 3. experimental

def experimental():
    s = Song2("Experimental: Glass Weather", 96)
    # F Lydian (the raised fourth, B, makes it float). The bar changes under the music: 4/4, then 7/8,
    # then 5/4. The drum pattern is always sixteen steps, so it slides against each new bar, and most
    # of its hits only happen some of the time: no two passes are the same.
    s.change(8, 96, 7, 8)
    s.change(16, 96, 5, 4)
    s.change(24, 96, 4, 4)
    drone_lo = s.gen("drone F1", SAW, 29, 8, 0.32, 1500, 1500)
    drone_hi = s.gen("drone C2", SAW, 36, 8, 0.26, 1500, 1500)
    glass = s.synth("Glass bell", "Glass bell", seconds=2.5)
    kick = s.synth("Soft kick", "Lo-fi kick")
    noise = s.synth("Noise hit", "Noise drum", macros=(0.6, 0.4, 0.5, 0.6, 0.5))
    rim = s.synth("Rim", "Rimshot")
    snap = s.synth("Snap", "Snap")
    kit(s, (glass, kick, noise, rim, snap))
    rglass = s.variant2("Glass bell reversed", glass, -12, 1)
    wind = s.synth("Wind", "Wind", seconds=8)
    sub = s.gen("sub F0", SINE, 17, 2, 0.6, 20, 600)
    choir_notes = [(65, 0, 3), (69, 3, 3), (71, 6, 5), (72, 12, 3), (71, 15, 2), (69, 17, 6)]
    choir_bends = [(0.0, -2.0), (1.0, 0.0), (2.6, 0.0), (2.99, 4.0), (3.0, 0.0), (5.4, 0.0), (5.99, 2.0), (6.0, 0.0),
                   (9.0, 0.0), (10.5, 0.5), (11.0, 0.0), (14.4, 0.0), (14.99, -1.0), (15.0, 0.0), (16.6, 0.0),
                   (16.99, -2.0), (17.0, 0.0), (21.0, 0.0), (23.0, -5.0)]
    choir = s.vocal("Sung: glass weather", "Rain on glass, slow-ly fall-ing", choir_notes, style=VS_CHOIR,
                    high=1, vibrato=0.6, volume=0.7, bends=choir_bends)

    t_lo = s.track("Drone low", 0.7, pan=-0.35, filt=3, cutoff=420, reso=0.7, drive=0.6, reverb=0.25, duck=0.5)
    t_hi = s.track("Drone high", 0.6, pan=0.35, filt=3, cutoff=640, reso=0.7, drive=0.5, reverb=0.25, duck=0.5)
    t_glass = s.track("Glass", 0.6, pan=0.1, delay=0.45, reverb=0.6)
    t_rev = s.track("Glass reversed", 0.45, pan=-0.2, filt=1, cutoff=3000, reverb=0.5)
    t_wind = s.track("Wind", 0.5, filt=3, cutoff=2400, reso=0.85, reverb=0.4)
    t_kit = s.track("Drums", 0.65, eq=(0, -2, 2), drive=0.3, reverb=0.2, delay=0.15)
    t_choir = s.track("Choir", 0.6, pan=-0.1, reverb=0.7)
    t_sub = s.track("Sub", 0.7, duck=0.4)
    s.fx.update(div=3, fb=0.58, tone=0.45, dlevel=0.75, ping=1, size=0.92, damp=0.25, rlevel=0.9, sc=t_kit, release=260)
    s.dynamics = (1, -20.0, 2.0, 20.0, 250.0, 2.0, 1, -1.0)   # gentle glue, and a limiter

    end = s.bar(28)
    at = 0.0
    while at < end:                                 # drones breathe in eight-beat swells
        s.clip(drone_lo, t_lo, at, beats=min(8, end - at), fade_in=1, fade_out=1)
        if at >= 8:
            s.clip(drone_hi, t_hi, at + 4 if at + 12 <= end else at, beats=min(8, end - at), fade_in=1, fade_out=1)
        at += 8
    for b in (0, 6, 14, 22):
        s.clip(wind, t_wind, s.bar(b), fade_in=3, fade_out=3)
    # glass falling in fives against the bar, louder then softer: the layers change its colour
    lyd = [77, 81, 83, 84, 88, 84, 83, 81]
    notes, k, at = [], 0, 0.0
    while at < s.bar(24) - s.bar(4):
        notes.append((lyd[(k * 3) % 8], at, 1.0, 0.35 + 0.55 * abs(math.sin(k * 0.45))))
        k, at = k + 1, at + 1.25
    s.noteclip(glass, t_glass, s.bar(4), s.bar(24) - s.bar(4), notes)
    for i, b in enumerate(range(6, 26, 2)):         # reversed glass rising into the bar lines
        s.clip(rglass, t_rev, s.bar(b) - 2, beats=2, fade_in=0.5)
    pulse = s.drums(t_kit, "Weather", 16, [(kick, "X.....o...x.....", "......6...7....."),
                                           (noise, "....x..g....x.g.", ".......4......53"),
                                           (rim, "..g.g..o.g.g.o..", "..3.4..6.3.4.5.."),
                                           (snap, ".......x......x.", ".......5......6.")])
    s.drumclip(pulse, t_kit, s.bar(8), s.bar(24) - s.bar(8))
    for at in range(int(s.bar(8)), int(s.bar(24)), 7):   # the sub every seven beats, against everything
        s.clip(sub, t_sub, at)
    s.clip(choir, t_choir, s.bar(16))
    s.clip(choir, t_choir, s.bar(24) - 2, gain=-4)
    s.pads = [(glass, 0, 0.8, t_glass), (rglass, 0, 0.8, t_rev), (kick, 0, 0.8, t_kit), (noise, 0, 0.8, t_kit),
              (rim, 0, 0.8, t_kit), (snap, 0, 0.8, t_kit), (sub, 0, 0.9, t_sub), (wind, 1, 0.7, t_wind),
              (drone_lo, 1, 0.7, t_lo), (drone_hi, 1, 0.7, t_hi), (choir, 0, 0.7, t_choir)]
    s.instrument = (glass, 79, t_glass)
    s.section(0, 8, "Fog")
    s.section(8, 8, "Glass (7/8)")
    s.section(16, 8, "Choir (5/4)")
    s.section(24, 4, "Clearing")
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
    return s


# ---------------------------------------------------------------- 5. rock with vocals

def rock():
    s = Song2("Rock: Highway Lights", 128)
    # E minor: Em - C - G - D. Intro, verse (sung), chorus (sung, with harmony), a guitar solo, chorus.
    guitar_patch = dict(wave=0, wave2=1, mix2=0.55, voices=2, detune=9, width=0.6, filter=1, cutoff=2600, reso=0.2,
                        fenv=0.6, fdecay=0.25, attack=0.003, decay=0.35, sustain=0.75, release=0.12, drive=0.95)
    lead_patch = dict(guitar_patch, voices=1, vibrato=22, lfo_hz=5.5, cutoff=3400, mix2=0.4, sustain=0.85)
    kick = s.synth("Rock kick", "Boom bap kick", macros=(0.6, 0.4, 0.6, 0.45, 0.5))
    snare = s.synth("Rock snare", "Break snare", seconds=0.35, macros=(0.55, 0.6, 0.6, 0.5, 0.5))
    hat = s.synth("Closed hat", "Trap hat", seconds=0.15, macros=(0.45, 0.55, 0.5, 0.4, 0.5))
    ohat = s.synth("Open hat", "Open hat", seconds=0.5)
    tom = s.synth("Tom", "Drum machine tom", macros=(0.45, 0.5, 0.6, 0.4, 0.5))
    crash = s.synth("Crash", "Open hat", seconds=1.6, volume=0.75, macros=(0.6, 0.95, 0.8, 0.6, 0.5))
    bass = s.synth("Picked bass", "Synth bass", seconds=0.5, macros=(0.55, 0.4, 0.6, 0.65, 0.5))
    gtr = s.synth("Crunch guitar", patch=guitar_patch, note=52, seconds=2.2, volume=0.75)
    lead = s.synth("Lead guitar", patch=lead_patch, note=64, seconds=2.2, volume=0.75)
    kit(s, (kick, snare, hat, ohat, tom, bass, gtr, lead))

    t_kick = s.track("Drums", 0.85, eq=(1, 0, 1), reverb=0.15)
    t_snare = t_hats = t_kick
    t_bass = s.track("Bass", 0.7, filt=1, cutoff=1600, drive=0.2)
    t_gl = s.track("Guitar L", 0.55, pan=-0.6, eq=(-2, 1, 0))
    t_gr = s.track("Guitar R", 0.55, pan=0.6, eq=(-2, 1, 0))
    t_lead = s.track("Lead guitar", 0.55, pan=0.1, delay=0.25, reverb=0.3)
    t_vox = s.track("Vocal", 0.85, delay=0.12, reverb=0.3)
    t_harm = s.track("Harmony", 0.55, pan=-0.25, reverb=0.4)
    t_cym = s.track("Cymbals", 0.45, pan=-0.2, reverb=0.2)
    s.fx.update(div=4, fb=0.3, tone=0.5, dlevel=0.5, ping=0, size=0.65, damp=0.45, rlevel=0.7)
    s.dynamics = (1, -18.0, 2.5, 15.0, 180.0, 3.0, 1, -1.0)   # Glue

    verse = s.drums(t_kick, "Verse beat", 32, [(kick, "X.....x.X.......X.......x.X....."),
                                               (snare, "....X..g....X.......X......gX..g", ".......5...........6.......4..5"),
                                               (hat, "X.x.x.x.X.x.x.x.X.x.x.x.X.x.x.xo")])
    chorus = s.drums(t_kick, "Chorus beat", 32, [(kick, "X.....x.X.x.....X.....x.X.x...x."),
                                                 (snare, "....X.......X.......X.......X..g", "...............................5"),
                                                 (hat, "X...x...X...x...X...x...X...x..."),
                                                 (ohat, "..x...x...x...x...x...x...x...x.")])
    fill = s.drums(t_kick, "Fill", 16, [(kick, "X.......X......."), (snare, "....X.......gogx"),
                                         (tom, "........x.o.....")])
    s.drumclip(verse, t_kick, s.bar(0), s.bar(12))
    for (b0, b1, pid) in ((12, 19, chorus), (19, 20, fill), (20, 23, verse), (23, 24, fill), (24, 31, chorus), (31, 32, fill)):
        s.drumclip(pid, t_kick, s.bar(b0), s.bar(b1) - s.bar(b0))
    for b in (4, 12, 20, 24, 32):
        s.clip(crash, t_cym, s.bar(b))
    s.clip(kick, t_kick, s.bar(32))
    prog = [40, 36, 43, 38]
    def power(root):
        r = root + 12
        return [r, r + 7, r + 12]
    for b in range(32):
        root = prog[b % 4]
        chor = 12 <= b < 20 or 24 <= b < 32
        s.noteclip(bass, t_bass, s.bar(b), 4, [(root, i * 0.5, 0.45, 0.95 if i % 4 == 0 else 0.75 if i % 2 == 0 else 0.55)
                                               for i in range(8)])
        for tr, shift in ((t_gl, 0), (t_gr, 0.01)):
            if chor or b < 4:
                notes = [(n, shift, 3.9, 0.9) for n in power(root)]
            else:                                    # palm-muted: accents on the beat, lighter between
                notes = [(n, i * 0.5 + shift, 0.3, 0.85 if i % 2 == 0 else 0.5) for i in range(8) for n in power(root)[:2]]
            s.noteclip(gtr, tr, s.bar(b), 4, notes)
    s.auto(t_gl, AUTO_VOLUME, 0, 0.0)
    s.auto(t_gl, AUTO_VOLUME, s.bar(2), 0.37)
    s.auto(t_gr, AUTO_VOLUME, 0, 0.0)
    s.auto(t_gr, AUTO_VOLUME, s.bar(2), 0.37)
    solo = [(64, 0, 1.5), (67, 1.5, 0.5), (69, 2, 1), (71, 3, 1), (74, 4, 2), (71, 6, 1), (69, 7, 1),
            (67, 8, 1.5), (64, 9.5, 0.5), (67, 10, 1), (69, 11, 1), (71, 12, 1), (69, 13, 0.5), (67, 13.5, 0.5),
            (66, 14, 1), (64, 15, 1)]
    s.noteclip(lead, t_lead, s.bar(20), 16, [(n, st, ln, 0.6 + 0.35 * (ln >= 1)) for n, st, ln in solo])

    verse_words = ("Head-lights burn-ing on the road to-night. Ci-ty fad-ing in the rear-view light. "
                   "En-gine roar-ing as the stars come out. No-thing left to lose and no-thing left to doubt.")
    r9 = [(0, 0.5), (0.5, 0.5), (1, 0.5), (1.5, 0.5), (2, 0.5), (2.5, 0.5), (3, 1), (4, 0.5), (4.5, 2.5)]
    r11 = [(0, 0.5), (0.5, 0.5), (1, 0.5), (1.5, 0.5), (2, 1), (3, 0.5), (3.5, 0.5), (4, 0.5), (4.5, 0.5),
           (5, 0.5), (5.5, 2)]
    lines = [([52, 52, 55, 55, 57, 55, 59, 57, 55], r9), ([59, 59, 57, 55, 54, 52, 55, 54, 52], r9),
             ([52, 55, 57, 59, 57, 55, 62, 59, 57], r9), ([59, 59, 57, 55, 57, 55, 54, 54, 52, 50, 52], r11)]
    vnotes = []
    for li, (pitches, rhythm) in enumerate(lines):
        vnotes += [(p, li * 8 + st, ln * 0.95) for p, (st, ln) in zip(pitches, rhythm)]
    chorus_words = "Hold on, we're run-ning to the light. Hold on, we're a-live to-night. " * 2
    ca = [(62, 0, 1), (64, 1, 1), (62, 2.5, 0.5), (64, 3, 0.5), (67, 3.5, 0.5), (66, 4, 0.5), (64, 4.5, 0.5), (64, 5, 2.5)]
    cb = [(62, 0, 1), (64, 1, 1), (62, 2.5, 0.5), (59, 3, 0.5), (62, 3.5, 1.5), (59, 5, 0.5), (64, 5.5, 2.5)]
    cnotes = []
    for rep in range(2):
        cnotes += [(n, rep * 16 + st, ln * 0.95) for n, st, ln in ca]
        cnotes += [(n, rep * 16 + 8 + st, ln * 0.95) for n, st, ln in cb]
    E_MINOR = [52, 54, 55, 57, 59, 60, 62, 64, 66, 67, 69, 71, 72, 74, 76, 78, 79, 81, 83, 84]
    third = lambda n: E_MINOR[E_MINOR.index(n) + 2] if n in E_MINOR else n + 3
    harmony = [(third(n) + 12, st, ln) for n, st, ln in cnotes]
    v = s.vocal("Sung: verse", verse_words, vnotes, singer=KAI, bends=shape(vnotes, scoop=1.0, slide=False, fall=2.0))
    c = s.vocal("Sung: chorus", chorus_words, cnotes, singer=KAI, bends=shape(cnotes, scoop=1.5, slide=True, fall=0))
    h = s.vocal("Sung: chorus harmony", chorus_words, harmony, singer=AIRA, high=1, vibrato=0.35,
                bends=shape(harmony, scoop=0, slide=True, fall=0))
    s.clip(v, t_vox, s.bar(4))
    s.clip(c, t_vox, s.bar(12))
    s.clip(h, t_harm, s.bar(12))
    s.clip(c, t_vox, s.bar(24))
    s.clip(h, t_harm, s.bar(24))
    s.pads = [(kick, 0, 0.9, t_kick), (snare, 0, 0.9, t_snare), (hat, 0, 0.7, t_hats), (ohat, 0, 0.7, t_hats),
              (tom, 0, 0.8, t_snare), (crash, 0, 0.8, t_cym), (bass, 0, 0.9, t_bass), (gtr, 1, 0.8, t_gl),
              (lead, 1, 0.8, t_lead), (v, 0, 0.8, t_vox), (c, 0, 0.8, t_vox), (h, 0, 0.7, t_harm)]
    s.instrument = (gtr, 52, t_gl)
    s.loop = (1, s.frame(s.bar(12)), s.frame(s.bar(20)))
    s.section(0, 4, "Intro")
    s.section(4, 8, "Verse")
    s.section(12, 8, "Chorus")
    s.section(20, 4, "Solo")
    s.section(24, 8, "Chorus")
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
    return s


if __name__ == "__main__":
    for name, song in (("house-late-train", house()), ("hiphop-corner-store", hiphop()),
                       ("experimental-glass-weather", experimental()), ("breakcore-static-bloom", breakcore()),
                       ("rock-highway-lights", rock()), ("trap-midnight-hollow", trap())):
        song.write(os.path.join(HERE, name + ".tdaw"))
