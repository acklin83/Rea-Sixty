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

## RESOLVED 2026-06-10 (native build, HW-verified)
The "counter-intuitive val 0x00 vs 0x11/0x12" confusion was because this analysis tracked only
FF39 and ignored the **FF38 companion frame**. A button LED needs the PAIR (FF38 then FF39).
The `val 0x00`-on-"on" frames were the LIT state (FF38=bright primary 0xef/0x3f, FF39=0x00); the
`0x11/0x12` frames were the DIM resting state (FF38=FF39=dim pair). See cap65 for the corrected
table. Solo lit = FF38 0xef / FF39 0x00; Solo dim = 0x11/0x11. Cut lit = FF38 0x3f / FF39 0x00;
Cut dim = 0x12/0x12 (dim red, never fully off). led id 0x1C moving alongside Solo: still
unexplained (likely screen-refresh coincidence), harmless.
