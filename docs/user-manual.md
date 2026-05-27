---
title: Rea-Sixty User Manual
subtitle: Native REAPER ↔ SSL UF8 / UC1 driver
author: |
  Frank Acklin
  \
  [www.stoersender-studio.ch](https://www.stoersender-studio.ch)
date: v0.1.10
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

- REAPER on macOS (Apple Silicon), Windows (x64), or Linux (x86_64). Tested against REAPER 6 and 7 through 7.71.
- An SSL UF8 plugged in over USB-C. UC1 is supported optionally; UF8-only or UC1-only rigs are fine.
- **ReaImGui** (install via ReaPack from Extensions → ReaPack → Browse packages → ReaImGui). Without ReaImGui the Settings window stays empty, but hardware control still works.
- **SSL 360° must not be running.** It claims the UF8/UC1 vendor interface exclusively. If it is running when REAPER starts, the surface will not appear and REAPER's Console shows an error.

Runtime dependencies (`libusb`, `hidapi`) ship inside the platform archives; no separate install needed.

## Versioning

This manual documents Rea-Sixty v0.1.10. Earlier manuals (anything dated before 2026-05-27) are superseded.

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

- **Mac:** `rea-sixty-mac-v0.1.7.zip` — three Apple-notarised dylibs. Unzip into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.1.7.zip` — three DLLs. Unzip into `%APPDATA%\REAPER\UserPlugins\`.
- **Linux:** `rea-sixty-linux-v0.1.7.tar.gz` — `.so` + udev rule + INSTALL.txt. Follow INSTALL.txt.

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

- **FX** = any audio effect on a REAPER track (everything `TrackFX_GetCount` sees).
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

Each strip's V-Pot rotation walks every FX on the strip's track. Push opens the active FX's floating window. The strip's colour bar shows the FX-cursor's current FX name. Internal name `SelectionMode::Instance` — kept for binding-file compatibility despite walking all FX.

## Instance Cycle (V-Pot Sel-Mode)

Each strip's V-Pot rotation walks only Instances (CS / BC / UF8-Mode-learned) on the strip's track. Push opens the active Instance's floating GUI. No-op when the strip's track has fewer than 1 Instance.

When the cycle lands on a learned Instance the per-domain Instance index updates so SSL Strip Mode + UF8 Plug-in Mode + the UC1 sections follow.

\newpage

# Channel Encoder modes

The large notched CHANNEL encoder (right of the strips, pushable, surrounded by the cursor pad) runs one of nine modes. Switch with the corresponding `encoder_*` builtin. The current mode persists across REAPER restarts.

| Mode | Rotation acts on | Notes |
|---|---|---|
| Channel Select | Move REAPER track selection ± | Default mode. Strips re-bank to keep selection visible. |
| Nudge | Playhead nudge | Step size from REAPER's nudge setting |
| Mousewheel | Synthesised scroll-wheel under the mouse cursor | Use to scroll plug-in windows or Project Browser |
| Markers | Step prev / next marker | Stops playback while seeking |
| Bank by 1ch | Shift the surface 1 strip left/right | Sub-bank precision |
| Last Touched Param | Step the last-touched REAPER param ± | Fine increments |
| Instance (Instance Cycle) | Walk Instances on the focused track | Same dispatch as the V-Pot Sel-Mode Instance Cycle, focused-track scope |
| FX Cycle | Walk every FX on the focused track | Focused-track scope |
| Selset Cycle | Step through populated Selection Set slots (off → 1 → 2 … → off) | Skips empty slots |

`Shift` + rotation re-banks ±1 strip in every mode (alias for Bank by 1ch).

The encoder also drives **SEL-Mode cycle** when **Settings → Modes → FX / Cycle → "UF8 Channel Encoder"** is ticked AND a cycle-kind Selection Mode is engaged. In that override the encoder dispatches via `reasixty_dispatchSelModeCycle` instead of its normal mode.

\newpage

# Nav Mode (Markers + Regions)

Nav Mode is a surface overlay — separate from the Selection Modes — that turns the UF8 strips into a live marker / region jump panel. It can be active alongside any Selection Mode; toggling it does not change `g_selectionMode`.

## Engaging Nav Mode

Three bindable builtins:

- `marker_overlay_toggle` — toggle on / off, opens the default view (set in Settings → Modes → NAV).
- `marker_overlay_markers_only_toggle` — toggle on / off, locks the view to **MarkersAll** (no drill into regions).
- `marker_overlay_regions_only_toggle` — toggle on / off, locks the view to **Regions** (region presses jump only, no drill).

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
| Top soft-key | SSL Soft-Key for this strip in the current PAGE bank | Default builtin `ssl_softkey(strip)`. Rebindable. |
| Colour TFT (scribble strip) | Upper zone = track name / mode-dependent. Lower zone = parameter readout. Track-colour bar at the bottom. | Hijacked by Plug-in Modes for parameter / FX names. |
| V-Pot rotation | Pan | Re-maps per Selection Mode (REC + RME → preamp gain; AUTO → automation indicator; FX Cycle / Instance Cycle → walk FX/Instances). |
| V-Pot push | Centre Pan | In FX Cycle / Instance Cycle Sel-Modes → open the active FX's GUI. |
| `SOLO` | Solo | LED colour follows REAPER track colour when *Settings → Device → Display behaviour → "SEL LED follows REAPER track colour"* is on. |
| `CUT` | Mute |  |
| `SEL` | Selection-Mode dependent | NORM = exclusive select; REC = arm; REC+MON = arm + monitor; AUTO = cycle automation mode. Long-press on a folder parent toggles spill. |
| Capacitive touch (fader) | Drives REAPER's "touch" automation | Alt/Option held during touch → snap back on release (Settings → Device → Keyboard Options). |
| 100 mm motorised fader | Track volume | Full 16-bit pitch-bend protocol. |

## Above the strips: 8 Top Soft-Keys + bank selectors

A row of 8 buttons above the V-Pots = **Top Soft-Keys 1..8**. By default each one focuses the SSL plug-in parameter at its strip position within the current PAGE bank (SSL 360° factory behaviour). Rebindable per-button.

Left of the soft-keys: 6 small bank-selector buttons — **V-POT** + **1 / 2 / 3 / 4 / 5**. Each picks one of the 6 SSL CS soft-key banks (or 2 BC banks while in Bus-Comp). Default builtin `softkey_bank_select(N)`.

## Right of the strips: CHANNEL encoder + cursor pad

A single block:

- **Large notched CHANNEL encoder** (push-button rotary). Rotation drives the active Channel Encoder mode (see chapter Channel Encoder modes). Push = mode-specific.
- **Cursor pad** — 5 buttons (4 arrows + central circle) **surrounding the encoder**.
  - Default behaviour: **zoom** via the `zoom_up` / `zoom_down` / `zoom_left` / `zoom_right` / `zoom_center` builtins (REAPER actions 40112 / 40111 / 1011 / 1012 / 40295).
  - SSL's reference UG also documents a "Cursor-Transport" mode (press-and-hold CHANNEL encoder to enter; ↓=Stop ↑=Play ←=Rew →=FF centre=Rec). Rea-Sixty leaves these as the standard zoom bindings — rebind them to transport actions via Settings → Bindings if you want SSL's behaviour.
- **NAV / NUDGE / FOCUS** mode buttons (around the encoder; ButtonId entries `Nav`, `Nudge`, `EncFocus`). Default builtins switch the encoder to that mode.

Above the encoder block: **Q1 / Q2 / Q3** ("Quick" user keys). Default bindings — Q1 = CS, Q2 = BC, Q3 = I/O meter (matches SSL's locked Plug-in Mixer assignment).

## Right of the encoder column: NORM / REC / AUTO

Three buttons (Selection Mode block). Default unbound — assign the `selection_mode_norm` / `selection_mode_rec` / `selection_mode_auto` builtins (or any other Selection Mode builtin, including `selection_mode_instance` / `selection_mode_instance_cycle`) via Settings → Bindings.

## Above NORM/REC/AUTO: AUTOMATION row

Six buttons — **Read / Write / Touch / Latch / Trim / Off**. Default builtins `auto_read`, `auto_write`, `auto_touch`, `auto_latch`, `auto_trim`, `auto_off` (set the automation mode of the focused track).

## Below the strips: PLUGIN / CHANNEL / mode row

| Button | Default |
|---|---|
| `PLUGIN` | Toggles SSL Strip Mode (`ssl_strip_mode_toggle`). With Shift held: `ssl_strip_mode_toggle_with_gui`. |
| `CHANNEL` | `home` — clear send / receive routing toggles so V-Pots / faders return to track volume + pan. |
| `BANK ←` / `BANK →` | Scroll ±8 strips. In UF8 Plug-in Mode → flip between fader-banks A / B (for 16-strip plug-ins). |
| `PAGE ←` / `PAGE →` | Step the SSL Soft-Key PAGE bank prev / next (6 CS banks + 2 BC banks). |
| `SEND / PLUGIN 1..8` | 8 buttons. Default `send_this(N)` / `recv_this(N)` — toggle the matching send / receive view. |

## FLIP / PAN / FINE

Three buttons in their own cluster:

| Key | Default action |
|---|---|
| `FLIP` | `flip` — swap fader and V-Pot values for the active mode. |
| `PAN` | `pan_force` — force V-Pots to Pan regardless of the active Selection Mode. Escape hatch from cycle / REC / AUTO modes. |
| `FINE` | `mod_shift` modifier (the SSL "FINE" = "Shift" key). Double-click latches it on; press again to unlatch. With *Settings → Device → Keyboard Options → Shift activates Fine mode* on, holding this also drops V-Pot / encoder step size to ×0.25 (faders unaffected). |

## Layer keys

`LAYER 1 / 2 / 3` — three SSL DAW layers. Bindable. Layer 3 LED on certain UF8 units does not light up — confirmed hardware quirk, not a Rea-Sixty bug. Layer functionality itself works.

## 360° key

Default binding `mixer_toggle` — opens / closes the Rea-Sixty Settings window.

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
- **Channel Fader Level** (CS fader stage)

When SSL Strip Mode is engaged on the UF8, the **Channel Fader Level** parameter is what the UF8 motorised faders drive.

## Bus Compressor section (7 knobs + 1 button)

- **Threshold** / **Ratio** / **Attack** / **Release** / **Make-Up** / **Mix** / **SC HPF** — 7 knobs across the top centre.
- **Bus Comp In** — single button enabling the BC section.

The BC controls drive the BC Instance on the **BC anchor track** — the track UC1 has currently pinned for Bus-Comp display. Encoder 2 (the *Secondary encoder* right of the central screen) scrolls the anchor between BC-bearing tracks.

The mechanical BC VU meter is driven from REAPER via the PreSonus standard `GainReduction_dB` host-side hook. Rest position = bottom of scale; the needle swings up through GR magnitude.

## Central control panel (between CS and BC)

A column of buttons + the central LCD + the two encoders:

- **Back** / **Confirm** — navigate the on-screen menus (Routing / Presets / etc.).
- **Routing** — opens the Routing menu on the LCD.
- **Presets** — opens the Presets menu.
- **360°** — default `mixer_toggle` (open / close Rea-Sixty Settings; bindable on its own UC1 entry so it can diverge from the UF8 360° key).
- **Magnifier** — no factory action; bindable.

## CHANNEL encoder (left of the central LCD)

The large rotary on the central control panel. ButtonId `Uc1Encoder1` in the bindings system.

- **Rotation** — `track_scroll` by default (step REAPER track selection ±, with UC1 focused-track and CS-domain focus following along). Rebindable in Settings → Bindings → UC1 (ROTATE tile under ENCODER 1) — Shift = `instance_cycle` by default; Cmd / Ctrl free.
- **Push** — push event arrives as button 0x0D; default binding empty.
- When **Settings → Modes → FX / Cycle → "UC1 Encoder 1 (CHANNEL)"** is ticked AND a cycle-kind Selection Mode is engaged, rotation drives `reasixty_dispatchSelModeCycle` instead, regardless of the binding.

## Secondary encoder (right of the central LCD)

ButtonId `Uc1Encoder2` in the bindings system; SSL calls it the "Secondary" or "BC" encoder.

- **Rotation** — `bc_track_scroll` by default (scroll the BC anchor between BC-bearing tracks). Rebindable, including to `instance_cycle` or `fx_cycle`.
- **Push** — `show_focused_plugin_gui` by default (toggle floating window of the cursor instance from the most recent cycle). Rebindable.
- Cycle-Control mask includes "UC1 Encoder 2 (BC)" — same SEL-Mode override mechanism as above when ticked + a cycle-kind Selection Mode active.

## Central LCD zones

Three addressable zones:

- **Channel-Strip readout (zone 0x03)** — last-touched CS parameter name + value, e.g. "HF Gain  +3.0 dB". Fades after a few seconds.
- **Bus-Comp readout (zone 0x05)** — last-touched BC parameter name + value.
- **Central main (zone 0x0F)** — multi-overlay area. Shows: track-name header + focused Instance variant (default), or the prev / curr / next Instance-carousel triple (after an Instance / FX Cycle just fired), or the BC compressor mode / status, or the Nav-Mode markers/regions carousel. Overlays are mutually exclusive; the most recent claim wins.

## Brightness

Set independently per channel (UC1 LEDs / UC1 LCDs / UF8 LEDs / UF8 LCDs) under Settings → Device → Brightness. Six `brightness_*_up` / `brightness_*_down` builtins drive these from a hardware button (LEDs / LCDs / Both, up / down).

## UC1 GR Calibration

If the UC1's mechanical VU meter or the CS Dynamics GR LEDs drift from their printed scale, a per-tick offset table at the very bottom of **Settings → Device** corrects each printed dB tick individually. The workflow mirrors SSL 360°'s own BC VU calibration tool — click `Test` next to a tick, then `+` / `-` until the UC1 lines up with the printed marking. Auto-saved per-tick. `Stop test` resumes normal GR.

\newpage

# Settings window

The Settings window is a dockable ReaImGui context. Open with the `360°` key (default), the `mixer_toggle` builtin, or REAPER's Action `Rea-Sixty: Toggle Settings window`.

Eight sidebar tabs: Device · Appearance · Bindings · Modes · FX Learn · Selection Sets · Parameter Groups · About.

## Device pane

### Connected devices

Live status dots for UF8 and UC1 (`[connected]` / `[not connected]`), plus the serial number when connected. Each device has an **Identify** button that overrides its LCD with a "THIS UNIT" marker for ~2 s — useful for multi-UF8 setups (deferred — see *Pending* at the bottom of the pane).

### Brightness

Two 5-step sliders (Dark / Dim / Half / Bright / Full):

- **LEDs** — drives buttons + V-Pot rings + UC1 LEDs
- **LCDs** — drives the UF8 LCD strips + UC1 LCD + UC1 status displays

Set independently so you can crank the displays while keeping the LED ring dim, or vice versa. Six `brightness_*_up` / `brightness_*_down` builtins drive these from any button binding.

### Display behaviour

| Toggle | Effect |
|---|---|
| SEL LED follows REAPER track colour | Each strip's SEL LED renders the track's REAPER colour instead of monochrome. Off → SEL is white when selected. |
| GR meter source (combo) | *Only Show Channel Strip GR* — meter limited to SSL CS / mapped CS plug-ins. *Show any GR Data* — falls back to any FX exposing the PreSonus `GainReduction_dB` host-extension on the focused track (ReaComp, FabFilter, etc.). |
| Track selection follows parameter change | V-Pot / CS / BC knob edits on a non-selected track auto-select that track. |
| Touch selects channel | Touching a UF8 fader exclusively selects that strip's track. |
| SSL Strip Mode follows focused plugin window | When REAPER's last-focused FX is a CS Instance, SSL Strip Mode auto-engages. |
| Plugin GUI follows active Instance | When an Instance Cycle / FX Cycle lands on a new target, an already-open floating plug-in GUI re-points to the new target. |
| Pin plug-in GUI position | Capture an XY coordinate (drag a window, click *Capture current*). Every subsequent managed `TrackFX_Show` snaps the window to that pin. Alternatively *Center on Screen*. |
| Pin FX-chain GUI position | Same pattern for FX-chain windows. Title matching looks for "FX:" on macOS. |
| Meter ballistic (combo) | Peak / VU / RMS — applies to the strip-bar level meters. |

### Tracks

| Toggle | Effect |
|---|---|
| TCP follows UF8 selection | UF8-triggered track selection scrolls REAPER's arrange-view track panel (action 40913). MCP follow is always on. |
| Show tracks hidden in TCP | Off (default) → tracks REAPER has hidden in TCP also disappear from the UF8. |
| Show tracks hidden in MCP | Same, independently, for MCP hidden tracks. |
| Hide tracks in collapsed folders | Independent surface-side mirror of REAPER's "hide children of collapsed folders" preference. When on, any track whose ancestor folder has `I_FOLDERCOMPACT == 2` (fully collapsed) drops from the UF8 strip list. Walks ancestors on every track-list rebuild so the filter follows live folder-state changes. Default off. |
| Long track-name handling (combo) | How track names longer than the 7-char scribble-strip slot are shortened. *Truncate* (default) keeps the legacy first-7-chars cut ("Background Vocals" → "Backgro"). *Smart abbreviate* drops separators, then vowels after the first letter of each token, then collapses repeated consonants, then proportionally distributes the remaining char budget across tokens ("Background Vocals" → "BckgVcl", "Drums Bus" → "DrmsBs"). Short all-caps tokens (DI / FX / EQ / …) survive untouched. Mode switch repaints all 8 strips immediately. |

### Plug-ins

| Toggle | Effect |
|---|---|
| Don't show offline FX | Cycle rings (FX Cycle, Instance Cycle, per-strip variants) and the UF8 colour-bar default cursor skip TrackFX slots whose `TrackFX_GetOffline` returns true. Offline-only tracks show a `-`. |
| Wrap Plugin Cycle | Default on (legacy behaviour) — cycle rings wrap from last FX back to first. When off, both ends of the FX chain hard-stop on every cycle path (Channel-Encoder FX/Instance Cycle, per-strip V-Pot FX/Instance Cycle), and the UC1 carousel shows no neighbour name past the first/last FX. |
| Auto-engage UF8 Plugin Mode for UF8-mapped plug-ins | When SEL-Mode cycle V-Pot push OR a `show_focused_plugin_gui` binding lands on a UF8-mapped plug-in, also engage UF8 Plugin Mode with GUI. |

### Keyboard Options

| Toggle | Effect |
|---|---|
| Alt/Option + fader drag → snap back to original on release | Hold Alt/Option while moving a fader; release while still holding Alt → fader snaps back to its touch-on value. Mirrors REAPER's mouse Alt-drag. |
| Keyboard Shift acts as Shift modifier | When on, holding **Shift** on the host keyboard counts as the Shift modifier for any binding's Plain/Shift/Cmd/Ctrl modifier slot — in addition to the hardware `mod_*` bindings. |
| Keyboard Cmd acts as Cmd modifier | Same, for Cmd on macOS. |
| Keyboard Ctrl acts as Ctrl modifier | Same, for Ctrl on Windows / Linux. |
| Shift activates Fine mode (V-Pots / encoders, not faders) | When on, holding the Shift modifier (keyboard Shift, UF8 `FINE` key, or UC1 `Fine` button) drops V-Pot + encoder step size to ×0.25 for momentary fine resolution. Faders are deliberately excluded (they already have Alt-drag for fine control). Stacks with the UC1 `Fine` toggle. Off by default. |

### Pending

A single line documents currently-deferred features (multi-UF8 drag-to-reorder).

### UC1 GR calibration

Per-tick offset editor for the mechanical BC VU meter and the CS Dynamics GR LEDs. See chapter UC1 hardware → UC1 GR Calibration.

\newpage

## Appearance pane

### Theme

Three-way radio:

- **Vanilla** (default) — ReaImGui's built-in dark theme, no overlay.
- **Dark** — Rea-Sixty's themed dark palette (blue-grey base + soft accents).
- **Light** — the Light counterpart for users on a light-mode REAPER setup.

Re-themes every Settings panel + the FX Learn schematic. Hardware-face colours (UF8 silk-screen mockups, UC1 schematic) stay constant.

### Font Size

Three-way radio: **Small** / **Normal** / **Large**. Drives every Settings widget except the UF8 / UC1 mockup schematic labels — those stay locked at 12 px so the schematics don't reflow when the picker changes. Numeric inputs (GR-cal table, FX Learn binding column) scale with the font picker so layouts stay aligned across sizes.

\newpage

## Bindings pane

Top of the pane: a tab bar with **UF8** and **UC1**, each rendering its hardware as a vector schematic. Click any button, knob, encoder, or fader to select it; the per-button editor opens below the schematic.

The "current layer" follows whichever Layer button (1 / 2 / 3) is highlighted in the schematic — click a Layer button to switch the live layer; the green outline indicates which one is active. There is no separate layer-tab strip.

Three click-to-edit special cases:

- **Top-soft-keys** (the 8 buttons above the V-Pots) open the **user-Quick slot editor** for the live (Layer, Quick, Sub-Bank) coordinate instead of the regular per-button editor — top-soft-keys are slot pickers, not direct actions.
- **Sub-bank selectors** (V-POT + 1..5) open the **sub-bank cell editor** with a Per-Quick LED override so the user can distinguish (Layer, Quick) contexts visually.
- Everything else uses the regular per-button binding editor.

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

Right-clicking a button in the schematic opens Copy / Paste / Clear options for that binding.

### Export / Import / Reset

Bindings are bundled into the **Setup** export available from the **About** pane (single file covers bindings + plug-in maps + Settings preferences + Parameter Group slot names). Per-binding-file export does not exist as a Bindings-pane action.

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
| Enable RME / TotalReaper integration | Master switch. Requires the TotalReaper extension. While REC Selection Mode is active, the assignments below dispatch TotalReaper actions against the strip's track. |
| V-Pot rotation → Preamp gain ±1 dB | Steps preamp gain instead of pan. |
| V-Pot rotation + Shift → Change input channel | Re-routes the strip's track input on rotation. |
| V-Pot push (combo) | TotalReaper action assignment. Choices: `None`, `Toggle 48V phantom`, `Toggle pad`, `Toggle phase invert`, `Toggle AutoLevel`. |
| Cut button (combo) | Same action list. |
| Solo button (combo) | Same action list. |

(Polarity is not a separately assignable REC button — only V-Pot push / Cut / Solo.)

### NAV

The NAV sub-tab is divided into five sections.

**Activation** — read-only list of which physical button currently fires each of the three Nav-mode toggle builtins:

- `marker_overlay_toggle` (Markers + Regions)
- `marker_overlay_markers_only_toggle`
- `marker_overlay_regions_only_toggle`

Each line shows the bound layer + button + modifier + long-press flag, or "(unbound)". Edit via Settings → Bindings.

**View defaults**

- **Default view on Nav Mode entry** (radio): `Regions` / `Markers in current region` / `Markers (all)` / `Last used`. Applied by `marker_overlay_toggle` only. *Markers in current region* snaps to the region under the playhead; falls back to Regions if the playhead is in a gap.
- **Region-press behaviour** (radio): `Jump + Drill` / `Jump only` / `Drill only`. Jump = move transport to region start; Drill = enter the region's marker list. RegionsOnly view-lock always suppresses Drill.

**UF8 strip display**

- **Lower-row format** (radio): `Off (V-Pot value)` / `Index (R03 / M07)` / `Timecode (MM:SS)`. Off keeps the V-Pot value visible; Index / Timecode overlay marker metadata on the lower row.
- **Color-bar source** (radio): `REAPER marker colour` / `Force palette grey`. REAPER honours the colour override set on each marker / region; Force grey suppresses it.

**UC1 Encoder 2**

- **Take over UC1 Encoder 2 while Nav Mode is active** (checkbox). When off, Encoder 2 rotation stays bound to its normal action (`bc_track_scroll` by default) and the UC1 LCD does not switch to the marker carousel — only UF8 reflects Nav Mode.
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

Top bar:

- **Domain** picker — `Channel Strip` / `Bus Comp` / `None (UF8-only)`. UF8-only maps the FX into the per-strip view without claiming a CS/BC slot.
- **UF8 Mode** checkbox (UF8-only domain) — drives Instance Cycle / Plug-in Mode dispatch.
- **Primary mode** picker (CS variant family) and other domain-specific options.
- **Mockup toggle** — visualises the UC1 layout via a UC1 mockup PNG instead of the strip-bar schematic. Persisted in ExtState `ReaSixty/fxLearnMockup`.
- **AutoLearn** button — runs the pattern-matching engine (hardcoded SSL seeds + user-map dictionary; three-pass: exact / substring / token) against either the live FX on the focused track or the catalog's stored param snapshot. Confidence-scored suggestions open in an *AutoLearn Preview* modal with a per-row checkbox + confidence %, plus All / None bulk helpers. UF8 V-Pot suggestions auto-group by category (EQ / Comp / Gate / Filter / I-O / Misc). Accept applies every checked mapping into the active map.
- Breadcrumb **`← All maps`** to leave the editor.

Editor body — depends on the domain:

- **CS / BC domain:** the UC1 / strip schematic. Click a control to learn it to a plug-in parameter; the active row's combo lists every plug-in parameter (with current value if a live instance is on the focused track).
- **None (UF8-only):** the UF8 strip-bar schematic. Drag a strip slot (V-Pot, top soft-key, etc.) onto a plug-in parameter.

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
- **Display label** (inline text field) — per-slot override for the scribble-strip name (1..7 ASCII chars). Empty = falls back to the parameter's default short name. Persisted as `UserLinkSlot.customLabel` (FX-Learn slot) or `UserUf8BankSlot.label` (UF8 V-Pot) / `UserUf8StripBinding.faderLabel` (UF8 fader) in `user_plugins.json` — no schema bump.

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

### Stepped parameters

When the bound parameter is a discrete-stepped enum (e.g. PSP Townhouse attack/release time selectors, HPF slope pickers, oversampling toggles) — anything REAPER reports with a non-zero per-step size via `TrackFX_GetParameterStepSizes` — the editor automatically switches to a stepped-aware layout. Sensitivity, range, and push-reset still apply; curve does not.

- **Sensitivity** label flips to **Detent speed**. The default 1.0× means *2 detents per step* on V-Pots and UC1 knobs (matches the legacy UC1 stepped feel). 2.0× → 1 detent per step. 0.5× → 4 detents per step. 4.0× and above fire multiple steps per detent. Shift-Fine still applies on top — at slider min (0.1×) Shift gives an effective 0.025× (≈40 detents per step) for ultra-precise crawling.
- **Canvas + preset row hidden.** A single info line replaces them: `Stepped parameter — N values (~X.XXX per step). Curve disabled; Min/Max snap to the step grid.`
- **Min / Max** in the right-click menu snap on commit to the nearest step boundary, so the encoder always traverses real values. Below the table a hint line reads `~K steps reachable in this range`.
- **Push reset** (Value-mode V-Pots) snaps the chosen default to the nearest step.

The runtime accumulator decays after 150 ms of inactivity, so reversing direction at sub-threshold doesn't leave residual fractional steps fighting the new motion.

In the FX-Learn schematic, slots with customised knob travel show:

- Two radial ticks at the Min / Max angles on the on-screen knob (7 o'clock → 12 → 5).
- A small centre dot when a curve is set.

UF8 V-Pots dispatch the math at the encoder-delta site (`sensitivity → inverseCurve → step → applyCurve`) so external automation writes stay coherent. UC1 channel-strip / bus-comp knobs and the EXT_FUNCS encoder honour the same path — a UC1 knob and a UF8 V-Pot bound to the same parameter stay in lock-step. Built-in SSL CS / BC slots are intentionally untouched and keep the legacy linear + EQ-gain virtual-notch path; knob travel only kicks in when a user-learned slot is present for the focused plug-in.

> **UF8 faders intentionally exclude knob travel.** Absolute-position + motor feedback creates round-trip races with plug-in quantisation (fader jumps during user motion, snaps on release). The plug-in's own taper is the right place for fader-side shaping.

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
- **Group** — bound to a REAPER track group (1..64). Membership refreshes every onTimer tick from `GetSetTrackGroupMembership` across all Lead/Follow categories (ANY category).

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

Parallel parameter control across multiple tracks: while a slot is active, plug-in tweaks on the focused track copy to every member track of the slot.

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

- **Version** — `git describe --tags --always --dirty` of the source tree at build time. On a tagged release: `v0.1.8`. Past a tag: `v0.1.8-N-g<sha>` (N commits past the tag). With uncommitted changes: trailing `-dirty`. Read this line first when triaging issues so it's obvious which build is loaded.
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

- Lists the diagnostic log paths (`/tmp/reaper_uf8_frames.log`, `/tmp/reaper_uf8_colors.log`, etc.).
- **Reveal /tmp in Finder** button (macOS only — equivalent on other platforms TBD).

\newpage

# Native actions

Bind any of these to a UF8 / UC1 control in Settings → Bindings → *(button)* → Native. Actions that take a parameter (slot number, soft-key index, etc.) are flagged below.

## Selection Mode toggles

Switch which Selection Mode is active (see chapter **Selection Modes** for what each one does). Pressing a mode's toggle while it is already active returns to NORM.

- **`selection_mode_norm`** — set the active Selection Mode to NORM (the default).
- **`selection_mode_rec`** — toggle REC mode (SEL = arm).
- **`selection_mode_rec_mon`** — toggle REC+MON mode (SEL = arm + monitor).
- **`selection_mode_auto`** — toggle AUTO mode (SEL = step the track's automation mode; V-Pots show automation indicator).
- **`selection_mode_instance`** — toggle FX Cycle Sel-Mode (V-Pots walk every FX per strip; push opens active FX).
- **`selection_mode_instance_cycle`** — toggle Instance Cycle Sel-Mode (V-Pots walk only Instances per strip).
- **`selection_clear_all`** — clear the REAPER track selection.

## Channel Encoder mode toggles

Change which job the large CHANNEL encoder does. The current mode persists across REAPER restarts.

- **`encoder_nav`** — Channel Select (the default; rotation moves track selection ±1).
- **`encoder_nudge`** — playhead nudge (step size from REAPER's nudge setting).
- **`encoder_focus`** — synthesised mouse-wheel under the screen cursor (use to scroll plug-in windows, browsers, etc.).
- **`encoder_markers`** — step prev / next REAPER marker.
- **`encoder_bank_by_1`** — surface bank by 1 strip per detent.
- **`encoder_last_param`** — step the last-touched REAPER parameter ±.
- **`encoder_instance`** — Instance Cycle on the focused track.
- **`encoder_fx_cycle`** — FX Cycle (every FX) on the focused track.
- **`encoder_selset_cycle`** — step through populated Selection Set slots (off → 1 → 2 → … → off).
- **`encoder_mode_dispatch`** — rotation handler that routes to whichever encoder mode is currently set. Bound by default to the CHANNEL encoder so rotation just "does the right thing"; rebind if you want a fixed behaviour.

## Direct encoder rotation handlers

Bind these to a non-CHANNEL rotation (UC1 Encoder 2, footswitches with rotation, etc.) when you want a single fixed behaviour regardless of the global encoder mode.

- **`instance_cycle`** — Instance Cycle on focused track (rotation = step ±).
- **`fx_cycle`** — FX Cycle on focused track.
- **`select_relative`** — step REAPER track selection ±1.
- **`track_scroll`** — visible-track scroll + select + UC1 focused-track follow + force CS-domain focus. Like `select_relative` but UC1-aware. Default binding on UC1 Encoder 1.
- **`playhead_nudge`** — playhead nudge ±.
- **`mouse_scroll`** — synthesised scroll-wheel under the screen cursor.
- **`bc_track_scroll`** — scroll the UC1's Bus Comp anchor track ±1 (which track the BC section is pinned to). REAPER selection and UF8 bank stay put.
- **`bc_track_scroll_select`** — same scroll as `bc_track_scroll`, but additionally pulls REAPER selection + UF8 bank to the new BC anchor. Use this when you want the BC encoder to drive the whole surface, not just the UC1 carousel.
- **`temp_selset_scroll`** — encoder scroll within the Temporary Selection Set (see below). Walks the temp set in REAPER project order; works regardless of whether the temp filter is recalled.

## Plug-in Mixer modes

Engage / disengage the on-surface plug-in editing modes.

- **`ssl_strip_mode_toggle`** — toggle SSL Strip Mode. While active, fader → CS Fader Level, V-Pots → CS controls, soft-keys → CS bypass / EQ-In / Filter-In.
- **`ssl_strip_mode_toggle_with_gui`** — same, AND open the CS plug-in's floating GUI (pinned per the GUI-pin settings).
- **`uf8_plugin_mode_toggle`** — toggle UF8 Plug-in Mode. All 8 strips become the strips of a single FX-Learn-mapped plug-in. Bank ← / → flips between fader-banks A / B for ≥9-control plug-ins.
- **`uf8_plugin_mode_toggle_with_gui`** — same, AND open the plug-in's floating GUI.

## Plug-in commands

These act on the FX the cursor currently points at on the focused track (the FX that the last cycle landed on; defaults to the first online Instance after a fresh load).

- **`show_focused_plugin_gui`** — toggle the floating GUI of the cursor FX. With the Device option *Auto-engage UF8 Plugin Mode* on, also engages UF8 Plug-in Mode if the cursor lands on a UF8-mapped plug-in.
- **`plugin_bypass`** — toggle bypass of the cursor FX.
- **`plugin_offline`** — toggle offline state of the cursor FX.
- **`plugin_preset_next`** — load the next preset.
- **`plugin_preset_prev`** — load the previous preset.
- **`plugin_preset_cycle`** — encoder-rotation variant that steps presets ± by detent count.
- **`plugin_move_up`** — move the cursor FX up one slot in the track's FX chain.
- **`plugin_move_down`** — move the cursor FX down one slot.
- **`show_fx_chain`** — open / close REAPER's FX chain window for the focused track (pinned per the FX-chain pin settings).
- **`close_all_fx_guis`** — close every floating FX window in the project.
- **`fx_param_inc`** — step the FX-Learn slot a V-Pot is bound to upward from a button. Action-picker exposes the slot target (combo built from the built-in PluginMap registry — link IDs are stable across SSL CS / BC variants), a step-size slider, and a wrap-vs-clamp checkbox. Honours the slot's range, curve, and sensitivity, so a button bound to `fx_param_inc` and a V-Pot bound to the same slot stay in sync. Useful for "+1 dB" or "next preset value" buttons.
- **`fx_param_dec`** — same as `fx_param_inc` with the sign flipped.

## Instance navigation

These step *only* the Instance index (CS / BC / UF8-Mode-mapped). They are the focused-domain equivalent of the Instance Cycle Sel-Mode, but as standalone bind targets.

- **`instance_next`** — next Instance in the focused domain on the focused track. Wraps.
- **`instance_prev`** — previous Instance in the focused domain.
- **`domain_cs`** — set the focused domain to Channel Strip (so subsequent `instance_next` walks CS Instances, UC1 CS section refreshes).
- **`domain_bc`** — set the focused domain to Bus Comp.

The focused parameter slot is **preserved across an Instance Cycle** when the new instance offers the same LinkSlot (same domain, same parameter convention). Stops the focused-param surfaces (UC1 BC/CS encoder, V-Pot mirroring) from snapping back to slot 0 — typically the Bypass / FX In toggle — on every cycle step. Cross-domain cycles (CS → BC) and Domain::None (UF8-only user maps) still reset to slot 0 because the slot index isn't meaningful there.

## Per-track automation modes

Set the focused track's automation mode. (`auto_off` and `auto_trim` are alternate names for the same REAPER mode 0; both kept for binding-file compatibility.)

- **`auto_off`**, **`auto_trim`** — mode 0 (Off / Trim).
- **`auto_read`** — mode 1.
- **`auto_touch`** — mode 2.
- **`auto_write`** — mode 3.
- **`auto_latch`** — mode 4.
- **`auto_latch_prv`** — mode 5 (Latch Preview).

## Project-global automation override

Same six modes, but applied via REAPER's *global override* (overrides every track without changing each track's own mode).

- **`auto_off_global`**, **`auto_trim_global`**, **`auto_read_global`**, **`auto_touch_global`**, **`auto_write_global`**, **`auto_latch_global`**, **`auto_latch_prv_global`**.
- **`automation_zero_all`** — reset every track's automation mode to Trim/Read (mode 0). Useful to revert a write session.

## Bank navigation

- **`bank_left`** — scroll the surface 8 strips left. In UF8 Plug-in Mode the same button flips between fader-banks A / B (for 16-strip plug-ins) instead.
- **`bank_right`** — scroll 8 strips right (same fader-bank flip in UF8 Plug-in Mode).
- **`bank_by_1_left`** / **`bank_by_1_right`** — scroll one strip at a time.
- **`home`** — clear all routing toggles (send / receive views) so V-Pots and faders return to track volume + pan.
- **`page_left`** / **`page_right`** — step the SSL Soft-Key PAGE bank (prev / next of the 6 CS banks + 2 BC banks).

## DAW Layer keys

- **`layer_select_1`** / **`layer_select_2`** / **`layer_select_3`** — switch SSL DAW layer.

## SSL Soft-keys

- **`softkey_bank_select` (param: 0..5)** — select Soft-Key bank N (CS banks 0..5; in Bus-Comp mode, 0..1).
- **`ssl_softkey` (param: 0..7)** — fire SSL Soft-Key cell N in the currently selected bank.
- **`ssl_bank_vpot` (param: 0..7)** — fire SSL V-Pot N (bank-current). The native bindings the Top Soft-Keys use to map to the active CS/BC bank's parameters.

## Send / Receive

- **`send_this` (param: 0..7)** — toggle the Send view for slot N. With the view active, V-Pots drive that send's level instead of pan.
- **`recv_this` (param: 0..7)** — same for Receive views.

## Selection Sets

- **`selset_recall` (param: 1..8)** — toggle slot N: activate if inactive, deactivate if already active. Activation filters the surface to the slot's tracks and snaps the bank to strip 0 = first slot track. Mutually exclusive with the Temporary Selection Set's recall — activating a slot deactivates the temp filter, and vice versa.
- **`selset_save` (param: 1..8)** — save the current REAPER track selection into slot N.
- **`selset_cycle`** — encoder-rotation handler that steps off → first populated slot → next → … → off. Skips empty slots.
- **`temp_selset_add`** — add every currently-selected REAPER track to the Temporary Selection Set.
- **`temp_selset_remove`** — remove every currently-selected REAPER track from the Temporary Selection Set.
- **`temp_selset_recall`** — toggle the Temporary Selection Set's surface filter on / off. LED state-of reports the active flag, so a bound button lights up when the filter is engaged.

## Surface filters / view toggles

- **`folder_mode`** — toggle Folder Mode (only top-level tracks visible; folder children appear on spill).
- **`show_only_selected`** — toggle Show Only Selected (only currently-selected REAPER tracks appear).
- **`mixer_toggle`** — open / close the Rea-Sixty Settings window. Default binding for the `360°` key.

## Nav overlay (Markers + Regions)

- **`marker_overlay_toggle`** — toggle the Nav overlay (UF8 strips show markers / regions, soft-keys jump to them).
- **`marker_overlay_markers_only_toggle`** — Nav overlay restricted to markers only.
- **`marker_overlay_regions_only_toggle`** — Nav overlay restricted to regions only.

## Track arming

- **`tracks_arm_all`** — toggle arm on every track in the project. State is "all armed" ↔ "all unarmed"; mixed state arms everything first, then needs a second press to unarm all.

## Brightness

Each press steps one level (Dark → Dim → Half → Bright → Full). The "Both" variants step LEDs + LCDs in lockstep.

- **`brightness_leds_up`** / **`brightness_leds_down`** — LED ring + button-LED brightness.
- **`brightness_lcds_up`** / **`brightness_lcds_down`** — UF8 LCD + UC1 LCD brightness.
- **`brightness_both_up`** / **`brightness_both_down`** — combined.

## Zoom

Each maps to a REAPER zoom action (defaults for the UF8 cursor pad).

- **`zoom_up`** — zoom in vertically (action 40112).
- **`zoom_down`** — zoom out vertically (40111).
- **`zoom_left`** — zoom out horizontally (1011).
- **`zoom_right`** — zoom in horizontally (1012).
- **`zoom_center`** — zoom to fit the whole project (40295).

## Parameter Groups

- **`param_group_remove_all`** — remove the currently-selected tracks from every Parameter Group slot they appear in.
- **`multi_select_as_temp_group_toggle`** — toggle the "multi-select acts as a temporary parameter group" behaviour. When on, any multi-track selection becomes the active group for parameter-fan-out; when off, the active group is whichever persistent slot is selected.

## Modifier keys

When held, these shift every other binding to its modifier slot. The three matching UF8 keys (Shift / Cmd / Ctrl, or FINE for Shift) are bound to them by default but you can rebind to any other button to relocate the modifier.

- **`mod_shift`** — Shift modifier. Double-click latches (press once more to unlatch). The SSL `FINE` key uses this builtin.
- **`mod_cmd`** — Cmd modifier (macOS) / Windows key (Win) / Super (Linux).
- **`mod_ctrl`** — Ctrl modifier.

## Surface-state toggles

- **`flip`** — swap fader and V-Pot values for the active routing target (e.g. swap send level and send pan between V-Pot ring and motorised fader).
- **`pan_force`** — force V-Pots to Pan regardless of the active Selection Mode. Escape hatch from cycle / REC / AUTO when you need pan back quickly.

## Internal (not user-bindable)

- **`__reaper_action__`** — internal carrier the REAPER Action picker writes into a binding's action field when you select a REAPER action by name. The picker UI is what you use; this action name is just storage.

\newpage

# Plug-in Mixer modes

## SSL Strip Mode

Engage with the `Plugin` button (default), via the `ssl_strip_mode_toggle` / `_with_gui` builtins, or via "SSL Strip Mode follows focused plugin window" auto-engage on a CS plug-in.

While SSL Strip Mode is on:

- Fader → CS Fader Level (the CS plug-in's own fader parameter, not REAPER's track volume)
- V-Pots → CS controls (per SSL's standard 6 soft-key banks)
- Soft-keys → CS bypass / EQ-In / Filter-In etc.
- The colour-bar Type zone shows the CS variant name (CS2, 4K B, 4K E, 4K G) or the user rename
- UC1 follows the same CS Instance

The `with_gui` variant also opens the CS plug-in's floating GUI alongside, pinning it via the pin-position settings.

`Page ←` / `Page →` step through the 6 SSL-defined Channel Strip soft-key banks. `Plug-in` again (or any other Plug-in Mode toggle) exits.

## UF8 Plug-in Mode

Engage with the `uf8_plugin_mode_toggle` (or `_with_gui`) builtin. Default binding: long-press the Plug-in button.

While UF8 Plug-in Mode is on:

- All 8 strips become the strips of a single FX-Learn-mapped plug-in — addressable even when fewer than 8 REAPER tracks are visible (strips past the bank-track count source their content from the focused FX instead of going blank).
- Two fader banks (16 strips total for plug-ins that need more than 8 controls) — Bank ← / Bank → flips between A and B.
- 8 soft-keys above each strip drive the FX Learn-mapped TopSoftKey slot per bank.
- The active group of soft-key cells is the bank's "active TopSoftKey ring" — 1 of 8 ringed brightly.
- Unassigned banks act as no-ops (LEDs dim).

The active plug-in is the focused track's first UF8-Mode-mapped instance, or the Instance the cursor is currently on.

Per-V-Pot customisation lives in Settings → FX Learn → (right-click the V-Pot on the schematic). Each `(fader-bank, soft-key bank, strip)` V-Pot carries its own *V-Pot mode* (Value / Toggle), *Polarity* (Unipolar / Bipolar — Bipolar renders the LCD ring centre-out, like SSL Pan), *Push reset* value, *display label*, *strip colour*, and *knob travel* (Min / Max range + response curve + sensitivity — see *Knob travel + curve editor* under FX Learn pane). Fill Sequential propagates every one of these fields to the strips to the right.

## 360° Link

The SSL 360° Link plug-in (a wrapper that mirrors third-party VSTs into the 360° surface) is recognised natively. When the focused track has a 360° Link instance pointing at a learned plug-in, SSL Strip Mode / UF8 Plug-in Mode dispatch through the linked plug-in transparently.

User-renamed 360° Link instances show the rename instead of the generic "Link" / "L-BC" abbreviation.

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

# Selection Sets

## Active set as a track filter

When a slot is active, the visible track list is filtered to the slot's tracks. Filter is ANDed with Folder Mode / Show Only Selected so they compose naturally.

## Group slots

A Group slot tracks REAPER's track-group membership in real time. Add a track to group N in REAPER → it appears in the surface; remove it → it disappears, without a re-recall.

## Auto-mode interaction

`Selection-Set auto-mode` (Settings → Modes → AUTO) is a single global value: -1 (off) or REAPER's automation mode index (0..5). When a selset is active in AUTO sel mode + the dropdown is non-off, recalling the slot arms its tracks to that mode. Leaving the slot OR leaving AUTO sel mode reverts those tracks to Trim/Read.

## Persistence

Project-scoped Snapshot + Group slots are saved into the project's RPP chunk via REAPER's project-config hook (lines `SELSET_<N>_DATA "..."`). Global-scoped slots ride REAPER's global ExtState (`reaper-extstate.ini`). The per-slot global/project flag itself, plus the `Selection-Set auto-mode` value, also live in global ExtState.

## Temporary Selection Set

A ninth, ad-hoc selection set living alongside the 8 numbered slots — no Settings UI, no slot name, just three actions you bind to hardware. Useful when you want to spin up a working set of tracks for a session ("just these 6 drum mics + the 2 talkback mics") without burning one of the named slots.

- **`temp_selset_add`** — adds every currently REAPER-selected track to the temp set.
- **`temp_selset_remove`** — removes every currently REAPER-selected track from the temp set.
- **`temp_selset_recall`** — toggles the surface filter on / off. While on, only temp-set tracks are visible. Mutually exclusive with the 1..8 slot recall — activating either kind drops the other.
- **`temp_selset_scroll`** (encoder rotation) — steps REAPER track selection through the temp set in project order. Works regardless of whether the filter is currently recalled.

Persists per-project (saved into the RPP via the same project-config hook the numbered slots use). Cleared on REAPER restart only if you delete it manually; otherwise it survives save → reopen.

\newpage

# Parameter Groups

Eight slots + a per-slot member list of REAPER tracks. The Active group's members all receive the same parameter edit when you twist a UF8 V-Pot, a UC1 knob, or move a fader on any one of them.

Slot management UI in Settings → Parameter Groups. Each slot:

- Name
- Member tracks (Add Selection, Remove, Clear)

Active group switch via the per-slot Active radio.

Temp-group mode: when `Multi-Select acts as Temp Group` is on, the active group is derived live from the current REAPER multi-track selection.

Unmapped FX deferred: if a member track lacks the same FX as the source track, the edit silently drops on that track.

Storage: each track's member-of-which-slots state is a bitmask in P_EXT (`P_EXT:rea_sixty:param_groups`). Slot metadata is in a project-scoped JSON sidecar.

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

The three modifier-key builtins (`mod_shift`, `mod_cmd`, `mod_ctrl`) shift every other button's binding to that modifier slot while held. Modifier keys themselves are bindable to any physical button.

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

Toggle: `folder_mode` builtin. When on, only top-level (depth-0) tracks are visible on the surface. Folder children appear only when "spilled" — long-press a folder parent's `SEL` button to toggle that parent's spill.

**Nested folders (ancestor-chain spill).** Long-pressing a folder at any depth spills *that* folder; the ancestor chain stays in the spill set as well, so the intermediate hierarchy remains visible. Collapsing an ancestor (re-long-pressing it) hides its subtree but **keeps the descendants' spill state in memory**, so re-spilling that ancestor restores the previous drill-down without having to long-press each level again. Toggling Folder Mode off (or re-pressing the `folder_mode` builtin) clears the spill set entirely.

## Show Only Selected

Toggle: `show_only_selected` builtin. When on, only currently-selected tracks appear on the surface. Live filter — changing REAPER selection updates the surface within one timer tick.

## Selection-Set filter

ANDs with Folder Mode / Show Only Selected. When a selset is active, only its tracks appear (further filtered by Folder Mode if also on). The Temporary Selection Set is a separate filter that ANDs in the same way; a 1..8 slot and the temp set are mutually exclusive (only one can be recalled at a time).

## Hide-hidden filter

Settings → Device → Tracks → "Show tracks hidden in TCP / MCP" (default both OFF) — hides tracks REAPER has hidden in TCP or MCP. ANDs with all other filters.

## Auto-hide Trim/Read

Settings → Modes → AUTO → "Hide Trim/Read tracks while in AUTO mode" — within AUTO selection mode only, hides tracks whose automation mode is Off/Trim (0) or Read (1).

\newpage

# Troubleshooting

## Rea-Sixty does not appear in Preferences → Control/OSC/Web → Add

The dylib / dll / so didn't load. Check:

- **macOS Console** for codesign / dyld errors.
- **Windows Event Viewer → Application** for module-load errors.
- **Linux:** start REAPER from a terminal and watch stderr.

Make sure the runtime libraries are in the same directory as `reaper_rea-sixty.{dylib,dll,so}` — for Windows this means `%APPDATA%\REAPER\UserPlugins\`, where the delay-load resolves them at first call.

## Surface does not respond / "SSL360Core owns the device"

SSL 360° is running and has claimed the UF8/UC1 vendor interface exclusively. Quit SSL 360° and restart REAPER.

## Disconnect after sleep / wake or sustained idle (macOS)

Known issue: both UC1 and UF8 IN endpoints can fail within ~3 ms of each other on a sustained host-side USB stack condition. `libusb_reset_device` does not escape it. Physical replug (one or both devices) recovers. Diagnostic logs in `/tmp/rea_sixty_uc1_stale.log` + `/tmp/rea_sixty_uf8_stale.log`.

## Track-colour wrong

- Confirm Settings → Device → Display behaviour → "SEL LED follows REAPER track colour" is on.
- The colour-bar quantiser is HSV polar — subtle RGB differences in REAPER's colour picker can land on neighbouring palette slots. Pick a more saturated colour if the nearest palette slot looks wrong.

## GR meter reads wrong on a third-party compressor

- Enable Settings → Device → Display behaviour → "GR meter source: Show any GR Data".
- Confirm the compressor implements the PreSonus VST3 host-side `GainReduction_dB` config-parm. Plug-ins that don't expose this will not drive the meter regardless of settings.

## Linux port resets / `xhci_hcd disabled by hub (EMI?)`

Plug UF8 and UC1 into separate PC USB ports. Daisy-chaining UC1 through UF8's downstream port triggers `xhci_hcd` port cycling on Linux 6.17.

## Settings window doesn't appear

ReaImGui isn't installed. Install it via ReaPack. Hardware control still works without it.

## Diagnostics

- `/tmp/rea_sixty_uc1_stale.log` — UC1 device-handle diagnostics
- `/tmp/rea_sixty_uf8_stale.log` — UF8 device-handle diagnostics
- macOS Console / Windows Event Viewer / Linux journal for in-process errors

\newpage

# Known limitations

- **Multi-UF8 support deferred.** Single-device assumption in bank-shift / colour-sync / VU-meter paths. The Connected devices list shows multi-UF8 setups but drag-to-reorder is greyed out.
- **No SSL Plug-in Mixer side panel** (the on-screen Mixer view alongside the Settings tabs) — Phase 2.6 on the roadmap, not in this build.
- **macOS: Intel binaries not shipped.** Apple Silicon only.
- **Layer 3 LED hardware quirk** on certain UF8 units — confirmed not fixable from code; layer functionality still works.
- **Firmware update breakage** is on the project to chase. Protocol is self-decoded; SSL gives no compatibility guarantees.

\newpage

# Vendor relationship

Not affiliated with Solid State Logic. SSL ACP Support replied to the project author on 2026-05-18 confirming "no objections to the public, open source project" but declined to share protocol documentation.

Protocol stays self-decoded; documented in `docs/protocol-notes.md` and adjacent capture notes in the repo.

No SSL binaries, firmware, or trademarks are redistributed. "SSL", "UF8", "UC1", "360°" are property of Solid State Logic Ltd.

