# Rea-Sixty v0.9.0, "Drop it like it's hot"

**Soft keys by drag and drop, and TotalMix on the UF8 as a live controller.** The TotalMix parts come with ORC 1.6.0: update both.

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

### Settings, Bindings: drag and drop

- **UF8 bank matrix:** drag a bank onto another cell, or a set's number onto another set's number. The two swap places, onto an empty place that is a move. Hold Cmd (Alt on Windows) as you let go to copy instead.
- **UF1 bank matrix:** drag a column header onto another to swap two banks, a cell onto another cell to swap two keys, also across banks.
- **UF8 schematic:** drag a soft key, or the LCD under it, onto another key of the bank you are on. Long press, double press and step chains go with it.
- Dragged on Plain, a bank or key goes with its Shift set; dragged on Shift, only the Shift set goes. Sets 8 and 9, keys the SSL plug-in fills and keys of a dynamic bank stay where they are.

### TotalMix on the UF8 and UF1, with ORC 1.6.0

- **SEND/PLUG-IN 1-8** each pick any output as the submix, assigned in ORC. Out of the box they are Phones 1 to 4, Main B, Main and two free keys. A key whose output is hidden in TotalMix is dark and does nothing.
- **Soft-key bank TotalMix submixes:** eight outputs of your choice with their names from TotalMix, set up in ORC.
- **Cue:** SOLO on an output is its Cue, on the UF8 and the UF1. On the UF8, FINE and a submix key cues that output, and so does SHIFT or FINE with a key of a submix bank. The submix stays, and the key turns yellow.
- **New action RME: Link Main B to Main.** Links Main B's volume to Main, as a toggle.
- **UF8 and UF1 each show their own soft-key bank.** One set of banks, and each surface can show another one at the same time.
- **UF8 meters:** a stereo channel shows left and right apart. Both bars used to show the left side.
- **Quick and Layer keys:** while TotalMix, Hue or DynaMount is on the left UF8, the Quick keys and the Layer keys without Shift do nothing. They used to change the bank of a mode that wasn't showing and announce it in the banner.
- **A channel hidden in TotalMix leaves the surfaces** a few seconds later. It used to stay until the next restart.

## Known issues

- Two UF8s has not been run on two units yet. One UF8 works as before, with *Second UF8* off.
- MIDI CC layer: if Settings shows *Device busy* next to the layer's Input, switch the device off as an input in REAPER's MIDI preferences.
- Windows 11 with Smart App Control on refuses to load Rea-Sixty ("Bad Image", error 0xc0e90002): the extension is not code-signed. Turning Smart App Control off lets it load.
- On a track without the envelope yet, the first pot turn in Touch goes through REAPER's usual path, which creates the envelope, and the value returns once. From the second turn on, the pot writes its point.
- FX Cycle, Instance Cycle and Favourites cycling show their prev/current/next carousel on the UC1 only. On a UF1 the landed plug-in name appears, without the neighbours.
- OBS chapter marks need one of OBS's Hybrid recording formats; on anything else OBS refuses the request and the status line says so.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.9.0.zip`, unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.9.0.zip`, unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from Settings, About on first launch.
- **Linux:** `rea-sixty-linux-v0.9.0.tar.gz`, unpack **all three** files (`reaper_rea-sixty.so`, `libusb-1.0.so.0`, `libhidapi-hidraw.so.0`) into `~/.config/REAPER/UserPlugins/`, keeping them together. Apply the bundled `99-rea-sixty.rules` udev rule, or use the in-app button. No separate dependency install needed.

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately, it is not part of the ReaPack package.
