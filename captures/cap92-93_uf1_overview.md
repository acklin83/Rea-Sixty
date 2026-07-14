# cap92 + cap93 — UF1 Overview screen: correlation isolation + full bargraph range

**Date:** 2026-07-14
**Device:** SSL UF1 (VID_31E9 PID_0025, dev 8 on `\\.\USBPcap3`), Plugin Mode, Meter view, **OVERVIEW**.
**Driver:** SSL 360° (we capture ITS codec — Rea-Sixty not attached).

| capture | signal | question it answers |
|---------|--------|--------------------|
| **cap92** | `corr_sweep.wav` — L/R noise, correlation stepped **+1 → 0 → −1** in 9 × 3 s, level CONSTANT at −20 dBFS | where does correlation live? |
| **cap93** | `dbfs_sweep_lr.wav` — 1 kHz, **0 → −90 dBFS** in 3 dB steps, L/R opposite | the bargraph range cap89 couldn't reach |

Both from `analysis/gen_rta_corr_wav.py`. cap92 holds level constant *by design*: anything
that moves is correlation-driven, everything else is excluded.

## cap93 — the bargraph table, completed and cross-validated

cap89 (`vu_sweep`) only spanned −13..−48 dBFS because it is scaled for the VU faceplate.
cap93 covers 0..−93 dBFS. **Where the two overlap, every single count agrees** — two
independent captures, different signals, opposite sweep directions, identical table:

| dBFS | −51 | −48 | −45 | −42 | −39 | −36 | −33 | −30 | −24 | −18 | −15 |
|------|----|----|----|----|----|----|----|----|----|----|----|
| cap89 | 34 | 38 | 42 | 46 | 51 | 60 | 69 | 77 | 95 | 115 | 126 |
| cap93 | 34 | 38 | 42 | 46 | 51 | 60 | 69 | 77 | 95 | 115 | 126 |

Full range: **0 dBFS → 199**, −93 dBFS → 9. Strongly non-linear (≈5.1 counts/dB at the
top, ≈2.0 at −40, nearly flat below −60) — a lookup table, not a formula. The canonical
merged table (cap89's 1 dB steps in the middle, cap93's 3 dB steps at the ends,
interpolated onto a 1 dB grid) is `kUf1BarScale[]`, index = dBFS + 93.

Resolution collapse below −60 dBFS is partly the scale bottom and partly the 16-bit test
signal (see cap90-91); it does not matter for a display that bottoms out there anyway.

## cap92 — correlation is NOT a value; the graphic is a chunked bitmap

The bars stay **completely still** across all nine correlation steps — `0x0125` = (72,72),
`0x0126` ≈ 107, `0x0127` ≈ 113 — exactly as the constant-level design intends. So
correlation lives in **none** of them, and `0x0122` is the only place left.

### `0x0122` on Overview is chunked
```
byte[0]      = chunk index, 0..34
byte[1..250] = 250 payload bytes  (last chunk, index 34, carries only 60)
total        = 35 chunks x 250 - 190 = 8560 payload bytes per full frame
```
Each index appears exactly 1102 times in cap92 = the number of full refreshes. Identical
structure in cap89. **This is why balance and correlation have no elements of their own:
SSL renders the whole graphic area — Lissajous, balance bar, correlation meter — into one
buffer and ships it.**

### It is NOT a 1-bit-per-pixel bitmap
Tested and rejected: with 68480 bits, no width from 16..700 makes the lit pixels of a
corr=+1 frame (which MUST be a straight line) linear in x/y — best |corr(x,y)| = 0.40,
i.e. noise. Only 668 of 68480 bits are set.

### The packing has variable-length rows (measured)
At corr = +1.0 the goniometer draws a **vertical line — one lit byte per row** (confirmed
by Frank from the instrument's behaviour: L=R → vertical, corr −1 → horizontal). So the
gaps between lit bytes ARE the row widths:

```
deltas: 2, 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19
```

The **second difference is exactly 1**: each row is 1 byte wider than the last. A
rectangular raster cannot produce this. A symmetric diamond would widen by 2 per row, so
the exact shape is not yet pinned down.

At corr = −1.0 (horizontal line) the lit bytes cluster around index 3500..4900 in several
long runs (75, 73, 70, 69, 68…) rather than one — the trace has thickness/persistence.

## SOLVED — the Lissajous codec (2026-07-14, desk analysis of cap92 + cap89)

`0x0122` on the Overview screen is a **goniometer bitmap**: a 45°-rotated square (a
vectorscope face) stored as **187 horizontal rows** of a diamond, one byte per pixel =
that pixel's **intensity** (0 = off, higher = brighter), NOT 1 bit.

### Geometry (exact, sums to 8560)
Row widths, measured from a corr=+1 frame where exactly one pixel per row is lit:
```
2, 1, 1, 2, 3, 4, ... , 91, 92, 92, 91, ... , 4, 3, 2, 1, 1
```
i.e. 1..92 then 92..1 (peak 92 at the centre, two rows wide), with an antialiased
`2,1,1` apex at each tip. 187 rows, Σ = 8560 = the payload byte count. The offset of row
r is the running sum of the widths before it. Decoder: `analysis/uf1_gonio_decode.py`
(rebuilds the geometry + renders any assembled frame).

### Proof — the three correlation extremes render as a textbook goniometer
- **corr +1** (L=R): one lit pixel per row tracing a straight line (display-vertical,
  which is a diagonal in the row-major diamond because the face is rotated 45°).
- **corr 0** (uncorrelated): the diamond fills with a scattered cloud.
- **corr −1** (L=−R): a dense horizontal band across the wide middle rows.

Frank confirmed the instrument behaviour that anchored the geometry: L=R → vertical line,
corr −1 → horizontal line.

### Still finer detail (not blocking)
The goniometer draws a fixed **graticule** (the diamond frame + diagonal reference lines)
that is always lit; the signal trace adds to it. Separating trace from graticule, and the
exact L/R→pixel coordinate transform, are refinements — the packing itself is done. The
balance bar and correlation meter are rendered *into* this same buffer (which is why they
have no elements of their own), so they come for free once we paint the buffer.
