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
*(FIXED in `2bf2789` — kept for the record.)* The **FF1B keepalive landed inside
the image burst in ~11% of images** (87/800): the worker thread emitted it every
150 ms while the timer thread was mid-image. SSL **never** sends anything inside
a burst. Now serialised (sendBurst + keepalive deferral) — but **89% of images
were clean, so this alone cannot have caused a permanently black face.**

**2026-07-15 evening: the table above only ever covered BULK traffic. EP0 was a
blind spot — see NEXT item 1: SSL sends a 20-request FTDI vendor init at connect
that we never sent.**

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

1. ~~Compare the UF1 INIT / connect sequence~~ — **DONE 2026-07-15 evening,
   FINDING (`2bf2789`): SSL sends a full FTDI-style D2XX vendor init on EP0
   before its first bulk byte; we sent NONE of it.** cap84 t=54.05–54.23,
   device 9: reset + modem-status twice ~150 ms apart, SET_DATA 8N1,
   SET_FLOW none, SET_BAUDRATE 0xc068 idx 0x0200 ×3, **SET_LATENCY_TIMER
   2 ms**, purge RX ×6, purge TX ×1 — the textbook FT_Open sequence, so the
   "SSL 18 Comms Device" is an FTDI-compatible bridge and SSL 360 drives it
   via D2XX. `tshark -Y "usb.transfer_type == 0x02"` to see it; **every
   bulk-level parser (incl. parse_usbpcap.py) filters EP0 out — that is WHY
   all comparisons read "identical".** Now replicated verbatim in
   `UF1Device::open()`. Fits the symptom: connect-time device config,
   invisible in steady-state diffs; plausibly gates internal buffering the
   8.5 KB image burst needs (the goniometer is the only payload of that
   size). **HW-verify pending.**

2. **Capture SSL 360 driving this UF1 on the Mac, on Overview, with audio.**
   Still the decisive test if the vendor init doesn't fix it, because SSL 360
   provably draws on Frank's exact hardware. Same machine, same MeterPro, same
   track — the first apples-to-apples comparison. All previous captures
   (cap75/76/89) are from a Windows session with a possibly different plug-in
   and a different device state. macOS can sniff USB with Wireshark on the
   `XHC20` interface (no Windows box needed). **Then diff SSL's stream against
   `captures/cap100` (ours) — element set, values, ORDER, and now also EP0.**

3. **Meter vs Meter Pro.** Frank runs **MeterPro** — the plug-in announces
   `PerSslMeterProPlugin` and `/Presets/MeterPro/Default Preset.xml` in clear
   text. cap75/76/89 may be plain Meter. `PluginType` (f1) is ABSENT on the wire,
   so we never distinguish them.

4. ~~Serialise the image burst~~ — **DONE (`2bf2789`)**: the 35 chunks are
   enqueued atomically (`sendBurst`), the FF1B keepalive defers while the queue
   is non-empty (500 ms liveness override). Nothing of ours can land inside an
   image burst any more.

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
