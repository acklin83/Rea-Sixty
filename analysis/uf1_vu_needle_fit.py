#!/usr/bin/env python3
"""UF1 Analogue-screen VU needle codec: map dB -> needle position.

The Analogue screen sends the needle positions (0x0125 = L, 0x0127 = R) and the
numeric dB readouts (0x011c, ASCII) in the SAME stream, so a capture of music
with wide dynamics IS a level sweep — no dedicated sweep capture needed. This
pairs each needle frame with the most recent readout and prints the observed
dB -> position mapping.

Usage:
  uf1_vu_needle_fit.py <pcapng> [--dev N] [--dump-readout]
"""
import sys, subprocess, binascii, re
from collections import OrderedDict, defaultdict

TSHARK = "/opt/homebrew/bin/tshark"

ADDR_READOUT = 0x011c
ADDR_NEEDLE_L = 0x0125
ADDR_NEEDLE_R = 0x0127


def out_frames(pcap, dev=None):
    dfilter = "usb.endpoint_address==0x02 && usb.capdata"
    if dev:
        dfilter = f"usb.device_address=={dev} && " + dfilter
    out = subprocess.run(
        [TSHARK, "-r", pcap, "-Y", dfilter, "-T", "fields",
         "-e", "frame.time_relative", "-e", "usb.capdata"],
        capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.split("\t")
        if len(parts) < 2:
            continue
        try:
            ts = float(parts[0])
            b = binascii.unhexlify(parts[1].strip().lower())
        except Exception:
            continue
        i = 0
        while i + 4 <= len(b):
            if b[i] != 0xFF:
                i += 1
                continue
            ln = b[i + 2]
            end = i + 3 + ln + 1
            if end > len(b):
                break
            f = b[i:end]
            if f[1] == 0x67 and len(f) >= 6:
                yield ts, (f[3] << 8) | f[4], f[5:-1]
            i = end


def ascii_of(b):
    return "".join(chr(c) if 32 <= c < 127 else "." for c in b)


FIELD_W = 25   # 0x011c = 6 fixed-width NUL-padded ASCII fields (150 B, cap80)


def parse_readout(payload):
    """0x011c -> (numeric fields, raw text fields).

    Fixed 25-byte NUL-padded ASCII fields. Do NOT go via ascii_of(): it renders
    non-printables as '.', which collides with the real decimal point and
    silently splits '-4.7' into '-4' and '7'.
    """
    fields = []
    for i in range(0, len(payload), FIELD_W):
        chunk = bytes(payload[i:i + FIELD_W])
        fields.append(chunk.split(b"\x00")[0].decode("latin-1", "replace").strip())
    vals = []
    for f in fields:
        if f in ("-inf", "inf", "-Inf"):
            vals.append(float("-inf"))
        elif re.fullmatch(r"[-+]?\d+(\.\d+)?", f):
            vals.append(float(f))
    return vals, fields


def main():
    pcap = sys.argv[1]
    dev = None
    for i, a in enumerate(sys.argv):
        if a == "--dev":
            dev = sys.argv[i + 1]
    dump_readout = "--dump-readout" in sys.argv

    frames = list(out_frames(pcap, dev))
    print(f"# {len(frames)} host->device FF67 frames")

    # Show what the readout row actually looks like before trusting the parse.
    if dump_readout:
        seen = 0
        for ts, addr, p in frames:
            if addr == ADDR_READOUT:
                vals, fields = parse_readout(p)
                print(f"t={ts:8.3f} len={len(p)} fields={fields} -> {vals}")
                seen += 1
                if seen >= 20:
                    break
        return

    # Pair each needle sample with the most recent readout.
    last_readout = None
    pairs = {"L": [], "R": []}
    for ts, addr, p in frames:
        if addr == ADDR_READOUT:
            vals, _ = parse_readout(p)
            if vals:
                last_readout = vals
        elif addr in (ADDR_NEEDLE_L, ADDR_NEEDLE_R) and len(p) == 2:
            if not last_readout:
                continue
            pos = (p[0] << 8) | p[1]
            side = "L" if addr == ADDR_NEEDLE_L else "R"
            pairs[side].append((ts, pos, tuple(last_readout)))

    for side in ("L", "R"):
        lst = pairs[side]
        if not lst:
            print(f"\n## needle {side}: no samples")
            continue
        positions = sorted({p for _, p, _ in lst})
        print(f"\n## needle {side} ({'0x0125' if side=='L' else '0x0127'})"
              f"  n={len(lst)}  distinct-pos={len(positions)}"
              f"  range={positions[0]}..{positions[-1]}")
        # For each distinct position, the readout values seen alongside it.
        by_pos = defaultdict(list)
        for _, pos, vals in lst:
            by_pos[pos].append(vals)
        print(f"  {'pos':>5}  {'n':>4}  readout-values-seen (first 6 fields)")
        for pos in positions[:40]:
            rows = by_pos[pos]
            # Show the range of each readout field at this needle position.
            ncol = min(6, min(len(r) for r in rows))
            cols = []
            for c in range(ncol):
                vs = [r[c] for r in rows if r[c] != float("-inf")]
                cols.append(f"{min(vs):7.1f}..{max(vs):<7.1f}" if vs else "   -inf     ")
            print(f"  {pos:>5}  {len(rows):>4}  {' '.join(cols)}")
        if len(positions) > 40:
            print(f"  ... (+{len(positions)-40} more positions)")


if __name__ == "__main__":
    main()
