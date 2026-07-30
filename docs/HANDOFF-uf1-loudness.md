# HANDOFF — UF1 Loudness screen (capture + build), starting 2026-07-24

> **2026-07-29 status (authoritative = memory `HANDOFF-uf1-meter-2026-07-29`):** Loudness
> is BUILT (readouts + history + V-Pot layer, HW-verified) with two follow-ups. The
> **left level bars** are now DECODED + BUILT + DEPLOYED (awaiting HW verify): they are
> **0x0122 sub-frame 0, idx 26 = Momentary / idx 27 = Short-Term**, driven live on the
> history plot law from `getMeter(11)/(12)`. Decoder: `analysis/uf1_loudness_bar_decode.py`.
> The old "sf2 momentary FROZEN" item below is superseded (sf2 now-region is live).

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

### ✅ DONE 2026-07-24 (Mac, uf1-native-build) — cycle + plumbing + readouts
Built, compiled, deployed (atomic mv, mtime-verified). **Not HW-verified — the UF1 is
still on the StoerPC.**
- **Cycle:** the Screen-Selector adds Loudness dynamically — `kUf1MeterScreenCycle`
  (=3, plain-Meter base) `+1` when `sslcore::meterProAvailable()` (main.cpp kDisplaySoft1
  handler). Page resets to 0 on screen change (already existed). Page-nav clamp + the two
  paint clamps extended 0..2 → 0..3; `kUf1MeterPageCount` is now `[4] = {3,3,3,1}`
  (Loudness 1 page until its V-Pot table lands).
- **Readouts (`0x011c`, 4×25 — byte-exact from cap120/121/122):** `uf1BuildLoudnessReadouts_()`.
  Each field = `[value:6][unit:4][caption:14][style:1]`; caption + unit are BAKED INTO the
  readout, style 0x01 (LUFS) / 0x02 (dBFS). F0 Integrated·LUFS·DT15 · F1 Short-Term·LUFS·DT16 ·
  F2 True Peak Max·dBFS·DT17 · F3 Short-TermMax·LUFS·DT18. Values from `getMeter(15..18)`
  (auto mode resolves the Pro instance since only a Pro streams DT≥11). Units are Absolute +
  LU(FS) (params 48/49 = 0, the capture state) — Relative/LK(FS) variants NOT captured, TODO.
- **V-Pot screen-3 guard:** `uf1MeterVPotPage_()` returns an all-unassigned page for screen 3
  so V-Pot2/3/4 stay blank (no RTA-label bleed, no OOB) until the real table exists.
- **setView guard:** Loudness stays on `setView(0)` (readouts stream on ALL views; view-3
  acceptance is unproven — a bad setView(3) could starve the readouts).

### ✅ DONE 2026-07-24 — History graphic (`0x0122`), scrolling short-term line (HW-verified `4955331`)
Resolved by a Mac `ssl_core_probe` **view-3 trace** (view 3 PROVEN accepted — 25/26 stream,
readouts keep flowing; Loudness now on `setView(3)`, `REASIXTY_FORCE_VIEW` env override added).
- **`LoudScrollableHistory`(26) IS the scrolling Short-Term LUFS series** (idx 0 oldest .. high
  newest, floor −100, + a 13-float metadata trailer with the Integrated Target + counters).
- **The plot is TARGET-RELATIVE** — that dissolves the +31.6-vs-+23 puzzle:
  `axisLU = LUFS − target`, `byte = clamp((axisLU − rangeBottom)·180/span, 0,180)`,
  target = **param 36** (Integrated Target), rangeBottom/span = **param 47**. cap120 measured
  +31.6 because that session's target was −31.6; a −23 target gives +23. Nothing hardcoded.
- **Wire:** the UF1 needs the FULL sub-frame set (sf1 alone draws NOTHING). sf1 = the live line
  (newest anchored to the RIGHT edge, scrolls LEFT — HW-verified column order); sf0/sf2/sf3 =
  per-range chrome from cap120/121/122, selected by param 47 (`extension/src/uf1_loudness_chrome.h`).

### ★ REMAINING — sf2 momentary "now" region is FROZEN (live-render follow-up)
The right-hand ~20% of the plot (sf2) is the momentary/now region; it is currently the CAPTURED
snapshot (static). The scrolling short-term history + axis + readouts + V-Pot layer are correct.
To finish: render sf2's momentary region live (needs its encoding — likely DataType 11
LoudMomentary; its left/right split within sf2 TBD). Do this FRESH, not at the end of a long
session (Frank 2026-07-24 — the history column-order/direction cost many HW round-trips; the
now-point-at-80% turned out to be the frozen sf2 region, NOT an sf1 bug).
- **V-Pot operator layer — ALL 10 pages BUILT (`kUf1LoudnessVPots[10]`, `kUf1MeterPageCount[3]=10`).**
  Decoded the 0x010e label groups; every param index is a real TrackFX index from the Meter
  Pro dump (nothing guessed). `nameOnly` per slot mirrors SSL's wire form (value-alone vs the
  8-B name field). Pages 0-6 = cap109, pages 7-9 = `captures/loud_8_10.pcap`:
  - P0 24 Play mode · 25 History Window · **[V4 = "Scroll Timeline" — NO param in the dump; a
    GUI scroll action, left unassigned; do NOT guess]**
  - P1 47 Scale Range · 48 Display · 26 History Scroll
  - P2 30 Int Gating · 36 Integrated Target · 28 Dialogue Detection
  - P3 37 Target Variance · 33 Short-Term Int · 32 Momentary Int
  - P4 29 Min Dialogue · 34 Loudness Mode · 35 Surround Weighting
  - P5 27 Loudness True Peak · 49 Terminology · 31 Integrated Overlap
  - P6 38 Int Loudness Alert · 39 Int Dialogue Alert · 46 True Peak Max Alert
  - P7 40 Short-Term Max Alert · 42 Loudness Range Min · 43 Loudness Range Max
  - P8 41 Momentary Max Alert · 44 Dialogue Range Min · 45 Dialogue Range Max
  - P9 52 Save Loudness History · [V3/V4 blank — empty payloads on the wire]
  Page ORDER is capture-derived (forward paging) — HW-verify. Soft-key 2 = PLAY on Loudness
  (already in the burst). Capture flow to reproduce: `docs/capture-uf1-stoerpc.md`.
- Full capture record + the law: `docs/session-2026-07-24-uf1-loudness-capture.md`.

## Context to load in the fresh session
- [[uf1-meter-operator-layer]] — the operator layer + all the wire facts.
- [[handoff-uf1-meter-live]] — meter codec + goniometer.
- `docs/session-2026-07-23-uf1-meter-operator-layer.md`, `docs/session-2026-07-24-uf1-bugd.md`.
- Deploy trap: never `cp` over a running REAPER's dylib (SIGKILL); atomic `mv`
  ([[macos-codesign-after-install-name-tool]]).
