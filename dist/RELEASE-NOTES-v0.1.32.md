# Rea-Sixty v0.1.32

Small follow-up to v0.1.31: the Device **V-Pot / encoder speed** minimum now goes all the way down to **0.01x** (was 0.10x), for ultra-slow, ultra-fine encoder moves — and the value now persists correctly across restarts.

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
- **macOS:** nothing extra (built for both Apple Silicon and Intel)

## What's new

### Finer Device encoder speed

- **Settings → Device:** the UF8 V-Pot speed and UC1 encoder speed minimum is lowered from `0.10x` to **`0.01x`**, for very slow / very fine control. (v0.1.31 already took the FX-Learn sensitivity down to 0.01 but left the device speed at 0.10 — this completes it.)
- Fixes a mismatch where a typed `0.01` showed in the field but was internally clamped back to `0.10` and didn't survive a restart. The lower bound is now applied consistently in the UI field, the setter, and the persisted setting.

## Known issues

Same as v0.1.31. Nothing new.

## Manual install

If ReaPack isn't an option:

- **macOS (Apple Silicon):** `rea-sixty-mac-v0.1.32.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **macOS (Intel):** use the `*-x86_64.dylib` assets, renamed to `reaper_rea-sixty.dylib` / `libusb-1.0.0.dylib` / `libhidapi.0.dylib`, into the same folder.
- **Windows:** `rea-sixty-win-v0.1.32.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.1.32.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).
