# cap62_uf1_transport_softkeys

**Date:** 2026-06-04
**Device:** SSL UF1 (PID_0025, dev 11 on `\\.\USBPcap1`)
**Action:** Frank pressed the 7 soft-keys above the transport, order:
Left, Right, Cycle, Click, 1, 2, Shift. ~2 s gaps.
**Duration:** 40 s.

## Result — 7 transport soft-keys (IN 0x81, FF 22 03), ids 0x30–0x36
| Soft-key | id |  | Soft-key | id |
|----------|----|--|----------|----|
| Left  | 0x30 |  | 1 | 0x34 |
| Right | 0x31 |  | 2 | 0x35 |
| Cycle | 0x32 |  | Shift | 0x36 |
| Click | 0x33 |  |  |  |

Contiguous block 0x30–0x36, all `FF 22 03 <id> 00 <state>`.
