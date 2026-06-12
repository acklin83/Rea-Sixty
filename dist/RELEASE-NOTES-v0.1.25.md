# Rea-Sixty v0.1.25 — "Catch the Game Last Night?"

The Master bus gets first-class treatment — pin it to a UF8 strip, drive its fader from the UC1 Out-Gain knob, and see its Bus Compressor in the BC carousel. Plus a new on-screen marker that shows which plug-in is the active CS / BC right in REAPER's Inserts list, an encoder mode to shuffle FX up/down in the chain, and combined gain-reduction metering across the whole channel.

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

### Master track

- **Show Master as Track 0 on UC1.** The CHANNEL encoder scrolls onto the Master bus before track 1. (Settings → Device.)
- **UC1 Out-Gain drives the Master fader** when the Master is focused.
- **Bus Compressor on the Master** shows in the UC1 BC carousel like any other track.
- **Pin Master to a UF8 strip.** New built-ins / REAPER actions pin the Master to Strip 1 or Strip 8, with a Replace-strip / Shift-banking choice (Settings → Device). The pinned strip's scribble reads "MASTER".

### Inserts

- **Active CS / BC marker.** Opt-in (Settings → Device): marks the active Channel Strip and Bus Compressor plug-in in REAPER's TCP/MCP Inserts list, so you can see at a glance which copy the surface is driving. Marker style is selectable; markers auto-strip when you turn it off.

### Encoder

- **Move FX in chain.** New encoder mode (`Encoder Mode → FX Move (in chain)`) and direct-rotation action (`fx_move`) shuffle the active FX up/down within the focused track's chain. Plus the existing `plugin_move_up` / `plugin_move_down` one-shots.

### Metering

- **Combine GR across plug-ins.** Optionally sum the gain reduction of every compressor on a track (CS + ReaComp + …) instead of a single source. Separate toggles for the UF8 strips and the UC1 Comp meter (Settings → Device).

### Settings & polish

- Device pane tidied (label-first dropdowns, shorter combos).
- Searchable combo popups no longer jump width.
- "Plug-in" spelt consistently throughout the UI and manual; encoder-mode picker labels unified.

## Known issues

- Same as v0.1.18.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.1.25.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.1.25.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.1.25.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).
