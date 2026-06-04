# cap65_uf1_cutled

**Date:** 2026-06-04
**Device:** SSL UF1 (PID_0025, dev 11 on `\\.\USBPcap1`)
**Action:** Frank toggled Cut (mute) several times and **watched the physical LED**.
**Ground truth (Frank):** "an aus an aus, goes from dim red to bright red" — the Cut LED is
never fully off; it sits **dim red** when unmuted and **bright red** when muted.
**Duration:** 30 s (action near the end, t≈27–30).

## Result — LED brightness byte SOLVED (for Cut)
Only `FF 39 04 06 00 <val>` is sent to toggle (no FF3B needed). val sequence: 00,12,00,12,00.
Correlated to ground truth (leading 0x00 = initial dim state):
- **`val 0x12` = bright red** (muted / "on")
- **`val 0x00` = dim red** (unmuted / "off")

## Re-interpretation of cap64 + hypothesis
cap64's Solo-LED lit value was 0x11, Cut's is 0x12 → the `val` **low nibble looks like a colour
index** (1 = green/Solo, 2 = red/Cut), `0x00` = dim/inactive, high nibble 0x1 = lit/bright.
i.e. `val = 0x10 | colourIndex` when bright, `0x00` when dim. **HYPOTHESIS** — confirm by
sweeping a multi-colour LED through its colours. Dim-vs-bright itself is confirmed by ground truth.

## Note
`FF 3B 03 <led> 00 <on/off>` (seen in cap64) vs `FF 39 04 <led> 00 <val>` (here): the brightness/
colour lives in FF39. FF3B's exact role (initial enable?) still loose — watch in future LED caps.
