# cap90 + cap91 — UF1 RTA screen codec

**Date:** 2026-07-14
**Device:** SSL UF1 (VID_31E9 PID_0025, dev 8 on `\\.\USBPcap3`), Plugin Mode, Meter view, **RTA** screen.
**Driver:** SSL 360° (we capture ITS codec — Rea-Sixty not attached).
**Screen settings (from the RTA setup burst, cap76):** `SlFreq None`, **`SclTop 0 dB`**, **`SclBtm -120 dB`**.

| capture | signal | question it answers |
|---------|--------|--------------------|
| **cap90** | `dbfs_sweep_mono.wav` — 1 kHz, 31 steps × 2.5 s, **0 → −90 dBFS** in 3 dB steps, L=R | level → byte value |
| **cap91** | `rta_bands.wav` — a sine at each of the 31 ISO third-octave centres, 2 s each, −20 dBFS | band index → byte offset |

Both from `analysis/gen_rta_corr_wav.py` (`dbfs --mono` / `rta`). Mono because the RTA
spectrum is not per-channel — an opposite L/R sweep would be ambiguous here (unlike the
Overview bars, cap89).

## Result — FULLY DECODED

`0x0122` on the RTA screen = **64 bytes**:

```
byte[0..1]   = header, constant (0x00, 0x03) across all 4299 frames of both captures
byte[2+2i]   = band i, current      i = 0..30
byte[3+2i]   = band i, peak-hold
```

Bands are the 31 ISO third-octave centres: 20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160,
200, 250, 315, 400, 500, 630, 800, **1000**, 1250, 1600, 2000, 2500, 3150, 4000, 5000,
6300, 8000, 10000, 12500, 16000, 20000 Hz.

### Band mapping — proven one band at a time (cap91)
Each tone lights exactly its own two adjacent bytes; argmax tracks `2+2i` across all 31:
band 0 (20 Hz) → bytes 2/3, band 17 (1 kHz) → 36/37, band 30 (20 kHz) → 62/63.

The few apparent off-by-ones (band 24 peaks at index 49, not 50) are the **previous
band's held peak** still being higher than the new band's current — which independently
confirms the odd byte is the peak-hold.

### Value scale — a pure linear dB law (cap90)
Unlike the VU faceplate (cap88) and the Overview bargraphs (cap89), which need lookup
tables, the RTA is exactly linear at **5 counts per 3 dB** from −3 to −72 dBFS, with zero
deviation:

| dBFS | −3 | −18 | −33 | −48 | −63 | −72 |
|------|----|----|----|----|----|----|
| byte | 198 | 173 | 148 | 123 | 98 | 83 |

```
byte = 3 + (dBFS + 120) * 5/3,  clamped to [3, 200]
```

This falls straight out of the screen's own settings: the scale spans SclBtm −120 dB to
SclTop 0 dB, and 120 dB × 5/3 = 200 counts, with 3 as the baseline — which is exactly the
constant `byte[1] = 3`. So the law should be **derived from SclTop/SclBtm**, not
hardcoded; those are TrackFX params we can read.

Below −72 dBFS the curve bends (−90 → 39 where the law predicts 53). That is **our test
signal, not the meter**: the WAV is 16-bit, so a −90 dBFS sine sits at ~1 LSB and is
badly quantised. Not a meter behaviour; no action needed.

## Note — the "broad hump" is not ours to reproduce
cap90 at 0 dBFS shows a wide symmetric hump around the 1 kHz band rather than a single
bar; that is the third-octave filter bank's skirts. It does **not** affect our codec: we
never synthesise the spectrum. The plugin hands us `Rta31Band` as 31 finished floats
(see `ssl360-plugin-protobuf-protocol`); we map each to a byte and place it. The skirts
are the plugin's business.
