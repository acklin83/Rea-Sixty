# Rea-Sixty v0.1.19 — "Anchors Aweigh"

A small fix release. UF8 faders no longer nudge a track's volume when you touch them in Touch automation, and UC1 buttons bound to a multi-option plug-in parameter now step through **all** the options instead of jumping straight to the last.

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

### Touch automation no longer nudges volume

On a UF8 fader in **Touch** automation, grabbing a fader on a track that had **no volume envelope yet** quietly dropped the track to ~−0.5 dB and seeded the new envelope there — before you even moved. Root cause: the fader's capacitive readback sits ~0.5 dB below where the motor actually parked, and that offset readback was written as the first automation point. Touches are now **anchored to the motor's true position** and write only relative motion, so the first frame is a no-op and the envelope seeds at the real value. Side benefit: the 0 dB detent now lands on exactly 0 dB.

### UC1 buttons step through all discrete options

A UC1 channel-strip button (EQ Type, HF Bell, LF Bell, …) bound to a plug-in parameter with **more than two options** (e.g. SPL Iron's "SC EQ", 6 settings) used to jump first↔last on every press. It now **cycles through every option** one press at a time and wraps back to the first. Plain on/off toggles are unchanged.

## Known issues

- Same as v0.1.18.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.1.19.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.1.19.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.1.19.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).
