# Rea-Sixty v0.2.2 — "Trust me - I know what I'm doing!"

The big Favourites release: Channel-Strip *and* Bus-Compressor favourites, named per-track Sets, copy-vs-own switching, plus send/receive automation and a pile of HUD/surface polish.

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

### Favourites
- **Bus-Compressor favourites** alongside Channel-Strip favourites — same 8-slot Switch / Cycle / Copy model, on its own domain.
- **Named favourite Sets, separate CS and BC libraries.** Build sets of favourites and assign one CS set + one BC set per track; tracks with no set fall back to the **Base** bank.
- **Per-project favourite bank** with global fallback, plus per-track and per-favourite settings memory keyed by plug-in identity (survives renames and slot moves).
- **Copy vs Own settings**, per domain — switching/cycling a favourite either carries the live values across or restores each favourite's own remembered settings. CS defaults to Copy, BC to Own; both toggleable.
- **Copy-to-CS-N (A/B):** insert a favourite below the active strip with the values carried and the original bypassed — an instant A/B. Per-section copy mask (EQ / Dyn / Gate / Fader).
- **Unified, domain-aware actions:** Switch / Copy / Cycle to Favourite N follow the focused domain, with one "Favourites" soft-key bank.
- **UC1 favourite-cycle carousel** — cycling favourites shows the same prev / current / next carousel the FX/Instance cycle uses. Bind the left UC1 encoder to CS cycle, the right to BC cycle.
- **Favourites Settings page** with searchable plug-in pickers, set library management, and per-track assignment.

### Sends / Receives
- **Send & receive faders write volume + pan automation** (Touch) and mirror onto the motor fader / V-Pot ring.
- **Send-pan readout fix** — shows the send's effective pan, not the track pan.

### HUD / surface
- **Learn-HUD favourite row:** CS-Fav and BC-Fav dropdowns annotated with the active set source (set name or "Base"); the copy/own field was removed.
- **Focused panel** favourite controls (copy/own + set picker), gated behind Layout options.
- **Additive search everywhere** in Settings — type space-separated words in any order and every word must match (e.g. "favo focu" finds "Favourite … Focused").
- **Soft-key bank fix:** with "Parameter change switches soft-key bank" off, a press now fires the same bank the labels show (no more label/dispatch mismatch on Quick 2).

## Known issues

Same as v0.2.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.2.2.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.2.2.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.2.2.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).
