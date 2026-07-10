# Rea-Sixty v0.3.1 — "Pain in All the Diodes"

A companion-focused point release: a native **Bitfocus Companion** module (community-supported), the option to reach the Rea-Sixty bridge over the LAN, and two Stream Deck meter refinements — adjustable readout font size and hardware-matching track-name abbreviation.

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

### Bitfocus Companion module (community)
- A native **[Bitfocus Companion](https://bitfocus.io/companion) module** now talks to Rea-Sixty through the same bridge the Stream Deck plugin uses — so you can drive Rea-Sixty from Companion **without** the Elgato Stream Deck app.
- Actions pulled **live** from your installed version's catalogue (grouped by category), plus raw REAPER command-by-id / by-action-string. Variables (selected track, layer, flip, per-track peak & GR), feedbacks (layer / flip / meter-over-threshold), a **graphical meter bar**, and ready-made presets.
- Provided **as-is / community-supported** — it is not a 1:1 port of the Stream Deck plugin (Companion styles buttons via feedbacks). Source is in the repo's `companion/` folder.

### Reach the bridge over the LAN
- The Rea-Sixty bridge can now bind to all interfaces so **Companion (or the Stream Deck plugin) on a separate machine** can reach REAPER. Off by default (loopback only). Opt in on a trusted network:
  ```lua
  reaper.SetExtState("rea_sixty", "sd_bridge_bind", "lan", true)
  ```
  then restart REAPER.

### Stream Deck meter refinements
- **Font size** control on the Meter tile (Small / Normal / Large / Extra large) — scales the generated dB values and track name so the readouts are legible at your preferred size.
- **Meter track names now follow your Rea-Sixty abbreviation strategy** (Settings → Track-name mode: Truncate vs Smart Abbreviate) instead of a naive truncation, so the key matches what your UF8 / UC1 surface shows.

## Known issues

Same as v0.2.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.3.1.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.3.1.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.3.1.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately — it is not part of the ReaPack package.
