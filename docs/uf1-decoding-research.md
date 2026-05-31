# UF1 Decoding — Pre-Capture Research

**Status:** research only, **no hardware in hand yet, no captures taken.**
Everything below the "USB identity" line is *hypothesis* extrapolated from
the UF8/UC1 decode and from public SSL material — flagged as such. It exists
so that the moment the loaner UF1 arrives we capture the right things on the
first plug-in instead of burning a session figuring out what to look at.

## Why this doc exists

The Swiss SSL distributor is lending us a **UF1** to decode. 🎉 This is the
third device in the SSL 360° "U-series" after the UF8 (8-fader, decoded — see
[`protocol-notes.md`](protocol-notes.md)) and the UC1 (plug-in controller,
decoded — see [`protocol-notes-uc1.md`](protocol-notes-uc1.md)). The UF1 is
the single-fader advanced DAW controller.

> ✅ **Manual now in the repo.** `docs/docs/SSL UF1 User Guide_Rev4.0.pdf`
> (Rev 4.0, 200 pp.) was added 2026-05-31. It is distilled into
> [`uf1-manual-reference.md`](uf1-manual-reference.md) — read that for the
> confirmed hardware/architecture detail; this doc keeps the decode strategy
> and open questions.

## Confirmed by the manual (2026-05-31)

Reading `SSL UF1 User Guide_Rev4.0.pdf` upgraded several hypotheses below from
guess to fact (detail in [`uf1-manual-reference.md`](uf1-manual-reference.md)):
the **two-stack architecture is identical to the UF8** (HUI/MCU + proprietary
Plug-in Mixer, p.28); the **SEL-key colour-follow is the same feature we
already ship** (p.23); the UF1 needs a **360° handshake at boot** before its
displays render (p.192) — so an init/wakeup replay is required, same as UF8.
**Still open** (capture-only): the real **VID/PID/endpoints**, the **jog-wheel**
and **foot-switch** encodings, and how the **4.3" graphical content** (SSL Meter
/ EQ-curve) is pushed.

## What the UF1 is (public material)

A single-channel "advanced DAW controller" — the desktop sibling of the UF8.
Confirmed hardware (SSL product page + Sound on Sound review + retailers):

| Feature | Detail |
| --- | --- |
| Fader | **1×** 100 mm motorised, touch-sensitive |
| Big display | **4.3" IPS LCD TFT** colour — the main meter/parameter screen |
| Small display | **1.77" LCD TFT** colour — secondary readout (around the channel encoder) |
| Channel encoder | one large **notched/pushable encoder** — channel banking, mouse-wheel emulation (Focus mode), system-volume |
| Jog wheel | large weighted **jog/shuttle wheel** |
| Transport | **5** large illuminated transport buttons (play/stop/rec/rew/ff-ish) |
| Secondary row | **6** buttons above transport (go-to-start/end, metronome, loop-rec, …) — "Secondary Transport Keys" |
| Soft keys | **46** assignable backlit rubber keys, **RGB** (12 selectable colours since 360 v1.6) |
| Connectivity | **hi-speed USB** (no ethernet, unlike the SSL audio interfaces) |
| Stands | included, 6 elevation angles (cosmetic, irrelevant to us) |

### Same two-stack architecture as UF8/UC1

The UF1 is **not** a class-compliant MIDI controller. Like the UF8 and UC1 it
runs the SSL dual-stack (see [`uf8-manual-reference.md`](uf8-manual-reference.md)):

1. **SSL 360°** owns the device over a **vendor-USB pipe**
   (`bInterfaceClass = 0xFF`) and surfaces virtual-MIDI ports to the DAW.
2. **DAW** talks **MCU/HUI** over those V-MIDI ports for the generic
   fader/transport/bank control.
3. **Plug-in Mixer Layer** is the SSL-proprietary protocol carrying track
   colour, SSL-plugin parameters, GR/VU — the bit MCU/MIDI can't do.

Public confirmation the UF1 shares this model:
- UF1 has "dedicated HUI and MCU DAW Profiles" *and* SSL 360° proprietary
  control — same hybrid as UF8.
- 360 v1.6 added "Customizable UF8 **& UF1** RGB soft key colours" and the
  "Plug-in Mixer **UF8/UF1** SEL Keys" colour-follow toggle (already
  referenced in [`uf8-manual-reference.md`](uf8-manual-reference.md) lines
  57/90/162) — i.e. the UF1's SEL key colour-follow is the **exact feature
  Rea-Sixty already implements** for the UF8 in `ColorSync.cpp`.
- UF1 was added alongside UF8/UC1 in the **SSL 360° v1.6** release (Oct 2023),
  which also shipped UF1 firmware. So a UF1 plugged into a current 360 install
  speaks a wire format from the *same* protocol family we've already cracked.

**Implication:** the UF1 is decodable with the **identical methodology** —
passive USBPcap capture of 360° ↔ UF1 traffic on Windows, then diff. No new
technique required. The frame grammar is almost certainly the shared
`FF <payload> <checksum>` form (checksum = `sum(payload) mod 256`) with the
`31 60` session header on the IN direction.

## USB identity (to confirm on first plug-in)

Known sibling IDs (from our decoded devices):

| Device | VID | PID | Notes |
| --- | --- | --- | --- |
| UF8 vendor-USB | `0x31E9` | `0x0021` | colours/displays/faders ([`Protocol.h`], `UF8Device.h`) |
| UF8 HID | `0x31E9` | `0x0022` | knobs/buttons MCU/HUI path (`HidDevice.h`) |
| UC1 vendor-USB | `0x31E9` | `0x0023` | `UC1Protocol.h` `kVid/kPid` |
| **UF1 vendor-USB** | `0x31E9` (assumed) | **`0x0024` (HYPOTHESIS)** | next free PID in the run — **verify by enumeration** |
| **UF1 HID?** | `0x31E9` (assumed) | `0x0025?` (HYPOTHESIS) | UF8 had a separate HID device; UF1 *may* too — confirm |

> **First thing to do with the device:** `lsusb -v` (Linux) /
> USBView / Zadig (Windows) / `system_profiler SPUSBDataType` (mac) and write
> the **real** VID/PID + endpoint addresses into this table. Do not assume
> `0x0024` — SSL may have left gaps. Capture the endpoint map too (UF8 uses
> `0x02` OUT bulk / `0x81` IN bulk).

## How UF1 maps onto our existing code

The UF1 is a **hybrid** of the two devices we already drive:

| UF1 trait | Closest decoded analog | Reusable asset |
| --- | --- | --- |
| 1 motorised touch fader | UF8 strip fader | `buildFaderPosition` / `FF 1E 03`, motor-limp `FF 1D 02`, touch `FF 20 02`, position `FF 21 03` — likely **strip index fixed at 0** |
| RGB soft keys (46) | UF8 per-strip + global LEDs | `FF 38/39 04 <cell>` pair-write family, tri-state Off/Dim/Bright (cap44) |
| SEL-key colour-follow | UF8 SEL DAW-colour | `ColorSync.cpp`, `FF 38/39` SEL palette (cap33) — same feature, same bytes likely |
| 4.3" **text/value** zones (names, timecode, param labels, fader dB) | UF8 colour-TFT LCD zones + UC1 text/7-seg zones | `FF 66 …` zone family + `UC1Protocol.h` text zones — reusable. **NB: the UC1 has no graphical screen** (7-seg + small text LCDs only), so there is **no precedent for the 4.3" graphical content** (EQ curve, SSL Meter) — that part is genuinely new |
| Channel/notched encoder | UC1 encoders / UF8 channel encoder | `encoder_*` actions (see [`concepts.md`](concepts.md)) |
| Jog/shuttle wheel | **NEW** — neither UF8 nor UC1 has one | needs fresh decode; probably an IN-direction delta frame like the V-Pot rotation |
| 5 transport + 6 secondary keys | UF8 transport buttons | button-event `FF 22 03 <id> 00 <state>` — new ID map to capture |

So roughly **70% is re-decode-by-analogy** (fader, LEDs, SEL colour, display
zones) and ~30% is genuinely new: the **jog/shuttle wheel** and the **button-ID
map** for transport/secondary/46 soft keys.

### Likely a new `UF1Device` + `UF1Protocol` pair

Mirroring `UF8Device`/`UC1Device`: a libusb wrapper claiming the UF1 PID on
its own worker thread, plus a `UF1Protocol.{h,cpp}` of frame builders. The
single-fader topology means most "strip N" loops collapse to N=0, so the
device class is simpler than `UF8Device`. Integration point is the same
`csurf_inst` surface in `main.cpp`. **Not** a goal for the loaner window —
the loaner window is for **capture + decode**, code comes after.

## Capture plan (when the device arrives)

Follow [`windows-capture-workflow.md`](windows-capture-workflow.md) /
[`windows-capture-workflow-uc1.md`](windows-capture-workflow-uc1.md). Priority
order, each an isolated USBPcap capture so the diff is clean:

1. **enum + idle baseline** — fresh replug with 360° running; grab the
   **init/wakeup sequence** (the UF8 needed one before anything renders —
   `protocol-notes.md` "Init sequence"). Confirm VID/PID + endpoints here.
2. **fader sweep** — full bottom→top→bottom throw; touch on/off; confirm
   16-bit LE position and whether strip index is `00`.
3. **transport buttons** — press each of the 5 transport + 6 secondary keys
   once, in a known order, to build the button-ID table.
4. **soft-key enum** — press all 46 soft keys in order (left→right, top→bottom)
   → ID map. Then a colour sweep on a couple of keys for the RGB byte table
   (expect the `FF 38/39 04` family).
5. **jog wheel** — slow CW, slow CCW, fast — decode the delta frame
   (genuinely new; no analog in UF8/UC1).
6. **channel encoder** — rotate + push in each of its modes (bank / Focus /
   volume) → delta + push frames.
7. **4.3" display content** — drive a track name / parameter change in 360°
   and capture the screen-write zones; compare to UC1 zone commands.
8. **SEL colour-follow** — toggle "Plug-in Mixer UF8/UF1 SEL Keys" and change
   a REAPER track colour; confirm it's the same `FF 38/39` SEL palette as UF8.

## Decoding the 4.3″ display / SSL Meter

The big colour TFT (SSL Meter: Peak/RMS bars, VU/PPM needles, phase
correlation, L/R balance, Lissajous vector scope, 31-band RTA — plus the
Plug-in-Mixer EQ-curve graph) is the hardest and most uncertain part. Notes
below capture the strategy and the conclusions from the 2026-05-31 design
discussion.

### Strong prior: SSL pushes *values*, not bitmaps

No device we've decoded streams a framebuffer. The UF8 scribble strips are
**colour TFTs** yet are driven entirely by semantic commands (text zones
`FF 66 …`, palette index, meter level 0–31, V-Pot-bar position) — never pixels.
The UC1 has **no graphical screen at all** (3-digit 7-seg + small text LCDs +
LED rings/meters). So SSL's consistent design is **device-side rendering**: the
firmware holds the renderer, the host sends numbers. That's the good news —
"decode" likely means *find which opcode carries which value*, not *reverse a
pixel format*.

**Caveat:** the dense graphics (**Lissajous**, **31-band RTA**) have **zero
precedent** in our UF8/UC1 decode (UC1 has no graphical display). The UF1 is
advertised **hi-speed USB** (UF8/UC1 are likely full-speed), which *could* mean
those specific surfaces are pushed as partial bitmaps. Settle it on capture #1.

### Step 1 — bitmap vs. value (the deciding test)

| Signature | Bitmap / framebuffer | Value-push (expected) |
| --- | --- | --- |
| Bandwidth | big fixed OUT blocks ≈ W×H×bpp (480×272×2 ≈ 261 KB/frame) | small frames, scale with meter count not pixels |
| Freeze-signal test (mute audio) | screen keeps streaming identical blocks (double-buffer) | traffic collapses to heartbeat |
| Entropy | payload looks random/compressed | clear `opcode + payload`, ramps monotonically with level |
| Enumeration | check `bcdUSB` + EP max-packet on `lsusb -v` | — |

### Step 2 — isolate each meter with a crafted test signal

Same calibrated-ramp method that cracked UF8 VU / UC1 `FF 13 04` / UC1 needle
`FF 5B`. One element per capture, diff against idle baseline:

| Element | Test signal | Expected wire shape |
| --- | --- | --- |
| Peak / RMS bargraph | sine at −20/−12/−6/0 dBFS | level array, 1 byte/segment (cf. UF8 `FF 66 21 09`) |
| VU / PPM needle | calibrated steady tone | position byte/needle (cf. UC1 `FF 5B 02 00 <pos>`, pos≈dB×10) |
| 31-band RTA | **slow sine sweep 20 Hz→20 kHz** (pink noise = all bands) | one value walks the array index → reveals layout + band order |
| Phase correlation | mono (+1) / L−R inverted (−1) / uncorrelated noise (≈0) | single signed byte swings |
| L/R balance | tone hard-L then hard-R | single byte swings |
| Lissajous scope | mono→45° line; L-only→horizontal; sine L + 90°-shift R→circle | (x,y) point pairs → point cloud; few bytes → on-device synth; nothing structured → bitmap region |
| EQ-curve graph (PM) | change one EQ band (LF gain) by a known step | likely drawn on-device from CS params we already push (freq/gain/Q), or a precomputed polyline |

The SSL Meter plug-in GUI **mirrors the UF1 display 1:1** (manual p.180), so the
on-screen values are ground-truth for the byte-correlation.

Generate the calibrated WAVs for every row above with
[`analysis/gen_test_signals.py`](../analysis/gen_test_signals.py)
(`python3 analysis/gen_test_signals.py` → `captures/test-signals/`, git-ignored).

### Can we just forward the SSL Meter stream 1:1? — No.

Asked 2026-05-31. The graphics are **not** produced by the plug-in. Topology
(see [`plugin-ipc-notes.md`](plugin-ipc-notes.md)):

```
SSL Meter (VST3) ──[JUCE IPC: Apache Thrift, ENCRYPTED envelope]──▶ SSL360Core ──[vendor-USB]──▶ UF1
```

- The plug-in only emits **data** (params, meter values) to **SSL360Core** over
  Thrift wrapped in `kEncryptedThriftContainerThriftId`.
- **SSL360Core is the renderer *and* the exclusive USB owner** — it makes the
  UF1 frames, and it can't co-own the device with us.

So there is no plug-in-side "stream" to relay, and Core (the renderer) is the
exact component we replace. Two near-variants and their verdicts:

1. **Raw USB-MITM (keep Core running, proxy its USB OUT):** the only literal 1:1
   path, but requires a fake-device shim (Core claims USB exclusively) and keeps
   the **entire SSL stack as a hard dependency** — defeats the project. Not a
   product path.
2. **Value-relay via REAPER (the sane version):** keep SSL Meter loaded, read its
   computed values through host hooks (as we already do for GR via the PreSonus
   `GainReduction_dB` extension) and format our own UF1 frames. Works only as far
   as the plug-in exposes values to the host — likely simple meters (Peak/RMS,
   correlation, maybe VU); the **RTA array and Lissajous cloud almost certainly
   are not exposed**.

Tapping the plug-in→Core data directly is rejected: it's **encrypted**, so it
would mean breaking SSL's envelope (not passive USB observation) — collapsing
the project's interop rationale — and `plugin-ipc-notes.md` already records the
decision *not* to reimplement the Thrift IPC / encryption envelope.

### Staging (recommendation)

- **Phase A** (cheap): non-Meter 4.3″ content — names, timecode, V-Pot param
  labels/values, soft-key labels, colour, fader dB → reuses the known
  `FF 66 …` zone family.
- **Phase B**: simple meters — Peak/RMS bars, VU needles, phase/balance, RTA
  (value arrays; calibrated decode; light DSP, or value-relay from SSL Meter).
- **Phase C** (hardest, optional): Lissajous + full SSL-Meter parity + EQ-curve
  graph. Most DSP, possibly bitmap. The natural v1 cut line.

**Two problems, not one:** (a) the *wire format* (tractable via the tests above)
and (b) the *data source* — because we replace SSL360Core we have no SSL Meter
feeding us, so anything not value-relayable means Rea-Sixty computes the DSP
itself from a REAPER audio tap. Determining (a) also tells us how expensive (b)
is.

## Open questions

- **VID/PID/endpoints** — unverified; `0x0024` is a guess. Confirm on enum.
- **Separate HID device?** UF8 splits vendor-USB (`0x0021`) from HID
  (`0x0022`). Does the UF1 do the same, or is it single-interface like the UC1?
- **Init/wakeup sequence** — does the UF1 need one before its display/LEDs
  render (UF8 did, sending raw colour frames to a cold device rendered nothing)?
- **Jog wheel encoding** — absolute vs. relative? signed delta? shuttle vs.
  jog distinction in the wire format?
- **4.3" display** — bitmap framebuffer push (heavy) or addressable text/graphic
  zones like UC1 (light)? Determines how much we can realistically drive.
- **Does Rea-Sixty even need to fully drive the UF1 to be useful?** The headline
  win for UF8 was *track colour on scribble strips*. The UF1 has one fader and
  one big screen — the analogous win is colour-follow SEL keys + the 4.3"
  showing the focused track/param. Scope this against effort before committing
  code.

## Sources

- [SSL UF1 product page](https://solidstatelogic.com/products/uf1) (403 to bots; features cross-referenced via retailers)
- [SSL UF1 User Guide Rev 4.0 (PDF)](https://eu1.download.solidstatelogic.com/UF1/SSL%20UF1%20User%20Guide_Rev4.0.pdf) — **not yet mirrored into `docs/docs/`**
- [Sound on Sound — SSL UF1 review](https://www.soundonsound.com/reviews/solid-state-logic-uf1)
- [SSL 360° v1.6 update (added UF1 RGB soft keys, SEL colour-follow)](https://solidstatelogic.com/media/ssl-enhances-controllers-and-plug-ins-with-latest-ssl-360%C2%B0-v1.6-update)
- [SSL 360° Downloads & Release Notes](https://support.solidstatelogic.com/hc/en-gb/articles/4408123894417-SSL-360-Downloads-and-Release-Notes)
- Internal: [`protocol-notes.md`](protocol-notes.md), [`protocol-notes-uc1.md`](protocol-notes-uc1.md), [`uf8-manual-reference.md`](uf8-manual-reference.md), [`architecture-decision.md`](architecture-decision.md), [`concepts.md`](concepts.md)
</content>
</invoke>
