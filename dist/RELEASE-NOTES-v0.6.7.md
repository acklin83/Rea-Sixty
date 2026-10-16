# Rea-Sixty v0.6.7, "Living in the fast lane"

**Fixed lanes on the surfaces.** REAPER's fixed item lanes, worked from the UF1 and the UF8 while the song plays: hear one take after the other, put the one that sits into the comp, paint a stretch of a take into the comp with the wheel, cut between takes live, change one part of the comp later. Also in this release: the UF8's cursor pad and encoder push get their own bindings per encoder mode, a UF8 or UC1 that does not come up at start is tried again, and the Settings window no longer closes when you click JOG in Bindings. The manual has a whole chapter, *Fixed lanes*.

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

## Fixed lanes

Needs REAPER 7.12 or newer. Works on the selected track.

- **Where it lives:** jog mode **Lanes** on the UF1 (**UF1 Jog Mode: Lanes**) and encoder mode **Lanes** on the UF8 (**Encoder Mode → Lanes**). The UF1's own encoder can run it too; it is unticked in its ring from the factory.
- **The wheel** moves the edit cursor. With `SHIFT` it changes the lane: on a track without a comp lane the next lane plays alone, on one with a comp lane the comp area under the edit cursor takes the next take while the comp keeps playing.
- **The cross:** `↑` / `↓` play the lane above or below alone, `SHIFT` + `↑` / `↓` give the comp area under the edit cursor the lane above or below. `←` / `→` go to the previous or next comp-area or item edge. `SHIFT` + `→` is A/B, back to whatever played before, the whole set of lanes. A tap on the centre plays the comp lane, `SHIFT` + centre is **Comp here**.
- **Comp here** puts the lane you hear into the comp, over the time selection, the loop, or the item under the edit cursor. It never takes from the comp lane itself. One undo step.
- **Painting:** hold the centre and turn the wheel. A razor shows the stretch you run over, turning back shortens it. Let go and that stretch goes into the comp from the lane you hear, one undo step, and REAPER plays it with a second either side.
- **Live comping:** bind **Lanes: live comping on / off** to a key. While the song plays, every lane change is a cut, the part before it goes into the comp at once. A lane heard for less than half a second counts as passed on the way. Stop ends the last part.
- **Looping a cut:** `SHIFT` + `←` loops two seconds around the nearest cut and switches repeat on; again at the same cut switches both back.
- **The Lanes bank:** a dynamic soft-key bank, one key per lane with its name, lit while it plays, the comp lane green. Push plays that lane alone, `SHIFT` + key layers it, Cmd + key is Comp here from it. On a track with a comp lane a push gives the comp area under the edit cursor that take, and `SHIFT` held auditions it. On the UF8 the bank comes by itself while the encoder is in Lanes. On a track without fixed lanes key 1 reads `Lanes on`.
- **Groups:** with REAPER's item grouping on, stepping, A/B and Comp here follow the media-edit group the selected track leads.
- Switching lanes adds nothing to REAPER's undo history. Moving a comp area with `SHIFT` + wheel is one undo step per turn.
- Settings under *Behaviour, Fixed lanes*: comp lane skipped when stepping, the UF8 Lanes bank, scrub while painting (off), play the stroke after painting (on, 1 s either side), snap cuts to the grid (off), cut lead-in (20 ms). Loop length under *Bindings, UF1* with the jog on Lanes.
- Every Lanes function is a built-in under **Lanes** in the picker, for any key.

## UF8 encoder

- **Hold the encoder's push and turn** to pick an encoder mode on the surface: the scribble strips list the modes around the current one, let go and the encoder stays there. Which modes and in which order is set under PUSH in *Settings, Bindings, UF8*.
- **The cursor pad and the push have their own bindings in every encoder mode.** The *Encoder mode* drop-down above the editor switches the mode, on the surface too. Out of the box every mode carries the bindings these keys had before.
- The push fires when you let go, and only if you did not turn. It has no long press any more: held without turning and let go, it does nothing.
- A mode order you set for a ring is kept when a new mode arrives; the new one goes at the end.

## Fixes

- **Settings, Bindings, UF1:** a click on JOG or a key of the nav cross closed the Settings window when the pane was scrolled to the top on a short window.
- A **UF8 or UC1** that does not open when REAPER starts is tried again every 5 seconds, as the UF1 already was. Before, it stayed away until REAPER restarted.
- A **UC1** that dropped off USB while it was being reopened could crash REAPER.
- When many **UC1** lamps change at once, only the newest state of each lamp goes out, not every step in between.

## Known issues

- Windows 11 with Smart App Control on refuses to load Rea-Sixty ("Bad Image", error 0xc0e90002): the extension is not code-signed. Turning Smart App Control off lets it load.
- On a track without the envelope yet, the first pot turn in Touch goes through REAPER's usual path, which creates the envelope, and the value returns once. From the second turn on, the pot writes its point.
- FX Cycle, Instance Cycle and Favourites cycling show their prev/current/next carousel on the UC1 only. On a UF1 the landed plug-in name appears, without the neighbours.
- OBS chapter marks need one of OBS's Hybrid recording formats; on anything else OBS refuses the request and the status line says so.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.6.7.zip`, unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.6.7.zip`, unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from Settings, About on first launch.
- **Linux:** `rea-sixty-linux-v0.6.7.tar.gz`, unpack **all three** files (`reaper_rea-sixty.so`, `libusb-1.0.so.0`, `libhidapi-hidraw.so.0`) into `~/.config/REAPER/UserPlugins/`, keeping them together. Apply the bundled `99-rea-sixty.rules` udev rule, or use the in-app button. No separate dependency install needed.

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately, it is not part of the ReaPack package.
