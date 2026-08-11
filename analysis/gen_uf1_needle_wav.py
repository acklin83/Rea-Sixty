#!/usr/bin/env python3
"""ONE signal that settles the analogue needle — scale AND ballistic.

Written 2026-08-11. The 2026-08-11 probe run used MUSIC, and music cannot answer
either question: a 38 dB drop in 84 ms is either "this source carries no PPM
fallback" or "the tune stopped", and nothing in the data tells the two apart.
With a known signal both fall out of the same run.

  Steady tones at the three landmarks of IEC 60268-10 Type II, against the
  screen's -18 dBFS reference:
      -18 dBFS  = PPM mark 4  (the alignment level, needle mid-dial)
      -10 dBFS  = PPM mark 6  (the standard's second landmark)
      -30 dBFS  = PPM mark 1  (bottom of the printed scale)
  Whatever the plug-in streams at those three levels IS the scale, measured.

  Silence AFTER each tone: a PPM Type II falls 24 dB in 2.8 s (8.6 dB/s), a VU
  returns in ~300 ms. One long fall separates them beyond argument.

  Bursts at the end: 200 ms tone, 3 s gap. PPM attack is ~10 ms to 80 %, VU takes
  300 ms — and the fall after each burst repeats the measurement 5 times.

Play it ONCE on the Analogue screen in PPM with ExtState uf1_ndl_probe = 1.
Then again in VU if the VU dial is ever in question. Sections are announced on
stdout so the [ndl] lines can be segmented by time.
"""
import math
import struct
import sys
import wave

SR = 48000
FREQ = 1000.0


def tone(level_dbfs, seconds):
    amp = 10.0 ** (level_dbfs / 20.0)
    n = int(SR * seconds)
    w = 2.0 * math.pi * FREQ / SR
    # Peak amplitude is the dBFS figure: a PPM reads PEAK, so a -18 dBFS sine
    # must peak at -18, not average there.
    return [amp * math.sin(w * i) for i in range(n)]


def silence(seconds):
    return [0.0] * int(SR * seconds)


def main(path):
    plan = [
        ("silence", None, 3.0),
        ("tone -18 dBFS  = PPM mark 4 (reference, mid-dial)", -18.0, 6.0),
        ("silence  -> FALL from mark 4", None, 5.0),
        ("tone -10 dBFS  = PPM mark 6", -10.0, 6.0),
        ("silence  -> FALL from mark 6", None, 5.0),
        ("tone -30 dBFS  = PPM mark 1", -30.0, 6.0),
        ("silence  -> FALL from mark 1", None, 5.0),
    ]
    for i in range(5):
        plan.append((f"burst {i+1}: 200 ms @ -10 dBFS", -10.0, 0.2))
        plan.append((f"gap {i+1}  -> ATTACK + FALL", None, 3.0))

    samples = []
    t = 0.0
    print(f"{'start':>8}  {'len':>5}  section")
    for name, lvl, secs in plan:
        print(f"{t:8.2f}  {secs:5.2f}  {name}")
        samples.extend(silence(secs) if lvl is None else tone(lvl, secs))
        t += secs

    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(b"".join(
            struct.pack("<h", max(-32768, min(32767, int(s * 32767.0))))
            for s in samples))
    print(f"\n{t:.1f} s written to {path}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "uf1_needle.wav")
