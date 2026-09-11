# cap130 — UF1 Analogue screen, VU: needle wav then LED wav, SSL 360 2.1.12 (2026-09-11)

**What:** 110 s USBPcap3 window, StoerPC, SSL 360 2.1.12.72214 + Meter Pro 1.3.7,
UF1 on the Meter mode, Analogue screen, **VU** faceplate. Frank played
`uf1_needle.wav` (52 s: -18/-10/-30 dBFS tones with 5 s silences, five 200 ms
bursts) followed by `uf1_led.wav` (43 s: Ref tone, five 1 s hits at -6 dBFS =
Ref+12, a 3 s -3 dBFS = Ref+15 tone, silences) on one track. Device address 18,
OUT 0x02. 7.5 MB, 22674 FF67 frames; 2708 each of 0x0125 (needle L), 0x0127
(hold needle L), 0x0128 (overload LED mask), 0x011c (readout).

**Why:** after Meter Pro 1.3.7 dropped VuPpm(0), our second needle (dt=1 f4
latches) and the red LED (dt=1 carries only the flash bit) had no source left
in the plug-in stream. This is what SSL 360 itself draws on the hardware from
the same stream: the hold needle's fall law on 0x0127 and the LED bits on 0x0128
against a signal that crosses the 9 dB LED threshold.

**Decode:** `analysis/uf1_vu_needle_fit.py captures/cap130_uf1_vu_needle_led.pcap --dev 18`,
`analysis/uf1_hold_law.py`, `analysis/uf1_meter_capture_analyze.py … --dev 18 --elem 0x0128`.
