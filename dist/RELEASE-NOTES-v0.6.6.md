# Rea-Sixty v0.6.6, "Orcs have Layers."

**Two fixes to v0.6.5.** *UF1 Mode: RME* is in the Settings picker, and clicking or pressing a Quick key in Settings to edit it no longer throws you onto another layer.

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

## Fixes

- **UF1 Mode: RME** is in the picker under Hardware Modes, next to the other UF1 modes. In v0.6.5 it sat under RME, which Rea-Sixty's picker does not show, so only the Stream Deck reached it.
- **Quick keys in Settings, Bindings:** a click on a Quick tile, or a press on the Quick key while the Bindings pane is open, also runs its binding. A Quick that engages a set on another layer therefore switched the surface there, the editor followed, and you edited that layer's Quick instead of the one you clicked. Such a key is now only selected; a Quick whose set is on the current layer still engages it. The same goes for the V-POT and Soft 1 to 5 bank keys.
- The Soft-Key Sets say where they live: *Soft-Key Set 2 (Layer 2, Quick 1)*. Set 1 is Layer 1's Quick 3, Sets 2 to 7 are Layers 2 and 3, Sets 8 and 9 are Layer 1's Quick 1 and 2.

## The UF1's RME mode is ORC

RME mode on the UF1 is [ORC](https://github.com/acklin83/ORC) running inside REAPER, with ORC's settings. Its soft keys and banks, what its transport keys do, and every RME action, *Ask TotalMix again* included, are set in ORC only. Rea-Sixty's Settings do not show them. Rea-Sixty's part is getting into the mode: SHIFT + MODE, RME on the UF1, or **UF1 Mode: RME** on any key.

## Known issues

- Windows 11 with Smart App Control on refuses to load Rea-Sixty ("Bad Image", error 0xc0e90002): the extension is not code-signed. Turning Smart App Control off lets it load.
- On a track without the envelope yet, the first pot turn in Touch goes through REAPER's usual path, which creates the envelope, and the value returns once. From the second turn on, the pot writes its point.
- FX Cycle, Instance Cycle and Favourites cycling show their prev/current/next carousel on the UC1 only. On a UF1 the landed plug-in name appears, without the neighbours.
- OBS chapter marks need one of OBS's Hybrid recording formats; on anything else OBS refuses the request and the status line says so.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.6.6.zip`, unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.6.6.zip`, unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from Settings, About on first launch.
- **Linux:** `rea-sixty-linux-v0.6.6.tar.gz`, unpack **all three** files (`reaper_rea-sixty.so`, `libusb-1.0.so.0`, `libhidapi-hidraw.so.0`) into `~/.config/REAPER/UserPlugins/`, keeping them together. Apply the bundled `99-rea-sixty.rules` udev rule, or use the in-app button. No separate dependency install needed.

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately, it is not part of the ReaPack package.
