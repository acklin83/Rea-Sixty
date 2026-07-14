#!/usr/bin/env python3
"""Build the UF1 Overview-screen bargraph scale: dBFS -> bar byte.

Decoded from cap89 (vu_sweep.wav on the Overview screen, L/R opposite):

  0x0125 payload = 2 B = (rms_L,  rms_R)    -- RMS bargraphs
  0x0126 payload = 2 B = (peak_L, peak_R)   -- peak bargraphs
  0x0127 payload = 2 B = (hold_L, hold_R)   -- peak-hold markers
  0x011c = 6x25B ASCII = [PEAK max, PEAK L, PEAK R, RMS max, RMS L, RMS R]

All three share ONE dBFS scale (a peak of -15.9 dBFS and an RMS of -16.0 dBFS
both render as byte 123). Note this DISPROVES the older note that had 0x0126 =
balance and 0x0127 = correlation — that reading came from a white-noise capture
where L==R, so the (L,R) pair looks like a single "position ~160".

Pairs are taken from the peak elements against the peak readouts, and the RMS
elements against the RMS readouts, so both feed one table. L and R are swept in
opposite directions, giving two independent confirmations per level.

Usage: uf1_bar_scale_table.py <cap89.pcapng> [--csv]
"""
import sys
from collections import defaultdict

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from uf1_vu_needle_fit import out_frames, parse_readout   # noqa: E402

ADDR_RMS = 0x0125
ADDR_PEAK = 0x0126
ADDR_READOUT = 0x011c

SETTLE = 1.5
STEP = 2.5


def main():
    pcap = sys.argv[1]
    as_csv = "--csv" in sys.argv

    obs = defaultdict(lambda: defaultdict(int))   # dbfs -> byte -> count
    src = defaultdict(set)

    last = None
    for ts, addr, p in out_frames(pcap):
        if addr == ADDR_READOUT:
            vals, _ = parse_readout(p)
            if len(vals) >= 6:
                last = vals
        elif addr in (ADDR_PEAK, ADDR_RMS) and len(p) == 2 and last:
            if (ts % STEP) < SETTLE:
                continue
            # readout layout: [PEAK max, PEAK L, PEAK R, RMS max, RMS L, RMS R]
            if addr == ADDR_PEAK:
                pairs = ((last[1], p[0], "pL"), (last[2], p[1], "pR"))
            else:
                pairs = ((last[4], p[0], "rL"), (last[5], p[1], "rR"))
            for val, byte, side in pairs:
                if val == float("-inf") or val < -95:
                    continue
                db = round(val)
                if abs(val - db) > 0.15:
                    continue
                obs[db][byte] += 1
                src[db].add(side)

    rows = []
    for db in sorted(obs):
        byte, n = max(obs[db].items(), key=lambda kv: kv[1])
        total = sum(obs[db].values())
        rows.append((db, byte, n, total, f"{min(obs[db])}..{max(obs[db])}",
                     ",".join(sorted(src[db]))))

    if as_csv:
        print("dbfs,bar_byte")
        for db, byte, *_ in rows:
            print(f"{db},{byte}")
        return

    print(f"# {pcap}\n# {len(rows)} levels, sampled >{SETTLE}s into each {STEP}s step\n")
    print(f"{'dBFS':>5} {'byte':>5} {'agree':>8} {'spread':>9}  sources")
    for db, byte, n, total, spread, sides in rows:
        print(f"{db:>5d} {byte:>5} {n:>3}/{total:<4} {spread:>9}  {sides}")

    multi = [r for r in rows if len(r[5].split(",")) > 1]
    print(f"\n{len(multi)}/{len(rows)} levels confirmed by more than one "
          f"element/channel (peak vs RMS, L vs R)")


if __name__ == "__main__":
    main()
