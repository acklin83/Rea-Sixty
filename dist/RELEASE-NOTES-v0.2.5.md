# Rea-Sixty v0.2.5 — "If we were to fight, I think it would better if we were dubbed and not in subtitles!"

A small maintenance release: three new hardware-mode actions, a tidier UC1 bindings page, and a fix for the Solo LED going dark after a trip through the send/receive views.

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

### Hardware-mode actions
- **Mirror to Arrange / Mirror to Mixer** — two mutually-exclusive built-in actions set which window the surface's visibility follows. The bound key lights while its mode is active.
- **Toggle "Arrange follows selection"** — a built-in action for the follow-selection setting.
- All three live in the **Hardware Modes** action category, so you can bind them to any surface key or trigger them from REAPER.

### UC1 bindings page
- The **LED-appearance block is hidden** for UC1 controls, which have no user-settable colour.
- The **"Layer N" breadcrumb is gone** from the editor header for UC1 controls.
- The editor is now **device-scoped**: the UC1 tab only edits UC1 controls, so a UF8 soft-key you had selected no longer bleeds its Quick / sub-bank selectors onto the UC1 tab.
- **UC1-only Save / Load** — export or import just the five UC1 controls; importing replaces only the UC1 bindings and leaves your UF8 bindings untouched.

## Bug fixes
- **Solo LED restored after the send/receive views.** With a track soloed, switching into a send or receive view and back out no longer leaves the Solo LED dark — the surface now re-reads the track's real solo state on the way out.

## Known issues

Same as v0.2.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.2.5.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.2.5.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.2.5.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).
