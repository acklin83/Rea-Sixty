# HANDOFF — UF1 Overview goniometer stays black

**Status 2026-07-15, end of day.** Everything on the Meter view works except the
goniometer. I burned a full day and four hardware rounds on it and did not solve
it. This is what is MEASURED, what is REFUTED, and where I would go next.

**Frank confirmed SSL 360 DOES draw the goniometer on this exact UF1, this exact
Mac, with his MeterPro plug-in.** So it is our code, not his config, not the
hardware. That single fact was established only at the very end and it makes a
macOS-side capture of SSL 360 the obvious next move (see NEXT).

---

## Ruled out — MEASURED, with the evidence. Do not redo any of this.

All from `captures/cap100_uf1_overview_our_stream.log` = **our own OUT frame
trace** (`REASIXTY_UF1_TRACE=1` → `/tmp/reaper_uf1_frames.log`). Taking that trace
is the thing I should have done first; I verified the image offline three times
and *assumed* it reached the device.

| claim | evidence |
|---|---|
| **Our image is correct ON THE WIRE** | Reassembled from our own stream: **2536 complete 8560-byte images**, brightest byte 255, best one renders as a textbook goniometer cloud. `lit` median is 1 only because that is SILENCE — content appears exactly while audio plays. |
| **Frames byte-identical to SSL** | header `ff 67 fd 01 22`, len bytes `0xfd`/`0x3f`, payloads 251/61, chunk indices 0..34, order **34→0** (SSL: 2570/2570 descend, zero ascend). |
| **Checksum correct** | our `(sum+1)&0xFF` reproduces SSL's on **all 116663** frames of cap89, including every 257-byte `0x0122`. |
| **Timing fine** | our burst **7.0 ms** (SSL 4.7), cadence **33 Hz** (SSL 24.5). We are faster, not slower. |
| **Right screen** | selector `04 00` confirmed in the trace while painting. |
| **Element SET on (4,0) == cap75** | identical list, after adding `0x0110`/`0x011f` and dropping `0x0119`. |
| **Element VALUES on (4,0) == cap75** | every one byte-for-byte. Only diff: our label says `TruePk On`, SSL's `TruePk Off` — a string, not state. |
| **Burst not corrupted by concurrency** | `UF1Device::send()` is a queue drained by ONE worker → no byte-level interleave. |
| **89.1% of image bursts are perfectly clean** | 713 of 800 images have zero interleaved frames. |

### The only measured difference left — and it does NOT explain black
The **FF1B keepalive lands inside the image burst in ~11% of images** (87/800):
the worker thread emits it every 150 ms while the timer thread is mid-image. SSL
**never** sends anything inside a burst. Worth serialising (batch the 35 chunks
under the queue lock, or suppress the keepalive mid-image) — but **89% of images
are clean, so this cannot cause a permanently black face.** Do not ship it as
"the fix".

---

## Solved today (keep — all HW-independent and verified)
- **t10 geometry**: the plug-in's Lissajous is **ONE 17113-float array**, chunked
  (f7/f8/f9; **f7 is often ABSENT** — detect the tail by a short chunk). Shape = a
  diamond of ODD row widths `1,3,5,…,185,…,5,3,1` — **185 rows, 93²+92² = 17113**.
  Derive rows from n: `2n-1 = (2k-1)²`. Proof is physics on the COMPLETE array:
  mono → 185 lit cells over ALL 185 rows at frac **0.500, min=max**; corr −1 → 185
  lit in ONE row; L/R-only → 93 cells over 93 rows. (`captures/cap99`)
- **Resample**: FORWARD-map source pixels, keep the **MAXIMUM**. Backward
  nearest-neighbour loses the trace (source 185 wide vs UF1 92 = 2:1 shrink):
  measured 93/187 rows lit vs **185/187** with forward+max.
- **Peak/hold** = the plug-in's `f4 PeakValues`. Never re-derive it.
- **Numbers** ← `TextPeak(4)`/`TextRms(5)`/`TextVuPpm(1)`, **not** the Bar/needle
  types — those are CLAMPED (at −10 dBFS VuPpm reads 3.00, TextVuPpm 8.00).
- **Red LEDs** = `0x0128` bitmask: bit0 L ovl, bit1 L latch, bit2 R ovl, bit3 R
  latch, from `f5`/`f6`. (`captures/cap80`)

## REFUTED — my own inventions. Do not resurrect.
- *"the device latches on chunk 0"* — invented, never shown. Order 34→0 does match
  SSL, keep it, but it was never the bug.
- *"`0x0128`+`0x011d` = a per-cycle commit/latch"* — `0x0128` is the overload
  bitmask. `0x011d` is Overview-only and still unidentified.
- *"packed protobuf / MTU truncation / fragments"* — chunked via f8/f9.
- Every geometry fitted to **chunk boundaries** (2113 = the remainder chunk,
  5000 = the chunk size; neither was ever an array).
- *"`0x0009`/`0x0015`/`0x0016` should be `ff`"* — they are **per-session state
  (colour)**. Setting them to `ff` recoloured the VU readout on hardware. cap89
  holds `ff` and draws fine → they gate nothing. Send `00` (cap75/cap76).

---

## NEXT — in the order I would actually do them

1. **Capture SSL 360 driving this UF1 on the Mac, on Overview, with audio.**
   Now decisive, because SSL 360 provably draws on Frank's exact hardware. Same
   machine, same MeterPro, same track — the first apples-to-apples comparison.
   All previous captures (cap75/76/89) are from a Windows session with a possibly
   different plug-in and a different device state. macOS can sniff USB with
   Wireshark on the `XHC20` interface (no Windows box needed). **Then diff SSL's
   stream against `captures/cap100` (ours) — element set, values, and ORDER.**
   Everything I compared came from foreign captures; this removes that variable.

2. **Compare the UF1 INIT / connect sequence — never examined.**
   `captures/cap84_uf1_plugin_init.pcapng`. We only ever compared steady state and
   the screen-entry burst. If SSL enables a graphic layer at CONNECT time and we
   do not, the goniometer would never draw no matter what we send afterwards, and
   every comparison I made would still come out "identical". This is the biggest
   unexamined surface and it fits the symptom exactly.

3. **Meter vs Meter Pro.** Frank runs **MeterPro** — the plug-in announces
   `PerSslMeterProPlugin` and `/Presets/MeterPro/Default Preset.xml` in clear
   text. cap75/76/89 may be plain Meter. `PluginType` (f1) is ABSENT on the wire,
   so we never distinguish them.

4. **Serialise the image burst** (FF1B, see above). Real, but not the cause.

## Tooling
- Frame trace: launch REAPER with `REASIXTY_UF1_TRACE=1` → `/tmp/reaper_uf1_frames.log`.
  **Launch it yourself** — Frank restarting REAPER by hand loses the env var, and
  the flag is read once at init. Format: `[ts] I|O rc=0 len=N <hex>`.
  ⚠ It grows ~700 MB in minutes. Delete it after.
- Meter dump: `REASIXTY_T10_DUMP=1` → `/tmp/reasixty_meter_dump.log` (every
  DataType, all fields, + the reassembled t10 at 2 Hz).
- **NEVER write logs to `~/Desktop`** — Frank synchronises it, and TCC blocks
  reading a file another app wrote there. See [[never-write-to-desktop]].
- Probe signal: `analysis/gen_uf1_meter_probe_wav.py` (verified: corr comes out
  +1.000/+0.005/−1.000, tones exact). Play once per screen — the plug-in only
  computes the meters its SELECTED view needs.
- SSL announces **288 named objects in clear text** on our own control socket:
  `ssl_core_trace=1` → `/tmp/reaper_sslcore.log`, hex-encoded protobuf; extract
  printable runs. Read the names before guessing at bytes.

## Method lessons that cost the day
1. **Take the frame trace FIRST.** I verified the image offline three times and
   assumed it reached the device. It did — perfectly — the whole time.
2. **Pick the capture that reproduces the USER'S action.** cap76 cycles between
   meter screens; Frank's action is Channel→Meter = **cap75**. I diffed against
   cap76 for hours. cap89 has no `0x0100` at all.
3. **Two captures that disagree are two STATES, not a vote to win.**
4. **An exact sum is not proof** — my odd-diamond fit summed exactly, passed a
   physics test, and was fitted to a chunk boundary.
5. **Never record a guess as a fact.** My memory said "chunk 0 latches" and
   "0x0128 is the commit" as established findings. Both invented; both sent the
   next session down a dead end.
