# Rea-Sixty v0.1.26 — "Pineapple Express"

The Inserts marker goes fully non-destructive and grows an on-screen companion. A `JS_Composite` overlay highlights the active Channel Strip and Bus Compressor right on REAPER's Mixer Inserts list — no FX rename, no dirty project — and an optional dockable panel shows the focused track's CS, the surface's BC, and the last parameter you touched (by its Rea-Sixty name). Colours, opacity, row height and font are all tunable, both toggles persist and come back when REAPER opens, and the companion ships baked into the extension — one click in Settings, no ReaPack hunt. Plus a way to unbind any binding slot.

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

### Inserts overlay (non-destructive)

- **On-screen CS / BC highlight** on REAPER's Mixer (MCP) Inserts list, drawn with `JS_Composite` — replaces the old `renamed_name` marker, so the project no longer goes dirty and nothing is baked into FX names.
- **Design-able:** CS colour, BC colour, fill opacity and border opacity, all live (Settings → Device → Inserts).
- **Row height + top offset** sliders to line the highlight up with your theme / UI scale.
- Marks only the surface's *active* CS (focused track) and BC (anchor track) — not every track that merely hosts one.
- The companion is **baked into the extension** and self-installs on load; a **Start/Stop** toggle lives in Settings (no separate action to find).

### Focused-track docker (optional)

- A small **dockable panel** showing the focused track's active **CS**, the surface's active **BC** (on whatever track it's anchored to), and the **last-changed parameter** with its value.
- Parameter shows its **Rea-Sixty mapping name** (or a user-defined alias), not the raw VST3 name.
- **Font size** adjustable; **right-click** to dock / undock / close.

### Persistence

- The MCP overlay and the docker are **two independent toggles**, both persistent — and they **come back automatically when REAPER opens**.

### Bindings

- **"None (disabled)"** option in the action editor to explicitly unbind a slot (distinct from "Reset to factory default").

### Performance

- Overlay rescans are now **event-driven** (active set / mixer scroll / track count) instead of on a timer — no more typing lag while it's running.

## Known issues

- Same as v0.1.18.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.1.26.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.1.26.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.1.26.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).
