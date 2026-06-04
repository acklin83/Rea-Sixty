# cap73_uf1_eq_graph

**Date:** 2026-06-04
**Device:** SSL UF1 (dev 12), SSL Plugin Mode, EQ page, transport stopped.
**Action:** Frank swept HMF Gain (up/down/0), HMF Freq (low→high), and HMF Q.
**Duration:** 80 s.

## EQ graph — SOLVED. Address 0x0122 = column-height array
The EQ curve is rendered at screen element **0x0122**, sent as `FF 67 FD 01 22 <payload> <ck>`
(0xFD=253-byte frame; **payload = 251 bytes = 251 pixel columns**, one byte per column = the
curve height at that x-position).

- **Baseline (0 dB) = byte 0x64 (100)** — the zero line. (Idle/flat EQ = all columns 0x64,
  matches cap52/66 where 0x0122 was filled with 0x64.)
- **Boost** → columns around the band frequency rise above 100 (max-boost peak ≈ **187**).
- **Cut** → columns dip below 100.
- **X position** of the bell = band **frequency**; **width** = **Q**. (Freq sweep moved the bump
  horizontally; Q changed its width.)
- Value range observed ≈ 0..187 (edge columns can read 0; meaningful band ~50..187 around base 100).

ASCII render of a max-boost frame showed a clean bell curve peaking at ~187; cut showed a dip.

## Native rendering implication
To draw the EQ on the UF1 natively: compute the EQ magnitude response, map dB→pixel height
(0 dB = 100, scale up to ~187 / down below 100), emit 251 column bytes to `FF 67 FD 01 22`.

## Note
0x0122 also emits short 8-byte frames (len 16 hex) interleaved with the FD full-graph frame —
role TBD (cursor/value marker?), the FD frame is the graph itself.
