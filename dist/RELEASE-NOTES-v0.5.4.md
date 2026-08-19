# Rea-Sixty v0.5.4, "Phantom Power"

**The UF1 records, and Rea-Sixty stops describing hardware you do not own.** REC is a real mode on the UF1 now: SEL arms the channel on the fader, and the pot above that fader reaches into an RME interface for gain, phantom, pad and input channel. On the other side of the release, the Settings window learned which surfaces this computer has actually had attached and hides the rest, so a UF1-only rig no longer scrolls past UC1 calibration and UF8 pot feel. Between the two, planning the RME controls flushed out five defects, one of which had been silently wrong on every UF1 since the small display existed.

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

### UF1: REC is a mode, and it reaches your preamp

Switch the UF1 into REC and SEL arms the channel the fader side is showing, with the SEL lamp red and bright while armed. It is the same selection mode the UF8 has always had, not a separate action, so the existing mode actions switch it and nothing new is seeded onto your keys.

With an RME interface and TotalReaper running, the same mode hands you the preamp of that channel. The V-Pot above the fader sets gain, Shift on it walks the input channel, and CUT and SOLO carry whatever you assign them: phantom, pad, phase, AutoLevel. Both rotation controls default **on** here, unlike the UF8's and the UC1's. On a surface with exactly one knob above the fader, in a mode you switched on deliberately, a knob that still pans is a dead control rather than a preference.

Settings, Modes, REC now says whether TotalReaper was detected, and hands you to ReaPack when it was not. Both packages come from the same repository, so it is one click.

### The Settings window hides surfaces you have never had

With only a UF1 on the desk, the Devices pane no longer offers UC1 GR calibration or UF8 pot feel, the Behaviour pane drops the UF8 and UC1 lines, the Bindings pane shows one surface tab, and the REC block shows one third of itself. The search box hides them too, so a search cannot land you on a control the page does not draw.

It **remembers** rather than checks. Unplugging a surface for an afternoon leaves every one of its settings exactly where it was; only a surface this computer has never seen is hidden. That is why it needs no setting to switch on, and why two buttons exist for the edge cases: **Show settings for devices you don't have** brings everything back, and **Forget devices that aren't connected** shrinks the list when a surface leaves the studio for good. Each appears only when it would do something.

### UF1: the fader half and the plug-in half address different tracks

The UF1's panel is split. The fader, its meter, the small display, SOLO, CUT, SEL and the pot above the fader belong to the channel on the fader; the four V-Pots, the four display soft keys and the EQ graph follow the selection. The painting moved to that rule earlier; the **actions** had not, so in Extender mode CUT lit for one track and muted another. Both halves agree now, and MASTER outranks the Extender on both.

### Fixes the REC round flushed out

- **The small display's bar never worked, for anything.** Not pan, not a Sticky Pot, not a preamp. The startup sequence set the bar style to "off" and nothing ever set it back, so every position sent to it drew nothing.
- **Changing a track's input left the preamp readout describing the previous channel.** TotalReaper (v0.2.2, same repository) now notices the change itself and asks for a fresh dump.
- **Turning the gain knob flashed TRIM and the channel number on the UF8.**
- **Both Settings tab restores were dead** after closing and reopening the window, so the pane and the surface tab were forgotten every time.
- **Pan felt sluggish on the UF8** next to the UF1: half the resolution per detent. Both surfaces use the same law now, and pan draws as a single travelling line on both instead of filling from the middle.

### Smaller things

- **Toggle V-Pots to Pan** no longer appears in the UF1's action picker. It writes state only the eight-strip surfaces read, so bound to a UF1 key it did nothing at all, silently.
- The REC settings text is a set of lines rather than a paragraph.
- The manual's MIDI chapter described a virtual MIDI port that the extension does not open, and two message types it cannot send. MIDI bindings go to REAPER's own outputs, chosen by name or all at once.
- The manual's install filenames stopped naming a version three releases old.

## Known issues

- A UF8 plug-in-mode fader assigned to an FX parameter does not return to its envelope in
  Touch during playback.
- FX Cycle, Instance Cycle and Favourites cycling show their prev/current/next carousel on the
  UC1 only. On a UF1 the landed plug-in name appears, without the neighbours.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.5.4.zip`, unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.5.4.zip`, unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from Settings, About on first launch.
- **Linux:** `rea-sixty-linux-v0.5.4.tar.gz`, unpack **all three** files (`reaper_rea-sixty.so`, `libusb-1.0.so.0`, `libhidapi-hidraw.so.0`) into `~/.config/REAPER/UserPlugins/`, keeping them together. Apply the bundled `99-rea-sixty.rules` udev rule, or use the in-app button. No separate dependency install needed.

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately, it is not part of the ReaPack package.
