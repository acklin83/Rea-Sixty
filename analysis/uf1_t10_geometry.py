#!/usr/bin/env python3
"""Measure the geometry of the plug-in's Lissajous stream (t10).

t10 is a sparse 0..1 intensity raster (measured 2026-07-15: dominant length
2113, occasionally 5000, all-zero on silence). What is NOT known is how it maps
onto the UF1's 187-row / 8560-byte diamond (codec in uf1-meter-codec-decoded).

The method is the one that cracked the bitmap: drive a KNOWN correlation and
read the structure off, rather than do numerology on the array length.
  L = R   -> corr +1 -> a vertical line -> ~one lit cell per row
  L = -R  -> corr -1 -> a horizontal band across the wide middle rows

If the raster is a diamond, the lit indices under L=R must fall at the row
starts, i.e. the gaps between consecutive lit indices trace the row widths, and
those widths must ramp 1,2,3,… and back down. That is the fingerprint.

Input:  /tmp/reaper_t10_frames.log  (written by the impersonator's trace probe)
        FRAME t=… src=… n=… nz=…
        <index> <value>
"""
import sys
from collections import Counter

PATH = sys.argv[1] if len(sys.argv) > 1 else "/tmp/reaper_t10_frames.log"

frames = []
cur = None
for line in open(PATH):
    if line.startswith("FRAME"):
        if cur:
            frames.append(cur)
        parts = dict(p.split("=") for p in line.split()[1:])
        cur = {"n": int(parts["n"]), "nz": int(parts["nz"]),
               "src": parts["src"], "t": float(parts["t"]), "pts": []}
    elif cur is not None:
        i, v = line.split()
        cur["pts"].append((int(i), float(v)))
if cur:
    frames.append(cur)

if not frames:
    sys.exit("no frames — was a signal playing, on the Overview screen?")

print(f"{len(frames)} frames")
print("lengths:", Counter(f["n"] for f in frames).most_common())
print()

# Use the sparsest frame: closest to "one lit cell per row" = the L=R case.
f = min(frames, key=lambda f: f["nz"])
idx = sorted(i for i, _ in f["pts"])
print(f"sparsest frame: t={f['t']} n={f['n']} nz={f['nz']}")
print(f"lit index range: {idx[0]} .. {idx[-1]}")

gaps = [idx[k] - idx[k - 1] for k in range(1, len(idx))]
print(f"\ngap histogram (consecutive lit indices): {Counter(gaps).most_common(12)}")
print(f"first 40 gaps: {gaps[:40]}")

# A diamond's row widths ramp up then down and sum to n. If the gaps ARE the row
# widths, they should be monotonic-ish up then down, and sum to about n.
print(f"\nsum(gaps)={sum(gaps)}  vs n={f['n']}")

# Cross-check: does the UF1 diamond's own row structure fit this n?
# UF1: widths 2,1,1,2,3,…,92,92,…,3,2,1,1 over 187 rows, sum 8560.
def diamond(nmax):
    w = [2, 1, 1] + list(range(2, nmax + 1)) + list(range(nmax, 1, -1)) + [1, 1, 2]
    return w

for nmax in range(20, 100):
    if sum(diamond(nmax)) == f["n"]:
        print(f"  !! n={f['n']} matches a UF1-shaped diamond with nmax={nmax} "
              f"({len(diamond(nmax))} rows)")

# Plain diamond (1..k..1), sum = k^2
k = int(round(f["n"] ** 0.5))
if k * k == f["n"]:
    print(f"  !! n={f['n']} = {k}^2 — a plain diamond, {2*k-1} rows of 1..{k}..1")

print("\nlit indices (first 60):", idx[:60])
