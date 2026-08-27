# Rea-Sixty v0.5.8, "Let There Be Light!"

**The room is part of the mix now.** Rea-Sixty talks to a Philips Hue bridge, and the lamps behave like everything else on the surface: a fader is brightness, a V-Pot is colour, CUT switches a lamp, SOLO leaves one lamp standing. Scenes go on any key you like, or on a whole soft-key bank at once. The recording light is the classic red one, except it is real, and it puts the room back exactly as it was when the take ends. A marker named `hue:Relax` recalls the scene called *Relax* as the playhead passes it. Around that, two smaller repairs where the surface was telling you something untrue: the colour keys now wear SSL's own names for SSL's own colours, and the UF1's resting LEDs sit at the same step as the UF8's instead of glowing at half power.

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

### Hue mode

Settings, Modes, Hue finds the bridge, pairs with it (press the round button, you have thirty seconds) and lists what it found. Eight rows, one per strip: each points at a single lamp or at a whole room or zone, carries a seven-character label for the scribble strip, and says what the lamp is doing right now. The lamps take one end of the surface, left or right as you choose, and the tracks that would have sat there shift onto the remaining strips rather than disappearing.

Bind **Selection Mode: Hue** to engage it. On a UF8 strip the fader is brightness (all the way down switches the lamp off), the V-Pot is hue, FLIP makes it saturation, pressing it swaps between colour and white, CUT switches the lamp and SOLO leaves this one lit while the other Hue strips go dark.

**UF1 Mode: Hue** puts one lamp on the channel screen with three axes on three pots, hue, saturation and colour temperature, and the CHANNEL encoder to walk between lamps. SEL on a UF8 strip sends that lamp to the UF1.

### Scenes, on a key or on a bank

Eight scene slots, each with a label and an LED colour. **Hue: recall scene** takes a parameter of 1 to 8, so a scene goes on any key of any surface; a long press starts the ones that move. Or set a soft-key bank's type to **Hue Scenes** and the whole bank becomes the eight slots, each key wearing its colour and lighting while the bridge reports that scene as the one showing. On the UF1 the bank names itself HUE on the time field and flashes the scene name on recall.

Two more actions: **Hue: all lamps off**, and **Hue: recording light on / off** for arming the light without opening Settings.

### Recording light

Pick the lamps (the rows you ticked, or a whole room), pick the colour and brightness, and decide what happens when the take ends: back exactly as they were, or into a scene of your choosing. The state of every lamp is read just before the light goes red and written back afterwards, lamp by lamp, so a room whose lamps sat on different colours comes back that way rather than flattened to one.

Only recording touches the lights. Play and stop leave them alone, because otherwise every scene you recalled by hand would be wiped the moment you hit the space bar.

### Marker cues

Name a marker `hue:Relax` and the scene called *Relax* is recalled as the playhead passes. The prefix is yours to change, and so is the transition time. Under the settings is a live list of every matching marker in the project with the scene it resolves to, or a red note where there is no scene by that name, so you can check it before the take rather than during it. Cues fire only while the transport is rolling, and only forwards.

### The colour keys wear SSL's names

A Track-Colours bank used to label its keys "Col 1" to "Col 8" unless you named them yourself, and the palette had been moved onto the ten colours the hardware can actually render without the names following. A key could say BROWN while its swatch was cyan. The names now come from SSL's own vocabulary, the one inside SSL 360 itself: RED, ORANGE, YELLOW, GREEN, CYAN, BLUE, PURPLE, MAGENTA, PINK, WHITE. An unnamed slot wears the name of the colour it shows, and a name that was left behind by the palette move is corrected once, on load. A name you chose yourself is never touched.

### Resting LEDs on the UF1

The UF1's coloured display soft keys rested at half power while the UF8 rests at one step above off, which read as a lamp somebody forgot to switch off. SSL's own dim bytes settle it: every colour in the palette rests on the lowest step, so that is where these rest now. SOLO and CUT were a step above the UF8 as well, and now sit on the same bytes it sends.

### Manual

The REC + RME chapter described our side of the bridge and nothing of TotalMix's, so following it exactly still left a dead mode. It now carries what has to be in place first: TotalMix FX 2.1 with Global OSC, the two OSC settings that switch it on, the "Receive to hidden channels" box that silently drops everything when it is off, the ports, and the TotalReaper version that follows a track when you change its input. The Nav Mode chapter was describing a settings tab that had moved on, including a view picker that no longer exists, and has been read against the code and corrected.

## Fixed

**"Touch selects channel" no longer fires in UF8 Plugin Mode.** In that mode the eight strips are not eight tracks, they are eight parameters of one plug-in on the focused track. Touching a fader selected whatever track sat under that strip, which moved the focus out from under the plug-in mid-gesture and left the next fader editing something else. The setting is about strips that stand for tracks, and there none of them does.

**Two memory leaks in the JSON path.** Every configuration file and every server answer goes through the same parser, and it hands back a tree that nothing was freeing. One layer deeper it also drops the key element of every object member somewhere nothing can reach. Parsing once, at load, made both look like nothing for years. Hue reads the bridge once a second, which is how they were found, and the second one has been in every release so far. Fixed and measured: a test that parses the same document twenty thousand times leaked 480 bytes a parse before and nothing after.

## Configuration

The bindings file stays at version 32. Hue keeps its own configuration outside it, so a project and a bindings file from v0.5.7 load unchanged.

## Known issues

- A UF8 plug-in-mode fader assigned to an FX parameter does not return to its envelope in
  Touch during playback.
- FX Cycle, Instance Cycle and Favourites cycling show their prev/current/next carousel on the
  UC1 only. On a UF1 the landed plug-in name appears, without the neighbours.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.5.8.zip`, unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.5.8.zip`, unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from Settings, About on first launch.
- **Linux:** `rea-sixty-linux-v0.5.8.tar.gz`, unpack **all three** files (`reaper_rea-sixty.so`, `libusb-1.0.so.0`, `libhidapi-hidraw.so.0`) into `~/.config/REAPER/UserPlugins/`, keeping them together. Apply the bundled `99-rea-sixty.rules` udev rule, or use the in-app button. No separate dependency install needed.

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately, it is not part of the ReaPack package.
