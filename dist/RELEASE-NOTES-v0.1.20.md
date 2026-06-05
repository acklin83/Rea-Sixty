# Rea-Sixty v0.1.20 — "Behave!"

UC1 channel-strip buttons get a proper push-cycle editor: pick exactly which options a button steps through, reorder them, or build a multi-parameter macro on an unmapped button — all from FX-Learn.

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

### UC1 button push-cycle editor

The discrete channel-strip buttons (EQ Type, HF/LF Bell, Comp Fast Attack, Comp Peak, Expand, Gate Fast Attack) can now be curated in **FX-Learn**, on any user plug-in:

- **Pick which options cycle.** Right-click a button bound to a multi-option parameter → every option shows as a checkbox. Untick the ones you don't want; they stay visible and in place, the push just skips them.
- **Reorder by drag.** A `≡` handle on each row sets the cycle order.
- **Build a macro on an empty button.** Right-click an unmapped button → "+ Add step" → pick parameters/values to step through. Useful when a third-party plug-in splits an SSL concept across several separate toggle parameters — one button now walks them all.

Tick every option in natural order again and the button quietly reverts to "cycle all" (the v0.1.19 behaviour). Built-in SSL plug-ins are unchanged.

## Known issues

- Same as v0.1.18.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.1.20.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.1.20.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.1.20.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).
