---
title: Rea-Sixty User Manual
subtitle: Native REAPER ↔ SSL UF8 / UC1 driver
author: |
  Frank Acklin
  \
  [www.stoersender-studio.ch](https://www.stoersender-studio.ch)
date: v0.3.2
documentclass: article
geometry: margin=2.5cm
fontsize: 11pt
linestretch: 1.15
toc: true
toc-depth: 3
numbersections: true
colorlinks: true
---

\newpage

# Introduction

## What Rea-Sixty is

Rea-Sixty is a REAPER extension that drives the SSL UF8 and UC1 control surfaces directly from REAPER. It replaces SSL 360° on the host side. One extension file installs into REAPER's `UserPlugins` directory; SSL 360° no longer needs to run, the surface is no longer behind a virtual MIDI port, and the per-track SSL plug-in that SSL 360° requires for track colours is no longer needed.

What goes out on the USB wire is the same byte protocol SSL 360° uses, re-emitted by REAPER. No SSL binaries, firmware, or trademarks are redistributed.

## What you need

- REAPER on macOS (Apple Silicon or Intel), Windows (x64), or Linux (x86_64). Tested against REAPER 6 and 7 through 7.75.
- An SSL UF8 plugged in over USB-C. UC1 is supported optionally; UF8-only or UC1-only rigs are fine.
- **ReaImGui** (install via ReaPack from Extensions → ReaPack → Browse packages → ReaImGui). Without ReaImGui the Settings window stays empty, but hardware control still works.
- **SSL 360° must not be running.** It claims the UF8/UC1 vendor interface exclusively. If it is running when REAPER starts, the surface will not appear and REAPER's Console shows an error.

Runtime dependencies (`libusb`, `hidapi`) ship inside the platform archives; no separate install needed.

## Versioning

This manual documents Rea-Sixty v0.3.2. Earlier manuals (anything dated before 2026-07-14) are superseded.

Each release also carries a codename, shown on the **About** tab below the version. The codename has no functional role — just makes the release easier to refer to in conversation.

\newpage

# Installation

## Via ReaPack (recommended)

In REAPER:

1. **Extensions → ReaPack → Manage repositories → Import/export → Import repositories**
2. Paste: `https://github.com/acklin83/reaper-scripts/raw/main/index.xml`
3. **Browse packages →** filter `Rea-Sixty` **→ Install**
4. Restart REAPER
5. **Preferences → Control/OSC/Web → Add → Rea-Sixty**

First-run setup buttons live in **Settings → About**:

- **Windows:** *Install UF8/UC1 WinUSB driver* (one UAC prompt)
- **Linux:** *Install Linux udev rule* (one pkexec prompt)
- **macOS:** no setup needed; IOKit already grants libusb access to the device class

## Manual install

Download from <https://github.com/acklin83/Rea-Sixty/releases>:

- **Mac:** `rea-sixty-mac-v0.3.2.zip` — three Apple-notarised dylibs. Unzip into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.3.2.zip` — three DLLs. Unzip into `%APPDATA%\REAPER\UserPlugins\`.
- **Linux:** `rea-sixty-linux-v0.4.4.tar.gz` — `reaper_rea-sixty.so` plus the two bundled libraries (`libusb-1.0.so.0`, `libhidapi-hidraw.so.0`), the udev rule, and INSTALL.txt. Unpack **all three** files together into `~/.config/REAPER/UserPlugins/` — the plugin loads the libraries from its own folder, so no `apt install` is needed. Then apply the udev rule (or use the *Install Linux udev rule* button in Settings → About).

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to each release separately — it is not part of the ReaPack package. Download it and double-click it; the Stream Deck app installs it.

## Enabling the surface

After install, restart REAPER, then:

**Preferences → Control/OSC/Web → Add → Rea-Sixty**

No MIDI device assignment needed — the extension claims the UF8/UC1 over USB on its own.

## Uninstall

- **Via ReaPack:** Browse packages → right-click Rea-Sixty → Uninstall.
- **Manual:** delete `reaper_rea-sixty.{dylib,dll,so}` plus the two runtime libraries (`libusb`, `libhidapi`/`hidapi`) from REAPER's UserPlugins directory. Remove the Control Surface entry under Preferences → Control/OSC/Web.

\newpage

# Concepts

A handful of terms appear throughout the manual.

## Focused track

The track REAPER considers "selected first" — `GetSelectedTrack(nullptr, 0)`. UC1 follows it by default; the SSL Channel Strip / Bus Compressor section reads the focused track's plug-in.

## FX vs Instance

- **FX** = any audio effect on a REAPER track.
- **Instance** = the surface-mapped subset only:
  - SSL Channel Strip 2 + 4K B/E/G variants
  - SSL Bus Compressor 2
  - SSL 360° Link
  - Combo plug-ins of the above
  - User-mapped UF8-only plug-ins with `uf8Mode` set in their FX Learn catalog entry

Every Instance is an FX. Most FX are not Instances.

The distinction matters for two pairs of cycle actions:

- **FX Cycle** walks every FX on the track.
- **Instance Cycle** walks only Instances on the track.

Six action names follow the same rule (per-surface × per-scope × per-cycle-kind). See the Cycle actions section under Native actions.

## FX-Cursor

A per-track pointer at the last FX a cycle landed on (the index the next cycle step proceeds from). Persistent across mode changes. When a cycle lands on a learned Instance, the cursor also updates the per-domain Instance index so SSL Strip Mode / UF8 Plug-in Mode follow.

## Plug-in identity

When you rename an FX instance in REAPER's FX chain ("Townhouse Comp" instead of the factory name), Rea-Sixty uses the rename everywhere the colour-bar would otherwise show the factory short name. Internally the rename is resolved by FX-GUID, so reordering or chunk-replacing the FX preserves the binding to the identity.

## Domain

The Instance domain (`ChannelStrip`, `BusComp`, or `None` for UF8-only user maps). Drives which UC1 section refreshes when the Instance cursor moves.

\newpage

# Selection Modes

Each strip's `SEL` button is hijacked by the active Selection Mode. Toggle modes via the dedicated buttons on the UF8 (mapped through Bindings) or via the per-mode builtin actions.

## NORM

The default — `SEL` toggles REAPER track selection. V-Pots default to Pan. No automatic colour-bar overrides on the upper LCD.

## REC

Each strip's `SEL` toggles arm. V-Pots stay on Pan unless `REC + RME` integration is on (next mode).

## REC + MON

Each strip's `SEL` toggles arm + monitor. Otherwise like REC.

### REC + RME (TotalReaper integration)

A sub-mode of REC / REC+MON. When **Settings → Modes → REC → "Enable RME / TotalReaper integration"** is on and TotalReaper is installed:

- V-Pot rotation = preamp gain ±1 dB (per the *V-Pot rotation → Preamp gain* toggle)
- V-Pot push / Cut / Solo / Polarity = configurable TotalReaper actions (48V toggle, pad toggle, phase invert, AutoLevel toggle).
- Shift+V-Pot rotation = change input channel
- The strip's colour bar shows the input name ("Mic 1", "Line 3") instead of the CS variant label

## AUTO

The strip's `SEL` cycles through automation modes (Off → Read → Touch → Latch → Write → Trim, then back). V-Pots become read-only automation-mode indicators.

When **Settings → Modes → AUTO → "Hide Trim/Read tracks while in AUTO mode"** is on, tracks set to Trim or Read disappear from the surface so only writing-armed tracks remain visible.

Leaving AUTO restores the previously-visible track list and refocuses the surface on the selected track.

If a Selection Set is active and **Settings → Modes → AUTO → "Selection-Set auto-mode"** is set to a value other than `None`, recalling a selset in AUTO mode auto-arms the set's tracks to that automation mode. Leaving AUTO reverts those tracks to Trim/Read.

## FX Cycle (V-Pot Sel-Mode)

Each strip's V-Pot rotation walks every FX on the strip's track. Push opens the active FX's floating window. The strip's colour bar shows the FX-cursor's current FX name.

## Instance Cycle (V-Pot Sel-Mode)

Each strip's V-Pot rotation walks only Instances (CS / BC / UF8-Mode-learned) on the strip's track. Push opens the active Instance's floating GUI. No-op when the strip's track has fewer than 1 Instance.

When the cycle lands on a learned Instance the per-domain Instance index updates so SSL Strip Mode + UF8 Plug-in Mode + the UC1 sections follow.

\newpage

# Channel Encoder modes

The large notched CHANNEL encoder (right of the strips, pushable, surrounded by the cursor pad) runs one of twelve modes. Switch with the corresponding **Encoder Mode → …** action. The current mode persists across REAPER restarts.

| Mode | Rotation acts on | Notes |
|---|---|---|
| Channel Select | Move REAPER track selection ± | Default mode. Strips re-bank to keep selection visible. |
| Nudge | Playhead nudge | Step size from REAPER's nudge setting |
| Mousewheel | Synthesised scroll-wheel under the mouse cursor | Use to scroll plug-in windows or Project Browser |
| Markers | Step prev / next marker | Stops playback while seeking |
| Bank by 1ch | Shift the surface 1 strip left/right | Sub-bank precision |
| Last Touched Param | Step the last-touched REAPER param ± | Fine increments |
| Instance (Instance Cycle) | Walk Instances on the focused track | Same behaviour as the V-Pot Sel-Mode Instance Cycle, focused-track scope |
| FX Cycle | Walk every FX on the focused track | Focused-track scope |
| Cycle Instance (across tracks) | Walk Instances on the focused track, then cross to the next track | One detent per track boundary; lands on the neighbour's first (fwd) / last (back) Instance. Empty neighbour is still selected (dead detent). Hard-stops at the project edge, no within-track wrap. |
| Cycle FX (across tracks) | Walk every FX on the focused track, then cross to the next track | Same cross-track behaviour as above, for all FX. SSL 360°-native feel. |
| Selset Cycle | Step through populated Selection Set slots (off → 1 → 2 … → off) | Skips empty slots |

`Shift` + rotation re-banks ±1 strip in every mode (alias for Bank by 1ch).

The encoder also drives **SEL-Mode cycle** when **Settings → Modes → FX / Cycle → "UF8 Channel Encoder"** is ticked AND a cycle-kind Selection Mode is engaged. In that override the encoder steps the active Selection-Mode cycle instead of its normal mode.

\newpage

# Nav Mode (Markers + Regions)

Nav Mode is a surface overlay — separate from the Selection Modes — that turns the UF8 strips into a live marker / region jump panel. It can be active alongside any Selection Mode; toggling it does not change the active Selection Mode.

## Engaging Nav Mode

Three bindable builtins:

- **Nav Mode (Markers & Regions): toggle** — toggle on / off, opens the default view (set in Settings → Modes → NAV).
- **Nav Mode: Markers only (no drill)** — toggle on / off, locks the view to **MarkersAll** (no drill into regions).
- **Nav Mode: Regions only (no drill)** — toggle on / off, locks the view to **Regions** (region presses jump only, no drill).

Bind any of these to a UF8 / UC1 button in Settings → Bindings. The same button toggles Nav Mode off again.

## Three views

- **Regions** — each strip is one REAPER region. Top-soft-key press jumps the transport to that region's start (and, by default, drills into its markers).
- **MarkersInRegion** — markers inside the region the playhead is in. Auto-rolls into the next region when the playhead crosses out.
- **MarkersAll** — flat list of every marker in the project.

The default view on Nav-Mode entry is configurable (Settings → Modes → NAV → *Default view*): Regions / MarkersInRegion / MarkersAll / Last used.

## What the UF8 strips show

Per strip while Nav Mode is active:

- **Top-soft-key** — press = jump (and, in Regions view, optionally drill). LED colour = the region / marker's REAPER colour (or grey if *Color-bar source: Force palette grey* is set).
- **Scribble strip upper row** — the marker / region name.
- **Scribble strip lower row** — configurable: Off (V-Pot value preserved) / Index (`R03`, `M07`) / Timecode (`MM:SS`).
- **Colour bar** — the marker / region's colour (or palette grey, per setting).

## Paging

When the project has more items than 8 strips, the overlay pages 8 at a time. Pagination via:

- **CHANNEL encoder rotation** while Nav Mode is active — pages forward / backward.

## UC1 Encoder 2 takeover

Off by default. When Settings → Modes → NAV → *Take over UC1 Encoder 2* is on:

- **Rotation** moves the cursor through items (sets a cursor pin that suppresses auto-follow until the playhead catches up or you push).
- **Plain push** — Jump + Drill / Jump only / Drill only (configurable).
- **Shift + push** — Drill / Back / Toggle View (configurable).
- **Long-press** (~500 ms) — Back / Add marker at playhead / Disabled (configurable).
- The UC1 central LCD switches to a marker carousel showing prev / curr / next items.

While a view-lock toggle (Markers-only / Regions-only) is engaged, shift and long-press are suppressed — only plain push fires.

## Auto-Follow

Settings → Modes → NAV → *Auto-Follow playhead / edit cursor* (checkbox).

When on, the cursor strip tracks whichever marker / region the playhead is on. In MarkersInRegion view, the overlay auto-rolls into the next region when the playhead crosses out (only after the playhead was first observed inside the current filter region — suppresses the snap-back when you drill manually during playback).

Manual cursor movement (UC1 Encoder 2 rotation) pins the cursor and pauses auto-follow until the playhead catches up, or you commit (push), exit Nav Mode, drill, or change view.

## Region-press behaviour

Settings → Modes → NAV → *Region-press behaviour*:

- **Jump + Drill** (default) — press a region's top-soft-key → transport jumps to region start AND the overlay drills into that region's markers.
- **Jump only** — transport jumps; overlay stays on the Regions view.
- **Drill only** — overlay drills; transport stays put.

RegionsOnly view-lock always suppresses Drill regardless of this setting.

\newpage

# UF8 hardware

The UF8 has **no transport keys, no Layer LED on Layer 3 on some units, no jog wheel**. The layout below mirrors SSL's published reference (User Guide Rev 11, p.14-17).

## Strips (×8)

Per strip, from top to bottom:

| Control | Function (NORM Selection Mode) | Notes |
|---|---|---|
| Top soft-key | SSL Soft-Key for this strip in the current PAGE bank | Default action **SSL Soft-Key (current bank, slot 0..7)**. Rebindable. |
| Colour TFT (scribble strip) | Upper zone = track name / mode-dependent. Lower zone = parameter readout. Track-colour bar at the bottom. | Hijacked by Plug-in Modes for parameter / FX names. |
| V-Pot rotation | Pan | Re-maps per Selection Mode (REC + RME → preamp gain; AUTO → automation indicator; FX Cycle / Instance Cycle → walk FX/Instances). In Pan, turning records track-pan automation in Touch mode and holds on release. |
| V-Pot push | Centre Pan | In FX Cycle / Instance Cycle Sel-Modes → open the active FX's GUI. |
| `SOLO` | Solo | LED colour follows REAPER track colour when *Settings → Appearance → Surface display → "SEL LED follows REAPER track colour"* is on. |
| `CUT` | Mute |  |
| `SEL` | Selection-Mode dependent | NORM = exclusive select; REC = arm; REC+MON = arm + monitor; AUTO = cycle automation mode. Long-press on a folder parent toggles spill. |
| Capacitive touch (fader) | Drives REAPER's "touch" automation | Alt/Option held during touch → snap back on release (Settings → Behaviour → Keyboard). |
| 100 mm motorised fader | Track volume | Full 16-bit pitch-bend protocol. |

## Above the strips: 8 Top Soft-Keys + bank selectors

A row of 8 buttons above the V-Pots = **Top Soft-Keys 1..8**. By default each one focuses the SSL plug-in parameter at its strip position within the current PAGE bank (SSL 360° factory behaviour). Rebindable per-button.

Left of the soft-keys: 6 small bank-selector buttons — **V-POT** + **1 / 2 / 3 / 4 / 5**. Each picks one of the 6 SSL CS soft-key banks (or 2 BC banks while in Bus-Comp). Default action **Select soft-key bank (param 0..5)**.

## Right of the strips: CHANNEL encoder + cursor pad

A single block:

- **Large notched CHANNEL encoder** (push-button rotary). Rotation drives the active Channel Encoder mode (see chapter Channel Encoder modes). Push = mode-specific.
- **Cursor pad** — 5 buttons (4 arrows + central circle) **surrounding the encoder**.
  - Default behaviour: **zoom** via the **Zoom in vertically** / **Zoom out vertically** / **Zoom out horizontally** / **Zoom in horizontally** / **Zoom to fit project** actions (REAPER actions 40111 / 40112 / 1011 / 1012 / 40295).
  - SSL's reference UG also documents a "Cursor-Transport" mode (press-and-hold CHANNEL encoder to enter; ↓=Stop ↑=Play ←=Rew →=FF centre=Rec). Rea-Sixty leaves these as the standard zoom bindings — rebind them to transport actions via Settings → Bindings if you want SSL's behaviour.
- **NAV / NUDGE / FOCUS** mode buttons (around the encoder; ButtonId entries `Nav`, `Nudge`, `EncFocus`). Default builtins switch the encoder to that mode.

Above the encoder block: **Q1 / Q2 / Q3** ("Quick" user keys). Default bindings — Q1 = CS, Q2 = BC, Q3 = I/O meter (matches SSL's locked Plug-in Mixer assignment).

## Right of the encoder column: NORM / REC / AUTO

Three buttons (Selection Mode block). Default unbound — assign **Selection Mode → NORM (SEL Button)** / **Selection Mode → REC (SEL Button)** / **Selection Mode → AUTO (V-Pot)** (or any other Selection Mode action, including **Selection Mode → FX Cycle (V-Pot)** / **Selection Mode → Instance Cycle (V-Pot)**) via Settings → Bindings.

## Above NORM/REC/AUTO: AUTOMATION row

Six buttons — **Read / Write / Touch / Latch / Trim / Off**. Default actions **Automation: Read**, **Automation: Write**, **Automation: Touch**, **Automation: Latch**, **Automation: Trim**, **Automation: Off / Trim** (set the automation mode of the focused track).

## Below the strips: PLUGIN / CHANNEL / mode row

| Button | Default |
|---|---|
| `PLUGIN` | Toggles SSL Strip Mode (**Toggle SSL Strip Mode**). With Shift held: **Toggle SSL Strip Mode (with GUI)**. |
| `CHANNEL` | **Home (clear routing toggles)** — clear send / receive routing toggles so V-Pots / faders return to track volume + pan. |
| `BANK ←` / `BANK →` | Scroll ±8 strips. In UF8 Plug-in Mode → flip between fader-banks A / B (for 16-strip plug-ins). In a **"of focused track"** routing view → page the send / receive list by 8 (see *Send and Receive views*). |
| `PAGE ←` / `PAGE →` | Step the SSL Soft-Key PAGE bank prev / next (6 CS banks + 2 BC banks). |
| `SEND / PLUGIN 1..8` | 8 buttons. Default **8 sends of focused track** / **8 receives of focused track** (param N) — toggle the matching send / receive view. |

## FLIP / PAN / FINE

Three buttons in their own cluster:

| Key | Default action |
|---|---|
| `FLIP` | **Toggle FLIP (fader ↔ V-Pot)** — swap fader and V-Pot values for the active mode. |
| `PAN` | **Toggle V-Pots → Pan** — force V-Pots to Pan regardless of the active Selection Mode. Escape hatch from cycle / REC / AUTO modes. |
| `FINE` | **Modifier: Shift / Fine (double-click latches)** (the SSL "FINE" = "Shift" key). Double-click latches it on; press again to unlatch. With *Settings → Devices → V-Pot / encoder feel → Shift activates Fine mode* on, holding this also drops V-Pot / encoder step size to ×0.25 (faders unaffected). |

## Layer keys

`LAYER 1 / 2 / 3` — three SSL DAW layers. Bindable. Layer 3 LED on certain UF8 units does not light up — confirmed hardware quirk, not a Rea-Sixty bug. Layer functionality itself works.

## 360° key

Default action **Open / Close Rea-Sixty Settings** — opens / closes the Rea-Sixty Settings window.

## Foot-switch jacks

`FS1` / `FS2` on the back (1/4" TS, normally-closed momentary). No factory bindings — assign via Settings → Bindings (ButtonId `Foot1` / `Foot2`).

## What the UF8 does NOT have

For clarity (Rev 11 reference, p.14-17):

- No dedicated `Play / Stop / Record / Loop` keys. Transport-on-cursor-pad is a press-and-hold-CHANNEL feature; Rea-Sixty leaves the cursor pad on zoom by default.
- No jog wheel. The CHANNEL encoder is the only large rotary.
- No master fader.
- No HUI/MCU display lane apart from the 8 strip scribble LCDs.

\newpage

# UC1 hardware

The UC1 mirrors the SSL Channel Strip 2 + Bus Compressor 2 controls on a dedicated unit, plus a central control panel for navigation. When UC1 is plugged in alongside UF8, it auto-engages on the focused REAPER track and follows the focused Instance.

The UC1 has no hardware mode-switch — the Channel Strip and Bus Compressor sections are always live, each driving whichever CS or BC Instance is currently in focus on the focused track.

## Channel Strip section — left side (EQ + Filters, 12 knobs)

Top to bottom on the left half:

- **LPF / HPF** — two filter knobs (low-pass + high-pass frequency).
- **HF** — Gain + Freq (2 knobs).
- **HMF** — Gain + Freq + Q (3 knobs).
- **LMF** — Gain + Freq + Q (3 knobs).
- **LF** — Gain + Freq (2 knobs).

Plus the EQ-section buttons: **HF Bell** (HF shape), **EQ Type** (E vs G EQ curve), **EQ In** (EQ section bypass), **LF Bell** (LF shape).

## Channel Strip section — right side (Dynamics + Channel, 7 knobs + 7 buttons)

- **Compressor:** Threshold + Ratio + Release (3 knobs).
- **Gate / Expander:** Threshold + Range + Hold + Release (4 knobs).

Buttons:

- **Fast Att Comp** — fast attack on the compressor.
- **Peak** — peak detection.
- **Dyn In** — Dynamics section bypass.
- **Expand** — switch the gate into expander mode.
- **Fast Att Gate** — fast attack on the gate.

Channel section (lowest row of buttons):

- **Polarity** (phase invert)
- **SC Listen** (side-chain monitor)
- **Solo Clear**
- **Solo** / **Cut** (track operations — routed to REAPER's track ops)
- **Channel In** (channel input enable)
- **Fine** (FINE / Shift modifier on UC1)

## Channel Strip top row (2 knobs)

- **Input Trim** (CS input gain)
- **Channel Fader Level** / **Output Gain** (CS fader stage)

When SSL Strip Mode is engaged on the UF8, the **Channel Fader Level** parameter is what the UF8 motorised faders drive.

The Output Gain knob can be flipped to drive **REAPER's track volume fader** instead of the CS Fader Level parameter — see *Native actions → Surface-state toggles → UC1 Out-Gain to REAPER fader*. Handy when the track has no SSL channel strip but you still want a hardware volume knob; the LED ring and readout follow the track fader while engaged. When the UC1 is focused on the **Master** track and there is no Channel Strip on it, the knob drives the Master fader automatically — no toggle needed (see *Master track*).

## Bus Compressor section (7 knobs + 1 button)

- **Threshold** / **Ratio** / **Attack** / **Release** / **Make-Up** / **Mix** / **SC HPF** — 7 knobs across the top centre.
- **Bus Comp In** — single button enabling the BC section.

The BC controls drive the BC Instance on the **BC anchor track** — the track UC1 has currently pinned for Bus-Comp display. Encoder 2 (the *Secondary encoder* right of the central screen) scrolls the anchor between BC-bearing tracks.

The mechanical BC VU meter is driven from REAPER via the PreSonus standard `GainReduction_dB` host-side hook. Rest position = bottom of scale; the needle swings up through GR magnitude.

## Central control panel (between CS and BC)

A column of buttons + the central LCD + the two encoders:

- **Back** / **Confirm** — navigate the on-screen menus (Routing / Presets / etc.). From the main screen, **Back** opens the **EXT FUNCS** menu — a hidden list of channel-strip parameters that don't have a dedicated knob. Scroll it with the secondary encoder; push the encoder to switch from scrolling to adjusting the selected parameter. For SSL channel-strip plug-ins the list is the fixed SSL set; for user-mapped (non-SSL) channel strips it's whatever you curate (see *FX Learn pane → UC1 EXT FUNCS list*).
- **Routing** — opens the Routing menu on the LCD.
- **Presets** — opens the Presets menu.
- **360°** — default **Open / Close Rea-Sixty Settings** (bindable on its own UC1 entry so it can diverge from the UF8 360° key).
- **Magnifier** — no factory action; bindable.

## CHANNEL encoder (left of the central LCD)

The large rotary on the central control panel. ButtonId `Uc1Encoder1` in the bindings system.

- **Rotation** — **Encoder: scroll tracks** by default (step REAPER track selection ±, with UC1 focused-track and CS-domain focus following along). Rebindable in Settings → Bindings → UC1 (ROTATE tile under ENCODER 1) — Shift = **Encoder: cycle plug-in instance** by default; Cmd / Ctrl free.
- **Push** — push event arrives as button 0x0D; default binding empty.
- When **Settings → Modes → FX / Cycle → "UC1 Encoder 1 (CHANNEL)"** is ticked AND a cycle-kind Selection Mode is engaged, rotation steps the active Selection-Mode cycle instead, regardless of the binding.

## Secondary encoder (right of the central LCD)

ButtonId `Uc1Encoder2` in the bindings system; SSL calls it the "Secondary" or "BC" encoder.

- **Rotation** — **Encoder: scroll BC anchor track** by default (scroll the BC anchor between BC-bearing tracks). Rebindable, including to **Encoder: cycle plug-in instance** or **Encoder: cycle FX (all on focused track)**.
- **Push** — **Plug-in: toggle focused GUI** by default (toggle floating window of the cursor instance from the most recent cycle). Rebindable.
- Cycle-Control mask includes "UC1 Encoder 2 (BC)" — same SEL-Mode override mechanism as above when ticked + a cycle-kind Selection Mode active.

## Central LCD zones

Three addressable zones:

- **Channel-Strip readout (zone 0x03)** — last-touched CS parameter name + value, e.g. "HF Gain  +3.0 dB". Fades after a few seconds.
- **Bus-Comp readout (zone 0x05)** — last-touched BC parameter name + value.
- **Central main (zone 0x0F)** — multi-overlay area. Shows: track-name header + focused Instance variant (default), or the prev / curr / next Instance-carousel triple (after an Instance / FX Cycle just fired), or the BC compressor mode / status, or the Nav-Mode markers/regions carousel. Overlays are mutually exclusive; the most recent claim wins.

## Brightness

Set independently per channel (UC1 LEDs / UC1 LCDs / UF8 LEDs / UF8 LCDs) under Settings → Devices → Brightness. Six **Brightness …** actions (LEDs +/-, LCDs +/-, Both +/-) drive these from a hardware button.

## UC1 GR Calibration

If the UC1's mechanical VU meter or the CS Dynamics GR LEDs drift from their printed scale, a per-tick offset table at the very bottom of **Settings → Devices** corrects each printed dB tick individually. The workflow mirrors SSL 360°'s own BC VU calibration tool — click `Test` next to a tick, then `+` / `-` until the UC1 lines up with the printed marking. Auto-saved per-tick. `Stop test` resumes normal GR.

\newpage

# UF1 hardware

One channel, a colour screen and a jog wheel. The UF1 is not a small UF8: its
screen is host-driven, so what the keys do depends on which **view** is up, and
almost every key is rebindable in *Settings → Bindings → UF1*.

Two keys are deliberately **not** rebindable: `MODE` and `SCRUB`. Both are held
modifiers that open a picker, so a binding on them would have nothing to fire.
`SOLO` and `CUT` are also fixed — they act on the focused track through REAPER's
own solo/mute, and Rea-Sixty does not reinvent those. `SEL` is a hybrid: a single
press always selects the focused track exclusively (hold **Shift** to extend the
selection instead), and the key *also* runs through the binding system, so a
double-press or a modifier gesture can fire an action on top.

## Views

`MODE` is a **hold**: keep it down and the four display soft-keys become a view
picker. Press one while holding to switch, release to close.

| Soft-key while `MODE` is held | View |
|---|---|
| SK1 | **Plugin** — the channel strip: EQ graph, section soft-keys, four V-Pots |
| SK2 | **DAW** — the same channel layout, but the four soft-keys fire your own soft-key bank |
| SK3 | **Meter** — the metering screens |
| SK4 | **Sends** — the channel layout showing the send group |

While `MODE` is held the **`CHANNEL` encoder** does a second job: it steps the
**Encoder Mode** through the ring you configured, and SK4's label shows the live
mode name as you turn. So one held key gives you both pickers — soft-keys for the
view, encoder for the encoder mode. Which modes appear in the ring, and in what
order, is set in *Settings → Bindings → UF1*.

The view survives until you change it. Everything below is described per view,
because the same physical key does different work in each.

## The channel (all views)

| Control | Function |
|---|---|
| Fader | Volume of the UF1's channel. Touch-sensitive, drives touch automation. |
| V-Pot above the fader | Pan by default, or whatever the Sticky Pot is pinned to. **Push** clears an armed pin, resets a live pin, or — with no pin — centres Pan. |
| `SOLO` / `CUT` | Solo / mute the focused track. Fixed, not rebindable. |
| `SEL` | Select the focused track exclusively; **Shift +** `SEL` extends the selection. Double-press opens the FX chain by default. |
| `FLIP` | Swap the fader and V-Pot assignments. Factory default, rebindable. |
| `MASTER` | Put the Master bus on the channel. Factory default, rebindable. |
| `CHANNEL` encoder | Step the UF1's channel through the track list. **Shift +** turn is always Instance Cycle, whatever mode is selected. **Push** opens the focused plug-in's GUI. Both are factory defaults and rebindable. |

## Plugin view

The channel strip of the plug-in on the focused track. When the plug-in is an SSL
channel strip — or a third-party plug-in you have taught through *FX Learn* — the
EQ curve is drawn on the screen and the four V-Pots carry that page's parameters.

| Control | Function |
|---|---|
| SK1-SK4 | The channel-strip section toggles for the current page (EQ In, HF Type, …). A blank slot does nothing. |
| V1-V4 | The current page's four parameters. |
| `◄` `►` (page arrows) | Step the strip page. |
| `5-8` | Jump to V-Pots 5-8 of the page. |
| `BANK ◄` `►` | Step one parameter left / right. |
| Quick-key `2` (bottom row) | Toggle **Fine** resolution for the channel V-Pots. |
| V-Pot push (V1-V4) | Reset that parameter to its default. |

## Meter view

| Control | Function |
|---|---|
| SK1 | **Meter Screen Selector** — cycles Overview → Analogue → RTA. A Meter **Pro** adds Loudness as a fourth screen; a plain Meter never streams that data, so it is not offered. |
| SK2 | **Reset** — clears the peak hold. |
| SK3 | **Fine** — finer V-Pot resolution. In the preset browser it is *Navigate Back* instead. Not on the Loudness screen, where SK3 is *Play*. |
| SK4 | **Presets** — open / close the preset browser for the pinned Meter instance. |
| V4 | In the preset browser, scroll to highlight. **Push V4** loads the highlighted preset — browsing never loads on its own. |
| `◄` `►` | Page the meter V-Pots. |
| Quick-key `2` | Toggle Fine, same as SK3. |

## DAW view

Identical to Plugin view except the four display soft-keys fire the current
**UF1 soft-key bank** instead of the channel-strip sections. Banks are built in
*Settings → Bindings → UF1*; a bank slot can also be a **dynamic** bank, in which
case the key behaves like a UF8 FX key — plain press, **+Shift**, **+Cmd**,
**+Ctrl** and long-press each do something different.

Soft-key labels are capped at **13 characters** and V-Pot labels at **11**; both
are abbreviated rather than truncated, so every word stays readable.

## Secondary transport row

`OFF` `READ` `WRT` `TRIM` `LTCH` `TCH` and `SHIFT` along the bottom, with the
primary transport (`◄◄` `►►` `■` `►` `●`) below. `SHIFT` is the UF1's Shift
modifier. The primary transport ships bound to REAPER's own transport actions and
can be rebound; the secondary row ships **unbound** and is yours.

## Nav cross and jog wheel

The five-key cross (up / left / centre / right / down) and the jog wheel belong to
**Jog Mode** — its own chapter, next.

\newpage

# Jog Mode

The jog wheel does not have one job. It has an **object**, and the object decides
what turning the wheel means. `SCRUB` is a hold: keep it down and turn the jog to
pick the object.

| Object | Jog turns |
|---|---|
| **Playhead** | Moves the play cursor. Step is 1 % of the visible view per count, so the feel follows your zoom. |
| **Scrub** | Audible scrub, 0.5 s per count. |
| **Items** | Moves the selected item(s). Default one quarter of a grid step per count. |
| **Envelope** | Moves the selected envelope points, same default step. |
| **Razor** | Moves the razor edit — which edge, see below. |

The arrow cross **selects**, the jog **moves**. That split is the whole idea: you
never have to let go of the wheel to change what you are working on.

Three modifiers stack on the jog:

- **Shift** — fine. Divides the step (default ÷4).
- **Cmd** — copy instead of move.
- **Ctrl** — cross axis. In Items, that means moving track by track rather than in
  time; the vertical axis is discrete, so it steps one track at a time.

## Razor

Razor mode aims at one of five targets: the whole razor area, or its left, right,
top or bottom edge. The **centre** key of the nav cross is a held gesture — press
and hold it and the whole razor area drags its content as one continuous move;
release commits. Holding it grabs the content once at the start, so the razor
cannot sweep up material it is dragging across.

Every speed here is adjustable — the picker speed, the fine divisor and the step
per object all live in *Settings → Bindings → UF1*.

\newpage

# Settings window

The Settings window is a dockable ReaImGui context. Open with the `360°` key (default), the **Open / Close Rea-Sixty Settings** action, or REAPER's Action `Rea-Sixty: Toggle Settings window`.

Twelve sidebar tabs: Devices · Appearance · Behaviour · Bindings · Modes · FX Learn · Favourites · Selection Sets · Parameter Groups · Exchange · Manual · About.

The first three hold the general settings, split by what they touch: **Devices** is the hardware itself, **Appearance** is what you look at, **Behaviour** is what the surface does.

## Search settings

A filter field sits at the top of the sidebar, above the tab list. Leave it empty and the sidebar is the normal tab list. Type into it and the tab list is replaced by matching settings, each row reading *Pane › Section › Setting*.

- Matching is **additive** and case-insensitive — the query is split on spaces and a setting has to contain **every** word, in any order. "notch hold" and "hold notch" both find *Notch hold*.
- Words are matched against the whole trail, not just the setting's name, so "devices meter" lists everything in Devices → Metering.
- Click a result to switch to its pane; the pane scrolls to the section that setting sits in.
- The sidebar is narrow, so long rows are shortened — the full trail when it fits, otherwise pane + setting, otherwise the setting alone. Hover a row to read the untruncated trail as a tooltip.
- Nothing matches → **No matches**.

Devices, Appearance and Behaviour are indexed setting by setting. The other nine tabs are indexed by name only — searching "bindings" jumps to the Bindings pane, but the individual controls inside it are not in the index. The query is not remembered between sessions.

## Devices pane

The hardware: what is plugged in, how bright it is, how its meters fall, how its pots feel.

### Connected devices

One line per device — **UF8**, **UC1**, **UF1** — reading `[connected]` or `[not connected]`, plus `SN` and the serial number when connected.

UF8 and UC1 each get an **Identify** button while connected: it flashes that unit's LEDs and displays between Dark and Full at 4 Hz for two seconds, so you can tell which box is which in a multi-unit rig. The UF1 has no Identify button.

### Brightness

Two 5-step sliders (Dark / Dim / Half / Bright / Full):

- **LEDs** — drives buttons + V-Pot rings + UC1 LEDs
- **LCDs** — drives the UF8 LCD strips + UC1 LCD + UC1 status displays

Set independently so you can crank the displays while keeping the LED ring dim, or vice versa. Six **Brightness …** actions (LEDs +/-, LCDs +/-, Both +/-) drive these from any button binding.

### Metering

Where the gain-reduction meters get their numbers:

| Control | Effect |
|---|---|
| GR meter source (combo) | *Only Show Channel Strip GR* — the GR meters are limited to SSL CS / mapped CS plug-ins. *Show any GR Data* (default) — falls back to any FX on the focused track exposing the PreSonus `GainReduction_dB` host extension (ReaComp, FabFilter, etc.). |
| Combine GR across plug-ins (UF8 strips) | With *Show any GR Data* selected, the UF8 CS-GR strip **sums** the gain reduction of every compressor on the channel instead of showing one source — in-series GR adds in dB, so the meter reads the channel's total reduction. No effect while *Only Show Channel Strip GR* is selected. Off by default. |
| Combine GR across plug-ins (UC1 Comp) | The same, for the UC1's Comp meter. Separate from the UF8 setting, so one surface can show the combined figure while the other shows the Channel Strip alone. Off by default. |

All level meters (UC1 Input + Output, UF8 strip bars) are **peak-hold**. The only adjustment is how fast each meter falls back after a peak, set per meter in **dB per second**:

| Control | Effect |
|---|---|
| UC1 Input | Fall rate of the UC1's input-level meter. |
| UC1 Output | Fall rate of the UC1's output-level meter. |
| UF8 Strips | Fall rate of the UF8 strip-bar level meters. |
| Copy Input to Output | Sets the UC1 Output fall rate to match UC1 Input. |

Default is **26.5 dB/s** — REAPER's own meter decay, so the UC1 input meter falls in lock-step with the output. Lower = slower decay (longer peak hold); higher = snappier. Entries are clamped to 1 – 200 dB/s.

### V-Pot / encoder feel

| Toggle | Effect |
|---|---|
| Shift activates Fine mode (V-Pots / encoders, not faders) | When on, holding the Shift modifier (keyboard Shift, UF8 `FINE` key, or UC1 `Fine` button) drops V-Pot + encoder step size by the configurable **Fine factor** (default ×0.25 — see below) for momentary fine resolution. Faders are deliberately excluded (they already have Alt-drag for fine control). Stacks with the UC1 `Fine` toggle. Off by default. |
| Fine mode steps JSFX sliders by their native increment | Some JSFX sliders are long enough that even Fine mode cannot resolve a single value cleanly. With this on, Fine on a continuous JSFX slider steps by the slider's own native increment — one detent = one step, a fast flick accelerates — which is the finest the plug-in supports. Continuous JSFX + Fine only; VST3 / AU and normal turns are unaffected. On by default. |

Below the two toggles, per-surface speed and 0 dB / centre-pan detent feel for **all** V-Pots and pots. Each field is a typeable value box (no slider). Faders are unaffected. Live — changes apply immediately, no reload.

| Field | Default | Effect |
|---|---|---|
| UF8 V-Pot speed | 1.00× | Linear multiplier on every UF8 V-Pot rotation delta. Higher = faster / coarser, lower = slower / finer. 1.00× = the historic feel. |
| UF8 Fine factor | 0.25× | How much the UF8 step shrinks while Fine is engaged (Shift / `FINE` / UC1 `Fine`). Lower = finer. |
| UC1 encoder speed | 1.00× | Same as UF8 V-Pot speed, for the UC1 knobs / encoders. Independent so the two surfaces can be tuned separately. |
| UC1 Fine factor | 0.25× | UC1 equivalent of the UF8 Fine factor. |

**Effective step** = base × per-control sensitivity (FX-Learn) × surface speed × (Fine factor while Fine held).

The speed minimum is **0.01×** (UF8 and UC1) — low enough for crawling through long sweeps a single detent at a time. Fine factors accept 0.05× – 0.50×.

The same section also tunes the **virtual notch** — the SSL-style magnet that lands bipolar V-Pot params (EQ gains, trims, fader level, pan) on their neutral point (0 dB / centre):

| Field | Default | Effect |
|---|---|---|
| Virtual notch zone | 2.0 % | Catch band half-width around the neutral point (% of the param's full range). Any inward move that lands inside snaps to centre; turning **away** from centre stays free. 0 % = no slow-approach snap (crossing the centre still snaps). Range 0 – 5 %. |
| Notch fine step | 0.50× | Step multiplier applied while the value sits within 2× the zone of centre — finer moves around 0 dB plus a more reliable catch. 1.00× = off. |
| Notch hold | 1.0 % | Soft-detent: once the value snaps to 0 dB it **parks** there and absorbs this much rotation before releasing — stops an endless encoder sailing past 0. 0 % = off (pure magnet, can overshoot). A mouse / automation move larger than the zone releases the hold. Range 0 – 10 %. |

Applies identically to UF8 V-Pots and UC1 knobs. Unipolar params (frequency, Q, threshold) have no neutral point and get no notch.

### UC1 GR calibration

Per-tick offset editor at the bottom of the pane, in two tables — **BC VU meter (0/4/8/12/16/20 dB)** for the mechanical VU, **CS DYN GR LEDs (3/6/10/14/20 dB)** for the Dynamics LEDs. Click `Test` on a row, then nudge that row's dB box until the UC1 lines up with its printed marking. Auto-saved per tick. `Reset all` clears one table, `Stop test` resumes normal GR. See chapter UC1 hardware → UC1 GR Calibration.

\newpage

## Appearance pane

### On-screen

The optional helpers Rea-Sixty draws over REAPER's own windows. Each is rendered by a companion script that auto-installs and auto-starts, and a status line under the toggle reports whether that companion is up yet. Full behaviour in chapter *On-screen display*.

| Toggle | Effect |
|---|---|
| Show MCP Inserts overlay | Highlights the active Channel Strip and Bus Comp plug-in on REAPER's Mixer Inserts list. |
| Show focused-track panel | The frameless floating panel showing the surface-focused track, its CS / BC plug-ins, and the last-touched parameter per domain. |
| Show mode-change banner | Flashes the new Selection- / Channel-Encoder Mode on screen for ~2 s, then hides. Off by default. |

The colour and geometry rows appear only while the helper that uses them is switched on:

| Control | Shown while | Effect |
|---|---|---|
| CS colour | Overlay or panel on | Colour of the active Channel Strip marking. Default yellow. Shared by overlay and panel. |
| BC colour | Overlay or panel on | Colour of the active Bus Comp marking. Default red. Shared by overlay and panel. |
| Selected FX colour | Overlay on | Colour of the surface-focused plug-in that is neither CS nor BC. MCP overlay only. Default blue. |
| Fill opacity | Overlay on | 0.00 (default) = outline only … 1.00 = solid fill. |
| Border opacity | Overlay on | Opacity of the box outline. Default 0.90. |
| Inserts row height | Overlay on | 8 – 48 px, default 17. Match it to your theme's FX-list row height. |
| Inserts top offset | Overlay on | -20 – 40 px, default 1. Nudges the first row up or down until the boxes line up. |

### Surface display

| Control | Effect |
|---|---|
| SEL LED follows REAPER track colour | Each strip's SEL LED renders the track's REAPER colour instead of monochrome. Off → SEL is white when selected. On by default. |
| Long track-name handling (combo) | How track names longer than the 7-char scribble-strip slot are shortened. *Truncate* (default) keeps the legacy first-7-chars cut ("Background Vocals" → "Backgro"). *Smart abbreviate* drops separators, then vowels after the first letter of each token, then collapses repeated consonants, then proportionally distributes the remaining char budget across tokens ("Background Vocals" → "BckgVcl", "Drums Bus" → "DrmsBs"). Short all-caps tokens (DI / FX / EQ / …) survive untouched. Mode switch repaints all 8 strips immediately. |

### Theme

Three-way radio:

- **Vanilla** (default) — ReaImGui's built-in dark theme, no overlay.
- **Dark** — Rea-Sixty's themed dark palette (blue-grey base + soft accents).
- **Light** — the Light counterpart for users on a light-mode REAPER setup.

Re-themes every Settings panel + the FX Learn schematic. Hardware-face colours (UF8 silk-screen mockups, UC1 schematic) stay constant.

### Font Size

Three-way radio: **Small** / **Normal** / **Large**. Drives every Settings widget except the UF8 / UC1 mockup schematic labels — those stay locked at 12 px so the schematics don't reflow when the picker changes. Numeric inputs (GR-cal table, FX Learn binding column) scale with the font picker so layouts stay aligned across sizes.

### Spelling

Two-way radio: **British (Colour, Grey)** / **American (Color, Gray)**. Switches the spelling of every user-facing string that differs between the two (Settings labels, the focused-track panel's right-click menu, etc.). British is the default.

### Settings window

- **Reopen on last tab viewed** — when on, the Settings window reopens on whichever pane you last had open instead of the default. Persisted globally.

\newpage

## Behaviour pane

### Tracks

| Toggle | Effect |
|---|---|
| TCP follows UF8 selection | UF8-triggered track selection scrolls REAPER's arrange-view track panel (action 40913). MCP follow is always on. Off by default. |
| Surface mirrors: TCP / MCP (radio) | Which of REAPER's two views the surface's track list mirrors. **TCP** (default) — the surface shows what the arrange view's track panel shows, so a track hidden in the TCP drops off the surface, and so do the children of a fully-collapsed folder whenever REAPER's own *Hide children of collapsed folders* preference is on. **MCP** — the surface follows the Mixer instead, hiding whatever the Mixer hides. Also available as the bindable **Surface mirrors: TCP** and **Surface mirrors: MCP** actions. |
| Pinned tracks survive banking | On by default. Pinned tracks — Focus-Set members, plus REAPER's own TCP pins while *Surface mirrors* is TCP — sit on the leftmost strips and stay there while everything else banks past them. Switches itself off when the pinned head would fill every usable strip, since there would be nothing left to bank. MCP has no pin concept, so the setting is inert in MCP mode. |
| Touch selects channel | Touching a UF8 fader exclusively selects that strip's track. Off by default. |
| Track selection follows parameter change | V-Pot / CS / BC knob edits on a non-selected track auto-select that track. Off → the UC1 stays on the currently selected track no matter which strip was just edited. Off by default. |

### Master track

Surface-side handling of the REAPER Master bus. See **Master track** (own chapter) for the full behaviour.

| Control | Effect |
|---|---|
| Show Master as Track 0 on UC1 | The UC1 CHANNEL encoder can scroll left past track 1 onto the Master as a virtual "track 0". UC1-only — the Master never appears on the UF8 strips through this toggle. Default off. |
| Pinned Master (combo) | How the *Pin Master to UF8 Strip 1 / 8* actions lay the strip out. *Replace strip* (default) — the pinned strip becomes the Master and hides whatever track was banked there. *Shift banking* — the regular tracks bank over the remaining 7 strips so none is hidden (the bank step + clamp drop to 7 while a pin is live). |

### Plug-ins

| Toggle | Effect |
|---|---|
| Don't show offline FX | Cycle rings (FX Cycle, Instance Cycle, per-strip variants) and the UF8 colour-bar default cursor skip FX slots that are set offline. Offline-only tracks show a `-`. Off by default. |
| Wrap Plug-in Cycle | Default on (legacy behaviour) — cycle rings wrap from last FX back to first. When off, both ends of the FX chain hard-stop on every cycle path (Channel-Encoder FX/Instance Cycle, per-strip V-Pot FX/Instance Cycle), and the UC1 carousel shows no neighbour name past the first/last FX. |
| SSL Strip Mode follows focused plug-in window | When REAPER's last-focused FX is a CS Instance, SSL Strip Mode auto-engages. Off by default. |
| Plug-in GUI follows active Instance | When an Instance Cycle / FX Cycle lands on a new target, an already-open floating plug-in GUI re-points to the new target. Off → the cycle moves the surface but leaves the window pinned to its current FX. On by default. |
| UF1 PLUG-IN key opens the plug-in GUI | Whether the UF1's `PLUG-IN` soft-key on a **native** SSL strip page opens the plug-in's GUI as well as claiming the surface. Off by default (surface only). An explicit UF1 map picks this per key instead (FX Learn → UF1 cell → Action); the built-in strip pages have no per-key storage, so they follow this one setting. |
| Auto-engage UF8 Plug-in Mode for UF8-mapped plug-ins | When SEL-Mode cycle V-Pot push OR a **Plug-in: toggle focused GUI** binding lands on a UF8-mapped plug-in, also engage UF8 Plug-in Mode with GUI. Off by default. |
| Pin plug-in GUI position | Every plug-in window Rea-Sixty subsequently opens snaps to a fixed position (size is left alone). Drag a window where you want it and click **Capture current**, or click **Center on Screen** for the middle of the display. The line above the buttons reads back the captured pin, or `(none captured yet)` — until something is captured the toggle does nothing. |
| Pin FX-chain GUI position | The same pattern for FX-chain windows, with its own captured position. Title matching looks for "FX:" on macOS. |

### Soft-keys

| Control | Effect |
|---|---|
| Parameter change switches soft-key bank | On by default. A focused-parameter change switches the SSL soft-key bank to whichever bank holds that parameter. Off → the bank stays put when you touch a parameter. The UF8 parameter display follows the parameter either way. |
| Engage a fixed soft-key bank at startup | Off by default. Ticking it reveals three combos — **Layer** (1-3), **Quick** (Q1-Q3), **Sub-bank** (V-POT / Soft 1-5) — and that user-Quick is engaged once on the first timer tick of every fresh REAPER session, instead of the plug-in-driven default. **Use current hardware bank** fills the three combos from whatever is engaged on the UF8 right now. Layer 1's Q1 / Q2 are greyed out: they are the hardcoded SSL CS / BC focus and carry no user-Quick slots. Ignored while UF8 Plug-in Mode is on — that mode owns the soft-keys. |

### Keyboard

| Toggle | Effect |
|---|---|
| Alt/Option + fader drag → snap back to original on release | Hold Alt/Option while moving a fader; release while still holding Alt → fader snaps back to its touch-on value. Mirrors REAPER's mouse Alt-drag. Off by default. |
| Keyboard Shift acts as Shift modifier | When on (the default), holding **Shift** on the host keyboard counts as the Shift modifier for any binding's Plain/Shift/Cmd/Ctrl modifier slot — in addition to the hardware **Modifier: …** bindings. |
| Keyboard Cmd (⌘) acts as Cmd modifier | Same, for Cmd on macOS. On by default. |
| Keyboard Ctrl acts as Ctrl modifier | Same, for Ctrl on Windows / Linux. On by default. |

The three FX-Learn modifier-layer switches used to sit here. They now live in *FX Learn → Modifier layers*.

\newpage

## Bindings pane

Top of the pane: a tab bar with **UF8**, **UC1** and **UF1**, each rendering its hardware as a vector schematic. Click any button, knob, encoder, or fader to select it; the per-button editor opens below the schematic.

The "current layer" follows whichever Layer button (1 / 2 / 3) is highlighted in the schematic — click a Layer button to switch the live layer; the green outline indicates which one is active. There is no separate layer-tab strip.

Three click-to-edit special cases:

- **Top-soft-keys** (the 8 buttons above the V-Pots) open the **user-Quick slot editor** for the (Layer, Quick, Sub-Bank) coordinate you are editing instead of the regular per-button editor — top-soft-keys are slot pickers, not direct actions.
- **Sub-bank selectors** (V-POT + 1..5) open the **sub-bank cell editor** with a Per-Quick LED override so the user can distinguish (Layer, Quick) contexts visually.
- Everything else uses the regular per-button binding editor.

### Offline soft-key editing + edit selector

You can view and edit the Top-Soft-Key slots **without a UF8 connected** and regardless of whether UF8 Plug-in Mode is on — the Bindings pane is fully offline.

When you click a Top-Soft-Key (or a Sub-Bank cell) in the schematic, an **Edit Quick** (Q1 / Q2 / Q3) and **Edit sub-bank** (V-POT / Soft 1-5) selector appears. Pick which coordinate to edit; the schematic and the slot editor then point at that (Layer, Quick, Sub-Bank).

The schematic shows **two rings** so you never lose track of what's live vs what you're editing:

- A **green ring** marks the Quick / Sub-Bank currently engaged live on the hardware.
- A **cyan ring** marks the one you're editing.

The Top-Soft-Key labels in the schematic **follow the edit selection**, so you can preview every Quick / Sub-Bank page offline — not just the one engaged on the surface.

### Factory Rea-Sixty soft-key banks

In the Sub-Bank editor there is a **Rea-Sixty factory banks** section. Pick a curated bank from the combo and **Recall into this Sub-Bank**, or **Load full set → Layer 1 / Quick 3** to drop all banks into Layer 1 / Quick 3's six sub-banks at once.

The seven factory banks are:

- **Encoder Modes** — Channel-Encoder mode switches (Ch Select / Instance / FX Cycle / FX Move / CS Cycle / Markers / Nudge / Focus Wheel).
- **Focus Set & Selsets** — pin / add / remove / toggle / set-from-selection / pin-focused / clear, plus Cycle Sets.
- **Plug-in Ops** — FX GUI / FX Chain / Close All FX / Bypass / Offline / Preset prev-next / SSL Strip.
- **Learn / Master** — Learn-HUD / Quick Learn (project + track) / Touch-Learn / Master pin left-right / focused-track panel / Out-Gain.
- **CS Favourites** — the eight *Switch to CS Favourite N* actions; the labels show each favourite's plug-in short name live (falling back to "CS Fav N" when a slot is empty).
- **BC Favourites** — the same for the eight Bus-Compressor favourites.
- **Brightness** — Both / LCDs / LEDs × up / down.

These are built only from Rea-Sixty's own actions; generic DAW actions (deselect, arm, zoom, etc.) you bind yourself to a free slot.

### Dynamic sub-banks

A normal Sub-Bank holds eight fixed assignments. A **dynamic** Sub-Bank computes its eight keys live from whatever track you are looking at — its plug-ins, for instance — so the keys change as you move around the session.

Open a Sub-Bank cell in the UF8 schematic and look for the **Dynamic bank** section. The dropdown offers:

| Setting | What the eight keys become |
|---|---|
| `Off (static slots)` | Normal behaviour — the eight slots you assigned |
| `FX (focused track, paged)` | The focused track's plug-ins |
| `Parameter Groups` | Parameter Groups 1–8 |
| `Track Colours` | Your eight-colour palette |

The choice is stored per Sub-Bank, so each of the six Sub-Banks under each Quick can be dynamic or static independently. While a bank is dynamic its eight stored slots are ignored — they are not lost, and turning the bank back to `Off` restores them.

Layer 1's Q1 and Q2 are driven by the plug-in and carry no user slots, so they cannot be made dynamic. Everything else can: Layer 1 Q3, and all three Quicks on Layers 2 and 3.

Dynamic banks are a UF8 feature and are inactive while UF8 Plug-in Mode is engaged.

**Track colours and Parameter Groups apply to every selected track,** not just the focused one, so you can colour or group a whole selection with one press. The FX bank always acts on the focused track alone.

**The FX bank**

Each key takes one plug-in from the focused track's chain, labelled with its name — the format prefix (`VST3:`, `JS:`) and the trailing vendor suffix are stripped so the name fits.

The LEDs read at a glance:

| LED | Meaning |
|---|---|
| Bright | The focused plug-in — the one the surface is following |
| Dim | Present and running |
| Dark | Bypassed, offline, or no plug-in in that position |

A track you have not touched yet shows its **first** plug-in bright, because that is where the surface's plug-in cursor starts.

Populated keys take their colour from the colour stored on the underlying static slot, so if you want your FX keys a particular colour, set it on the slots underneath.

**Paging.** A track can hold more than eight plug-ins. The **Page with** dropdown chooses what pages the bank:

| Setting | Control |
|---|---|
| `(no paging — stays on 1-8)` | **Default** — the bank shows the first eight only |
| `UF8 encoder` | The UF8 Channel encoder |
| `UC1 Encoder 1` / `UC1 Encoder 2` | Either UC1 encoder |
| `UF8 Bank ◄ ►` | The UF8 bank keys |

Note the default: **out of the box an FX bank does not page**, so a track with twelve plug-ins shows only the first eight until you pick a paging control. The chosen control only pages while this Sub-Bank is the engaged one; the rest of the time it does its normal job. Paging returns to the first page whenever the focused track changes.

**What a key press does** is configurable per gesture, and the setting is **global — it applies to every FX bank you create**, not to one bank:

| Gesture | Default |
|---|---|
| Push | Focus FX (surface follows) |
| +Shift | Float/close FX window |
| +Cmd | Bypass toggle |
| +Ctrl | FX solo (bypass others) |
| Long-press | Offline toggle |

The full set also includes **Delete FX**, **Move FX up** and **Move FX down**.

A long press is half a second and fires when you let go. Long-press *replaces* the modifier rather than combining with it — Shift plus a long press runs the Long-press action, not the +Shift one.

> **Delete FX removes the plug-in immediately, with no confirmation.** Think before you put it on Push.

*Focus FX* moves the surface onto that plug-in and re-points an already-open plug-in window at it. It does not open a window that was closed — use *Float* for that.

*FX solo* bypasses every other plug-in on the track and remembers what was on; pressing the same key again, or soloing another plug-in, puts them all back.

**Parameter Groups and Track Colours**

For these two the FX gesture table does not apply. Only short versus long press matters, and modifiers make no difference.

**Parameter Groups** — the eight keys are the eight groups, labelled with your group names (falling back to `Grp 1`…`Grp 8`). A key is bright when the focused track belongs to that group. A short press toggles membership across every selected track; a long press toggles whether the group is broadcasting at all. Group names are edited in the separate **Parameter Groups** Settings section, not here. Membership is stored in the project; group names and their on/off state are global.

**Track Colours** — the keys are labelled `Col 1`…`Col 8` and each glows its own palette colour, brightening when the focused track wears it. A short press paints every selected track; a long press clears the custom colour instead. The palette itself is edited right below the dropdown as eight swatches, and is global.

**If a bank stays blank**

With no track focused or selected, every dynamic bank goes blank and dark and its keys do nothing — there is no context to compute from.

One historical case: Rea-Sixty briefly had a **Sends** dynamic bank, which was removed. A configuration saved while it existed still loads, but the bank stays permanently blank. Set it to something else, or back to `Off (static slots)`.

### Binding visibility in the schematic

Bound buttons in the UF8 schematic are **tinted** (a soft green face / border); empty buttons stay neutral, so you can see at a glance which controls carry a binding. Hovering a bound button shows a **tooltip** listing what it fires on **Plain / +Shift / +Cmd / +Ctrl**, any **long-press** action, and the **behaviour** (Momentary / Toggle / Hold).

### Per-binding editor

For a regular button, the editor exposes:

- **Action type** — Native / REAPER Action / Keyboard / MIDI Command / Noop.
- **Action name** — text picker (auto-complete) for Native + REAPER Action.
- **Modifier slot** — Plain / Shift / Cmd / Ctrl. Each modifier slot is bound separately, so one physical button can carry up to 4 different bindings per layer.
- **Behavior** — Momentary / Toggle / Hold.
- **Long-press action** — separate action that fires after the long-press threshold (~500 ms) instead of the short-press action on release.
- **LED appearance** — colour + brightness override that replaces the action's default state-of mapping.
- For **MIDI Command** bindings: channel / note number / velocity / CC value as appropriate.
- For **REAPER Action** bindings: a search dialog that browses REAPER's Action List by name or command ID (also exposes ReaScript loading).

### Right-click context menu

Right-clicking a button in the schematic opens Copy / Paste / Clear options for that binding, plus **Reset binding to factory default** — restores just that one control's baked-in factory binding, leaving every other binding untouched (unlike the *Reset to factory defaults* in the About pane, which wipes everything).

### Export / Import / Reset

Bindings are bundled into the **Setup** export available from the **About** pane (single file covers bindings + plug-in maps + Settings preferences + Parameter Group slot names).

The Bindings pane itself offers **Save UC1 bindings…** and **Load UC1 bindings…** — these cover only the five UC1 controls. Loading replaces the UC1 bindings and leaves the UF8 bindings untouched.

Bindings storage paths:

- macOS: `~/Library/Application Support/REAPER/rea_sixty/bindings.json`
- Windows: `%APPDATA%\REAPER\rea_sixty\bindings.json`
- Linux: `~/.config/REAPER/rea_sixty/bindings.json`

\newpage

## Modes pane

**This pane configures how the Selection Modes behave when they are active.** Each sub-tab corresponds to one Selection Mode (or one cycle behaviour) — switching Selection Modes on the hardware uses the modes themselves; this pane only sets their per-mode options.

Four sub-tabs: AUTO · FX / Cycle · REC · NAV.

### AUTO

| Setting | Effect |
|---|---|
| Show only tracks armed for automation writing (hide Trim / Read) | While AUTO Selection Mode is engaged, tracks in mode 0 (Trim) or 1 (Read) are hidden from the surface. Touch / Write / Latch / Latch-Preview tracks remain visible. |
| Fill from left / Fill from right (radio pair) | When fewer visible tracks than the 8 hardware strips, choose which side they collect on. Project order is preserved either way. Active only while AUTO Selection Mode is engaged. |
| Selection-Set Auto-Mode (combo) | `None` / `Trim/Off` / `Read` / `Touch` / `Write` / `Latch` / `Latch Preview`. When set, recalling a Selection Set in AUTO mode forces its member tracks into this REAPER automation mode. Deactivating the set (or leaving AUTO mode) reverts those tracks to Trim/Read (mode 0). |
| Focus-Set Auto-Mode (combo) | Same options, for the pinned Focus Set — own knob, decoupled from the slot Selsets. Pinning the Focus Set in AUTO mode arms its members to this mode; unpinning (or leaving AUTO) reverts to Trim/Read. `None` = leave members' modes untouched. |

### FX / Cycle

> Active only while a cycle-kind Selection Mode (FX Cycle or Instance Cycle) is on the V-Pot row. Picks which physical controls drive the cycle; the active FX opens in the chosen view (V-Pot push only).

**Controls (multi-select checkboxes):**

- UF8 V-Pots (per-strip cycle) — default ON
- UF8 Channel Encoder
- UC1 Encoder 1 (CHANNEL)
- UC1 Encoder 2 (BC)

V-Pots cycle per-strip (each strip's own track). The three single encoders cycle the focused track and override their normal function while SEL Mode is engaged.

**V-Pot push opens active FX as (radio pair):** Floating window / FX chain.

### REC

| Setting | Effect |
|---|---|
| Enable RME / TotalReaper integration | Master switch. Requires the TotalReaper extension. While REC Selection Mode is active, the assignments below trigger TotalReaper actions against the strip's track. |
| V-Pot rotation → Preamp gain ±1 dB | Steps preamp gain instead of pan. |
| V-Pot rotation + Shift → Change input channel | Re-routes the strip's track input on rotation. |
| V-Pot push (combo) | TotalReaper action assignment. Choices: `None`, `Toggle 48V phantom`, `Toggle pad`, `Toggle phase invert`, `Toggle AutoLevel`. |
| Cut button (combo) | Same action list. |
| Solo button (combo) | Same action list. |

(Polarity is not a separately assignable REC button — only V-Pot push / Cut / Solo.)

### NAV

The NAV sub-tab is divided into five sections.

**Activation** — read-only list of which physical button currently fires each of the three Nav-mode toggle builtins:

- **Nav Mode (Markers & Regions): toggle**
- **Nav Mode: Markers only (no drill)**
- **Nav Mode: Regions only (no drill)**

Each line shows the bound layer + button + modifier + long-press flag, or "(unbound)". Edit via Settings → Bindings.

**View defaults**

- **Default view on Nav Mode entry** (radio): `Regions` / `Markers in current region` / `Markers (all)` / `Last used`. Applied by **Nav Mode (Markers & Regions): toggle** only. *Markers in current region* snaps to the region under the playhead; falls back to Regions if the playhead is in a gap.
- **Region-press behaviour** (radio): `Jump + Drill` / `Jump only` / `Drill only`. Jump = move transport to region start; Drill = enter the region's marker list. RegionsOnly view-lock always suppresses Drill.

**UF8 strip display**

- **Lower-row format** (radio): `Off (V-Pot value)` / `Index (R03 / M07)` / `Timecode (MM:SS)`. Off keeps the V-Pot value visible; Index / Timecode overlay marker metadata on the lower row.
- **Color-bar source** (radio): `REAPER marker colour` / `Force palette grey`. REAPER honours the colour override set on each marker / region; Force grey suppresses it.

**UC1 Encoder 2**

- **Take over UC1 Encoder 2 while Nav Mode is active** (checkbox). When off, Encoder 2 rotation stays bound to its normal action (**Encoder: scroll BC anchor track** by default) and the UC1 LCD does not switch to the marker carousel — only UF8 reflects Nav Mode.
- **Carousel scope** — currently single option *Mirror UF8 view*. Independent UC1 scopes (Always Regions / Always Markers / Always Markers-in-UF8-region) are not yet implemented.

**Push actions** — Plain push, Shift + push, and Long-press each pick any of the same 7 actions via dropdown:

| Action | Effect |
|---|---|
| Jump + Drill | In Regions view: jump transport to the region start AND enter the region's marker list. In Markers view: jump only (drill is meaningless there). |
| Jump only | Move transport / edit cursor to the cursor item. |
| Drill only | Enter the region's marker list (Regions view only; no-op elsewhere). |
| Back | Return from a drilled-into Markers-in-Region view to Regions. |
| Toggle View | Flip between Regions and Markers (all). |
| Add marker at playhead | Insert an empty marker at the playhead (or edit cursor when stopped). |
| Disabled | No-op. |

Defaults: Plain = Jump+Drill, Shift = Drill only, Long = Back.

View-locks (Markers-only / Regions-only) suppress **Drill only** specifically (Jump+Drill collapses to Jump only; Drill only becomes a no-op). Every other action fires regardless of lock. Long-press threshold ~500 ms.

**Auto-Follow**

- **Auto-Follow playhead / edit cursor** (checkbox). While Nav Mode is active, the cursor strip tracks whichever marker / region the playhead is on (or the edit cursor when stopped). In Markers-in-Region view, the overlay auto-rolls into the next region when the playhead crosses out.

\newpage

## FX Learn pane

The FX Learn pane teaches third-party plug-ins to behave as virtual Channel-Strip or Bus-Comp Instances. Built-in maps (SSL CS 2 / 4K B/E/G / BC 2 / 360 Link) always win — user maps can't shadow them.

The pane has two views:

### Master view (default)

Toolbar:

- **+ New** — opens the "new map" picker (browses your installed-FX catalog by name, lets you choose primary mode + UF8-Mode flag).
- **Export…** — write the full user-plug-in catalog to a JSON file.
- **Import…** — read a catalog JSON back in.

Below the toolbar:

- Search field (case-insensitive match against FX name or derived developer string).
- Table of user maps. Per row: display short, FX-name match, developer, domain, variant count, mapped-param count, last-edit date, **Edit** button, **Delete** button.

### Editor view (entered via Edit)

The editor **live-follows the active FX** — open it and it loads the map + instance for the plug-in you're currently looking at (the last-touched FX), so you don't have to find it in the list first. Manually picking a different map / instance still sticks until you touch another plug-in.

Top bar:

- **Domain** picker — `Channel Strip` / `Bus Comp` / `None (UF8-only)`. UF8-only maps the FX into the per-strip view without claiming a CS/BC slot.
- **UF8 Mode** checkbox (UF8-only domain) — drives Instance Cycle / Plug-in Mode.
- **Primary mode** picker (CS variant family) and other domain-specific options.
- **CS Favourite** dropdown (Channel-Strip domain only) — assign this plug-in to one of the 8 CS favourite slots, or clear it. See chapter *Favourites*.
- **Mockup toggle** — visualises the UC1 layout via a UC1 mockup PNG instead of the strip-bar schematic. Persisted in ExtState `ReaSixty/fxLearnMockup`.
- **AutoLearn** button — runs the pattern-matching engine (hardcoded SSL seeds + user-map dictionary; three-pass: exact / substring / token) against either the live FX on the focused track or the catalog's stored param snapshot. Confidence-scored suggestions open in an *AutoLearn Preview* modal with a per-row checkbox + confidence %, plus All / None bulk helpers. UF8 V-Pot suggestions auto-group by category (EQ / Comp / Gate / Filter / I-O / Misc). Accept applies every checked mapping into the active map.
- Breadcrumb **`← All maps`** to leave the editor.

Editor body — depends on the domain:

- **CS / BC domain:** the UC1 / strip schematic. Click a control to learn it to a plug-in parameter; the active row's combo lists every plug-in parameter (with current value if a live instance is on the focused track).
- **None (UF8-only):** the UF8 strip-bar schematic. Drag a strip slot (V-Pot, top soft-key, etc.) onto a plug-in parameter.

### UC1 EXT FUNCS list (CS mode)

Below the UC1 mockup, **Channel Strip domain only**, a 4-column grid lets you fill the UC1's hidden **EXT FUNCS** menu (opened with the UC1's **Back** button) with up to 10 of this plug-in's parameters. The grid is two side-by-side groups of five rows; each row is a **Name** field plus a **parameter** dropdown.

- The **Name** is shown on the UC1 LCD — the carousel shows up to **11 characters**, the header line shows the full name.
- Empty rows are skipped on the surface; the order on the device follows the grid (left column top-to-bottom, then right column).
- The dropdown lists this plug-in's parameters (from the FX-Learn param snapshot); pick **(none)** to clear a slot.
- This applies to **user-mapped (non-SSL) channel-strip plug-ins only**. SSL channel strips keep their fixed built-in EXT FUNCS list and are not edited here.
- Saved per plug-in in `user_plugins.json` (`extFuncs`, schema v8).

Note: a parameter you put in EXT FUNCS that's also on a physical V-Pot uses the plain encoder path — it doesn't share that V-Pot's range / curve / sensitivity. (SSL built-in EXT FUNCS slots keep full knob travel.)

### Right-click context menu

Right-clicking a mapped control on the UF8 / UC1 schematic opens per-control options:

- **Copy / Paste / Clear** the binding.
- **Fill sequential (right)** on a V-Pot / Fader / Solo / Cut / Sel — propagates the source-strip's attributes onto every strip to the right. Carried fields: faderInverted; V-Pot inverted / vpotMode / polarity / defaultNorm / stripColour / travel (range + curve + sensitivity); Solo / Cut / Sel colour; Reverse LED flag.
- **Inverted [off/on]** on a Fader / V-Pot — flips the rotation / direction-to-value mapping.
- **Reverse LED [off/on]** on a Solo / Cut / Sel button — XORs the LED on/off bit before painting. Use this for plug-ins whose Cut/Bypass param reports `1 = inactive` so the LED would otherwise stay bright while the function is off. Saved per `(fader-bank, strip, button)` in `user_plugins.json`.
- **V-Pot mode: Value / Toggle** on a V-Pot — Value = continuous (rotate scrubs, push resets to *Push reset*); Toggle = binary (rotate ignored, push flips 0↔1).
- **Polarity: Unipolar / Bipolar** on a Value-mode V-Pot — Unipolar (default) renders the LCD ring as L→R sweep; Bipolar renders centre-out (like SSL Pan) and makes the Log / Exp curve presets mirror around 0.5. Made for Pan, EQ-gain, mid-range freq sweeps — anything where "neutral" sits in the middle.
- **Knob travel** (V-Pot only — see *Knob travel + curve editor* below) — inline Min / Max sliders + **Advanced…** opens the curve editor.
- **Push reset** slider (Value-mode V-Pot) — the value the V-Pot snaps to when pushed. On a Bipolar V-Pot, a small "0.5" quick-set button + hint appears when the slider isn't already at centre.
- **Display label** (inline text field) — per-slot override for the scribble-strip name (1..7 ASCII chars). Empty = falls back to the parameter's default short name. Persisted as `UserLinkSlot.customLabel` (FX-Learn slot) or `UserUf8BankSlot.label` (UF8 V-Pot) / `UserUf8StripBinding.faderLabel` (UF8 fader) in `user_plugins.json` — no schema bump. Re-binding a slot to a *different* parameter clears its custom label (the label named the old param), so the field and the readout stay in agreement.
- **Save feel to** / **Apply feel from** / **Clear preset** (UC1 knobs + UF8 V-Pots only) — reusable tuning presets; see *Feel presets* below.

### UF1 layer

A map can carry a **UF1 layer** of its own, next to the UC1 one. The `UF1 layer`
tick-box turns it on and the `Mockup:` radio swaps the schematic between the UC1
and the UF1, so you lay parameters onto whichever surface you are editing.

Leave the UF1 layer **off** and the UF1 fills itself from the UC1 mapping — one
map, both surfaces, nothing to maintain twice. Turn it on when the UF1 should
differ, and the four buttons below the schematic manage it:

| Button | Does |
|---|---|
| **Fill: Replace** | Fill the page from the plug-in's parameters, discarding what is there |
| **Fill: Append** | Fill only the empty slots |
| **Fill from UC1** | Copy the UC1 layer's assignments onto the UF1 |
| **Unbind all** | Clear the page |

One warning about *Unbind all*: an **empty UF1 layer is authoritative**. Clearing
the layer does not hand the UF1 back to the UC1 fill — it leaves the UF1 empty,
which is what "I want nothing here" has to mean. Untick `UF1 layer` to go back to
inheriting.

**Show EQ Graph on the UF1** draws the EQ curve for a *learned* channel strip.
Rea-Sixty normally finds the fifteen EQ parameters by SSL's own parameter names,
which a third-party plug-in does not use; with this ticked it resolves each one
through the map instead. It only makes sense on a map whose domain is a channel
strip.

### Modifier layers (Normal / Option / Control / Control+Option)

A user-mapped UC1 control can carry **four independent layers** — *Normal* plus three held-modifier overlays, *Option*, *Control* and *Control+Option* — so the same physical knob or button drives a different parameter (with its own invert, knob-travel, push-cycle, and Display label) depending on which modifier you hold.

- **Editing:** the editor's layer tab strip (*Normal* / *Option* / *Control* / *Ctrl+Opt*) selects which layer you're editing; every per-control edit (bind, invert, Display label, knob travel, push-cycle) applies to the selected layer. Controls with no overlay on the active layer show a dim "ghost" ring — at runtime they inherit their Normal mapping.
- **Enabling at runtime:** three switches under *Settings → FX Learn → Modifier layers* — *"Hold Option for the FX-Learn Option layer"*, *"Hold Control for the FX-Learn Control layer"* and *"Hold Control+Option for the combined FX-Learn layer"*. All three are on by default. Holding **Option/Alt** selects the Option layer, **Control** the Control layer, and **both together** the Control+Option layer; with the combined switch off, both-held falls back to Normal, and holding neither is always Normal. The held modifier takes effect live — on the bound control, the UC1 LCD readout, and the Learn-HUD's layer badge. *(Windows: **AltGr** — the right Alt on European layouts — engages the Option layer, even though Windows reports it as Ctrl+Alt. Because of that, the Control+Option layer is reachable on Windows only with the **left** Control + **left** Alt.)*
- **Fallback:** an overlay left unmapped passes through to the Normal mapping for that control, so you only override the controls you want different under a modifier.
- **Scope:** UC1 only. The Display label (custom name) is **per layer** — each layer can show its own name on the UC1 readout / Learn-HUD.

### Knob travel + curve editor

Every user-learned FX-Learn slot and every UF8 V-Pot binding can carry a custom range, response curve, and encoder sensitivity. Defaults (Min=0, Max=1, sensitivity=1, no curve) make the maths byte-identical to a plain linear mapping, so untouched bindings behave exactly as if the feature wasn't there.

In the per-slot right-click menu:

- **Min** / **Max** rows — a fixed-width 4-column table (label · slider · numeric input · **Set** button) so both rows line up pixel-perfect. Slider scrubs in 0..1; the input accepts exact values; **Set** snaps the edge to the plug-in's *current* parameter value (handy when you've dialled the FX to where you want the limit and just want to capture it). Sliders auto-correct the opposing edge to keep Min ≤ Max.
- **Reset** — restores Min=0, Max=1.
- **Advanced…** — opens the Curve editor popup.

The Curve editor popup:

- **Sensitivity** — a single labelled row (label on its own line, then slider + numeric input + **1x** reset button). Range 0.1× .. 4×, encoder-delta multiplier. Combines multiplicatively with Shift = Fine (Shift still quarters on top of the user-set value). Hidden when editing a fader target.
- **Canvas** — draw a piecewise-linear response curve. Click empty space to add a breakpoint, drag to move, right-click to remove. The Y axis is normalised within [Min..Max], so the Linear preset is always a 45° diagonal regardless of how the range is trimmed.
- **Presets** — **Linear** (clears all breakpoints), **Log** (param rushes to the top — fine control near 0; on a Bipolar V-Pot the curve mirrors around 0.5 for a gentle ramp near centre + coarse at the edges), **Exp** (param stays small longer — fine control near 1; Bipolar mirrors for fine control at centre + rush to extremes), **Reset all** (clears curve + resets sensitivity to 1×). Bipolar polarity is re-read on every preset click so flipping it in the parent menu takes effect without re-opening the editor.
- **Close** dismisses the popup; all edits persist live as you make them.

### Feel presets

A tuned "feel" — the bundle of per-control tuning values, independent of which parameter the control is bound to — can be saved into one of **ten named preset slots** and re-applied to any other knob or V-Pot, on any plug-in. Useful for reusing, say, a Log frequency sweep or a slow stepped-detent across many controls without re-dialling each one.

The preset carries: **invert · Min/Max range · sensitivity · curve · polarity · push-reset**. It deliberately does **not** carry the binding (which parameter) or the display label (which names a specific parameter).

At the bottom of the right-click menu on a **UC1 knob** or **UF8 V-Pot**:

- **Save feel to ▸** — pick a slot (`1: (empty)` … or an existing name to overwrite); a small dialog asks for a name. The current control's feel is captured into that slot.
- **Apply feel from ▸** — lists the saved presets by name; picking one writes its feel onto the right-clicked control.
- **Clear preset ▸** — lists the saved presets; picking one empties that slot.
- **Apply this feel to all mappings** — takes the right-clicked control's current feel (invert · Min/Max range · sensitivity · curve · polarity · push-reset) and writes it onto **every mapped control in this plug-in's map**, on the layer you're editing — one action instead of re-applying a preset control by control. Knobs / V-Pots only (toggles have no travel). The same action is on the on-screen **Learn HUD** (Feel presets ▸ *Apply this feel to all mappings*).

The ten slots are **global** — shared across UC1 knobs, UF8 V-Pots, and every plug-in, and they survive a REAPER restart. Because both surfaces draw from the same store, a feel saved from a UC1 knob can be applied to a UF8 V-Pot and vice-versa. Persisted to REAPER's global ExtState (`rea_sixty` / `knob_feel_presets`) as JSON, separate from `user_plugins.json`. Toggles, buttons, and faders have no continuous travel, so the menu only appears on knobs and V-Pots.

### Stepped parameters

When the bound parameter is a discrete-stepped enum (e.g. PSP Townhouse attack/release time selectors, HPF slope pickers, oversampling toggles) — anything REAPER reports as having discrete steps — the editor automatically switches to a stepped-aware layout. Sensitivity, range, and push-reset still apply; curve does not.

- **Sensitivity** label flips to **Detent speed**. The default 1.0× means *2 detents per step* on V-Pots and UC1 knobs (matches the legacy UC1 stepped feel). 2.0× → 1 detent per step. 0.5× → 4 detents per step. 4.0× and above fire multiple steps per detent. Shift-Fine still applies on top — at slider min (0.1×) Shift gives an effective 0.025× (≈40 detents per step) for ultra-precise crawling.
- **Canvas + preset row hidden.** A single info line replaces them: `Stepped parameter — N values (~X.XXX per step). Curve disabled; Min/Max snap to the step grid.`
- **Min / Max** in the right-click menu snap on commit to the nearest step boundary, so the encoder always traverses real values. Below the table a hint line reads `~K steps reachable in this range`.
- **Push reset** (Value-mode V-Pots) snaps the chosen default to the nearest step.

The runtime accumulator decays after 150 ms of inactivity, so reversing direction at sub-threshold doesn't leave residual fractional steps fighting the new motion.

**JSFX continuous params** are handled specially: a JSFX often reports a coarse step size for a knob that is really continuous, which would otherwise force jumpy 2.4 dB jumps. Rea-Sixty normalises the JSFX step against the parameter's range so such a control resolves finely (continuous), with a fine-control accumulator for sub-step moves.

In the FX-Learn schematic, slots with customised knob travel show:

- Two radial ticks at the Min / Max angles on the on-screen knob (7 o'clock → 12 → 5).
- A small centre dot when a curve is set.

UF8 V-Pots apply the same sensitivity → curve → step math on every encoder movement, so external automation writes stay coherent. UC1 channel-strip / bus-comp knobs and the EXT_FUNCS encoder honour the same path — a UC1 knob and a UF8 V-Pot bound to the same parameter stay in lock-step. Built-in SSL CS / BC slots are intentionally untouched and keep the legacy linear + EQ-gain virtual-notch path; knob travel only kicks in when a user-learned slot is present for the focused plug-in.

> **UF8 faders intentionally exclude knob travel.** Absolute-position + motor feedback creates round-trip races with plug-in quantisation (fader jumps during user motion, snaps on release). The plug-in's own taper is the right place for fader-side shaping.

### Step cycle (rotary)

A V-Pot or UC1 knob can step through a curated list of a plug-in's button / enum options instead of moving a continuous parameter — for example walking an EQ type A → B → C, or a saturation mode, from a single rotary. **Turning** scrubs the list and stops at the ends; **pressing** advances one step and wraps around.

There are two ways to build a step cycle:

- **Capture by touch** (UF8 V-Pots): with **Touch-to-Learn** armed, wiggle the V-Pot you want, then click the plug-in's buttons in the order you want them — each press is recorded as a step. Wiggle the next V-Pot, or turn Touch-to-Learn off, to finish. The plug-in must already be mapped. Continuous controls keep binding the normal way, so a knob wiggle is not captured as steps.
- **Curate in the editor**: in the FX-Learn V-Pot right-click menu (or the on-screen Learn-HUD), pick **Step cycle (turn)** and edit the list — drag to reorder, untick to exclude an option, **+ Add step** to add one by name. The same editor opens for UC1 knobs.

On UC1, build step cycles through the **editor** rather than by touch capture. Re-learning a knob the normal way clears any step cycle on it, so a continuous pot is one re-learn away.

### Multi-instance picker

When the focused track has multiple FX matching the map's name, the editor surfaces a combo to choose which Instance's live readouts feed the editor. Picked index is per plug-in.

### GR meter override

Default GR meter behaviour is to read the host-extension `GainReduction_dB` value REAPER exposes for any plug-in implementing the PreSonus VST3 convention. That works for most modern compressors out of the box, with no setup.

When a plug-in doesn't expose the host-extension (or exposes a wrong value), a small **GR** button next to **AutoLearn** in the editor header opens a compact override popup:

- Combo lists every VST3 parameter on the editing map; pick the one that reads the plug-in's gain-reduction value.
- **Offset (dB)** slider — added before |abs| at render time. Lets you calibrate compressors whose GR reads negative-going (e.g. -6 dB at peak reduction → set offset −6 so the meter reads +6).
- **(none)** / **Use host extension** clears the override and restores the default behaviour.

When set, the button shows a tick mark next to the **GR** label. The override flows through to both the UC1 BC VU motor calibration tables and the DYN GR LED strip. Per-map; saved in `user_plugins.json` under `metering.grVst3Param` + `metering.grOffsetDb`.

### Param snapshot

When a catalog entry is created with the FX live on a track, parameter names + value formatters are snapshotted into the catalog so the editor stays usable even if no instance of the plug-in is currently loaded.

### Storage

Catalog file at `~/Library/Application Support/REAPER/rea_sixty/user_plugins.json` (and equivalent paths on Windows / Linux). Versioned schema (currently v7). Old v5 / v6 files auto-migrate on first load.

\newpage

## Selection Sets pane

Eight slots (1..8). The slot acts as a filter — combined with any active Folder Mode / Show-Only-Selected / AUTO-mode filters (a track must pass all of them). Recall toggles — pressing the active slot's hardware button again deactivates it.

Each slot is either:

- **Snapshot** — fixed list of REAPER track GUIDs frozen at save time.
- **Group** — bound to a REAPER track group (1..64). Membership refreshes continuously from REAPER's track groups across all Lead/Follow categories (ANY category).

The slot rows are laid out as a fixed-width 7-column table so columns align across rows regardless of slot type. Left to right:

- **`• Slot N`** — the `•` prefix marks the currently active slot.
- **Global** checkbox. When ON, the slot's content is workspace-global (ExtState, persists immediately). When OFF, project-scoped (saved into the project's RPP chunk on Cmd+S). Group slots benefit most from Global since "group N" is a stable concept across projects.
- **Type** combo: `Snapshot` / `Group`.
- **Name** text field.
- **Grp** spinner (Group rows) — REAPER track group index 1..64. Snapshot rows show `(N tracks)` in this column instead.
- **Save** button — Snapshot rows only. Overwrites the slot's GUID list with the current REAPER selection. (Save is hidden on Group rows — pressing it there would silently convert the slot to Snapshot and drop the live group binding, which is bad UX.)
- **Clear** button — Snapshot rows only. Empties the slot.

**Recall is not a settings-pane button**: slot activation lives on the hardware. Bind a button to the `Recall Selection Slot (toggle)` builtin with param 1..8 — pressing it engages the slot; pressing it again disengages. The `•` marker shows which slot is currently active.

### Auto-mode binding

A single global "Selection-Set Auto-Mode" combo (Settings → Modes → AUTO) applies to every slot. When a slot is active in AUTO Selection Mode + the combo is set to a value other than `None`, recalling that slot arms its tracks to the selected automation mode. Leaving the slot (or switching out of AUTO) reverts them to Trim/Read.

### Slot bank-snap

Recalling a slot snaps the surface to strip 0 = first channel of the set, so larger sets always start at the beginning. Re-pressing the same slot key keeps your current bank position.

### Driving from hardware

Bind buttons to **Recall Selection Slot (toggle)** with param 1..8, and **Save current REAPER selection to slot** with param 1..8, via Settings → Bindings.

\newpage

## Parameter Groups pane

Parallel parameter control across multiple tracks: while a slot is active, plug-in tweaks on the focused track copy to every member track of the slot. This works for the SSL Channel Strip / Bus Comp, for learned FX, **and now for any other plug-in with no mapping required** — as long as the member track hosts the exact same plug-in. (See the Parameter Groups reference section for the full rules.)

Top of the pane:

- **Multi-Select acts as temporary Parameter Group** (checkbox). When on AND no persistent slot is active AND multiple tracks are selected, those tracks become the live group.

Slot rows are laid out as a fixed-width 6-column table (so columns stay aligned across rows). Per row, left to right:

- **`• Slot N`** — `•` prefix marks an active slot.
- **Active** checkbox — toggle this slot's active state. Multiple slots can be active simultaneously (the fan-out is union-of-all-active-slot-members).
- **Name** text field.
- Member count display: `(N members)`.
- **Add Selected** button — add currently-selected REAPER tracks to this slot's membership.
- **Clear** button — empty the slot's membership.

Bottom of the pane:

- **Remove Selected Tracks from All Groups** button — pull every currently-selected track out of every slot.
- Hint text pointing at the Param Group native actions in Settings → Bindings.

### Storage

Per-track slot membership lives in `P_EXT:rea_sixty:param_groups` as an 8-bit bitmask (one bit per slot). Slot metadata (names, active flag) lives in a project-scoped JSON sidecar (`param_groups.json`).

\newpage

## About pane

The pane stacks several sections from top to bottom.

### Header

- Title + tagline ("Open-source SSL 360 replacement for UF8 / UC1").
- Author byline: "Made by Frank Acklin @ Stoersender Studio, Switzerland".
- Button **stoersender-studio.ch** — opens the studio website in the system browser.
- Commit-count blurb ("This project took N commits so far").
- "You can buy me a beer:" + **paypal.me/FrankAcklin** button.

### Versions

- **Version** — `git describe --tags --always --dirty` of the source tree at build time. On a tagged release: `v0.3.2`. Past a tag: `v0.3.2-N-g<sha>` (N commits past the tag). With uncommitted changes: trailing `-dirty`. Read this line first when triaging issues so it's obvious which build is loaded.
- **Build** — date + time of the compiled extension.
- **REAPER** — the host REAPER version string.
- **ReaImGui** — the bundled-ABI banner (currently v0.10).

### Project

- Repository URL display + **Open repository in browser** button.

### Setup (Export / Import / Reset)

A single bundled file format covering: bindings, learned plug-in maps, Parameter Group slot names, and every Settings preference. *Selection Sets + Parameter Group track memberships stay per-project and travel with the .RPP*.

- **Export setup…** — save the bundle to a chosen JSON file.
- **Import setup…** — load a bundle (replaces in-memory state immediately; warnings reported inline).
- **Reset to factory defaults** — confirmation popup. Replaces bindings + learned FX + parameter-group slot meta + every Settings preference with the baked-in factory configuration. Per-project Selection Sets and Parameter Group memberships are untouched.

### Windows USB driver (Windows only)

Section appears only when the build is Windows.

- Text: binds UF8 + UC1 to WinUSB so libusb can claim them without Zadig. One-time setup, requires admin. SSL 360° stops seeing the devices after install — reinstall SSL 360° to revert.
- **Install UF8/UC1 WinUSB driver** button — kicks off the in-product installer with a UAC + publisher prompt. After acceptance, unplug + replug devices.
- **Uninstall** button — runs `pnputil /delete-driver /uninstall` (UAC prompt), removes `rea_sixty_winusb.inf` from the driver store, and clears the Rea-Sixty signing cert from the My / Root / TrustedPublisher stores. After uninstall, unplug + replug devices; the SSL 360° driver (or whatever was previously bound) takes over again.

### Linux udev rule (Linux only)

Section appears only when the build is Linux.

- Text: grants non-root USB access by installing `/etc/udev/rules.d/99-rea-sixty.rules`. One-time setup, requires sudo (graphical password prompt).
- **Install Linux udev rule** button — runs pkexec, writes the rule, reloads udev. After install, unplug + replug UF8 + UC1, then restart REAPER.
- **Uninstall** button — pkexec removes `/etc/udev/rules.d/99-rea-sixty.rules`, then reloads + triggers udev. After uninstall the surface drops back to root-only USB access until a rule is reinstalled.

### Logs

- Lists the diagnostic log paths (`reaper_uf8_frames.log`, `reaper_uf8_colors.log`, and the rest). Logs live in the system temporary folder: `/tmp` on macOS and Linux, `%TEMP%` on Windows. The pane shows the real path for your platform rather than assuming one.
- **Reveal log folder** button — opens the folder in Finder, Explorer or your file manager.
- **Console output** checkbox, **off by default**. REAPER pops its Console window open for every message, which is noise when a device simply is not connected. The same text still goes to the log files either way, so nothing is lost — switch it on while diagnosing.

\newpage

# On-Screen Display

Rea-Sixty draws several optional helpers on REAPER's own screen. Each is rendered by a companion script that auto-installs and auto-starts with the extension — no manual ReaScript setup. Enable the overlay, focused-track panel, and mode-change banner in the Settings window; the Learn-HUD is toggled by an action.

## MCP Inserts overlay

A highlight drawn directly on the Mixer's track Inserts (FX) list, marking the **active Channel-Strip** plug-in (default **yellow**) and **active Bus-Comp** plug-in (default **red**) on the surface-focused track / BC anchor. The default is an **outline only** (Fill opacity 0).

Enable via *Settings → Appearance → On-screen → "Show MCP Inserts overlay"*. Controls (the CS / BC colours are shared with the focused-track panel):

- **CS colour / BC colour** pickers.
- **Fill opacity** (0 = outline only) and **Border opacity**.
- **Inserts row height** and **Inserts top offset** — fine-tune the box to line up with your theme's FX-list rows.

## Focused-track panel

A frameless, Gridbox-style box that floats on the Arrange (or any window you drag it onto), showing the surface-focused track's **name**, its active **CS / BC plug-in** (as **short names** — your FX-Learn `displayShort`, with the SSL factory plug-ins shown as **CS2** / **BC2**), and the last-touched parameter per domain. A touched parameter shows your custom **Display label** when one is set (for the held FX-Learn layer), matching the surface readout.

Enable via *Settings → Appearance → On-screen → "Show focused-track panel"*. Drag the body to move, drag the edges to resize; position + size persist. Right-click menu:

- **Layout** — Two lines (CS / BC) or One line.
- **Track name** — *Show track name*; **Use track colour** (draws the track name in the track's REAPER colour — falls back to grey if the track has no custom colour assigned); *Full name* / *Smart abbreviate* / *Abbreviation length*; **Before / After CS/BC** (whether the track name sits before or after the plug-in tag).
- **Customize** — Font size, Corner radius, Background / Border / CS / BC colour.
- **Load on startup** — auto-launch the panel when REAPER starts (writes a marked one-liner into `Scripts/__startup.lua`; untick to remove it).
- **Close panel**.

## Mode-change banner

A transient on-screen banner that flashes the new mode for ~2 s whenever you switch **Selection Mode** or **Channel-Encoder Mode**, then auto-hides — a quick "what did I just switch to?" confirmation without looking at the surface.

Enable via *Settings → Appearance → On-screen → "Show mode-change banner"*, or bind / run the **Mode-change banner: show / hide (flashes Sel / Encoder mode)** action. It is **off by default** and remembers its on / off state across restarts.

Drag the banner (while it's visible) to reposition it — position persists. Right-click it for **font size**, **duration**, and **colours**.

## Learn-HUD

A dockable window showing the focused plug-in's **UC1 control → parameter assignments** as a grouped, readable text list — so you can see what each knob / button does without opening Settings.

- Toggle with the **Learn-HUD: show / hide (focused plug-in assignments)** action (bind it to a surface button) or the REAPER action **"Rea-Sixty: Toggle Learn-HUD"** (`REASIXTY_LEARN_HUD_TOGGLE`). There is no Settings checkbox.
- **CS / BC / UF8 tabs** at the top, auto-following the focused domain (click to pin one). The BC tab follows the BC anchor / BC-encoder selection, and a CS-mapped plug-in only ever shows on CS (and BC on BC) — the two domains never cross. The **UF8 tab** shows the per-strip UF8 assignments (V-Pot / fader / soft-keys) for the focused plug-in and offers the same learn / tuning controls.
- Each row = the SSL slot name + the bound parameter's name (or your custom Display label). Rows are grouped by section (Filter / EQ / Dynamics / Gate / I-O for CS; the Bus-Comp knobs for BC), with Dynamics + Gate in a right-hand column to mirror the hardware.
- A **layer badge** (NORM / OPT / CTRL / C+O) shows the held FX-Learn modifier layer; the list follows it live. On a modifier layer the list shows **only the controls you actually overlaid on that layer** — controls that fall through to their Normal mapping read as unmapped, so you can see at a glance what's layer-specific.
- **Right-click → View** switches between the grouped text **List** and a hardware **Mockup** (the UC1/UF8 face). **Right-click → Text size** (Small … Huge); the menus themselves honour your *Appearance → Font Size*. The window size persists globally.
- **Click a row (or mockup control), then wiggle that plug-in's parameter** to learn it onto the control (user maps only — built-in SSL maps are factory-fixed and show a hint instead).
- **Right-click a control** (list row or mockup knob/button) for a per-control menu: **Learn** (wiggle a parameter), **Invert** (flip the control's polarity), **Rename…** (set a custom Display label; empty reverts to the default name), **Unbind**, plus the **knob-travel / curve editor**, **feel presets** and **stepped-parameter** controls — full FX-Learn parity without opening Settings. All act on the active modifier layer; Invert / Rename / Unbind are disabled on an unmapped control, and built-in SSL maps show the factory-fixed hint.
- The window's **☰ menu** carries a **CS Favourite** submenu — favourite the live Channel-Strip into a favourite slot (greyed when no CS is on the focused track). See chapter *Favourites*.

\newpage

# Native actions

Bind any of these to a UF8 / UC1 control in Settings → Bindings → *(button)* → Native. Actions that take a parameter (slot number, soft-key index, etc.) are flagged below.

The picker groups them into categories — Selection Modes, Encoder Modes, Cycle Actions, Plug-in, Layer, Soft-Key Bank, SSL, Master, and so on. **Hardware Modes** is the largest: it collects everything that changes what the surface *is doing* rather than acting on a track — FLIP and PAN, Folder Mode and Show Only Selected, Home, SSL Strip and UF8 Plug-in Mode, the on-screen panel toggles, Touch-to-Learn, the mirror and follow settings below, and Restart. The sections in this chapter are grouped by function rather than by category, so a single category's actions appear in several places.

## Selection Mode toggles

Switch which Selection Mode is active (see chapter **Selection Modes** for what each one does). Pressing a mode's toggle while it is already active returns to NORM.

- **Selection Mode → NORM (SEL Button)** — set the active Selection Mode to NORM (the default).
- **Selection Mode → REC (SEL Button)** — toggle REC mode (SEL = arm).
- **Selection Mode → REC + MON (SEL Button)** — toggle REC+MON mode (SEL = arm + monitor).
- **Selection Mode → AUTO (V-Pot)** — toggle AUTO mode (SEL = step the track's automation mode; V-Pots show automation indicator).
- **Selection Mode → FX Cycle (V-Pot)** — toggle FX Cycle Sel-Mode (V-Pots walk every FX per strip; push opens active FX).
- **Selection Mode → Instance Cycle (V-Pot)** — toggle Instance Cycle Sel-Mode (V-Pots walk only Instances per strip).
- **Selection: Clear All Tracks** — clear the REAPER track selection.

## Channel Encoder mode toggles

Change which job the large CHANNEL encoder does. The current mode persists across REAPER restarts.

- **Encoder Mode → Channel Select** — Channel Select (the default; rotation moves track selection ±1).
- **Encoder Mode → Nudge** — playhead nudge. Step size is set in **Settings → Modes → Nudge**: an amount + unit (ms, seconds, grid, bars, samples, frames). Default is one **grid** step per detent — "Grid" follows the project grid, so it covers beats and bars.
- **Encoder Mode → Mousewheel** — synthesised mouse-wheel under the screen cursor (use to scroll plug-in windows, browsers, etc.).
- **Encoder Mode → Markers (prev / next)** — step prev / next REAPER marker.
- **Encoder Mode → Bank by 1 channel** — surface bank by 1 strip per detent.
- **Encoder Mode → Last Touched Param** — step the last-touched REAPER parameter ±.
- **Encoder Mode → Instance Cycle** — Instance Cycle on the focused track.
- **Encoder Mode → FX Cycle** — FX Cycle (every FX) on the focused track.
- **Encoder Mode → Instance Cycle (across tracks)** — Cycle Instance across tracks: Instances on the focused track, then one detent per track boundary onto the neighbour's first/last Instance. Hard-stop at the project edge.
- **Encoder Mode → FX Cycle (across tracks)** — Cycle FX across tracks: same cross-track behaviour for every FX.
- **Encoder Mode → FX Move (in chain)** — move the active FX up / down within the focused track's chain on rotation (within-chain only; hard-stop at the ends).
- **Encoder Mode → Selection Set Cycle** — step through populated Selection Set slots (off → 1 → 2 → … → off).
- **Encoder Mode → CS Cycle (favourites)** — cycle the active CS through the favourite slots (see chapter *Favourites*).
- **Encoder: dispatch by current mode** — routes rotation to whichever encoder mode is currently set. Bound by default to the CHANNEL encoder so rotation just "does the right thing"; rebind if you want a fixed behaviour.

## Direct encoder rotation handlers

Bind these to a non-CHANNEL rotation (UC1 Encoder 2, footswitches with rotation, etc.) when you want a single fixed behaviour regardless of the global encoder mode.

- **Encoder: cycle plug-in instance** — Instance Cycle on focused track (rotation = step ±).
- **Encoder: cycle FX (all on focused track)** — FX Cycle on focused track.
- **Encoder: cycle instance (across tracks)** — Cycle Instance across tracks (direct): Instances on the focused track, then cross to the adjacent track at the edge.
- **Encoder: cycle FX (across tracks)** — Cycle FX across tracks (direct): every FX, crossing to the adjacent track at the edge.
- **Encoder: move active FX up/down in chain** — direct-rotation counterpart of the FX Move mode: rotation moves the active FX up / down in the focused track's chain (hard-stop at the ends).
- **Encoder: select prev/next track** — step REAPER track selection ±1.
- **Encoder: scroll tracks** — visible-track scroll + select + UC1 focused-track follow + force CS-domain focus. Like *Encoder: select prev/next track* but UC1-aware. Default binding on UC1 Encoder 1.
- **Encoder: extend track selection (Shift+Arrow style)** — extend the REAPER track selection across the visible space, anchored to the focused track (Shift+Arrow-style range select).
- **Encoder: nudge playhead** — playhead nudge ±.
- **Encoder: scroll mouse-wheel under cursor** — synthesised scroll-wheel under the screen cursor.
- **Encoder: scroll BC anchor track** — scroll the UC1's Bus Comp anchor track ±1 (which track the BC section is pinned to). REAPER selection and UF8 bank stay put.
- **Encoder: scroll and select BC anchor track** — same scroll as *Encoder: scroll BC anchor track*, but additionally pulls REAPER selection + UF8 bank to the new BC anchor. Use this when you want the BC encoder to drive the whole surface, not just the UC1 carousel.
- **Encoder: scroll Focus Set members** — encoder scroll within the Focus Set (see below). Walks the set in REAPER project order; works regardless of whether the Focus Set is pinned.

## Plug-in Mixer modes

Engage / disengage the on-surface plug-in editing modes.

- **Toggle SSL Strip Mode** — toggle SSL Strip Mode. While active, fader → CS Fader Level, V-Pots → CS controls, soft-keys → CS bypass / EQ-In / Filter-In.
- **Toggle SSL Strip Mode (with GUI)** — same, AND open the CS plug-in's floating GUI (pinned per the GUI-pin settings).
- **Toggle UF8 Plug-in Mode** — toggle UF8 Plug-in Mode. All 8 strips become the strips of a single FX-Learn-mapped plug-in. Bank ← / → flips between fader-banks A / B for ≥9-control plug-ins.
- **Toggle UF8 Plug-in Mode (with GUI)** — same, AND open the plug-in's floating GUI.

## Plug-in commands

These act on the FX the cursor currently points at on the focused track (the FX that the last cycle landed on; defaults to the first online Instance after a fresh load).

- **Plug-in: toggle focused GUI** — toggle the floating GUI of the cursor FX. With the Device option *Auto-engage UF8 Plug-in Mode* on, also engages UF8 Plug-in Mode if the cursor lands on a UF8-mapped plug-in.
- **Plug-in: toggle bypass (active FX)** — toggle bypass of the cursor FX.
- **Plug-in: toggle offline (active FX)** — toggle offline state of the cursor FX.
- **Plug-in: next preset (active FX)** — load the next preset.
- **Plug-in: previous preset (active FX)** — load the previous preset.
- **Plug-in: cycle preset (encoder, active FX)** — encoder-rotation variant that steps presets ± by detent count.
- **Plug-in: move active FX up in chain** — move the cursor FX up one slot in the track's FX chain.
- **Plug-in: move active FX down in chain** — move the cursor FX down one slot.
- **Plug-in: toggle FX chain window (focused track)** — open / close REAPER's FX chain window for the focused track (pinned per the FX-chain pin settings).
- **Plug-in: close all floating FX windows** — close every floating FX window in the project.
- **FX param: step up** — step the FX-Learn slot a V-Pot is bound to upward from a button. Action-picker exposes the slot target (combo built from the built-in plug-in map registry — link IDs are stable across SSL CS / BC variants), a step-size slider, and a wrap-vs-clamp checkbox. Honours the slot's range, curve, and sensitivity, so a button bound to *FX param: step up* and a V-Pot bound to the same slot stay in sync. Useful for "+1 dB" or "next preset value" buttons.
- **FX param: step down** — same as *FX param: step up* with the sign flipped.

## Sticky Pot

Pin one plug-in parameter to a track's V-Pot. The pin stays on that track — it moves with the track across banks and overrides the strip's normal V-Pot target (the focused parameter, or pan) until you clear it. Each track can hold its own pin. A pinned strip's value line shows the parameter's name (with a leading `*`) and value, and the readout ring follows it.

- **Sticky Pot: Get next touched Parameter** — arm capture. The next plug-in parameter you touch — in the plug-in's own window, or on a controller — pins to the track it belongs to. Touch the parameter you want and it sticks. While capture is armed, a **V-Pot press** on a strip clears that strip's pin instead of capturing.
- **Sticky Pot: Toggle active/inactive** — suspend every pin at once (so the V-Pots return to their normal view), then bring them all back. The pins themselves are kept. Lit while active.

A **V-Pot press** on a pinned strip resets its parameter to the centre (a two-state parameter flips instead). Pins are **saved with the project** and restored on reload — keyed to the track and the plug-in, so they survive re-banking and FX-chain reorders.

Under **FLIP**, the pin moves onto the **fader** instead of stepping aside. FLIP assigns the current V-Pot parameter to the fader, and the pin *is* that parameter — so the fader's value, motor follow and touch all track the pinned parameter, while the V-Pot rides Volume as it normally does under FLIP.

Sticky Pot otherwise steps aside automatically whenever a mode already owns the V-Pot: UF8 Plug-in Mode, SSL Strip Mode, PAN held, and the Instance / Instance-Cycle Selection Modes. Release the mode and the pins reappear.

## Favourites

Replace or duplicate the active plug-in, carrying values across (see chapter *Favourites*). All of these act on every selected track, else the surface-focused track. Each exists three times: for the Channel Strip, for the Bus Compressor, and in a **Focused Domain** form that follows whichever you are working on.

- **Switch to CS Favourite 1 … 8** / **Switch to BC Favourite 1 … 8** / **Switch to Favourite 1 … 8 (Focused Domain)** — replace the active plug-in with favourite N. Lit when it already is that favourite.
- **Copy to CS Favourite 1 … 8** / **Copy to BC Favourite 1 … 8** / **Copy to Favourite 1 … 8 (Focused Domain)** — insert favourite N below the active plug-in with the values carried, and bypass the original. An instant A/B.
- **CS Favourite Cycle** / **BC Favourite Cycle** / **Favourite Cycle (Focused Domain)** — step through the favourites; on an encoder, one step per detent.
- **CS Favourite Copy/Own** / **BC Favourite Copy/Own** / **Favourite Copy/Own (Focused Domain)** — flip that domain between carrying live values and restoring each favourite's own settings.

The cycles are also available as Channel-Encoder modes — **Encoder Mode → CS Cycle (Favourites)**, **→ BC Cycle (Favourites)** and **→ Favourite Cycle (Focused Domain)** (also listed under *Channel Encoder mode toggles*).

## Instance navigation

These step *only* the Instance index (CS / BC / UF8-Mode-mapped). They are the focused-domain equivalent of the Instance Cycle Sel-Mode, but as standalone bind targets.

- **Instance: next (focused domain)** — next Instance in the focused domain on the focused track. Wraps.
- **Instance: previous (focused domain)** — previous Instance in the focused domain.
- **Focus → Channel Strip** — set the focused domain to Channel Strip (so subsequent *Instance: next* walks CS Instances, UC1 CS section refreshes).
- **Focus → Bus Comp** — set the focused domain to Bus Comp.

The focused parameter slot is **preserved across an Instance Cycle** when the new instance offers the same LinkSlot (same domain, same parameter convention). Stops the focused-param surfaces (UC1 BC/CS encoder, V-Pot mirroring) from snapping back to slot 0 — typically the Bypass / FX In toggle — on every cycle step. Cross-domain cycles (CS → BC) and UF8-only user maps still reset to slot 0 because the slot position isn't meaningful there.

## Per-track automation modes

Set the focused track's automation mode. (*Automation: Off / Trim* and *Automation: Trim* are alternate names for the same REAPER mode 0; both kept for binding-file compatibility.)

- **Automation: Off / Trim**, **Automation: Trim** — mode 0 (Off / Trim).
- **Automation: Read** — mode 1.
- **Automation: Touch** — mode 2.
- **Automation: Write** — mode 3.
- **Automation: Latch** — mode 4.
- **Automation: Latch Prv** — mode 5 (Latch Preview).

## Project-global automation override

Same six modes, but applied via REAPER's *global override* (overrides every track without changing each track's own mode).

- **Automation: Off / Trim (Global)**, **Automation: Trim (Global)**, **Automation: Read (Global)**, **Automation: Touch (Global)**, **Automation: Write (Global)**, **Automation: Latch (Global)**, **Automation: Latch Prv (Global)**.
- **Automation: Zero All Tracks (→ Trim/Read)** — reset every track's automation mode to Trim/Read (mode 0). Useful to revert a write session.

## Bank navigation

- **Bank ← (UF8 Plug-in Mode: fader-bank; else ±strip scroll)** — scroll the surface 8 strips left. In UF8 Plug-in Mode the same button flips between fader-banks A / B (for 16-strip plug-ins) instead.
- **Bank → (UF8 Plug-in Mode: fader-bank; else ±strip scroll)** — scroll 8 strips right (same fader-bank flip in UF8 Plug-in Mode).
- **Bank by 1ch ← (one strip)** / **Bank by 1ch → (one strip)** — scroll one strip at a time.
- **Home (clear routing toggles)** — clear all routing toggles (send / receive views) so V-Pots and faders return to track volume + pan. Also resets the send-list page to the start.
- **Page ← (soft-key bank prev)** / **Page → (soft-key bank next)** — step the SSL Soft-Key PAGE bank (prev / next of the 6 CS banks + 2 BC banks).

## DAW Layer keys

- **Switch to Layer 1** / **Switch to Layer 2** / **Switch to Layer 3** — switch SSL DAW layer.

## SSL Soft-keys

- **Select soft-key bank (param 0..5)** — select Soft-Key bank N (CS banks 0..5; in Bus-Comp mode, 0..1).
- **SSL Soft-Key (current bank, slot 0..7)** — fire SSL Soft-Key cell N in the currently selected bank.
- **SSL Standard Bank: V-POT (param: 0..7)** — fire SSL V-Pot N (bank-current). The native bindings the Top Soft-Keys use to map to the active CS/BC bank's parameters. (Explicit-bank siblings **SSL Standard Bank 1** … **SSL Standard Bank 5** target a fixed bank.)

## Send / Receive

All of these are toggles, and all take **param: 0 = Faders, 1 = V-Pots** to choose which control the route lands on. In the binding editor that choice is the **Flip** checkbox. See *Send and Receive views* for what the strips then do.

- **8 sends of focused track** — spread the focused track's send list across the eight strips.
- **8 receives of focused track** — same for its receives.
- **Send 1 ↦ all tracks** … **Send 8 ↦ all tracks** — eight separate actions. Each keeps every strip on its own banked track and drives that track's send number N.
- **Receive 1 ↦ all tracks** … **Receive 8 ↦ all tracks** — same for receives.

**Home (clear routing toggles)**, listed above under bank navigation, turns all of them off at once.

## Selection Sets

- **Recall Selection Slot (toggle) (param: 1..8)** — toggle slot N: activate if inactive, deactivate if already active. Activation filters the surface to the slot's tracks and snaps the bank to strip 0 = first slot track. Coexists with the Focus Set (the slot filters, the Focus Set pins within the filtered list).
- **Save current REAPER selection to slot (param: 1..8)** — save the current REAPER track selection into slot N.
- **Encoder: cycle Selection Set (off → 1 → 2 → … → off)** — encoder-rotation handler that steps off → first populated slot → next → … → off. Skips empty slots.
- **Focus Set** — pin-source actions (*Focus Set: add selected* / *remove selected* / *pin (toggle)* / *clear* / *toggle selected* / *set from selection* / *pin focused track*, plus the *Encoder: scroll Focus Set members* encoder action). See *Selection Sets → Focus Set* for the full list and behaviour. *Focus Set: pin (toggle)* lights its bound button via LED state-of while pinning is on.

## Surface filters / view toggles

- **Toggle Folder Mode (parents only)** — toggle Folder Mode (only top-level tracks visible; folder children appear on spill).
- **Toggle Show Only Selected** — toggle Show Only Selected (only currently-selected REAPER tracks appear).
- **Open / Close Rea-Sixty Settings** — open / close the Rea-Sixty Settings window. Default binding for the `360°` key.
- **Surface mirrors: TCP** / **Surface mirrors: MCP** — choose which window's track visibility the surface follows: the Arrange view's track panels (TCP) or the Mixer (MCP). Hiding a track in the chosen window removes it from the surface. These are not a toggle but a mutually-exclusive pair, so a bound key sets one mode absolutely and lights while that mode is the active one — which means you can bind both and see at a glance which is on. The choice is remembered between sessions.
- **TCP follows selection** — toggle whether selecting a track on the surface scrolls the Arrange view to it. Same setting as *Settings → Behaviour → Tracks*.

## Nav overlay (Markers + Regions)

- **Nav Mode (Markers & Regions): toggle** — toggle the Nav overlay (UF8 strips show markers / regions, soft-keys jump to them).
- **Nav Mode: Markers only (no drill)** — Nav overlay restricted to markers only.
- **Nav Mode: Regions only (no drill)** — Nav overlay restricted to regions only.

## Track arming

- **Tracks: Arm All / Unarm All (toggle)** — toggle arm on every track in the project. State is "all armed" ↔ "all unarmed"; mixed state arms everything first, then needs a second press to unarm all.

## Master track

- **Pin Master to UF8 Strip 1** — pin the REAPER Master bus onto UF8 strip 1 (toggle). Bindings category **Master**; also the REAPER action *Rea-Sixty: Pin Master to UF8 Strip 1*.
- **Pin Master to UF8 Strip 8** — pin the Master onto UF8 strip 8 (toggle). REAPER action *Rea-Sixty: Pin Master to UF8 Strip 8*. See *Master track* for Replace vs Shift layout and the mode interactions.

## Brightness

Each press steps one level (Dark → Dim → Half → Bright → Full). The "Both" variants step LEDs + LCDs in lockstep.

- **Brightness LEDs +** / **Brightness LEDs -** — LED ring + button-LED brightness.
- **Brightness LCDs +** / **Brightness LCDs -** — UF8 LCD + UC1 LCD brightness.
- **Brightness Both (LEDs+LCDs) +** / **Brightness Both (LEDs+LCDs) -** — combined.

## Zoom

Each maps to a REAPER zoom action (defaults for the UF8 cursor pad).

- **Zoom in vertically** — zoom in vertically (action 40111).
- **Zoom out vertically** — zoom out vertically (40112).
- **Zoom out horizontally** — zoom out horizontally (1011).
- **Zoom in horizontally** — zoom in horizontally (1012).
- **Zoom to fit project** — zoom to fit the whole project (40295).

## Parameter Groups

Per-slot actions also exist for slots 1..8: **Param Group N → Add Selected Tracks**, **Param Group N → Clear Members**, and **Param Group N → Toggle Active**.

- **Param Groups → Remove Selected from All** — remove the currently-selected tracks from every Parameter Group slot they appear in.
- **Param Groups → Multi-Select acts as Temp Group** — toggle the "multi-select acts as a temporary parameter group" behaviour. When on, any multi-track selection becomes the active group for parameter-fan-out; when off, the active group is whichever persistent slot is selected.

## Modifier keys

When held, these shift every other binding to its modifier slot. The three matching UF8 keys (Shift / Cmd / Ctrl, or FINE for Shift) are bound to them by default but you can rebind to any other button to relocate the modifier.

- **Modifier: Shift / Fine (double-click latches)** — Shift modifier. Double-click latches (press once more to unlatch). The SSL `FINE` key uses this builtin.
- **Modifier: Cmd** — Cmd modifier (macOS) / Windows key (Win) / Super (Linux).
- **Modifier: Ctrl** — Ctrl modifier.

## Surface-state toggles

- **Toggle FLIP (fader ↔ V-Pot)** — swap fader and V-Pot values for the active routing target (e.g. swap send level and send pan between V-Pot ring and motorised fader).
- **Toggle V-Pots → Pan** — force V-Pots to Pan regardless of the active Selection Mode. Escape hatch from cycle / REC / AUTO when you need pan back quickly.
- **Learn-HUD: show / hide (focused plug-in assignments)** — show / hide the **Learn-HUD** (focused plug-in's assignments). Bindable here (category *Hardware Modes*) **and** available as the REAPER action *Rea-Sixty: Toggle Learn-HUD* (`REASIXTY_LEARN_HUD_TOGGLE`). See *On-Screen Display → Learn-HUD*.
- **Focused-track panel: show / hide (frameless, on Arrange)** — show / hide the frameless **focused-track panel**. See *On-Screen Display → Focused-track panel*.
- **Mode-change banner: show / hide (flashes Sel / Encoder mode)** — show / hide the transient **mode-change banner**. See *On-Screen Display → Mode-change banner*.
- **Touch-to-Learn: arm / disarm (touch a control, wiggle a param)** — arm / disarm **Touch-to-Learn**. While armed, touch a control on the surface and wiggle a plug-in parameter to learn it to that control on the fly — FX-Learn without opening Settings. Disarming cancels and clears the pending learn. Bindable here (category *Hardware Modes*); the soft-keys switch to the V-Pot layer while armed, and a V-Pot **press** learns as a Toggle binding. Pressing several of the plug-in's buttons in a row while a V-Pot is armed builds a **step cycle** on it (see *FX Learn → Step cycle*).
- **Toggle UC1 Out-Gain (Mapped ↔ REAPER Fader)** — flip the UC1 **Out Gain** knob between its mapped SSL Channel-Strip *Fader Level* parameter and **REAPER's track volume fader**. While engaged, the knob drives track volume even on tracks with no channel-strip plug-in, and the LED ring + readout follow the track fader. Bindable from the Bindings picker (under *Hardware Modes*) **and** available as the REAPER action *Rea-Sixty: Toggle UC1 Out-Gain (Mapped ↔ REAPER Fader)* (`REASIXTY_UC1_OUTGAIN_FADER_TOGGLE`) for the keyboard / toolbar.
- **FX: Quick Learn Project** / **FX: Quick Learn Track** — run the AutoLearn sweep over the whole project / the focused track. Same one-shot as the REAPER actions *Rea-Sixty: Quick Learn …*.
- **Restart Rea-Sixty (re-open devices)** — close the UF8, UC1 and MIDI ports and open them again, so a surface that has stopped responding can be revived without the trip through Preferences → Control Surface. Bindable here (category *Hardware Modes*) **and** available as the REAPER action *Rea-Sixty: Restart Rea-Sixty (re-open devices)* (`REASIXTY_RESTART`) — worth a keyboard shortcut, since a hardware key is no help when the hardware is the thing that needs re-opening. This re-opens the **devices**, not the extension: REAPER holds the plug-in for the life of the process, so a new build still needs REAPER restarted.

## Internal (not user-bindable)

- The REAPER Action picker writes an internal carrier into a binding's action field when you select a REAPER action by name. The picker UI is what you use; that storage name is not shown in the Native action list.

\newpage

# Plug-in Mixer modes

## SSL Strip Mode

Engage with the Plug-in button (default), via the **Toggle SSL Strip Mode** / **Toggle SSL Strip Mode (with GUI)** actions, or via "SSL Strip Mode follows focused plug-in window" auto-engage on a CS plug-in.

While SSL Strip Mode is on:

- Fader → CS Fader Level (the CS plug-in's own fader parameter, not REAPER's track volume)
- V-Pots → CS controls (per SSL's standard 6 soft-key banks)
- Soft-keys → CS bypass / EQ-In / Filter-In etc.
- The colour-bar Type zone shows the CS variant name (CS2, 4K B, 4K E, 4K G) or the user rename
- UC1 follows the same CS Instance

The `with_gui` variant also opens the CS plug-in's floating GUI alongside, pinning it via the pin-position settings.

`Page ←` / `Page →` step through the 6 SSL-defined Channel Strip soft-key banks. `Plug-in` again (or any other Plug-in Mode toggle) exits.

### Free soft-key slots (CS / BC)

On Layer 1, Q1 (SSL CS) and Q2 (SSL BC), the Top-Soft-Key slots the SSL plug-in leaves **empty** on a page — all of the unused BC banks plus the gaps in the populated banks — are now **user-assignable**. The slots the plug-in actually occupies stay plug-in-driven and show their fixed parameter name; the empty ones open the normal slot editor in Settings → Bindings. Assign any action and it fires when that Top-Soft-Key is pressed in that CS / BC page, with its own LED and label. This lets you fill the dead keys on the SSL banks with your own functions without disturbing the SSL mapping.

### CS / BC soft-key parameter preview

In the Bindings schematic the Top-Soft-Key labels for CS (Q1) / BC (Q2) **follow the page you've selected to edit**, so you can preview every CS / BC bank's parameter names — not just the one engaged live on the surface.

## UF8 Plug-in Mode

Engage with the **Toggle UF8 Plug-in Mode** (or **Toggle UF8 Plug-in Mode (with GUI)**) action. Default binding: long-press the Plug-in button.

While UF8 Plug-in Mode is on:

- All 8 strips become the strips of a single FX-Learn-mapped plug-in — addressable even when fewer than 8 REAPER tracks are visible (strips past the bank-track count source their content from the focused FX instead of going blank).
- Two fader banks (16 strips total for plug-ins that need more than 8 controls) — Bank ← / Bank → flips between A and B.
- 8 soft-keys above each strip drive the FX Learn-mapped TopSoftKey slot per bank.
- The active group of soft-key cells is the bank's "active TopSoftKey ring" — 1 of 8 ringed brightly.
- Unassigned banks act as no-ops (LEDs dim).

The active plug-in is the focused track's first UF8-Mode-mapped instance, or the Instance the cursor is currently on.

Per-V-Pot customisation lives in Settings → FX Learn → (right-click the V-Pot on the schematic). Each `(fader-bank, soft-key bank, strip)` V-Pot carries its own *V-Pot mode* (Value / Toggle), *Polarity* (Unipolar / Bipolar — Bipolar renders the LCD ring centre-out, like SSL Pan), *Push reset* value, *display label*, *strip colour*, and *knob travel* (Min / Max range + response curve + sensitivity — see *Knob travel + curve editor* under FX Learn pane). Fill Sequential propagates every one of these fields to the strips to the right.

## 360° Link

The SSL 360° Link plug-in (a wrapper that mirrors third-party VSTs into the 360° surface) is recognised natively. When the focused track has a 360° Link instance pointing at a learned plug-in, SSL Strip Mode / UF8 Plug-in Mode drive the linked plug-in transparently.

User-renamed 360° Link instances show the rename instead of the generic "Link" / "L-BC" abbreviation.

\newpage

# Favourites (Channel Strip and Bus Compressor)

Swap the active Channel-Strip or Bus-Compressor plug-in on a track for a different one **in place**, carrying the values of every shared control across. Use it to audition the same EQ / dynamics moves through different emulations (SSL 4K E / G / B, API, bx, a JSFX strip…) without re-dialling anything.

## Two domains

**Channel Strip** and **Bus Compressor** favourites are entirely separate: eight slots each, their own set libraries, their own preferences. A track can carry one of each.

Most actions come in three flavours — an explicit CS one, an explicit BC one, and a **Focused Domain** one that follows whichever of the two you are working on at the time. Bind the Focused Domain versions if you want one button that does the right thing on both; bind the explicit ones if you want a button that always means Channel Strip.

## Requirement — the plug-ins must be mapped

Favourite switching works through the **UC1 / SSL-Link control map**: it finds the active Channel Strip on a track, and transfers values, by the shared control each parameter is mapped to (the same control you would map in FX Learn). So every channel strip you switch **between** must be recognised by Rea-Sixty:

- The **built-in maps** (SSL CS 2, 4K B / E / G, BC 2, 360 Link) work out of the box — nothing to set up.
- Any **other** strip (API, bx, a JSFX strip…) needs a **Channel-Strip FX-Learn map** with its controls assigned to the UC1 knobs / buttons (*Settings → FX Learn*, or learn from the Learn-HUD). That map is what tells Rea-Sixty which parameter shares each control.

Without a map: a track whose active plug-in isn't a recognised Channel Strip is **skipped** (treated as having no CS), and value transfer to / from an unmapped favourite can only fall back to matching by parameter **name** — so map your favourites for reliable, value-accurate switching.

## Setting favourites

**Settings → Favourites** is the main place. Each domain has eight numbered slots, and each slot is a searchable dropdown over your mapped plug-ins. One plug-in occupies one slot — putting it in a new slot moves it out of the old one.

Two shortcuts exist for favouriting whatever is in front of you:

- **Settings → FX Learn**, with the plug-in's editor open: a **CS Favourite:** or **BC Favourite:** dropdown assigns it to a slot.
- **Learn-HUD**, from its menu — favourite the live plug-in directly.

Both of those edit the **base** bank, not a named set.

## Sets, and the Base bank

Eight slots is not many once you work across genres, so favourites can be grouped into named **sets** — a full bank of eight, saved under a name. Drums might want one set of strips, vocals another.

In **Settings → Favourites** the set library is split into two columns, **Channel Strip sets** and **Bus Compressor sets**, because the libraries are separate. **New** creates a set, **Dup** copies the one you are looking at, **Del** removes it. A set can be renamed in the field beside those buttons.

Sets are assigned **per track**, under **Assign sets to selected tracks**: select tracks, then pick a **CS set** and a **BC set** for them. Each track can hold one of each, independently.

A track with no set assigned uses the **Base** bank — the fallback everything starts on. Base is normally global and shared by every project. Ticking **This project uses its own Favourites** gives the current project its own base bank instead, seeded from the global one, so a project can carry a specific selection without disturbing your usual setup. The dropdown label tells you which you are editing: `Base (global)` or `Base (this project)`.

> When a track *is* assigned a set, that set is the whole story: an empty slot in it stays empty rather than falling back to Base.

Set libraries and the global base bank are stored with Rea-Sixty and shared by all projects. Per-track assignments, the per-project bank, and the remembered values of each favourite live in the project file.

## Copy values, or own settings

There are two ways a swap can behave, and it is worth understanding the difference because it is the heart of the feature.

**Copy values** carries the live settings from the outgoing plug-in onto the incoming one. You keep the sound you have and change the flavour of the box producing it.

**Own settings** does the opposite: each favourite remembers what *it* was last set to on that channel, and switching restores that. The plug-in comes back as you left it.

The two domains default differently, and deliberately so:

| Domain | Default | Toggle in Settings → Favourites |
|---|---|---|
| Channel Strip | **Copy values** | `Channel Strip: use own settings (vs copy values)` |
| Bus Compressor | **Own settings** | `Bus Compressor: use own settings (vs copy values)` |

Carrying live EQ and dynamics onto the next strip is usually what you want; a bus compressor is more often a thing with its own character you return to. Both are toggleable, from Settings or from a bound action — **CS Favourite Copy/Own**, **BC Favourite Copy/Own**, or the domain-following **Favourite Copy/Own (Focused Domain)**.

The setting governs **Switch** and **Cycle**. *Copy to Favourite N*, below, always copies — that is its purpose.

## Switching, copying, cycling

Three things you can do with a favourite, each available for CS, for BC, and for the focused domain:

- **Switch to CS Favourite 1 … 8** — replace the active plug-in with favourite N. The button lights when the focused plug-in already *is* that favourite.
- **Copy to CS Favourite 1 … 8** — the A/B, described below.
- **CS Favourite Cycle** — step through the favourites. Bound to an encoder it steps per detent; there is also **Encoder Mode → CS Cycle (Favourites)** to give a channel encoder that job outright.

Cycling starts from wherever the track already sits: if its plug-in is favourite 3, the next detent goes to 4. If its plug-in is not a favourite at all, the first forward step lands on the first favourite. Empty slots are skipped, and whether it wraps at the ends follows the global cycle-wrap setting.

A track with no plug-in in that domain is skipped. The swap preserves the old plug-in's **bypass state**, and if its window was open the new one opens the same way.

## Copy to Favourite N — the A/B

*Switch* replaces the plug-in. **Copy** does not: it inserts favourite N **directly below** the current one, carries the settings onto it, and **bypasses the original**.

You end up with both strips on the track, same settings, one active — so you can flip between them by toggling bypass, and keep the original to go back to. Copy always carries values, whatever the copy/own setting says.

## Which sections carry

On the Channel Strip you can restrict what a swap carries, under **Copy sections (copy mode only):** — four checkboxes, all on by default:

| Box | Covers |
|---|---|
| `EQ` | The EQ bands, and the filters, which ride with the EQ on an SSL strip |
| `Dyn` | Compressor and the sidechain listen |
| `Gate` | Gate and expander |
| `Fader` | The strip's fader level |

Anything outside those — input trim, polarity — always carries. The mask applies to Switch, Cycle and Copy alike. **The Bus Compressor has no mask**; it is a single block of eight controls with nothing to divide.

**Favourites remember non-copied sections** (on by default) keeps the parts you did *not* carry. Switch away with `Gate` unticked and back again, and the gate returns as it was rather than as the plug-in's default. This also covers parameters that are unique to a plug-in and have no equivalent anywhere else — the odd character control that only one emulation has — which are remembered per favourite and restored when you come back to it.

## Value transfer

Each control carries its value to the matching control on the new plug-in, matched by the shared SSL-Link control (the same control you would map in FX-Learn) — never by guessing:

- **Numeric values** (gains, frequencies, Q, threshold, ratio, time) transfer by **engineering value**: the search lands the new plug-in on the value whose display matches the source, exactly when achievable. Hz↔kHz and s↔ms scale differences are reconciled automatically; dB / ratio are never rescaled.
- **Same-family swaps** (e.g. SSL → SSL) stay **bit-exact**.
- **Discrete states / buttons** (Bell/Shelf, In/Out, Gate/Expander…) transfer by **meaning**: identical label first, else the active/inactive sense (In/On/Engaged/Expander ↔ Out/Off/Bypass/Gate), else the same state position — so a toggle lands correctly even when the two plug-ins label or order it differently.
- A control the **new** plug-in lacks is remembered, so the value survives a round-trip through a simpler strip and is restored when you cycle back.
- **The SSL routing order and the EXT FUNCS carry too** — the EQ / filter / dynamics arrangement and the free function assignments follow the swap, matched by what each control *is*, so they survive a move between different strips whose internals are numbered differently.
- A filter parked **fully open** lands on the destination's own "out" position where it has one, rather than arriving as a real 20 kHz.

**Copy only mapped parameters** (Settings → Favourites, on by default) decides how far the matching goes. On, only parameters mapped to a control carry. Off, Rea-Sixty also matches unmapped parameters by name — which can be useful for an unmapped strip, but can also drag across things you did not mean, such as an auto-makeup-gain control that happens to be named like a makeup gain.

If a value genuinely can't be represented (a frequency far outside the target's range), the control is left at its default rather than slammed to a rail.

## Several tracks at once

Switch, Copy and Cycle act on **every selected track**, in a single undo step — or on the surface-focused track when nothing is selected.

When the selected tracks carry different sets, **Multi-select: unify sets to focused track** (on by default) re-assigns them all to the focused track's set so they end up consistent. Turn it off and each track keeps its own set, switching within it.

## On the surface

Two factory soft-key banks exist, **CS Favourites** and **BC Favourites**, each filling eight keys with that domain's Switch actions. The key labels show each favourite's plug-in short name — resolved through the focused track's assigned set — falling back to `CS Fav N` when a slot is empty.

Cycling on the UC1 shows the same **previous / current / next** carousel the FX and Instance cycles use, headed with the track name, so you can see what you are stepping into. At the ends of a non-wrapping cycle it still shows, sitting on the current favourite.

\newpage

# Solo / Mute modifier modes

The UF8 strip **Solo / Cut** buttons and the UC1 **Solo / Cut** buttons normally toggle the track's solo or mute. Hold a modifier on your **computer keyboard** while pressing the button to invoke REAPER's other solo / mute modes instead — the same modes you get from right-clicking a track's solo or mute button in REAPER.

The modifier follows REAPER's own convention: on **macOS** the Command key (⌘) is the "Ctrl" role (what REAPER's solo / mute menus show); on **Windows** it is Ctrl. Option/Alt and Shift are the same on both. Windows **AltGr** counts as Alt.

| Hold while pressing Solo | Action |
|---|---|
| *(nothing)* | Solo in place (normal) |
| Alt | Solo, ignoring routing |
| Cmd / Ctrl | Unsolo all tracks |
| Cmd / Ctrl + Alt | Exclusive solo (this track only) |
| Cmd / Ctrl + Shift | Toggle solo defeat |

| Hold while pressing Cut / Mute | Action |
|---|---|
| *(nothing)* | Mute (normal) |
| Alt | Mute all other tracks |
| Cmd / Ctrl | Unmute all tracks |
| Cmd / Ctrl + Alt | Exclusive mute (this track only) |

Without a modifier the buttons behave exactly as before. In routing or Plug-in modes — where Solo / Cut already act on a send or a plug-in parameter — the modifiers do not apply; they only affect the plain track solo / mute.

\newpage

# Send and Receive views

A routing view repurposes the eight UF8 strips so they drive **sends** or **receives** instead of tracks. This is a UF8 feature; the UC1 has no routing view.

## Two layouts

There are two ways to spread routing across the strips, and they are opposites.

**"8 sends of focused track"** puts the *focused track's* send list across the eight strips — strip 1 is its first send, strip 2 its second, and so on. Use this to ride one track's sends.

**"Send N ↦ all tracks"** keeps every strip on its own banked track and shows *that track's* send number N. `Send 3 ↦ all tracks` gives you send slot 3 of each of the eight banked tracks. Use this to ride one bus across the whole bank.

Receives work the same way in both layouts.

## Turning a view on

Routing views are surface functions bound to hardware buttons — they are not in REAPER's Action list, so they cannot be triggered from a keyboard shortcut.

Every routing function is a **toggle**: pressing the active one again turns it off. Each binding also chooses **which control owns the route** — the faders or the V-Pots. In the binding editor this is the **Flip** checkbox.

Factory defaults:

| Control | Function |
|---|---|
| `SEND / PLUGIN 1..8` | **Send N ↦ all tracks** on the faders |
| Shift + `SEND / PLUGIN 1..8` | **Receive N ↦ all tracks** on the faders |
| Long-press `FLIP` | **8 sends of focused track** on the V-Pots |
| Shift + long-press `FLIP` | **8 receives of focused track** on the V-Pots |
| `CHANNEL` | **Home** — clears every routing toggle and returns the strips to track volume and pan |

Because faders and V-Pots hold their routing independently, a send view and a receive view can be live at the same time on different controls — sends on the faders while receives sit on the V-Pots, for instance. Two routing modes cannot share one control. When both are active, the fader's route wins for Solo, Cut and the scribble text; the V-Pot's route drives the colour bar.

The view follows the focused track live.

## What the controls do

| Control | With the route on the **faders** | With the route on the **V-Pots** |
|---|---|---|
| Fader | Send/receive **volume**; with FLIP, **pan** | Track volume, as normal |
| V-Pot | Send/receive **pan** (regardless of the PAN button) | Send/receive **volume**; **pan** while PAN is engaged |
| V-Pot push | Centre the send pan | Reset volume to 0 dB, or centre pan when PAN is engaged |
| Solo | Solo this send (see below) | Solo this send |
| Cut | Mute this send or receive | Mute this send or receive |
| Sel | **Selects the banked track**, not the send | Same |

**Sel is deliberately not routing-aware** — it still selects the track sitting on that strip, so you can change focus without leaving the view.

On the scribble strips the upper line shows the route's name — the destination track for a send, the source track for a receive, abbreviated to fit. The value line shows the send's pan. The colour bar takes the colour of the *other end* of the route, so a strip's colour tells you where the signal is going, not where it came from.

## Paging a long send list

In the **"of focused track"** layouts, `BANK ←` / `BANK →` page the send list by eight instead of scrolling tracks. A track with twenty sends pages through them eight at a time; the window clamps so the last send never scrolls past the right-hand strip. The single-step bank bindings page by one.

In the **"↦ all tracks"** layouts the strips *are* tracks, so `BANK ←` / `BANK →` scroll tracks exactly as they normally do.

The page resets to the start when you press **Home**, when you switch routing mode, and whenever the focused track changes. Nothing on the surface indicates which page you are on, so if a long send list looks wrong, press Home and start again.

## Hardware outputs

In a **Send** view, the track's hardware outputs share the slot list with its track sends, in the same order REAPER's mixer send list shows them. Each has its own fader and pan, and is named after the output channel your audio device reports — for example `Analog 1 / Analog 2`. Long names are abbreviated to fit the scribble strip.

Hardware outputs have no far-end track, so those strips do not take a routed colour.

Receives have no hardware-output equivalent; a Receive view lists receives only.

## Empty slots

REAPER lets a send list contain gaps. An empty slot shows as a **completely blank strip** — no name, no value, colour bar off, V-Pot ring off, and the motor fader parked at the bottom so a gap is obvious at a glance. Buttons and faders on a blank strip do nothing.

Strips past the end of a short list are blank in the same way: a track with four sends fills strips 1–4 and blanks 5–8.

## Solo and Cut on a send

**Cut** toggles the send's or receive's own mute.

**Solo** is an exclusive un-mute: it un-mutes the send you pressed and mutes every other one in the list. In a Send view that includes the hardware outputs; in a Receive view it stays within the receives. Pressing it again un-mutes everything.

> **This discards whatever mute pattern you had before** — the same as the solo button on a console. If you had three sends deliberately muted, soloing and un-soloing leaves all of them open.

While any routing view is active the **Solo LEDs stay dark**, so there is no lit button telling you a send is soloed. The visible cue is the other strips' Cut LEDs coming on. Leaving the view restores the LEDs to the tracks' real solo states.

## Writing automation

Moving a **fader** in a routing view writes the send's volume — and its pan under FLIP — through the same path REAPER uses when you drag the control with the mouse. Whether the move is *recorded* is therefore REAPER's decision and follows the track's automation mode, exactly as it would on screen. Releasing the fader ends the edit.

Two limits are worth knowing:

- **The V-Pot writes pan automation, but V-Pot volume changes the level without recording.** If you want an automated send-level move, use the fader.
- **A send whose level is already automated reads its written value only after you have touched its fader once** in the session. Before that the strip shows and parks at the underlying trim level. Touch the fader and it corrects itself.

If you add or delete a send on a track that already has send automation, a fader move may change the level without recording it. Leaving the routing view and re-entering it re-establishes the link.

\newpage

# REC + RME (TotalReaper) integration

Requires the **TotalReaper** extension (separate ReaPack package) and an RME interface supporting TotalMix FX 2.1 Global OSC.

Master switch: Settings → Modes → REC → "Enable RME / TotalReaper integration".

When on AND REC / REC+MON selection mode is active:

- V-Pot rotation → preamp gain ±1 dB (configurable per `V-Pot rotation → Preamp gain ±1 dB` toggle)
- V-Pot rotation + Shift → input channel reassignment (configurable per `V-Pot rotation + Shift → Change input channel` toggle)
- V-Pot push → action of choice (`None` / 48V / Pad / Phase invert / AutoLevel)
- Cut button → action of choice (same enum)
- Solo button → action of choice
- Polarity (if mapped) → action of choice

Strip colour bar shows the input channel name ("Mic 1", "Line 3") instead of the CS variant label.

Hardware inputs only — MIDI / multichannel inputs leave the original colour-bar label intact.

The TotalReaper action names are looked up via `NamedCommandLookup`; if TotalReaper isn't installed the integration silently no-ops.

\newpage

# DynaMount mode

DynaMount makes robotic microphone stands. In DynaMount mode the UF8 strips drive up to eight of them over your network — move a mic from the control room, with the fader.

## Setting the mounts up

**Settings → Modes → Dynamount.** Each of the eight rows is one stand:

| Column | What it is |
|---|---|
| **On** | Include this mount. Only enabled mounts take a strip. |
| **Name** | Shown on the scribble strip |
| **IP address** | The mount's address on your network — you type it; there is no discovery |
| **Colour** | The strip's colour bar |
| **Detect** | Check whether that address answers |
| **Calibrate** | The **Home** button (below) |
| **Status** | `Gen1`, `Gen2`, `offline`, or `--` when the row is off |

**Fill from left** / **Fill from right** decides which end of the surface the mounts occupy.

## Engaging the mode

Bind **Selection Mode → DynaMount** to a button; it toggles like every other Selection Mode. There is no switch in Settings — the Dynamount tab configures the stands, it does not turn the mode on.

With no mounts enabled the mode does nothing at all, and every strip stays a track.

Enabled mounts pin to one end of the surface and carry no track. The other strips remain ordinary track strips, and the tracks that would have sat under the mounts shift onto them rather than disappearing.

## Driving a mount

| Control | Axis |
|---|---|
| Fader | **X** — left and right |
| Fader with **FLIP** | **Y** — distance, fader up = nearest |
| V-Pot | **R** — rotation, 0–180° |

The fader **sends when you let go**, not while you move it: the readout follows your hand, the stand moves once on release. The V-Pot behaves similarly — the number changes per detent, but the mount only turns about a second after you stop.

The scribble strip shows the mount's name (or `MOUNT` if you have not named it), `DYNA` in the type zone, and the live position as `X`, `Y` and `R`. The digit is the mount's row number in Settings, not the strip number. A mount that is not answering has **OFF** appended to its value line.

Solo, Cut and Sel have no function on a mount strip and their LEDs stay dark.

## Home

**Home** — the button under *Calibrate* — drives the mount to the reference pose **X50 Y0 R90**: centred, nearest, straight ahead. It also resets Rea-Sixty's idea of where the stand is.

This matters because the protocol is one-way: the stands report no position, so Rea-Sixty knows only what it last told them. Positions are remembered globally and restored on the next launch without re-driving the motors, on the assumption that nothing moved in between. If you move a stand with the DynaMount app, by hand, or after a power cut, the two disagree — press Home and they match again.

Home is a normal move command, so the stand travels to that pose; it is not a hardware homing or self-calibration routine.

## Connection

Rea-Sixty talks to the stands over plain HTTP on your local network, at the addresses you enter. Motor speed is fixed.

**Detect** is a reachability check for an address you have already typed, not a search of the network. On an address with nothing at the other end it can take around six seconds to give up and report `offline`.

> Once a mount has been found offline, Rea-Sixty stops trying it. Faders and V-Pots keep moving the numbers on screen, but the stand will not follow, and the value line shows **OFF**. Press **Detect** again after fixing the network — or edit the address, or restart REAPER. It will not recover on its own.

Every enabled mount is probed once when REAPER starts, so Status is filled in before you open Settings.

> **Only first-generation stands can be driven.** A newer stand is detected and reports `Gen2` in Status, but Rea-Sixty cannot move it — motion is implemented for the Gen1 HTTP protocol only.

\newpage

# Stream Deck and Companion

Rea-Sixty carries a small server — the **bridge** — that external control apps talk to. Two clients use it: an official **Stream Deck plugin**, and a community **Bitfocus Companion module**. Both let you fire any Rea-Sixty action from a button and read live meters back.

Neither is part of the ReaPack package.

## The Stream Deck plugin

Download `com.reasixty.companion.streamDeckPlugin` from the *Assets* of any [release](https://github.com/acklin83/Rea-Sixty/releases/latest) and **double-click it** — the Stream Deck app installs it. There is nothing else to install and nothing to configure; no Node, no separate server. You need the Stream Deck app **6.5 or newer**.

The tiles then appear in the Stream Deck action list under **Rea-Sixty Companion**. Drag one onto a key.

Nine tile types:

| Tile | What it fires |
|---|---|
| **Favourite** | Switch and cycle CS / BC favourites |
| **Layer** | Select binding layer 1 / 2 / 3 |
| **Hardware Mode** | Surface-wide toggles — Flip, Home, Settings, mirrors, overlays, learn |
| **Navigation** | Bank and page navigation, zoom |
| **Plug-in** | FX windows, chain, close-all, Quick-Learn, bypass, offline |
| **Selection Set** | Recall selection sets |
| **Surface Target** | What the surface drives — Selection and Encoder modes, sends, Parameter Groups, soft-key banks, SSL |
| **Any Rea-Sixty Action** | Any built-in at all, or a raw REAPER command |
| **Meter** | A live readout rather than a button (below) |

Each tile's action list is **pulled live from the running extension**, so it always matches your installed version rather than a list baked into the plugin. The consequence is that the list is empty until REAPER is up — the property panel then reads *"Rea-Sixty not connected — start REAPER with the extension."*

Keys ship blank: no image, no title. Set a **Title**, or tick **Show track** to mirror the selected track's name.

**The Meter tile** shows five sources — `Peak`, `Gain Reduction`, `Gain Reduction — Bus Comp`, `Peak + GR`, and `Peak + GR (Bus Comp)`. Gain Reduction reads the first plug-in on the track that reports it, so any compressor works; the Bus Comp variants target the mapped SSL Bus Compressor.

Point it at the **Selected track**, the **Master track**, a fixed **Track number…** or a **Track name…**. The rest of the options are presentation: **Track name** and **Track colour** to show them, **Name position** (`Bottom` or `Top` — top reads better on an angled deck), **Wrap name** for a second line, and **Font size** (`Small` / `Normal` / `Large` / `Extra large`).

**Smart abbrev.** is on by default and shortens long track names intelligently. Note that it *overrides* the global Track-name mode rather than following it: with the box ticked you get smart abbreviation even if the surface itself is set to plain truncation, and unticking it shows the full, unshortened name.

A meter tile can also fire an action when pressed — see **On press (optional)**.

## The bridge

The bridge listens on **port 49900**, on **loopback only** — so out of the box, only the machine running REAPER can reach it.

To let Companion on another machine connect, open it to the network by running this once in REAPER's ReaScript console and restarting REAPER:

```
reaper.SetExtState("rea_sixty", "sd_bridge_bind", "lan", true)
```

The port can be moved the same way with `sd_bridge_port`. Both settings are read once when the extension loads, so a restart is required either way.

> **The bridge has no authentication.** In LAN mode anyone who can reach the port can drive REAPER. Only open it on a network you trust.

Two limitations of the Stream Deck plugin are worth knowing before you plan a setup:

- **It always connects to 127.0.0.1 on port 49900.** There is no host or port setting. So the Stream Deck plugin works on the machine running REAPER only — the LAN option above is useful for Companion, not for it.
- **Moving the port with `sd_bridge_port` therefore disconnects the Stream Deck plugin** permanently. Only change the port if Companion is your only client, or if something else on the machine already occupies 49900.

If the port cannot be claimed at all, the bridge fails quietly and everything else keeps working — the only evidence is a `StreamDeck bridge FAILED to bind` line in the log.

## The Companion module

> **Community module, provided as-is.** It is not officially supported, and it is not a one-to-one port of the Stream Deck plugin: Companion styles buttons through feedbacks rather than pre-made tiles, and some conveniences — metering a track by name, for one — are not implemented. The source is in the repository for anyone who wants to extend it.

It needs **Bitfocus Companion 4.x** and is installed as a developer module: run `npm install` inside `companion/`, then point Companion's **Developer modules path** at the directory containing that folder and restart the launcher. Rea-Sixty then appears under Connections → Add connection.

> **Do not point the developer path at a symlink.** Companion runs developer modules under Node's permission model, which refuses to read through one. The connection dies at startup with nothing but `Error: Restart forced` in the log — the real reason only appears at debug log level. Use a real directory.

The connection is configured with a **REAPER host** and **Bridge port**, plus which tracks to meter: the selected track, the master, and a comma-separated list of track numbers. Metering is off until you enable it there, and each metered track is polled about fifteen times a second — so list only what you need.

It provides an action for any Rea-Sixty built-in (the list again pulled live), plus REAPER commands by numeric ID or by action string. Feedbacks cover the active binding layer, flip state, a meter-over-threshold test with a configurable dB threshold, and a graphical meter bar. Variables expose the connection state, selected track name and number, active layer, flip, and per-track peak and gain-reduction values. Presets ship grouped by category, with a Meters group and a Status group.

## If the keys go dead

Both clients reconnect by themselves roughly every one and a half seconds, so starting REAPER after the Stream Deck app is fine — the keys come to life on their own, and nothing needs reinstalling.

While the bridge is unreachable, meter keys show a dim dash and **no REAPER** rather than freezing on their last reading, and any track name shown on a key clears. That display *is* the diagnosis: REAPER is closed, the extension is not loaded, or something else has taken the port.

\newpage

# Master track

The REAPER Master bus isn't a normal track — it's excluded from banking and has no track number. Rea-Sixty surfaces it in four places. A Channel Strip on the Master is treated exactly like one on any track (it drops into SSL Strip Mode automatically), and its scribble reads **MASTER**.

## On the UC1

- **Bus Compressor on the Master** — a BC plug-in inserted on the Master always shows in the UC1's Bus Comp context, sitting at the **far left** of the BC carousel (the Master is "track 0"). It also becomes the default BC anchor when no other track carries a BC. This is always on; it does not depend on the *Show Master as Track 0* toggle.
- **Show Master as Track 0 on UC1** (Settings → Behaviour → Master track) — with this on, rotating the CHANNEL encoder left past track 1 lands on the Master, so the channel-strip section drives the Master bus. Rotate right to return to track 1. UC1-only.
- **Out-Gain → Master fader** — when the UC1 is focused on the Master and there is **no** Channel Strip on it, the **Out-Gain** knob automatically drives REAPER's Master fader (readout reads *Mst Vol*). No toggle needed — if there's no CS Fader-Level parameter to ride, the knob falls back to master volume. (With a CS on the Master, Out-Gain rides the CS Fader Level as usual; the global *UC1 Out-Gain → REAPER fader* toggle still forces track volume on any track.)

## On the UF8

Two bindable actions pin the Master onto a physical UF8 strip — fader, V-Pot (pan), Solo/Cut/Sel LEDs, colour bar and the **MASTER** scribble all follow the Master bus:

- **Pin Master to UF8 Strip 1**
- **Pin Master to UF8 Strip 8**

Each is a toggle (press again to un-pin; pinning the other strip switches directly). Both are bindable from the Bindings picker (category **Master**) **and** exposed as REAPER actions *Rea-Sixty: Pin Master to UF8 Strip 1 / 8* for the keyboard / toolbar — handy on the UC1, which has no spare buttons.

The **Pinned Master** combo (Settings → Behaviour → Master track) chooses the layout:

- **Replace strip** (default) — the pinned strip becomes the Master and hides the track that was banked there.
- **Shift banking** — the regular tracks bank over the remaining 7 strips so none is hidden; the bank step and clamp drop to 7 while a pin is live.

The pin is a **normal-mode** feature: it steps aside automatically when a Selection Set, AUTO selection mode, a Send/Receive routing mode, or UF8 Plug-in Mode is active (all of which repurpose the strips), and resumes when you leave them. The Master wins over a TCP-pinned track on the same strip. The pin is not persisted across REAPER restarts.

\newpage

# Selection Sets

## Active set as a track filter

When a slot is active, the visible track list is filtered to the slot's tracks. Filter is ANDed with Folder Mode / Show Only Selected so they compose naturally.

## Group slots

A Group slot tracks REAPER's track-group membership in real time. Add a track to group N in REAPER → it appears in the surface; remove it → it disappears, without a re-recall.

## Auto-mode interaction

`Selection-Set auto-mode` (Settings → Modes → AUTO) is a single global value: -1 (off) or REAPER's automation mode index (0..5). When a selset is active in AUTO sel mode + the dropdown is non-off, recalling the slot arms its tracks to that mode. Leaving the slot OR leaving AUTO sel mode reverts those tracks to Trim/Read.

## Persistence

Project-scoped Snapshot + Group slots are saved into the project's RPP chunk via REAPER's project-config hook (lines `SELSET_<N>_DATA "..."`). Global-scoped slots ride REAPER's global ExtState (`reaper-extstate.ini`). The per-slot global/project flag itself, plus the `Selection-Set auto-mode` value, also live in global ExtState.

## Focus Set

A ninth, ad-hoc set living alongside the 8 numbered slots — no Settings UI, no slot name, just actions you bind to hardware. Unlike the slot Selsets (which *filter* the visible list), the Focus Set is a **pin source**: its members stick to the leftmost strips and the rest of the tracks keep banking past them — nothing is hidden. Useful for keeping a working set ("the 6 drum mics + 2 talkbacks") permanently under your hands while you still scroll the rest of the session.

- **Focus Set: add selected** — add every REAPER-selected track to the set.
- **Focus Set: remove selected** — remove every REAPER-selected track.
- **Focus Set: pin (toggle)** — toggle pinning on / off. Coexists with the 1..8 slot recall — a slot filters the list, the Focus Set pins members within it.
- **Focus Set: clear** — empty the set in one shot.
- **Focus Set: toggle selected** — flip membership of the selected tracks (in → out, out → in).
- **Focus Set: set from selection** — replace the set contents with the current selection.
- **Focus Set: pin focused track** — add the focused track and turn pinning on in one step.
- **Encoder: scroll Focus Set members** (encoder) — step REAPER track selection through the set in project order.

The last four also have REAPER-action equivalents (`REASIXTY_FOCUS_CLEAR` / `_TOGGLE_SELECTED` / `_SET_FROM_SELECTION` / `_PIN_FOCUSED`) for a keyboard shortcut or toolbar button.

**Collisions.** Focus pins, REAPER TCP pins (`B_TCPPIN`) and a Master pin compose left-to-right: `[Master][pin head: TCP ∪ Focus][banked rest]`. Focus pins work in both TCP and MCP surface-mirror modes; TCP pins only in TCP mode, and only while REAPER is honouring them (the *Override/unpin* and *Show/hide all pinned tracks in TCP* actions stand the TCP pins down — Focus pins are unaffected).

**Auto-mode.** A dedicated *Focus-Set Auto-Mode* dropdown (Settings → Modes → AUTO) arms members to a chosen automation mode when pinned in AUTO sel mode — decoupled from the slot Selset auto-mode. Members are exempt from the *Auto-hide Trim/Read* filter, so a pinned member stays visible even when unarmed.

Persists per-project (saved into the RPP via the same project-config hook the numbered slots use).

\newpage

# Parameter Groups

Eight slots + a per-slot member list of REAPER tracks. The Active group's members all receive the same parameter edit when you twist a UF8 V-Pot, a UC1 knob, or move a fader on any one of them.

Slot management UI in Settings → Parameter Groups. Each slot:

- Name
- Member tracks (Add Selection, Remove, Clear)

Active group switch via the per-slot Active radio.

Temp-group mode: when `Multi-Select acts as Temp Group` is on, the active group is derived live from the current REAPER multi-track selection.

Which edits travel — and onto which tracks:

- **SSL Channel Strip / Bus Comp (and learned FX):** the edit follows the surface control, so it lands on the matching control even when members run a *different variant* of the plug-in (e.g. one track on Channel Strip 2, another on 4K E).
- **Any other plug-in — no mapping or FX-Learn required:** the edit copies to members that host the **exact same plug-in**. Tweak a knob (on the hardware *or* with the mouse on the plug-in GUI) and every member track with an identical plug-in follows along. Members without that plug-in are silently skipped.

This deliberately does **not** copy across *different* plug-ins: the same internal value rarely means the same thing on another plug-in, so only identical plug-ins mirror.

Storage: each track's member-of-which-slots state is a bitmask in P_EXT (`P_EXT:rea_sixty:param_groups`). Slot metadata is in a project-scoped JSON sidecar.

\newpage

# Mapping Exchange

A shared library of plug-in maps, reachable from *Settings → Exchange*. If someone
has already taught Rea-Sixty a plug-in you own, you can pull their map instead of
learning it again.

## Browsing

The landing table lists every plug-in the exchange knows:

| Column | Meaning |
|---|---|
| Plug-in | The plug-in name |
| Vendor | Its maker |
| Maps | How many maps exist for it |
| Surfaces | Which surfaces those maps target (`uc1`, `uf8`, `uf1`) |
| Coverage | How much of the surface the best map fills |

Pick a plug-in and you get its maps, one row each, with the author, the surface,
the coverage and a **Works** column — whether other people report the map working.
Open a map and you see exactly what it does before you take it: a Control /
Parameter table, an *Also mapped* list for parameters with no fixed control, the
**Modifier layers** if the author used any, and the **EXT FUNCS** list for the
UC1's hidden menu.

## Taking a map

If you already have a map for that plug-in, Rea-Sixty asks rather than
overwriting: **Replace mine** or **Keep mine**. Nothing is replaced silently.

## Publishing

*Server settings* holds the server address and a **device token**. The token is
what lets you upload; getting one is a browser round-trip you start from that
pane. Without a token you can browse and download — publishing is the only part
that needs it.

\newpage

# Bindings

The Bindings tab renders the UF8 + UC1 hardware as schematics. Every button / knob / fader is editable.

## Per-binding fields

- **Action type:** Native / REAPER Action / MIDI Command
- **Action name** (Native + REAPER Action) / MIDI message (MIDI Command)
- **Modifier:** None / Shift / Cmd / Ctrl / combinations
- **Trigger:** Press / Hold / Long-press
- **LED override:** colour + brightness

## Modifier system

The three modifier-key actions (**Modifier: Shift / Fine (double-click latches)**, **Modifier: Cmd**, **Modifier: Ctrl**) shift every other button's binding to that modifier slot while held. Modifier keys themselves are bindable to any physical button.

Double-clicking `Shift` (= the SSL `FINE` key) latches it on. Press once more to unlatch.

Modifier slots are per-modifier-combination, so a button can have up to 8 separate bindings.

## Long-press

A long-press binding fires after the long-press threshold elapses. The short-press binding fires on release if the threshold wasn't reached. A button can have both bindings.

## Toggle / Hold semantics

- **Press**: fires once per press edge
- **Hold**: keeps firing while held (for repeats like bank-shift)
- **Long-press**: fires once when the long-press threshold elapses

## LED override

Every bound button can override its LED colour and brightness, independent of the action's natural state.

## REAPER Action picker

Pick any action from REAPER's Action List by name or command ID. Supports filtering and ReaScript loading (Load ReaScript button drops a `.lua` file into the catalog).

Named commands (_-prefixed) resolve via REAPER's `NamedCommandLookup` so ReaScripts stay bound across re-numberings.

## MIDI Command bindings

Emit any MIDI message: Note On/Off, CC, Program Change, Pitch Bend, NRPN. Channel / Note / Value editable. The message goes to a virtual MIDI port that the extension opens — route it in REAPER's MIDI plumbing as needed.

\newpage

# Operational modes (track filters)

## Folder Mode

Toggle: **Toggle Folder Mode (parents only)** action. When on, only top-level (depth-0) tracks are visible on the surface. Folder children appear only when "spilled" — long-press a folder parent's `SEL` button to toggle that parent's spill.

**Nested folders (ancestor-chain spill).** Long-pressing a folder at any depth spills *that* folder; the ancestor chain stays in the spill set as well, so the intermediate hierarchy remains visible. Collapsing an ancestor (re-long-pressing it) hides its subtree but **keeps the descendants' spill state in memory**, so re-spilling that ancestor restores the previous drill-down without having to long-press each level again. Toggling Folder Mode off (or re-pressing the **Toggle Folder Mode (parents only)** action) clears the spill set entirely.

## Show Only Selected

Toggle: **Toggle Show Only Selected** action. When on, only currently-selected tracks appear on the surface. Live filter — changing REAPER selection updates the surface within one timer tick.

## Selection-Set filter

ANDs with Folder Mode / Show Only Selected. When a slot selset is active, only its tracks appear (further filtered by Folder Mode if also on). The Focus Set is **not** a filter — it is a pin source that coexists with the slot filter (see *Selection Sets → Focus Set*); it never hides tracks.

## Visibility-follow filter

Settings → Behaviour → Tracks → "Surface mirrors: TCP / MCP" (default TCP) — the surface only shows tracks that are visible in the mirrored view, so anything REAPER hides in the TCP (or, in MCP mode, in the Mixer) drops off the surface. In TCP mode that also covers the children of a fully-collapsed folder whenever REAPER's own "Hide children of collapsed folders" preference is on. ANDs with all other filters.

## Auto-hide Trim/Read

Settings → Modes → AUTO → "Hide Trim/Read tracks while in AUTO mode" — within AUTO selection mode only, hides tracks whose automation mode is Off/Trim (0) or Read (1).

\newpage

# Troubleshooting

**Try this first.** If the surface was working and has stopped — dead faders, no scribble text, buttons ignored — run the REAPER action *Rea-Sixty: Restart Rea-Sixty (re-open devices)*. It closes and re-opens the devices without the Preferences round-trip and clears most wedged states. Give it a keyboard shortcut; a hardware key is no use when the hardware is what stopped responding.

It re-opens the devices, not the extension, so it will not pick up a newly installed build — that still needs REAPER restarted.

## Rea-Sixty does not appear in Preferences → Control/OSC/Web → Add

The dylib / dll / so didn't load. Check:

- **macOS Console** for codesign / dyld errors.
- **Windows Event Viewer → Application** for module-load errors.
- **Linux:** start REAPER from a terminal and watch stderr.

Make sure the runtime libraries are in the same directory as `reaper_rea-sixty.{dylib,dll,so}` — for Windows this means `%APPDATA%\REAPER\UserPlugins\`, where the delay-load resolves them at first call.

## Surface does not respond / "SSL360Core owns the device"

SSL 360° is running and has claimed the UF8/UC1 vendor interface exclusively. Quit SSL 360° and restart REAPER.

## Disconnect after sleep / wake or sustained idle (macOS)

Known issue: both UC1 and UF8 IN endpoints can fail within ~3 ms of each other on a sustained host-side USB stack condition. `libusb_reset_device` does not escape it. Physical replug (one or both devices) recovers. Diagnostic logs are `rea_sixty_uc1_stale.log` + `rea_sixty_uf8_stale.log` in the system temporary folder (see *Diagnostics*).

## Track-colour wrong

- Confirm Settings → Appearance → Surface display → "SEL LED follows REAPER track colour" is on.
- The colour-bar quantiser is HSV polar — subtle RGB differences in REAPER's colour picker can land on neighbouring palette slots. Pick a more saturated colour if the nearest palette slot looks wrong.

## GR meter reads wrong on a third-party compressor

- Enable Settings → Devices → Metering → "GR meter source: Show any GR Data".
- Confirm the compressor implements the PreSonus VST3 host-side `GainReduction_dB` config-parm. Plug-ins that don't expose this will not drive the meter regardless of settings.

## Linux port resets / `xhci_hcd disabled by hub (EMI?)`

Plug UF8 and UC1 into separate PC USB ports. Daisy-chaining UC1 through UF8's downstream port triggers `xhci_hcd` port cycling on Linux 6.17.

## Settings window doesn't appear

ReaImGui isn't installed. Install it via ReaPack. Hardware control still works without it.

## Diagnostics

Log files live in the system temporary folder — `/tmp` on macOS and Linux, `%TEMP%` on Windows. **Settings → About → Logs** shows the real path and has a button to open the folder.

- `rea_sixty_uc1_stale.log` — UC1 device-handle diagnostics
- `rea_sixty_uf8_stale.log` — UF8 device-handle diagnostics
- macOS Console / Windows Event Viewer / Linux journal for in-process errors

Note that **Console output** in Settings → About → Logs is off by default, so REAPER's Console stays quiet. The log files are written either way; turn the checkbox on if you want messages in the Console as well.

\newpage

# Vendor relationship

Not affiliated with Solid State Logic. SSL ACP Support replied to the project author on 2026-05-18 confirming "no objections to the public, open source project" but declined to share protocol documentation.

Protocol stays self-decoded; documented in `docs/protocol-notes.md` and adjacent capture notes in the repo.

No SSL binaries, firmware, or trademarks are redistributed. "SSL", "UF8", "UC1", "360°" are property of Solid State Logic Ltd.

