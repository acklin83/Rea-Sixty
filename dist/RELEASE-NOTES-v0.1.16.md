# Rea-Sixty v0.1.16 — "Boy, that escalated quickly!"

What started as a one-line meter fix turned into a big one. This release brings **knob "feel" presets** (save a tuned range/curve/sensitivity and reuse it across any knob or V-Pot), an **in-app user manual** (Settings → Manual), and a stack of UC1 FX-Learn fixes: the IN button is finally sane, param-toggle buttons honour invert + custom names, the Polarity button can be bound to a plug-in param, and the I/O meter no longer goes dark when the Bus Compressor is bypassed.

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

### Knob "feel" presets

A tuned "feel" — invert, Min/Max range, sensitivity, response curve, polarity, push-reset — can now be saved into one of five **named, global preset slots** and re-applied to any other knob or V-Pot, on any plug-in. Save a Log frequency sweep or a slow stepped detent once and stamp it anywhere. From the FX-Learn right-click menu: **Save feel to**, **Apply feel from**, **Clear preset**, **Reset feel to default**. The slots are shared across UC1 knobs and UF8 V-Pots and survive restarts — a feel saved on a UC1 knob applies to a UF8 V-Pot and back. Bindings and display labels are intentionally not part of a preset.

### In-app user manual

The full user manual now renders inside the app at **Settings → Manual** — two-level chapter navigation on the left, the selected chapter on the right, with bold/monospace text and tables. It's baked from the same source as the PDF, so it always matches the installed build.

### UC1 IN button — fixed

The Channel-IN button is now mapped to the CS **Bypass** slot (matching the Bus-Comp IN button and what it actually does at runtime), instead of the confusing "Input Trim" label that clashed with the Input Level meter. Its FX-Learn popup no longer throws a Dear ImGui "conflicting ID" error, no longer shows doubled rows, and no longer bleeds the knob-style editor onto the toggle.

### UC1 buttons — invert + custom names

- The FX-Learn **Inverted** toggle now actually works on param-toggle buttons (EQ In, Dyn In, Polarity, Fast Attack, etc.) — it flips the LED + readout sense, mirroring the knob path.
- **Custom display labels** now stick on buttons, the same way they already did on knobs (the readout used to revert to the hard-coded name).

### Polarity button — optionally bindable

The UC1 **POL** button toggles REAPER track phase by default, as always. A new **"Reaper Track Polarity"** checkbox (default on) in its FX-Learn right-click menu lets you turn that off per plug-in and bind POL to a plug-in param instead, like any other button.

### Bug fixes & smaller changes

- **UC1 I/O meter no longer blanks on BC bypass.** The meter is track-level (media input + post-FX output) and now keeps reading regardless of the Bus Compressor's bypass state.
- **Re-binding a slot clears its custom label** (the label named the old param), so the field and the readout stay in agreement.
- **AutoLearn** no longer lists the dead SSL "Quick 1–6" wrapper slots (no UF8/UC1 hardware behind them).
- **Docs:** corrected the macOS note — Intel Macs are supported via build-from-source; only the pre-built binaries are Apple Silicon only.

## Known issues

- Same as v0.1.15.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.1.16.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.1.16.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.1.16.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).
