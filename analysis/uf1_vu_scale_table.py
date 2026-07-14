#!/usr/bin/env python3
"""Build the UF1 Analogue-screen VU scale table: VU dB -> needle byte.

Decoded from cap88 (vu_sweep.wav: 1 kHz, L +5..-30 VU, R opposite, 2.5s steps):

  0x0125 payload = 2 bytes = (needle_L, needle_R)   -- the live VU needles
  0x0127 payload = 2 bytes = (hold_L,   hold_R)     -- the peak-hold markers
  0x011c = 6x25B ASCII = [L VU, L hold, R VU, R hold, "L", "R"]

Both needles share ONE scale, which the opposite-direction sweep proves twice
over: the L-derived and R-derived tables agree. The scale is a real VU
faceplate - compressed low (~2 counts/dB below -10 VU), expanded high
(~15 counts/dB above -3 VU) - so it is a lookup table, not a formula. It pins
at 4 (<= -20 VU) and 180 (>= +3 VU).

Usage: uf1_vu_scale_table.py <cap88.pcapng> [--csv]
"""
import sys
from collections import defaultdict

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from uf1_vu_needle_fit import out_frames, parse_readout   # noqa: E402

ADDR_NEEDLE = 0x0125
ADDR_HOLD = 0x0127
ADDR_READOUT = 0x011c

SETTLE = 1.5    # seconds into a 2.5s step before we trust it to have settled
STEP = 2.5


def main():
    pcap = sys.argv[1]
    as_csv = "--csv" in sys.argv

    # VU value -> observed needle bytes, gathered from BOTH channels.
    obs = defaultdict(lambda: defaultdict(int))   # vu -> byte -> count
    src = defaultdict(set)                        # vu -> {"L","R"}

    last = None
    for ts, addr, p in out_frames(pcap):
        if addr == ADDR_READOUT:
            vals, _ = parse_readout(p)
            if len(vals) >= 4:
                last = vals
        elif addr == ADDR_NEEDLE and len(p) == 2 and last:
            # Only sample once each step has settled: the needle is ballistic
            # (~300ms) and mid-transition pairs would smear the table.
            if (ts % STEP) < SETTLE:
                continue
            for val, byte, side in ((last[0], p[0], "L"), (last[2], p[1], "R")):
                if val == float("-inf") or val < -40:
                    continue
                vu = round(val)
                if abs(val - vu) > 0.15:     # skip anything still drifting
                    continue
                obs[vu][byte] += 1
                src[vu].add(side)

    rows = []
    for vu in sorted(obs):
        byte, n = max(obs[vu].items(), key=lambda kv: kv[1])
        total = sum(obs[vu].values())
        spread = f"{min(obs[vu])}..{max(obs[vu])}"
        rows.append((vu, byte, n, total, spread, "".join(sorted(src[vu]))))

    if as_csv:
        print("vu_db,needle_byte")
        for vu, byte, *_ in rows:
            print(f"{vu},{byte}")
        return

    print(f"# {pcap}")
    print(f"# {len(rows)} VU steps, sampled >{SETTLE}s into each {STEP}s step\n")
    print(f"{'VU':>5} {'byte':>5} {'agree':>7} {'spread':>9}  sides")
    for vu, byte, n, total, spread, sides in rows:
        flag = "" if n == total else "  <-- disagreement"
        print(f"{vu:>+5d} {byte:>5} {n:>3}/{total:<3} {spread:>9}  {sides}{flag}")

    # Consistency check: L and R must land on the same byte for the same VU.
    both = [r for r in rows if r[5] == "LR"]
    print(f"\n{len(both)}/{len(rows)} steps observed on BOTH channels "
          f"(independent confirmation of one shared scale)")

    print("\n// C++ table (index = VU + 20, i.e. -20..+3 VU):")
    lo, hi = -20, 3
    tbl = {vu: b for vu, b, *_ in rows}
    vals = [str(tbl[v]) for v in range(lo, hi + 1) if v in tbl]
    print(f"// covers {lo}..{hi} VU")
    print("constexpr uint8_t kUf1VuScale[] = { " + ", ".join(vals) + " };")


if __name__ == "__main__":
    main()
