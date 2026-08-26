# Rea-Sixty v0.5.7, "Say my name."

**Things that were silent now say what they are.** Switch a soft-key bank on the UF1 and its name flashes across the time display, the way a format change already flashed BARS or TIME. Banks can carry a name of your own, one per bank and per modifier set, and a bank without one announces what kind of bank it is or else its number, so the field always has something true to say. Around that: an unnamed marker or region stops arriving on the surface as a blank strip, the Envelope centre key shows which target the wheel is on, the dynamic-bank pager names both of the keys that page it, and the two keys that cannot be bound now say so, one of them having quietly accepted actions that could never fire. The time display also stopped guessing: its three formats are its own now, independent of whatever REAPER's ruler and transport are set to.

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

### The soft-key bank names itself

Switching the UF1's soft-key bank flashes the bank's name across the ten cells of the time display for about a second, and the clock comes back by itself. On by default, in Settings, Behaviour, UF1.

A bank you named shows that name. A dynamic bank shows what kind it is: `EFFECTS`, `GROUPS`, `COLOURS`, `FAVS`. Anything else shows its number, `SOFT 3`. Holding SHIFT counts as a switch, because a modifier set is a bank of its own, and if that set simply takes Plain's bank its name is the same one and nothing flashes.

Names are typed in Settings, Bindings, UF1, one per bank and per modifier set. The field is never empty: with no name of its own it carries the name the bank will announce anyway, and clearing it puts that back, which is also how a name is undone.

**The UF1 itself is the preview.** While you type, the time display shows the name, so you read the real cells in the real font rather than a drawing of them. That matters because seven segments have no shape for K, M, V, W or X and the font approximates them without saying so, which is why `Mix Keys` arrives as `MIH KEYS`. With no UF1 attached the editor draws the ten cells under the field instead.

### The time display stands on its own

Time, Bars and Samples are the 360 key's own three readings and none of them asks REAPER which unit its ruler or transport is set to. Time is minutes and seconds, which is the reading you compare against.

This also ends a set of wrong readings on negative positions. With a negative project start time, a cursor 26 ms before zero reached the field as `-1:59:59:29`.

### Envelope mode

Where the wheel is driving the play cursor, **Cmd** pulls a time selection: the first detent drops an anchor, the rest drag the far end. That already worked in Playhead mode and now follows the wheel into Envelope, whose centre key hands it back and forth between the envelope's points and the cursor.

Once the cursor has moved, the arrows start from where it now is rather than from a point that was selected before the move, so nudging the playhead and reaching for an arrow lands where you are looking.

The centre key's LED shows which of the two the wheel is on, in the colours you gave that key. It used to be lit in both.

### Nav Mode

A marker or region with no name of its own reads `Region 3` or `Marker 7`, on the number REAPER shows in the ruler. Until now it arrived on the surface as an empty strip and read as missing, even though its number was in the channel-number zone the whole time. The UC1 carousel and the region readout use the same fallback.

### The focused-track panel

Two new elements in the right-click Elements menu, both off by default: **UF1 encoder mode** and **UF1 jog mode**. Both are rings you scroll blind under a held key, so this is the only always-on readout of them, and the UF1's channel encoder runs its own ring, separate from the UF8's.

### Editors

- The FX-Learn page row stopped fighting its own click. The `+` that starts a new page was dead whenever the UF1 happened to be showing the plug-in being edited. Both editors also move the device onto a page once something is bound there.
- Three text fields could write onto the wrong object when the thing they belong to changed under the cursor: the bank name, FX Learn's Display label and the UF1 cell's Display name. A bank name walked from bank to bank as you paged.
- The MODE-hold menu takes the soft-key highlight with it, so a dynamic bank's selection no longer sits under the mode names.
- `SCRUB` is greyed out like `MODE`. Holding it picks the Jog Mode, and the key never reached the binding system, so an action put there could never fire. Both keys say what they do on hover.
- The dynamic-bank pager line names both page arrows. It named one and read as a claim that only that one pages.
- Settings, Modes, NAV lists every key bound to each Nav toggle, by silk-screen label.
- The UF1's Strip Mode window follows a channel change. Only the UF8's did.

## Configuration

The bindings file moves to version 32. The addition is the soft-key bank names and nothing else; a configuration from an earlier version loads unchanged.

## Known issues

- A UF8 plug-in-mode fader assigned to an FX parameter does not return to its envelope in
  Touch during playback.
- FX Cycle, Instance Cycle and Favourites cycling show their prev/current/next carousel on the
  UC1 only. On a UF1 the landed plug-in name appears, without the neighbours.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.5.7.zip`, unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.5.7.zip`, unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from Settings, About on first launch.
- **Linux:** `rea-sixty-linux-v0.5.7.tar.gz`, unpack **all three** files (`reaper_rea-sixty.so`, `libusb-1.0.so.0`, `libhidapi-hidraw.so.0`) into `~/.config/REAPER/UserPlugins/`, keeping them together. Apply the bundled `99-rea-sixty.rules` udev rule, or use the in-app button. No separate dependency install needed.

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately, it is not part of the ReaPack package.
