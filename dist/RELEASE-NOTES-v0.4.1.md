# Rea-Sixty v0.4.1 — "Finest Merchandise"

**The wares are fully on display now.** A fast follow-up to v0.4.0 that finishes the exchange's map detail — a mapping's UC1 EXT FUNCS were being dropped and never shown — adds a way to clear off-face bindings from FX-Learn, and makes re-publishing a mapping update your existing one instead of failing.

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

### Mapping Exchange

- **A mapping's UC1 EXT FUNCS now show up.** The curated hidden-BACK-menu params (Channel Strip mode) were decoupled from the face controls and so were being dropped at ingest — a map's extra plug-in params simply vanished from the exchange. The map detail (in-app and on the web) now lists them as a name → parameter table, and they count toward parameter coverage. Already-uploaded maps light up too, no re-upload needed.
- **Re-publishing updates your existing mapping.** "Publish to exchange" had no way to say "update my map", so a re-publish either failed as a duplicate (unchanged) or spawned a second entry (changed). It now replaces your published mapping for that plug-in. Someone else's identical mapping is still rejected, and the diff feature still keeps distinct maps side by side.

### FX-Learn

- **Clear off-face bindings.** SSL 360 Link slots with no control on the UC1 face — Fader, Pan, the quick-access buttons, Comp Mix, SAT — bind legitimately but never appeared on the mockup, so a stray one (e.g. a parameter double-bound to Comp Mix alongside its real knob) was invisible and impossible to remove. A new **"Also mapped — off-face"** list sits under the schematic with a **Clear** button per binding.

## Known issues

Same as v0.4.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.4.1.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.4.1.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.4.1.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately — it is not part of the ReaPack package.
