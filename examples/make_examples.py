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
        self.bf = round(60.0 / bpm * RATE)          # frames per beat, as tinydaw rounds it

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
        for c in sorted(self.clips, key=lambda c: (c[2], c[1])):
            out.append("clip %d %d %d %d %d %s %d %d" % (c[0], c[1], c[2], c[3], c[4], t(c[5]), c[6], c[7]))
        for i, p in enumerate(self.pads[:16]):
            out.append("pad %d %d %d %s 0 %d 0" % (i, p[0], p[1], t(p[2]), p[3]))
        with open(path, "w") as fh:
            fh.write("\n".join(out) + "\n")
        secs = max(c[2] + c[4] for c in self.clips) / RATE
        print("%-40s %3d sounds %4d clips %5.1f s" % (os.path.basename(path), len(self.samples), len(self.clips), secs))


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


if __name__ == "__main__":
    for name, song in (("house-late-train", house()), ("hiphop-corner-store", hiphop()),
                       ("experimental-glass-weather", experimental())):
        song.write(os.path.join(HERE, name + ".tdaw"))
