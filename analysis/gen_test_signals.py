#!/usr/bin/env python3
"""Generate calibrated test-audio WAVs for decoding meter/display content.

These drive the "isolate each meter with a crafted test signal" capture method
from docs/uf1-decoding-research.md ("Decoding the 4.3" display / SSL Meter").
Each file is built to make exactly one on-screen element move so the
correlating bytes in a USBPcap diff stand out. The same signals are useful for
re-verifying the already-decoded UF8 VU / UC1 VU+GR meters.

Pure stdlib (wave/struct/math/random) — no numpy. Runs on a stock Mac/PC
Python 3 so you can generate the kit on the capture machine itself.

Usage:
    python3 analysis/gen_test_signals.py [--out DIR] [--rate 48000] [--depth 24]

Default output: captures/test-signals/ (git-ignored; regenerate any time).
A MANIFEST.txt is written alongside mapping each file to the meter it probes.

dBFS note: levels are PEAK amplitudes of a sine (amp = 10**(dB/20)). The RMS of
a sine is 3.01 dB below its peak — keep that in mind when reading an RMS meter
vs a peak meter.
"""

import argparse
import math
import random
import struct
import wave
from pathlib import Path

# ---------------------------------------------------------------------------
# Low-level WAV writing (stdlib only)
# ---------------------------------------------------------------------------


def _pack_sample(x, depth):
    """Clip float x in [-1, 1] and return little-endian PCM bytes."""
    if x > 1.0:
        x = 1.0
    elif x < -1.0:
        x = -1.0
    if depth == 16:
        return struct.pack("<h", int(round(x * 32767.0)))
    if depth == 24:
        v = int(round(x * 8388607.0))
        return v.to_bytes(3, "little", signed=True)
    raise ValueError("depth must be 16 or 24")


def write_wav(path, left, right, rate, depth):
    """Write interleaved stereo float channels (lists) to a PCM WAV."""
    frames = bytearray()
    for l, r in zip(left, right):
        frames += _pack_sample(l, depth)
        frames += _pack_sample(r, depth)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(depth // 8)
        w.setframerate(rate)
        w.writeframes(bytes(frames))


# ---------------------------------------------------------------------------
# Signal generators (return lists of floats)
# ---------------------------------------------------------------------------


def db2amp(db):
    return 10.0 ** (db / 20.0)


def silence(dur, rate):
    return [0.0] * int(dur * rate)


def sine(freq, dur, rate, amp=1.0, phase=0.0):
    n = int(dur * rate)
    w = 2.0 * math.pi * freq / rate
    return [amp * math.sin(w * i + phase) for i in range(n)]


def log_sweep(f0, f1, dur, rate, amp=1.0):
    """Exponential (constant-octaves-per-second) sine sweep f0 -> f1."""
    n = int(dur * rate)
    out = [0.0] * n
    k = math.log(f1 / f0)
    phase = 0.0
    for i in range(n):
        t = i / n
        f = f0 * math.exp(k * t)
        phase += 2.0 * math.pi * f / rate
        out[i] = amp * math.sin(phase)
    return out


def white(dur, rate, amp=1.0, seed=None):
    rng = random.Random(seed)
    return [amp * (2.0 * rng.random() - 1.0) for _ in range(int(dur * rate))]


def pink(dur, rate, target_rms_db=-12.0, seed=1):
    """Paul Kellet's economy pink-noise filter, scaled to a target RMS dBFS."""
    rng = random.Random(seed)
    n = int(dur * rate)
    b0 = b1 = b2 = b3 = b4 = b5 = b6 = 0.0
    raw = [0.0] * n
    for i in range(n):
        wn = 2.0 * rng.random() - 1.0
        b0 = 0.99886 * b0 + wn * 0.0555179
        b1 = 0.99332 * b1 + wn * 0.0750759
        b2 = 0.96900 * b2 + wn * 0.1538520
        b3 = 0.86650 * b3 + wn * 0.3104856
        b4 = 0.55000 * b4 + wn * 0.5329522
        b5 = -0.7616 * b5 - wn * 0.0168980
        raw[i] = b0 + b1 + b2 + b3 + b4 + b5 + b6 + wn * 0.5362
        b6 = wn * 0.115926
    # Scale to target RMS.
    rms = math.sqrt(sum(s * s for s in raw) / max(1, n))
    if rms > 0:
        g = db2amp(target_rms_db) / rms
        raw = [s * g for s in raw]
    return raw


def equal_power_pan(mono, pan_start, pan_end):
    """Pan a mono list from pan_start..pan_end (0=L, 1=R), constant power."""
    n = len(mono)
    left = [0.0] * n
    right = [0.0] * n
    for i, s in enumerate(mono):
        p = pan_start + (pan_end - pan_start) * (i / max(1, n - 1))
        ang = p * (math.pi / 2.0)
        left[i] = s * math.cos(ang)
        right[i] = s * math.sin(ang)
    return left, right


# ---------------------------------------------------------------------------
# File kit
# ---------------------------------------------------------------------------


def build_kit(out, rate, depth):
    out.mkdir(parents=True, exist_ok=True)
    manifest = []

    def emit(name, left, right, purpose):
        write_wav(out / name, left, right, rate, depth)
        manifest.append((name, purpose))
        print(f"  {name:<34} {purpose}")

    print("Peak/RMS bargraph + VU needle + fader-dB readout:")
    # Stepped level sequence (single capture, time-correlate each plateau).
    seq_l, seq_r = [], []
    for db in (-40, -20, -12, -6, -3, 0):
        tone = sine(1000, 3.0, rate, db2amp(db))
        seq_l += tone
        seq_r += tone
        seq_l += silence(1.0, rate)
        seq_r += silence(1.0, rate)
    emit("level_steps_1k.wav", seq_l, seq_r,
         "sine 1k stepping -40/-20/-12/-6/-3/0 dBFS, 3s each + 1s gap")
    # Isolated steady tones for clean single-level diffs.
    for db in (-20, -12, -6, 0):
        t = sine(1000, 4.0, rate, db2amp(db))
        emit(f"tone_{db:+03d}dBFS_1k.wav", t, t, f"steady 1k sine at {db:+d} dBFS, 4s")

    print("31-band RTA:")
    sw = log_sweep(20.0, 20000.0, 30.0, rate, db2amp(-12))
    emit("sweep_log_20-20k_30s.wav", sw, sw,
         "slow log sweep 20Hz->20kHz, 30s — one band walks the array index")
    pk = pink(10.0, rate, target_rms_db=-12.0)
    emit("pink_-12dBFS_10s.wav", pk, pk, "pink noise ~-12 dBFS RMS, 10s — all RTA bands lit")
    # Discrete octave-centre tones as an alternative band probe.
    steps_l = []
    for f in (31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000):
        steps_l += sine(f, 2.0, rate, db2amp(-12))
        steps_l += silence(0.7, rate)
    emit("freq_steps_octaves.wav", steps_l, list(steps_l),
         "octave-centre tones 31.5Hz..16kHz, 2s each + gap")

    print("Phase correlation meter:")
    nz = white(4.0, rate, db2amp(-12), seed=7)
    emit("phase_mono_corr+1.wav", nz, list(nz), "identical L/R noise -> correlation +1")
    emit("phase_inverted_corr-1.wav", nz, [-s for s in nz], "L = -R -> correlation -1")
    emit("phase_uncorrelated_corr0.wav",
         white(4.0, rate, db2amp(-12), seed=11),
         white(4.0, rate, db2amp(-12), seed=29),
         "independent L/R noise -> correlation ~0")

    print("L/R balance meter:")
    t1k = sine(1000, 4.0, rate, db2amp(-12))
    emit("balance_hardL.wav", t1k, silence(4.0, rate), "1k tone on L only")
    emit("balance_hardR.wav", silence(4.0, rate), t1k, "1k tone on R only")
    bl, br = equal_power_pan(sine(1000, 10.0, rate, db2amp(-12)), 0.0, 1.0)
    emit("balance_sweep_L-to-R.wav", bl, br, "equal-power pan sweep L->R over 10s")

    print("Lissajous / vector scope:")
    s1k = sine(1000, 4.0, rate, db2amp(-12))
    emit("lissajous_mono_line45.wav", s1k, list(s1k), "L==R -> 45deg diagonal line")
    emit("lissajous_L_horizontal.wav", s1k, silence(4.0, rate), "L only -> horizontal line")
    emit("lissajous_R_vertical.wav", silence(4.0, rate), s1k, "R only -> vertical line")
    cos1k = sine(1000, 4.0, rate, db2amp(-12), phase=math.pi / 2.0)
    emit("lissajous_circle.wav", s1k, cos1k, "L sin / R cos (90deg) -> circle")
    emit("lissajous_fig8.wav", sine(1000, 4.0, rate, db2amp(-12)),
         sine(2000, 4.0, rate, db2amp(-12)), "L f / R 2f -> figure-eight")

    (out / "MANIFEST.txt").write_text(
        f"# Test-signal kit — {rate} Hz, {depth}-bit PCM\n"
        "# Generated by analysis/gen_test_signals.py\n"
        "# See docs/uf1-decoding-research.md, 'Decoding the 4.3\" display / SSL Meter'\n\n"
        + "".join(f"{n:<34} {p}\n" for n, p in manifest)
    )
    print(f"\n{len(manifest)} files + MANIFEST.txt written to {out}/")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="captures/test-signals", help="output directory")
    ap.add_argument("--rate", type=int, default=48000, help="sample rate (Hz)")
    ap.add_argument("--depth", type=int, default=24, choices=(16, 24), help="bit depth")
    args = ap.parse_args()
    build_kit(Path(args.out), args.rate, args.depth)


if __name__ == "__main__":
    main()
