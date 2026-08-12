# Rea-Sixty v0.4.5 — "Works on my machine"

**The one where the Linux build stopped assuming your computer is as new as the build server.** v0.4.4 bundled libusb and hidapi so the Linux install would finally be self-contained — and it was, on recent distributions. On Debian 12, MX Linux 23 and anything else a little older, REAPER refused to load the plugin with `version 'GLIBCXX_3.4.31' not found`. The libraries were all there; they were simply compiled against a newer system than yours. The Linux package is now built on a deliberately old baseline and carries its own C++ runtime, so it loads on any distribution from 2022 onward. **Linux only — nothing else changed.**

## Install via ReaPack (recommended)

```
Extensions → ReaPack → Manage repositories → Import/export → Import repositories
```
Paste:
```
https://github.com/acklin83/reaper-scripts/raw/main/index.xml
```
Then **Browse packages** → `Rea-Sixty` → Install. Restart REAPER. Preferences → Control/OSC/Web → Add → Rea-Sixty.

First-run setup buttons (`Settings → About`):
- **Windows:** "Install UF8/UC1 WinUSB driver" (UAC prompt)
- **Linux:** "Install Linux udev rule" (pkexec prompt)
- **macOS:** nothing extra

## What's new

### Linux: the plugin loads on older distributions again

If REAPER started with one of these on the terminal, this release is the fix:

```
reaper_rea-sixty.so: libstdc++.so.6: version `GLIBCXX_3.4.31' not found
reaper_rea-sixty.so: libc.so.6: version `GLIBC_2.38' not found
```

A compiled plugin records the exact version of every system function it calls, taken from the machine that built it. v0.4.4 was built on Ubuntu 24.04, so it asked for versions that only exist on Ubuntu 24.04 and newer — and Debian 12 / MX Linux 23 (and their derivatives) cannot provide them. The result was the same as the bug v0.4.4 set out to fix: installed via ReaPack, but no Control Surface entry and no actions.

Two changes, both invisible in use:

- **The Linux package is now built against a much older system baseline**, below every currently supported distribution. The bundled `libusb-1.0.so.0` and `libhidapi-hidraw.so.0` come from that same baseline — in v0.4.4 they carried the problem too.
- **The C++ runtime is linked into the plugin** instead of borrowed from your system, so the `GLIBCXX` version of your distribution no longer matters at all. The `.so` grows by about 2 MB; nothing else about it changes.

**Requires glibc 2.34 or newer** — Ubuntu 22.04+, Debian 12+, MX Linux 23+, Fedora 35+, and anything more recent. If you are on something older, build from source.

Nothing to do beyond updating: the ReaPack update replaces all three Linux files. macOS and Windows packages are unaffected and functionally identical to v0.4.4.

## Known issues

Same as v0.4.4.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.4.5.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.4.5.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.4.5.tar.gz` → unpack **all three** files (`reaper_rea-sixty.so`, `libusb-1.0.so.0`, `libhidapi-hidraw.so.0`) into `~/.config/REAPER/UserPlugins/`, keeping them together. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button). No separate dependency install needed.

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately — it is not part of the ReaPack package.
