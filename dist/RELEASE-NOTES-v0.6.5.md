# Rea-Sixty v0.6.5, "My Llama spits Codenames. My Lama not so much."

**Fixes and the RME side-car.** Quick keys edited on Layer 2 or 3 stay on their own layer again, the UC1 input meter no longer stalls the Mixer on tracks with item FX, and letting go of a key no longer wakes a sleeping surface. The RME side-car asks TotalMix for its state twice more after it connects, and two new built-ins put the side-car and that request on any key or Stream Deck button.

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

- **Layers switch Quicks only:** Quick 1 to 3 and the V-POT and Soft 1 to 5 bank keys edited on Layer 2 or 3 in Settings, Bindings were written to Layer 1, so Layer 1's Quick then jumped to the other layer. The key editor, the right-click menu (copy, paste, reset) and the tile colours now use the layer the key fires on. Quicks that already landed on Layer 1 stay there until you set them again.
- *Settings, Behaviour, Soft-keys, Use current hardware bank* took Layer 1 under *Layers switch Quicks only*. It takes the layer the surface is on now.
- **UC1 input meter on tracks with item FX:** the meter read such a track through REAPER's audio accessor, which runs the item FX on REAPER's main thread with the same plug-in instances that play, and reset them on every read. Changing channels in the Mixer took a second or two and the audio broke up. On a track where an item's active take carries FX the meter now skips that read; drop the Rea-Sixty Input Level JSFX on the track to meter it there.
- **Sleep:** letting go of the key that had just put the surfaces to sleep (**Sleep now**) woke them again. A release no longer wakes; a press, a turn or a touch does.

## RME side-car

- After connecting, Rea-Sixty asks TotalMix for its whole state twice more, 3 and 10 seconds later.
- New built-ins, under RME in the picker and on the Stream Deck: **RME: Ask TotalMix again** does what the button of the same name on the Connection page of ORC's settings does. **UF1 Mode: RME** switches the UF1 to the side-car and back, as SHIFT + MODE, RME does. Both work from any surface.
- The factory Output page carries Crossfeed, Delay, Ref Level and FX Return. Pan stays on the V-Pot above the fader, and the second Output page that held FX Return alone is gone. Pages you arranged yourself stay as they are.
- Keys of a TotalMix snapshot or layout bank light in the key's own colours: Active for the loaded snapshot or layout, Inactive for the others. Without colours set they stay white.

## Known issues

- Windows 11 with Smart App Control on refuses to load Rea-Sixty ("Bad Image", error 0xc0e90002): the extension is not code-signed. Turning Smart App Control off lets it load.
- On a track without the envelope yet, the first pot turn in Touch goes through REAPER's usual path, which creates the envelope, and the value returns once. From the second turn on, the pot writes its point.
- FX Cycle, Instance Cycle and Favourites cycling show their prev/current/next carousel on the UC1 only. On a UF1 the landed plug-in name appears, without the neighbours.
- OBS chapter marks need one of OBS's Hybrid recording formats; on anything else OBS refuses the request and the status line says so.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.6.5.zip`, unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.6.5.zip`, unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from Settings, About on first launch.
- **Linux:** `rea-sixty-linux-v0.6.5.tar.gz`, unpack **all three** files (`reaper_rea-sixty.so`, `libusb-1.0.so.0`, `libhidapi-hidraw.so.0`) into `~/.config/REAPER/UserPlugins/`, keeping them together. Apply the bundled `99-rea-sixty.rules` udev rule, or use the in-app button. No separate dependency install needed.

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately, it is not part of the ReaPack package.
