# cap60_uf1_navcross

**Date:** 2026-06-04
**Device:** SSL UF1 (PID_0025, dev 11 on `\\.\USBPcap1`)
**Action:** Frank pressed the NAV cross (4 arrows + centre), order Up, Down, Left, Right, Centre.
(Two stray late events ignored: 0x2A centre re-press at t=20, 0x20 Mode bump at t=22.)
**Duration:** 35 s.

## Result — NAV cross (IN 0x81, FF 22 03), ids 0x28–0x2C
| Direction | id |
|-----------|----|
| Up     | 0x28 |
| Left   | 0x29 |
| Centre | 0x2A |
| Right  | 0x2B |
| Down   | 0x2C |

Contiguous block right after the nav-button block (0x20–0x27). All `FF 22 03 <id> 00 <state>`.
