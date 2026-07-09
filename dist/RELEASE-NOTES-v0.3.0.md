# Rea-Sixty v0.3.0 — "Mostly Harmless"

The headline of this release is the **Stream Deck Companion**: control Rea-Sixty from an Elgato Stream Deck over a small local bridge built into the extension. Plus finer control over long JSFX sliders, and two HUD / metering fixes.

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

### Stream Deck Companion
- A **companion Stream Deck plugin** talks to Rea-Sixty through a **localhost bridge** built into the extension — no extra server to run.
- Nine action tiles plus a **Meter tile** (peak / gain-reduction / combined, per track, showing the track name + colour, with an action on press).
- Download `com.reasixty.companion.streamDeckPlugin` from this release and double-click to install into Stream Deck. See the install guide in the repo's `streamdeck/` folder.

### Finer JSFX sliders
- **Fine mode now steps long continuous JSFX sliders by their own native increment** — so REAPER's own JSFX (and any plug-in with a very long slider) can finally be set cleanly, instead of the analog step being too coarse. One detent = one increment, a fast flick accelerates.
- On by default; toggle under **Settings → Plug-ins** ("Fine mode steps JSFX sliders by their native increment"). VST3/AU and normal (non-Fine) turns are unchanged.

## Bug fixes
- **Learn-HUD curve editor no longer glued to the frame.** The "Advanced…" knob-travel curve popup in the Learn-HUD now has the same margin as the right-click menus.
- **UC1 CS gain-reduction strip no longer mirrors the bus compressor.** When the focused track was the bus-compressor anchor, its GR could show up on the Channel-Strip Comp LEDs too. The CS-GR fallback now skips the BC anchor FX, so the strip stays dark (or shows a real CS compressor) while the BC needle keeps the bus GR.

## Known issues

Same as v0.2.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.3.0.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.3.0.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.3.0.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately — it is not part of the ReaPack package.
