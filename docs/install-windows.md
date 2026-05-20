# Installing Rea-Sixty on Windows

Short reference. The full procedure incl. the SSL 360° driver swap
lives in `user-manual.md` chapter 2.3.

## Prerequisites
- REAPER (recent x64 build)
- **ReaImGui** from ReaPack — Extensions → ReaPack → Browse packages →
  ReaImGui → Install. Without it the Settings window stays empty
  (hardware bindings still work).
- UF8 and/or UC1 plugged in
- **Visual C++ Runtime 2015-2022** (usually present on Win10/11). If
  not: [install vc_redist.x64.exe](https://aka.ms/vs/17/release/vc_redist.x64.exe)
- SSL 360° **not running** — and its kernel driver will be replaced
  during install (see step 2 below)

## 1. Drop all three DLLs into UserPlugins

From v0.1.1 on, the Rea-Sixty DLL extends REAPER's DLL search path
to its own directory at load time (`SetDefaultDllDirectories` +
`AddDllDirectory`) and delay-loads `libusb-1.0.dll` / `hidapi.dll`,
so all three live in UserPlugins together:

```
%APPDATA%\REAPER\UserPlugins\reaper_rea-sixty.dll
%APPDATA%\REAPER\UserPlugins\libusb-1.0.dll
%APPDATA%\REAPER\UserPlugins\hidapi.dll
```

Typically `C:\Users\<your-user>\AppData\Roaming\REAPER\UserPlugins\`.

(Installing via ReaPack puts them there automatically — see the
README for the ReaPack repo URL.)

Restart REAPER. Open Action List (`?`) → "Rea-Sixty". You should see
**"Rea-Sixty: Open / Close Rea-Sixty Settings"** in the list.

## 2. WinUSB driver swap (one-time, admin)

1. Action List → **"Rea-Sixty: Open / Close Rea-Sixty Settings"**.
2. Settings → **About** tab → **"Windows USB driver"** section.
3. Click **"Install UF8/UC1 WinUSB driver"**.
4. UAC prompt → accept.
5. **"Publisher unknown"** warning (the INF is unsigned) → "Install
   anyway".
6. Unplug + replug UF8 and UC1.
7. Restart REAPER.

Surface should come up.

## Uninstall

```
del %APPDATA%\REAPER\UserPlugins\reaper_rea-sixty.dll
```
Restart REAPER. To fully revert the USB binding to SSL 360°, run
SSL 360°'s installer — it reinstalls SSLBUS and takes the devices back.

## Failure modes

- **"Rea-Sixty" action doesn't appear in REAPER** — the DLL didn't
  load. Most common: `libusb-1.0.dll` or `hidapi.dll` missing from
  `%APPDATA%\REAPER\UserPlugins\`, or VC++ Runtime missing.
- **Settings window opens blank** — ReaImGui isn't installed. Install
  via ReaPack.
- **Hardware doesn't respond after driver swap** — UF8/UC1 weren't
  re-enumerated. Unplug/replug both devices, then restart REAPER.
- **SSL 360° also stops working after install** — expected. The
  WinUSB and SSLBUS drivers are mutually exclusive on the same
  device. Reinstall SSL 360° to revert.
