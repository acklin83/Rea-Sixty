# Rea-Sixty v0.1.31

Knob-feel follow-up, focused on JSFX. FX-Learn params on JSFX plug-ins now turn smoothly instead of slamming to an extreme, dial down to ultra-fine (sensitivity to 0.01), and stay alive in Fine mode on coarse sliders. Plus: the UC1 CHANNEL encoder no longer "counts up" after a fast scroll, and you can choose whether scrolling past the surface edge banks by 8 or by 1.

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

### JSFX knob feel

- FX-Learn params on **JSFX** plug-ins no longer jump to an extreme — JSFX sliders mis-report their step size, so they're now driven as continuous with a sub-linear acceleration curve. Slow turns aren't sluggish, fast flicks don't overshoot.
- UF8 V-Pots and UC1 encoders now feel **identical** on the same JSFX param.
- **Fine mode** (Shift) keeps working on coarsely-quantised JSFX sliders — a residual accumulator carries sub-step moves until the value actually advances.
- VST3 / AU params are untouched — same behaviour as before.

### Finer FX-Learn sensitivity

- Sensitivity in **FX Learn → Advanced** now goes down to **0.01** (was 0.1), for ultra-fine control. The slider is now the same typeable value box used in Settings → Device.
- Device V-Pot / encoder speed minimum lowered to 0.10.

### UC1 channel-scroll de-lag

- Flicking the CHANNEL encoder across many tracks no longer leaves the 7-segment number + LCD counting up after the turn stops — the display now jumps straight to the landed track, matching the UF8 strips.

### Bank-scroll stride (Settings → Bindings → UF8)

- New option below the UF8 mockup: when a selection scrolls past the surface edge, **by 8** jumps to the next bank (default) or **by 1** slides one channel so the selection sits on the edge strip.

## Known issues

Same as v0.1.30. Nothing new.

## Manual install

If ReaPack isn't an option:

- **macOS (Apple Silicon):** `rea-sixty-mac-v0.1.31.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **macOS (Intel):** use the `*-x86_64.dylib` assets, renamed to `reaper_rea-sixty.dylib` / `libusb-1.0.0.dylib` / `libhidapi.0.dylib`, into the same folder.
- **Windows:** `rea-sixty-win-v0.1.31.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.1.31.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).
