# cap56_uf1_vpots

**Date:** 2026-06-04
**Device:** SSL UF1 (PID_0025, dev 11 on `\\.\USBPcap1`)
**Action:** Frank worked all 5 V-pots in order — V-pot above the fader, then V-pot 1,2,3,4 —
each: rotate back-and-forth, then push. ~2 s gaps.
**Duration:** 50 s.

## V-pots fully decoded (IN 0x81). Timeline maps cleanly to the 5 pots.
| Function | frame | ids |
|----------|-------|-----|
| **Rotation** | `FF 24 02 <id> <delta> <ck>` | id 0x00–0x04 |
| **Touch** (capacitive) | `FF 23 02 <id> <state>` | id 0x00–0x04, 01=touch / 00=release |
| **Push** | `FF 22 03 <id> 00 <state>` | id 0x08–0x0C (same FF22 button family) |

### V-pot id ↔ physical
| V-pot | rot/touch id | push id |
|-------|--------------|---------|
| above fader | 0x00 | 0x08 |
| 1 | 0x01 | 0x09 |
| 2 | 0x02 | 0x0A |
| 3 | 0x03 | 0x0B |
| 4 | 0x04 | 0x0C |

### Rotation delta encoding
Delta byte values observed: only `0x01, 0x02, 0x3E, 0x3F` → **6-bit signed (mod 0x40)**:
`0x01`=+1, `0x02`=+2, `0x3F`=-1, `0x3E`=-2. One direction = positive small, other = 0x3F down.
No value ≥0x40 seen. **Absolute CW/CCW sign (which physical direction is +) = confirm at
implementation** (turn direction wasn't logged here). Relative encoder, not absolute.

V-pots are **touch-sensitive** like the fader (FF23 touch frames bracket each rotation).

## Encoder family summary so far
- Fader: touch FF20 / pos FF21 (read), engage FF1D / pos FF1E (motor)
- V-pot: rotate FF24 / touch FF23 / push FF22
- Buttons: FF22 03 (shared by channel buttons AND v-pot pushes; id space distinguishes them)
