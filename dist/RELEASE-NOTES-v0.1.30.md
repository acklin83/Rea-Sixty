# Rea-Sixty v0.1.30

Knob feel pass: global per-surface V-Pot / encoder speed + Fine factor, and a tunable SSL-style 0 dB virtual notch (now on **both** UF8 V-Pots and UC1 knobs) with a soft-detent that stops an endless encoder sailing past 0. Plus a Learn-HUD crash fix, Intel-Mac ReaPack support, and a cleanup of stale companion scripts.

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
- **macOS:** nothing extra (now built for both Apple Silicon and Intel)

## What's new

### V-Pot / encoder resolution (Settings → Device)

- Per-surface **speed** + **Fine factor** for every UF8 V-Pot and UC1 encoder — tune the two surfaces independently. Defaults match the previous feel.
- Pan now honours the same speed / Fine scaling.

### Tunable 0 dB virtual notch (UF8 **and** UC1)

- The SSL-style centre magnet for bipolar params (EQ gains, trims, fader level) now runs on UC1 knobs too, not just UF8 V-Pots.
- Snaps to exactly 0 dB on any inward approach (no more parking at -0.2 dB); turning away stays free.
- Three new settings: **notch zone** (catch width), **notch fine step** (finer moves near 0), and **notch hold** (soft-detent that parks on 0 so an endless encoder can't overshoot).

### Intel Mac via ReaPack

- macOS x86_64 is now built in CI and shipped through ReaPack — Intel Macs update like everyone else instead of building from source.

## Bug fixes

- **Learn-HUD "End() too many times" crash** — `ImGui_End` is now only called when the window began (fixes the crash on the hardware-layout tab / collapsed HUD).
- Geometry-section parse hardened (Windows / FabFilter Pro-C 3 report).
- Stale pre-ReaImGui companion scripts are removed from `Scripts/rea-sixty/` on load, so an old Action can no longer launch the broken gfx HUD.

## Known issues

Same as v0.1.29. Nothing new.

## Manual install

If ReaPack isn't an option:

- **macOS (Apple Silicon):** `rea-sixty-mac-v0.1.30.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **macOS (Intel):** use the `*-x86_64.dylib` assets, renamed to `reaper_rea-sixty.dylib` / `libusb-1.0.0.dylib` / `libhidapi.0.dylib`, into the same folder.
- **Windows:** `rea-sixty-win-v0.1.30.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.1.30.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).
