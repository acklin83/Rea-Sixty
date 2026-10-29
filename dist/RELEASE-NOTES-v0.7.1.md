# Rea-Sixty v0.7.1, "Shift Happens"

**A fix release.** With Shift held, or latched with a double-tap on FINE, a key with nothing on Shift does what it does without Shift. PAGE pages again.

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

- **Keys with an empty Shift slot** run their normal action while Shift is held or latched. This stopped PAGE on the UF1, whose PAGE keys also have a long press, and any key whose Shift slot carried only a colour or a label.

## Known issues

- MIDI CC layer: if Settings shows *Device busy* next to the layer's Input, switch the device off as an input in REAPER's MIDI preferences.
- Windows 11 with Smart App Control on refuses to load Rea-Sixty ("Bad Image", error 0xc0e90002): the extension is not code-signed. Turning Smart App Control off lets it load.
- On a track without the envelope yet, the first pot turn in Touch goes through REAPER's usual path, which creates the envelope, and the value returns once. From the second turn on, the pot writes its point.
- FX Cycle, Instance Cycle and Favourites cycling show their prev/current/next carousel on the UC1 only. On a UF1 the landed plug-in name appears, without the neighbours.
- OBS chapter marks need one of OBS's Hybrid recording formats; on anything else OBS refuses the request and the status line says so.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.7.1.zip`, unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.7.1.zip`, unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from Settings, About on first launch.
- **Linux:** `rea-sixty-linux-v0.7.1.tar.gz`, unpack **all three** files (`reaper_rea-sixty.so`, `libusb-1.0.so.0`, `libhidapi-hidraw.so.0`) into `~/.config/REAPER/UserPlugins/`, keeping them together. Apply the bundled `99-rea-sixty.rules` udev rule, or use the in-app button. No separate dependency install needed.

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately, it is not part of the ReaPack package.
