# Rea-Sixty v0.5.3, "Hold my Shift"

**Shift became a real, visible binding.** Every soft-key bank now carries two full sets of keys, Plain and Shift, each with its own dynamic kind, its own presets and its own LED colour. The UF1's nav cross carries a separate binding per jog object, five keys by five objects, and Shift on those keys is a binding you can see rather than a check hidden inside an action. Along the way the bindings editor learned to edit the bank you are actually holding, a dynamic bank learned to say that it is one, and REAPER stopped being able to vanish without a word.

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

### REAPER could disappear without a message

The audio kept playing and the window was simply gone, with no crash report and nothing in the log. The extension watches its network connections with a mechanism that has a hard ceiling on how many it can watch at once, and a connection numbered past that ceiling wrote outside the memory set aside for the answer. Nothing marked the moment it happened, which is why it looked like anything but what it was.

It now uses a mechanism with no such ceiling, and the number of connections it will accept is higher than anything a session produces. Three separate diagnoses were overturned by measurement before this one held, which is recorded where the next person will look.

### A soft-key bank is two full banks now

Holding the modifier switches the whole row of eight, labels and all, and releasing returns to Plain. This is not a second list of actions hanging off the same bank: each set carries its own **Dynamic bank** setting, its own presets and its own LED colour, so Plain can hold eight fixed assignments while Shift is an FX bank on the same sub-bank.

Slots also take a **Behavior** now (Momentary, Toggle, Hold) and all four modifier slots, the same settings every other key has had. The UF1's ten banks and the UF8's six sub-banks per Quick both work this way.

There is no Cmd or Ctrl set. The surface carries one modifier key, so those two would have to come from the computer keyboard, where they already belong to the FX-Learn modifier layers.

### The editor edits the bank you are holding

Clicking a Quick or a Sub-Bank in the schematic engages it, so the pane can never sit on a different bank than the surface, and pressing the modifier key on the hardware moves the editor to that set. What you edit is what you are holding, in both directions.

Copy and paste work on soft-keys, which they did not, and recalling a preset into a set brings its labels along instead of leaving the previous set's names in place.

### A dynamic bank says so

A dynamic sub-bank used to look exactly like a static one while quietly ignoring everything you assigned to it. It now announces itself in three places: an amber corner on the sub-bank cell, scribble strips that read the bank's kind rather than slot labels that never reach the hardware, and a key press that shows what the bank computes instead of an editor for slots that cannot fire.

**CS and BC Favourites are a dynamic bank kind of their own.** Any sub-bank can become the eight Channel Strip or Bus Compressor favourites without loading a preset, and the keys carry each favourite's plug-in name live. Firing a favourite on a track that has no Channel Strip now inserts the plug-in rather than doing nothing.

### UF1: the nav cross is yours, five times over

The five keys around the jog wheel hold a separate binding **per jog object**. The same physical key can do five different things and you decide all five, in Settings, Bindings, UF1, where the object picker follows the surface and moves it. Every one of the twenty-two things the cross does is a named action now, so any of them can go on any other key too.

Shift on those keys is a binding as well. The add-to-selection gestures sit in the Shift slot where you can see them and change them, instead of being a check inside the action. A Shift slot left empty falls back to the plain action, so the factory assignment behaves exactly as it did before.

Playhead and Scrub carry the plain zoom cross, which is what the cross does when no editing object is engaged.

### UF1: sends, and a screen that says more

The Sends view has a three-state send mode on each soft key, walking REAPER's own order, and Shift mutes a send. Shift with the V-Pot pans it, Shift with the V-Pot push centres the pan, and Shift with 5-8 flips between sends and receives.

The four soft keys now highlight on screen when engaged. That matters while the Bus Compressor's gain reduction owns their LEDs: the highlight is the second channel that still tells you which sections are in.

### UF1: fixes from the forum report

- The phantom low cut on every plug-in that is not an SSL strip is gone. Any resolved FX was treated as an EQ, so the curve was drawn from defaults, and the fallback high-pass sits inside the visible range.
- The CS type cell and PRE, SOLO SAFE, PLUG-IN stay blank on a track with no plug-in.
- Six functions no longer need a UC1 to be connected before they will run.
- SOLO is yellow, both lit and dim, like REAPER's own track panels.
- The V-Pot reaches minus infinity, the way the fader always could.
- The V-Pot bar style is driven live rather than frozen at whatever it was, and a centre bar shows a signed deviation instead of an absolute position.
- Holding MODE no longer draws the header twice.
- Plugin view on a track without a strip shows nothing instead of dead Pan and Volume readings.
- The surface comes back up in the view it was left in.

### Smaller things

- UF8: Bank left and right stay the fader bank in Plugin Mode.
- The mode banner no longer announces a state that was merely restored when the project opened.
- For plug-in display names your own mapping wins over the SSL mapping.
- The dynamic FX bank shows the name you gave a plug-in, not the factory one.
- Bindings are written in a stable order, so a config diff is readable. The first save after updating is still large, because that is the reordering itself.
- The Bindings pane remembers which surface you were last on.
- The manual covers the nav cross and the Jog actions, and several passages that had fallen behind the code are correct again.

## Known issues

- A UF8 plug-in-mode fader assigned to an FX parameter does not return to its envelope in
  Touch during playback.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.5.3.zip`, unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.5.3.zip`, unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from Settings, About on first launch.
- **Linux:** `rea-sixty-linux-v0.5.3.tar.gz`, unpack **all three** files (`reaper_rea-sixty.so`, `libusb-1.0.so.0`, `libhidapi-hidraw.so.0`) into `~/.config/REAPER/UserPlugins/`, keeping them together. Apply the bundled `99-rea-sixty.rules` udev rule, or use the in-app button. No separate dependency install needed.

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately, it is not part of the ReaPack package.
