# Rea-Sixty v0.1.27 — "Randy - I AM the Liquor!"

The on-screen story gets its headline act: the **Learn-HUD**, a dockable window that shows the focused plug-in's UC1 control → parameter assignments as a clean, readable text list — so you can see what every knob and button does without opening Settings. The Inserts overlay and the focused-track panel are now first-class citizens too, FX-Learn grows three full modifier layers (Normal / Option / Control), and a pile of per-layer naming bugs are gone — your custom Display labels now show correctly everywhere.

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

### Learn-HUD

A dockable window listing the **focused plug-in's UC1 control → parameter map** — a cheat-sheet for what each knob/button does.

- **How to open it:** bind the `learn_hud_toggle` built-in to a surface button, or run the REAPER action *"Rea-Sixty: Toggle Learn-HUD"* (`REASIXTY_LEARN_HUD_TOGGLE`). There is no Settings checkbox.
- **CS / BC tabs** auto-follow the focused domain (click one to pin it).
- Each row is the **SSL slot name** (left) and the **bound parameter's name** — or your custom Display label — (right). Rows are grouped by section, with Dynamics + Gate in a right-hand column to mirror the hardware.
- A **layer badge** (NORM / OPT / CTRL) shows the held FX-Learn modifier layer; the list follows it live.
- **Right-click** for text size (Small … Huge) and text colour (CS/BC or white). Window size persists globally; default 500×500.
- **Learn from the HUD:** click a row, then wiggle that plug-in's parameter to bind it (user maps only — built-in SSL maps stay factory-fixed).

### FX-Learn modifier layers (Normal / Option / Control)

A user-mapped UC1 control can now carry **three independent overlays**, each with its own parameter, invert, knob-travel and Display label.

- **Edit** them via the layer tabs in the FX-Learn editor — every per-control change applies to the selected layer.
- **Use** them by ticking *Settings → Device → Keyboard Options → "Hold Option / Control for the FX-Learn layer"*, then holding **Option/Alt** (Option layer) or **Control** (Control layer). An unmapped overlay falls back to the Normal mapping.

### On-screen display

- **MCP Inserts overlay** (Settings → "Show MCP Inserts overlay") now defaults to a clean **outline** — CS yellow, BC red, no fill.
- **Focused-track panel** (Settings → "Show focused-track panel"): new **"Use track colour"** option (right-click → Track name) draws the track name in its REAPER colour. The parameter line shows your custom Display label, and keeps it after you release a modifier — until you touch a different parameter.

### Custom names everywhere (bug fixes)

- Your **per-layer Display labels** now show correctly on the UC1 LCD, in the FX-Learn editor, in the Learn-HUD and in the focused-track panel — pressing or turning an Option/Control-layer control no longer snaps the readout back to the default name.
- **Modifier-layer data no longer gets lost:** the per-domain slot cache now stores its overlays, so switching the edit domain can't silently wipe an Option/Control mapping.

### Appearance

- The **British / American spelling** setting (Settings → Appearance) is now also honoured in the focused-track panel's menu.

## Known issues

- Same as v0.1.18.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.1.27.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.1.27.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.1.27.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).
