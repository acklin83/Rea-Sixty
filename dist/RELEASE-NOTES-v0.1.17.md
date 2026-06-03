# Rea-Sixty v0.1.17 — "Off on the Charabanc!"

A metering + ergonomics release. Level meters are now **peak-hold with a per-meter fall rate** you can dial in dB/sec (the UC1 input now decays in lock-step with the output, matching REAPER's own meters). The UC1 **Out Gain** knob can be flipped to drive **REAPER's track volume fader** — handy on tracks with no SSL channel strip. Plus a couple of FX-Learn picker fixes: case-insensitive search, and dropdowns that no longer shrink as you type.

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

### Metering — per-meter peak fall rate

All level meters (UC1 Input + Output, UF8 strip bars) are now **peak-hold**, with a single per-meter setting: how fast each falls back, in **dB per second** (**Settings → Device → Metering**). Default is **26.5 dB/s** — REAPER's own meter decay — so the UC1 input meter now falls in lock-step with the output instead of dropping ~2× too fast. A **Copy Input to Output** button matches the two UC1 meters in one click. (The old Peak / VU / RMS selector is gone — everything is peak now.)

- **Output meter under-read fixed.** A double-read of REAPER's peak info per tick was returning a microsecond window on the second call, so a –6 dB signal only lit to –9. The focused track's peak is now cached and reused.

### UC1 Out-Gain → REAPER fader toggle

The UC1 **Out Gain** knob can now be flipped between its SSL Channel-Strip *Fader Level* parameter and **REAPER's track volume fader**. While engaged, the knob drives track volume even on tracks with no channel-strip plug-in, and the LED ring + readout follow the track fader. Bind it from the Bindings picker (under *Hardware Modes*) or run it as the REAPER action **"Rea-Sixty: Toggle UC1 Out-Gain (Mapped ↔ REAPER Fader)"** from the keyboard / toolbar.

### FX-Learn picker fixes

- **Case-insensitive parameter search** — typing "out" now finds "Output Gain". (All other search boxes already were.)
- **Dropdowns no longer shrink** as the search narrows the list — the param picker popup keeps a stable width instead of slowly closing in on the results.

### Bug fixes & smaller changes

- **EQ-IN / DYN-IN inverted button** no longer dims the knob-ring cascade in reverse — the section-bypass dim now honours the button's Inverted flag.

## Known issues

- Same as v0.1.16.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.1.17.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.1.17.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.1.17.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).
