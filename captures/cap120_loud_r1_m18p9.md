# cap120 — Loudness history, Scale Range −18..+9 (param 47 = 0.0)

2026-07-24, StoerPC USBPcap3, SSL Meter Pro on track "meter_test", SSL 360 driving
the UF1 (dev 28). Signal ramped loud→quiet (~50 s), Short-Term swept −0.8 → −60 LUFS.

History line = **0x0122 sub-frame 1** (250 cols, byte = plot Y 0..180). Fit (newest
col ↔ Short-Term readout): **slope 6.66 byte/LU (=180/27), byte180 @ ST −22.6,
byte0 @ ST −49.6**, span 27 LU. See `docs/session-2026-07-24-uf1-loudness-capture.md`
for the unified 3-range law. (cap95's "sf3 = history" was the wrong sub-frame; sf1 is.)
