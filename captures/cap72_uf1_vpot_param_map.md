# cap72_uf1_vpot_param_map (+ cap71 page sweep)

**Date:** 2026-06-04
**Device:** SSL UF1 (dev 12), SSL Plugin Mode, **SSL Channel Strip focused**, transport stopped.
**cap71:** paged through all 8 pages and back (no V-pot turning).
**cap72:** per page, wiggled each V-pot 1→4 back-and-forth, all 8 pages.

## Plugin-data screen model — FOCUS-BASED (not a 4-column layout)
Correlating V-pot rotation (`FF24 02 <id>` IN) vs screen-address changes (FF67 OUT): on **every
page, all 4 V-pots correlate with the same address `0x010e`**. So the named readout is the
**focused** parameter (whichever V-pot you touch), not 4 separate named slots.

Screen element roles (SSL Channel Strip, Plugin Mode):
- **`0x010e`** (len 0x16=22) = **focused parameter readout**: name (left) + value (right), e.g.
  `In Trim       0.0dB`, `LMF Freq    1.50kHz`. Shared by all V-pots (follows focus).
- **`0x0104`** (len 0x0d=13) = section / soft-key label (e.g. `S/C LISTEN`, `HQ MODE`, `A/B`, `EQ`, `DYN`).
- **`0x010f`** (len 0x0a=10) + **`0x0122`** (FD, 253 B) = **value-bar graphics** — these carry the
  "up to 4" V-pot value indicators shown simultaneously (graphic, not text).
- `0x011c` = static header `REAPER | N/8 | OFF` (page indicator at offset 75).

## Full SSL Channel Strip parameter set (as the UF1 prints it, from 0x010e)
In Trim · High Pass · Low Pass · LF Gain · LF Freq · LMF Gain · LMF Freq · LMF Q ·
HMF Gain · HMF Freq · HMF Q · HF Gain · HF Freq · Ratio · Threshold · Release · Range ·
Threshold · Release · Width · Mic · Out Trim · Mix
(values formatted e.g. `8.1dB`, `200.0Hz`, `1.50kHz`, `10:1`, `auto`, `100.0%`, `OUT`).

## Open / next
- The per-V-pot **value-bar bytes** inside `0x010f` (10 B) / `0x0122` (253 B graphic) — map which
  bytes = which of the up-to-4 V-pot bars (turn ONE v-pot, watch which bytes move).
- If Frank means 4 *named* params show at once in some view, it's not in this Channel-Strip
  protocol path (only one named readout 0x010e seen) — revisit if a different view does it.
