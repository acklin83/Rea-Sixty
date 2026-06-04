# cap59_uf1_navbuttons

**Date:** 2026-06-04
**Device:** SSL UF1 (PID_0025, dev 11 on `\\.\USBPcap1`)
**Action:** Frank pressed the 8 soft-buttons beside the channel encoder in order:
Mode, 5-8, ←, →, Bank Left, Bank Right, 360, Scrub. ~2 s gaps.
**Duration:** 45 s.

## Result — 8 nav buttons (IN 0x81, FF 22 03), ids 0x20–0x27
| Button | id |  | Button | id |
|--------|----|--|--------|----|
| Mode | 0x20 |  | Bank Left  | 0x21 |
| 5-8  | 0x22 |  | Bank Right | 0x23 |
| ←    | 0x24 |  | 360        | 0x25 |
| →    | 0x26 |  | Scrub      | 0x27 |

Even ids = top row (Mode/5-8/←/→), odd ids = bottom row (Bank L/Bank R/360/Scrub).
All `FF 22 03 <id> 00 <state>`. Scrub = plain button (0x27), does NOT by itself engage any
jog/HID traffic. dev-10 HID silent throughout.
