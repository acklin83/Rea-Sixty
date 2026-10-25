# Rea-Sixty v0.6.8, "Smooth Operator"

**The faders.** A fix release for the motorised faders: the UF8 and the UF1 reach +12 dB and -inf at their end stops, UF8 faders move REAPER's fader and write automation without stairs, and the short spikes in volume automation when several UF8 faders move at once are gone.

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

- **UF8 and UF1 faders reach +12 dB and -inf** at the end stops. On the UF8 the bottom stopped around -100 dB, a different value on every channel, and a fast push stopped short of +12. The UF1 stopped at +11.9 on track volume, sends and item volume.
- **UF8 faders move smoothly in REAPER.** Every position the UF8 reports is used now, twice as many as before, so REAPER's fader and written automation no longer move in stairs.
- **UF8: no more short spikes in volume automation** when several faders move at once. A fader position that arrived split over two USB packets was put together wrong and moved the fader to a fixed value for one step.
- **UF8:** a held fader no longer jumps back to where it was touched when the touch sensor flickers.

## Known issues

- Windows 11 with Smart App Control on refuses to load Rea-Sixty ("Bad Image", error 0xc0e90002): the extension is not code-signed. Turning Smart App Control off lets it load.
- On a track without the envelope yet, the first pot turn in Touch goes through REAPER's usual path, which creates the envelope, and the value returns once. From the second turn on, the pot writes its point.
- FX Cycle, Instance Cycle and Favourites cycling show their prev/current/next carousel on the UC1 only. On a UF1 the landed plug-in name appears, without the neighbours.
- OBS chapter marks need one of OBS's Hybrid recording formats; on anything else OBS refuses the request and the status line says so.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.6.8.zip`, unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.6.8.zip`, unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from Settings, About on first launch.
- **Linux:** `rea-sixty-linux-v0.6.8.tar.gz`, unpack **all three** files (`reaper_rea-sixty.so`, `libusb-1.0.so.0`, `libhidapi-hidraw.so.0`) into `~/.config/REAPER/UserPlugins/`, keeping them together. Apply the bundled `99-rea-sixty.rules` udev rule, or use the in-app button. No separate dependency install needed.

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately, it is not part of the ReaPack package.
