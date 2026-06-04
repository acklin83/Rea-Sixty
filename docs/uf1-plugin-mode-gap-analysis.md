# UF1 Plugin Mode — decode gap analysis (vs SSL UF1 User Guide Rev4.0, p180–192)

What the manual says Plugin Mode does, vs what we've reverse-engineered (cap52–76).
✅ decoded · ⚠️ partial / needs work · ❌ not decoded.

## Two large-screen views (MODE button = id 0x20 toggles)
- **Channel Strip Mode** (backlit) — control 360°-enabled channel strips + EQ curve.
- **Meter Mode** (yellow) — control SSL Meter / Meter Pro (sub-screens: Overview / Analogue / RTA;
  Frank also has **Loudness** → likely Meter Pro / newer fw; Rev4.0 manual lists only 3).

## Physical controls — all ✅ (ids decoded cap53–63)
Fader (read+motor), 5 encoders + jog, all buttons. Manual confirms roles:
- PLUG-IN toggle = control plugin output fader/pan vs DAW fader/pan.
- FLIP disabled in Plugin Mode. MASTER decouples fader (channel-select via Channel Encoder).
- Channel Encoder = bank by 1 / push = DAW-host select. Bank keys = ±8. Cursor/NAV cross = move across strips.
- MODE / Page keys / 5-8 (no fn) / Scrub / 360-Layer — all ids known.
- Transport: SSL360 relays to DAW via HUI/MCU; we drive the UF1 buttons (vendor FF22) — fine.

## Channel Strip Mode screen — element status
- **EQ display graph** ✅ — `0x0122` 251-col height array (cap73); colour via FF38 id 0x03 (cap74).
- **Parameters page (1/8)** ✅ — paging Left/Right; `0x011c` shows page.
- **Soft Keys (4) + labels** ✅ addr `0x0104`. Logical per-page assignments now KNOWN from manual
  p188 tables (CS2 / 4K B / 4K E / 360 Link) — see below.
- **4× V-Pot param readouts (name+value) + readout bars** ⚠️ **RE-DECODE NEEDED.** Manual p187/p188:
  the screen shows the 4 current-page params SIMULTANEOUSLY (e.g. Width/Mic/Out Trim/Comp Mix), one
  per V-pot, each with a readout bar. My cap72 wrongly concluded a single focused readout (`0x010e`).
  → capture: turn ONLY V-pot 1, find its name/value/bar addresses; repeat 2,3,4.
- **Small channel-info LCD zone** (manual p182 "Small LCD Layout") ⚠️/❌ — `0x00xx` plane (we saw
  0x0004="PAN", 0x000c="dB"). Holds: CS TYPE, DAW track colour, **TrkNam**, **O/PdB output fader dB**,
  **Dynamics Metering (gate+comp GR)**, Pan label/value/bar, Bypass. Not systematically decoded.
- **Timecode** ❌ — needs HUI/MCU timecode configured in REAPER; then decode the digit frame
  (our `0x011c` showed `REAPER|N/8|OFF` placeholder because TC wasn't configured).
- **DAW Host label** ⚠️ ("HOST" text seen). **Solo Active** ❌. **Fine-mode indicator** ❌ (Quick Key 2 = fine).

## Meter Mode — status (cap75 static, cap76 audio+modes)
Structure ✅: MODE→yellow; `0x011c` = per-channel dB readouts (−inf silent); `0x010e` = settings;
`0x0104` = screen label (OVERVIEW/ANALOGUE/RTA[/LOUDNESS]) + RESET/FINE/PRESETS soft-keys;
`0x0122` = animated graphic, length per mode (FD/253 bars, D2/210 RTA, 42/66, 3F/63).
Graphic CODECS ❌ (own chapter):
- **Overview**: digital L/R bargraphs, T PEAK & RMS text (max/current), Lissajous phase scope,
  L-R balance bar, phase-correlation meter. (bar bytes {0,3,14,15} = packed segment — not decoded.)
- **Analogue**: VU / PPM moving-coil needles + text readouts.
- **RTA**: 31-band analyser bars + selected-freq readout.
- **Loudness** (if Meter Pro): not in Rev4.0 manual.
Meter V-pot param pages also known from manual (Overview/Analogue/RTA tables p189–191).

## Manual gift — Soft Key & V-Pot assignment tables (p188), per page (1–8)
We no longer need to capture these logically; they're documented per EQ type:
- **CS 2 / 4K B / 4K E**: SK1–4 + V-Pot1–4 per page (Ø, PRE, SOLO SAFE, PLUG-IN, S/C, HQ MODE,
  A/B, EQ bells, EQ COLOUR [4K E only], Width, Mic, In/Out Trim, High/Low Pass, LF/LMF/HMF/HF
  Gain/Freq/Q, Comp Mix, Ratio/Threshold/Release, Range, Expand, Fast Attack…).
- **360 Link**: adds SATURATION, EQ TYPE/EQ IN, Gate params, etc.
(Full tables: docs/docs/SSL UF1 User Guide_Rev4.0.pdf p188.)

## What we still NEED to decode (priority order)
1. **4 V-Pot param readouts + readout bars** (Channel Strip Mode) — re-capture, per-V-pot. ← corrects cap72.
2. **Small channel-info LCD zone** (`0x00xx`): TrkNam, O/PdB, Pan, CS TYPE, Bypass, **Dynamics GR meter**.
3. **Meter graphic codecs**: Overview bargraphs + scope + correlation, Analogue VU, RTA 31-band.
4. **Timecode** digit frame (after configuring HUI/MCU TC in REAPER).
5. Status indicators: Solo Active, Fine-mode, Host label.
6. Then: NATIVE BUILD (UF1Surface + WinUSB-bind + init-replay cap66 → send).
