# Rea-Sixty v0.1.22 — "It's Suntory Time"

Marquee-zoom in the arrange view stays put. The "keep the selected channel visible" follow no longer fires on REAPER's own selection changes — so a marquee zoom (or any mouse/action selection) zooms exactly where you drag it instead of snapping back to the selected track. The view only follows when you change the channel from the hardware (UF8/UC1 SEL, encoder, param-follow).

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

### Bug fixes

- **Marquee zoom no longer snaps back to the selected track.** With "TCP follows selection" on, REAPER re-broadcasts the whole selection set on a view change, which made the arrange view scroll straight back to the selected channel the instant you marquee-zoomed elsewhere. The view-follow now fires only for hardware-originated selection (UF8/UC1 SEL, encoder channel-select, param-follow) — mouse, marquee and actions leave your view where you put it. Bank-follow and UC1 focus still track REAPER's selection as before.

## Known issues

- Same as v0.1.18.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.1.22.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.1.22.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.1.22.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).
