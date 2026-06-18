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

## 4K E
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
