# cap80 / cap81 — UF1 Meter modes Analogue & RTA (music)

**Date:** 2026-06-04
**Device:** SSL UF1 (dev 12), Plugin Mode → Meter, `meter_test.m4a` playing.
**cap80** = ANALOGUE mode, **cap81** = RTA mode.

## ANALOGUE (VU) mode
- **`0x0125`** (2 B, values 17..180, 136 distinct, smooth) = **VU needle L** position.
- **`0x0127`** (2 B, values 62..180, 69 distinct) = **VU needle R** position.
- `0x011c` (200 B) = text readouts (current/max for L/R).
- `0x0122` (64 B), `0x012a` = secondary graphic / state.
Needles are simple position values → cleanly self-driveable.

## RTA (31-band analyser) mode
- **`0x0122`** (64 B, values 0..183, 615×/run) = **31-band spectrum** bar heights (~2 B/band:
  current + peak). The spectrum frame.
- `0x011c` = 0 here (selected-freq readout shown elsewhere / on demand).
- `0x0009` (4 B, 0..31) varies — input/meter slot.

## All 3 meter modes now located
| mode | level graphic | numeric |
|------|---------------|---------|
| Overview | `0x0122` Lissajous scope (251 B) + `0x0126` balance + `0x0127` correlation | `0x011c` T PEAK/RMS |
| Analogue | `0x0125` VU needle L + `0x0127` VU needle R (position) | `0x011c` current/max |
| RTA | `0x0122` 31-band spectrum (64 B) | selected-freq readout |

Addresses + formats are decoded. Exact byte→dB / byte→position scaling per mode STILL needs a
level-sweep capture — and that is CRITICAL work, not optional: the meter/analyzer display is the
main UF1 selling point, so we match SSL's real graphic byte-for-byte. The Meter view is 100%
SSL-plugin-gated (Frank 2026-07-02); REAPER self-rendering is DEFERRED. See memory
`uf1-meter-analyzer-display`.
