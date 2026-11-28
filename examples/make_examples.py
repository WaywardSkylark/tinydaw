#!/usr/bin/env python3
"""Writes tinydaw's example songs (examples/*.tdaw).

The songs are made only from what tinydaw can make itself: synth notes and chords, the genre
drum kits from src/theory.c, pitched and reversed copies, clip gain and fades, and the mixer's
track effects, delay, reverb and sidechain. A project stores recipes, so the files are small
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


# ---------------------------------------------------------------- 1. house

def house():
    s = Song("House: Late Train", 124)
    # A minor: Am7 - Fmaj7 - Cmaj7 - G6, a bar each
    chords = [("Am7", [57, 60, 64, 67], 33), ("Fmaj7", [53, 57, 60, 64], 29),
              ("Cmaj7", [55, 59, 60, 64], 36), ("G6", [55, 59, 62, 64], 31)]
    kick = s.beat("House kick", "House", KICK_L, root=9)
    clap = s.beat("House clap", "House", CLAP_L, root=9)
    tops = s.beat("House hats", "House", HAT_L | OPEN_L, root=9)
    bass = {n: s.gen("bass %s" % n, SAW, root, 0.5, 0.55, 2, 40) for n, _, root in chords}
    stab = {n: s.chord("%s stab" % n, notes, SAW, 0.5, 0.42, 3, 90) for n, notes, _ in chords}
    pad = {n: s.chord("%s pad" % n, [x - 12 for x in notes] + [notes[-1]], TRI, 4, 0.30, 450, 900)
           for n, notes, _ in chords}
    riser = s.variant("Am7 pad reversed", pad["Am7"], 0, 1)
    snare = s.gen("snare 16th", SNARE, 60, 0.25, 0.55)
    lead_notes = {n: s.gen("lead %d" % n, SQUARE, n, 0.5, 0.28, 4, 140) for n in (69, 72, 74, 76, 79, 81)}

    t_kick = s.track("Kick", 0.9)
    t_clap = s.track("Clap", 0.7, reverb=0.15)
    t_hats = s.track("Hats", 0.55, pan=0.15, eq=(0, 0, 2))
    t_bass = s.track("Bass", 0.75, filt=1, cutoff=700, reso=0.35, drive=0.25, duck=0.7)
    t_stab = s.track("Stabs", 0.7, pan=-0.15, filt=1, cutoff=2800, delay=0.3, reverb=0.3, duck=0.5)
    t_pad = s.track("Pad", 0.6, pan=0.2, filt=1, cutoff=3200, reverb=0.55, duck=0.35)
    t_lead = s.track("Lead", 0.5, pan=0.1, filt=1, cutoff=4500, delay=0.4, reverb=0.3, duck=0.3)
    t_fx = s.track("Riser", 0.6, reverb=0.4)
    s.fx.update(div=4, fb=0.38, tone=0.55, dlevel=0.7, ping=1, size=0.7, damp=0.4, rlevel=0.8, sc=t_kick, release=150)

    full = list(range(4, 16)) + list(range(20, 32))
    s.every_bar(tops, t_hats, list(range(0, 16)) + list(range(20, 32)))
    s.every_bar(kick, t_kick, full)
    s.every_bar(clap, t_clap, list(range(12, 16)) + list(range(20, 28)))
    for bar in range(32):
        name, notes, root = chords[bar % 4]
        if bar in full or bar >= 28:
            for off in (0.5, 1.5, 2.5, 3.5):          # off-beat bass
                if bar >= 4 and bar < 28 or bar >= 28 and off < 2:
                    s.clip(bass[name], t_bass, bar * 4 + off)
        if 12 <= bar < 28:
            for off in (0.75, 1.75, 2.5, 3.5):        # chord stabs
                s.clip(stab[name], t_stab, bar * 4 + off)
        last = bar == 31
        s.clip(pad[name], t_pad, bar * 4, fade_in=2 if bar == 0 else 0, fade_out=4 if last else 0)
    # the lead: two answering phrases over the drop
    phrase = [(0, 76, 1), (1, 74, 0.5), (1.5, 72, 0.5), (2, 69, 1.5), (4, 72, 0.5), (4.5, 74, 0.5), (5, 76, 1),
              (6, 79, 1), (7, 76, 1), (8, 81, 1.5), (9.5, 79, 0.5), (10, 76, 1), (11, 74, 0.5), (11.5, 72, 0.5),
              (12, 74, 1), (13, 72, 1), (14, 69, 2)]
    for start_bar in (16, 20, 24):
        for at, note, ln in phrase:
            s.clip(lead_notes[note], t_lead, start_bar * 4 + at, beats=min(ln, 0.5) if start_bar == 16 else ln)
    s.clip(riser, t_fx, 76, fade_in=2)                # into the drop at bar 21
    for i in range(16):                                # a snare roll over bar 20
        s.clip(snare, t_fx, 76 + i * 0.25, gain=-20 + i * 1.25)
    s.pads = [(kick, 0, 0.9, t_kick), (clap, 0, 0.8, t_clap), (tops, 0, 0.7, t_hats), (snare, 0, 0.8, t_fx)] + \
             [(stab[n], 0, 0.8, t_stab) for n, _, _ in chords] + [(bass[n], 0, 0.8, t_bass) for n, _, _ in chords] + \
             [(pad[n], 1, 0.7, t_pad) for n, _, _ in chords]
    s.loop = (1, 80 * s.bf, 96 * s.bf)
    s.section(0, 4, "Intro")
    s.section(4, 8, "Groove")
    s.section(12, 4, "Build")
    s.section(16, 4, "Break")
    s.section(20, 12, "Drop")
    return s


# ---------------------------------------------------------------- 2. hip-hop

def hiphop():
    s = Song("Hip-hop: Corner Store", 88)
    # D minor, a bar each: Dm9 - Gm7 - Bbmaj7 - A7
    chords = [("Dm9", [50, 53, 57, 60, 64], 38), ("Gm7", [50, 55, 58, 62, 65], 31),
              ("Bbmaj7", [50, 53, 57, 58, 62], 34), ("A7", [49, 52, 55, 57, 61], 33)]
    drums = s.beat("Boom bap kick+snare", "Hip-hop", KICK_L | SNARE_L, root=2)
    hats = s.beat("Boom bap hats", "Hip-hop", HAT_L | PERC_L, root=2)
    keys = {n: s.chord("%s keys" % n, notes, TRI, 4, 0.42, 12, 500) for n, notes, _ in chords}
    shimmer = {n: s.chord("%s air" % n, [x + 12 for x in notes[2:]], SINE, 4, 0.12, 300, 900) for n, notes, _ in chords}
    bass = {n: s.gen("808 %s" % n, SINE, root - 12 if root > 36 else root, 1.5, 0.5, 4, 250) for n, _, root in chords}
    bell = {n: s.gen("bell %d" % n, SINE, n, 1, 0.3, 2, 700) for n in (74, 77, 79, 81, 84)}
    hiss = s.gen("tape hiss", NOISE, 60, 8, 0.05, 800, 800)

    t_drums = s.track("Drums", 0.8, filt=1, cutoff=12000, reso=0.1, eq=(1, 0, 1), drive=0.2)
    t_hats = s.track("Hats", 0.55, pan=-0.12, eq=(0, 0, 2))
    t_bass = s.track("808", 0.6, filt=1, cutoff=380, drive=0.12)
    t_keys = s.track("Keys", 0.8, filt=1, cutoff=2600, reso=0.15, eq=(-3, 1, 0), drive=0.15, reverb=0.3)
    t_air = s.track("Air", 0.4, pan=0.25, reverb=0.5)
    t_bell = s.track("Bells", 0.45, pan=0.2, delay=0.38, reverb=0.35)
    t_hiss = s.track("Tape", 0.4, filt=2, cutoff=4500, pan=-0.2)
    s.fx.update(div=4, fb=0.42, tone=0.35, dlevel=0.6, ping=0, size=0.55, damp=0.6, rlevel=0.7)

    for b in range(0, 24, 2):
        s.clip(hiss, t_hiss, b * 4, fade_in=1 if b == 0 else 0, fade_out=3 if b == 22 else 0)
    for bar in range(24):
        name, notes, root = chords[bar % 4]
        s.clip(keys[name], t_keys, bar * 4, fade_out=3 if bar == 23 else 0)
        if 8 <= bar < 24:
            s.clip(shimmer[name], t_air, bar * 4, fade_in=1)
        if 4 <= bar < 23 and not 16 <= bar < 18:
            s.clip(drums, t_drums, bar * 4)
            s.clip(bass[name], t_bass, bar * 4)
            s.clip(bass[name], t_bass, bar * 4 + 2.75, beats=1)
        if 4 <= bar < 23:
            s.clip(hats, t_hats, bar * 4)
    melody = [(0, 81, 1), (1.5, 79, 0.5), (2, 77, 1), (4, 74, 2), (8, 77, 1), (9, 79, 1), (10.5, 81, 0.5),
              (11, 84, 1), (12, 81, 2)]
    for start in (12, 20):
        for at, note, ln in melody:
            s.clip(bell[note], t_bell, start * 4 + at, beats=ln)
    s.pads = [(drums, 0, 0.9, t_drums), (hats, 0, 0.7, t_hats)] + [(keys[n], 1, 0.8, t_keys) for n, _, _ in chords] + \
             [(bass[n], 1, 0.9, t_bass) for n, _, _ in chords] + [(bell[n], 0, 0.7, t_bell) for n in (74, 77, 79, 81, 84)]
    s.loop = (1, 48 * s.bf, 64 * s.bf)
    s.section(0, 8, "Verse")
    s.section(8, 8, "Hook")
    s.section(16, 8, "Verse")
    return s


# ---------------------------------------------------------------- 3. experimental

def experimental():
    s = Song("Experimental: Glass Weather", 96)
    # F Lydian (the raised fourth, B, makes it float). Bells fall in fives against the four-beat bar,
    # drums loop every three and a half beats, so the parts keep shifting against each other.
    drone_lo = s.gen("drone F1", SAW, 29, 8, 0.32, 1500, 1500)
    drone_hi = s.gen("drone C2", SAW, 36, 8, 0.26, 1500, 1500)
    bells = [s.gen("glass %d" % n, SINE, n, 1, 0.34, 2, 650) for n in (77, 81, 83, 84, 88)]
    rbells = [s.variant("glass %d low reversed" % n, b, -12, 1) for n, b in zip((77, 81, 83, 84, 88), bells)]
    wind = s.gen("wind", NOISE, 60, 8, 0.22, 2800, 2800)
    kit = s.beat("Breakbeat kit", "Breakbeat", KICK_L | SNARE_L | HAT_L | OPEN_L, root=5)
    snare = s.gen("snare", SNARE, 60, 0.5, 0.6)
    glitch = s.variant("snare up a fifth reversed", snare, 7, 1)
    sub = s.gen("sub F0", SINE, 17, 2, 0.6, 20, 600)

    t_lo = s.track("Drone low", 0.72, pan=-0.35, filt=3, cutoff=420, reso=0.7, drive=0.6, reverb=0.25, duck=0.6)
    t_hi = s.track("Drone high", 0.62, pan=0.35, filt=3, cutoff=640, reso=0.7, drive=0.5, reverb=0.25, duck=0.6)
    t_bell = s.track("Glass", 0.62, pan=0.1, delay=0.45, reverb=0.6)
    t_rev = s.track("Glass reversed", 0.45, pan=-0.2, filt=1, cutoff=3000, reverb=0.5)
    t_wind = s.track("Wind", 0.55, filt=3, cutoff=2400, reso=0.85, reverb=0.4)
    t_kit = s.track("Drums", 0.62, filt=2, cutoff=120, drive=0.5, eq=(0, -3, 2), reverb=0.15)
    t_glitch = s.track("Glitch", 0.45, pan=0.4, delay=0.5, filt=3, cutoff=3500, reso=0.5)
    t_sub = s.track("Sub", 0.7, duck=0.4)
    s.fx.update(div=3, fb=0.58, tone=0.45, dlevel=0.75, ping=1, size=0.92, damp=0.25, rlevel=0.9, sc=t_kit, release=260)

    total = 24 * 4
    for at in range(0, total, 8):                      # drones and wind breathe in eight-beat swells
        s.clip(drone_lo, t_lo, at, fade_in=1, fade_out=1)
        if at >= 8:
            s.clip(drone_hi, t_hi, at + 4 if at + 12 <= total else at, fade_in=1, fade_out=1)
    for at in (0, 12, 28, 44, 60, 76):
        s.clip(wind, t_wind, at, fade_in=3, fade_out=3)
    k, at = 0, 16.0                                    # glass every 1.25 beats: 5 against 4
    while at < 80:
        s.clip(bells[(k * 3) % 5], t_bell, at, gain=-3 if k % 4 else 0)
        k, at = k + 1, at + 1.25
    for i, at in enumerate(range(30, 94, 6)):          # reversed glass rising into the bells
        s.clip(rbells[i % 5], t_rev, at, fade_in=0.5)
    at = 32.0                                          # drums: 3.5-beat loop against the bar
    while at < 80:
        s.clip(kit, t_kit, at, beats=3.5, fade_out=0.05)
        at += 3.5
    for i, at in enumerate([48.5, 51.25, 53.75, 57, 60.5, 61.25, 66.75, 70, 71.5, 74.25, 77.75]):
        s.clip(glitch, t_glitch, at, beats=0.25 + (i % 3) * 0.125, gain=-2 * (i % 2))
    for at in range(32, 80, 7):                        # the sub follows the drum cycle, every 7 beats
        s.clip(sub, t_sub, at)
    s.pads = [(b, 0, 0.8, t_bell) for b in bells] + [(r, 0, 0.8, t_rev) for r in rbells] + \
             [(kit, 2, 0.8, t_kit), (glitch, 0, 0.8, t_glitch), (sub, 0, 0.9, t_sub), (wind, 1, 0.7, t_wind),
              (drone_lo, 1, 0.7, t_lo), (drone_hi, 1, 0.7, t_hi)]
    s.loop = (0, 0, 0)
    return s


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

    def vocal(self, name, words, notes, style=VS_CLEAR, singer=KAI, high=0, vibrato=0.45, volume=0.8):
        """notes: (MIDI note, start, length) in beats from the sound's start, one per syllable."""
        end = max(st + ln for _, st, ln in notes)
        self.samples.append(dict(kind="vocal", name=name, frames=int((end * 60.0 / self.bpm + 0.25) * RATE),
                                 words=words, vnotes=notes, style=style, singer=singer, high=high,
                                 vibrato=vibrato, volume=volume))
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
                if s["kind"] == "synth":
                    out.append("sample synth %d %s" % (s["frames"], s["name"]))
                    m = s["macros"]
                    out.append("synth %d %s %s 440 %s" % (s["note"], t(s["seconds"]), t(s["volume"]),
                                                          " ".join(t(x) for x in m)))
                    out.append("patch" + "".join(" %s=%s" % (key, int(v) if key in INT_FIELDS else t(v))
                                                 for key, v in sorted(s["patch"].items()) if v))
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
                    out.append("end")
                    while rest[k] != "end":
                        k += 1
                    k += 1
                    continue
            if ln.startswith("pad ") and not any(o.startswith("clip ") for o in out):
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


def roll(s, sound, track, at, beats, step, gain0=-14.0, gain1=0.0, pitches=None):
    """A drum roll: `sound` every `step` beats for `beats`, getting louder (and higher, with pitches)."""
    n = int(round(beats / step))
    for i in range(n):
        snd = pitches[min(len(pitches) - 1, i * len(pitches) // n)] if pitches else sound
        s.clip(snd, track, at + i * step, beats=step, gain=gain0 + (gain1 - gain0) * i / max(1, n - 1))


# ---------------------------------------------------------------- 4. breakcore

def breakcore():
    s = Song2("Breakcore: Static Bloom", 86)
    # A half-time intro at 86, the break at 172, four bars of 7/8, then a faster run to the end at 186.
    s.change(4, 172, 4, 4)
    s.change(20, 172, 7, 8)
    s.change(24, 186, 4, 4)
    kick = s.synth("Break kick", "Boom bap kick", macros=(0.6, 0.35, 0.6, 0.6, 0.5))
    snare = s.synth("Break snare", "Break snare", macros=(0.65, 0.4, 0.5, 0.65, 0.5))
    ghost = s.synth("Ghost snare", "Break snare", volume=0.35, macros=(0.4, 0.3, 0.4, 0.5, 0.5))
    hat = s.synth("Hat", "Trap hat", macros=(0.6, 0.4, 0.5, 0.5, 0.5))
    ohat = s.synth("Open hat", "Open hat")
    stutter = [snare] + [s.variant2("Snare +%d" % k, snare, k) for k in (3, 5, 7, 12)]
    pad = s.synth("Bloom pad", "Shimmer pad", seconds=6, macros=(0.45, 0.6, 0.7, 0.4, 0.55))
    reese = s.synth("Reese", "Reese", seconds=2, macros=(0.5, 0.45, 0.65, 0.7, 0.6))
    bell = s.synth("Glass bell", "Glass bell", seconds=3)
    chop = s.synth("Vocal chop", "Vocal chop")
    vow = s.synth("Robot vowels", "Robot vowels", seconds=1.2)
    zap = s.synth("Laser zap", "Laser zap")
    riser = s.synth("Riser", "Riser", seconds=5)
    impact = s.synth("Impact", "Impact")

    t_kick = s.track("Kick", 0.85, drive=0.25)
    t_snare = s.track("Snare", 0.75, eq=(0, 1, 2), reverb=0.12)
    t_hat = s.track("Hats", 0.5, pan=0.2, eq=(0, 0, 2))
    t_pad = s.track("Pad", 0.62, filt=1, cutoff=900, reverb=0.5, duck=0.6)
    t_bass = s.track("Reese", 0.6, filt=1, cutoff=900, reso=0.3, drive=0.3, duck=0.7)
    t_bell = s.track("Bell", 0.45, pan=-0.2, delay=0.4, reverb=0.4)
    t_vox = s.track("Chops", 0.55, pan=0.15, delay=0.3, reverb=0.25)
    t_fx = s.track("FX", 0.6, reverb=0.35)
    s.fx.update(div=3, fb=0.45, tone=0.55, dlevel=0.6, ping=1, size=0.8, damp=0.35, rlevel=0.8, sc=t_kick, release=120)
    s.dynamics = (1, -22.0, 4.0, 6.0, 110.0, 7.0, 1, -1.0)   # Loud

    # Ebm9 - Cbmaj7 - Gbmaj7 - Db6 in the pad, a bar each (pad notes relative to its C4)
    chords = [[51, 58, 61, 65], [47, 54, 58, 63], [54, 58, 61, 65], [49, 56, 58, 61]]
    roots = [27, 23, 30, 25]
    # the intro: pad chords opening up under a filter, glass bell over the top
    s.noteclip(pad, t_pad, 0, s.bar(4), [(n, i * 4, 4, 0.7) for i in range(4) for n in chords[i]])
    s.auto(t_pad, AUTO_CUTOFF, 0, lufs_cutoff(300))
    s.auto(t_pad, AUTO_CUTOFF, s.bar(4), lufs_cutoff(6000))
    s.auto(t_pad, AUTO_CUTOFF, s.bar(20), lufs_cutoff(6000))
    s.auto(t_pad, AUTO_CUTOFF, s.bar(24), lufs_cutoff(1800))
    s.noteclip(bell, t_bell, 0, s.bar(4), [(75, 0, 1, 0.7), (73, 2, 1, 0.6), (70, 4, 2, 0.7), (68, 8, 1, 0.6),
                                            (70, 10, 2, 0.6), (73, 12, 3, 0.7)])
    s.clip(riser, t_fx, s.bar(3), beats=4, fade_in=2)

    # the break: amen-style chops, a different one each bar, rolls at the end of every fourth
    breaks = ["K.K...S...KK..S.", "K.K...S..SK...S.", "K...K.S.K.K...Sg", "K.Kg..S..gK.S..S"]
    def bar_of_break(b, steps16=16, fast=False):
        at0, pat = s.bar(b), breaks[b % 4]
        for i in range(steps16):
            at = at0 + i * 0.25
            c = pat[i % 16]
            if c == "K":
                s.clip(kick, t_kick, at, beats=0.5)
            elif c == "S":
                s.clip(snare, t_snare, at, beats=0.5)
            elif c == "g":
                s.clip(ghost, t_snare, at, beats=0.25, gain=-6)
            if i % 2 == 0 or fast:
                s.clip(hat, t_hat, at, beats=0.25, gain=-3 if i % 4 else 0)
        if b % 4 == 3 and steps16 == 16:            # a stutter roll, rising, over the last beat
            roll(s, snare, t_snare, at0 + 3, 1, 0.125, -12, 0, stutter)
    for b in range(4, 20):
        bar_of_break(b)
    for b in range(20, 24):                          # 7/8: fourteen sixteenths a bar
        bar_of_break(b, 14)
    for b in range(24, 36):
        bar_of_break(b, fast=b >= 28)
        if b % 2 == 0:
            s.clip(ohat, t_hat, s.bar(b) + 1.5, beats=0.5, gain=-4)
    s.clip(impact, t_fx, s.bar(4))
    s.clip(impact, t_fx, s.bar(24))

    # the pad and reese carry the chords through the break
    for b in range(4, 36):
        i = (b - 4) % 4
        bars_q = s.bar(b + 1) - s.bar(b)
        s.noteclip(pad, t_pad, s.bar(b), bars_q, [(n, 0, bars_q, 0.55) for n in chords[i]])
        r = roots[i] + 12
        s.noteclip(reese, t_bass, s.bar(b), bars_q, [(r, 0, bars_q * 0.75, 0.8), (r + 12 if b % 2 else r, bars_q * 0.75,
                                                                                   bars_q * 0.25, 0.7)])
    s.auto(t_bass, AUTO_CUTOFF, s.bar(24), lufs_cutoff(500))
    for b in range(24, 36):                         # the bass opens and closes, bar by bar
        s.auto(t_bass, AUTO_CUTOFF, s.bar(b) + 1, lufs_cutoff(2600 if b % 2 else 1400))
        s.auto(t_bass, AUTO_CUTOFF, s.bar(b) + 3, lufs_cutoff(500))
    # chops and robot vowels answering over the 7/8 and the finale
    for b in list(range(20, 24)) + list(range(28, 36, 2)):
        s.clip(chop, t_vox, s.bar(b) + 1.5, beats=0.5)
        s.clip(chop, t_vox, s.bar(b) + 2.25, beats=0.5, gain=-3)
    for b in range(12, 20, 2):
        s.clip(vow, t_vox, s.bar(b) + 2, beats=2, gain=-4)
    for b in (8, 16, 26, 30, 34):
        s.clip(zap, t_fx, s.bar(b) + 3.5, gain=-6)
    s.clip(riser, t_fx, s.bar(22), beats=s.bar(24) - s.bar(22), fade_in=1)
    s.clip(pad, t_pad, s.bar(36), beats=6, fade_out=4)
    s.pads = [(kick, 0, 0.9, t_kick), (snare, 0, 0.9, t_snare), (ghost, 0, 0.8, t_snare), (hat, 0, 0.7, t_hat),
              (ohat, 0, 0.7, t_hat)] + [(x, 0, 0.8, t_snare) for x in stutter[1:]] + \
             [(chop, 0, 0.8, t_vox), (vow, 1, 0.8, t_vox), (zap, 0, 0.8, t_fx), (impact, 0, 0.9, t_fx),
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
    crash = s.synth("Crash", "Open hat", seconds=1.6, volume=0.75, macros=(0.6, 0.95, 0.8, 0.6, 0.5))
    bass = s.synth("Picked bass", "Synth bass", seconds=0.5, macros=(0.55, 0.4, 0.6, 0.65, 0.5))
    gtr = s.synth("Crunch guitar", patch=guitar_patch, note=52, seconds=2.2, volume=0.75)
    lead = s.synth("Lead guitar", patch=lead_patch, note=64, seconds=2.2, volume=0.75)

    t_kick = s.track("Kick", 0.85)
    t_snare = s.track("Snare", 0.8, reverb=0.25)
    t_hats = s.track("Hats", 0.45, pan=0.25, eq=(0, 0, 1))
    t_bass = s.track("Bass", 0.7, filt=1, cutoff=1600, drive=0.2)
    t_gl = s.track("Guitar L", 0.55, pan=-0.6, eq=(-2, 1, 0))
    t_gr = s.track("Guitar R", 0.55, pan=0.6, eq=(-2, 1, 0))
    t_lead = s.track("Lead guitar", 0.55, pan=0.1, delay=0.25, reverb=0.3)
    t_vox = s.track("Vocal", 0.85, delay=0.12, reverb=0.3)
    t_harm = s.track("Harmony", 0.55, pan=-0.25, reverb=0.4)
    t_cym = s.track("Cymbals", 0.45, pan=-0.2, reverb=0.2)
    s.fx.update(div=4, fb=0.3, tone=0.5, dlevel=0.5, ping=0, size=0.65, damp=0.45, rlevel=0.7)
    s.dynamics = (1, -18.0, 2.5, 15.0, 180.0, 3.0, 1, -1.0)   # Glue

    prog = [40, 36, 43, 38]                          # E2, C2, G2, D2: the roots
    def power(root):                                 # root, fifth, octave, an octave up for the guitars
        r = root + 12
        return [r, r + 7, r + 12]
    # the drums: kick on 1 and 3 (and the "and" of 3), snare on 2 and 4, eighths on the hats
    for b in range(32):
        at = s.bar(b)
        chorus = 12 <= b < 20 or 24 <= b < 32
        for k in (0, 2, 2.5) if b % 2 else (0, 1.5, 2):
            s.clip(kick, t_kick, at + k, beats=0.5)
        for k in (1, 3):
            s.clip(snare, t_snare, at + k, beats=1)
        for i in range(8):
            if chorus and i % 2 == 1:
                s.clip(ohat, t_hats, at + i * 0.5, beats=0.5, gain=-4)
            else:
                s.clip(hat, t_hats, at + i * 0.5, beats=0.5, gain=0 if i % 2 == 0 else -5)
        if b % 4 == 0 and b:
            s.clip(crash, t_cym, at)
        if b in (11, 19, 23, 31):                    # a fill into the next part
            roll(s, snare, t_snare, at + 3, 1, 0.25, -6, 0)
    s.clip(crash, t_cym, s.bar(32))
    s.clip(kick, t_kick, s.bar(32))
    # bass and guitars, as note clips: chugging eighths in the verse, open chords in the chorus
    for b in range(32):
        root = prog[b % 4]
        chorus = 12 <= b < 20 or 24 <= b < 32
        s.noteclip(bass, t_bass, s.bar(b), 4, [(root, i * 0.5, 0.45, 0.9 if i % 2 == 0 else 0.7) for i in range(8)])
        for tr, shift in ((t_gl, 0), (t_gr, 0.01)):
            if chorus or b < 4:
                notes = [(n, shift, 3.9, 0.85) for n in power(root)]
            else:                                    # palm-muted: short and tight
                notes = [(n, i * 0.5 + shift, 0.3, 0.8 if i % 2 == 0 else 0.6) for i in range(8) for n in power(root)[:2]]
            s.noteclip(gtr, tr, s.bar(b), 4, notes)
    s.auto(t_gl, AUTO_VOLUME, 0, 0.0)               # the guitars swell in at the start
    s.auto(t_gl, AUTO_VOLUME, s.bar(2), 0.37)
    s.auto(t_gr, AUTO_VOLUME, 0, 0.0)
    s.auto(t_gr, AUTO_VOLUME, s.bar(2), 0.37)
    # the solo, bars 20-23
    solo = [(64, 0, 1.5), (67, 1.5, 0.5), (69, 2, 1), (71, 3, 1), (74, 4, 2), (71, 6, 1), (69, 7, 1),
            (67, 8, 1.5), (64, 9.5, 0.5), (67, 10, 1), (69, 11, 1), (71, 12, 1), (69, 13, 0.5), (67, 13.5, 0.5),
            (66, 14, 1), (64, 15, 1)]
    s.noteclip(lead, t_lead, s.bar(20), 16, [(n, st, ln, 0.85) for n, st, ln in solo])

    # the vocals: Kai sings; Aria joins a third above in the chorus
    verse_words = ("Head-lights burn-ing on the road to-night. Ci-ty fad-ing in the rear-view light. "
                   "En-gine roar-ing as the stars come out. No-thing left to lose and no-thing left to doubt.")
    r9 = [(0, 0.5), (0.5, 0.5), (1, 0.5), (1.5, 0.5), (2, 0.5), (2.5, 0.5), (3, 1), (4, 0.5), (4.5, 2.5)]
    r11 = [(0, 0.5), (0.5, 0.5), (1, 0.5), (1.5, 0.5), (2, 1), (3, 0.5), (3.5, 0.5), (4, 0.5), (4.5, 0.5),
           (5, 0.5), (5.5, 2)]
    lines = [([52, 52, 55, 55, 57, 55, 59, 57, 55], r9), ([59, 59, 57, 55, 54, 52, 55, 54, 52], r9),
             ([52, 55, 57, 59, 57, 55, 62, 59, 57], r9), ([59, 59, 57, 55, 57, 55, 54, 54, 52, 50, 52], r11)]
    verse = []
    for li, (pitches, rhythm) in enumerate(lines):
        verse += [(p, li * 8 + st, ln * 0.95) for p, (st, ln) in zip(pitches, rhythm)]
    chorus_words = "Hold on, we're run-ning to the light. Hold on, we're a-live to-night. " * 2
    ca = [(62, 0, 1), (64, 1, 1), (62, 2.5, 0.5), (64, 3, 0.5), (67, 3.5, 0.5), (66, 4, 0.5), (64, 4.5, 0.5), (64, 5, 2.5)]
    cb = [(62, 0, 1), (64, 1, 1), (62, 2.5, 0.5), (59, 3, 0.5), (62, 3.5, 1.5), (59, 5, 0.5), (64, 5.5, 2.5)]
    chorus = []
    for rep in range(2):
        chorus += [(n, rep * 16 + st, ln * 0.95) for n, st, ln in ca]
        chorus += [(n, rep * 16 + 8 + st, ln * 0.95) for n, st, ln in cb]
    E_MINOR = [52, 54, 55, 57, 59, 60, 62, 64, 66, 67, 69, 71, 72, 74, 76, 78, 79, 81, 83, 84]
    third = lambda n: E_MINOR[E_MINOR.index(n) + 2] if n in E_MINOR else n + 3
    harmony = [(third(n) + 12, st, ln) for n, st, ln in chorus]
    v = s.vocal("Sung: verse", verse_words, verse, singer=KAI)
    c = s.vocal("Sung: chorus", chorus_words, chorus, singer=KAI)
    h = s.vocal("Sung: chorus harmony", chorus_words, harmony, singer=AIRA, high=1, vibrato=0.35)
    s.clip(v, t_vox, s.bar(4))
    s.clip(c, t_vox, s.bar(12))
    s.clip(h, t_harm, s.bar(12))
    s.clip(c, t_vox, s.bar(24))
    s.clip(h, t_harm, s.bar(24))
    s.pads = [(kick, 0, 0.9, t_kick), (snare, 0, 0.9, t_snare), (hat, 0, 0.7, t_hats), (ohat, 0, 0.7, t_hats),
              (crash, 0, 0.8, t_cym)] + [(bass, 0, 0.9, t_bass)] + [(gtr, 1, 0.8, t_gl), (lead, 1, 0.8, t_lead)] + \
             [(v, 0, 0.8, t_vox), (c, 0, 0.8, t_vox), (h, 0, 0.7, t_harm)]
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
    hats = [hat] + [s.variant2("Hat +%d" % k, hat, k) for k in (2, 5, 7)]
    ohat = s.synth("Open hat", "Open hat", seconds=0.4)
    bell = s.synth("Dark bell", "Dark bell", seconds=1.6)
    pad = s.synth("Night pad", "Silk pad", seconds=4, macros=(0.4, 0.6, 0.6, 0.4, 0.5))
    riser = s.synth("Riser", "Riser", seconds=4)
    impact = s.synth("Impact", "Impact")
    tag = s.vocal("Sung: tag", "Mid-night, hol-low. Let's go!", [(65, 0, 0.5), (63, 0.5, 1), (60, 2, 0.5), (58, 2.5, 1.5),
                                                                       (65, 5, 0.5), (68, 5.5, 1.5)],
                  style=VS_TALKBOX, high=0, vibrato=0.2)

    t_808 = s.track("808", 0.75, drive=0.25)
    t_kick = s.track("Kick", 0.8)
    t_clap = s.track("Clap", 0.7, reverb=0.25)
    t_hats = s.track("Hats", 0.5, pan=0.15, eq=(0, 0, 2))
    t_bell = s.track("Bell", 0.55, pan=-0.15, filt=1, cutoff=4000, delay=0.35, reverb=0.4, duck=0.3)
    t_pad = s.track("Pad", 0.5, filt=1, cutoff=1200, reverb=0.5, duck=0.5)
    t_vox = s.track("Tag", 0.7, delay=0.4, reverb=0.35)
    t_fx = s.track("FX", 0.55, reverb=0.4)
    s.fx.update(div=4, fb=0.4, tone=0.5, dlevel=0.6, ping=1, size=0.7, damp=0.5, rlevel=0.7, sc=t_kick, release=200)
    s.dynamics = (1, -22.0, 4.0, 8.0, 120.0, 7.0, 1, -1.0)   # Loud

    chords = [[53, 56, 60], [49, 53, 56], [48, 51, 56], [51, 55, 58]]
    roots = [29, 25, 32, 27]                         # F1, Db1, Ab1, Eb1
    melody = [(72, 0, 0.5), (75, 0.75, 0.5), (77, 1.5, 1), (75, 3, 0.5), (72, 4, 0.5), (70, 4.75, 0.5),
              (72, 5.5, 1.5), (68, 8, 0.5), (70, 8.75, 0.5), (72, 9.5, 1), (75, 11, 0.5), (77, 12, 1.5),
              (75, 13.75, 0.5), (72, 14.5, 1.5)]
    drops = list(range(4, 12)) + list(range(16, 28))
    for b in range(30):                              # pad chords all the way through
        s.noteclip(pad, t_pad, s.bar(b), 4, [(n, 0, 4, 0.6) for n in chords[b % 4]])
    s.auto(t_pad, AUTO_CUTOFF, 0, lufs_cutoff(250))   # the intro opens up
    s.auto(t_pad, AUTO_CUTOFF, s.bar(4), lufs_cutoff(2400))
    s.auto(t_bell, AUTO_CUTOFF, 0, lufs_cutoff(400))
    s.auto(t_bell, AUTO_CUTOFF, s.bar(4), lufs_cutoff(9000))
    for start in (0, 8, 16, 24):                     # the bell melody, every four bars
        s.noteclip(bell, t_bell, s.bar(start), 16, [(n, st, ln, 0.8) for n, st, ln in melody])
    s.clip(riser, t_fx, s.bar(2), beats=8, fade_in=2)
    s.clip(riser, t_fx, s.bar(14), beats=8, fade_in=2)
    for b in (4, 16):
        s.clip(impact, t_fx, s.bar(b))
    hat_bars = {}
    for b in drops:
        at = s.bar(b)
        r = roots[b % 4]
        # the 808: on the one, an answer on the "and" of two, sliding up an octave at every fourth bar's end
        notes = [(r, 0, 1.5, 0.95), (r, 1.75, 0.75, 0.8), (r + 7, 3, 0.5, 0.8)]
        if b % 4 == 3:
            notes[-1] = (r + 12, 3, 1, 0.9)
        s.noteclip(b808, t_808, at, 4, notes, root=36)
        s.clip(kick, t_kick, at, beats=0.5)
        s.clip(kick, t_kick, at + 1.75, beats=0.5, gain=-4)
        s.clip(clap, t_clap, at + 2, beats=1)
        # hats: eighths, with rolls (sixteenths, thirty-seconds, triplets) to finish phrases
        style = b % 4
        for i in range(8):
            t = at + i * 0.5
            if style == 1 and i == 5:
                roll(s, hat, t_hats, t, 0.5, 0.125, -8, -2)
            elif style == 3 and i >= 6:
                roll(s, hat, t_hats, t, 0.5, 1 / 6.0, -9, 0, hats)
            elif style == 2 and i == 3:
                roll(s, hat, t_hats, t, 0.5, 0.25, -4, -1)
            else:
                s.clip(hat, t_hats, t, beats=0.5, gain=0 if i % 2 == 0 else -4)
        if b >= 24:                                  # the last drop doubles up
            roll(s, hat, t_hats, at + 3, 1, 0.125, -10, 0, hats)
        if b % 2 == 1:
            s.clip(ohat, t_hats, at + 3.5, beats=0.5, gain=-6)
    # the break: no drums, the tag twice, the 808 holding the root under it
    s.clip(tag, t_vox, s.bar(12))
    s.clip(tag, t_vox, s.bar(14), gain=-2)
    for b in range(12, 16):
        s.noteclip(b808, t_808, s.bar(b), 4, [(roots[b % 4], 0, 3.5, 0.6)], root=36)
    s.clip(tag, t_vox, s.bar(28))
    s.clip(pad, t_pad, s.bar(30), beats=4, fade_out=3)
    s.pads = [(kick, 0, 0.9, t_kick), (clap, 0, 0.9, t_clap)] + [(h, 0, 0.7, t_hats) for h in hats] + \
             [(ohat, 0, 0.7, t_hats), (b808, 0, 0.9, t_808), (bell, 0, 0.8, t_bell), (pad, 1, 0.6, t_pad),
              (riser, 0, 0.8, t_fx), (impact, 0, 0.9, t_fx), (tag, 0, 0.8, t_vox)]
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
