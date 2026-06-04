# cap76_uf1_meter_audio_modes

**Date:** 2026-06-04
**Device:** SSL UF1 (dev 12), Plugin Mode, Meter view, **white noise playing**.
**Action:** cycled meter modes ANALOGUE → RTA → LOUDNESS → OVERVIEW (MODE/soft-keys), ~5 s each.

## Meter view structure (with audio)
- **Mode** read from `0x0104` first soft-key label: ANALOGUE / RTA / LOUDNESS / OVERVIEW
  (other soft-keys: RESET, FINE/PLAY, PRESETS).
- **`0x011c`** = per-channel dB readouts (animated; `-inf` when silent, real values w/ audio).
- **`0x010e`** = meter settings (MASTER, TruePk, Non-Linear, Fade…).
- **`0x0122`** = the meter graphic, animated ~600 frames/s with audio. **Length varies by mode:**
  | frame | bytes | mode |
  |-------|-------|------|
  | `FF67 FD 0122` | 253 (251 payload) | main bars (OVERVIEW / ANALOGUE) |
  | `FF67 D2 0122` | 210 | RTA spectrum |
  | `FF67 42 0122` | 66 | (LOUDNESS?) |
  | `FF67 3F 0122` | 63 | compact (static meter view, cap75) |

## Graphic byte semantics differ from the EQ graph
OVERVIEW bar frame (251 B) with white noise: values only `{0, 3, 14, 15}` (NOT 0..187 heights
like the EQ graph). Looks like a packed segment/bitmap encoding, **per-mode specific**.

## Overview mode element breakdown (frame-split, white noise)
- **`0x011c`** = the 6 numeric readouts: T PEAK (value/max/current) + RMS (value/max/current),
  e.g. `-5.9 | -6.7 | -6.7 | -16.4 | -16.5 | -16.5`. **This is the actionable meter data.**
- **`0x0122`** (251 B, values 0..48, ~600/s) = the dominant animated graphic = Lissajous scope
  (+ likely L/R bargraphs packed). Sparse with white noise (scatter cloud).
- **`0x0126` / `0x0127`** (2 B, ~160..164) = L-R balance bar / phase-correlation position.
- `0x0125` = meter-instance label; 0x0009/a/0015/0016 = idle here.

## Remaining (own chapter; LOW priority — can self-render)
Exact per-mode `0x0122` pixel codec (Overview scope+bars, Analogue VU needle, RTA 31-band) needs a
level-SWEEP capture per mode to map level→bytes. **But for native output we can render our own
meters from REAPER metering (like the EQ graph), so matching SSL's exact pixel codec is optional
polish.** The meter NUMERIC readouts (0x011c) + structure are decoded — enough to drive a meter.
