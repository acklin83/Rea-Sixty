# cap52_uf1_baseline_idle

**Date:** 2026-06-04
**Device:** SSL UF1 (VID_31E9, PID_0025 "SSL Control I/F", serial UF1-009184)
**Interface:** `\\.\USBPcap1` (ASMedia USB 3.1 xHCI root hub), whole-interface capture
**Duration:** 10 s, SSL 360 v2.0.6.x driving UF1, REAPER open, **no user input** (steady-state baseline)
**Captured by:** Claude over SSH (`tshark -i \\.\USBPcap1 -a duration:10`)

## Why
First UF1 capture. Smoke-test the SSH→tshark→scp pipeline AND get the steady-state
endpoint map + frame mix before any labelled control capture. Subtractive decode plan:
account for known controls, screen is the leftover.

## USBPcap1 device map
- `[8] Generic USB Hub` → `[9] Generic USB Hub` (both internal, Microchip VID_0424 PID_2422)
  - `[10] USB Input Device` = **PID_0022 HID composite** (mouse+keyboard+consumer). Idle = silent.
  - `[11] Solid State Logic UF1` = **PID_0025 control IF**. The vendor protocol.
- [10] and [11] share the **same parent hub** (`VID_0424&PID_2422\7&2ae38c01&0&1`) → both inside the UF1 chassis.

## Endpoints (device 11, control IF) — all BULK
| EP | Dir | pkts/10s | bytes | role |
|----|-----|----------|-------|------|
| 0x02 | OUT (host→UF1) | 3122 | 61838 | screen + LEDs + keepalive |
| 0x81 | IN (UF1→host) | 10097 | ~10k (≈1 B/pkt) | control input — idle = empty polls |

**Only one OUT + one IN bulk endpoint.** The colour screen is NOT on its own endpoint —
it is multiplexed onto 0x02 OUT with LEDs/keepalive (same single-OUT design as UF8).
→ "endpoint separation isolates screen" does NOT apply; use elimination-by-decoding +
audio-playback diff.

## OUT frame mix (idle steady-state loop, FF 67 family)
Six frames repeat 249× each in 10 s (~25 Hz refresh loop) + FF 1B keepalive:
- `ff 67 06 00 09 ff ff 00 00 74`
- `ff 67 06 00 0a 00 00 00 00 77`
- `ff 67 03 00 15 ff 7e`
- `ff 67 03 00 16 ff 7f`
- `ff 67 ca 01 1c <202B payload> da`  ← **screen frame**: payload is ASCII (`2d`='-', `4f4646`="OFF")
- `ff 67 03 01 1d 11 99`
- `ff 1b 01 0x xx` keepalive (4 variants, ~17× each) — **same FF 1B 01 keepalive as UF8**

### Frame structure (working hypothesis, UNVERIFIED)
`FF 67 <len> <b3> <b4> <payload…> <ck>` — byte after 67 = payload length
(06, 03, 0xCA=202). Last byte = checksum. The big `FF 67 CA` frame = one screen text row
(contains "OFF" + dashes). High-level text/layout, not raw framebuffer → screen decodable.

## Next
Labelled control captures (Frank drives hardware), fader first (strongest UF8 analog).
