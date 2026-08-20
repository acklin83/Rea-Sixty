# Rea-Sixty v0.5.5, "He's not just some dingbat I found on the strip, man."

**Presets, and a jog mode for fades.** The preset browsers on both surfaces stopped being decoration: they read SSL's own preset library off disk, in its folders, and a preset you pick is genuinely loaded, so the plug-in reports it as loaded rather than as the last one with changes. The UC1's PRESETS screen has had nothing to show since it was built, because REAPER's preset API is empty for every SSL plug-in; it now browses the real thing, and each half of the screen browses its own section's instance. On the UF1 the jog wheel learned fades, the meter reached the master track and the monitoring chain, and the Loudness screen's three controls all do something for the first time.

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

### Presets that actually load

Picking a preset used to write a wall of extremes into the plug-in. The number in an SSL preset file is not a normalised value, and normalised is the only domain REAPER has for a VST3: a choice parameter stores its index, a float parameter its plain value in its own unit. Both are converted now, and anything that cannot be verified is skipped rather than guessed at.

That fixed the values and still left the plug-in reading "the last preset, with changes", because SSL does not derive the loaded preset from the values. It keeps it as an attribute in its own state. So a load is written where the plug-in keeps it, which is what makes the instance show the preset as loaded, and what lets the browser open on whatever is loaded instead of at the top of the list.

### The UC1's PRESETS screen

It reads SSL's library now, in the folders SSL files it in: sub-folders first, CONFIRM steps into one, BACK comes back up, and CONFIRM on a preset loads it. Browsing changes nothing on the way, which the old live-preview did on every detent.

CHANNEL STRIP browses the strip under the Channel Strip section and BUS COMP the compressor under the Bus Comp section. The two sections address different tracks, which is the point of the layout, and both lists were being read off the Channel Strip's track.

### UF1: fades on the jog wheel

Fades is a sixth jog mode. The wheel pulls the fade under the edit cursor, and which edge the cursor sits nearer decides whether you are shaping a fade or a crossfade. On a crossfade the seam stays where it is, so pulling one open never moves audio. The view frames the fade you are working on and stands still while you turn.

Cmd and the wheel pull a time selection in Playhead and Scrub, on Windows Alt. Scrub no longer drags one on its own.

### UF1 meter

A meter on the master track or in the monitoring FX chain is reachable at last; both were invisible, for two separate reasons. On sessions past 127 tracks the meter followed the wrong track. The view shows a track's current name, so renaming updates it. The V-Pots resolve their parameters by name, so on a plain SSL Meter the labels no longer name one parameter and turn another. Umlauts survive on the display.

On the Loudness screen the scale is labelled from the plug-in's own Integrated Target and its Absolute or Relative setting, rather than from the values it was captured with. PRESETS opens there like everywhere else, PLAY pauses and resumes the measurement, and RESET clears the plug-in's own peak holds and overloads, plus its loudness measurements on the screen that shows them.

### On-screen

The mode ring is a carousel you can turn, the jog ring is sortable, and the ring opens around the button you pressed instead of walking up the screen with every pick.

## Known issues

- A UF8 plug-in-mode fader assigned to an FX parameter does not return to its envelope in
  Touch during playback.
- FX Cycle, Instance Cycle and Favourites cycling show their prev/current/next carousel on the
  UC1 only. On a UF1 the landed plug-in name appears, without the neighbours.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.5.5.zip`, unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.5.5.zip`, unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from Settings, About on first launch.
- **Linux:** `rea-sixty-linux-v0.5.5.tar.gz`, unpack **all three** files (`reaper_rea-sixty.so`, `libusb-1.0.so.0`, `libhidapi-hidraw.so.0`) into `~/.config/REAPER/UserPlugins/`, keeping them together. Apply the bundled `99-rea-sixty.rules` udev rule, or use the in-app button. No separate dependency install needed.

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately, it is not part of the ReaPack package.
