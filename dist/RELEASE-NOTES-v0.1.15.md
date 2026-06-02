# Rea-Sixty v0.1.15

Metering-accuracy + EXT FUNCS release. The new "Rea-Sixty Input Level" JSFX probe gives the UC1 a true input meter for whatever flows into the plug-in — live even while the transport is stopped. On top of that, three calibration fixes make the UC1 I/O meter read honestly: an LED at its labelled level (e.g. −18 dBFS) now actually lights at that level, the output meter no longer flickers at a steady boundary, and a snare transient no longer under-reads. Plus: the FX-Learn "+New" plugin picker is now grouped by vendor, and the UC1's hidden EXT FUNCS menu (BACK button, CS mode) is now user-curatable per plug-in.

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

### True UC1 input metering — "Rea-Sixty Input Level" JSFX probe

The extension's normal input meter reads samples at the project playhead via AudioAccessor, so it only ever sees recorded/playing media and is gated on the transport running — it can't meter a live armed/monitored input while stopped. The new bundled **Rea-Sixty Input Level** JSFX is a pass-through probe: drop it anywhere in a track's FX chain (including the input/record FX chain) and the extension reads its L/R peak instead, feeding the UC1's dedicated input meter with whatever signal flows *into* the probe — live hardware input, an upstream plug-in's output, or a media source, transport running or not. The probe is auto-deployed to REAPER's Effects folder; absent the probe the extension falls back to the AudioAccessor path.

### UC1 I/O meter — three calibration fixes

- **LED thresholds light at their label.** A tone at exactly the labelled level (e.g. a −18 dBFS signal on the −18 LED) now lights it. The measured peak sits a sliver below true (discrete sample peaks miss the sine crest + float rounding in 20·log10), so a bare comparison left the −18 LED dark until ~−17.9. A 0.1 dB tolerance closes the gap.
- **No more boundary flicker on the output meter.** A steady tone sitting on a threshold jittered the boundary LED as the output peak (read straight from `Track_GetPeakInfo`) crossed it tick to tick. A 0.6 dB hysteresis debounces it — once lit, an LED stays lit until the level drops below its on-threshold.
- **Transients no longer under-read.** The probe's peak-hold previously decayed before the host's ~30 Hz read caught it, so a −12 dBFS snare metered −15. It now holds the latched peak flat (120 ms window) long enough to be read accurately before the decay release begins.

### FX-Learn "+New" picker grouped by vendor

The "+New" plugin picker now groups installed plug-ins by vendor (alphabetical, plug-ins alphabetical within each vendor), exactly like the main FX-Learn map dropdown. Typing in the filter auto-expands matching vendor groups and hides empty ones.

### UC1 EXT FUNCS — user-curatable per plug-in (CS mode)

The UC1's hidden BACK-button drill-down menu (EXT FUNCS) was a fixed list of 10 SSL Channel-Strip params. For a user-mapped (non-SSL) channel-strip plug-in you can now fill 10 slots with your own name + param assignment via a 2×5 grid under the UC1 mockup in the FX-Learn editor. Empty slots are skipped on the surface; the carousel shows up to 11 characters of each name, the LCD header the full name. SSL plug-ins keep their built-in EXT FUNCS list unchanged.

## Known issues

- An EXT FUNCS param that's also on a physical V-Pot uses the plain encoder path — it doesn't share the V-Pot's range/curve/sensitivity or param-group broadcast. SSL slots keep full knob-travel.
- Otherwise same as v0.1.14.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.1.15.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.1.15.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.1.15.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).
