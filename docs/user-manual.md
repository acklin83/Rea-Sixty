# Rea-Sixty User Manual

Native REAPER ↔ SSL UF8 / UC1 driver. Replaces SSL 360° on the host
side.

Companion documents in this repository: `architecture-decision.md`
(why no CSI, no virtual MCU), `interop-rationale.md` (legal basis),
`uf8-manual-reference.md` (extract from SSL's own UF8 guide that this
manual mirrors in structure).

---

## 1. Introduction

### 1.1 What Rea-Sixty is

Rea-Sixty is a REAPER extension that drives the SSL UF8 and UC1
controllers directly from REAPER. It replaces SSL 360° on the
host side. You install one extension file; SSL 360° is no longer
required, the surface is no longer behind a virtual MIDI port, and
the per-track SSL plug-in that SSL 360° needs for track colours is no
longer required.

What appears on the wire is the same byte protocol SSL 360° uses,
re-emitted by REAPER. The legal basis for this is documented in
`interop-rationale.md`; no SSL binaries, firmware, or trademarks are
redistributed.

### 1.2 What you need

- A REAPER installation (recent build, macOS).
- An SSL UF8 plugged in over USB-C. UC1 is supported optionally; a
  UF8-only or UC1-only rig is fine.
- `libusb` from Homebrew (`brew install libusb`).
- The **ReaImGui** extension installed in REAPER via ReaPack. Without
  ReaImGui, hardware control still works but the on-screen Settings
  window will not appear.
- **SSL 360° must not be running.** It claims the UF8 vendor
  interface exclusively. If it is running when Rea-Sixty starts,
  REAPER's Console will show an error and the surface will not
  appear.

### 1.3 What this version does

A full inventory is in chapter 11. Headline features:

- DAW-layer track colours on the scribble strips — every REAPER
  track shows its assigned colour, with no per-track plug-in.
- Faders at full 16-bit resolution.
- Every UF8 button is routable to a REAPER action or a built-in
  Rea-Sixty behaviour through the Bindings system.
- UC1 follows the focused REAPER track and drives the SSL Channel
  Strip 2 / Bus Compressor 2 / 4K series / 360° Link plug-in on
  that track.
- A dockable Settings window with six tabs.

Chapter 13 lists what is **not** yet implemented, including a few
items that the project's README and ROADMAP currently overstate.

---

## 2. Installation

Rea-Sixty ships as a REAPER extension binary plus two runtime
dependencies (libusb and hidapi). The binary is the same C++ on both
platforms; what differs is how each OS exposes the UF8/UC1 USB
endpoints to user-space libusb.

### 2.1 Prerequisites (both platforms)

1. **REAPER** installed (recent build).
2. **ReaImGui** installed via ReaPack — Extensions → ReaPack → Browse
   packages → ReaImGui → Install. Without ReaImGui the Surface still
   registers and hardware bindings work, but the on-screen Settings
   window stays empty.
3. **UF8 and/or UC1** connected over USB-C. A UF8-only or UC1-only
   rig is fine.
4. **SSL 360° not running.** It owns the device's vendor-USB
   interface exclusively; Rea-Sixty can't open it while 360° is up.
   On macOS this means quit the `SSL360Core` background daemon too.
   On Windows it means SSL 360°'s kernel driver has to be replaced
   (Section 2.3.3).

### 2.2 macOS

#### 2.2.1 Library deps

```
brew install libusb hidapi
```

#### 2.2.2 Build

```
cd extension
cmake -B build -G "Unix Makefiles"
cmake --build build -j$(sysctl -n hw.ncpu)
```

This produces `build/reaper_rea-sixty.dylib`.

#### 2.2.3 Install

Symlink (recommended for development — rebuilds are picked up after a
REAPER restart):

```
ln -sf "$PWD/build/reaper_rea-sixty.dylib" \
       ~/Library/Application\ Support/REAPER/UserPlugins/reaper_rea-sixty.dylib
```

Or copy:

```
cp build/reaper_rea-sixty.dylib \
   ~/Library/Application\ Support/REAPER/UserPlugins/
```

Quit SSL 360° + the `SSL360Core` daemon, then restart REAPER. On
success, the UF8 scribble strips show REAPER track colours within
one display tick. On failure, REAPER → View → Console shows a line
beginning with `Rea-Sixty UF8:` or `Rea-Sixty UC1:` — see chapter 10.

### 2.3 Windows

Windows binds a kernel driver to the UF8/UC1 vendor interface; libusb
can only claim the device when that driver is **WinUSB** (or
libusbk / libusb-win32). SSL 360° ships its own driver (`SSLBUS`),
which is mutually exclusive. Rea-Sixty includes an in-product driver
swap so users don't need Zadig — see 2.3.3.

#### 2.3.1 Library deps

Drop `libusb-1.0.dll` and `hidapi.dll` next to **`reaper.exe`** (e.g.
`C:\Program Files\REAPER (x64)\`). REAPER's DLL search path doesn't
include `UserPlugins\`, so the deps have to live alongside REAPER
itself. Sources:

- `libusb-1.0.dll` — libusb [GitHub release](https://github.com/libusb/libusb/releases),
  `VS2022/MS64/dll/libusb-1.0.dll` from the libusb-1.0.x.7z archive.
- `hidapi.dll` — hidapi [GitHub release](https://github.com/libusb/hidapi/releases),
  `x64/hidapi.dll` from `hidapi-win.zip`.

A signed Microsoft Visual C++ Runtime 2015-2022 must be present (it
usually is on Win10/11; if `vcruntime140.dll` is missing, install the
[VC++ Redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe)).

#### 2.3.2 Install the DLL

Drop `reaper_rea-sixty.dll` into your REAPER user profile:

```
%APPDATA%\REAPER\UserPlugins\reaper_rea-sixty.dll
```

(Typically `C:\Users\<your-user>\AppData\Roaming\REAPER\UserPlugins\`.)

Restart REAPER. Open Action List (`?`) → search **"Rea-Sixty"** →
**"Rea-Sixty: Open / Close Rea-Sixty Settings"** should appear. If it
doesn't, the DLL didn't load — most often `libusb-1.0.dll` or
`hidapi.dll` aren't next to `reaper.exe`, or VC++ Runtime is missing.

#### 2.3.3 WinUSB driver swap (one-time)

The first time you launch REAPER with Rea-Sixty, the UF8/UC1 still
belong to SSL 360°'s kernel driver and libusb can't open them. To
hand them over:

1. In REAPER, run the action **"Rea-Sixty: Open / Close Rea-Sixty
   Settings"**.
2. Settings → **About** tab → scroll to **"Windows USB driver"**.
3. Click **"Install UF8/UC1 WinUSB driver"**.
4. A UAC prompt opens — accept.
5. Windows shows a **"Publisher unknown"** warning (the INF is
   unsigned; this is the same friction as Zadig). Click **"Install
   anyway"**.
6. Unplug + replug both UF8 and UC1 so Windows re-enumerates them
   onto the new WinUSB driver.
7. Restart REAPER. The surface should come up.

**Reverting:** SSL 360° will no longer see the devices after the
swap. Re-running SSL 360°'s installer reinstalls its driver and
takes the devices back; Rea-Sixty then can't see them until you
re-run the WinUSB install. The two are mutually exclusive at the
driver level — that's a Windows USB-stack constraint, not a Rea-Sixty
design choice.

### 2.4 Uninstall

- macOS: delete `~/Library/Application Support/REAPER/UserPlugins/reaper_rea-sixty.dylib`,
  restart REAPER.
- Windows: delete `%APPDATA%\REAPER\UserPlugins\reaper_rea-sixty.dll`,
  restart REAPER. The WinUSB driver entry persists — re-run SSL 360°'s
  installer to fully revert the device binding.

---

## 3. Quick start

Five minutes from install to working hardware.

1. Open REAPER's Console (View → Console). You should see no
   messages. If you see `Rea-Sixty UF8: …` or `Rea-Sixty UC1: …`, see
   chapter 10.

2. Add a few tracks in REAPER and colour them. The first 8 appear on
   the UF8 scribble strips with their colours rendered to the UF8's
   16-colour palette.

3. Move a fader. REAPER track volume follows. Release the fader; the
   motor settles to REAPER's authoritative value.

4. Press SEL on a strip. REAPER selects that track. If UC1 is
   connected and the track has an SSL plug-in, UC1's controls
   immediately reflect that plug-in's parameters.

5. Press the **360°** key. The Rea-Sixty Settings window opens.
   Press 360° again to close it.

6. Press **BANK ▶**. The 8 strips shift by 8 tracks. **BANK ◀**
   shifts back.

---

## 4. Hardware tour

### 4.1 Per-strip controls

Eight identical strips, left-to-right.

**100 mm motorised touch fader.** Touch-capacitive: REAPER receives a
touch event when you grip the fader cap, so automation modes that
distinguish touched from untouched faders behave correctly. Movement
sends track volume at full 16-bit resolution (not the 14-bit cap MCU
imposes). When you let go, the motor settles to REAPER's value, even
if REAPER has moved during your touch.

**V-Pot (push-encoder above the fader).** Rotation produces a signed
delta routed through the Bindings system. The push action is a
separate button. What the V-Pot controls depends on the active
Selection Mode and page; see chapter 5.

**Colour TFT scribble strip.** Multiple zones:

- A coloured top band carrying the REAPER track colour.
- The track name on the upper text line.
- A short value line below.
- In Plug-in Mixer Mode (chapter 5.4), additional zones appear:
  Channel-Strip-Type label, parameter name + value, fader dB readout,
  V-Pot readout bar, channel number with folder sigils when Folder
  Mode is on.

Rea-Sixty maps REAPER's RGB track colour to the UF8's fixed 16-entry
palette by closest match. A small number of palette indices are not
yet calibrated, so off-palette REAPER colours snap to the nearest
calibrated neighbour.

**SOLO / CUT / SEL keys.** Three illuminated keys per strip. LEDs
follow REAPER's solo / mute / select / arm state, with dim and bright
variants for automation modes. The SEL key colour optionally follows
the track colour. On extension start every per-strip LED is force-
darkened so the firmware power-up "everything lit" state does not
leak into your idle session.

**Top soft-key (above the scribble).** Per-strip; user-bindable.

### 4.2 Bank navigation

**BANK ◀ / BANK ▶.** Shift the 8-track window in steps of 8. Banking
is REAPER-track-list ordered.

**PAGE ◀ / PAGE ▶.** Page within bank where applicable.

**HOME.** Return to bank 0.

### 4.3 Channel encoder

The large notched encoder on the right has six bindable modes:

| Mode | Effect |
|---|---|
| Standard | Bank ±1 (single-track shift) |
| NAV | Transport scrub |
| NUDGE | Playhead nudge |
| FOCUS | Mouse-wheel emulation |
| Instance Cycle | Cycle through SSL Instances on the focused track |
| FX Cycle | Cycle through every FX on the focused track |

See `concepts.md` for the FX vs. Instance distinction.

### 4.4 Layer keys

**LAYER 1 / 2 / 3** on the left edge. Each layer is an independent
bindings map. Switching layer recalls a different set of soft-key
assignments, modifier behaviours, and V-Pot defaults. You can also
copy and recall named Bank Presets within a layer.

### 4.5 Soft-key column

The top row of buttons above the scribble strips and the right-hand
soft-key cluster are all bindable. Each binding slot supports four
modifier variants (Plain / Shift / Cmd / Ctrl) and short- or long-
press behaviour.

### 4.6 SEND / PLUGIN row

Eight keys, default-bindable to per-strip sends 1..8, or to a single
"Send Layer" view, or to plug-in routing — see chapter 8 for the
full builtin list.

### 4.7 Selection Mode block — NORM / REC / AUTO

The NORM, REC and AUTO hardware keys are bindable like any other
button. Under Rea-Sixty they default to switching the strip-wide
**Selection Mode**, a mutually-exclusive state that re-targets what
the SEL row of LEDs shows and what the V-Pots do across all eight
strips. There are six Selection Modes — see chapter 5.7 for the
full list and chapter 5.8 for the REC + RME deep dive.

Under stock SSL 360° + REAPER MCU, AUTO is Pro-Tools-only and the
OFF automation state has no effect. Under Rea-Sixty all six REAPER
automation modes are reachable directly.

### 4.8 Transport keys

Bound to REAPER's standard transport actions (Play, Stop, Record,
Rewind, Fast-Forward, Cycle). Rebindable.

### 4.9 Zoom / nav cluster

Five keys mapping to REAPER's zoom-vertical-in/out, zoom-horizontal-
in/out, and zoom-to-fit-project actions.

### 4.10 360° key

Opens and closes the Rea-Sixty Settings window. Rebindable.

### 4.11 PLUGIN / CHANNEL keys

**PLUGIN** toggles UF8 Plugin Mode (V-Pots drive plug-in parameters)
and SSL Strip Mode (the focused track's Channel Strip Instance takes
over the strip layout). Each has a "with GUI" variant that also
opens the plug-in's own window.

**CHANNEL** flips the focused domain between Channel Strip and Bus
Compressor.

### 4.12 FLIP / PAN / FINE

- **FLIP** swaps fader and V-Pot assignment.
- **PAN** forces V-Pots into pan mode.
- **FINE / SHIFT** doubles as the Shift modifier — any binding with a
  Shift variant fires while you hold this key.

### 4.13 Foot-switch jacks

Two 1/4" jacks on the back labelled FS1 / FS2. **Foot-switch input
is not yet decoded in this version.** Plugging in a foot-switch
produces no action. The jacks themselves still receive the SSL 360°
factory function ("Play Footswitch" / "Record Footswitch") if you
load SSL 360°; under Rea-Sixty alone, they are inert.

---

## 5. Modes

### 5.1 DAW Mode

The default. Eight strips map to a contiguous window of REAPER
tracks. Fader = volume. V-Pot = pan unless reassigned through a
binding. Scribble shows track colour and name.

### 5.2 Folder Mode

Triggered by binding a key to the Folder Mode action. When on, only
parent (top-level) tracks fill the bank; children are hidden until
expanded.

### 5.3 Show Only Selected

A complementary mode that filters the bank to currently-selected
REAPER tracks.

### 5.4 Plug-in Mixer Mode — Channel Strip

When the focused track hosts a recognised SSL Channel Strip
Instance — Channel Strip 2, 4K B, 4K E, 4K G, or a 360° Link
wrapping one of those — the LCD layout switches:

| Position | Field |
|---|---|
| Top zone | Soft-key label — which plug-in parameter the V-Pots drive |
| Below | Channel-Strip-Type label ("CS 2", "4K G", …) |
| Below | DAW track colour |
| Below | Track name |
| Below | O/PdB fader readout |
| Below | Selected parameter name and value |
| Bottom | V-Pot readout bar |

Track colour is always shown in Plug-in Mixer Mode under Rea-Sixty;
SSL 360°'s "Plug-in Mixer UF8/UF1 SEL Keys" toggle is not needed.

The PLUGIN key inside Plug-in Mixer Mode toggles whether the fader
and pan address the plug-in or the DAW track.

A/B compare and HQ-mode toggles for Channel Strip plug-ins are
exposed as bindable actions; both are surfaced safely (changing them
does not cause a state-reload click).

### 5.5 Plug-in Mixer Mode — Bus Compressor

When the focused track hosts Bus Compressor 2 or a 360° Link Bus
Compressor wrapper, the layout shifts to: track position, name,
MAKE-UPdB readout, GR meter, selected parameter and value, V-Pot
readout bar.

### 5.6 Hybrid Mode

When a layer is set to Plug-in Mixer, cursor keys and automation
keys still drive transport and automation as in DAW mode. SSL's
documentation flags REAPER as "seamless" for Hybrid Mode (no bank
desync between the two banks). Rea-Sixty preserves this because
there is only one bank, shared across modes.

### 5.7 Selection Modes

A Selection Mode is a global, surface-wide setting that re-targets
the SEL row and the V-Pots simultaneously. Only one Selection Mode
is active at a time. Switch between them by binding the
`selection_mode_*` builtins to keys — the SSL NORM / REC / AUTO
block is the natural home, but any key works.

| Mode | SEL row | V-Pot push | V-Pot rotation |
|---|---|---|---|
| **Norm** | Track selection (white, or follows track colour) | Track select | Pan (or whatever the layer default is) |
| **REC** | Record-arm state (dim red / bright red) | Toggle `I_RECARM` | Pan |
| **REC + MON** | Same as REC | Toggle `I_RECARM` | Pan |
| **AUTO** | Coloured per automation mode (Read / Write / Trim / Latch / Latch-Preview / Touch) | Cycle automation mode | Step automation mode |
| **FX Cycle** | Track selection | Toggle the active FX's GUI | Cycle through every FX on the strip's track |
| **Instance Cycle** | Track selection | Open active Instance's floating GUI | Cycle SSL-mapped Instances only |

The matching builtins are:

- `selection_mode_norm` — return to Norm
- `selection_mode_rec` — REC
- `selection_mode_rec_mon` — REC + MON
- `selection_mode_auto` — AUTO
- `selection_mode_instance` — FX Cycle (kept under this internal name
  for backward compatibility with saved binding files; the
  on-screen label reads "Selection Mode → FX Cycle")
- `selection_mode_instance_cycle` — Instance Cycle

Notes:

- In REC and REC + MON the selection bit is intentionally invisible
  on the SEL row — the row is dedicated to record-arm state.
- In AUTO mode, the SEL key cycles through REAPER automation modes
  when pressed, and V-Pot rotation steps between modes.
- FX Cycle walks every FX on the track (Tone Generator, ReaEQ,
  third-party); Instance Cycle walks only learned Instances. The
  former is broader; the latter promotes Instance bindings (SSL
  Strip Mode / UF8 Plugin Mode) when it lands on a recognised
  Instance.

### 5.8 REC + RME — TotalReaper integration

When the **REC + RME master switch** is on in Settings → Modes,
the REC and REC + MON Selection Modes pick up an additional layer
of behaviour aimed at RME interfaces driven via the
[TotalReaper](https://github.com/acklin83/totalreaper) bridge.
The bridge mirrors TotalMix FX state into per-track REAPER ExtState
keys (`P_EXT:totalreaper_*`); Rea-Sixty reads those keys and writes
them back through TotalReaper's named REAPER actions.

This section assumes you already have TotalReaper installed and
configured against your RME device. Without TotalReaper, REC + RME
falls back to normal REC behaviour — the buttons simply do not
fire and the scribble strips show the standard REC layout.

#### Master switch and per-button assignment

In Settings → Modes:

- **REC + RME** master toggle — enables the integration globally.
- **V-Pot Push action** — pick from None / Toggle 48V / Toggle Pad
  / Toggle Phase / Toggle Autolevel.
- **CUT action** — same menu.
- **SOLO action** — same menu.
- **V-Pot rotates preamp gain** — when on, V-Pot rotation steps
  preamp gain ±1 dB per detent through TotalReaper.
- **Shift + V-Pot steps input channel** — when on, holding the
  Shift modifier and rotating a V-Pot steps the track's hardware
  `I_RECINPUT` channel by one detent. The MIDI / multichannel /
  stereo flags are preserved, so a stereo input stays stereo as
  you walk through inputs.

"None" leaves the strip's default REC behaviour intact for that
button. Talkback, Routing Mirror and similar global TotalReaper
actions are not in the per-button menu because they are global,
not per-track — bind them to a soft-key like any other REAPER
action through the Bindings tab.

#### Colour-bar zone

In REC or REC + MON with REC + RME enabled, the colour-bar zone
of the scribble strip switches from the generic "REAPER" / CS-
variant label to the **track's hardware input name**.

- Mono inputs show as-is (e.g. `Mic 1`, `Line 3`, `MADI 5`).
- Stereo inputs show as a pair (e.g. `MADI 5/6`). The pair is
  built by reading the left channel's trailing number and
  appending `/(n+1)`.
- If the input name is too long for the 7-character zone, common
  RME device prefixes are shortened automatically: `MADI ` → `MA `,
  `ANALOG ` → `AN `, `ADAT ` → `AD `, `SPDIF ` → `SP `, `AES ` →
  `AE `. Further over-length names are hard-truncated.
- User-aliased names with no trailing number (e.g. `Drums OH`) are
  left unchanged — the pair indicator is dropped because there is
  no obvious "next channel" to append.

Non-hardware inputs (MIDI, multichannel, "no input") leave the
zone showing the surrounding mode's default text.

#### Value-line zone

The V-Pot value-line zone shows live TotalMix preamp state for the
strip's track:

```
   48V  Pd  Ph        12.5dB
```

- Left half: flag indicators. `48V` for phantom, `Pd` for pad, `Ph`
  for phase. Inactive flags render as blank space so the position
  of each indicator does not move when one toggles.
- Right half: current preamp gain in dB. RME preamp gain is always
  non-negative (the attenuator is on the pad side, not the mic-pre
  side); the readout drops the sign and clamps negatives to zero,
  so it never appears signed.
- When TotalReaper has not yet populated a gain value for the
  track, the readout shows `--dB` instead of `0.0dB`.

Gain values come from TotalReaper's `P_EXT:totalreaper_gain` cache,
which TotalReaper populates via OSC `/sendall` on csurf enable
(alpha-5 or later). So as soon as you arm REAPER's TotalReaper
csurf, your scribble strips reflect whatever state TotalMix is
already in — they do not start at 0 dB.

#### V-Pot rotation, push, CUT, SOLO under REC + RME

When REC + RME is active and a track has a hardware input bound:

- **V-Pot push** fires the user-assigned action against the
  strip's track (toggles 48V / Pad / Phase / Autolevel via the
  matching TotalReaper named action).
- **CUT** and **SOLO** fire their assigned actions in the same
  way.
- **V-Pot rotation** (with "V-Pot rotates preamp gain" enabled)
  steps preamp gain ±1 dB. Without that toggle, V-Pot rotation
  keeps its normal pan / mode behaviour.
- **Shift + V-Pot rotation** (with "Shift + V-Pot steps input
  channel" enabled) steps the hardware input channel. This wins
  over the plain rotation handler when Shift is held.

#### Surface-update behaviour during input switches

When you step through input channels with Shift + V-Pot, the
surface coalescer is suppressed briefly during the swap so that
the new colour-bar text, value-line and SEL state all appear
together rather than tearing into separate frames.

#### Falling back

To leave REC + RME, switch Selection Mode away from REC / REC + MON
(press NORM, for example), or toggle the master switch off in
Settings → Modes. In either case the SEL row and value-line revert
to their default behaviour for the active Selection Mode.

---

## 6. UC1

UC1 is a parallel surface that follows the focused REAPER track. Its
selection is independent of the UF8 bank — focusing a track in REAPER
re-targets UC1 immediately.

### 6.1 Plug-in recognition

UC1 recognises the same set of Instances as the UF8 Plug-in Mixer
Mode:

| FX name | Variant | Domain |
|---|---|---|
| Channel Strip 2 | CS 2 | Channel Strip |
| 4K G / 4K E / 4K B | 4K variants | Channel Strip |
| Bus Compressor 2 | BC 2 | Bus Compressor |
| SSL 360 Link Bus Compressor | Wrapper BC | Bus Compressor |
| SSL 360 Link | Wrapper CS | Channel Strip |

Multiple Channel Strip instances on the same track are handled via
the Instance Cycle action.

### 6.2 Knob mapping

Per Instance, UC1's physical controls map to specific plug-in
parameters:

- **Channel Strip 2 and 4K variants** — twelve EQ knobs (LP, HP,
  four bands with frequency / gain / Q), seven dynamics knobs (Comp
  threshold / ratio / release, Gate threshold / range / release /
  hold), ten buttons for bell / type / in toggles. Channel Strip 2
  has polarity inversions on LP, HMF/LMF Q and the Comp/Gate
  Threshold knobs (intentional, matches SSL's 360° behaviour); the
  4K variants do not.
- **Bus Compressor 2** — seven top V-Pots (Threshold, Makeup,
  Attack, Release, Ratio, HPF, Mix) plus IN.

### 6.3 GR meter

For built-in SSL plug-ins, gain reduction is read through the
PreSonus VST3 extension that REAPER exposes as
`GainReduction_dB` — no JSFX probe, no sidechain tap. The meter
goes through piecewise-linear calibration tables (six breakpoints
for the BC VU motor at 0/4/8/12/16/20 dB; five for the LED ladder
and the UF8 GR row at 3/6/10/14/20 dB).

For user-mapped plug-ins via FX Learn, the meter source can be
overridden — pick the VST3 parameter that carries gain reduction in
the FX Learn editor, optionally with an offset and custom calibration
breakpoints.

A small JSFX sidechain envelope-follower probe is shipped in the
repository (`extension/jsfx/rea_sixty_gr_probe.jsfx`) as a fallback
for compressors that do not expose `GainReduction_dB`. It is not
auto-inserted; if you need it, place it manually on the track after
the compressor and route the compressor's input to its sidechain
input.

### 6.4 LCD zones

UC1 has three 3-digit 7-segment displays plus a colour LCD area
split into header, sub-header, value, unit, round indicator
(value-arc), preset carousel, and central-control-panel zones. All
are driven from the focused-parameter state and the active routing
or preset mode.

### 6.5 Brightness

Three independent brightness levels — LEDs, scribble LCDs, and the
GR / status area — all settable from Settings → Device.

---

## 7. Settings window

Open: bind any key to **Rea-Sixty: Open / Close Rea-Sixty Settings**
(default-bound to the 360° key). The window is dockable and themes
itself to REAPER's active colour theme. Six tabs in a left rail.

### 7.1 Device

Per-device connection status with serial number. Controls:

- **Identify** — flashes the corresponding device to confirm which
  unit is which in multi-UF8 setups.
- **LED brightness** — five levels.
- **Scribble LCD brightness** — five levels.
- **GR meter source** — choose between the built-in
  `GainReduction_dB` path and any FX Learn override.
- **Track-select-follows-param** — focusing a parameter on the UC1
  also selects its track.
- **Plug-in GUI follows Instance** — keep the SSL plug-in window
  open and re-target it when the focused Instance changes.
- **Pin position** — capture and remember the FX-window position so
  it reappears in the same place next session.

### 7.2 Bindings

Full REAPER action picker and per-control binding editor across the
modifier matrix (Plain / Shift / Cmd / Ctrl × short or long press).
Export and import per-layer JSON files via the standard macOS save
dialog.

On-disk persistence path:

- macOS: `~/Library/Application Support/REAPER/rea_sixty/bindings.json`
- Windows: `%APPDATA%/REAPER/rea_sixty/bindings.json`

Three layers; per layer, three Quick slots with six sub-banks each
(V-POT and Soft 1..5) for 144 user-fillable soft-key slots, plus
all global, per-strip, transport, and soft-key positions. Named
Bank Presets let you snapshot and recall whole layer states.

### 7.3 Modes

Toggles for Folder Mode, Show Only Selected, and per-layer V-Pot
default behaviours.

### 7.4 FX Learn

See chapter 9.

### 7.5 Selection Sets

Eight slots per project. **The slots are partly wired.** Pressing a
slot key currently marks the slot as active and lights the LED, but
the actual store-and-recall of selection lists is not yet
implemented — see chapter 13. The UI is in place; the storage layer
behind it is on the roadmap.

### 7.6 About

Version, build hash, REAPER and ReaImGui versions, log-file
location, and repository links.

---

## 8. Bindings — what you can bind

The Bindings tab lets every UF8 / UC1 button, V-Pot and soft-key
trigger one of two kinds of action:

1. **Any REAPER action.** Anything in REAPER's action list is
   reachable, including custom actions and ReaScripts.
2. **A built-in Rea-Sixty action.** These are surface-specific
   behaviours that know about banks, focused tracks, Instances and
   so on.

The full built-in catalogue, grouped:

**Navigation.** Bank Left, Bank Right, Page Left, Page Right, Home,
Zoom Up / Down / Left / Right / Center.

**Modifiers.** Shift, Cmd, Ctrl — each available as Momentary or
Toggle.

**Channel-encoder modes.** Nav, Nudge, Focus, Instance Cycle, FX
Cycle, Mode Dispatch.

**Track operations.** Selection Mode Normal, Selection Clear All,
Select Relative, Tracks Arm All, Automation Zero All, Playhead
Nudge, Mouse Scroll.

**Instance and FX.** Instance Cycle, Instance Next, Instance
Previous, FX Cycle, BC Track Scroll.

**Strip modes.** SSL Strip Mode Toggle (with and without GUI), UF8
Plugin Mode Toggle (with and without GUI).

**Plug-in control.** Show Focused Plug-in GUI, Plug-in Bypass,
Plug-in Offline, Plug-in Preset Next / Previous / Cycle, Show FX
Chain.

**Display.** Brightness LEDs Up / Down, LCDs Up / Down, Both Up /
Down.

**Surface filters.** Folder Mode, Show Only Selected, Selection-Set
Recall (slot 1..8).

**Domain / mixer.** Domain CS, Domain BC, Mixer Toggle, Pan Force,
Flip.

**Soft-key utilities.** Softkey Bank Select, SSL Softkey, SSL Bank
V-Pot.

**Routing.** Send All 1..8, Send This, Receive This.

**Automation.** Off, Read, Write, Trim, Latch, Latch Preview, Touch
— per-track and global variants of each.

Plus an escape hatch that lets a binding fire any REAPER command ID
directly.

Total: 88 built-in actions plus the entire REAPER action namespace.

---

## 9. FX Learn

Located in Settings → FX Learn. FX Learn lets you assign UF8 and UC1
controls to any plug-in parameter — not just SSL plug-ins. ReaEQ,
FabFilter, JS effects, anything REAPER can host.

### 9.1 What FX Learn stores per plug-in

- A name match string (substring of the FX name).
- A domain (Channel Strip, Bus Compressor, or none).
- A "UF8 mode" flag — when on, all eight UF8 strips drive parameters
  of this plug-in.
- Per-slot VST3 parameter indices, with optional polarity inversion.
- Optional gain-reduction metering: which VST3 parameter carries GR,
  an offset in dB, and two calibration breakpoint tables (six points
  for the BC-style VU motor, five for the LED ladder).
- Optional UF8 bank and strip bindings.
- A snapshot of parameter names for display on the scribble strip.

### 9.2 How matching works

Each lookup runs the substring match against the FX name as REAPER
reports it. This means:

- **Reordering** FX on a track is safe — the match runs on each
  lookup and finds the plug-in wherever it now sits.
- **Renaming** the FX (in REAPER's FX window) breaks the mapping —
  the next lookup will not match.

If you want to rename a plug-in for clarity, also update the match
string in FX Learn so the lookup keeps working.

### 9.3 Where the data lives

The catalogue is stored in a single JSON file at
`<REAPER_RESOURCE>/rea_sixty/user_plugins.json`. Saves are atomic
(write to a temporary file, then rename) so a crash during save
cannot corrupt the file.

---

## 10. Troubleshooting

### 10.1 `Rea-Sixty UF8: SSL360Core owns the device`

SSL 360° is still running. Quit the SSL 360° app and the
`SSL360Core` background process (you can find it in Activity
Monitor), then reload the extension or restart REAPER.

### 10.2 No on-screen window when I press the 360° key

ReaImGui is not installed in REAPER. Install it via ReaPack and
restart REAPER. The hardware-control path works without ReaImGui;
only the Settings window depends on it.

### 10.3 No LED feedback on solo / mute / select / arm

Rea-Sixty has both a vendor-USB LED path (which always runs) and a
legacy MCU-MIDI LED fallback (which depends on REAPER's CoreMIDI
seeing a port whose name contains "UF8"). On extension start the
extension writes the list of MIDI destinations it saw to
`/tmp/reaper_uf8_midi_dests.log`. Open that file to see what was
available. If the vendor-USB LEDs themselves are working, the
fallback is cosmetic.

### 10.4 Fader behaves erratically

REAPER's action **Rea-Sixty: Toggle fader calibration logging** and
**Rea-Sixty: Toggle fader input log** turn on diagnostic streams that
let you see what the fader is actually sending.

### 10.5 An LED is in the wrong colour

REAPER's action **Rea-Sixty: Probe next global LED cell** walks the
UF8's LED cells one at a time so you can map which physical LED
corresponds to which protocol cell. There is a parallel action for
the legacy monochrome LEDs.

### 10.6 GR meter reads wrong on a third-party compressor

Open Settings → FX Learn, find the compressor, set its GR override:
pick the VST3 parameter that carries GR, set an offset if needed,
and adjust the calibration breakpoints until the on-screen meter
matches the plug-in's own meter under known signal levels.

### 10.7 I want to see the raw bytes Rea-Sixty sent

REAPER's action **Rea-Sixty: Frame trace** toggles a frame log to
`/tmp`. Useful when a colour or LED is wrong and you want to compare
the bytes to a capture.

### 10.8 Reset everything

Delete `~/Library/Application Support/REAPER/rea_sixty/` and restart
REAPER. The extension recreates defaults.

---

## 11. What this version supports

Drawn from the live code. If something is missing here, it is not
yet implemented — see chapter 13.

**UF8 outputs.** Track colours per strip, upper and lower scribble
text, 19-character value line, Channel-Strip-Type label, channel
number with folder sigils, V-Pot readout bar, fader dB readout,
selected-strip bitmask. Per-strip SOLO / CUT / SEL colour LEDs with
dim and bright automation variants. Top soft-key LEDs. 40+ global
button LEDs (Layer, Send/Plugin, Page, Bank, Plugin, Channel, Flip,
Automation, Selection Mode, Transport, Zoom, Nav, Nudge, Focus,
PAN, Fine, Norm, Rec, Auto, 360°). VU meter pair. GR row stamped
into Plug-in Mixer heartbeats. Master LED and LCD brightness.

**UF8 inputs.** Every documented button (per-strip and global). Fader
position at 16-bit precision. Fader touch with debouncing. V-Pot
deltas.

**UC1 outputs.** GR meter at 50 Hz. VU meter pair per channel strip.
LED bus for every button. 7-segment digits (ones, tens, hundreds
partly calibrated). Display zones: header, sub-header, value, unit,
context blocks for CS and BC, round indicator with value-arc,
preset carousel (5-slot, 14-char), central control panel mode and
label, routing indicator, mode dots. Track-name carousels (small
and large). Three independent brightness levels (LED, LCD, status).

**UC1 inputs.** Every documented button (31 IDs). All 16 V-Pots plus
3 encoders, with signed 6-bit deltas.

**REAPER integration.** Surface registration as a `csurf_inst`. Bank
window of 8 tracks, REAPER-track-list ordered, with explicit Bank
Left / Bank Right actions. Selection-follow on `SetSurfaceSelected`.
Folder Mode with parent-only banking. Six Selection Modes (Norm,
REC, REC + MON, AUTO, FX Cycle, Instance Cycle) with re-targetable
SEL rows and V-Pot behaviours. Selection-set recall (action wired;
storage layer not yet — see chapter 13). 88 built-in Rea-Sixty
actions plus the full REAPER action list.

**RME / TotalReaper integration.** In REC and REC + MON Selection
Modes, an optional TotalReaper-driven layer surfaces hardware input
names in the colour-bar zone, preamp 48V / Pad / Phase flags and
gain dB in the value-line zone, V-Pot push / CUT / SOLO actions on
the assigned TotalMix preamp toggles, V-Pot rotation as preamp gain
control (±1 dB per detent), and Shift + V-Pot as input-channel
selection.

**Plug-in integration.** Channel Strip 2, 4K B, 4K E, 4K G, Bus
Compressor 2, SSL 360° Link (both Channel Strip and Bus Compressor
wrappers). Recognition by FX-name substring. Multi-instance handling
on a single track. A/B compare and HQ-mode toggles via direct VST3
state-chunk patching (those flags are not reachable through REAPER's
parameter API).

**Settings window.** Six tabs (Device, Bindings, Modes, FX Learn,
Selection Sets, About) themed to REAPER's active theme.

---

## 12. Status messages

Messages printed to REAPER → View → Console by Rea-Sixty:

- `Rea-Sixty UF8: <reason>  (UF8 optional — continuing)` — UF8 could
  not be opened. The most common reason is SSL 360° holding the
  device.
- `Rea-Sixty UC1: <reason>  (UC1 optional — UF8 continues)` — UC1
  could not be opened. The extension still runs against UF8.

Files written under `/tmp`:

- `reaper_uf8_midi_dests.log` — every CoreMIDI destination visible
  when the extension opened its MIDI port. Useful when LED feedback
  via the MCU fallback path is missing.
- Frame-trace logs created by the Frame-trace action.

Strings the UF8 firmware itself writes to its scribble strips
before any host has handshaken (Rea-Sixty replaces these on its init
replay):

- `UF8 Initialisation Complete` + `Awaiting Connection to SSL 360°
  Software`
- `Layer Set To None`
- `SSL 360° Connection Lost. Attempting to Reconnect`

If you see the last of these after Rea-Sixty was working, the
extension crashed mid-session. Look for a REAPER crash log and
restart REAPER.

---

## 13. Known limitations

What is **not** in this version, despite being described in
`README.md` or `ROADMAP.md`:

1. **On-screen Plug-in Mixer view.** The dockable window currently
   only contains the Settings tabs. The themed mixer view that
   mirrors SSL 360°'s Plug-in Mixer page is on the roadmap (Phase
   2.6) but not in this build.

2. **GUID-keyed FX Learn.** The README describes FX Learn as
   "GUID-keyed". In practice FX Learn matches by FX-name substring.
   This is more conservative: reorders are safe, renames break the
   mapping. The behaviour is correct; the documentation is loose.

3. **Selection-set storage.** Pressing a Selection-Set key currently
   marks the slot as active and lights its LED, but does not yet
   store or recall a list of selected tracks. The action exists, the
   UI is in place; the storage layer is on the roadmap (Phase 2.5b).

4. **Auto-insert of the JSFX GR probe.** The probe is shipped in the
   repository but the extension does not insert it automatically.
   Place it manually if you need it.

5. **Foot-switch input.** The vendor-USB event for foot-switch
   press has not yet been decoded. FS1 and FS2 produce no action.

6. **Cross-platform builds.** The build system targets macOS, Windows
   and Linux in principle. Only macOS is currently smoke-tested.

7. **Long-press SEL → folder expand.** Folder Mode works as a
   parent-only filter, but the long-press SEL gesture to expand a
   single parent into its children is not yet wired (Phase 2.5a).

8. **In-app firmware update.** Out of scope. SSL still distributes
   firmware blobs through SSL 360°; keep SSL 360° installed for the
   rare occasion of a firmware update, then quit it again.

---

## 14. Quick reference — button names

| Strip-local | Per strip 0..7 |
|---|---|
| Fader | Touch-capacitive, motorised |
| V-Pot | Push-encoder |
| Top Soft-Key | Above scribble |
| SOLO / CUT / SEL | Colour-pair LEDs |

| Global | |
|---|---|
| LAYER 1 / 2 / 3 | Left edge |
| 360° | Settings toggle |
| BANK ◀ / ▶ | Bank by 8 |
| PAGE ◀ / ▶ | Page within bank |
| PLUGIN | Plug-in mode toggle |
| CHANNEL | Domain CS / BC flip |
| FLIP | Fader / V-Pot swap |
| PAN | Force V-Pots to pan |
| FINE / SHIFT | Shift modifier |
| NORM / CLEAR | Selection mode normal |
| REC / ALL | Tracks arm all |
| AUTO / ZERO | Automation zero |
| Touch | Fader-touch behaviour |
| READ / WRITE / TRIM / LATCH | Automation modes |
| SEND / PLUGIN 1..8 | Send and routing |
| Soft 1..5 + V-POT | Top-right soft cluster |
| Channel encoder | Nav / Nudge / Focus / Instance / FX Cycle |
| Zoom cluster | Vertical / horizontal zoom |

---

End of manual.
