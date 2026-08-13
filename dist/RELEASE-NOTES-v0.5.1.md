# Rea-Sixty v0.5.1, "HOTFIX"

**v0.5.0 stopped REAPER from starting on Windows. This release fixes that, and nothing else.** The extension asked Windows for more working memory at startup than a program's main thread is given, so Windows killed the process while the extension was still loading. REAPER never opened, which also meant ReaPack was out of reach and the extension could not be removed from inside the app. macOS and Linux were never affected, because they give a program roughly eight times more of that memory, and the same build ran fine there all along. Sorry to anyone this caught.

## If REAPER will not start right now

You cannot install this update from inside REAPER, because REAPER does not open. Move one file first, then update normally.

1. Press Windows key and R together.
2. Paste `%APPDATA%\REAPER\UserPlugins` and press Enter.
3. Rename `reaper_rea-sixty.dll` to `reaper_rea-sixty.dll.OFF`.

REAPER starts again now, without Rea-Sixty. Update through ReaPack as usual, restart REAPER, then delete the `.OFF` file you made. Or skip ReaPack and drop the `reaper_rea-sixty.dll` from this release straight into that folder, replacing the old one.

On a portable REAPER install the folder is `UserPlugins` next to `reaper.exe` instead. If Windows hides the `.dll` part of the name, switch on File name extensions under View, Show in Explorer first.

The full version of this, with the checks for whether it really was this bug, is in [docs/v0.5.0-windows-recovery.md](https://github.com/acklin83/Rea-Sixty/blob/main/docs/v0.5.0-windows-recovery.md).

## Install via ReaPack (recommended)

Under Extensions, ReaPack, Manage repositories, Import/export, Import repositories. Paste:

```
https://github.com/acklin83/reaper-scripts/raw/main/index.xml
```

Then Browse packages, `Rea-Sixty`, Install. Restart REAPER. Add the surface under Preferences, Control/OSC/Web, Add, Rea-Sixty.

First-run setup buttons live under Settings, About:

- **Windows:** "Install UF8/UC1 WinUSB driver" (UAC prompt)
- **Linux:** "Install Linux udev rule" (pkexec prompt)
- **macOS:** nothing extra

## What's new

### Windows: REAPER starts again

One line in the settings loader built a fresh copy of the whole configuration on the stack, the small scratch area Windows caps at one megabyte for a program's main thread. That copy had quietly grown to a little over one megabyte on its own, because the UF1 work in v0.5.0 added fields to every button binding and the configuration holds several thousand of them. Windows checks the whole request before the function runs its first instruction, found it did not fit, and ended the process. Nothing in the extension's own error handling ever got a chance to run, which is why REAPER died in silence rather than showing a message.

The copy now lives in ordinary memory instead of on the stack, which is where it always belonged. The same crash was fixed once before, in May, for the same reason in the same file. That fix covered every variable holding a configuration and missed the one that was a temporary value, and back then it still fit anyway.

So that there is no third time, the build now refuses to compile if any function reserves more than 256 KB of stack. The largest one left in the extension uses 149 KB.

This was reported on the forum first, and confirmed on a Windows machine here: three crashes, all with Windows error code `0xc00000fd`, all at the same instruction inside the memory check.

## Known issues

Same as v0.5.0.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.5.1.zip`, unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.5.1.zip`, unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from Settings, About on first launch.
- **Linux:** `rea-sixty-linux-v0.5.1.tar.gz`, unpack **all three** files (`reaper_rea-sixty.so`, `libusb-1.0.so.0`, `libhidapi-hidraw.so.0`) into `~/.config/REAPER/UserPlugins/`, keeping them together. Apply the bundled `99-rea-sixty.rules` udev rule, or use the in-app button. No separate dependency install needed.

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately, it is not part of the ReaPack package.
