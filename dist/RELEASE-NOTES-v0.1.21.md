# Rea-Sixty v0.1.21 — "Stayin' Alive"

Motorised faders stay glued to REAPER again. Selected/ganged channels move relative to each other and never drift apart, a fader you briefly touched no longer goes deaf to volume changes, and Touch-mode automation records from the value where you actually grabbed the fader. Plus marker LEDs now show their colour from the start.

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

### Fader fixes

- **Multiple selected faders stay in sync.** Moving one fader of a multi-track selection no longer collapses the others onto its level — REAPER's native relative ganging now drives them, so the channels keep their offsets and never lose sync with the DAW.
- **Tapped faders follow REAPER again.** After briefly touching a fader (without moving it), the motor went limp and silently ignored later volume changes from REAPER — the routing dialog, automation playback or a ganged move wouldn't reach it until you physically nudged it. The fader now re-engages the moment REAPER drives it, so it always tracks.
- **Touch-mode automation seeds at the right spot.** A new volume envelope created in Touch mode now starts at the value the fader was at when you grabbed it, instead of a slightly-later moved value.

### Markers

- **Uncoloured markers show REAPER's colour.** Marker/region strips on the UF8 now use REAPER's displayed marker colour (including the theme default for markers you haven't coloured), instead of showing grey until a colour was assigned.

## Known issues

- Same as v0.1.18.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.1.21.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.1.21.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.1.21.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).
