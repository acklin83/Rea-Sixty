# UF1 Meter — SSL→UF1 capture plan (StoerPC / USBPcap)

**Goal.** Capture *everything* SSL 360 sends the UF1 for the meter view, so we can
replicate every visual variation without a second trip to the box. The immediate
unblocker is the **live-value render trigger** (feature A), but while the rig is
set up we grab the full matrix of faceplates/scales/layouts, because — Frank's
point — PPM looks nothing like VU, and K-12/K-14/K-20 are each a different scale.

**Setup.** SSL 360 driving the UF1 (SSL's own driver, NOT our WinUSB — reinstall
if needed). USBPcap dev 8, VID_31E9 PID_0025 (`\\.\USBPcap3` may have moved —
re-probe per [[capture-workflow]]). A track with **SSL Meter Pro** + a **steady
tone** so the meters are alive and the faceplate/scale is visible with signal.
Keep SSL 360's plugin GUI open on a second monitor to set params.

**Method — one capture PER TIER**, stepping through that tier's list with a
~2-3 s pause between each change, saying the change out loud / noting wall-clock
time so the pcap can be split. Name files `capNNN_<tier>`.

**★ PREP STEP 0 — enumerate the FULL enum lists first.** The param dump only
samples 3 points, so we don't yet know how many values each enum has. Before
capturing, step each multi-value enum through EVERY detent in the plugin GUI and
write down each label. Critical unknowns:
- **Digital Type** (idx 6): Non-Linear, Linear 2x, then the K-scales — K-20? K-14?
  K-12? (dump only shows K-12). Likely 5 values.
- **Analogue Mode** (idx 8): VU, PPM — is PPM one type (manual says "PPM Type II")
  or several (Type I/II, DIN, Nordic, BBC, EBU)? dump shows only VU/PPM.
- **Channel Format** (idx 7): AUTO … PT 7.1 … Nndo 7.1.4 — the full surround list.
This list drives how many captures Tiers 1-2 actually need.

---

## TIER 0 — the render trigger (feature A unblocker) — DO FIRST
Feature A is byte-perfect on the wire but the UF1 won't re-render mid-stream
0x010e VALUE updates for V-Pot 2/3/4 (index 1/2/3); index 0 does. We need to see
what SSL sends AROUND a value change.
- On **Overview**, change **RMS Integration** (V-Pot 3) by ONE detent. Capture
  the full SSL→UF1 sequence: the 0x010e AND any element immediately before/after
  (candidates: 0x010d styling, 0x0102, 0x011a, a re-send of the label group, a
  dedicated commit byte). **This is the whole point of the trip.**
- Repeat one detent on **Analogue** (Reference Level) and **RTA** (Scale Top) to
  confirm the trigger is the same across screens.
- Also change a param via the UF1's own V-Pot (not the GUI) if possible — SSL may
  send a different sequence for surface- vs GUI-originated edits.

## TIER 1 — Analogue faceplates & scales  (baseline = VU, 0 VU=0 dBu, Ref=-18)
Capture each; diff against baseline to find the delta element (selector byte vs a
faceplate graphic vs label strings).
- **Analogue Mode** (idx 8): **VU**, then **PPM** (and every PPM sub-type found in
  prep). ← Frank: "PPM sieht gänzlich anders aus." The whole faceplate.
- **0 VU Line-Up** (idx 11): **+4 dBu**, **0 dBu**, **-2 dBu** — the VU scale
  reference marks / label.
- **Analogue Reference Level** (idx 9): **-36**, **-18**, **0 dBFS** (verify if
  discrete-3 or continuous — affects the value→needle mapping AND the scale).
- **Analogue Dual Format** (idx 12): **Stereo**, **Mid-Side**, **Custom** — the
  L/R vs M/S channel labels and any layout change (Custom → idx 13/14 sources).
- **Analogue Max Needle** (idx 10): Off / 2 sec / Infinite — does the lag-needle
  presence change the face?
- **Analogue Meters LED Overload** (idx 15): 0 dB / 13 dB / Off — the red-zone
  threshold (already partly in cap80; confirm the element).

## TIER 2 — Overview digital-meter scales  (baseline = Non-Linear)
- **Digital Type** (idx 6): **Non-Linear**, **Linear 2x**, **K-20**, **K-14**,
  **K-12** (full list from prep). ← Frank: each K is a different bargraph SCALE
  (where 0 sits, the dB range, the labels 0/5/10/20/…/inf). Capture EACH.
- **Digital Peak Hold** (idx 3): Off / 3 sec / Infinite (behavior; confirm no face
  change).
- **Digital True Peak** (idx 4) + **True Peak HQ** (idx 2): Off / On.
- **Channel Format** (idx 7) — surround: AUTO (stereo) baseline, then at least
  **5.1** and **7.1** to see how the bargraph COUNT/LAYOUT changes (and the
  Analogue needle count). ⚠ Big scope — mark optional/last; do only if time.
- **RMS Integration** (idx 5), **Lissajous Fade** (idx 16): value-only, covered by
  Tier 0's trigger capture.

## TIER 3 — RTA scales
- **RTA Scale Top** (idx 22) × **Scale Bottom** (idx 23): capture the useful
  combos — **0 / -120** (baseline), **-60 / -70**, **-110 / -120** — the dB axis
  labels (0/12/24/…/120 in the manual) and the bar mapping both shift.
- **RTA Weighting** (idx 20): None / A-Weighting / C-Weighting (curve + label).
- **RTA Selected Band** (idx 51): select a band (e.g. 1 kHz) — the highlighted
  column + the "160 Hz / current / max" readout.
- **RTA Averaging** (idx 21), **RTA Peak Hold** (idx 18), **Analysis Source**
  (idx 19): behavior/label — quick captures.

## TIER 4 — soft-keys, pages, submenus
- **Page switching**: on EACH screen step page 1→2 (RTA also →3). Capture (a) the
  KEY SSL uses (arrow keys under V-Pot 3/4? — confirm the button id), (b) the
  page's V-Pot 2/3/4 LABEL burst (we have page-1 only), (c) any page indicator
  element.
- **Reset** soft-key: press — does it clear clip/max on the display, and what does
  SSL send?
- **Fine Encoder Mode** soft-key: toggle on/off — the on-screen FINE indicator +
  whether the V-Pot step visibly changes.
- **Presets Menu** soft-key: OPEN it, scroll the 4th encoder, load one. This is a
  whole submenu OVERLAY — capture the open, the scroll (highlight moves), and the
  load/close. Entirely new layout.

## TIER 5 — Loudness screen (Meter Pro only; out of our cycle today)
Lower priority, but grab it while set up:
- Enter Loudness (SSL selector 05). Capture the full entry layout.
- **Loudness Meter Scale Range** (idx 47): -18..+9 / -36..+18 / -54..+27 (the
  scale). **Display Type** (idx 48): Absolute/Relative. **Terminology** (idx 49):
  LU(FS)/LK(FS). **History Window Size** (idx 25). **Play/Pause** (idx 50).
- The 0x0122 sub-frames (history plot) per [[uf1-meter-codec-decoded]].

## Also worth a quick grab
- **Mono vs stereo source**: does Overview/Analogue drop to one bargraph/needle?
- **Entry from Channel view** for each screen (the cap75-style full entry burst),
  re-confirmed with the current firmware.

---

## What to extract from each capture
For every setting: split the pcap at the change, isolate the SSL→UF1 OUT frames,
and DIFF the burst against the baseline for that screen to find the delta
element(s) — a selector byte, a label string (0x010e/0x0104), a scale element, or
a full graphic. Feed each into the meter burst tables. The Tier-0 trigger is the
one that unblocks feature A; everything else fills the faceplate/scale/page
matrices.
