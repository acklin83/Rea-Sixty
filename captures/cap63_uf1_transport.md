# cap63_uf1_transport

**Date:** 2026-06-04
**Device:** SSL UF1 (PID_0025, dev 11 on `\\.\USBPcap1`)
**Action:** Frank pressed the transport buttons left→right: RWD, FFW, Stop, Play, Rec.
**Duration:** 30 s.

## Result — transport buttons (IN 0x81, FF 22 03), ids 0x3A–0x3E
| Button | id |
|--------|----|
| RWD  | 0x3A |
| FFW  | 0x3B |
| Stop | 0x3C |
| Play | 0x3D |
| Rec  | 0x3E |

Contiguous with Flip(0x38)/Master(0x39) → full block 0x38–0x3E.
(Late stray 0x3C at t=21 = Stop again, ignored.)

**Input side complete.** All UF1 controls now decoded.
