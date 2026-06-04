# Rea-Sixty v0.1.18 — "That's not a knife... that's a knife!"

A track-selection release. The REAPER mixer now **only scrolls when the selected track is off-screen** instead of yanking it to the far left on every selection — exactly like clicking a track with the mouse. And **Shift + the channel encoder** now extends the selection across adjacent channels, REAPER's Shift+Arrow style.

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

### Mixer scrolls only when needed

Selecting a track from the UF8/UC1 no longer forces it to the leftmost mixer strip. The mixer now **follows the selection minimally** — it scrolls only when the selected track is outside the visible area, and leaves already-visible tracks where they are. Works the same in a wide, narrow, or floating mixer (uses REAPER's native follow under the hood).

### Shift + channel encoder = range-select

A new bindable encoder action, **"Encoder: extend track selection (Shift+Arrow style)"** (Bindings picker → *Cycle Actions*). Bind it to any encoder + modifier slot — e.g. **Shift + Channel Encoder** (UF8) or **Shift + UC1 Encoder 1** — and turning the encoder grows or shrinks a contiguous selection across adjacent channels from an anchor, exactly like REAPER's Shift+Arrow. No default binding (the Shift slot ships as Instance-Cycle), so add it where you like.

## Known issues

- Same as v0.1.17.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.1.18.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.1.18.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.1.18.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).
