# cap121 — Loudness history, Scale Range −36..+18 (param 47 = 0.5)

2026-07-24, StoerPC USBPcap3, SSL Meter Pro, SSL 360 driving the UF1. Signal ramped
loud→quiet. History line = 0x0122 sub-frame 1. Fit: **slope 3.33 byte/LU (=180/54),
byte180 @ ST −13.6, byte0 @ ST −67.5**, span 54 LU. Confirms the same offset
(axisLU = LUFS + 31.6) and the unified law. See
`docs/session-2026-07-24-uf1-loudness-capture.md`.
