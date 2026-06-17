# Rea-Sixty v0.1.29

The Learn-HUD grows a full UF8 device tab — you can now **see and edit** every UF8 strip assignment from the on-screen HUD, with two views (interactive strip-grid + a hardware mockup that matches the FX-Learn face), colour-aware V-Pot banks, and two ways to map: software (pick a param, click a cell) or Touch-to-Learn (move a control to arm it, wiggle a parameter to bind). Plus a 4th UC1 modifier layer, 10 knob feel-preset slots, and a clutch of UC1 input/readout fixes.

## Install via ReaPack (recommended)

```
Extensions → ReaPack → Manage repositories → Import/export → Import repositories
```
Paste:
```
https://github.com/acklin83/reaper-scripts/raw/main/index.xml
```
Then **Browse packages** → `Rea-Sixty` → Install. Restart REAPER. Preferences → Control/OSC/Web → Add → Rea-Sixty.

First-run setup buttons (`Settings → About`):
- **Windows:** "Install UF8/UC1 WinUSB driver" (UAC prompt)
- **Linux:** "Install Linux udev rule" (pkexec prompt)
- **macOS:** nothing extra

## What's new

### UF8 device tab in the Learn-HUD

- **Interactive strip-grid** — 8 strips × V-Pot / Fader / Solo / Cut / Sel, bank-aware (the 8 Top-Soft-Keys pick the V-Pot bank; faders/buttons follow the fader bank). The grid tracks the live hardware banks; click a bank in the HUD to switch it.
- **Parameter List** (software bind) — open the drawer, type to filter, pick a parameter, then click a cell to bind it. No wiggle needed. Bound params show a green dot.
- **Touch-to-Learn** — arm a cell by moving its control on the hardware (touch a fader, turn a V-Pot, press Solo/Cut/Sel), then wiggle a plug-in parameter to bind. Works alongside the UC1 tab's Touch-to-Learn.
- **Virgin-plugin bootstrap** — map an unmapped plug-in straight from the HUD: the first bind creates a UF8-only map for it.
- **Right-click a cell** — Invert, Fill sequential (CH1→CH2…), Unbind, V-Pot value/toggle mode.
- Opening the UF8 tab auto-engages UF8 Plugin Mode so the hardware soft-keys drive the bank row; leaving reverts it (only if the HUD engaged it).

### UF8 hardware-mockup view

A second view for the UF8 tab (like the UC1 tab), faithful to the FX-Learn UF8 face — 8 strips with scribble LCD, V-Pot ring, stacked Solo/Cut/Sel and a tall fader; the 8 Top-Soft-Keys are the V-Pot bank selectors. Toggle list ↔ mockup from the right-click View menu. All learn / colour / bank interactions work in both views.

### V-Pot bank colours + display colour bars

Right-click a V-Pot bank → a 10-swatch SSL palette sets that bank's Top-Soft-Key LED colour (and the bank chip in the HUD). A "Bar colour (all)" entry paints all 8 of the active bank's display colour bars at once.

### UC1 — Control+Option FX-Learn layer

A fourth modifier overlay (C+O) joins Normal / Option / Control, so a single UC1 knob can carry up to four different parameter mappings depending on the held modifier.

### 10 knob feel-preset slots

The global knob-feel presets went from 5 to 10 slots (shared UC1 / UF8).

## Bug fixes

- **UC1 two-knob "fight for priority"** — turning two knobs at once dropped frames; the input parser now walks every frame in a USB transfer (mirrors the UF8 path).
- **UC1 modifier-layer knob travel + focused readout** now fall back correctly when a layer has no explicit mapping.
- **Learn-HUD** — the held modifier layer is latched at context-menu open, so Rename / Invert / Unbind act on the layer you were holding (the native dialog releases it).
- **Esc** cancels a HUD learn / assign even when the HUD isn't focused (reads the key globally via js_ReaScriptAPI when present).
- **UF8 Touch-to-Learn fader** is no longer stiff while arming (the motor limps as usual).
- **Esc** also closes the FX-Learn curve editor ("Advanced…") popup.
- **Console spam fix** — full ReaImGui header signature-drift audit (the `SetNextWindowSizeConstraints` "expected a valid ImGui_Function*" warnings are gone).
- CS/BC + HUD terminology unified (display strings only); onboarding doc gaps fixed.

## Known issues

Same as v0.1.28. Nothing new.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.1.29.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.1.29.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.1.29.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).
