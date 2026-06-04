# cap61_uf1_jog

**Date:** 2026-06-04
**Device:** SSL UF1 (PID_0025, dev 11 on `\\.\USBPcap1`)
**Action:** Frank turned the jog wheel — **CCW first, then CW** (stated).
**Duration:** 30 s.

## Two key results

### 1. Jog wheel = encoder-family id 0x06 (vendor protocol, NOT HID)
`FF 24 02 06 <delta> <ck>` — same FF24 rotation opcode as the V-pots/channel encoder.
Encoder id sequence is now: 0x00–0x04 V-pots, 0x05 channel encoder, **0x06 jog wheel**.
No push/touch frames for the jog (plain rotary). Velocity-sensitive (±2 when spun faster).

### 2. dev-10 HID stayed SILENT during jog
The jog wheel does NOT use the HID composite (PID_0022). Combined with all prior captures:
**no UF1 control tested so far uses dev-10 HID** — fader, all buttons, all encoders, jog are
all on the PID_0025 vendor protocol. dev-10 role still unknown (OS media keys? unused?).

## Direction sign — CONFIRMED for the whole FF24 encoder family
CCW-first then CW, clean transition at t≈8.0 s:
- CCW phase: delta `0x3F` (-1), `0x3E` (-2 when faster)
- CW phase: delta `0x01` (+1), `0x02` (+2 when faster)

→ **CW = positive, CCW = negative**, 6-bit signed mod 0x40. This resolves the earlier
"absolute CW/CCW sign TBD" note — applies to V-pots, channel encoder, and jog alike.
