# cap94_uf1_ppm_sweep

**Date:** 2026-07-14
**Device:** SSL UF1 (dev 8 on `\\.\USBPcap3`), Plugin Mode, Meter view, **ANALOGUE** screen, meter set to **PPM** (not VU).
**Driver:** SSL 360°.
**Signal:** `dbfs_sweep_lr.wav` — 1 kHz, 31 steps × 2.5 s, 0 → −90 dBFS, L/R opposite.

## Why this capture exists
Frank caught the gap: the Analogue screen can run **VU or PPM Type-II**, and cap88 only
covered VU. The needle byte (4..180) is a *physical deflection* — a different faceplate
means the same deflection is a different value, so `kUf1VuScale` would simply be wrong in
PPM. Confirmed: it is a different law entirely.

| value | −2 | +1 | +4 | +7 |
|-------|----|----|----|----|
| **PPM** byte | 0 | 9 | 89 | 173 |
| **VU** byte (cap88) | 104 | 149 | — | — |

## Result — PPM is linear in marks (unlike VU)

| PPM | ≤−0.5 | 0.2 | 1.0 | 1.7 | 2.5 | 3.2 | 4.0 | 4.7 | 5.5 | 6.2 | 7.0 | ≥7.7 |
|-----|------|----|----|----|----|----|----|----|----|----|----|----|
| byte | 0 | 2 | 9 | 28 | 48 | 68 | 89 | 110 | 131 | 152 | 173 | 180 |

From 1.0 to 7.0 the mapping is linear to within ±2 counts:

```
byte = 9 + (mark - 1) * 27.3,  clamped to [0, 180]
```

`(173-9)/6 = 27.33` counts per mark — the classic **PPM Type-II faceplate, marks 1..7**,
which is linear by construction. That is why PPM needs a formula and VU needs a lookup
table (curved faceplate). Below mark ~1 the scale compresses hard into the 0 pin; above
mark 7 it sits at 180.

Every L/R pair agrees (the opposite sweep gives two independent measurements per level).

## Two needles in PPM — confirmed by Frank at the hardware
Frank: *"bei ppm sinds 2 nadeln, eine ist schnell, die andre zieht nach."* That matches
the wire exactly and confirms the cap88 reading:

* `0x0125` = the fast needle
* `0x0127` = the lagging one — the same element cap88 identified as the peak-hold marker
  (at cap88 t=86.8, `0x0125` had fallen to `(4,21)` while `0x0127` still held `(4,180)`).

So the element roles are the same in VU and PPM; only the scale differs. PPM draws the
hold as a visible second needle.

## Method note
The scale table builder (`uf1_vu_scale_table.py`) rounds to integer units, which is right
for VU (1 VU per sweep step) but wrong here: a 3 dB step moves PPM only ~0.75 units, so
it discarded most samples and produced 6 usable points. The curve above comes from
pairing at the readout's own 0.1 resolution instead.

## Open
Whether the plugin's protobuf `VuPpm` value equals this on-screen readout is **not yet
verified** — that is the one assumption left in the Analogue chain. Check it against the
impersonator when wiring, not by assumption.

`0x010e` carries no frames in this capture (the screen was set to PPM before the window
opened), so the mode could not be verified from the setup burst. It is confirmed instead
by the scale itself, which is unambiguously different from cap88's.
