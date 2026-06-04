# cap53_uf1_fader

**Date:** 2026-06-04
**Device:** SSL UF1 (PID_0025, dev 11 on `\\.\USBPcap1`)
**Action:** Frank pulled the **single UF1 fader** slowly full up → full down, one pass (no other control touched).
**Duration:** 30 s window.

## Result — UF1 fader fully decoded (read side)

IN endpoint **0x81**, frame layout:
```
32 60 | ff 21 03 00 | <lo> <hi> | <ck>
prefix   position-op   15-bit LE    checksum
```

- **`32 60`** = constant 2-byte IN-report prefix. Idle polls = bare `32 60`.
  (Occasionally `32 00` seen — second byte may be a touch/seq status nibble; UNVERIFIED.)
- **`FF 20 02 00 …`** = fader **TOUCH** (analogous to UF8 `FF 20 02`). Touch-down at t=1.689.
- **`FF 21 03 00 <lo> <hi> <ck>`** = fader **POSITION**. `len=03` → 3 data bytes `00 lo hi`.
  Value = `hi*256 + lo`, **15-bit, 0x0000 = bottom … 0x7FFF = top**.
  Observed range this pass: min `0x0034`, max `0x7FD8` (≈ full travel).
- **Checksum** = `(sum(FF,21,03,00,lo,hi) + 1) & 0xFF`. Verified on all frames.
  (i.e. running sum seeded at 1 over the payload from the FF opcode byte.)

Position frames arrive ~every 10 ms while moving (~100 Hz). No FF21 when static.

## Deltas vs UF8
- UF8 fader position used a pitch-bend-style frame; **UF1 uses FF 21 + plain 15-bit LE**.
- UF8 has 8 faders (0-indexed strip byte). **UF1 has exactly ONE fader** → the `00` after
  `FF 21 03` is presumably the fader index (always 0). Confirm if any control reuses FF21.

## Open / next
- Motor-drive (write side): capture SSL 360 moving the fader (track select / automation)
  to find the OUT frame on 0x02 that sets motor position. Likely FF 21-ish on OUT.
- `32 60` vs `32 00` prefix meaning.
