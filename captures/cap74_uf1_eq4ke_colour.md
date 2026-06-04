# cap74_uf1_eq4ke_colour

**Date:** 2026-06-04
**Device:** SSL UF1 (dev 12), Plugin Mode, 4K E EQ, transport stopped.
**Action:** boosted HMF Gain (4K E curve), then toggled the **EQ Colour** option a few times.

## EQ graph colour = FF38 GRB at element id 0x03
Toggling "EQ Colour" changes **`FF 38 04 03 00 XX YY <ck>`** (+ matching FF39), i.e. the **same
4-bit GRB colour mechanism** (XX=(G<<4)|R, YY=0xF0|B) as SEL/LED colour — just a different element
id (**0x03** = the EQ graph/band colour element).

Observed values (XX YY): `ef f0` (G=E,R=F,B=0 → amber), `55 f0`, `11 f0`, `00 f0` (darker→off).
Exact EQ-Colour-option → value mapping not fully pinned (toggle states), but the mechanism is clear.

## Takeaway — colour is ONE universal mechanism
`FF 38 04 <element-id> 00 (G4<<4|R4) (F0|B4)` sets colour for ANY addressable element:
- button LEDs, SEL track colour (id 0x07), **EQ graph (id 0x03)**.
Confirms cap70's GRB encoding is general, not SEL-specific.

## 4K E EQ curve
Same `0x0122` 251-column height-array mechanism as 4K B (cap73) — only the computed curve shape
differs (SSL EQ-type DSP). No protocol difference. (Curve-shape quantification not pursued;
relevant only for future native EQ-graph rendering.)
