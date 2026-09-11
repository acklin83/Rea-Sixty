# cap131 — UF1 Analogue screen, PPM: LED wav then needle wav, SSL 360 2.1.12 (2026-09-11)

Same rig and signals as cap130 (StoerPC, SSL 360 2.1.12.72214, Meter Pro 1.3.7,
device address 18, OUT 0x02), UF1 Meter mode, Analogue screen, **PPM**
faceplate. 110 s, 7.5 MB, 2708 frames each of 0x0125 / 0x0127 / 0x0128 / 0x011c.
Readout 0x011c shows PPM marks (4.0 at Ref, 7.0 at Ref+12, 7.7 at Ref+15),
needle rest byte 0, top 180. Overload mask: 0x0f (flash+latch, L+R) during
each Ref+12 hit, 0x0a (latch only) from the first hit to the end of the window;
the latch had been cleared between cap130 and this run.

**Why:** the PPM twin of cap130 — hold-needle fall law and LED bits on the
PPM faceplate, where our main needle already tracks (10 dB/s) but the second
needle and the LED had no source since Meter Pro 1.3.7.

**Decode:** as cap130 (`uf1_vu_needle_fit.py` pairs 0x0125 with the readout;
`uf1_hold_law.py` for 0x0127 dwell/fall; `uf1_meter_capture_analyze.py --elem 0x0128`).
