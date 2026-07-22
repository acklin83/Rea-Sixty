# UF1 meter SSL→UF1 capture session — 2026-07-22 (StoerPC / USBPcap)

Executed the plan in `docs/uf1-meter-capture-plan.md`. Rig: UF1 (PID_0025,
UF1-009184) rebound **WinUSB → sslbus** so SSL 360 drives it; captured the
SSL→UF1 USB stream on **`\\.\USBPcap3`** (dev 14, OUT = ep 0x02, FF67 frames);
track "meter_test" with SSL **Meter Pro** + steady tone. All 7 captures pulled to
`captures/` (pcaps are gitignored — kept local).

Frame model (unchanged): `FF 67 <len> <elemHi> <elemLo> <payload…> <cksum>`;
element id = bytes 3–4; payload = bytes 5..-1.

## ★★ TIER 0 — the render trigger (Feature A unblocker) — SOLVED
**The value-label repaint trigger = re-send the FULL 0x010e label group, indices
0→1→2→3, INCLUDING index 0** (the instance/track name). Our feature-A emit sent
only 1/2/3 → the UF1 updated its buffer but never repainted. SSL always sends all
four as one group on every change.

- **cap103** (Digital Type, value + SCALE change): full group **+ 0x011a**; 0x011a
  fired 2× for 2 changes, absent on the settle re-sends.
- **cap104** (Fade, value-only): Fade stepped 0/0.25/0.5/1/2/3/5 sec, each
  rendered; full group each time, **0x011a NEVER fired**.

⇒ **0x011a is NOT the value trigger** — it is a scale/faceplate selector (below).
**Fix:** `uf1EmitMeterParamLabels_` must emit indices 0,1,2,3. Testable on the Mac.

## 0x011a — the meter SCALE / FACEPLATE selector (1 byte, per-screen namespace)
Fires ONLY when the visual scale/faceplate changes; the graphics are firmware-
built-in. The streaming bar elements do NOT change per scale (cap105:
0x011c/0125/0126/0127 stay `distinct=1` across all 7 Digital Types).

- **Overview · Digital Type** (cap105, consistent fwd+back):
  Non-Linear=`07` · Non-Linear 2x=`08` · Linear=`03` · Linear 2x=`04` ·
  K-20=`0e` · K-14=`0c` · K-12=`0a`
- **Analogue · VU/PPM** (cap107): VU=`02` · PPM=`04`
- **RTA / Loudness**: 0x011a NOT used for params — fully data-driven (the plug-in
  rescales and streams). RTA Scale Top/Bottom, Weighting, Select Frequency and all
  Loudness params are value-only labels.

## Screen selector + soft-keys + pages + presets (cap106)
- **0x0100** (2 bytes) = current screen/mode: `0400` Overview · `0401` Analogue ·
  `0402` RTA · `0405` Loudness · `0403` Presets menu. Screen-Selector key cycles
  **Overview → Analogue → RTA → Loudness** (4 screens — Loudness IS in the cycle;
  manual says 3).
- **0x0104** (idx 0–3) = the 4 soft-key labels: `[screen name] · RESET · FINE ·
  PRESETS`. Context-swaps: on **Loudness** soft-key 2 = **PLAY**; in the **Presets
  menu** soft-key 3 = **Navigate Back** (and V-Pot 4 label → **Select**).
- **0x0102** (1 byte) = soft-key highlight/state: `01` normal · `03` Reset
  (momentary flash) · `05` Fine active · `09` Presets menu active.
- **0x010d** (4 bytes) = per-V-Pot styling, one byte each: `06` = dimmed/disabled,
  `0a`/`0b` = active variants.
- **Page switching** sends NO dedicated page element — just the new page's full
  0x010e group + 0x010d. (Feature B = emit the new page's group, like Feature A.)
- **Presets menu**: `0x0100=0403`; the list scroll cursor = **0x011f** (swept
  cleanly up/down as V-Pot 4 turned), 0x011e a companion flag; push loads.
- **Reset** = plug-in reset + 0x0102=03 flash. **Fine** = 0x0102=05 toggle.

## Verified meter V-Pot layout (HW Meter Pro + manual — supersedes guesses)
**V-Pot 1 = Meter Instance selector on every screen & page.** V2/V3/V4 = Parameter
Control 1/2/3. Manual `docs/docs/SSL UF1 User Guide_Rev4.0.pdf` p189-191 is
OUTDATED (2 Overview pages, no Loudness, no Custom/TPkHQ/SmartComp/Overload/Delay).

- **OVERVIEW** (3 pp): P1 True Peak On/Off · Digital Type (7) · Lissajous Fade
  (0/0.25/0.5/1/2/3/5 s). P2 [—] · Peak Hold (Off/1/3/6 s/Inf) · RMS Integration
  (10–5000 ms). P3 True Peak HQ Off/On · Smart Comp Off/On · Delay (0–1000 ms).
- **ANALOGUE** (3 pp): P1 VU/PPM · 0 VU Line-Up (+4/0/-2 dBu) · Reference Level
  (-36…0.0 dB). P2 Dual Format (Stereo/Mid-Side/Custom); Custom → V3 L src, V4 R src
  (L+R Sum/Surrounds Sum/Heights Sum/Sum All/Chan 1-12). P3 Max Needle
  (Off/2 s/Inf) · Overload (0–24 dB / Off).
- **RTA** (3 pp): P1 Select Frequency (None + each band 20 Hz…20 kHz) · Scale Top
  (0…-110 dB /10) · Scale Bottom (-120 dB…up /10, max 10 below Top). P2 Peak Hold
  (Off/0.5/1/2/3/10/30 s/Inf) · Weighting (None/A/C) · Averaging
  (Real Time/Fast/Medium/Slow/Inf). P3 Analysis Source (L+R Sum/Mid/Side/Surrounds
  Sum/Heights Sum/Sum All/Chan 1-12).
- **LOUDNESS** (10 pp, Meter Pro only; data-driven, history plot = 0x0122,
  numeric readouts = 0x011c). Param short-names seen across pages (cap109): Play
  mode (DAW Sync/SystemClk/Continuous/Auto P+R/Auto Pause) · History window
  (30 s…1 day) · Scroll Timeline · Scale Range (-18..+9 / -36..+18 / -54..+27) ·
  Gate · Prog · MinDia · LdIntA · TrueP · Mode (ITU/Leq(m)) · Short · Mom ·
  Display (Absolute/Relative) · Terminology LK(FS)/LU(FS) · Target (LKFS) · IDAlrt ·
  Auto Condense · Dial · AutoScroll · Ovrlp · Surr · TPMax.

## Captures
| file | tier | content |
|---|---|---|
| cap103_tier0_digitaltype | 0 | Digital Type ×2 detents → trigger + 0x011a |
| cap104_tier0_fade | 0 | Fade ×N (value-only) → no 0x011a, proves group trigger |
| cap105_tier2_digitaltype_scales | 2 | all 7 Digital Types → 0x011a selector map |
| cap106_tier4_pages_softkeys | 4 | pages, Reset, Fine, Presets, screen selector |
| cap107_tier1_analogue | 1 | VU↔PPM faceplate + line-up/ref/dual/maxneedle |
| cap108_tier3_rta | 3 | Scale Top/Bottom, Select Freq, Weighting (all value-only) |
| cap109_tier5_loudness | 5 | Loudness screen, history plot, 10-page param names |

## Next
1. **Build Feature A on the Mac**: `uf1EmitMeterParamLabels_` emit indices 0,1,2,3.
2. Feature B (page arrows → re-emit page group), C (Reset), D (Fine → 0x0102=05),
   E (Presets overlay). All machinery now known.
3. Digital-Type scale switching via 0x011a; Analogue VU/PPM via 0x011a (02/04).
