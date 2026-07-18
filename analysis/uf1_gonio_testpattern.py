#!/usr/bin/env python3
"""Build a gonio_replay.bin holding a KNOWN test pattern, not captured data.

Why: every codec claim so far was checked by decoding with the same table used
to encode — circular, and it survived four days that way. A test pattern breaks
the circle: the pattern is chosen before the test, and the UF1 either shows it
or does not. Nothing about the answer depends on our own decoder.

Pattern: a bright horizontal band sweeping top -> bottom, with a dimmer band
trailing it, laid out under the 8-BIT model (8560 bytes = 8560 pixels over the
MEASURED 187-row diamond, widths 2,1,1,2..91,92,92,91..2,1,1).

Read on hardware:
  band sweeps cleanly            -> 8bpp + this row geometry are right
  two half-height bands, or the
  sweep runs at double speed     -> the device reads 4 bits per pixel
  noise / nothing                -> neither; the layout is something else

Usage:
  uf1_gonio_testpattern.py [-o gonio_replay.bin] [-n 120]
"""
import argparse

IMG_BYTES = 8560
ROWS = 187


def widths():
    """The MEASURED row widths (analysis/uf1_gonio_decode.py). Do not tidy into
    a loop: the obvious reconstruction gives 188 rows / 8562 bytes."""
    w = ([2, 1, 1] + list(range(2, 92)) + [92, 92]
         + list(range(91, 1, -1)) + [1, 1])
    assert sum(w) == IMG_BYTES and len(w) == ROWS, (sum(w), len(w))
    return w


W = widths()
OFF = [0]
for _w in W:
    OFF.append(OFF[-1] + _w)


def frame(pos):
    """One frame: bright band at row `pos`, dimmer trail above it."""
    img = bytearray(IMG_BYTES)
    for r in range(ROWS):
        d = pos - r
        if 0 <= d < 6:
            v = 255                      # the band itself
        elif 6 <= d < 24:
            v = 200 - (d - 6) * 10       # trail, fading
        else:
            continue
        img[OFF[r]:OFF[r] + W[r]] = bytes([max(v, 0)]) * W[r]
    return bytes(img)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--out", default="gonio_replay.bin")
    ap.add_argument("-n", "--count", type=int, default=120)
    a = ap.parse_args()
    with open(a.out, "wb") as f:
        for k in range(a.count):
            f.write(frame(int(k * (ROWS + 24) / a.count)))
    print(f"{a.count} test frames -> {a.out} "
          f"({a.count * IMG_BYTES} B, sweep ~{a.count / 24.5:.1f} s per loop)")


if __name__ == "__main__":
    main()
