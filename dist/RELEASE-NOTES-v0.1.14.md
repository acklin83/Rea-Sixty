# Rea-Sixty v0.1.14 — *Dancing in the Dark*

A single, important fix: on Windows, Rea-Sixty no longer touches the process-wide DLL search path — which could crash **other** plug-ins when you opened them. Acustica Audio (Acqua-engine) plug-ins such as **Tiger / Tiger Mix** were the visible casualty: they load helper DLLs from their own install folder, and Rea-Sixty's search-path change hid that folder from them, faulting the plug-in on open.

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

### Bug fixes

- **Windows: Rea-Sixty could crash other plug-ins on open.** To find its own bundled `libusb` / `hidapi`, Rea-Sixty changed the **process-wide** DLL search at load time — and that permanently dropped "search the loading DLL's own directory" for every plug-in in the session. Plug-ins that load sub-DLLs from their own folder (Acustica/Acqua: Tiger, Tiger Mix, …) then couldn't find them and crashed with an access violation on open. Rea-Sixty now pre-loads its own dependencies by full path and **leaves the process DLL search untouched**, so nothing downstream is affected. This is the real cause behind the Acustica crash that the v0.1.13 gain-reduction guard did not resolve.

## Known issues

- During playback, the UF8 region top-soft-key drill is gated by REAPER's smooth-seek queue — the display update + the playhead seek can lag by a region length. Stop transport for snappier drills.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.1.14.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.1.14.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.1.14.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).
