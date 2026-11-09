# Rea-Sixty v0.9.2, "Left Side Story"

**In the Meter view the UF1's fader and the display above it stay with the track, and TotalMix on the UF8 gets PAN, 48V and SEL on the submix.**

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

- **UF1 Meter view:** the fader and the display above it work as in every other view. Track name, dB, pan line, channel number and colour follow a track change, a rename and the fader. Until now they stayed on the track that was there when the Meter view opened.
- **TotalMix on the UF8, with ORC installed:**
  - `PAN` on the left UF8 switches the V-Pots on inputs with a preamp between gain and pan.
  - A V-Pot push on gain switches 48V on and off where TotalMix reports it for the channel, and does nothing on other channels. It used to reset the gain to 0 dB. The value line reads `Gain 48V` while it is on.
  - On the output row, SEL follows the submix picked with SEND/PLUG-IN, the submix bank or nav `◄` `►`, and the UF1 fader goes along with it.
  - With Link A/B on in TotalMix, Main and Main B light together on SEL, SEND/PLUG-IN and the submix bank. The faders still write to the one submix.
  - TotalMix has six layouts, so keys 7 and 8 of a layout bank are empty and send nothing.
  - A layout loaded from a surface takes hidden channels off the surface sooner.
  - Four new actions for any key, each with a lamp: *RME: Input row*, *RME: Playback row*, *RME: Output row*, *RME: FX row*.
- **Fix:** a TotalMix fader moved shortly after connecting, a layout load or a refresh could jump back to its previous value.

## Known issues

- Two UF8s has not been run on two units yet. One UF8 works as before, with *Second UF8* off.
- MIDI CC layer: if Settings shows *Device busy* next to the layer's Input, switch the device off as an input in REAPER's MIDI preferences.
- Windows 11 with Smart App Control on refuses to load Rea-Sixty ("Bad Image", error 0xc0e90002): the extension is not code-signed. Turning Smart App Control off lets it load.
- On a track without the envelope yet, the first pot turn in Touch goes through REAPER's usual path, which creates the envelope, and the value returns once. From the second turn on, the pot writes its point.
- FX Cycle, Instance Cycle and Favourites cycling show their prev/current/next carousel on the UC1 only. On a UF1 the landed plug-in name appears, without the neighbours.
- OBS chapter marks need one of OBS's Hybrid recording formats; on anything else OBS refuses the request and the status line says so.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.9.2.zip`, unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.9.2.zip`, unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from Settings, About on first launch.
- **Linux:** `rea-sixty-linux-v0.9.2.tar.gz`, unpack **all three** files (`reaper_rea-sixty.so`, `libusb-1.0.so.0`, `libhidapi-hidraw.so.0`) into `~/.config/REAPER/UserPlugins/`, keeping them together. Apply the bundled `99-rea-sixty.rules` udev rule, or use the in-app button. No separate dependency install needed.

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately, it is not part of the ReaPack package.
