#!/usr/bin/env python3
"""ONE probe signal that answers every open UF1 meter question in one run.

Written 2026-07-15 after Frank's hardware test found four faults at once and the
existing per-question captures (cap87 music, cap97 chunk-fragments) could settle
none of them. Play this ONCE on the Overview screen and ONCE on the Analogue
screen with REASIXTY_T10_DUMP=1; that is the whole experiment.

Why the sections are what they are:

  A goniometer is a rotated (L,R) scatter: X = (R-L)/sqrt(2), Y = (R+L)/sqrt(2).
  So a known correlation draws a known SHAPE, and the shape is what identifies
  the raster geometry — not arithmetic on the array length.

    corr +1  (L=R)        -> X=0        -> a VERTICAL line   = every row's CENTRE
    corr  0               -> a round scatter cloud           = the whole face
    corr -1  (L=-R)       -> Y=0        -> a HORIZONTAL line = the widest ROW
    L only   (R=0)        -> a 45 deg diagonal               = one diamond EDGE
    R only   (L=0)        -> the other diagonal              = the other EDGE

  Centres + the centre row + both edges pin the geometry down completely, with no
  guessing about what the length "must" factor into.

  SILENCE between sections is mandatory, not padding: the plug-in's Lissajous has
  a "Fade 2 sec" persistence (the raster is ~50% lit at any instant and only ~120
  cells change per frame), so without a gap each section would still contain the
  previous one's trace. 3 s clears it.

  Noise, not tones: a tone at corr +1 would only ever light TWO points on the
  vertical line (its +/- peak). Noise sweeps the whole line, so every row gets a
  lit cell in one section.

Sections (timeline printed on stdout, and each section is announced by its own
start time so the dump can be segmented):
   1  silence            3 s   fade clears / baseline
   2  corr +1            4 s   -> row CENTRES
   3  silence            3 s
   4  corr  0            4 s   -> full face extent
   5  silence            3 s
   6  corr -1            4 s   -> the widest row
   7  silence            3 s
   8  L only             3 s   -> diamond edge
   9  silence            3 s
  10  R only             3 s   -> other edge
  11  silence            3 s
  12  1 kHz -30 dBFS     3 s   -> Analogue VU/PPM + TextVuPpm(12 floats)
  13  1 kHz -20 dBFS     3 s
  14  1 kHz -10 dBFS     3 s
  15  silence            2 s
  16  1 kHz 0 dBFS CLIP  3 s   -> f5 OverloadValues = 1, the red LED
   -  silence            3 s   tail

Usage: gen_uf1_meter_probe_wav.py [out.wav]      (default ~/Desktop/uf1_meter_probe.wav)
"""
import math
import os
import random
import struct
import sys
import wave

SR = 48000
FADE = 0.005          # anti-click ramp on each section edge


def amp(db):
    return 10.0 ** (db / 20.0)


def env_at(k, n):
    f = int(SR * FADE)
    if k < f:
        return k / f
    if k > n - f:
        return max(0.0, (n - k) / f)
    return 1.0


def sect(data, secs, fn, label, timeline):
    t0 = len(data) / SR          # data holds FRAMES, not samples — do not /2
    n = int(SR * secs)
    for k in range(n):
        l, r = fn(k, n)
        e = env_at(k, n)
        data.append((max(-1.0, min(1.0, l * e)), max(-1.0, min(1.0, r * e))))
    timeline.append((t0, t0 + secs, label))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else \
        os.path.expanduser("~/Desktop/uf1_meter_probe.wav")
    random.seed(1)                     # reproducible: same file every time
    data = []
    tl = []
    a = amp(-20.0)

    def silence(k, n):
        return 0.0, 0.0

    def corr(c):
        def f(k, n):
            # L = A, R = c*A + sqrt(1-c^2)*B  =>  corr(L,R) = c exactly
            A = random.gauss(0, 0.3)
            B = random.gauss(0, 0.3)
            return a * A, a * (c * A + math.sqrt(max(0.0, 1 - c * c)) * B)
        return f

    def only(left):
        def f(k, n):
            v = a * random.gauss(0, 0.3)
            return (v, 0.0) if left else (0.0, v)
        return f

    def tone(db):
        g = amp(db)
        def f(k, n):
            v = g * math.sin(2 * math.pi * 1000.0 * k / SR)
            return v, v
        return f

    sect(data, 3, silence,    "silence",            tl)
    sect(data, 4, corr(1.0),  "corr +1  -> row CENTRES (vertical line)", tl)
    sect(data, 3, silence,    "silence",            tl)
    sect(data, 4, corr(0.0),  "corr  0  -> full face extent",            tl)
    sect(data, 3, silence,    "silence",            tl)
    sect(data, 4, corr(-1.0), "corr -1  -> widest ROW (horizontal line)", tl)
    sect(data, 3, silence,    "silence",            tl)
    sect(data, 3, only(True), "L only   -> diamond EDGE",                tl)
    sect(data, 3, silence,    "silence",            tl)
    sect(data, 3, only(False),"R only   -> other EDGE",                  tl)
    sect(data, 3, silence,    "silence",            tl)
    sect(data, 3, tone(-30),  "1 kHz -30 dBFS -> VU/PPM",                tl)
    sect(data, 3, tone(-20),  "1 kHz -20 dBFS -> VU/PPM",                tl)
    sect(data, 3, tone(-10),  "1 kHz -10 dBFS -> VU/PPM",                tl)
    sect(data, 2, silence,    "silence",            tl)
    sect(data, 3, tone(0),    "1 kHz 0 dBFS CLIP -> f5 OverloadValues",  tl)
    sect(data, 3, silence,    "silence (tail)",     tl)

    frames = bytearray()
    for l, r in data:
        frames += struct.pack("<hh", int(l * 32767), int(r * 32767))
    with wave.open(out, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(bytes(frames))

    print(f"wrote {out}  ({len(data)/SR:.1f} s)\n")
    print(f"{'start':>7} {'end':>7}  section")
    for t0, t1, lab in tl:
        print(f"{t0:>7.1f} {t1:>7.1f}  {lab}")


if __name__ == "__main__":
    main()
