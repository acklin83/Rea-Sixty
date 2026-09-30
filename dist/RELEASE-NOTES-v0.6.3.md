# Rea-Sixty v0.6.3, "Where we're going we don't need no codenames"

**Fixes for things that moved.** Tracks inserted above an SSL plug-in no longer shift its gate GR, EQ curve and meter onto another track. Each open project tab now keeps its own Selection Sets and the rest of the state saved in the project, and saving from another tab no longer empties them. On Windows and Linux the driver installer is back in Settings, About.

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

## SSL plug-ins follow their tracks

**Insert or move tracks and the plug-in data stays on its own track.** Rea-Sixty learned each SSL plug-in's track number once, when it connected. The plug-ins report a new number when tracks are renumbered, and that was ignored, so after inserting two tracks above, the gate GR of four drum tracks showed two tracks further up on all three surfaces until the project was reloaded. It now follows the renumbering. The same applied to:

- the gate GR on the UC1, the UF8's second GR row and the UF1,
- the EQ curve on the UF1,
- the meter instance on the UF1's meter view,
- the HQ and A/B switches, which pressed the plug-in on another track.

## Project tabs

**Each open project tab keeps its own state.** A Selection Set saved in one project was gone after a detour through a new tab, and saving that project afterwards wrote the empty slots into its file. Now every tab keeps its own:

- Selection Set slots and the recalled slot,
- Focus Set and its pin,
- Parameter Groups (on/off and names),
- Sticky Pot pins,
- the Channel Strip and Bus Comp favourite memory, the project favourite bank and the per-track favourite sets.

Saving a project writes its own state, whichever tab is in front.

## Settings, About

**The Windows USB driver and the Linux udev rule are back**, both with Install and Uninstall, and so are Logs and Acknowledgements. Since v0.6.1 they were only shown with a developer setting on. The udev text names the UF1 too; the rule always covered it.

## RME side-car on the UF1 (macOS)

**Transport keys show what they do.** A transport key you gave a TotalMix action in ORC runs it in REAPER too, and its lamp now shows that action's state in the colours set in ORC. A transport key ORC leaves free stays REAPER's, lamp included. Soft-key colours set in ORC show in REAPER as well. Colours need ORC 1.1.0.

## Known issues

- Windows 11 with Smart App Control on refuses to load Rea-Sixty ("Bad Image", error 0xc0e90002): the extension is not code-signed. Turning Smart App Control off lets it load.
- On a track without the envelope yet, the first pot turn in Touch goes through REAPER's usual path, which creates the envelope, and the value returns once. From the second turn on, the pot writes its point.
- FX Cycle, Instance Cycle and Favourites cycling show their prev/current/next carousel on the UC1 only. On a UF1 the landed plug-in name appears, without the neighbours.
- OBS chapter marks need one of OBS's Hybrid recording formats; on anything else OBS refuses the request and the status line says so.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.6.3.zip`, unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.6.3.zip`, unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from Settings, About on first launch.
- **Linux:** `rea-sixty-linux-v0.6.3.tar.gz`, unpack **all three** files (`reaper_rea-sixty.so`, `libusb-1.0.so.0`, `libhidapi-hidraw.so.0`) into `~/.config/REAPER/UserPlugins/`, keeping them together. Apply the bundled `99-rea-sixty.rules` udev rule, or use the in-app button. No separate dependency install needed.

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately, it is not part of the ReaPack package.
