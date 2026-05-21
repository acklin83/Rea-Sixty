---
title: Rea-Sixty User Manual
subtitle: Native REAPER ↔ SSL UF8 / UC1 driver
author: Frank Acklin
date: v0.1.2
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

This manual documents Rea-Sixty v0.1.2. Earlier manuals (anything dated before 2026-05-21) are superseded.

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

- **Mac:** `rea-sixty-mac-v0.1.2.zip` — three Apple-notarised dylibs. Unzip into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.1.2.zip` — three DLLs. Unzip into `%APPDATA%\REAPER\UserPlugins\`.
- **Linux:** `rea-sixty-linux-v0.1.2.tar.gz` — `.so` + udev rule + INSTALL.txt. Follow INSTALL.txt.

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

The big channel encoder above the strips runs one of nine modes. Switch with the corresponding `encoder_*` builtin. The current mode persists across REAPER restarts.

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

# UF8 hardware

## Strips (×8)

Per strip:

| Control | Function (NORM) | Notes |
|---|---|---|
| Fader | Track volume | Full 16-bit pitch-bend protocol; motorised, capacitive-touch detection. |
| V-Pot rotation | Pan | Re-maps per Selection Mode. |
| V-Pot push | Centre Pan / mode-specific | In Instance / FX Cycle SelMode → open FX GUI. |
| Capacitive touch | Drives REAPER's "touch" automation | Touch ON + Alt held + drag → on release, snaps fader back to touch-on position (Device → Faders → "Alt/Option + fader drag → snap back"). |
| `SEL` | Selection-Mode dependent | NORM = exclusive select; REC = arm; AUTO = automation mode cycle. Long-press SEL on a folder parent toggles spill. |
| `SOLO` | Solo | LED colour follows REAPER track colour when Display behaviour → "SEL LED follows REAPER track colour" is on. |
| `CUT` | Mute |  |
| LCD scribble | Upper zone = track name / mode-dependent; lower zone = parameter readout | Track-colour bar at bottom unless a Plug-in Mode hijacks it. |

## Bank navigation

| Control | Action |
|---|---|
| `Bank ←` / `Bank →` | Scroll ±8 strips. In UF8 Plug-in Mode → flip between fader-bank A / B (16-strip plug-ins). |
| `Channel ←` / `Channel →` | Scroll ±1 strip |
| `Home` | Clear routing toggles (send / receive views); leaves bank position alone |
| `Page ←` / `Page →` | Step the SSL Soft-Key bank (prev/next of the 6 hard-coded CS banks + 2 BC banks) |

## Channel encoder

Top centre. Rotation drives the active Encoder Mode (see chapter Channel Encoder modes). Push = mode-specific.

## Layer keys (`1` / `2` / `3`)

Three SSL DAW layers. Bind any builtin via the Bindings tab. The Layer 3 LED on certain UF8 units is unresponsive — confirmed hardware quirk; the layer functionality still works, only the LED feedback is missing.

## Send / Plug-in row

| Button | Function |
|---|---|
| `Send` (×8, one per strip) | Toggle the per-strip send view. Push then rotates that strip's V-Pot to drive the send level. |
| `Plugin` | Toggles SSL Strip Mode. Default. With *Shift* (or via `_with_gui`) opens the master plug-in GUI alongside. |
| `Quick 1 / Quick 2 / Quick 3` | User-Quick slots — configurable in Settings → Bindings, default Q1 = CS, Q2 = BC, Q3 = I/O. |

## Selection-Mode block

`NORM` / `REC` / `AUTO` buttons live below the encoder. They map to the corresponding selection-mode builtins.

## FX Cycle / Instance Cycle on the SEL row

The FX Cycle and Instance Cycle Selection Modes have no dedicated hardware button by default — they live as builtin actions (`selection_mode_instance` and `selection_mode_instance_cycle`) for the user to bind to whichever button they like.

## Transport keys

`Play / Stop / Record / Loop / << / >>` are bindable. Defaults map to REAPER's transport actions (1007 / 1016 / 1013 / 1068 / seek start / seek end).

## Zoom cluster

The four zoom arrows + zoom-center map to REAPER's standard zoom actions (40112 / 40111 / 1011 / 1012 / 40295) via the `zoom_*` builtins.

## 360° key

Opens the Rea-Sixty Settings window (`mixer_toggle` builtin).

## PLUGIN / CHANNEL

DAW-spec PLUGIN / CHANNEL keys are bindable like every other surface button — no fixed assignment.

## FLIP / PAN / FINE

| Key | Default |
|---|---|
| `FLIP` | Toggle fader ↔ V-Pot. |
| `PAN` | `pan_force` — V-Pots forced to Pan regardless of Sel-Mode. Acts as an escape hatch from cycle / REC / AUTO modes. |
| `FINE` | Modifier (`Shift` family). Held + V-Pot rotation = fine resolution. Double-click latches. |

## Modifier keys

`Shift` / `Cmd` / `Ctrl` are bindable as modifiers — when held they shift every other button's binding to its modifier variant. Double-click `Shift` (FINE) latches it on until pressed again.

## Foot-switch jacks

Two jacks on the back. Bindable like any other button. No factory defaults.

\newpage

# UC1 hardware

The UC1 mirrors the SSL Channel Strip 2 + Bus Compressor 2 + 4K EQ + 4K Dynamics + Filters surface controls on a dedicated unit. When UC1 is plugged in alongside UF8, it auto-engages on the focused REAPER track and follows the focused Instance.

The UC1 has no hardware mode-switch — Channel Strip and Bus Compressor sections are always live.

## Channel Strip section

24 knobs across EQ (HF / HMF / LMF / LF + freq + Q) + Dynamics (Compressor + Gate / Expander) + Filters (HPF / LPF) + Input / Output stages + the Channel Fader Level.

Each knob writes the corresponding parameter of whichever CS Instance is currently in focus on the focused track. For tracks with multiple CS Instances, Encoder 2 push or the Instance Cycle action selects which one is the active CS Instance.

Bypass-domain buttons (HF Bell, EQ In, LF Bell, Dyn-Out, Gate-In, SC) drive boolean params on the CS plug-in. Solo / Cut / Polarity / ChannelIn / Solo-Clear are surface-state buttons routed to REAPER's track ops.

## Bus Compressor section

8 dedicated BC knobs (Threshold / Ratio / Attack / Release / Make-Up + Mix + In + Side-Chain). They drive the BC Instance on the **BC anchor track** — the track UC1 has currently pinned for BC display. Encoder 2 push pins the BC anchor to the focused track.

The mechanical BC VU meter is driven from REAPER via the PreSonus standard `GainReduction_dB` host-side hook. The needle rest position is the bottom-of-scale; the meter swings up through GR magnitude.

## Encoder 1 (CHANNEL)

Tracks the focused track. Rotation = step focused-track selection ±. The encoder's surrounding 7-segment digit shows the absolute REAPER track number of the focused track.

When **Settings → Modes → FX / Cycle → "UC1 Encoder 1"** is ticked, this encoder drives `reasixty_dispatchSelModeCycle` while a cycle-kind Selection Mode is active — same override mechanism as the UF8 Channel Encoder.

## Encoder 2 (BC)

Tracks the BC anchor. Rotation = scroll the BC anchor track ±. Push = pin BC anchor to currently focused track AND fire the active FX cursor's GUI ("show focused plug-in GUI"). When Cycle Control mask includes UC1 Encoder 2, this encoder drives the SEL-Mode cycle override.

## Central LCD

Three zones, mode-dependent:

- **CS readout (zone 0x03)** — last-touched CS parameter name + value, fades after a few seconds.
- **BC readout (zone 0x05)** — last-touched BC parameter name + value.
- **Central main (zone 0x0F)** — header (track name + focused-domain Instance), then prev / curr / next Instance carousel (when an Instance / FX cycle has just fired), then the BC compressor mode / status. Multiple overlays compete here (Nav-mode markers/regions carousel, Instance-cycle carousel, BC-scroll carousel) — only one shows at a time, with the most recent winning.

## Brightness

Independent slider per device (UC1 LEDs / UC1 LCDs / UF8 LEDs / UF8 LCDs) in **Settings → Device → Brightness**, plus three pairs of `brightness_*_up` / `brightness_*_down` builtins for direct bind on a button.

## UC1 GR Calibration

If the UC1's mechanical VU meter or the CS Dynamics GR LEDs drift from their printed scale, a per-tick offset table at the bottom of **Settings → Device** corrects each printed dB tick individually. The workflow mirrors SSL 360°'s own BC VU calibration tool — click `Test` next to a tick, then `+` / `-` until the UC1 lines up. Auto-saved per-tick.

\newpage

# Settings window

The Settings window is a dockable ReaImGui context. Open with the `360°` key (default), the `mixer_toggle` builtin, or REAPER's Action `Rea-Sixty: Toggle Settings window`.

Seven sidebar tabs: Device · Bindings · Modes · FX Learn · Selection Sets · Parameter Groups · About.

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

### Plug-ins

| Toggle | Effect |
|---|---|
| Don't show offline FX | Cycle rings (FX Cycle, Instance Cycle, per-strip variants) and the UF8 colour-bar default cursor skip TrackFX slots whose `TrackFX_GetOffline` returns true. Offline-only tracks show a `-`. |
| Auto-engage UF8 Plugin Mode for UF8-mapped plug-ins | When SEL-Mode cycle V-Pot push OR a `show_focused_plugin_gui` binding lands on a UF8-mapped plug-in, also engage UF8 Plugin Mode with GUI. |

### Faders

| Toggle | Effect |
|---|---|
| Alt/Option + fader drag → snap back | Hold Alt/Option while moving a fader; release while still holding Alt → fader snaps back to its touch-on value. Mirrors REAPER's mouse Alt-drag. |

### Pending

A single line documents currently-deferred features (multi-UF8 drag-to-reorder).

### UC1 GR calibration

Per-tick offset editor for the mechanical BC VU meter and the CS Dynamics GR LEDs. See chapter UC1 hardware → UC1 GR Calibration.

\newpage

## Bindings pane

Renders the UF8 hardware (top half) + UC1 hardware (bottom half) as schematics. Click any button or knob to edit its binding.

Three binding types:

- **Native** — pick from the catalogue of built-in actions (see chapter Native actions).
- **REAPER Action** — pick any action from REAPER's Action List, via a dedicated picker. Includes ReaScript browsing (Load ReaScript dropdown).
- **MIDI Command** — emit a raw MIDI message (Note On/Off, CC, PC, NRPN, etc.). Useful for sending messages to non-REAPER targets.

### Per-binding fields

- **Modifier** — `None` / `Shift` / `Cmd` / `Ctrl` and combinations. One physical button can have up to 8 different bindings (none / S / C / Ct / SC / SCt / CCt / SCCt).
- **Trigger** — `Press` (default) / `Hold` / `Long-press`. Hold keeps firing while pressed; long-press fires once when the threshold elapses.
- **LED appearance** — colour + brightness override.
- **MIDI Channel / Note / Value** (MIDI Command bindings only).

### Bank L / R override

By default `Bank ←` / `Bank →` invoke `bank_left` / `bank_right` builtins. They can be rebound like any other button — useful e.g. to point them at a FX Learn parameter.

### Sorting + Export / Import

Sort dropdown above the bindings table: Name / Developer / Group / Last used. Export saves the current bindings to JSON; Import overwrites. Stored at `~/Library/Application Support/REAPER/rea_sixty/bindings.json` (macOS), `%APPDATA%\REAPER\rea_sixty\bindings.json` (Windows), `~/.config/REAPER/rea_sixty/bindings.json` (Linux).

\newpage

## Modes pane

Four sub-tabs: AUTO · FX / Cycle · REC · NAV.

### AUTO

| Setting | Effect |
|---|---|
| Hide Trim/Read tracks while in AUTO mode | While AUTO selection mode is active, tracks set to Trim or Read are hidden from the surface. |
| Auto-fill from right when fewer tracks than strips | When fewer than 8 tracks are visible, lay them right-aligned (strip 0 padded blank) instead of left-aligned. |
| Selection-Set auto-mode (dropdown) | `None` / `Off / Trim` / `Read` / `Touch` / `Write` / `Latch`. When set, recalling a selset in AUTO mode auto-arms its tracks to this automation mode. Leaving AUTO reverts them to Trim/Read. |

### FX / Cycle

Mask of "which encoder(s) drive Sel-Mode cycle":

- UF8 V-Pots (per-strip cycle) — default ON
- UF8 Channel Encoder
- UC1 Encoder 1 (CHANNEL)
- UC1 Encoder 2 (BC)

When ticked, that encoder bypasses its normal mode and drives `reasixty_dispatchSelModeCycle` while a cycle-kind Selection Mode is active.

**V-Pot push opens active FX as:** Floating window / FX chain — when the active cycle target is opened from a V-Pot push.

### REC

| Setting | Effect |
|---|---|
| Enable RME / TotalReaper integration | Master switch for the REC + RME behaviour. Requires the TotalReaper extension. |
| V-Pot rotation → Preamp gain ±1 dB | When on, V-Pot rotation in REC mode steps preamp gain instead of pan. |
| V-Pot rotation + Shift → Change input channel | When on, Shift+rotation re-routes the strip's track input. |
| V-Pot push / Cut button / Solo button / Polarity (dropdowns) | Per-button TotalReaper-action assignment (`None`, `Toggle 48V`, `Toggle pad`, `Toggle phase invert`, `Toggle AutoLevel`). |

### NAV

| Setting | Effect |
|---|---|
| Activation (read-only) | Mirrors the three Nav-mode toggle builtin bindings (`marker_overlay_toggle`, `…_markers_only`, `…_regions_only`). Edit them in the Bindings tab. |
| Default view | Markers + Regions / Markers only / Regions only. |
| Auto-follow playhead | Sticky toggle; when on the Nav overlay re-pages itself to keep the playhead's region/marker visible. |
| Pagination granularity | Page (8 items) / Item (one at a time). |
| Region press behaviour | Jump only / Jump + Drill (default — entering a region pages to its markers). |
| Colour bar follows overlay item | When on, the UF8 colour bar takes the colour of the marker/region currently under the cursor. |
| Lower row | What appears in the lower scribble zone during Nav: item name / item number / nothing. |

\newpage

## FX Learn pane

The FX Learn editor lets you map any third-party plug-in's parameters onto UF8 strips so the surface controls them like an SSL Instance.

### Top bar

- **UF8 / UC1** schematic toggle (top tab pair) — choose which surface you're laying out for.
- **Domain** — `Channel Strip`, `Bus Comp`, `None` (UF8-only). UF8-only mode lets you map an FX into the per-strip view without claiming a CS/BC slot.
- **UF8 Mode** checkbox (UF8-only domain) — drives Instance Cycle / Plug-in Mode dispatch.
- **Mode picker** (three radio buttons) — selects which UI variant the editor shows for placement.
- **Mockup toggle** — visualises the UC1 layout via a UC1 mockup PNG instead of the strip-bar schematic. Persisted in ExtState `ReaSixty/fxLearnMockup`.

### Layout

- Left pane: searchable plug-in list. Each entry shows display short / full name / domain / variant count / # of mapped params / last edit date.
- Right pane (when a plug-in is selected): the per-strip schematic. Drag a UF8 strip slot onto a plug-in parameter to learn it.

### Multi-instance picker

When the focused track has multiple FX matching the same plug-in name, the editor surfaces a combo to choose which Instance the editor's live readouts come from.

### GR meter combo

Per learnable plug-in, a combo: `None` / `(parameter)` / `Use PreSonus standard`. When set to PreSonus standard, the UF8 strip's GR meter is driven via REAPER's `TrackFX_GetNamedConfigParm(... "GainReduction_dB" ...)` query — works for any plug-in implementing the PreSonus VST3 host extension.

### Param snapshot

When the catalog entry was created with the FX live, parameter names / value formatters are snapshotted into the catalog so the editor stays usable even if the FX isn't currently loaded.

### Storage

Catalog file at `~/Library/Application Support/REAPER/rea_sixty/user_plugins.json` (and equivalent paths on Windows / Linux). Versioned schema (currently v7). Import / Export via the toolbar.

\newpage

## Selection Sets pane

Eight slots (1..8). Each slot is either:

- **Snapshot** — fixed list of REAPER track GUIDs frozen at save time.
- **Group** — bound to a REAPER track group (1..64). Membership refreshes every onTimer tick from `GetSetTrackGroupMembership` across all Lead/Follow categories.

Per-slot controls:

- **Save** — capture the current REAPER selection into the slot.
- **Recall** — make the slot active. Filters the surface to only the slot's tracks.
- **Cycle** — step through populated slots (off → 1 → 2 → ... → off).
- **Name** — editable.
- **Type dropdown** — Snapshot / Group.
- **Group index** (Group type only) — 1..64.
- **Global checkbox** — Group slots only. When OFF, the slot's membership is stored in the project's ProjExtState. When ON, the slot follows whichever project is open.

### Auto-mode binding

A single global "Selection-Set auto-mode" dropdown (Settings → Modes → AUTO) applies to every slot. When a slot is active in AUTO selection mode + the dropdown is set to a value other than `None`, recalling that slot arms its tracks to the selected automation mode. Leaving the slot (or switching out of AUTO) reverts them to Trim/Read.

### Slot bank-snap

Recalling a slot snaps the surface to strip 0 = first channel of the set, so larger sets always start at the beginning. Re-pressing the same slot key keeps your current bank position.

\newpage

## Parameter Groups pane

Maps the parallel-control feature: when you adjust a param on one track, the same param adjusts on every other track in the active group.

Eight persistent slots, each with a list of REAPER tracks. The active group is one of those 8, OR a **temporary group derived from the current multi-track selection** when **Multi-Select acts as Temp Group** is on.

UF8 V-Pot edits + SC / BC encoder edits + UC1 knob edits all fan out to the active group's tracks.

Per-slot UI:

- **Active** button — set this slot as the active group.
- **Name** field.
- **Members** — list of tracks, with Add Selection / Remove / Clear.
- **Save current selection to this slot** — convenience button.

Native actions:

- `param_group_remove_all` — strip the current REAPER selection from every group.
- `multi_select_as_temp_group_toggle` — switch the temp-group behaviour on/off.

Unmapped FX deferred. Storage: `P_EXT` bitmask per track + JSON sidecar (`param_groups.json`) for slot metadata.

\newpage

## About pane

- Version banner + commit hash
- **Install UF8/UC1 WinUSB driver** button (Windows only — UAC prompt, runs the in-product WinUSB installer)
- **Install Linux udev rule** button (Linux only — pkexec, copies the rule into `/etc/udev/rules.d/`, reloads udev)
- Links: GitHub repo, issue tracker, release page

The driver / udev installers are idempotent — clicking with the rule already installed is a no-op.

\newpage

# Native actions

All actions registered as Rea-Sixty natives, grouped by purpose. Bind any of them to a UF8 / UC1 button in Settings → Bindings → (button) → Native. Several take a parameter (e.g. `selset_recall(N)` for slot N) — flagged with **(param)** below.

## Selection modes

| Builtin | Display |
|---|---|
| `selection_mode_norm` | Selection Mode → NORM (SEL Button) |
| `selection_mode_rec` | Selection Mode → REC (SEL Button) |
| `selection_mode_rec_mon` | Selection Mode → REC + MON (SEL Button) |
| `selection_mode_auto` | Selection Mode → AUTO (V-Pot) |
| `selection_mode_instance` | Selection Mode → FX Cycle (V-Pot) |
| `selection_mode_instance_cycle` | Selection Mode → Instance Cycle (V-Pot) |
| `selection_clear_all` | Selection: Clear All Tracks |

## Encoder modes

| Builtin | Display |
|---|---|
| `encoder_nav` | Encoder Mode → Channel Select |
| `encoder_nudge` | Encoder Mode → Nudge |
| `encoder_focus` | Encoder Mode → Mousewheel |
| `encoder_markers` | Encoder Mode → Markers (prev / next) |
| `encoder_bank_by_1` | Encoder Mode → Bank by 1 channel |
| `encoder_last_param` | Encoder Mode → Last Touched Param |
| `encoder_instance` | Encoder Mode → Instance Cycle |
| `encoder_fx_cycle` | Encoder Mode → FX Cycle |
| `encoder_selset_cycle` | Encoder Mode → Selection Set Cycle |
| `encoder_mode_dispatch` | Encoder: dispatch by current mode (rotation handler) |

## Encoder rotation handlers

Bind these to non-Channel-Encoder rotations to drive a single behaviour regardless of mode.

| Builtin | Display |
|---|---|
| `instance_cycle` | Encoder: cycle plug-in instance |
| `fx_cycle` | Encoder: cycle FX (all on focused track) |
| `select_relative` | Encoder: select prev/next track |
| `playhead_nudge` | Encoder: nudge playhead |
| `mouse_scroll` | Encoder: scroll mouse-wheel under cursor |
| `bc_track_scroll` | Encoder: scroll BC anchor track |

## Plug-in modes

| Builtin | Display |
|---|---|
| `ssl_strip_mode_toggle` | Toggle SSL Strip Mode |
| `ssl_strip_mode_toggle_with_gui` | Toggle SSL Strip Mode (with GUI) |
| `uf8_plugin_mode_toggle` | Toggle UF8 Plugin Mode |
| `uf8_plugin_mode_toggle_with_gui` | Toggle UF8 Plugin Mode (with GUI) |

## Plug-in commands (operate on the cursor-active FX of the focused track)

| Builtin | Display |
|---|---|
| `show_focused_plugin_gui` | Plug-in: toggle focused GUI |
| `plugin_bypass` | Plug-in: toggle bypass (active FX) |
| `plugin_offline` | Plug-in: toggle offline (active FX) |
| `plugin_preset_next` | Plug-in: next preset (active FX) |
| `plugin_preset_prev` | Plug-in: previous preset (active FX) |
| `plugin_preset_cycle` | Plug-in: cycle preset (encoder, active FX) |
| `plugin_move_up` | Plug-in: move active FX up in chain |
| `plugin_move_down` | Plug-in: move active FX down in chain |
| `show_fx_chain` | Plug-in: toggle FX chain window (focused track) |
| `close_all_fx_guis` | Plug-in: close all floating FX windows |

## Instance navigation (focused-domain Instance index ±1)

| Builtin | Display |
|---|---|
| `instance_next` | Instance: next (focused domain) |
| `instance_prev` | Instance: previous (focused domain) |
| `domain_cs` | Focused domain → Channel Strip |
| `domain_bc` | Focused domain → Bus Comp |

## Automation modes (per-track, target = last-touched track)

| Builtin | Display |
|---|---|
| `auto_off` | Automation: Off / Trim |
| `auto_read` | Automation: Read |
| `auto_write` | Automation: Write |
| `auto_trim` | Automation: Trim |
| `auto_latch` | Automation: Latch |
| `auto_latch_prv` | Automation: Latch Prv |
| `auto_touch` | Automation: Touch |

## Automation modes (project-global override)

| Builtin | Display |
|---|---|
| `auto_off_global` | Global automation: Off / Trim |
| `auto_read_global` | Global automation: Read |
| `auto_write_global` | Global automation: Write |
| `auto_trim_global` | Global automation: Trim |
| `auto_latch_global` | Global automation: Latch |
| `auto_latch_prv_global` | Global automation: Latch Prv |
| `auto_touch_global` | Global automation: Touch |
| `automation_zero_all` | Automation: Zero All Tracks (→ Trim/Read) |

## Bank navigation

| Builtin | Display |
|---|---|
| `bank_left` | Bank ← (UF8 Plugin Mode: fader-bank; else ±8-strip scroll) |
| `bank_right` | Bank → (UF8 Plugin Mode: fader-bank; else ±8-strip scroll) |
| `bank_by_1_left` | Bank by 1ch ← (one strip) |
| `bank_by_1_right` | Bank by 1ch → (one strip) |
| `home` | Home (clear routing toggles) |
| `page_left` | Page ← (soft-key bank prev) |
| `page_right` | Page → (soft-key bank next) |

## DAW Layer keys

| Builtin | Display |
|---|---|
| `layer_select_1` | Layer 1 |
| `layer_select_2` | Layer 2 |
| `layer_select_3` | Layer 3 |

## SSL Soft-keys

| Builtin | Display |
|---|---|
| `softkey_bank_select` **(param: 0..5)** | Select soft-key bank (CS banks 0..5 or BC bank 0/1) |
| `ssl_softkey` **(param: 0..7)** | SSL Soft-Key cell N |
| `ssl_bank_vpot` **(param: 0..7)** | SSL V-Pot N (bank-current) |

## Send / Receive toggles

| Builtin | Display |
|---|---|
| `send_this` **(param: 0..7)** | Toggle Send view for slot N |
| `recv_this` **(param: 0..7)** | Toggle Receive view for slot N |

## Selection Sets

| Builtin | Display |
|---|---|
| `selset_recall` **(param: 1..8)** | Recall Selection Slot (toggle) |
| `selset_save` **(param: 1..8)** | Save current REAPER selection to slot |
| `selset_cycle` | Selection-Set Cycle (off → first populated → … → off) |

## View toggles

| Builtin | Display |
|---|---|
| `folder_mode` | Toggle Folder Mode (parents only) |
| `show_only_selected` | Toggle Show Only Selected |
| `mixer_toggle` | Open / Close Rea-Sixty Settings |

## Markers / Regions (Nav overlay)

| Builtin | Display |
|---|---|
| `marker_overlay_toggle` | Nav Mode (Markers & Regions): toggle |
| `marker_overlay_markers_only_toggle` | Nav Mode: Markers only (no drill) |
| `marker_overlay_regions_only_toggle` | Nav Mode: Regions only (no drill) |

## Track arming

| Builtin | Display |
|---|---|
| `tracks_arm_all` | Tracks: Arm All / Unarm All (toggle) |

## Brightness

| Builtin | Display |
|---|---|
| `brightness_leds_up` | Brightness LEDs + |
| `brightness_leds_down` | Brightness LEDs - |
| `brightness_lcds_up` | Brightness LCDs + |
| `brightness_lcds_down` | Brightness LCDs - |
| `brightness_both_up` | Brightness Both (LEDs+LCDs) + |
| `brightness_both_down` | Brightness Both (LEDs+LCDs) - |

## Zoom

| Builtin | Display |
|---|---|
| `zoom_up` | Zoom in vertically |
| `zoom_down` | Zoom out vertically |
| `zoom_left` | Zoom out horizontally |
| `zoom_right` | Zoom in horizontally |
| `zoom_center` | Zoom to fit project |

## Parameter Groups

| Builtin | Display |
|---|---|
| `param_group_remove_all` | Param Groups → Remove Selected from All |
| `multi_select_as_temp_group_toggle` | Param Groups → Multi-Select acts as Temp Group |

## Modifier keys

| Builtin | Display |
|---|---|
| `mod_shift` | Modifier: Shift / Fine (double-click latches) |
| `mod_cmd` | Modifier: Cmd |
| `mod_ctrl` | Modifier: Ctrl |

## Misc

| Builtin | Display |
|---|---|
| `flip` | Toggle FLIP (fader ↔ V-Pot) |
| `pan_force` | Toggle V-Pots → Pan |
| `__reaper_action__` | (internal — used by the REAPER Action picker, not directly bindable) |

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

- All 8 strips become the strips of a single FX-Learn-mapped plug-in
- Two fader banks (16 strips total for plug-ins that need more than 8 controls) — Bank ← / Bank → flips between A and B
- 8 soft-keys above each strip drive the FX Learn-mapped TopSoftKey slot per bank
- The active group of soft-key cells is the bank's "active TopSoftKey ring" — 1 of 8 ringed brightly
- Unassigned banks act as no-ops (LEDs dim)

The active plug-in is the focused track's first UF8-Mode-mapped instance, or the Instance the cursor is currently on.

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

Snapshot slots store GUIDs in REAPER's ProjExtState (per project). Group slots store: group index, project-scoped or global. The `Selection-Set auto-mode` value is project-global (ExtState).

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

## Show Only Selected

Toggle: `show_only_selected` builtin. When on, only currently-selected tracks appear on the surface. Live filter — changing REAPER selection updates the surface within one timer tick.

## Selection-Set filter

ANDs with Folder Mode / Show Only Selected. When a selset is active, only its tracks appear (further filtered by Folder Mode if also on).

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

\newpage

# Building this manual

This file is markdown intended for pandoc → PDF conversion.

```bash
# HTML preview
pandoc docs/user-manual.md -o user-manual.html --toc

# PDF (requires a LaTeX engine, e.g. tectonic or xelatex)
pandoc docs/user-manual.md -o user-manual.pdf \
    --toc \
    --pdf-engine=xelatex \
    -V mainfont="Helvetica" \
    -V monofont="Menlo"
```

The `\newpage` directives in the source force section breaks in the PDF output. For consistency across platforms, install pandoc + a LaTeX distribution (tectonic is the lightest).
