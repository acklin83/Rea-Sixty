# Rea-Sixty — User Manual

A REAPER extension that drives the SSL UF8 and UC1 controllers directly,
without SSL 360° and without an SSL plug-in on every track.

Every behavioural claim in this manual is annotated with a `file:line`
citation into the repository, so anything that turns out to be wrong can
be traced and fixed.

---

## 1. What this extension is

Rea-Sixty is a single REAPER extension (`reaper_uf8.dylib` on macOS,
`reaper_uf8.dll` on Windows once the port lands). When loaded, it:

- registers itself as a REAPER control surface (`csurf_inst`) — see
  `main.cpp:12380` and `main.cpp:4173-4176` (`g_csurfReg`);
- opens the UF8 over libusb (`UF8Device.cpp:22-26`, VID `0x31E9`, PID
  `0x0021`, EP `0x02` OUT / `0x81` IN);
- optionally opens the UC1 over libusb (`UC1Device.cpp:107`, PID
  `0x0023`) — absent UC1 is fine (`main.cpp:4133-4137`), absent UF8 is
  fine too (`main.cpp:4087-4092`);
- runs a periodic `onTimer()` on REAPER's timer service
  (`main.cpp:4139`, registered as `"timer"`);
- creates two virtual MIDI ports `reaper_uf8 in` / `reaper_uf8 out`
  (`MidiBridge.cpp:21-43`) — used internally for MCU-style LED feedback
  and as a legacy compatibility path for CSI users
  (`main.cpp:4069-4081`). **You do not need to wire these up in REAPER**
  — the extension uses them itself.

The extension also tries to find the UF8's native MCU MIDI port (search
string contains `UF8`, `MidiBridge.cpp:88`) and uses it for MCU
note-on/off LED feedback (`MidiBridge.cpp:106-...`). If no such port
exists, LED feedback degrades gracefully (`main.cpp:4076-4079`).

## 2. What works today (verified)

This section is exhaustive. If a feature is not listed, it is not yet
implemented in the code as of `main` at the time of writing.

### 2.1 UF8 — scribble-strip output

Every payload below is built in `Protocol.cpp` and pushed via libusb.

| Zone | Build function | Frame | Citation |
|---|---|---|---|
| Track colours (per-strip palette index) | `buildColorCommand` | `FF 66 09 18 <8 indices> CKSUM` | `Protocol.cpp:16-32` |
| Upper scribble text (track name) | `buildStripTextUpper` | `FF 66 <len> 0B <strip> <≤7 chars>` | `Protocol.cpp:34-54` |
| Lower scribble text (value line) | `buildStripTextLower` | `FF 66 09 0E <strip> <7 chars>` | `Protocol.cpp:496-512` |
| 19-char Value Line (PM-mode) | `buildValueLine` | `FF 66 15 0E <strip> <19 chars>` | `Protocol.cpp:183-193` |
| Channel Strip Type label | `buildChannelStripType` | `FF 66 <len> 17 <strip> <≥4 chars>` | `Protocol.cpp:155-181` |
| Channel Number Zone | `buildChannelNumber` | `FF 66 <len> 14 <strip> <digits>` | `Protocol.cpp:444-460` |
| V-Pot readout bar | `buildVPotReadoutBar` | `FF 66 11 0F <8 × 2-byte LE>` | `Protocol.cpp:462-477` |
| Fader dB readout | `buildFaderDbReadout` | `FF 66 0A 0C <strip> <"-3.4">"dB"` | `Protocol.cpp:479-494` |
| Selected-strip bitmask | `buildSelectedStripBitmask` | `FF 66 03 06 <lo> <hi>` | `Protocol.cpp:432-442` |

Colour push happens every tick if the bank or a track colour changed
(`ColorSync.cpp:29`, called from `onTimer` via `g_sync->refresh()`).
REAPER RGB is quantised to the UF8's 16-entry palette by
`Palette::quantize` (`Palette.cpp:82-99`); indices `0x00`, `0x05`,
`0x0D`, `0x0F` are currently unmapped and return `std::nullopt` from
`paletteEntry` (`Palette.h:32`).

### 2.2 UF8 — fader, motor, V-Pot

| Signal | Direction | Mechanism | Citation |
|---|---|---|---|
| Motor position (16-bit) | host→UF8 | `buildFaderPosition` (`FF 1E 03 <s> <lsb> <msb>`) | `Protocol.cpp:56-69` |
| Motor enable/disable | host→UF8 | `buildMotorEnable` (`FF 1D 02 <s> <0/1>`) | `Protocol.cpp:71-83` |
| Fader move | UF8→host | parsed in `onUf8Input`, routed via `CSurf_OnVolumeChange` with full 16-bit precision | `main.cpp:4316-4410` |
| Fader touch | UF8→host | capacitive debounce, answered via `IReaperControlSurface::GetTouchState` | `main.cpp:3801-3830` |
| V-Pot delta | UF8→host | dispatched through bindings | `main.cpp:4288` + bindings system |

The motor position settles on touch-release via `g_lastTouchPb`
(`main.cpp:4374-4375`) — i.e. the fader stops fighting your finger.

### 2.3 UF8 — LEDs

Per-strip SOLO / CUT / SEL LEDs use the colour-pair frame family
discovered in cap31 (`Protocol.cpp:356-388`). Cell formula:
`cell = 0x17 - 3*strip - led_offset` (`Protocol.cpp:368-369`). Bright /
dim colour tables for automation modes live in
`Protocol.cpp:218-239`. Track-colour → SEL-LED mapping is a 10-palette
Euclidean nearest-match (`Protocol.cpp:333-354`).

Top soft-keys: `buildTopSoftKeyLed` (`Protocol.cpp:390-430`). Global
buttons (40+, e.g. Layer / Page / Bank / Automation / Transport / Norm
/ Rec / PAN / Fine / Channel-Encoder push): `buildUf8GlobalLed`
(`Protocol.cpp:757-804`) with a 51-entry cell+colour table at
`Protocol.cpp:691-754`.

Master brightness for LEDs: `buildLedBrightness`
(`Protocol.cpp:532-559`). Scribble-LCD brightness:
`buildLcdBrightness` (`Protocol.cpp:561-574`).

On open, every strip LED is force-darkened so the firmware power-up
"everything lit" state doesn't leak into REAPER's idle state
(`main.cpp:4098-4106`).

### 2.4 UF8 — meters

VU meter pair: `buildVuMeter` (`Protocol.cpp:576-597`). GR row uses a
single byte per strip, stamped into the 13-byte `FF 66 09 15`
heartbeat (`UF8Device.cpp:298-359`). The single-byte builder exists
(`buildGrByte`, `Protocol.cpp:599-618`) but the canonical path is
heartbeat stamping, not standalone frames.

Per-strip meter format `FF 38 04` exists (`buildMeter`,
`Protocol.cpp:85-99`) but is marked semantically incomplete in the
header (`Protocol.h:74`).

### 2.5 UF8 — input

Every UF8 button arrives on EP `0x81` as
`FF 22 03 <id> 00 <state> CKSUM` (`Protocol.cpp:827-842`). The button
ID map is in `protocol-notes.md` lines 63–100 and is verified against
a physical UF8 (cap commit 2026-04-21).

Per-strip buttons (Sel, Cut, Solo, V-Pot push, top soft-key) are
dispatched in `main.cpp:4419-4448`. Global buttons go through
`uf8::bindings::dispatch()` (`main.cpp:4411-4450`) — i.e. they are
**user-bindable**, not hardcoded.

### 2.6 UF8 — init sequence

The UF8 firmware needs a handshake before it renders anything. Stored
as a captured byte blob in `extension/src/init_sequence.inc` and
replayed in `UF8Device.cpp:126-240`, with load-bearing inter-phase
delays. A 371-frame Plugin-Mixer-layer replay
(`layer_plugin_mixer.inc`, `UF8Device.cpp:208-218`) is fired when
switching into PM-mode. Heartbeat pattern after init:

- 64-byte pair `FF 66 21 09 / 0A` at ~50 Hz (`UF8Device.cpp:293-294`)
- 13-byte pair `FF 66 09 15 / 16` at ~50 Hz, PM-mode only — `15` carries
  live per-strip GR bytes (`UF8Device.cpp:298-299`)
- `FF 5B 02 00 00 5D` liveness frame at ~50 Hz (`UF8Device.cpp:304`)
- `FF 1B 01 <0..3>` PM-mode keepalive at 150 ms cadence
  (`UF8Device.cpp:378-384`)

### 2.7 UC1 — what gets driven

UC1 is registered as a parallel surface (`uc1::UC1Surface`), instantiated
in `main.cpp:92` and attached to the device in `main.cpp:4126-4132`.
It follows track selection: knob turns update `focusedTrack_` via
`setFocusedTrack` (`UC1Surface.h:65`), and on attach it focuses
REAPER's last-touched track (`main.cpp:4130-4131`).

UC1 reads parameters from the focused track's SSL plug-in and writes
them back through `TrackFX_SetParamNormalized`. Parameter routing
tables per plug-in variant live in `UC1PluginMap.cpp`:

- Channel Strip 2 — 12 EQ knobs (LP, HP, four bands × {freq, gain, Q}),
  7 dynamics knobs, 10 buttons (`UC1PluginMap.cpp:102-151`).
- Bus Compressor 2 — 7 top V-Pots (Threshold / Makeup / Attack /
  Release / Ratio / HPF / Mix) + IN (`UC1PluginMap.cpp:42-57`).
- 4K G / 4K E / 4K B variants — same layout, different VST3 param
  indices (and without CS2's inversions, `UC1PluginMap.cpp:93-100`).
- SSL 360° Link wrappers — recognised for both CS and BC domains
  (`PluginMap.cpp:374-378`, BC variant matched first so the
  substring "SSL 360 Link" doesn't shadow it).

GR meter pipeline: read `GainReduction_dB` via
`TrackFX_GetNamedConfigParm` (`main.cpp:8934-8947`,
`UC1Surface.cpp:2688`). For user-mapped plug-ins, a VST3 param index
chosen in the FX-Learn tab is used instead (`UC1PluginMap.cpp:83`,
`UserPluginCatalog.h:32-48`).

UC1 USB protocol: same FF-magic + checksum frame format
(`UC1Protocol.cpp:9-22`). Distinct commands include:

- `FF 5B 02 <hi> <lo>` — GR meter at 50 Hz, 1/10 dB units, 0..20 dB
  (`UC1Protocol.cpp:50-61`)
- `FF 13 04 …` family — LEDs, VU meters, segment digits
  (`UC1Protocol.cpp:74-205`)
- `FF 66 …` family — display zones (header, sub-header, value, unit,
  context blocks for CS / BC, round indicator, preset carousel)
  (`UC1Protocol.cpp:207-461`)
- Knob events `FF 24 02 <id> <delta>` with 6-bit signed deltas
  (`UC1Protocol.cpp:273-295`)

### 2.8 Bank navigation

8 visible strips, offset by `g_bankOffset` (`main.cpp:198`). Bank-left
and bank-right are registered as builtin actions and wired through the
bindings system:

- `bank_left` (`main.cpp:11971`)
- `bank_right` (`main.cpp:11984`)

Selection-follows-track works through the `SetSurfaceSelected` callback
(`main.cpp:4025-4045`) — clicking a track in REAPER updates UF8/UC1
state.

### 2.9 Folder Mode

Toggled by the `folder_mode` action (`main.cpp:11288`). State is
`g_folderMode` (`main.cpp:275`). When on, the bank fills with parent
tracks only; children expand on demand (`main.cpp:427-440`). Page
indices update so bank-snap stays sensible (`main.cpp:2253`).

### 2.10 Plug-in Mixer mode

When the focused track has a recognised SSL plug-in, the UF8 enters
PM-mode and shows colour bars, value lines, channel-strip-type
labels, V-Pot readout arcs. Plug-in recognition is substring match on
the FX name (`PluginMap.cpp:368-390`):

```
"Channel Strip 2"   → CS2          (Domain::ChannelStrip)
"4K G" / "4K E" / "4K B" → 4K variants
"Bus Compressor 2"  → BC2          (Domain::BusComp)
"SSL 360 Link Bus Compressor" → wrapper BC
"SSL 360 Link"      → wrapper CS   (matched after BC variant)
```

Multi-instance handling: `lookupPluginOnTrack(track, domain)`
(`PluginMap.cpp:439-466`) picks the Nth match via the
`InstanceIdxFn` provider, so cycling between two CS2 instances on a
single track works.

A/B compare and HQ (CS-only) are exposed through
`PluginChunkPatch` (`PluginChunkPatch.cpp:78-129`), which edits the
VST3 state chunk directly because those flags aren't reachable via the
normal `TrackFX_*` API.

The encoder "virtual notch" snap (faders/V-Pots cling to neutral —
0 dB on gains, centre on pan) is implemented in `VirtualNotch.h:6-10`.

## 3. Settings window

There is a single dockable ImGui window. Toggle action:

- **`REASIXTY_TOGGLE_SETTINGS`** — display name *"Rea-Sixty: Open /
  Close Rea-Sixty Settings"* (`main.cpp:9825`).

Six tabs in the left rail (`MixerWindow.cpp:39-46`):

1. **Device** (`drawDevice`, `SettingsScreen.cpp:149`) — UF8/UC1
   connection status with serial number, Identify button, LED
   brightness, scribble-LCD brightness, GR-meter-source toggle,
   track-select-follows-param toggle, plug-in-GUI-follows-instance,
   plug-in-GUI pin-position capture.
2. **Bindings** (`drawBindings`, `SettingsScreen.cpp:3737`) — full
   REAPER action picker, layer export/import via macOS NSSavePanel,
   custom action mapping.
3. **Modes** (`drawModes`, `SettingsScreen.cpp:7381`).
4. **FX Learn** (`drawFxLearn`, `SettingsScreen.cpp:7008`) — plug-in
   discovery, per-parameter binding to UF8/UC1 controls.
5. **Selection Sets** (`drawSelectionSets`, `SettingsScreen.cpp:7485`).
6. **About** (`drawAbout`, `SettingsScreen.cpp:7496`).

Themes follow REAPER's active theme via `ThemeBridge`
(`ThemeBridge.cpp:25-28`, indexed `GetColorTheme` API), with derived
Hovered/Active variants (`ThemeBridge.cpp:34-46`) and `pushAll/popAll`
per frame.

> **There is no on-screen Plug-in Mixer view yet.** `MixerLayout.cpp`
> is a scaffold with placeholder text only (`MixerLayout.cpp:7-9`).
> The Settings window is the only ImGui content currently rendered.
> Phase 2.6 of `ROADMAP.md` is the work to fill that in.

## 4. Bindings system

JSON file location (`Bindings.cpp:8-10`):

- macOS: `~/Library/Application Support/REAPER/rea_sixty/bindings.json`
- Windows: `%APPDATA%/REAPER/rea_sixty/bindings.json`

Layout (`Bindings.h:289-343`):

- **3 layers**, switched via UF8's left-side Layer buttons (button IDs
  `0x40 / 0x41 / 0x42`, `protocol-notes.md`).
- Per layer: **3 Quick slots** (Q1/Q2/Q3) with **6 sub-banks** each
  (V-POT + Soft 1..5) = 144 user-fillable slots per layer.
- **Modifier matrix** per binding: Plain / Shift / Cmd / Ctrl, short or
  long press (`Bindings.h:270-280`).
- Per-strip / transport / global / soft-keys all bindable.

**Bank Presets** — named binding-snapshots with snapshot / recall /
rename / delete API (`Bindings.h:335-338, 499-507`).

Persistence: `loadJson` / `saveJson` (`Bindings.h:397-400`),
import / export (`Bindings.h:407-408`).

### Registered builtins (callable from a binding)

88 builtins are registered through `registerBuiltin`
(`main.cpp:10923-12290`). The complete list — abridged here for the
manual; see the source for argument semantics:

- **Modifiers**: `mod_shift`, `mod_cmd`, `mod_ctrl`.
- **Bank / page / nav**: `bank_left`, `bank_right`, `page_left`,
  `page_right`, `home`, `zoom_up`, `zoom_down`, `zoom_left`,
  `zoom_right`, `zoom_center`.
- **Encoder modes** (UF8 Channel Encoder): `encoder_nav`,
  `encoder_nudge`, `encoder_focus`, `encoder_instance`,
  `encoder_fx_cycle`, `encoder_mode_dispatch`.
- **Selection modes**: `selection_mode_norm`,
  `selection_clear_all`, `select_relative`.
- **Tracks / global**: `tracks_arm_all`, `automation_zero_all`,
  `playhead_nudge`, `mouse_scroll`.
- **Plug-in cycling**: `instance_cycle`, `instance_next`,
  `instance_prev`, `fx_cycle`, `bc_track_scroll`.
- **Strip modes**: `ssl_strip_mode_toggle`,
  `ssl_strip_mode_toggle_with_gui`, `uf8_plugin_mode_toggle`,
  `uf8_plugin_mode_toggle_with_gui`.
- **Plug-in control**: `show_focused_plugin_gui`, `plugin_bypass`,
  `plugin_offline`, `plugin_preset_next`, `plugin_preset_prev`,
  `plugin_preset_cycle`, `show_fx_chain`.
- **Display**: `brightness_leds_up/down`, `brightness_lcds_up/down`,
  `brightness_both_up/down`.
- **Surface filters**: `folder_mode`, `show_only_selected`,
  `selset_recall` (slot 1..8 spinner).
- **Other**: `mixer_toggle`, `pan_force`, `flip`, `domain_cs`,
  `domain_bc`, `softkey_bank_select`, `send_all_1..8`, `send_this`,
  `recv_this`, `ssl_softkey`, `ssl_bank_vpot`.
- **Automation**: `auto_off / read / write / trim / latch / latch_prv
  / touch` + matching `*_global` variants (`main.cpp:11919-11968`).

Plus an escape hatch: a binding may invoke any REAPER action by ID via
the `__reaper_action__` builtin (`main.cpp:10929`).

## 5. FX Learn

Schema in `UserPluginCatalog.h:146-164` (`UserPluginMap`):

- `match` — FX-name substring (**not** a VST3 GUID; lookup is
  substring-match per call to `TrackFX_GetFXName`,
  `UserPluginCatalog.cpp:567-589`).
- `domain` — ChannelStrip / BusComp / None.
- `uf8Mode` — when true, all 8 UF8 strips drive this plug-in's params.
- Per-slot VST3 param index + inverted flag.
- Optional metering overrides: `grVst3Param`, `grOffsetDb`, two
  calibration tables (`grBcVuCalDb[6]`, `grLedsCalDb[5]`,
  `UserPluginCatalog.h:32-48`, schema v5).
- Optional UF8 bank/strip bindings.

Storage: `<REAPER_RESOURCE>/rea_sixty/user_plugins.json`
(`UserPluginCatalog.cpp:65`), written via atomic `.tmp` + rename
(`UserPluginCatalog.cpp:100-128`).

> **Caveat re. the README claim "GUID-keyed so FX-slot reorders don't
> break mappings":** the code does not key on VST3 GUIDs. It keys on
> FX-name substring match, performed each time. Reorders are safe
> because the match is name-based; **renames break mappings**, not
> reorders.

## 6. GR meter pipeline (what actually runs)

Source of truth in three places, all using
`TrackFX_GetNamedConfigParm(tr, fx, "GainReduction_dB", …)`:

- `main.cpp:8934-8947` — the UF8 GR row poll, stamped into the PM-mode
  heartbeat.
- `UC1Surface.cpp:2688` — the UC1 GR meter for the focused BC.
- `UC1Surface.cpp:2734` — the CS-combined Comp+Gate poll.

Calibration uses piecewise-linear interpolation against the breakpoint
tables in `GrCalibration.h:14-51` (`kBcVuBpDb` at 0/4/8/12/16/20 dB,
`kLedsBpDb` at 3/6/10/14/20 dB), applied via `applyGrCalibration`
(`GrCalibration.h:34-51`).

The JSFX probe `extension/jsfx/rea_sixty_gr_probe.jsfx` exists as a
fallback for compressors that don't expose `GainReduction_dB`. It is
**not** auto-inserted by the extension — no reference to it exists in
`extension/src/`. Manual install if you need it; the in-tree comment
documents pin routing (lines 9-15 of the JSFX file).

## 7. Diagnostic actions

Registered via `gaccel_register`, all prefixed `REASIXTY_*`
(`main.cpp:9353-9825`):

| Action ID | Purpose |
|---|---|
| `REASIXTY_BRIGHTNESS_UP` / `_DOWN` | LED + scribble brightness (`main.cpp:9353/9356`) |
| `REASIXTY_PROBE_LED` | Walk global LED cells one at a time (`main.cpp:9428`) |
| `REASIXTY_PROBE_LEGACY_LED` | Same for legacy monochrome cells (`main.cpp:9477`) |
| `REASIXTY_FRAME_TRACE` | Log host→UF8 frames to `/tmp` (`main.cpp:9504`) |
| `REASIXTY_TOGGLE_FADER_CAL` | Toggle the fader-calibration diagnostic (`main.cpp:9519`) |
| `REASIXTY_TOGGLE_FADER_INPUT_LOG` | Log every fader event (`main.cpp:9574`) |
| `REASIXTY_UC1_TEST_INDICATOR` | Drive UC1 round indicator (`main.cpp:9611`) |
| `REASIXTY_PROBE_GATE_GR` | Probe CS Gate GR via candidate named-config-parm strings (`main.cpp:9677`) |
| `REASIXTY_DUMP_CS_PARAMS` | Dump CS plug-in's param table to log (`main.cpp:9723`) |
| `REASIXTY_DUMP_CS_CHUNK` | Dump CS plug-in's VST3 state chunk (`main.cpp:9765`) |
| `REASIXTY_DUMP_ROUTING_FLAGS` | Dump UC1 routing-zone byte (`main.cpp:9815`) |
| `REASIXTY_TOGGLE_SETTINGS` | Open / close the Settings window (`main.cpp:9825`) |

The MidiBridge writes a list of every CoreMIDI destination it sees to
`/tmp/reaper_uf8_midi_dests.log` on open (`MidiBridge.cpp:70-91`) —
useful when the UF8 MCU port isn't being picked up.

Surface bring-up logs to REAPER's Console (View → Console). Look for
`Rea-Sixty UF8:` / `Rea-Sixty UC1:` prefixes
(`main.cpp:4090, 4134`).

## 8. Installation

### macOS

Prerequisites (`install-macos.md`, file is stale — first three points
still apply; the "Tracks 1..8 only" / "palette incomplete" disclaimers
in that file are out of date):

1. `brew install libusb`
2. REAPER (recent build)
3. UF8 plugged in, **SSL 360° must be quit** — it claims the UF8
   vendor interface exclusively (no shared open).
4. **ReaImGui extension installed in REAPER** (via ReaPack) — the
   Settings window resolves ReaImGui exports lazily through
   `plugin_getapi`; without ReaImGui the toggle action will not show
   a window. The extension still drives UF8/UC1 in that case.

Build:

```bash
cd extension
cmake -B build -G "Unix Makefiles"
cmake --build build -j$(sysctl -n hw.ncpu)
```

Install:

```bash
ln -sf "$PWD/build/reaper_uf8.dylib" \
       ~/Library/Application\ Support/REAPER/UserPlugins/reaper_uf8.dylib
```

Restart REAPER. UF8 scribble strips should show REAPER track colours
within a tick.

### Windows / Linux

Build system is generic CMake with `PkgConfig::LIBUSB` and
`PkgConfig::HIDAPI` (`CMakeLists.txt:96, 113, 140, 149`).
macOS-specific files (`macos_save_dialog.mm`,
`macos_pin_fx_gui.mm`, `MidiBridge.cpp` macOS branch) are isolated
behind `__APPLE__` / `if(APPLE)` guards (`CMakeLists.txt:81-104`,
`MidiBridge.cpp:7, 162-174`). Untested in current state, see Phase 4
of `ROADMAP.md`.

## 9. Build artefacts

| Artefact | Source | Purpose |
|---|---|---|
| `reaper_uf8.dylib` / `.dll` | 27 source files (`CMakeLists.txt:55-78`) | The extension |
| `test_protocol` | `tests/test_protocol.cpp` | UF8 frame format + checksum + button-event parse — no libusb, no REAPER |
| `test_uc1_protocol` | `tests/test_uc1_protocol.cpp` | Same for UC1 |
| `uf8_color_test` | `tools/uf8_color_test.cpp` | Standalone CLI — opens UF8 via libusb, pushes test colours. Useful for "does the hardware respond at all" |
| `uf8_palette_probe` | `tools/uf8_palette_probe.cpp` | Interactive palette calibration helper |
| `uf8_hid_probe` | `tools/uf8_hid_probe.cpp` | Dumps raw HID reports from PID `0x0022` (the UF8's HID controller sibling) |

Note: `HidDevice.cpp/h` exists in `extension/src/` and is compiled
into the extension (`CMakeLists.txt`), but `g_hid` is never
constructed or opened in `main.cpp` (only declared,
`main.cpp:85`). The HID device path is **not** currently used at
runtime — input arrives via the vendor-USB endpoint. The HID class is
exercised only by the `uf8_hid_probe` standalone tool.

## 10. Coexistence with SSL 360°

The UF8 vendor-USB interface is exclusive. While SSL 360° (or its
background daemon `SSL360Core`) is running, `libusb_open_device` will
fail and the extension logs `SSL360Core owns the device` to the REAPER
Console (`main.cpp:4090`). Quit SSL 360° (including the background
daemon) and reload the extension. The same applies in reverse: while
Rea-Sixty is loaded, SSL 360° will not be able to claim the UF8.

The UC1 path has the same exclusivity (`UC1Device.cpp:115-126`
kernel-driver detach and interface claim).

## 11. What is *not* implemented (don't expect it from main)

The README and ROADMAP describe several features that are not in the
shipped code at the time of writing. Listed here so the manual is
honest about scope:

- **On-screen Plug-in Mixer view.** `MixerLayout.cpp:7-9` is a
  placeholder. Settings tabs are complete; the Mixer view is not.
- **Selection-set storage.** `selset_recall` writes
  `g_selsetActive` but does not yet pull GUID lists from
  `SetProjExtState` (`main.cpp:11331-11332` is marked `TODO Phase 2.5b`).
  The action exists, the UI tab exists, the storage step is open.
- **Auto-insert of the GR JSFX probe.** No call site in
  `extension/src/`; the probe ships as a separate JSFX file the user
  installs by hand if needed.
- **Foot-switch bindings.** UF8 has two foot-switch jacks. No USB
  event for them is parsed in `Protocol.cpp`.
- **GUID-keyed FX Learn bindings.** Bindings match plug-ins by FX-name
  substring, not by VST3 GUID. Rename-safety is therefore weaker than
  the README implies.
- **Cross-platform build.** Code compiles to a `.dll`/`.so` in
  principle; not currently smoke-tested on Windows or Linux.
- **In-app firmware update for UF8 / UC1.** Out of scope by design —
  SSL still ships firmware blobs through SSL 360°.

## 12. Where to look in the repo

- Protocol decoding notes — `docs/protocol-notes.md`,
  `docs/protocol-notes-uc1.md`
- Architecture decision (no CSI / no virtual MCU) —
  `docs/architecture-decision.md`
- Legal basis — `docs/interop-rationale.md`
- Bindings spec — `docs/bindings.md`
- Concepts (FX vs Instance, V-Pot cycles) — `docs/concepts.md`
- Settings UI plan — `docs/plan-settings-ui.md`
- USB capture workflow — `docs/windows-capture-workflow.md`
- Roadmap and phase plan — `ROADMAP.md`
- This manual lives at `docs/user-manual.md`.
