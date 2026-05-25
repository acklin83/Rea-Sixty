# Rea-Sixty v0.1.0 — first public beta

REAPER extension that drives the **SSL UF8 and UC1** control surfaces natively, without requiring an SSL plug-in on every track. Native track colours on the UF8 scribble strips, full SSL 360°-style colour-aware Plug-in Mixer mode, UC1 Channel Strip + Bus Comp encoder sections, REC + RME (TotalReaper) integration, custom bindings, 8 persistent parameter groups, and a Settings UI built on ReaImGui.

This is a **beta** — feedback welcome via [issues](https://github.com/acklin83/Rea-Sixty/issues).

## Quick install

ReaPack is not the install path for v0.1.0 (libusb/hidapi placement on Windows + udev on Linux can't be automated by ReaPack). Manual install per the platform archive below:

### macOS (Apple Silicon)
Download `rea-sixty-mac-v0.1.0.zip`. Unzip. Drop all three files into:
```
~/Library/Application Support/REAPER/UserPlugins/
```
Install [ReaImGui](https://github.com/cfillion/reaimgui) via ReaPack inside REAPER. Restart REAPER. Preferences → Control/OSC/Web → Add → **Rea-Sixty**. The dylibs are Developer-ID signed and notarised by Apple so Gatekeeper accepts them on first load.

Intel Mac is not in this build. Reach out if you need it.

### Windows (x64)
Download `rea-sixty-win-v0.1.0.zip`. Unzip. Place files **at different paths**:
```
reaper_rea-sixty.dll  →  %APPDATA%\REAPER\UserPlugins\
libusb-1.0.dll        →  C:\Program Files\REAPER (x64)\
hidapi.dll            →  C:\Program Files\REAPER (x64)\
```
The two runtime DLLs MUST be next to `reaper.exe` — REAPER's DLL search path doesn't include UserPlugins. Install ReaImGui via ReaPack. Restart REAPER. Open Settings → About → **"Install UF8/UC1 WinUSB driver"** (one-time elevated step that swaps SSL360Core's driver out). Unplug/replug UF8 and UC1. Preferences → Control/OSC/Web → Add → Rea-Sixty.

Full step-by-step: see `docs/install-windows.md` in the repo.

### Linux (x86_64)
Download `rea-sixty-linux-v0.1.0.tar.gz`. Extract. Then:
```
cp reaper_rea-sixty.so          ~/.config/REAPER/UserPlugins/
sudo cp 99-rea-sixty.rules      /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
sudo apt install libusb-1.0-0 libhidapi-hidraw0   # or the equivalent on your distro
```
Install ReaImGui via ReaPack. Restart REAPER. Preferences → Control/OSC/Web → Add → Rea-Sixty.

**Known Linux issue:** USB stability depends on topology. Plug UF8 and UC1 into **separate PC USB ports**. Daisy-chaining (UC1 → UF8 → PC) triggers `usb usb1-port5: disabled by hub (EMI?), re-enabling...` in dmesg on Linux 6.17 / xhci_hcd — kernel-level port reset that interrupts the session. Same hardware works fine on Windows / macOS.

## What works

- Track colour mirror on UF8 scribble strips (8-colour palette + custom RGB)
- 4× SSL plug-in families (Channel Strip 2, Bus Compressor 2, 4K B, 360° Link) and FX Learn for arbitrary 3rd-party plug-ins
- UF8 fader motor + touch + V-Pot rings + Soft Keys + Layer / Send / Plugin buttons
- UC1 Channel Strip + Bus Comp encoder sections, GR + VU meters, motorised BC fader, magnifier overlay
- 8 persistent parameter groups (multi-track param sync) + temp-from-selection
- Per-bank Soft-Key palettes, V-Pot Readout Bar with bipolar centre-out, virtual notch
- Bindings editor (Settings → Bindings) with keyboard / MIDI / REAPER-action targets, hold/momentary/toggle behaviours, modifier chords (Shift / Cmd / Ctrl), LED colour overrides
- Settings UI (Mixer + 6 sub-tabs: Device / Bindings / Soft-Key Banks / Modes / Selection Sets / About)
- Setup bundle save/restore for porting your whole config between machines
- Nav Mode (Markers + Regions) overlay
- REC + RME TotalReaper integration: per-strip preamp mirror (gain / phantom / pad / phase) + Shift+V-Pot input-channel switch
- Brightness control as bindable Native actions

## Source

Repository: <https://github.com/acklin83/Rea-Sixty>
Build instructions: `docs/install-{macos,windows}.md` and the Linux INSTALL.txt in the tarball.
Issue tracker: <https://github.com/acklin83/Rea-Sixty/issues>
