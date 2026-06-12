# Rea-Sixty v0.1.24 — "Scroll to Infinity and Beyond"

Endless FX scrolling. Two new encoder modes walk through every FX on a track and then keep going onto the next track — just like SSL 360° does natively. Plus a per-binding "reset to factory default" right in the schematic, and a fix for the vertical-zoom buttons that were the wrong way round.

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

### Encoder

- **Cross-track FX / Instance scroll.** Two new encoder behaviours scroll through all FX (or just learned Instances) on a track, then cross to the next track and continue from its first FX — SSL 360°-native feel. One detent per track boundary; hard-stop at the project edge. Available both as toggleable Encoder Modes (`Encoder Mode → Cycle FX / Cycle Instance (across tracks)`) and as direct-rotation actions (`Encoder: cycle FX / instance (across tracks)`) you can bind to any encoder.

### Bindings

- **Reset a single binding to factory default.** Right-click any control in the UF8 / UC1 schematic → "Reset binding to factory default" — affects only that one binding, leaving the rest of your layer untouched.

### Bug fixes

- **Vertical zoom in/out was inverted.** The "Zoom in vertically" action zoomed out and vice versa. Fixed.

## Known issues

- Same as v0.1.18.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.1.24.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.1.24.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.1.24.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).
