# cap55_uf1_buttons

**Date:** 2026-06-04
**Device:** SSL UF1 (PID_0025, dev 11 on `\\.\USBPcap1`)
**Action:** Frank pressed 6 channel/fader-strip buttons in order: Soft-Key(above channel),
Solo, Cut, Sel, Flip, Master — restarted after the 3rd, second run went cleanly through all 6.
(Originally mis-stated the 1st as "Track V-Pot push" — Frank corrected: it was the SOFT-KEY
above the channel. V-Pot push NOT yet captured.)
**Duration:** 40 s.

## Button events (IN 0x81)
Format: `32 60 | FF 22 03 | <id> | 00 | <state> | <ck>` — **same FF 22 03 opcode as UF8.**
state `01`=down, `00`=up. Checksum = `(sum(FF..state)+1)&0xFF`.

Reconstructed by timestamp (matches stated order):
| Button | event id |
|--------|----------|
| Soft-Key (above channel) | **0x18** |
| Solo | **0x1D** |
| Cut  | **0x1E** |
| Sel  | **0x1F** |
| Flip | **0x38** |
| Master | **0x39** |

## Button LEDs (OUT 0x02), triggered by the presses
- **`FF 3B 03 <led_id> 00 <state>`** = LED on/off (`01`/`00`). Saw led_ids 0x05,0x06,0x07,0x1c,0x20,0x21.
- **`FF 39 04 <led_id> 00 <val>`** = LED colour/level (4-byte, vals 0x11/0x12…).
- LED id space ≠ button-event id space (separate addressing, like UF8 LED cells).
- Exact led_id ↔ button mapping = TODO (needs per-button LED correlation).

## Parser note — IN stream is concatenated frames
A button UP arrived coalesced with a fader frame in ONE URB read:
`3260 ff210300714ce1 ff22033800005d`. So after the `32 60` prefix the IN payload is a
**byte stream of back-to-back `FF…` frames**; split on the length byte (`FF <op> <len> …`),
do NOT assume one frame per USB read.

## dev-10 HID (PID_0022)
**No HID traffic during button presses** → fader-strip buttons use the vendor protocol, not HID.
HID-composite role still unknown.
