# Rea-Sixty v0.9.4, "No Country for Main Actions"

**REAPER actions from the MIDI Editor, the MIDI Event List and the Crossfade Editor on any key and in the UF1 jog modes, the keyboard modifiers on Linux, and SEL lit again after leaving a send mode.**

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

- **REAPER actions outside Main:** a REAPER Action binding has a *Section* menu above the Action ID: *Main*, *MIDI Editor*, *MIDI Event List* or *Crossfade Editor*.
  - *Browse Action...* opens REAPER's Action List on that section, *Load ReaScript...* adds the script to that section.
  - MIDI Editor and MIDI Event List actions run in the MIDI editor you used last. With no MIDI editor open the key does nothing.
  - Each section numbers its actions on its own, so changing the section clears the action. Until now a MIDI Editor command ID ran whatever Main action had that number.
  - The action's name and its LED follow the section.
  - On every key of the UF8, UC1 and UF1 and in the UF1 user jog modes. *One action with direction* takes Main actions only, for a MIDI Editor action use *Left / right*.
  - The MIDI Inline Editor and the Media Explorer are not offered: REAPER has no way for an extension to run their actions.
- **Linux, keyboard modifiers:** Shift, Ctrl and Alt on the computer keyboard count on Linux now, until now they did nothing there. That brings the Solo / Cut modifier modes, *Settings, Behaviour, Keyboard*, Alt + fader drag snap back, keyboard Shift for fine and the FX Learn modifier layers to Linux. As on Windows, Alt drives the Cmd slot. Under Wayland the keyboard counts while a REAPER window has the focus.
- **Fix:** leaving a Send or Receive mode on the UF8 by pressing its own key again lights SEL again. Until now the SEL lamps stayed dark until another track was selected. HOME was not affected.

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

- **macOS:** `rea-sixty-mac-v0.9.4.zip`, unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.9.4.zip`, unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from Settings, About on first launch.
- **Linux:** `rea-sixty-linux-v0.9.4.tar.gz`, unpack **all three** files (`reaper_rea-sixty.so`, `libusb-1.0.so.0`, `libhidapi-hidraw.so.0`) into `~/.config/REAPER/UserPlugins/`, keeping them together. Apply the bundled `99-rea-sixty.rules` udev rule, or use the in-app button. No separate dependency install needed.

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately, it is not part of the ReaPack package.
