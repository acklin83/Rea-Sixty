# HANDOFF — UF1 Loudness screen (capture + build), starting 2026-07-24

Fresh-context entry point. The meter operator layer (Overview/Analogue/RTA) is
DONE + HW-verified; Bug D (per-Digital-Type Overview bar scales) shipped `4cc9177`.
**Loudness is the one meter screen NOT yet built** and is the next job. The StoerPC
capture rig is UP — grab Loudness data now before it's freed.

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

## Then build (Mac)
- Add Loudness to `kUf1MeterScreenCycle` (currently 3 = OV/AN/RTA). It needs
  **Meter-Pro detection** — the impersonator doesn't surface PluginType yet; see
  [[ssl360-plugin-protobuf-protocol]] / [[gr-meter-source-search]] (DataType is a
  bare int32, PluginType ABSENT). Figure out how to tell Meter vs Meter Pro.
- Render the Loudness screen (entry burst + per-screen readout + history + bars per
  the captured scale). V-Pot operator layer for the 10 pages (labels via the same
  0x010e name/value / name-only path — see [[uf1-meter-operator-layer]]).

## Context to load in the fresh session
- [[uf1-meter-operator-layer]] — the operator layer + all the wire facts.
- [[handoff-uf1-meter-live]] — meter codec + goniometer.
- `docs/session-2026-07-23-uf1-meter-operator-layer.md`, `docs/session-2026-07-24-uf1-bugd.md`.
- Deploy trap: never `cp` over a running REAPER's dylib (SIGKILL); atomic `mv`
  ([[macos-codesign-after-install-name-tool]]).
