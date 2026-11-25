# Rea-Sixty v0.9.5, "Lock, Step and Gone"

**TotalMix pans that stop on the centre and stay there, ORC's lamp colours on the UF8, and Lock position and Lock size for the focused-track panel.**

## Install via ReaPack (recommended)

Under Extensions, ReaPack, Manage repositories, Import/export, Import repositories. Paste:

```
https://github.com/acklin83/reaper-scripts/raw/main/index.xml
```

Then Browse packages, `Rea-Sixty`, Install. Restart REAPER. Add the surface under Preferences, Control/OSC/Web, Add, Rea-Sixty.

First-run setup buttons live under Settings, About:

- **Windows:** "Install UF8/UC1/UF1 WinUSB driver" (UAC prompt)
- **Linux:** "Install Linux udev rule" (pkexec prompt)
- **macOS:** nothing extra

## What's new

- **Focused-track panel:** *Lock position* and *Lock size* in its right-click menu. The floating panel then no longer moves or resizes under the mouse, so a stray drag leaves it where it is. Greyed out while the panel is docked.
- **TotalMix, pan:** a pan moves in whole percent and stops on the centre on its way to the other side, on the UF1's pot above the fader, the UF8's V-Pots and the channel pages. At a V-Pot speed other than 1.00 a pan could skip from L1 to R1 and only a push found the centre.
- **TotalMix, starting up:** a UF1 or UF8 that is switched on or comes back on USB can report turns of its jog, encoders and V-Pots that nobody made. For five seconds after Rea-Sixty picks up a surface, its turns do nothing in TotalMix, so Main and the output pans stay where they were. Keys and a fader under the hand work at once.
- **TotalMix, pan log:** every change the UF1 and the UF8 make to an output's pan goes into `rme_pan.log` (Mac: `/tmp`, Windows: `%TEMP%`) with the control that made it, and each turn ignored while a surface starts up as a line with *ignored*.
- **TotalMix, lamps:** the colours set in ORC 1.8 for SEND/PLUG-IN and for the bank keys V-POT and 1-5 show on the UF8 in REAPER too.
- **TotalMix, names:** with more than one RME interface, the snapshot and layout names come from the settings file of the interface TotalMix shows. Until now the newest file was taken, which could belong to another unit, and the names read *Layout 1* and *Mix 1*.

## Known issues

- Two UF8s has not been run on two units yet. One UF8 works as before, with *Second UF8* off.
- MIDI CC layer: if Settings shows *Device busy* next to the layer's Input, switch the device off as an input in REAPER's MIDI preferences.
- Windows 11 with Smart App Control on refuses to load Rea-Sixty ("Bad Image", error 0xc0e90002): the extension is not code-signed. Turning Smart App Control off lets it load.
- On a track without the envelope yet, the first pot turn in Touch goes through REAPER's usual path, which creates the envelope, and the value returns once. From the second turn on, the pot writes its point.
- FX Cycle, Instance Cycle and Favourites cycling show their prev/current/next carousel on the UC1 only. On a UF1 the landed plug-in name appears, without the neighbours.
- OBS chapter marks need one of OBS's Hybrid recording formats; on anything else OBS refuses the request and the status line says so.
- Many REAPER actions make an undo step each time they run, so one turn of the jog in a user mode can make a lot of them.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.9.5.zip`, unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.9.5.zip`, unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from Settings, About on first launch.
- **Linux:** `rea-sixty-linux-v0.9.5.tar.gz`, unpack **all three** files (`reaper_rea-sixty.so`, `libusb-1.0.so.0`, `libhidapi-hidraw.so.0`) into `~/.config/REAPER/UserPlugins/`, keeping them together. Apply the bundled `99-rea-sixty.rules` udev rule, or use the in-app button. No separate dependency install needed.

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately, it is not part of the ReaPack package.
