# cap83 — UF1 Timecode display

**Date:** 2026-06-04
**Device:** SSL UF1 (dev 12), **MCU/DAW layer** (REAPER MCU configured), transport playing from bar 110.

## Key fact (Frank)
**Timecode (measures/beats) is only shown in the MCU layer.** In SSL Plugin Mode the
measures/beats field stays empty (timecode comes via HUI/MCU, not the 360° plugin protocol).
So to drive a TC display natively we'd need to feed it from REAPER's transport in the MCU-layer
context (or replicate it). In this capture the view is the DAW/MCU layer: 0x011c = "FADER SEL …
N/10", 0x0104 = "F1…F8" / "SMPTE/BEATS".

## Timecode = address 0x0119, 7-segment encoded
- **`0x0119`** (11-byte payload) = the timecode digit display. 121 distinct values in 125 frames
  (changes ~every frame = running TC). Layout: `00 00 00 00 | d d d d d | 00 00` — middle 5 bytes
  are the visible digits, leading/trailing zeros = blank digit positions.
- **Per-digit 7-segment encoding** (one byte = one digit's segments). At bar "110…": middle bytes
  `0c 0c 7f …` (so `0x0c`='1'; `0x7f`/`0x7e` ≈ '0'); the last two middle bytes run fast = ticks.
- `0x0104` includes "SMPTE/BEATS" = the soft-key that toggles TC format.
- Exact segment-bit→digit map: derive by correlating known displayed TC values (polish).

## Status
Timecode LOCATED (0x0119, 7-seg). Gap item #4 done (located; exact digit map = polish).
Only-in-MCU-layer is an important constraint for native TC.
