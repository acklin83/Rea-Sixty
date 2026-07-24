# UF1 Bug D — per-Digital-Type Overview bar scales, 2026-07-24 (StoerPC)

The Overview peak bars showed "garbage" under the K-modes (Frank): `uf1BarByte_`
used one fixed `kUf1BarScale` (Non-Linear) for all 7 Digital Types, but the
K-System and Linear/2x variants map dBFS to a different bar height. Fixed by
measuring each scale and adding per-type tables. Branch `uf1-native-build`,
commit `4cc9177`.

## Method (validated)
SSL 360 drives the UF1 (UF1 on sslbus, StoerPC `192.168.177.198` — DHCP moved it
off .197; USBPcap3). For each Digital Type, ramp the level through the range and
**correlate the plug-in's dB readout with the peak-bar byte**:
- `0x011c` field **1** = the current L-peak dB (field 0 is a held max — do NOT use
  it; found by eyeballing which field tracks the byte).
- `0x0126` = the **peak** bar (0x0125 = rms, 0x0127 = hold).
- K-mode readouts are the **K-scale value** (0 dBFS shows as +20/+14/+12); convert
  back: dBFS = K-scale − 20/14/12 before tabulating.

**Validation:** the rebuilt Non-Linear curve reproduced the existing kUf1BarScale
byte-for-byte (±2) — so the method + the field-1/0x0126 choice are correct.

Distinctness (byte at −20 dBFS): Non-Linear 106 · NonLin2x 49 · Linear 138 ·
Linear2x 46 · K-20 122 · K-14 95 · K-12 113. All different → per-type tables needed
(Frank called this out: Linear/Linear2x need their own too, not just the K-modes).

## Captures (gitignored pcaps, .md siblings tracked)
cap110 Non-Linear (baseline, validates method) · cap111 K-20 · cap112 K-14 (short,
superseded) · cap113 K-12 · cap116 Linear (cap114/115 were bad: still K-12 / signal
pinned at 0 dBFS) · cap117 Linear 2x · cap118 Non-Linear 2x · cap119 K-14 (full
ramp-down — cap112 only reached −28). The 2x variants zoom the top, so their bar is
empty below ~−13/−26 dBFS by design.

## Code (`4cc9177`)
- 6 new tables `kUf1BarNonLin2x/Linear/Linear2x/K20/K14/K12[94]` next to
  `kUf1BarScale` (= Non-Linear, unchanged). Smoothed to remove peak-hold-lag
  plateaus in sparse ramp regions; endpoints + monotonicity preserved.
- `uf1BarScaleFor_(int digitalType)` maps param-6 enum (0..6) → table.
- `uf1BarByte_(dbfs, const uint8_t* tbl)` now takes the table.
- `uf1PaintMeter_` reads the PINNED instance's Digital Type (param 6) once per
  paint and drives rms/peak/hold bars off that scale.

## Open / next
- **HW-verify on the Mac** — the UF1 was on the StoerPC this session. Move it back,
  start REAPER, step Digital Type on Overview with signal: the bars should now sit
  at the right height for K-20/14/12, Linear, Linear 2x, Non-Linear 2x.
- The tables are ±a few bytes in sparse mid regions (fast ramp + peak-hold lag). If
  a scale looks off, a slower dedicated ramp for that scale tightens it.
- Still open from 2026-07-23: auto-mode default state; Loudness 4th screen.
