# Rea-Sixty v0.4.2 — "Reticulating Splines"

**The one where loading a project stops counting through your tracks.** With the 360°-Core impersonator running (gate metering on), opening a project used to select track 14, then 13, then 12… one per second down to 2 — a slow, distracting sweep. That's gone. Plus the UF8 grows a second gain-reduction row for the gate, gate GR now follows the focused channel on both surfaces, and a batch of Nav-Mode fixes.

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

### Metering

- **UF8 gate gain-reduction row.** The UF8 has a second per-strip GR row next to the compressor one; it sat dark. It now shows the focused track's SSL **gate** attenuation, driven from the plug-in via the 360°-Core impersonator, in lock-step with the UC1 gate LEDs. It follows the value directly (no smoothing) so it tracks the gate's fast open/close.
- **Gate GR follows the focused channel.** On both the UC1 and the UF8, the gate GR now shows the *selected* track's channel strip instead of whichever SSL instance happened to connect first — so a gate on the Toms no longer shows the Snare's reduction.

### Load behaviour

- **No more load-time track-selection countdown.** When the impersonator is active (needed for gate metering), each SSL Channel Strip selected its own track as it connected, and with the plug-ins connecting one per second the arrange "counted through" the channels over ~12 s at project load. The plug-ins now connect in a quick burst instead, so the selection settles almost instantly. Metering is unaffected.

### Nav Mode

- **Soft-key jump works with a pinned bank engaged.** With a fixed startup soft-key bank (or any user-Quick) active, the top-soft-key marker/region jumps silently did nothing — the Quick claimed the keys first. Nav Mode now owns the top soft-keys while its overlay is showing.
- **Region jump works with the transport stopped.** Jumping to a region did nothing unless REAPER was playing; it now moves the edit cursor to the region when stopped (and still smooth-seeks during playback).
- **Uncoloured markers/regions are legible.** They rendered dim on the UC1 carousel; they now show at a bright neutral.

### Soft-keys

- **FX dynamic-bank keys are colour-coded by SSL class** — Channel Strip yellow, Bus Comp red, UF8-only blue, plain white.
- **Pinned startup soft-key bank.** Settings → General: engage a fixed user-Quick soft-key bank on boot instead of the plug-in-driven default.

## Known issues

Same as v0.4.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.4.2.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.4.2.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.4.2.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately — it is not part of the ReaPack package.
