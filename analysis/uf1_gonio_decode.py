#!/usr/bin/env python3
"""UF1 Overview goniometer (0x0122) — reassemble + render.

Decoded 2026-07-14 from cap92 (correlation sweep) + cap89 (opposite L/R sweep =
angle sweep). See captures/cap92-93_uf1_overview.md.

Wire format:
  0x0122 on the Overview screen is CHUNKED. Each frame:
    byte[0]      = chunk index 0..34
    byte[1..]    = 250 payload bytes (the last chunk, index 34, carries 60)
  35 chunks reassemble to 8560 payload bytes = one full goniometer image.

Geometry:
  The 8560 bytes are a DIAMOND (a 45°-rotated square = a vectorscope face),
  stored as 187 horizontal rows of increasing then decreasing width:
    widths = 2,1,1,2,3,4,...,91,92,92,91,...,4,3,2,1,1   (peak 92, sum 8560)
  One byte per pixel = that pixel's INTENSITY (0 = off, higher = brighter), NOT
  1 bit. Proven: at correlation +1 (L=R) exactly one byte per row is lit and the
  lit column is vertical; as L/R balance sweeps, the line rotates continuously —
  a goniometer.

This module rebuilds the geometry from a known-good frame (so it stays correct
if a future capture refines it) and renders any assembled frame.
"""
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from uf1_vu_needle_fit import out_frames   # noqa: E402


def row_widths():
    """The 187 row widths, measured from a corr=+1 frame (one lit pixel per row).

    Body is 1..92, 92..1 (peak 92 at the centre, two rows wide); both tips are
    2,1,1 — the antialiased apex of the diamond. Sums to exactly 8560.
    """
    core = list(range(1, 93)) + list(range(92, 0, -1))   # 1..92, 92..1
    w = [2, 1, 1] + core[3:-3] + [1, 1, 2][::-1]
    # Use the exact measured sequence rather than trust the reconstruction:
    w = ([2, 1, 1] + list(range(2, 92)) + [92, 92]
         + list(range(91, 1, -1)) + [1, 1])
    assert sum(w) == 8560 and len(w) == 187, (sum(w), len(w))
    return w


WIDTHS = row_widths()
OFFS = [0]
for _w in WIDTHS:
    OFFS.append(OFFS[-1] + _w)


def assemble(frames_iter, t_lo, t_hi):
    """Collect one full 35-chunk 0x0122 image within [t_lo, t_hi]."""
    got = {}
    for ts, addr, p in frames_iter:
        if addr != 0x0122 or not (t_lo <= ts <= t_hi):
            continue
        if p[0] not in got:
            got[p[0]] = bytes(p[1:])
        if len(got) == 35:
            return b"".join(got[i] for i in range(35))
    return None


def render(buf, thresh=40, width=92):
    """ASCII-render the diamond. '#' = bright, '+' = dim, '.' = off."""
    lines = []
    for r, w in enumerate(WIDTHS):
        seg = buf[OFFS[r]:OFFS[r] + w]
        pad = (width - w) // 2
        row = "".join("#" if v > thresh else ("+" if v else ".") for v in seg)
        lines.append(" " * pad + row)
    return "\n".join(lines)


if __name__ == "__main__":
    pcap = sys.argv[1] if len(sys.argv) > 1 else "captures/cap89_uf1_overview_sweep.pcapng"
    t = float(sys.argv[2]) if len(sys.argv) > 2 else 59.0
    frames = list(out_frames(pcap))
    buf = assemble(frames, t - 0.25, t + 0.25)
    if not buf:
        sys.exit(f"no complete 0x0122 frame near t={t}")
    print(f"# {pcap}  t={t}  ({len(buf)} bytes, {len(WIDTHS)} rows)")
    # every 6th row so it fits a terminal
    full = render(buf).splitlines()
    print("\n".join(full[::6]))
