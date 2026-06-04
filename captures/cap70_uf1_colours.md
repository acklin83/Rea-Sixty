# cap70_uf1_colours

**Date:** 2026-06-04
**Device:** SSL UF1 (dev 12), SSL Plugin Mode, transport stopped.
**Action (anchored):** baseline track = **RED** (already selected). Then changed selection
through colours and back, each set via REAPER's RGB fields:
forward Green→Blue→Yellow→Cyan→White→Orange, back White→Cyan→Yellow→Blue→Green→Red.
12 change events; every colour appears twice (fwd+back) → cross-verified.
**Duration:** 120 s. (cap69 was the same idea but mis-synced; cap70 supersedes it.)

## COLOUR ENCODING — SOLVED (structure)
Each track-colour change emits, for SEL (id 0x07), two FF38 frames; the **2nd FF38 frame**
carries the colour (the FF39 2nd value is constant `0000f0`; FF39 ≠ colour here).

```
FF 38 04 07 | 00 | XX | YY | <ck>
   XX = (G4 << 4) | R4      YY = 0xF0 | B4
```
→ **4 bits per channel, order G·R·B**, with a constant 0xF nibble in YY's high position.
**Not RGB, not BGR** — answers the byte-order question.

| colour (R,G,B) | payload `00 XX YY` | G4 | R4 | B4 |
|----------------|--------------------|----|----|----|
| Red   (255,0,0)     | `00 0f f0` | 0 | F | 0 |
| Green (0,255,0)     | `00 f0 f0` | F | 0 | 0 |
| Blue  (0,0,255)     | `00 00 ff` | 0 | 0 | F |
| Yellow(255,255,0)   | `00 ef f0` | E | F | 0 |
| Cyan  (0,255,255)   | `00 f0 ff` | F | 0 | F |
| White (255,255,255) | `00 ff ff` | F | F | F |
| Orange(255,128,0)   | `00 3f f0` | 3 | F | 0 |

Pure primaries fit perfectly. G=255→E (yellow) and G=128→3 (orange) show the per-channel
8-bit→4-bit quantization is **non-linear (gamma-ish), not a plain truncation** — exact curve
TBD if needed, but structure (G·R·B nibbles) is solid and ground-truth verified fwd+back.

## Notes / open
- The **1st** FF38 frame per change varies (transient/animation?) — ignore for colour; the 2nd is final.
- This is the same FF38 mechanism as button LEDs → revisit cap64/65 LED colour bytes with this
  G·R·B-nibble model (e.g. Solo lit 0x11, Cut lit 0x12 may decode as nibble colours).
- For native output: to set element colour, send `FF 38 04 <id> 00 (G4<<4|R4) (F0|B4) <ck>`.
