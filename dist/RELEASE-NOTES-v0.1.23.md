# Rea-Sixty v0.1.23 — "That's all he said?"

Knob feel pass. The UC1 SSL Channel-Strip EQ-gain knobs now turn at the same rate as Freq/Q instead of crawling, while the 0-dB centre still snaps reliably. And the UF8 V-pots are responsive again — a stray debug log that hammered the disk on every USB packet was making them lag the faster you turned.

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

### Knob feel

- **UC1 EQ-gain knobs turn like Freq/Q.** The four SSL CS EQ-gain knobs (HF/HMF/LMF/LF) used to step far finer than the Freq/Q knobs because of a deliberate half-speed scaling. They now turn at nearly the same rate, and the 0-dB notch magnet was widened so the faster knob still settles on 0 dB cleanly.
- **UF8 V-pots are responsive again.** A leftover debug log was writing to disk on every USB input packet (the file had grown to hundreds of MB), which blocked the input thread and made the V-pots lag — worse the faster you turned. Removed it, and lifted the per-detent step so plug-in parameters track your hand better.

## Known issues

- Same as v0.1.18.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.1.23.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.1.23.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.1.23.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).
