# cap75_uf1_meter_view_nudge

**Date:** 2026-06-04
**Device:** SSL UF1 (dev 12), Plugin Mode, **no audio**, transport stopped.
**Action:** toggled Channel ↔ Meter view with the **MODE button (id 0x20)** ~5 times.

## Meter view static layout (vs Channel view)
Same element addresses, **re-purposed by view context**:
- **`0x011c`** (main row): in Meter view = **per-channel dB readouts**, shows `-inf ... -inf ...`
  ×6 when silent (the meter level numbers). (Channel view = `REAPER | N/8 | OFF`.)
- **`0x0122`**: in Meter view becomes a **63-byte** frame = the **meter-bar graphic** (vs 253-byte
  EQ-graph in Channel view). Same address, different element by context.
- **`0x0104`** = meter soft-keys: `OVERVIEW`, `RESET`, `FINE`, `PRESETS`.
- **`0x010e`** = meter settings: `MASTER`, `TruePk..Off`, `Non-Linear`, `Fade..2 sec`.
- **`0x0125 / 0x0126 / 0x0127`** (len 4) = new in meter view — likely per-meter values (TBD).

MODE (0x20) toggles the view; we read the view from these address contents.

## Next: audio
With audio playing, the `0x011c` dB readouts (−inf → real levels) and the `0x0122` 63-byte bar
graphic will animate → decode the meter value/bar format. (This is the SSL Meter / Meter Pro view.)
