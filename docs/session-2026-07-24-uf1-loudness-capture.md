# UF1 Loudness capture — history byte-law SOLVED, 2026-07-24 (StoerPC)

Followed `docs/HANDOFF-uf1-loudness.md`. Rig: StoerPC `192.168.177.198`, USBPcap3,
UF1 (PID_0025) on **sslbus**, SSL 360 + REAPER + SSL Meter Pro driving it. Captured
SSL→UF1 (OUT ep 0x02, FF67) while ramping a signal, per Scale Range. Analyzer auto-
picks the busiest OUT dev (was 28 this session; changes per boot).

## Loudness screen — element map (confirmed; corrected after the manual cross-check)
Frank: "cross-check with the manual." The SSL UF1 Rev4.0 guide has NO Loudness (plain
Meter = 3 screens only; Loudness is Meter PRO). SSL's Meter Pro docs list a *momentary
bar*, *short-term history + histogram*, and text readouts (Integrated, True Peak Max,
LRA, PLR, PSR…). The UF1 shows a SUBSET — mapped from the captures:
- **`0x011c`** = 4×25 B ASCII readouts: `[Integrated][Short-Term][True Peak Max][Short-Term Max]`
  (4 fixed slots; the plug-in has more — Momentary/LRA/PLR/PSR — not shown as text on UF1).
- **`0x0122`** = sub-frame image, `byte[0]` selects 4 sub-frames (251 B; sf3 = 208 B):
  - **sf0** = the Y-axis TEXT labels ("−14 −17 −20 …" as ASCII glyphs). Per-range, ~static.
  - **sf1** = **the short-term history LINE** (250 cols, byte = plot Y 0..180). ← main meter.
  - **sf2** = **TWO parts**: cols **0–63 = a SECOND scrolling history curve** (momentary vs
    short-term window?) that uses the **SAME byte law as sf1** (verified: ST −39.2 → col0=69
    = the law's 69.3; pegs >−22, 0 at <−50); cols **64–91 = static** target-band + legend
    (`60,1,0,78, 0x7f-block, 108,74,38,4`). ⚠ NOT a static "target band" only — the left
    64 cols are a LIVE meter. Frank's cross-check caught this; do NOT treat sf2 as chrome.
  - **sf3** = a near-empty "now" edge cursor (max ~27). NOT the history.
  - ⚠ cap95's "sf3 = the history" read the wrong sub-frame. The history is sf1 (+ sf2 curve).
- `0x0123` = present, distinct=1 (Loudness marker). `0x0119` change-driven, codec unknown (also RTA).
- `0x011a` is NOT used on Loudness (data-driven; the plug-in rescales & streams).

## ★ HISTORY BYTE-LAW — SOLVED, all 3 Scale Ranges (param 47)
Method (Bug-D style): ramp Short-Term across the range, pair the `0x011c` Short-Term
readout (field 1) ↔ sf1 newest column (= last non-zero index). Linear, clean (±2).

| Range (param 47) | span | slope (byte/LU) | byte180 @ ST | byte0 @ ST | pcap |
|---|---|---|---|---|---|
| **−18..+9** (0.0) | 27 | 6.66 (=180/27) | −22.6 | −49.6 | cap120 |
| **−36..+18** (0.5) | 54 | 3.33 (=180/54) | −13.6 | −67.5 | cap121 |
| **−54..+27** (1.0) | 81 | 2.22 (=180/81) | −4.6 | −85.7 | cap122 |

**One formula:** `byte = clamp( round( (axisLU − rangeBottom_LU) · 180 / span ), 0, 180 )`
- Plot height fixed **0…180**; the range's LU span maps linearly onto it.
- `rangeBottom_LU ∈ {−18, −36, −54}`, `span ∈ {27, 54, 81}` (from param 47).
- **`axisLU = ST_LUFS + 31.6`** — a CONSTANT offset that held across ALL three ranges
  (byte0 = rangeBottom − 31.6 LUFS; byte180 = rangeTop − 31.6 LUFS, every range).
  The +31.6 is session-specific (reference/target related). **Resolve it on the Mac**
  against the plug-in's `LoudScrollableHistory` floats — those may already be axis-LU
  (then map directly) or absolute LUFS (then add 31.6). Do NOT hardcode 31.6 blindly.

## Build implication
We render sf1 from the plug-in's `LoudScrollableHistory`(26)/`LoudCompleteHistory`(25)
float array: map each float → byte via the formula, scroll 250 columns. Read `param 47`
(Scale Range) for rangeBottom/span. Readouts (`0x011c`) come from the Loud DataTypes.
sf0 (axis) + sf2 (target band) can be replayed near-static per range for the chrome.

## Tooling (rebuilt this session; bugd.py was gone)
- `scratchpad/cap.sh NAME SECS [IFNUM]` — fixed-window USBPcap capture + scp to /tmp.
  **TRAP that cost the first runs:** an UNQUOTED bash heredoc ate the backslashes in
  `\\.\USBPcap3` → device passed as `\.\USBPcap3` → USBPcapCMD "empty capture", 0 bytes.
  Fix = QUOTED heredoc + inject vars as PS assignments (cap.sh does this now).
  Also: USBPcapCMD needs **`-A`** (or `--devices`) or it captures nothing.
  Also: a DETACHED Start-Process dies when the SSH session closes (orphan-reaped) —
  the capture must run inside a live SSH session (fixed-duration sleep), as a bg task.
- `scratchpad/loud.py <pcap> [cols|law|labels] [subframe]` — decode sf1 history vs readout.
- Pcaps preserved: `captures/cap12{0,1,2}_loud_r{1,2,3}_*.pcap` (gitignored).

## Still open (Mac build, UF1 must move back from StoerPC)
- Meter-Pro detection (instance streams a Loud DataType ≥11) so Loudness joins the cycle.
- Wire readouts + history (this law) + the 10-page V-Pot operator layer (cap109 names).
- HW-verify. See the memory notes + `docs/HANDOFF-uf1-loudness.md`.
