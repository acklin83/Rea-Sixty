# Rea-Sixty v0.5.9, "Nobody was painting"

**A repair release for the UF1 display and the SSL meters behind it.** Selecting the master froze the whole face: bars, needle, readouts and the small channel peak meter, all standing still until a turn of the channel encoder brought them back. The painter was giving up before it drew anything, and the pacer kept restating its last picture at 25 frames a second, so every stage in between measured healthy while the glass sat there. Around that fix sit five more from the same week, all of them cases where a meter was showing something that was not true: the master's own chain and its monitoring chain reading as one instance, a steady tone vanishing at transport stop, a VU needle parked at rest, a goniometer frozen on its final frame, and on Windows an impersonator that could quietly bind next to a running SSL 360.

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

## Fixed

**The UF1 display froze when the master was selected.** Everything on the glass stopped at once, including the channel peak meter, which has nothing to do with the meter view. Three separate lookups go blank in the same instant for the master and for nothing else: REAPER's `GetSelectedTrack` ignores the master by definition, `GetLastTouchedTrack` points straight at it, and the line that keeps the strip from sticking to the master then discards that answer too. With no track to show, the painter returned before it painted, and the pacer went on repeating its last snapshot. A channel-encoder move healed it every time because it puts a real track back in the selection. The surface now holds the last valid track instead of going blank, and the master is still not followed automatically, which was a deliberate choice.

**An empty project kept showing the last channel.** Delete the final track and the strip sat on "CH 1" with its old levels. The zone is now cleared properly, down to the firmware's own "channel populated" flag and a wipe of the large plane, which otherwise keeps its last image when no chunks arrive. The idle cycle keeps streaming while nothing is there, because the firmware falls back to a lazy redraw if the chain breaks.

**The master's two chains read as one meter.** The master track and its monitoring FX carry separate SSL Meter instances, and REAPER hands back the same track pointer for both. Every peak hold keyed off that pointer, so switching V-Pot 1 between them changed the stream underneath while the held numbers stayed from the previous instance. The instance is the UDP port, and that is what the holds follow now. Part of the same repair: a negative track index arrives as ten varint bytes, and only five were being read, so the master announced itself as a track number nobody could match.

**A steady tone disappeared while the transport was stopped.** The stale-slot rule threw away bars, needle and readouts as soon as a level stopped changing for 300 milliseconds with the transport stopped. Its own comment described a frozen scale floor, but the test never asked whether the value had stopped at the bottom. A held note is unchanging and nowhere near the floor, and it vanished after a third of a second. The floor is now part of the test.

**The VU needle sat at rest.** Minus 120 dBFS was doing two jobs, standing for measured silence and for nothing having answered yet, and the needle's rest-at-silence gate read the second as the first. PPM was unaffected throughout, which is what made it look like a needle problem.

**The goniometer froze on its last frame.** When the Lissajous stream stopped, no image went into the cycle at all, and the device keeps whatever it last received. An instance with no signal has an empty goniometer, and that is what it draws now, after a short delay so the plug-in's periodic gaps do not flash it empty.

**Windows: the impersonator could bind next to a live SSL 360.** `SO_REUSEADDR` means different things on the two platforms. On macOS and Linux it only gets past a leftover socket, and a second bind next to a live listener still fails, which is exactly the signal used to report "360 running?". On Windows the same flag lets the bind succeed anyway, and which of the two sockets then receives a packet is documented as indeterminate. Meter data could go missing with no error anywhere.

**UF1 soft-key 4 did nothing for a plug-in mapped on the UF1 layer alone.** Soft-key 3 worked, which hid it. A UF1-only map is handed the CS2 layout, and that layout's slot 4 carries a fixed built-in action that fires and returns before the user's own parameter is ever read. An explicit slot now owns its key. The RME gain readout also got the ceiling it was missing.

## Configuration

The bindings file stays at version 32. A project and a bindings file from v0.5.8 load unchanged.

## Known issues

- A UF8 plug-in-mode fader assigned to an FX parameter does not return to its envelope in
  Touch during playback.
- FX Cycle, Instance Cycle and Favourites cycling show their prev/current/next carousel on the
  UC1 only. On a UF1 the landed plug-in name appears, without the neighbours.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.5.9.zip`, unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.5.9.zip`, unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from Settings, About on first launch.
- **Linux:** `rea-sixty-linux-v0.5.9.tar.gz`, unpack **all three** files (`reaper_rea-sixty.so`, `libusb-1.0.so.0`, `libhidapi-hidraw.so.0`) into `~/.config/REAPER/UserPlugins/`, keeping them together. Apply the bundled `99-rea-sixty.rules` udev rule, or use the in-app button. No separate dependency install needed.

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately, it is not part of the ReaPack package.
