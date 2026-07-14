# cap89_uf1_overview_sweep

**Date:** 2026-07-14
**Device:** SSL UF1 (VID_31E9 PID_0025, dev 8 on `\\.\USBPcap3`), Plugin Mode, Meter view, **OVERVIEW** screen.
**Driver:** SSL 360° (we capture ITS codec — Rea-Sixty not attached).
**Signal:** `vu_sweep.wav` (same as cap88) — 1 kHz, 36 steps × 2.5 s, L **+5 → −30 VU**,
R opposite. 0 VU = −18 dBFS, so the sweep spans **−13 .. −48 dBFS**.
**Duration:** 105 s window (playback started ~13 s in). 43.8 MB.

## Result — DECODED

| element | payload | meaning |
|---------|---------|---------|
| **`0x0125`** | 2 B | **(rms_L, rms_R)** — RMS bargraphs |
| **`0x0126`** | 2 B | **(peak_L, peak_R)** — peak bargraphs |
| **`0x0127`** | 2 B | **(hold_L, hold_R)** — peak-hold markers |
| **`0x011c`** | 6 × 25 B ASCII | `[PEAK max, PEAK L, PEAK R, RMS max, RMS L, RMS R]` |
| `0x0122` | 251 B | the animated graphic (Lissajous + ?) — **still open** |
| `0x0119` | 11 B | not decoded; prime suspect for balance / correlation |

### Readout layout proof
Fields 1/2 track the sweep exactly (−13 → −48 dBFS as L falls, mirrored on R) while
fields 0/3 stay pinned at the run's maximum (−13.0 / −16.0) — i.e. `max`. Fields 4/5 sit
a consistent **3 dB below** fields 1/2, which is exactly the RMS of a sine. That fixes
the layout as `[PEAK max, PEAK L, PEAK R, RMS max, RMS L, RMS R]`.

### Hold proof
At t=104.3 (signal ended) `0x0126` collapses to `(0, 2)` while `0x0127` stays at
`(38, 134)` — the held maximum.

### ⚠ Corrects an earlier note
Memory had `0x0126` = balance bar and `0x0127` = phase correlation, read off the
white-noise capture cap76. That is **wrong**: both are `(L, R)` bargraph pairs. With
white noise L == R, so the two bytes are near-equal and look like one "position ~160".
The opposite-direction sweep separates them unambiguously.

## The bargraph scale — one shared dBFS table

RMS bars, peak bars and hold markers all use the same scale (peak −15.9 dBFS and RMS
−16.0 dBFS both render as **123**). 39/40 levels are confirmed by more than one of the
four independent sources (peak L, peak R, RMS L, RMS R):

| dBFS | −51 | −50 | −49 | −48 | −47 | −46 | −45 | −44 | −43 | −42 | −41 | −40 | −39 |
|------|----|----|----|----|----|----|----|----|----|----|----|----|----|
| byte | 34 | 35 | 36 | 38 | 39 | 40 | 42 | 43 | 44 | 46 | 47 | 48 | 51 |

| dBFS | −38 | −37 | −36 | −35 | −34 | −33 | −32 | −31 | −30 | −29 | −28 | −27 | −26 |
|------|----|----|----|----|----|----|----|----|----|----|----|----|----|
| byte | 54 | 57 | 60 | 63 | 66 | 69 | 72 | 75 | 77 | 80 | 83 | 86 | 89 |

| dBFS | −25 | −24 | −23 | −22 | −21 | −20 | −19 | −18 | −17 | −16 | −15 | −14 | −13 |
|------|----|----|----|----|----|----|----|----|----|----|----|----|----|
| byte | 92 | 95 | 98 | 101 | 104 | 107 | 111 | 115 | 119 | 123 | 126 | 130 | 134 |

Piecewise: ~3 counts/dB above −30 dBFS, flattening to ~1.3 counts/dB below −40.
Rebuild with `analysis/uf1_bar_scale_table.py <this capture>`.

## Gap — why cap93 exists
`vu_sweep.wav` is scaled for the **VU faceplate** (−13..−48 dBFS), but these bargraphs
run on a **dBFS** scale to 0. So the top (0..−13) and bottom (−51..−90) of the table are
missing from this capture. `dbfs_sweep_lr.wav` (`gen_rta_corr_wav.py dbfs --lr`,
0 → −90 dBFS, L/R opposite) fills them in cap93.
