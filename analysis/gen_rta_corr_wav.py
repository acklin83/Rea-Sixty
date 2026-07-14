#!/usr/bin/env python3
"""Generate the RTA and correlation test signals for the UF1 meter decode.

Companion to gen_vu_sweep_wav.py (which covers the Analogue screen). Same idea:
hold each condition long enough that the meter settles, so the USB frames pair
exactly with a known input.

  rta_bands  — a sine at each of the 31 ISO third-octave centres, one at a time,
               at a fixed level. Maps band index -> byte offset inside 0x0122.
               (The level->value mapping for a single band comes free by
               replaying vu_sweep.wav on the RTA screen: it's a 1 kHz stepper.)
  corr       — L/R noise with a known correlation coefficient, stepped
               +1 -> 0 -> -1. Maps correlation -> the Overview correlation
               element. Built as L = A, R = c*A + sqrt(1-c^2)*B with A,B
               independent and equal-variance, which yields exactly
               corr(L,R) = c.
  dbfs       — 1 kHz stepped over the full dBFS range (0 .. -90). vu_sweep.wav
               only spans -13..-48 dBFS (it's scaled for the VU faceplate), but
               the Overview bargraphs run to 0 dBFS and the RTA scale to -120,
               so their top end is missing from cap88/cap89. Two variants:
                 --lr    L and R opposite (two sweeps at once) — Overview bars
                 --mono  L = R — RTA, whose spectrum is not per-channel, so an
                         opposite sweep would be ambiguous

Usage: gen_rta_corr_wav.py rta|corr|dbfs <out.wav> [--lr|--mono]
"""
import math
import random
import struct
import sys
import wave

SR = 48000
FADE = 0.005

# ISO 266 third-octave centres — the 31 bands an RTA of this class shows.
ISO_BANDS = [20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315,
             400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150,
             4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000]

RTA_LEVEL_DBFS = -20.0   # comfortably inside the RTA's 0..-120 dB scale
RTA_STEP_SECS = 2.0

CORR_STEPS = [1.0, 0.75, 0.5, 0.25, 0.0, -0.25, -0.5, -0.75, -1.0]
CORR_LEVEL_DBFS = -20.0
CORR_STEP_SECS = 3.0     # noise needs longer than a tone to average out


def amp(db):
    return 10.0 ** (db / 20.0)


def env_at(k, n):
    if k < SR * FADE:
        return k / (SR * FADE)
    if k > n - SR * FADE:
        return max(0.0, (n - k) / (SR * FADE))
    return 1.0


def gen_rta(path):
    a = amp(RTA_LEVEL_DBFS)
    n = int(SR * RTA_STEP_SECS)
    data = bytearray()
    for f in ISO_BANDS:
        phase, dphase = 0.0, 2 * math.pi * f / SR
        for k in range(n):
            s = math.sin(phase) * env_at(k, n) * a
            phase += dphase
            if phase > 2 * math.pi:
                phase -= 2 * math.pi
            v = int(s * 32767)
            data += struct.pack("<hh", v, v)   # mono-in-stereo: both channels equal
    write(path, data)
    print(f"wrote {path}: {len(ISO_BANDS)} bands x {RTA_STEP_SECS}s "
          f"= {len(ISO_BANDS)*RTA_STEP_SECS:.0f}s @ {RTA_LEVEL_DBFS} dBFS")
    print("\nband schedule (t = seconds from playback start):")
    for i, f in enumerate(ISO_BANDS):
        print(f"  t={i*RTA_STEP_SECS:6.1f}  band[{i:2d}] = {f:>7.1f} Hz")


def gen_corr(path):
    a = amp(CORR_LEVEL_DBFS)
    n = int(SR * CORR_STEP_SECS)
    rng = random.Random(20260714)   # fixed seed: the capture stays reproducible
    data = bytearray()
    for c in CORR_STEPS:
        k2 = math.sqrt(max(0.0, 1.0 - c * c))
        for k in range(n):
            # Independent, equal-variance noise sources.
            A = rng.gauss(0.0, 0.25)
            B = rng.gauss(0.0, 0.25)
            e = env_at(k, n) * a
            L = A * e
            R = (c * A + k2 * B) * e
            data += struct.pack("<hh",
                                max(-32767, min(32767, int(L * 32767))),
                                max(-32767, min(32767, int(R * 32767))))
    write(path, data)
    print(f"wrote {path}: {len(CORR_STEPS)} steps x {CORR_STEP_SECS}s "
          f"= {len(CORR_STEPS)*CORR_STEP_SECS:.0f}s @ {CORR_LEVEL_DBFS} dBFS")
    print("\ncorrelation schedule (t = seconds from playback start):")
    for i, c in enumerate(CORR_STEPS):
        print(f"  t={i*CORR_STEP_SECS:6.1f}  corr = {c:+.2f}")


DBFS_HI, DBFS_LO, DBFS_STEP_DB = 0, -90, 3
DBFS_STEP_SECS = 2.5
DBFS_FREQ = 1000.0


def gen_dbfs(path, mono):
    levels = list(range(DBFS_HI, DBFS_LO - 1, -DBFS_STEP_DB))
    n = int(SR * DBFS_STEP_SECS)
    data = bytearray()
    phase, dphase = 0.0, 2 * math.pi * DBFS_FREQ / SR
    for i, dl in enumerate(levels):
        aL = amp(dl)
        aR = aL if mono else amp(levels[len(levels) - 1 - i])
        for k in range(n):
            s = math.sin(phase) * env_at(k, n)
            phase += dphase
            if phase > 2 * math.pi:
                phase -= 2 * math.pi
            data += struct.pack("<hh", int(s * aL * 32767), int(s * aR * 32767))
    write(path, data)
    kind = "mono (L=R)" if mono else "opposite L/R"
    print(f"wrote {path}: {len(levels)} steps x {DBFS_STEP_SECS}s "
          f"= {len(levels)*DBFS_STEP_SECS:.0f}s, {kind}, {DBFS_FREQ:.0f} Hz")
    print(f"  L: {levels[0]} -> {levels[-1]} dBFS in {DBFS_STEP_DB} dB steps")
    if not mono:
        print(f"  R: {levels[-1]} -> {levels[0]} dBFS (opposite)")
    print("\nstep schedule (t = seconds from playback start):")
    for i, dl in enumerate(levels):
        r = dl if mono else levels[len(levels) - 1 - i]
        print(f"  t={i*DBFS_STEP_SECS:6.1f}  L={dl:+4d} dBFS  R={r:+4d} dBFS")


def write(path, data):
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(bytes(data))


def main():
    mode, out = sys.argv[1], sys.argv[2]
    if mode == "rta":
        gen_rta(out)
    elif mode == "corr":
        gen_corr(out)
    elif mode == "dbfs":
        if "--mono" not in sys.argv and "--lr" not in sys.argv:
            sys.exit("dbfs needs --mono or --lr")
        gen_dbfs(out, mono="--mono" in sys.argv)
    else:
        sys.exit("mode must be rta|corr|dbfs")


if __name__ == "__main__":
    main()
