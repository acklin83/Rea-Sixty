# Rea-Sixty v0.2.1 — "We can't stop here, this is bat country."

REAPER 7.75 routing follow-up: empty slots, reordered sends, and hardware outputs in the surface Send view.

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

- **Send/Receive views are 7.75 slot-aware.** Empty send slots show as gaps in the right place; reordered sends keep the mixer's top-to-bottom order; a send sitting after an empty slot no longer vanishes (the slot count now includes the gap).
- **Hardware outputs in the Send view.** Track sends and hardware outputs share one slot list exactly like REAPER's mixer send list, named by output channel ("Analog 1 / Analog 2"), each with its own fader/pan.
- **Send-solo covers the whole list.** Soloing a send mutes every other track send *and* hardware output to the last slot (no longer stopping at a gap), and toggles them all back.
- **Send mute LEDs on every routed strip,** including sends past the visible bank.
- **Empty FX slots (REAPER 7.75):** inserts overlay, slot-aware Encoder FX-Move, and CS-Switch all handle empty FX-chain slots.
- **Inserts overlay:** blue "Selected FX" box, mixer hide/show, fullscreen-mixer re-acquire.
- **Sensitivity shown in hover tooltips** (Learn-HUD + FX-Learn, UC1/UF8).
- **Fill Sequential** carries all per-control properties (steps / mode / feel / label) across the bank.
- **Windows:** Alt→Cmd modifier feed + AltGr guard; CS-Switch / FX-Move as native REAPER actions; MSVC build fix.

## Known issues

Same as v0.2.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.2.1.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.2.1.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.2.1.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).
