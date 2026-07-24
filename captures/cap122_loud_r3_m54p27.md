# cap122 — Loudness history, Scale Range −54..+27 (param 47 = 1.0)

2026-07-24, StoerPC USBPcap3, SSL Meter Pro, SSL 360 driving the UF1. Signal ramped
loud→quiet. History line = 0x0122 sub-frame 1. Fit: **slope 2.22 byte/LU (=180/81),
byte180 @ ST −4.6, byte0 @ ST −85.7**, span 81 LU. Third confirmation of the unified
law (byte = clamp((axisLU − rangeBottom)·180/span, 0, 180); axisLU = LUFS + 31.6).
See `docs/session-2026-07-24-uf1-loudness-capture.md`.
