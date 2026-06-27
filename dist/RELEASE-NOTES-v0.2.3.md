# Rea-Sixty v0.2.3 — "Did you try turning it off and on again?"

A stability-and-polish release: the macOS 26 meter-stutter fix, a batch of focused-panel additions (now genuinely dockable across restarts), and a set of Channel-Strip / Bus-Compressor favourite value-transfer fixes.

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

### Performance
- **macOS 26 meter-stutter fix.** The MCP Inserts overlay (active CS/BC highlight) located its targets with a full-mixer pixel sweep; on macOS 26 each probe round-trips through SkyLight, so a single scan stalled the main thread and the peak meters + mouse-edit cursor stuttered every 1–2 s while mixing. The overlay now finds each strip's column directly from track geometry — ~60 probes per active track instead of ~8000. Meters stay smooth.

### Focused panel
- **Click a plug-in name to open it** — clicking the CS or BC name in the panel floats/unfloats that FX window.
- **Persistent mode indicator** (current Selection / Encoder mode) and a **transient mode-change flash**.
- **Settings and HUD buttons**, and **CS / BC favourite cycle ◀▶ buttons**.
- **Drag-to-arrange elements** — turn on Arrange mode to drag the panel's blocks into any order, with per-block line breaks. Order persists.
- **Centre horizontally / vertically.**
- **Docking that survives a restart** — dock the panel into a REAPER docker and it re-attaches where you left it next launch.

### Favourites
- **Copy only mapped parameters** (on by default) — switching/cycling favourites now carries only the parameters actually mapped to a UC1 control, instead of also matching unmapped plug-in internals by name (which could drag things like the SSL channel's Auto-Makeup setting).
- **Filter open → off across plug-ins** — a low/high-pass parked fully open on one plug-in now lands on the destination's discrete "out" position instead of the nearest frequency when switching strips.
- **Routing + EXT FUNCS preserved on cycle.** The SSL EQ-Filter-Dyn routing order (incl. the Ext-Sidechain variants) and the EXT FUNCS now carry across favourite switches and are restored per-favourite in "own settings" mode — matched by control identity so it works between different SSL strips, and saved with the project.
- **Per-favourite memory stays consistent across copy/own** — a strip is restored exactly as it was when you last left it, even after switching the global copy/own mode in between.

## Known issues

Same as v0.2.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.2.3.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.2.3.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.2.3.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).
