#!/usr/bin/env python3
"""Measure SSL's peak-hold reset period from cap89.

Result (2026-07-15): 28 snaps, median 3.019 s, 26/27 intervals in 2.90-3.10 s.
The hold marker does NOT decay -- it keeps the max and snaps to the current
peak every 3.0 s. This is the source of kUf1HoldResetSec in main.cpp.

The dump shows hold does NOT decay: it keeps the max, then SNAPS to the current
peak.  A snap = hold jumps DOWN to exactly the current peak.  Measure the
interval between snaps on the falling channel (L).
"""
import sys
sys.path.insert(0, "/Users/stoersender/Documents/dev/Rea-Sixty/analysis")
from uf1_vu_needle_fit import out_frames  # noqa: E402

CAP = "/Users/stoersender/Documents/dev/Rea-Sixty/captures/cap89_uf1_overview_sweep.pcapng"

rows = {}
for ts, addr, payload in out_frames(CAP):
    if len(payload) != 2:
        continue
    if addr in (0x0126, 0x0127):
        rows.setdefault(round(ts, 3), {})[addr] = payload

series = []
for t in sorted(rows):
    r = rows[t]
    if 0x0126 in r and 0x0127 in r:
        series.append((t, r[0x0126][0], r[0x0127][0]))   # t, peakL, holdL

snaps = []
for i in range(1, len(series)):
    t, p, h = series[i]
    _, pp, ph = series[i - 1]
    # snap: hold moved DOWN and landed on the current peak
    if h < ph and h == p:
        snaps.append(t)

print(f"snaps detected on L: {len(snaps)}")
iv = [snaps[i] - snaps[i - 1] for i in range(1, len(snaps))]
iv = [x for x in iv if 0.5 < x < 8]
iv.sort()
if iv:
    n = len(iv)
    print(f"intervals n={n}")
    print(f"  median = {iv[n // 2]:.3f} s")
    print(f"  mean   = {sum(iv)/n:.3f} s")
    print(f"  min/max= {iv[0]:.3f} / {iv[-1]:.3f} s")
    print("\n  all:", " ".join(f"{x:.2f}" for x in iv))
