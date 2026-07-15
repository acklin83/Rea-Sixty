# cap98 — UF1 meter probe run (2026-07-15)

Our own impersonator dump (`REASIXTY_T10_DUMP=1`), NOT a USB capture.
Signal: `analysis/gen_uf1_meter_probe_wav.py` -> `uf1_meter_probe.wav` (53 s).
Played twice: once on the Overview screen, once on Analogue (the plug-in only
computes the meters its selected view needs, so each screen needs its own pass).

Anchors (dump time is seconds since boot):
  run 1 (Overview) wav t=0 at +61.81 s
  run 2 (Analogue) wav t=0 at +123.75 s
Two plug-in instances: src 61563 and 61825 (identical values throughout).

## MEASURED — every value against a known input

Overview run (BarPeak/TextPeak/TextRms live, VuPpm parked, Lissajous live):

| input      | BarPeak cur | TextPeak cur | TextPeak pk (hold) | TextRms cur |
|------------|-------------|--------------|--------------------|-------------|
| -30 dBFS   | -30.00      | -30.00       | -16.16             | -33.02      |
| -20 dBFS   | -20.00      | -20.00       | -16.16             | -23.01      |
| -10 dBFS   | -10.00      | -10.00       | -10.00             | -13.01      |
|   0 dBFS   |  -0.00      |  -0.00       |  -0.00             |  -3.01      |

* Peak types are exact dBFS. TextRms = peak - 3.01 dB on a sine = the true RMS.
* The hold (f4) is a genuine running max: it sits at -16.16 through the quiet
  tones, which is the loudest earlier section of the WAV (corr -1 peaks at
  -16.2 dBFS, measured in the generator's own verification). Then it follows.

| input   | Correlation(6) | StereoBalance(7) |
|---------|----------------|------------------|
| corr +1 |  1.000         |  0.00            |
| corr  0 | -0.021         | -0.01            |
| corr -1 | -1.000         |  0.00            |
| L only  |  0.000         | -1.00            |
| R only  |  0.000         | +1.00            |

Both are plain floats in -1..+1. No decode needed.

Analogue run (VuPpm live, BarPeak parked at the floor):

| input       | VuPpm cur (NEEDLE) | VuPpm pk    | TextVuPpm cur (NUMBER) | ovl f5 |
|-------------|--------------------|-------------|------------------------|--------|
| -30 dBFS    | -12.01             | -11.88      | -12.01                 | 0      |
| -20 dBFS    |  -2.01             |  -1.83      |  -2.01                 | 0      |
| -10 dBFS    |   3.00 (CLAMPED)   |   8.17      |   8.00                 | 0      |
|   0 dBFS    |   3.00 (CLAMPED)   |  18.12      |  18.00                 | **1**  |

* VU = dBFS + 18 exactly (the -18 dBFS AnalogueMetersRefLevel default).
* **VuPpm(0) cur is CLAMPED to +3.0** = the faceplate's end stop -> that is the
  NEEDLE. **TextVuPpm(1) cur is unclamped** (8.0, 18.0) -> that is the NUMBER.
  Same split as BarPeak vs TextPeak. Reading the wrong one pins the readout at 3.0.

## THE RED LED
`f5 OverloadValues = [1,1]` fires **only on VuPpm(0)**, 177 messages, exactly at
the 0 dBFS clip. During the Overview run the SAME clip sets overload on NO data
type at all (VuPpm is not computed there) — consistent with the clear-text
property name `AnalogueMetersLedOverload`. The SOURCE is settled; which UF1
ELEMENT displays it is NOT (see cap88/cap94: 0x0015/0x0016 constant 0xff, and
neither capture ever contains a clip).

## GONIOMETER — NOT captured, our bug
The T10 lines here are CHUNKS (n=5000), not images: `chunked()` required f7
(MaxValueCount) and this sender omits it (`f7=-1 f8=5000 f9=0,1,2,3`, sizes
5000,5000,5000,2113 = 17113 — the same total cap87 states explicitly via f7).
Fixed after this run; needs one more pass to get the reassembled 17113 array.
