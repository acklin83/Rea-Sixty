# Rea-Sixty v0.5.2, "It was never the firmware"

**Bug fixes.** REAPER could be killed outright by a plug-in whose parameter values print long text, FabFilter above all. The Windows driver installer did nothing at all from the second press onwards, which locked out anyone who pressed it twice. Names on the surface were still being cut to seven characters in seven places after the zone grew to twelve. And on the UF1 the EQ graph did not follow a channel change, while SEL in Extender mode selected the channel that was already selected.

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

### REAPER no longer dies on a long parameter value

The UF1 display line is nineteen characters, built as a name, then padding, then the value. All three lengths were counted in a type that cannot go below zero. A value at least as long as the whole line made the arithmetic wrap to an enormous number, the padding step asked for a string of that size, and the resulting error escaped into REAPER's event loop, which treats one as fatal. REAPER ended immediately, with a crash report that named nothing at all.

The value is the plug-in's own formatted text, so its length is entirely up to the plug-in. SSL's strips never reach nineteen characters. FabFilter does, "External / Sidechain" is twenty, which is why this only ever happened with FabFilter loaded and looked like a compatibility problem.

The tick that drives the surface now also catches an unexpected error instead of letting it reach REAPER. A future bug of this kind costs a glitched frame and a line in the log rather than the session.

### Windows driver install works on the second press

The installer built a signed catalogue of its own working folder while writing that catalogue into the same folder. The first run was clean, because the folder held only the driver file. Every run after that found the previous catalogue sitting there, failed while hashing it, and stopped before installing anything, with the window closing too fast to read. Pressing the button again is exactly what people do when nothing appears to happen, so anyone who did was locked out from then on. One forum user came close to selling a working UF1 over it.

Also fixed: each press added another copy of the driver package to Windows' store, while the uninstall only ever removed one. That is how machines ended up with copies that could not be removed from inside the app.

The install now reports what actually happened, rather than "started": installed, failed with the path of a log to paste, or cancelled at the permission prompt. Cancelling is not a failure and no longer reads as one. Settings, About also shows what each device is bound to right now, with a Refresh button, so you can see whether the driver took without opening Device Manager.

The button says UF8, UC1 and UF1 now. It always bound all three; the label said two, so UF1 owners read it as not being for them. After a successful install the devices move from "Universal Serial Bus controllers" to "Universal Serial Bus devices" in Device Manager and are renamed, which the text now says, because looking in the old place and finding nothing reads as a dead unit.

Verified on Windows 11: the old installer fails on the second run with the exact error the forum user reported, the new one survives five runs in a row and leaves one copy in the driver store.

### Names are twelve characters everywhere

The colour-bar zone was measured at twelve characters back in v0.5.0 and the caps were raised where they were found. Seven were missed: the Learn-HUD's own rename cut what its own dialog let you type, the new-map dialog said seven and had a buffer to match, and the shared parameter name was cut to seven when set from the UF8 while the UF1 accepted eleven for the same string.

### UF1: the EQ graph follows the channel, and SEL selects

The graph stayed on the previous channel until an EQ value was touched. Choosing a channel by hand, with SEL or the channel encoder, now takes precedence over the plug-in instance you last activated. The button had that rule and the encoder did not, which is why one worked and the other did not.

In Extender mode SEL selects the track the UF1's fader is on. Its lamp has always reported that track; the press acted on the selected one instead, so it did nothing.

The graph also redraws after the device is unplugged and back, instead of believing a curve is on screen that the device cleared.

### Smaller UF1 fixes

- The Learn-HUD's UF1 tab shows the name of the plug-in the UF1 is on. It showed the Channel Strip's name, or nothing, because the two can be on different tracks.
- Umlauts on the UF1 panel came out as two characters each.
- The Extender's small display named the selected track instead of the track under the fader.
- An envelope jog left nothing to undo.
- A long parameter name overflowed the FX-Learn cell identifier, and "Fill: Replace" on a plug-in with hundreds of parameters is now capped.

## Known issues

- A UF8 plug-in-mode fader assigned to an FX parameter does not return to its envelope in
  Touch during playback.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.5.2.zip`, unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.5.2.zip`, unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from Settings, About on first launch.
- **Linux:** `rea-sixty-linux-v0.5.2.tar.gz`, unpack **all three** files (`reaper_rea-sixty.so`, `libusb-1.0.so.0`, `libhidapi-hidraw.so.0`) into `~/.config/REAPER/UserPlugins/`, keeping them together. Apply the bundled `99-rea-sixty.rules` udev rule, or use the in-app button. No separate dependency install needed.

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately, it is not part of the ReaPack package.
