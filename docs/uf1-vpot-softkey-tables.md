# UF1 Plugin-Mode Soft Key & V-Pot assignment tables

**Source: SSL UF1 User Guide Rev4.0, printed p188** (transcribed verbatim from the
PDF 2026-06-18 — NOT from memory). p187 confirms the mechanism:
- 4 V-Pots + 4 Soft Keys surround the large LCD; each plugin has **8 pages**.
- The **← / → page arrows** (physically under V-Pots 3 & 4 = nav ids `0x24`/`0x26`)
  change the Channel-Strip parameter page (1–8).
- **Quick Key 2** toggles the encoders Normal ↔ **Fine** resolution.
- Soft Keys control toggles/params (and the PLUG-IN/DAW toggle); the Soft-Key
  Parameter text above each shows what it drives on the current page.

Blank cell = unassigned on that page. Params are resolved against the focused
SSL plugin **by name** (mirrors `uf1ParamByName_` in the painter) — exact REAPER
param-name strings still need confirming per plugin during the build.

## Channel Strip 2
| Page | Soft Key 1 | Soft Key 2 | Soft Key 3 | Soft Key 4 | V-Pot 1 | V-Pot 2 | V-Pot 3 | V-Pot 4 |
|---|---|---|---|---|---|---|---|---|
| 1 | Ø | | SOLO SAFE | PLUG-IN | Width | | Out Trim | Comp Mix |
| 2 | S/C MODE | | HQ MODE | A/B | In Trim | | High Pass | Low Pass |
| 3 | LF BELL | | E | EQ | LF Gain | LF Freq | | |
| 4 | | | E | EQ | LMF Gain | LMF Freq | Q | |
| 5 | | | E | EQ | HMF Gain | HMF Freq | Q | |
| 6 | HF BELL | | E | EQ | HF Gain | HF Freq | | |
| 7 | FAST ATTACK | PEAK | S/C LISTEN | DYNAMICS | Ratio | Threshold | Release | |
| 8 | EXPAND | FAST ATTACK | | DYNAMICS | Range | Threshold | Release | Hold |

## 4K B
| Page | Soft Key 1 | Soft Key 2 | Soft Key 3 | Soft Key 4 | V-Pot 1 | V-Pot 2 | V-Pot 3 | V-Pot 4 |
|---|---|---|---|---|---|---|---|---|
| 1 | Ø | PRE | SOLO SAFE | PLUG-IN | Width | Mic | Out Trim | Comp Mix |
| 2 | S/C LISTEN | | HQ MODE | A/B | In Trim | | High Pass | Low Pass |
| 3 | LF BELL | | | EQ | LF Gain | LF Freq | | |
| 4 | | | | EQ | LMF Gain | LMF Freq | Q | |
| 5 | | | | EQ | HMF Gain | HMF Freq | Q | |
| 6 | HF BELL | | | EQ | HF Gain | HF Freq | | |
| 7 | | | S/C LISTEN | DYNAMICS | Ratio | Threshold | Release | |
| 8 | EXPAND | | | DYNAMICS | Range | Threshold | Release | |

## 4K E — SSL 360 2.1.12, MEASURED (cap133, 2026-09-11)
This is what 360 2.1.12 sends to the UF1 with a 4K E focused, page by page
(`analysis/…/pages.py` over cap133), and what `kUf1CsSoftKeys[2]` /
`kUf1CsVPots[2]` implement since 2026-09-11 (Frank: "diese param-reihenfolge
für unsere version übernehmen"). Home is page 1 of ours; SSL numbers the eight
after it. The 4K G row copies this layout (same params) until it is captured.

| Page | Soft Key 1 | Soft Key 2 | Soft Key 3 | Soft Key 4 | V-Pot 1 | V-Pot 2 | V-Pot 3 | V-Pot 4 |
|---|---|---|---|---|---|---|---|---|
| 1 | Ø | PRE | SOLO SAFE | PLUG-IN | Width | Mic | Out Trim | Mix |
| 2 | FILTERS | | HQ MODE | A/B | In Trim | | High Pass | Low Pass |
| 3 | LF BELL | | EQ COLOUR | EQ | LF Gain | LF Freq | | |
| 4 | | | EQ COLOUR | EQ | LMF Gain | LMF Freq | LMF Q | |
| 5 | | | EQ COLOUR | EQ | HMF Gain | HMF Freq | HMF Q | |
| 6 | HF BELL | | EQ COLOUR | EQ | HF Gain | HF Freq | | |
| 7 | FAST ATTACK | | | DYN | Ratio | Threshold | | Release |
| 8 | FAST ATTACK (gate) | EXPANDER | S/C LISTEN | DYN | Range | Threshold | Release | |
| 9 | AUTO MAKEUP | | | | | | | |

The p188 (2.0.6-era) table it replaces, for the record:

| Page | Soft Key 1 | Soft Key 2 | Soft Key 3 | Soft Key 4 | V-Pot 1 | V-Pot 2 | V-Pot 3 | V-Pot 4 |
|---|---|---|---|---|---|---|---|---|
| 1 | Ø | PRE | SOLO SAFE | PLUG-IN | Width | Mic | Out Trim | Mix |
| 2 | S/C LISTEN | | HQ MODE | A/B | In Trim | | High Pass | Low Pass |
| 3 | LF BELL | | EQ COLOUR | EQ | LF Gain | LF Freq | | |
| 4 | | | EQ COLOUR | EQ | LMF Gain | LMF Freq | Q | |
| 5 | | | EQ COLOUR | EQ | HMF Gain | HMF Freq | Q | |
| 6 | HF BELL | | EQ COLOUR | EQ | HF Gain | HF Freq | | |
| 7 | FAST ATTACK | | S/C LISTEN | DYN | Ratio | Threshold | Release | |
| 8 | EXPANDER | FAST ATTACK | | DYN | Range | Threshold | Release | |

## 360 Link
| Page | Soft Key 1 | Soft Key 2 | Soft Key 3 | Soft Key 4 | V-Pot 1 | V-Pot 2 | V-Pot 3 | V-Pot 4 |
|---|---|---|---|---|---|---|---|---|
| 1 | Ø | SATURATION IN | SOLO SAFE | PLUG-IN | Width | SaturationAmt | Output Trim | Comp Mix |
| 2 | LISTEN | | | | Input Trim | | High Pass Filter | Low Pass Filter |
| 3 | LF TYPE | | EQ TYPE | EQ IN | LF Gain | LF Freq | | |
| 4 | | | EQ TYPE | EQ IN | LMF Gain | LMF Freq | Q | |
| 5 | | | EQ TYPE | EQ IN | HMF Gain | HMF Freq | Q | |
| 6 | HF TYPE | | EQ TYPE | EQ IN | HF Gain | HF Freq | | |
| 7 | CMP FST ATTK | COMP PEAK | LISTEN | DYNAMICS IN | Comp Ratio | Comp Threshld | Comp Release | |
| 8 | GTE EXPANDR | GATE ATTACK | | DYNAMICS IN | Gate Range | Gate Threshold | Gate Release | Gate Hold |

## Notes for the native build
- V-Pot rotation ids `0x00`–`0x04`: per the decode, `0x00` = above-fader V-pot,
  `0x01`–`0x04` = the 4 plugin V-pots under the LCD → V-Pot 1–4 in these tables.
  (Confirm 0x00's role — these tables only cover V-Pot 1–4.)
- Page state is shared by Soft Keys AND V-Pots (the page applies to both rows).
- These are the SSL **Plugin-Mode** assignments; they presuppose the focused FX is
  one of the four 360°-enabled strip types. For a non-SSL focused FX we fall back
  to a generic mapping (out of scope for 3b).
- Implementation: encode as a static table keyed by (plugin type, page) → 4 soft-key
  + 4 V-pot param descriptors (name string or toggle id). Resolve to a live
  vst3Param via name match at use time. ← / → step the page (wrap 1↔8).

## Harrison 32Classic Channel Strip v2 — SSL 360 2.1.12, MEASURED (cap134, 2026-09-11)
Rea-Sixty factory strip (type 7) since 2026-09-11. Ten pages as SSL sends
them; V-Pot labels are SSL's (the plug-in's short names where it has them).
Params from `docs/ssl-native-params/VST3__Harrison_32Classic_Channel_Strip_(Harrison_Audio).md`.

| Page | Soft Key 1 | Soft Key 2 | Soft Key 3 | Soft Key 4 | V-Pot 1 | V-Pot 2 | V-Pot 3 | V-Pot 4 |
|---|---|---|---|---|---|---|---|---|
| 1 | Ø | PRE (label only) | SOLO SAFE | PLUG-IN | Width | Mic (= Saturator Drive) | Out Trim | Mix |
| 2 | FILTERS (EQ Filters In) | | HQ MODE (HQ param) | A/B (label only) | In Trim | | High Pass | Low Pass |
| 3 | LOW BELL MODE | | | EQ (EQ Bands In) | Low Gain | Low Freq | | |
| 4 | | | | EQ | Low-Mid Gain | Low-Mid Freq | | |
| 5 | | | | EQ | Hi-Mid Gain | Hi-Mid Freq | | |
| 6 | HI BELL MODE | | | EQ | Hi Gain | Hi Freq | | |
| 7 | | | | DYN | CpRt (Ratio) | Threshold | CpAt (Attack) | CpRl (Release) |
| 8 | | EXPANDER | | DYN | | Threshold (gate) | Release (gate) | Gate Hold · XpRt when Expander on |
| 9 | | COMP EMPHASIS IN | GSFT (Gate SC Filter In) | | CpMu (Makeup) | CEFq (Emphasis Freq) | GSFq (Gate SC Filter Freq) | Gate Attack |
| 10 | EXPANDER | GATE SC LISTEN | | | GtHs (Hysteresis) · XpKn when Expander on | | | |

GR on UC1, UF1 and UF8 comes the way it does for a 4K: comp from REAPER's
`GainReduction_dB` (the 32C binary carries the same PreSonus
`IGainReductionInfo` as SSL 4K E/B/G), gate from the impersonator's `GateGain`.
The 32C connects to SSL 360 Core itself: it is built on SSL's plug-in library,
declares `32CEQCurveData`, announces its track and streams `CompGain` /
`GateGain` (sslcore trace and `rea_sixty.log`, 2026-09-11). Its "Comp Reduction
Meter" / "Gate Reduction Meter" params (0..1, no dB law) are not used.

Not wired yet: presets (no SSL preset folder). Over its Core connection the 32C
does send a preset list (XML) and a Harrison preset path (same trace).
