# UF1 User Guide — Reference for Rea-Sixty

Distilled from `docs/docs/SSL UF1 User Guide_Rev4.0.pdf` (Rev 4.0, 200 pp.,
SSL 360 v2.0-era). Companion to [`uf8-manual-reference.md`](uf8-manual-reference.md).
Only the parts relevant to building a native REAPER↔UF1 path without SSL 360° /
SSL plugins. Pairs with the pre-capture strategy in
[`uf1-decoding-research.md`](uf1-decoding-research.md).

The UF1 is the **single-fader** sibling of the UF8. Same SSL 360° two-stack
model, same protocol family — but one channel strip's worth of fader/LEDs plus
**two graphical TFTs** (a 4.3" main + a 1.77" channel screen), a **jog/shuttle
wheel**, a **notched channel encoder**, **46 user keys**, and **2 foot-switches**.

## What SSL ships (and why we'd replace it)

Identical architecture to the UF8 (manual p.28). Two stacks:

1. **SSL 360°** — desktop app + V-MIDI driver. Owns the UF1 over a vendor-USB
   pipe. "The brains behind your UF1." Surfaces virtual MIDI ports to the OS.
2. **DAW** — opens the V-MIDI port as an MCU/HUI controller.

Three protocols in play (p.28):

- **HUI** — Pro Tools.
- **MCP/MCU** — Logic, Cubase/Nuendo, Live, Studio One, LUNA, Pyramix,
  **REAPER**, Bitwig, DP, Mixbus. No track colour in the protocol.
- **NATIVE / Plug-in Mixer** — SSL-proprietary. Carries track colour, SSL
  plug-in params, GR/VU, EQ curve, SSL Meter. *"uses neither HUI nor MCU."*
  Only **Transport** is still HUI/MCU when on the Plug-in Mixer layer.

### V-MIDI port map (p.28) — one port per layer, not a group

Unlike the UF8 (which eats a whole port-group per unit), the single-channel
UF1 uses **one** V-MIDI port per DAW layer:

| UF1 Layer | V-MIDI Port |
| --- | --- |
| 1 | SSL V-MIDI Port 1 |
| 2 | SSL V-MIDI Port 5 |
| 3 | SSL V-MIDI Port 9 |

Up to **3 DAWs simultaneously**, switched with the 360°/Layer key. A layer can
instead be set to **MIDI CC** (port 1/5/9, for VIs) or to the **Plug-in Mixer**.

**Rea-Sixty's job (if we pursue UF1):** replace stack #1 — open the UF1 vendor
interface ourselves and drive its displays/LEDs/fader/SEL-colour from REAPER's
API, no SSL plugin required on any track.

## Hardware layout (p.16–19, front-panel callouts 1–21)

| # | Control | Notes (for input/output decode) |
| --- | --- | --- |
| 1 | **Small-screen soft key** | one button above the 1.77" screen; V-Pot param control, DAW-dependent |
| 2 | **Small screen** (1.77" colour TFT) | track meter, 6-char name, V-Pot readout, REC-arm state |
| 3 | **Small-screen V-Pot** | pushable encoder; controls selected param of the fader channel (Pan push = centre) |
| 4 | **100 mm motorised fader** | touch-sensitive — same family as UF8 strip fader |
| 5 | **SOLO / CUT / SEL** | three per-channel LED buttons (single strip) |
| 6 | **FLIP** | swap V-Pot ↔ fader assignment |
| 7 | **MASTER** | fader controls DAW master (MCU); decouples channel in Plug-in Mixer |
| 8 | **4× large-screen soft keys** | top row of the 4.3"; 40 user keys across 10 pages |
| 9 | **Large screen** (4.3" IPS TFT) | timecode, names, V-Pot readouts, EQ curve, **SSL Meter** visualisation |
| 10 | **4× large-screen V-Pots** | the four encoders under the 4.3" |
| 11 | **360° / Layer key** | short = open/min SSL 360°; long-hold + soft key = switch layer |
| 12 | **BANK < >** | bank tracks onto the surface |
| 13 | **MODE & 5-8 keys** | MODE cycles large-screen modes; 5-8 shifts the 4 V-Pots to chans 5–8 |
| 14 | **< > (page) keys** | page through the 10 soft-key pages (sit beneath V-Pots 3 & 4) |
| 15 | **Large notched CHANNEL encoder** | modes: FADER SEL / `<>` bank / NUDGE (PT) / FOCUS (mouse-wheel) / VOLUME (system). Pushable (mode menu / host select) |
| 16 | **Cursor keys + Mode (circle) key** | zoom / arrange navigation; plug-in slot select (Logic) |
| 17 | **Jog/shuttle wheel** | weighted; timeline scroll — **NEW control, no UF8/UC1 analog** |
| 18 | **SCRUB key** | puts jog into scrub; Factory/User mode |
| 19 | **Transport keys (5)** | Rewind, Forward, Stop, Play, Record (left→right) |
| 20 | **Secondary transport keys (6)** | customisable; default Loop/Click/Quick-keys; SHIFTed = automation modes |
| 21 | **SHIFT key** | re-purposes secondary row to automation (Off/Read/Write/Trim/Touch/Latch) |

So the V-Pot count is **5** (1 small-screen + 4 large-screen). "Fader Mode"
(green "FAdr") maps the 4 large V-Pots to four track **volumes** at once.

### Connector panel (p.19)

- **USB-C** — to computer. Carries *all* DAW↔UF1 comms via SSL 360°.
- **THRU (USB-A)** — **built-in USB hub.** Chain UF1 ↔ UF8/UC1 off one host
  port (up to 4×UF8 + UC1 + UF1 theoretically). ⚠️ **Decode implication:** a
  chained rig enumerates as a **hub with multiple SSL devices behind it** —
  capture/enumeration must walk the hub, and our libusb open must target the
  UF1 by PID regardless of bus topology.
- **DC** — 12 V 2 A.
- **FS1 / FS2** — ¼" foot-switches (normally-closed, momentary). Assignable
  DAW commands / keyboard macros. **New input class vs UF8/UC1.**

### Boot / handshake (p.192) — confirms a 360° wakeup is needed

On power-up the UF1 does a "fader wave", shows **"UF1 Initialisation
Complete"**, then **"Awaiting Connection to SSL 360°"** until the app connects;
**"SSL 360° Connection Lost. Attempting to Reconnect"** if the link drops.
→ Same pattern as the UF8's required init/wakeup sequence: the device sits idle
until 360° performs a mode-set handshake. **We must replay that handshake at
`open()` before any display/LED renders** (see `protocol-notes.md` "Init sequence").

## Large-screen / small-screen zones

These are the addressable output targets we'd have to drive. Two layouts —
**DAW (MCU) mode** and **Plug-in Mixer mode**.

### DAW mode — REAPER (p.135–136)

**Small (1.77") LCD:** Top-Zone soft-key label · REC-enabled flag · Fader-Sel
indication (fader # in bank) · UpLCD 6-char track name · LowLCD (blank under
stock REAPER MCU; 3rd-party scripts can write) · 12-seg track meter + clip ·
FaderdB readout · V-Pot readout bar.

**Large (4.3") LCD:** 4 soft-key labels · Timecode (Bars/Beats or SMPTE) ·
Channel-encoder mode indicator · Solo-active indicator · MCU bank-window number
· soft-key page number · Top Scribble (4× 6-char names for chans 1–4 or 5–8) ·
Low Scribble (blank under stock REAPER) · FaderdB · V-Pot readout bar · 4 V-Pots.

These zone names map almost 1:1 onto the UF8 LCD-zone commands we already
decoded (`FF 66 …` family) — same vocabulary (TrkNam, O/PdB, V-Pot bar,
Channel-Strip-Type, Value Line). **Strong prior** the UF1 reuses the same
opcodes with a single-strip address.

### Plug-in Mixer mode (p.182, 187) — the proprietary layer

**Small LCD:** Bypass (top) · Plug-in-Mixer position · **Channel-Strip Type**
(CS 2 / 4K B / 4K E, or the 3rd-party name under 360 Link) · **DAW Colour**
(VST3 track colour) · TrkNam · O/PdB · I/O metering · **Dynamics (GR) metering**
· Pan label + V-Pot readout bar.

**Large LCD (Channel-Strip mode):** 4 soft-key param labels · timecode · DAW
host layer · solo-active · **Channel-Strip EQ curve graph** · param page · fine-
mode indicator · 4 V-Pot param names+values. (**Meter mode** drives SSL Meter —
Overview / Analogue VU-PPM / 31-band RTA.)

Every one of these (Colour, CS-Type, O/PdB, V-Pot bar, GR, VU) is a zone we
already crack on the UF8/UC1. The **EQ-curve graph** and the **SSL Meter
screens** are genuinely new graphical surfaces — heavier than anything on the
UF8 (which has no full graphical TFT). Decoding those is likely framebuffer-ish
and **out of scope for a first useful build**.

## REAPER specifics (p.134, 143–144)

- **Setup = plain MCU.** REAPER Prefs → Control/OSC/web → add **Mackie Control
  Universal**, MIDI in/out = **SSL V-MIDI Port 1**. No SSL-specific driver beyond
  360°. If a UF8 is already set up, UF1 needs *no extra* MCU device.
- **Stock REAPER MCU is shallow:** only fader, solo, mute, **Pan** (the only
  working V-Pot mode), transport, automation. Track/EQ/Send/Plugin/Instrument
  V-Pot modes are starred "no function in REAPER's default MCU implementation"
  — they need 3rd-party (CSI, ReLearn, DrivenByMoss, klinke). **This is exactly
  the gap Rea-Sixty fills natively for the UF8**, so the UF1 motivation is the
  same: deeper, colour-aware control without CSI.
- **Secondary-transport defaults (REAPER):** Unassigned, Unassigned, Cycle,
  Click, Quick-Key 1 = Solo-Clear, Quick-Key 2 = MCU Shift. SHIFTed = automation
  (OFF disabled in REAPER).

### Plug-in Mixer DAW-control params for REAPER (VST3, p.183)

REAPER **is** a compatible VST3 host. Via the native layer SSL exposes:
Track Volume, **Pan**, **Synchronised Track Colour**, **Solo**, Solo-Clear,
**Mute**, **Selected Track**. (Send levels 1-8 / Sends-on-off are **UF8-only**,
not UF1.) **SEL-key colour-follow** is the "PLUG-IN MIXER UF8/UF1 SEL KEYS
(VST3)" toggle (p.23) — the *same feature Rea-Sixty already implements* in
`ColorSync.cpp`, almost certainly the same `FF 38/39 04` SEL-palette bytes
decoded for the UF8 (cap33).

## What this means for Rea-Sixty

**Re-decode-by-analogy (~70%)** — fader (`FF 1E/1D/20/21`), SOLO/CUT/SEL LEDs
and SEL colour-follow (`FF 38/39 04`), brightness (`FF 2D 08` / `FF 4F 02`),
soft-key RGB (12-colour palette, `FF 38/39` tri-state), LCD text zones
(`FF 66 …` TrkNam / O/PdB / CS-Type / Value Line / V-Pot bar), button events
(`FF 22 03 <id> 00 <state>`), VU/GR. Single-strip ⇒ most "strip N" addressing
collapses to N=0.

**Genuinely new — needs fresh capture/decode:**
- **Jog/shuttle wheel** + **SCRUB** state (no UF8/UC1 analog).
- **Notched channel encoder** with 5 modes + push-menu.
- **Foot-switch** events (FS1/FS2).
- **Cursor + circle key** cluster, **FLIP/MASTER/MODE/5-8/360°** globals — new
  button-ID map.
- **4.3" graphical content:** EQ-curve graph + the entire **SSL Meter** UI
  (Overview/Analogue/RTA). Bitmap-ish, heavy — defer; not needed for a
  colour + name + fader + LED build.

**Scope reality check:** the UF8 headline was *track colour on scribble
strips*. The UF1's equivalent quick win is **SEL-key colour-follow + the small
LCD showing the focused track name/meter + the fader/transport** — all of which
sit in the already-decoded opcode families. The big graphical TFT (Meter/EQ) is
where new, expensive decoding lives and is the natural cut line for v1.

See [`uf1-decoding-research.md`](uf1-decoding-research.md) for the USB-identity
hypothesis (VID `0x31E9`, PID `0x0024`?, **confirm on enumeration**) and the
prioritised capture plan.

## Differences from UF8/UC1 that matter for decode

| Aspect | UF8 | UC1 | **UF1** |
| --- | --- | --- | --- |
| Faders | 8 | 0 | **1** (touch, 100 mm) |
| Graphical TFT | 8× small scribble TFT | **none** (3-digit 7-seg + small text LCDs + LED rings/meters) | **4.3" main + 1.77" channel** |
| Jog wheel | no | no | **yes** (+ scrub) |
| Foot-switches | no | no | **2** |
| Built-in USB hub (THRU) | yes | — | **yes** |
| V-MIDI per layer | port-group | — | **single port (1/5/9)** |
| SSL Meter plug-in surface | no | no | **yes** (new) |
| EQ-curve graph **on hardware** | no | **no** (curve only in the on-screen 360° mixer, not on the UC1 itself) | **yes** (drawn on the 4.3") |

## Sources

- `docs/docs/SSL UF1 User Guide_Rev4.0.pdf` — pp. 6, 10, 16–19, 22–28, 134–144, 178–192 (this distillation)
- Internal: [`uf1-decoding-research.md`](uf1-decoding-research.md), [`uf8-manual-reference.md`](uf8-manual-reference.md), [`protocol-notes.md`](protocol-notes.md), [`protocol-notes-uc1.md`](protocol-notes-uc1.md)
</content>
