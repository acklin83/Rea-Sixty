# cap88_uf1_vu_sweep

**Date:** 2026-07-14
**Device:** SSL UF1 (VID_31E9 PID_0025, dev 8 on `\\.\USBPcap3`), Plugin Mode, Meter view, **ANALOGUE** screen.
**Driver:** SSL 360° (we capture ITS codec — Rea-Sixty not attached).
**Signal:** `analysis/gen_vu_sweep_wav.py` → `vu_sweep.wav` — 1 kHz sine, 36 steps × 2.5 s,
L sweeps **+5 → −30 VU**, R sweeps **−30 → +5 VU** (opposite). 0 VU = −18 dBFS
(the Analogue screen's own "Ref −18.0dB" setting).
**Duration:** 105 s window, 90 s signal. 150152 packets.

## Why a stepped signal
The VU needle is ballistic (~300 ms) and the text readout has a peak hold, so during
music the two never agree and the pairs are useless (proven on cap80: the readout sat
at 3.2 while the needle fell from 46250 to 31856). Holding each level for 2.5 s lets
BOTH settle, which turns `0x0125` vs `0x011c` into exact pairs straight out of the USB
stream — **no simultaneous protobuf capture needed to correlate.**

Opposite L/R directions mean one capture yields two independent sweeps *and* proves the
byte→channel assignment.

## Result — DECODED

| element | payload | meaning |
|---------|---------|---------|
| **`0x0125`** | 2 B | **(needle_L, needle_R)** — the live VU needles |
| **`0x0127`** | 2 B | **(hold_L, hold_R)** — the peak-hold markers |
| **`0x011c`** | 6 × 25 B ASCII, NUL-padded | `[L VU, L hold, R VU, R hold, "L", "R"]` |
| `0x0119` | 11 B | appears in this screen; not yet decoded |

`0x0125`/`0x0127` are **two independent bytes, NOT one 16-bit value** (the earlier
"17..180" note in memory was reading the two bytes separately and was right; a later
16-bit reading was wrong). Proof: at sweep start (L max, R min) the payload is
`(180, 4)`, and `0x0127` holds at `(4, 180)` at t=86.8 while `0x0125` has already fallen
to `(4, 21)` — exactly the hold behaviour `0x011c` field 1/3 reports numerically.

## The VU scale — one shared table for both needles

Both channels land on the same byte for the same VU value (34/40 steps confirmed on
BOTH channels independently — that's what the opposite sweep buys):

| VU | −20 | −19 | −18 | −17 | −16 | −15 | −14 | −13 | −12 | −11 | −10 | −9 |
|----|----|----|----|----|----|----|----|----|----|----|----|----|
| byte | 4 | 7 | 9 | 11 | 13 | 15 | 17 | 19 | 21 | 23 | 25 | 32 |

| VU | −8 | −7 | −6 | −5 | −4 | −3 | −2 | −1 | 0 | +1 | +2 | +3 |
|----|----|----|----|----|----|----|----|----|----|----|----|----|
| byte | 39 | 45 | 55 | 65 | 77 | 89 | 104 | 119 | 134 | 149 | 164 | 179 |

Pins: **4** at ≤ −20 VU, **180** at ≥ +3 VU (+4/+5 VU both read 180).

It is a real VU faceplate geometry — compressed at the bottom (~2 counts/dB below
−10 VU), expanded at the top (~15 counts/dB above −3 VU) — so it is a **lookup table,
not a formula**. Rebuild it with `analysis/uf1_vu_scale_table.py <this capture>`.

Residual ±1-count spread per step is ballistic ripple plus the readout's 0.1 dB
rounding; the dominant byte per step is unambiguous (typically 41/49 samples).

## Open
The `0x0122` graphic is not part of this screen's payload (Analogue draws needles via
`0x0125`), so the Overview/RTA `0x0122` codec is still open — see cap89/cap90/cap91.
