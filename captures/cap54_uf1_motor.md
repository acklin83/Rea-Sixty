# cap54_uf1_motor

**Date:** 2026-06-04
**Device:** SSL UF1 (PID_0025, dev 11 on `\\.\USBPcap1`)
**Action:** Frank dragged the **REAPER** track fader up/down with the mouse; UF1 fader motor
followed. Hardware fader NOT touched (no read-side FF20/FF21 mixed in).
**Duration:** 30 s.

## Result — UF1 fader MOTOR (write side) decoded

OUT endpoint **0x02**, new frames (absent from idle baseline):
- **`FF 1D 02 00 <01|00>`** = motor **engage / release**. `01` = engage (one per position
  update, 263×), `00` = release (3× at end of a move).
- **`FF 1E 03 00 <lo> <hi> <ck>`** = motor **position**, **15-bit LE, 0x0000…0x7FFF** —
  same scale as the read-side FF21. Range this capture: 0x06C8 … 0x7F4E.
- Checksum = `(sum(FF,1E,03,00,lo,hi)+1)&0xFF` — same formula as read side, verified on all 263.
- `FF 67 0B…` / `FF 67 0D…` on OUT = screen value-readout updating as the fader moves.

Pattern per motor tick: `FF 1D 02 00 01` (engage) + `FF 1E 03 00 <pos>` (position).

## Fader fully decoded — both directions
| Dir | engage/touch | position |
|-----|--------------|----------|
| Read (UF1→host, EP 0x81) | `FF 20 02` | `FF 21 03 00 <15-bit LE>` |
| Motor (host→UF1, EP 0x02) | `FF 1D 02 00 01/00` | `FF 1E 03 00 <15-bit LE>` |

15-bit LE 0x0000 (bottom) … 0x7FFF (top), same scale both ways, same checksum.

## vs UF8
Same *concept* (touch/limp + position + motor) but **different bytes**: UF1 uses FF 1E/1D/21/20
and a clean 15-bit 0x7FFF-max value; UF8 uses pitch-bend / 14-bit (pb14_max=15583). UF8 fader
code is NOT directly reusable for UF1 — new opcodes + value scale needed.
