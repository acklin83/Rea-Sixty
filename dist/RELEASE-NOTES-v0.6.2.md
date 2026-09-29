# Rea-Sixty v0.6.2, "Look Ma, No ARC!"

**The UF1 as a remote for RME TotalMix FX, in REAPER and without it.** ORC 1.0.0 is out, a free Mac app that turns the UF1 into a TotalMix remote in the menu bar. With ORC installed, Rea-Sixty offers the same view inside REAPER, and the two hand the UF1 to each other when REAPER starts and quits. On the REAPER side, a pot push now sticks: after the push the pot ignores turns for 250 ms, on the UF1 as it already did on the UF8.

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

## RME side-car on the UF1 (macOS)

**The UF1 drives TotalMix FX inside REAPER.** Hold SHIFT, press MODE, and pick RME on the first soft key. The fader, the pots and the keys then move TotalMix, and the two screens show what TotalMix is doing: channel names and colours, levels, the EQ, reverb and echo. It runs the code ORC runs, so ORC's manual describes it control by control: <https://acklin83.github.io/ORC/>

- **It needs ORC.** The side-car's settings (the TotalMix connection, the soft-key banks, the STRIP pages) are set up in ORC, and Rea-Sixty reads them. Without ORC the RME page does not appear. ORC is Mac only, so the side-car is too.
- **REAPER and ORC hand the UF1 over.** When REAPER starts, Rea-Sixty takes the UF1 and the TotalMix port over from ORC; when REAPER quits, ORC takes both back. *Settings, Connected devices, Take the UF1 over from ORC*, on by default, switches this off.

## ORC 1.0.0

The same TotalMix remote on the UF1 without a DAW, as its own free Mac app (macOS 13 or later, Apple silicon or Intel): <https://github.com/acklin83/ORC>

## UF1 and UF8

- **A pot push sticks.** The small twist that rides along with a press used to move the value straight off the default the push had just set. The UF1's four display pots and the pot above the fader now ignore turns for 250 ms after a push, in REAPER and in the RME side-car. The UF8 already did.
- **UF1 pot above the fader under FLIP:** a push resets what the pot moves (the plug-in's default, else 0 dB), as on the UF8. It used to centre the pan.
- **UF8 send pan:** a turn right after the push no longer brings back the pan from before the push.
- **UF1 jog with Playhead snap** stops on every grid line, counted from the bar. A slow turn used to skip from an odd sixteenth past every bar line. Shift makes the steps and the lines finer alike, and the nav arrows count grid lines from the bar too.

## Bindings

- **A key is Press or Toggle**, and the choice is offered only for actions that can be on or off: a switching built-in, a modifier, a REAPER toggle action. It replaces Momentary, Toggle and Hold, where Momentary and Toggle did the same and Hold ran one-shot actions twice.

## Known issues

- On a track without the envelope yet, the first pot turn in Touch goes through REAPER's usual path, which creates the envelope, and the value returns once. From the second turn on, the pot writes its point.
- FX Cycle, Instance Cycle and Favourites cycling show their prev/current/next carousel on the UC1 only. On a UF1 the landed plug-in name appears, without the neighbours.
- OBS chapter marks need one of OBS's Hybrid recording formats; on anything else OBS refuses the request and the status line says so.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.6.2.zip`, unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.6.2.zip`, unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from Settings, About on first launch.
- **Linux:** `rea-sixty-linux-v0.6.2.tar.gz`, unpack **all three** files (`reaper_rea-sixty.so`, `libusb-1.0.so.0`, `libhidapi-hidraw.so.0`) into `~/.config/REAPER/UserPlugins/`, keeping them together. Apply the bundled `99-rea-sixty.rules` udev rule, or use the in-app button. No separate dependency install needed.

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately, it is not part of the ReaPack package.
