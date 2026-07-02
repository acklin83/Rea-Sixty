# cap82 — UF1 Meter Loudness mode (Meter Pro; music)

**Date:** 2026-06-04
**Device:** SSL UF1 (dev 12), Plugin Mode → Meter → **LOUDNESS** (not in Rev4.0 manual → Meter Pro),
`meter_test.m4a` playing.

## Loudness mode — decoded
- **`0x011c`** (100 B) = LUFS text readouts: **Integrated LKFS** (e.g. −29.3 LKFS Integrated),
  **Loudness Range / LRA** (e.g. 3.4 LK Loudness Range), with "Integrated Dial" / "LRA Dial" labels.
- **`0x000c`** (9 B) = momentary/short-term value ("−28.1 dB", …).
- **`0x0122`** (251 B) = **loudness-history time-graph**: embeds the dB scale labels
  ("-18-21-24-27-30-33-39-45") and the TIME axis ("00:02:03 00:02:13 …") plus the plot data
  (values 0..164). So the scrolling loudness-over-time plot.
- **`0x0104`** = LOUDNESS / RESET / **PLAY-PAUSE** / PRESETS soft-keys (PLAY/PAUSE the measurement).

## All 4 meter modes now decoded
| mode | numeric | graphic |
|------|---------|---------|
| Overview | 0x011c T PEAK/RMS | 0x0122 scope + 0x0126 balance + 0x0127 correlation |
| Analogue | 0x011c current/max | 0x0125 VU needle L + 0x0127 VU needle R |
| RTA | sel-freq | 0x0122 31-band spectrum (64 B) |
| Loudness | 0x011c LKFS/LRA + 0x000c momentary | 0x0122 loudness-history graph (+ dB & time axes) |

Meter decode complete (addresses + formats for all modes). Exact byte→value scaling per mode STILL
needs level-sweep captures — CRITICAL, not optional: the meter/analyzer display is the main UF1
selling point (match SSL's graphic byte-for-byte). Meter view is 100% SSL-plugin-gated (Frank
2026-07-02); REAPER self-rendering DEFERRED. See memory `uf1-meter-analyzer-display`.
