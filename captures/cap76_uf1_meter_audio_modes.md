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

## Remaining (own decode chapter, next session)
- Exact per-mode 0x0122 byte format: OVERVIEW/ANALOGUE bars, RTA spectrum (210 B), LOUDNESS.
- 0x011c dB-readout text format with real levels (field layout).
- Correlate dB readout ↔ bar bytes to derive the level→graphic mapping.
The meter STRUCTURE (addresses, modes, frame lengths) is mapped; the detailed graphic codecs remain.
