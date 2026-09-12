# cap138 — UF1 Meter view, entry into the Analogue screen, SSL 360 2.1.12 (2026-09-12)

**What:** 60 s USBPcap3 window, StoerPC, SSL 360 2.1.12 + Meter Pro 1.3.7, REAPER playing
with signal. Frank switched the UF1 to the Analogue screen at 11.6 s and left it there.
Device address 27, OUT 0x02. 4.0 MB, 12158 FF frames: 11718 FF67, 400 FF1B, 40 LED frames.
Readout on Analogue reached +14.4 (values -inf .. 14.4).

**Why:** (1) the readout text on our stream went red at high level while it was believed
SSL's stayed white; Frank at the device during this window: *"scheint bei der neuen
firmware auch so zu sein, dass zu hohe pegel in rot daher kommen"* — the red is the
firmware's own high-level tint, present under SSL 360 too. Hunt closed. (2) The first
capture of a 2.1.12 Analogue ENTRY; every earlier burst came from cap76 (360 2.0.6).

**The 2.1.12 Analogue entry, in order (11.563..11.604 s):**
LED frames first (soft keys 0x0e, 0x02, 0x03, 0x04, 0x08, 0x01: FF38/FF39/FF3B), then
`0100=0401, 0101=03, 0102=01, 0104 x4 (ANALOGUE/RESET/FINE/PRESETS), 0110=0f,
010d=0a0a0b0b, 010e x4 (slot 0 EMPTY, VU, 0dBu/0 dBu, Ref/-18.0dB), 011a=02, 011e=10,
0120=00, 0129=ff, 011f=00`. No 0x00xx, no 0x0128, no 0009 group in the entry.
Steady stream afterwards: 0009/000a/0015/0016/011c/0125/0127/0128 at 40 ms, 0x0119
(time field) at ~12 Hz while playing, 0x0102 twice — identical to cap130.

**Consumed by:** the Analogue burst in `uf1MeterScreenBurst_` (main.cpp) is now this
sequence byte for byte; `0x011e` on the meter screens is back to the measured 0x10.

**Decode:** `python3 analysis/uf1_meter_capture_analyze.py captures/cap138_uf1_2112_meter_entry_analogue.pcap --dev 27 --timeline`
