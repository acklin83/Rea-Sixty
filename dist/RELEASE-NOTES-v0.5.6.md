# Rea-Sixty v0.5.6, "The other half of Shift"

**A modifier set is a full bank, and now it is one everywhere.** Holding Shift over a soft-key row has shown its own set for a while, but three places never got the message: the SSL Channel-Strip and Bus-Comp rows, which handed the row back to the plug-in; a dynamic bank, which computed its keys and ignored what you had put on the set; and the surface itself, which fell back to Plain the moment you let the key go, while the editor stayed where you had latched it. All three follow now, and a set can be a dynamic bank of its own, so FX on Plain and Track Colours on Shift is a thing you can build. Alongside that: the UF1's LEDs read their bindings instead of a colour baked into the painter, its time display can spell words, and eleven points from a forum round landed.

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

### The SSL rows open up

The Channel-Strip (Q1) and Bus-Comp (Q2) rows on Layer 1 are the plug-in's, but only where the plug-in actually puts a parameter. The gaps have been yours since June and there was no way to reach them in Settings, so the capability sat there unusable. The editor offers them now, per key rather than refusing the whole Quick, and the empty Bus-Comp pages 2 to 5 are free real estate.

On top of that, Plain belongs to the plug-in and **every modifier set belongs to you**, on every key, including the eight the Channel Strip fills. Hold the FINE key and the row shows your set: the keys you assigned, and nothing at all on the ones you did not. A key you left empty does nothing when pressed, rather than quietly moving the plug-in's focused parameter behind a blank label.

### The Bindings pane drives the surface

While Settings is open on the Bindings pane, the surface follows the set the Modifier row is on, rather than the key you are physically holding. Press FINE to jump to the Shift set and the row stays there when you let go, so what you see on the hardware is what you are editing, and pressing a key fires that set. Close the pane and the surface goes back to following the held key.

Holding the modifier also moves the editor from anywhere in the pane now, not only once a soft-key is selected.

### Dynamic banks got their sets

A dynamic bank computes its eight (or four) keys from the focused track, and the modifiers over it run the FX-key gestures. That was read as "a dynamic bank has no sets", which is only true where the set is empty. Assign a key in a set and it fires instead of that key's gesture; leave it empty and the gesture runs exactly as before, so nothing you were using goes away. The row paints what it will do, which it did not before: the assigned key shows your label and colour rather than the computed one.

The UF1's banks work this way too now, and their slot editors stay reachable while a set is picked, since that is where the key goes.

**And a set can be a dynamic bank of its own.** Pick a kind with the Modifier row on Shift and that set becomes its own bank, so FX on Plain and Track Colours on Shift is one bank, not two. A set left on "Off (take Plain's bank)" behaves as it always has. A set that owns its bank has no gestures on it: holding the modifier is how you got there, so a press is that bank's own push.

### Typing in Settings

Typing a capital letter into a label field mirrored the keyboard's Shift into the shared modifier state, and every LED and scribble strip on the surface follows that, so the hardware flickered onto its Shift set per keystroke. While the Settings window owns the keyboard, the mirror stands down. Holding SHIFT on the surface is untouched, and so is the keyboard's Shift everywhere else.

### UF1 LEDs

- The nav cross lights from its bindings, one set per jog mode, so a key shows what it does right now instead of sitting dark in three of the six modes. The razor edge, the envelope target, a held item drag and the fade edge still win their own key, now in that binding's active colour.
- Holding a modifier no longer changes the LED of a key that has nothing assigned on it. Only keys that carry an action for the held modifier switch to its colour.
- MASTER, BANK left/right, PAGE left/right and 5-8 follow the colour set for them. They ignored it and painted a fixed one, so they now look like every other key: their colour, bright when the move is possible, dim when it is not. Set Inactive to Off for the old unlit look.
- LED brightness Off means off, on every UF1 key and on the DAW soft-keys. It rendered as Dim.

### The UF1 time display

It can show text. Stepping the format flashes BARS, TIME or SAMPLES, so the field names what it is showing. A position of ten digits or more no longer loses its leading digit: the field is ten cells wide, not eleven. And a negative position keeps its minus sign, which used to be dropped.

### Track colours

The factory palette of the Track Colours bank used colours the hardware does not have, so the firmware rounded them and two swatches came out looking the same while a third went pink. It is eight real palette values now. Separately, a dimmed key no longer loses its hue: dimming happens after the colour is quantised, not before, which is why three keys read as red and the fourth as unlit.

### From the forum round

- A parameter with more than five steps steps through all of them. Five probes could not tell ten steps from continuous, and a parameter declared continuous became a hard 0/1 toggle.
- A long press on a dynamic bank key fires under the finger at half a second, rather than waiting for the release.
- FLIP in Plug-in Mode puts the last touched V-Pot's plug-in parameter on the fader. Strip Mode, send faders and a sticky pin still win, and without a parameter to resolve it falls back to Pan.
- Selection Mode comes back as NORM on start. Settings, Behaviour, Tracks; on by default, and turning it off restores the old behaviour without having lost the stored mode.
- Soft-key 1 on the UF1 display no longer sits green while the other three are yellow.
- `send_this` and `recv_this` are hidden from the UF1 action picker. They write state no UF1 path reads.
- Every built-in action carries a one-line description, shown as a tooltip in the action picker and searched along with the name.
- The Settings window no longer closes itself while scrolling the FX Learn pane.

## Known issues

- A UF8 plug-in-mode fader assigned to an FX parameter does not return to its envelope in
  Touch during playback.
- FX Cycle, Instance Cycle and Favourites cycling show their prev/current/next carousel on the
  UC1 only. On a UF1 the landed plug-in name appears, without the neighbours.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.5.6.zip`, unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.5.6.zip`, unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from Settings, About on first launch.
- **Linux:** `rea-sixty-linux-v0.5.6.tar.gz`, unpack **all three** files (`reaper_rea-sixty.so`, `libusb-1.0.so.0`, `libhidapi-hidraw.so.0`) into `~/.config/REAPER/UserPlugins/`, keeping them together. Apply the bundled `99-rea-sixty.rules` udev rule, or use the in-app button. No separate dependency install needed.

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately, it is not part of the ReaPack package.
