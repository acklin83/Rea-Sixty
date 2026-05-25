# Rea-Sixty v0.1.1 — ReaPack ready

Maintenance release that makes Rea-Sixty installable through ReaPack on all three platforms. No new surface features over v0.1.0.

## Install via ReaPack (recommended)

```
Extensions → ReaPack → Manage repositories → Import/export → Import repositories
```
Paste:
```
https://github.com/acklin83/reaper-scripts/raw/main/index.xml
```
Then **Browse packages** → `Rea-Sixty` → Install. Restart REAPER. Preferences → Control/OSC/Web → Add → Rea-Sixty.

First-run setup buttons live in **Settings → About**:
- **Windows:** "Install UF8/UC1 WinUSB driver" (one UAC prompt)
- **Linux:** "Install Linux udev rule" (one pkexec prompt)
- **macOS:** nothing extra — IOKit already lets libusb claim the devices

## What changed under the hood

- **Windows DLL loading:** linker `/DELAYLOAD:libusb-1.0.dll /DELAYLOAD:hidapi.dll` plus `SetDefaultDllDirectories + AddDllDirectory` early in `REAPER_PLUGIN_ENTRYPOINT`. All three DLLs now live in `%APPDATA%\REAPER\UserPlugins\` — no more manual copy next to `reaper.exe`.
- **Linux udev:** `reasixty_installLinuxUdevRule` writes `/tmp/rea_sixty_udev.rules` and runs `pkexec` to install + reload udev. Single graphical password prompt.

## Manual install (also still supported)

If you'd rather skip ReaPack, download the matching archive and follow the install path docs:
- Mac: `rea-sixty-mac-v0.1.1.zip` (Developer-ID signed + Apple-notarised)
- Win: `rea-sixty-win-v0.1.1.zip` — drop all three DLLs into UserPlugins (no more program-files dance)
- Linux: `rea-sixty-linux-v0.1.1.tar.gz` — see `INSTALL.txt` inside

## Known Linux quirk (unchanged from v0.1.0)

Plug UF8 and UC1 into **separate PC USB ports**. Daisy-chaining UC1 through UF8's downstream port triggers `usb usb1-port5: disabled by hub (EMI?), re-enabling...` cycles on Linux 6.17 / xhci_hcd. Same hardware works fine on Windows and macOS.
