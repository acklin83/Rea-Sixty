# cap64_uf1_leds_scs

**Date:** 2026-06-04
**Device:** SSL UF1 (PID_0025, dev 11 on `\\.\USBPcap1`)
**Action:** Frank toggled Solo / Cut on a track via the HW buttons; **Sel toggled manually via
REAPER track selection** (selecting a track in REAPER always lights UF1 Sel, so it can't be
cleanly toggled from the surface alone). Watching OUT 0x02 LED frames.
**Duration:** 30 s.

## Result — Solo/Cut/Sel LED ids CONFIRMED
| Button | event id | **LED id** |
|--------|----------|------------|
| Solo | 0x1D | **0x05** |
| Cut  | 0x1E | **0x06** |
| Sel  | 0x1F | **0x07** |

LED frames on OUT 0x02:
- `FF 3B 03 <led_id> 00 <01|00>` = on/off
- `FF 39 04 <led_id> 00 <val>` = colour/level

## OPEN — colour/level byte semantics NOT yet solved
Observed `val` bytes: `0x00`, `0x11`, `0x12`. Counter-intuitive in this capture
(button "on" emitted val 0x00, "off" emitted 0x11/0x12) and entangled with REAPER's own
solo/select state — do NOT guess the encoding. Needs a clean capture that drives a single LED
to known colours/brightness. led id 0x1C also moved alongside Solo (unexplained; maybe screen
refresh coincidence). **TODO next LED session:** isolate one LED, sweep colour/brightness.
