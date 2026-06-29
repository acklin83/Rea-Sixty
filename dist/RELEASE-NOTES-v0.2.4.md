# Rea-Sixty v0.2.4 — "1.21 Gigawatts!"

A big feature batch: dynamic soft-key banks that compute themselves from the focused track, the new DynaMount robotic-mic-stand mode, a Keyboard-macro binding type, correct umlaut rendering on the UC1/UF8 LCDs, and favourites that keep their plug-in's own parameters across cycling.

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

### Dynamic soft-key banks
- A Sub-Bank can now compute its 8 keys **live from the focused track**: **FX 1-8/9-16**, **Parameter Groups**, or **Track Colours** — no manual per-key assignment.
- The **FX bank is pageable** by a control you choose (a UF8 encoder, UC1 Enc 1/2, or the UF8 Bank keys), so a track with more than 8 FX walks 1-8 / 9-16 / …
- **FX bank**: only the focused FX's key lights bright (bypassed = dark). Each key acts on its FX with a **configurable gesture set** — Focus, Float/close window, Bypass, FX-solo (bypass the others), Offline, Move, Delete (default press = Focus).
- **Parameter Groups** and **Track Colours** apply to **all selected tracks**; Parameter-Group on/off state and names are saved **per project**.

### DynaMount mode
- Control up to **8 DynaMount robotic mic stands** straight from the UF8 strips. Enabled mounts pin to one side; the rest stay normal tracks.
- **Fader = X** (left/right) or, with **FLIP**, **Y** (distance, fader up = nearest); **V-Pot = rotation**. Fader is send-on-release (open-loop friendly).
- Per-mount **Home calibration** to the reference pose, positions saved globally across restarts.
- New **Settings → Dynamount** tab: per-mount enable / name / IP / colour / detect / status.

### Keyboard macro binding
- New **"Keyboard macro"** action type alongside REAPER Action / Native Action / MIDI.
- **Capture a key chord live** (press the keys) — modifiers included — or type it; chain several entries with per-step delays for a multi-key macro.
- Chords are delivered **inside REAPER** (never OS-global), so they fire whatever REAPER action is bound to that shortcut. Ctrl and Cmd stay distinct on macOS.

### Surface LCD
- **Umlauts render correctly** on the UC1 and UF8 LCDs — track, marker, FX and preset names with ä ö ü ß (and the rest of Latin-1) now show right across every name field instead of as garbled bytes.
- **UC1 channel-strip carousel** shows the neighbouring track names for ~1 s after a channel switch, then collapses to just the current name; the edited parameter stays on screen.

### Favourites
- A favourite now **keeps its plug-in's own parameters** across cycling. Parameters with no SSL/UC1 mapping (e.g. Analog Molecule's "3D Flux" / "Thermal Bloom") are remembered per favourite and restored when you cycle back, instead of resetting to plug-in defaults — for both Channel-Strip and Bus-Compressor favourites, and saved with the project.

## Known issues

Same as v0.2.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.2.4.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.2.4.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.2.4.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).
