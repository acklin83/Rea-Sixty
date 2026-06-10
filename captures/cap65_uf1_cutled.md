# cap65_uf1_cutled

**Date:** 2026-06-04
**Device:** SSL UF1 (PID_0025, dev 11 on `\\.\USBPcap1`)
**Action:** Frank toggled Cut (mute) several times and **watched the physical LED**.
**Ground truth (Frank):** "an aus an aus, goes from dim red to bright red" — the Cut LED is
never fully off; it sits **dim red** when unmuted and **bright red** when muted.
**Duration:** 30 s (action near the end, t≈27–30).

## Result — LED button drive SOLVED (HW-verified 2026-06-10)
**CORRECTION (2026-06-10):** the original note below tracked ONLY the FF39 frame and got
bright/dim BACKWARDS. Native build proved it on HW (Frank: "stimmt jetzt, beide korrekt").
A button LED needs a **PAIR of frames — FF38 then FF39 — both required; FF39 alone is inert.**
This capture *does* contain the FF38 frames; the first analysis missed them.

Actual frame pairs (time-ordered, 06 = Cut):
```
ff380406003ff071   FF38 06 = 0x3f   ┐ MUTED / bright red (the LIT state)
ff3904060000f033   FF39 06 = 0x00   ┘   FF38=bright primary, FF39=0x00
ff3804060012f044   FF38 06 = 0x12   ┐ UNMUTED / dim red (resting; never fully off)
ff3904060012f045   FF39 06 = 0x12   ┘   FF38=FF39=dim colour pair
```
So the corrected encoding is:
- **LIT** (muted/soloed): `FF38 = <bright primary>` (Cut 0x3f, Solo 0xef), `FF39 = 0x00`
- **DIM/off**:            `FF38 = FF39 = <dim colour pair>` (Cut 0x12 red, Solo 0x11 green)

The earlier "0x12 = bright red" was wrong — 0x12 is the DIM resting red. Bright is 0x3f via FF38.

## FF3B role
`FF 3B 03 <led> 00 <01|00>` = per-LED enable (cap64). The init sweep enables Solo/Cut/Sel
(frames 180/183/196). The native painter re-asserts it on track-focus change because the
plugin-layout mode frames re-latch the button-LED bank. Once enabled, the FF38+FF39 pair drives
the LED. **Lesson logged: never conclude an LED protocol from a single frame type — sweep ALL
opcodes touching the led id (FF38/FF39/FF3B) before writing the driver.**
