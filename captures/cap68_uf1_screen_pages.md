# cap68_uf1_screen_pages

**Date:** 2026-06-04
**Device:** SSL UF1 (PID_0025, **dev 12** on `\\.\USBPcap1`), **SSL Plugin Mode**, transport stopped.
**Action (narrated):** baseline → paged forward through all 8 pages (Right=→) → paged back
(Left=←) → selected a red track → a blue track → back to red.
**Duration:** 90 s.

## Paging — SOLVED
In **Plugin Mode** the main text row `0x011c` reads `REAPER | N/8 | OFF` (field0="REAPER",
field1=page "N/8", field4="OFF"). Paged 1/8…8/8 and back cleanly. **Paging control = Left/Right
nav buttons** (← id 0x24 / → id 0x26, the keys after Mode & "5-8" above the jog wheel).
(The "1/10" DAW-mode pager is a different mode; Plugin Mode = N/8.)
Per-page *content* lives in the 0x00xx param-zone + 0x0104 soft-key-label addresses, not 0x011c.

## Colour — LOCATED, encoding NOT yet solved
Track colour (SEL element, id 0x07) is carried by the **same FF38/FF39 frames as LEDs**,
element-addressed, sent **only on change** (not every cycle). Reproducible:
| track | FF38/FF39 id 0x07 payloads (3 bytes each, a pair) |
|-------|---------------------------------------------------|
| red  (t=38.4, 63.5) | `00 01 f0` + `00 f0 ff` |
| blue (t=43.1)       | `00 10 f1` + `00 0f f0` |

Nibble symmetry red↔blue (`01↔10`, `f0↔0f`) suggests a hue/palette index, not raw RGB
(a 3-byte field couldn't hold REAPER's full RGB anyway → quantised, like the UF8 SEL palette).
**Encoding unsolved — needs a multi-colour sweep (pure R/G/B/Y/C/W tracks) to map the bytes.
Do NOT guess.**

## Structural insight
LED state AND screen-element colour both go through **FF38 + FF39** (id-addressed), with FF3B
for on/off. So "LED colour" and "screen SEL colour" are one mechanism. The init LED sweep
(cap66) sets every element's FF38/FF39/FF3B. This also means the earlier cap64/65 LED bytes
should be re-read in this FF38+FF39-pair light.
