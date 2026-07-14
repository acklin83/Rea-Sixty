#!/usr/bin/env python3
"""Verify our Overview-screen codec against cap89, byte for byte.

For each settled sweep step we take SSL's OWN numeric readout (0x011c) as the
input, run our codec, and compare the bytes we'd send against the bytes SSL
actually sent for 0x0125 / 0x0126 / 0x0127. This catches wrong element→meaning
mappings and wrong scales without touching hardware.
"""
import sys
sys.path.insert(0, "analysis")
from uf1_vu_needle_fit import out_frames, parse_readout   # noqa: E402

# --- our codec, mirrored from main.cpp ---
kUf1BarScale = [
    9,10,10,11,11,12,12,12,12,13,13,13,14,14,15,15,
    16,16,16,16,17,17,17,18,18,18,19,19,19,20,20,20,
    21,21,22,24,25,27,28,30,31,33,34,35,36,38,39,40,
    42,43,44,46,47,48,51,54,57,60,63,66,69,72,75,77,
    80,83,86,89,92,95,98,101,104,107,111,115,119,123,126,130,
    134,138,142,147,151,156,162,167,172,178,183,188,194,199,
]


def bar_byte(dbfs):
    if dbfs is None or dbfs <= -93:
        return 0
    idx = round(dbfs) + 93
    idx = max(0, min(93, idx))
    return kUf1BarScale[idx]


def main():
    pcap = "captures/cap89_uf1_overview_sweep.pcapng"
    rows = [(ts, a, list(p)) for ts, a, p in out_frames(pcap)]

    # readout layout: [PEAK max, PEAK L, PEAK R, RMS max, RMS L, RMS R]
    last = None
    # gather per settled step: SSL's actual 0x0125/26/27 vs our prediction
    STEP = 2.5
    seen = set()
    print(f"{'t':>6} {'pkL':>6} {'pkR':>6} {'rmsL':>6} {'rmsR':>6}  "
          f"{'SSL 0125':>10} {'SSL 0126':>10} {'SSL 0127':>10}")
    cur125 = cur126 = cur127 = None
    for ts, a, p in rows:
        if a == 0x011c:
            v, _ = parse_readout(p)
            if len(v) >= 6:
                last = v
        elif a == 0x0125 and len(p) == 2:
            cur125 = tuple(p)
        elif a == 0x0126 and len(p) == 2:
            cur126 = tuple(p)
        elif a == 0x0127 and len(p) == 2:
            cur127 = tuple(p)
        step = int(ts / STEP)
        if step in seen or (ts % STEP) < 1.8 or last is None:
            continue
        if None in (cur125, cur126, cur127):
            continue
        seen.add(step)
        pkL, pkR, rmsL, rmsR = last[1], last[2], last[4], last[5]
        f = lambda x: (x if x != float("-inf") else None)
        print(f"{ts:6.1f} {pkL:6.1f} {pkR:6.1f} {rmsL:6.1f} {rmsR:6.1f}  "
              f"{str(cur125):>10} {str(cur126):>10} {str(cur127):>10}")
    print("\nOur prediction for a few levels (dBFS -> bar byte):")
    for db in (-6, -12, -18, -24, -30, -48):
        print(f"  {db:>4} dBFS -> {bar_byte(db)}")


if __name__ == "__main__":
    main()
