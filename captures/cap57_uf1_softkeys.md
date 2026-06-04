# cap57_uf1_softkeys

**Date:** 2026-06-04
**Device:** SSL UF1 (PID_0025, dev 11 on `\\.\USBPcap1`)
**Action:** Frank pressed the 4 soft-keys above the display, left→right, ~2 s gaps.
**Duration:** 30 s.

## Result — 4 display soft-keys (IN 0x81, FF 22 03)
| Soft-key | id |
|----------|----|
| 1 (left)  | 0x19 |
| 2 | 0x1A |
| 3 | 0x1B |
| 4 (right) | 0x1C |

Same `FF 22 03 <id> 00 <state>` button family. Contiguous with the channel soft-key:
**0x18 = channel soft-key, 0x19–0x1C = the 4 display soft-keys.**

dev-10 HID silent again.
