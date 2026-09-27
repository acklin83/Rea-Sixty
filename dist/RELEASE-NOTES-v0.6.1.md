# Rea-Sixty v0.6.1, "My whole brain is crying!"

**Automation from the surfaces, and a UF1 that comes back up.** The UF1 now follows automation while it plays and records it in Touch, the same as the UF8. Faders let go in Touch when the hand does, also on pan under FLIP and on plug-in parameters. Pots write their own point into the envelope and leave the value there until the envelope's next point, the way a plug-in parameter on a pot always behaved. The receiver for the SSL plug-ins' own data starts with REAPER, so the UF1 meter view has its goniometer and RTA without a script line. A Sticky Pot pin moves along when a CS or BC switch replaces the plug-in, and the UF1 got a factory bank for the three Sticky keys.

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

## Automation

**The UF1 follows automation.** Its motor fader, the dB and pan readouts on its display and the four volumes of the DAW view show what REAPER plays, envelope included. They used to show the stored track value, which stands still while automation plays, so the UF1 fader stayed put where the UF8's moved.

**The UF1 fader records in Touch.** REAPER is told the fader is held for as long as the hand is on it, as it always was for the UF8. Before, only the eight UF8 strips were reported, and in Touch the automation kept playing under the UF1 fader.

**Faders let go when the hand does.**

- **Pan under FLIP**, on the UF8 and the UF1: the fader now writes pan through REAPER's surface path and reports the touch, so pan automation records and returns on release. It used to be written straight into the track, which REAPER neither records nor holds.
- **A fader on a plug-in parameter** ends its edit on release, and Touch returns the parameter to its envelope like track volume. On the UF8 that covers Plug-in Mode, SSL Strip Mode (and the members of a parameter group it writes to), FLIP onto a focused parameter and a Sticky Pot under FLIP; on the UF1 the Strip Mode fader, the Sticky Pot and the FLIP parameter. This was listed as a known issue in v0.6.0.
- A fader reports as touched only what it writes in that touch. A fader on a plug-in parameter used to report the track's volume as touched as well, and REAPER wrote volume or pan points nobody had moved.

**Pots write a point.** A pot has no touch sensor. In Touch, with the transport running, each turn now writes one point into the envelope at the play position, and the value stays there until the envelope's next point, the way a plug-in parameter on a pot behaves. One undo step per movement. This covers track pan (UF8 V-Pots, the UF1 pot above the fader), track volume on a pot (the UF8 V-Pot and the UF1 pot above the fader under FLIP, the four pots of the UF1 DAW view) and send pan and send volume on pots (UF8, UF1 SENDS, the UF1 as Extender). In Read, Latch and Write nothing changes.

- Volume on a pot under FLIP no longer zigzags towards zero while automation plays.
- The pan value on the UF8's display follows automation the way the ring beside it already did.

## UF1

- **Comes back up after being switched off and on.** The first contact after power-on is ignored by the unit; Rea-Sixty now waits for its answer and opens it a second time when none comes.
- **No fader sweep on connect**, UF1 and UF8. The start-up sequence used to drive every motor fader through its whole travel.
- **An empty project leaves the face alive.** With no track, only the channel zone is blank; the soft-key names, the bank header, the time field and the key lamps stay.
- **Items can take the fader.** Under *Settings, Bindings, UF1, JOG WHEEL*, tick *Fader = Item Volume* on the Items row. With the jog on Items, the fader sets the volume of every selected item: the first one follows the fader, the others change by the same number of dB. The motor follows the first item; with none selected the display reads *no item*.
- **The meter view's soft-key names come back after MODE.** The MODE menu's own names used to stay on the keys after it closed.
- **A Sticky Pot factory bank**: *Pin Sticky*, *Pair Sticky* and *Sticky OnOff*. The UF8 carries the three on the Shift half of its Focus Set bank; the UF1's Focus Set bank is full on both halves, so they have a bank of their own.
- A crash when the UF1 was reopened while its display stream was running is fixed.

## Meter view and the SSL plug-ins' own data

**The receiver starts with REAPER.** Goniometer, RTA and Loudness on the UF1 meter view and the gate gain reduction on the UC1 and on the UF8's second GR row come from the SSL plug-ins themselves, through a receiver that stands in for SSL 360°Core. It used to start only after a `SetExtState` line had been run by hand, and without it the meter view showed REAPER's level and nothing from the plug-in. SSL 360° has to be closed while Rea-Sixty runs, as before: the two use the same ports.

## Sticky Pot

- **A pin follows a CS or BC switch.** A switch puts a different plug-in on the slot, and the same control usually sits at another parameter number there. The pin used to point at the removed plug-in, and the V-Pot fell back to pan. It now moves to the same control on the new plug-in, both targets of a paired pin. A parameter without a counterpart on the new plug-in leaves the pin as it was. In Copy mode (A/B) the pin stays on the original.

## Settings, FX Learn and the Learn HUD

- **The search field stays in view** above the parameter list, in FX Learn and in the Learn HUD, while the list scrolls.
- **Settings, Bindings reopens on the surface tab you used last.** It used to return to the tab it had when REAPER started.

## Known issues

- On a track without the envelope yet, the first pot turn in Touch goes through REAPER's usual path, which creates the envelope, and the value returns once. From the second turn on, the pot writes its point.
- FX Cycle, Instance Cycle and Favourites cycling show their prev/current/next carousel on the UC1 only. On a UF1 the landed plug-in name appears, without the neighbours.
- OBS chapter marks need one of OBS's Hybrid recording formats; on anything else OBS refuses the request and the status line says so.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.6.1.zip`, unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.6.1.zip`, unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from Settings, About on first launch.
- **Linux:** `rea-sixty-linux-v0.6.1.tar.gz`, unpack **all three** files (`reaper_rea-sixty.so`, `libusb-1.0.so.0`, `libhidapi-hidraw.so.0`) into `~/.config/REAPER/UserPlugins/`, keeping them together. Apply the bundled `99-rea-sixty.rules` udev rule, or use the in-app button. No separate dependency install needed.

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately, it is not part of the ReaPack package.
