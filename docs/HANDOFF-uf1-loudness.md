# HANDOFF — UF1 Loudness screen (capture + build), starting 2026-07-24

Fresh-context entry point. The meter operator layer (Overview/Analogue/RTA) is
DONE + HW-verified; Bug D (per-Digital-Type Overview bar scales) shipped `4cc9177`.
**Loudness is the one meter screen NOT yet built** and is the next job.

## ✅ CAPTURE DONE 2026-07-24 — read `docs/session-2026-07-24-uf1-loudness-capture.md`
All 3 Scale Ranges captured (cap120/121/122); the history byte-law is SOLVED:
history LINE = `0x0122` **sub-frame 1** (250 cols, byte 0..180);
`byte = clamp((axisLU − rangeBottom)·180/span, 0, 180)`, rangeBottom∈{−18,−36,−54},
span∈{27,54,81}, `axisLU = LUFS + 31.6` (resolve the +31.6 on the Mac vs the plug-in
floats). Readouts = `0x011c` 4×25 `[Integrated][Short-Term][TruePkMax][ShortTermMax]`.
**Remaining = BUILD (Mac; UF1 must move back from StoerPC) + HW-verify.** The capture
steps below are historical — the data is in hand.

## Rig state (VERIFY FIRST)
- StoerPC LAN IP is now **`192.168.177.198`** (DHCP moved it off .197 — always
  re-check: `ping stoerpc.local` or scan the /24 for port 22). SSH:
  `sshpass -p claudepass ssh claude@192.168.177.198` (reset the host key first,
  it changes per boot: `ssh-keygen -R <ip>`). NOT the Tailscale IP
  ([[tailscale-ssh-bypasses-authorized-keys]]).
- UF1 on **SSLBUS**, SSL 360 running, meter traffic on **`\\.\USBPcap3`** (dev
  addr auto-detects; parser filters ep 0x02 FF67).
- Capture mechanic + analysis are proven from Bug D: the reusable launcher script
  pattern (Start-Process USBPcapCMD → sleep → stop, base64 -EncodedCommand, run in
  a Bash background task; confirm growing before GO). Correlation tool:
  `scratchpad/bugd.py` — `0x011c` field-1 = current L-peak readout, bar elements
  `0x0125` rms / `0x0126` peak / `0x0127` hold. Pull with scp, pcaps gitignored.

## What to capture (Loudness)
1. **Enter Loudness** on the UF1 (SSL 360 selector). It's a 10-page screen (Meter
   Pro). cap109 (2026-07-20 session, `session-2026-07-22-uf1-meter-capture.md`)
   already has the 10-page V-Pot param NAMES; re-grab if needed.
2. **Element map:** capture Loudness idle + with signal, histogram the OUT FF67
   elements (analysis/uf1_meter_capture_analyze.py --hist) to find which elements
   carry the Loudness meters (Integrated / Short-Term / Momentary / True Peak / LRA
   bars + the history plot — the history is `0x0122`, the same image element as the
   goniometer, per cap109). Identify the numeric readout element (LUFS text).
3. **Scale ranges (param 47 = Loudness Meter Scale Range):** step its 3 values
   (-18..+9 / -36..+18 / -54..+27) and, for EACH, ramp a signal and correlate the
   LUFS readout ↔ the loudness bar byte — exactly the Bug-D method — to build the
   per-range LUFS→byte table(s). Also check Display Type (48: Absolute/Relative)
   and Terminology (49: LU(FS)/LK(FS)) for label effects.
4. **History plot:** confirm the `0x0122` sub-frame codec for the loudness history
   graph ([[uf1-meter-codec-decoded]] notes 0x0122 sub-frames).

## BUILD (Mac) — remaining. UF1 must move back from the StoerPC for HW-verify.
### ✅ DONE — Meter-Pro detection (`sslcore::meterProAvailable()`, built + compiles)
Instance gains `isPro`, set in `classify_` when a DataType ≥ `LoudMomentary`(11) is
seen (plain Meter never streams those; PluginType is absent on the wire). Public
`meterProAvailable()` = the read instance (pin/sticky/front) is Pro, else any live Pro.
**VERIFIED on the Mac (ssl_core trace 2026-07-24):** the Meter Pro streams Loudness
DataTypes (11–24) on ALL views incl. Overview(0) — so detection works before entering
Loudness. Detection is sound. ⚠ **Instance caveat:** with 3 Meter instances loaded, only
the PRO streams 11–24 and it may be the SILENT one (auto-pick prefers the live plain
Meter). The Loudness screen MUST read the Pro instance (the one with DataType≥11), not
the auto-picked live one — else readouts/history are empty. History (25/26) is view-gated.

### ✅ RESOLVED on the Mac trace 2026-07-24 — the 4 readouts (double-confirmed)
type-17 prepare frames give name→DataType, and the floor values confirm the units:
- Field 0 **Integrated = DataType 15** (LoudReadout1, LUFS)
- Field 1 **Short-Term = DataType 16** (LoudReadout2, LUFS)
- Field 2 **True Peak Max = DataType 17** (LoudReadout3, **dBFS** — floor −139.2, not −70)
- Field 3 **Short-Term Max = DataType 18** (LoudReadout4, LUFS)
The UF1 shows Readout slots 1–4; the plug-in supplies name+unit per slot in the type-17
(also: Readout7=21 Loudness Range LU, Readout9=23 Momentary, Readout10=24 Momentary Max).
Build `0x011c` = 4×25B "value unit caption" from DataTypes 15/16/17/18 of the PRO instance.

### TODO — task #3 (rendering + cycle + V-Pot layer)
- **Cycle:** make the Screen-Selector cap dynamic — `kUf1MeterScreenCycle` is a
  `constexpr int = 3` (main.cpp ~395; used at ~15696 + the selector). Replace with
  `meterProAvailable() ? 4 : 3` so Loudness (kUf1MeterScreens[3], already built) joins
  only for Meter Pro. Reset page on entry. Also: line ~15707 clamps the page-nav screen
  to 0..2 (RTA) — extend to 3; `kUf1MeterPageCount` needs a Loudness entry (10).
- **Readouts (`0x011c`, 4×25):** DataTypes 15/16/17/18 (RESOLVED above), from the PRO instance.
- **History LINE (`0x0122` sub-frame 1, 250 cols, byte 0..180):** from
  `LoudScrollableHistory`(26)/`LoudCompleteHistory`(25) floats. **View-gated: set
  `setView(3)` and re-trace to see 25/26 stream + resolve the +31.6 (axisLU vs LUFS) and
  whether the plug-in accepts view 3.** Law (SOLVED, cap120/121/122):

### TODO — task #3 (rendering + cycle + V-Pot layer)
- **Cycle:** make the Screen-Selector cap dynamic — `kUf1MeterScreenCycle` is a
  `constexpr int = 3` (main.cpp ~395; used at ~15696 + the selector). Replace with
  `meterProAvailable() ? 4 : 3` so Loudness (kUf1MeterScreens[3], already built) joins
  only for Meter Pro. Reset page on entry.
- **Readouts (`0x011c`, 4×25):** `[Integrated][Short-Term][True Peak Max][Short-Term Max]`
  from the Loud DataTypes. Map which DataType feeds each field (LoudShortTerm=12 for
  Short-Term; Integrated/TruePkMax/ShortTermMax are among LoudReadout1..10=15..24 —
  confirm on the Mac against the clear-text object names, don't guess).
- **History LINE (`0x0122` sub-frame 1, 250 cols, byte 0..180):** from
  `LoudScrollableHistory`(26)/`LoudCompleteHistory`(25) floats. Law (SOLVED, cap120/121/122):
  `byte = clamp((axisLU − rangeBottom)·180/span, 0, 180)`, rangeBottom∈{−18,−36,−54},
  span∈{27,54,81} from **param 47** (Scale Range). `axisLU = LUFS + 31.6` — **resolve the
  +31.6 on the Mac**: compare the plug-in's history floats to the `0x011c` readout; the
  floats may already be axis-LU (map direct) or absolute LUFS (add offset).
  **sf2 is TWO parts** (manual cross-check 2026-07-24): cols 0–63 = a SECOND scrolling
  history curve (momentary vs short-term window?) using the SAME byte law → render it
  from the matching plug-in history array; cols 64–91 = static target-band/legend,
  replay per range. **sf0 = Y-axis TEXT labels** (per-range static, replay). sf3 = a
  near-empty "now" cursor. So the screen has TWO live history traces, not one.
- **V-Pot 10-page operator layer:** extend `kUf1MeterVPots` + the per-screen page count
  to Loudness (10 pages) using cap109 names + the Meter Pro param dump
  (`docs/ssl-native-params/VST3__SSL_Meter_Pro_(SSL).md`: param 24 Play mode, 25 History
  Window, 47 Scale Range, 48 Display, 49 Terminology, 50 Play/Pause, …). Labels via the
  same 0x010e name/value / name-only path ([[uf1-meter-operator-layer]]). Soft-key 2 =
  PLAY on Loudness (already in the burst).
- Full capture record + the law: `docs/session-2026-07-24-uf1-loudness-capture.md`.

## Context to load in the fresh session
- [[uf1-meter-operator-layer]] — the operator layer + all the wire facts.
- [[handoff-uf1-meter-live]] — meter codec + goniometer.
- `docs/session-2026-07-23-uf1-meter-operator-layer.md`, `docs/session-2026-07-24-uf1-bugd.md`.
- Deploy trap: never `cp` over a running REAPER's dylib (SIGKILL); atomic `mv`
  ([[macos-codesign-after-install-name-tool]]).
