# cap58_uf1_chan_encoder

**Date:** 2026-06-04
**Device:** SSL UF1 (PID_0025, dev 11 on `\\.\USBPcap1`)
**Action:** Frank turned the **channel encoder** back-and-forth, then pushed it.
**Duration:** 30 s.

## Result — channel encoder = encoder-family id 0x05
Same FF24/FF23/FF22 encoder family as the V-pots, IDs continue the sequence:
| Function | frame | id |
|----------|-------|----|
| Rotation | `FF 24 02 05 <delta> <ck>` | 0x05 |
| Touch | `FF 23 02 05 <state>` | 0x05 (touch-sensitive) |
| Push | `FF 22 03 0D 00 <state>` | 0x0D |

Delta: 6-bit signed (0x01=+1, 0x3F=-1, 0x3E=-2, 0x02=+2) — same as V-pots.

### Encoder id space (consolidated)
- rot/touch ids: 0x00–0x04 = V-pots (above-fader + 1..4), **0x05 = channel encoder**
- push ids: 0x08–0x0C = V-pot pushes, **0x0D = channel encoder push**

## Incidental: fader motor-echo
207 `FF 21` fader-position frames arrived while turning the channel encoder (no fader touch).
Turning the encoder changed the selected channel → REAPER repositioned the fader motor
(FF1E OUT) → the physical fader moved → position sensor echoed FF21 IN. Confirms the channel
encoder drives channel navigation, and that fader motor moves produce read-side echo
(same class as the UF8 motor-echo).
