# Rea-Sixty v0.1.11 — *Even Prettier*

Hotfix for two same-day regressions surfaced after v0.1.10 shipped: focused-parameter snap-to-Bypass during track scrolling in Strip Mode, and the Temporary Selection Set ignoring the global Selection-Set Auto-Mode setting.

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

## Bug fixes

### Strip Mode — focused-param slot no longer snaps to Bypass on track scroll

With **SSL Strip Mode** engaged AND a plug-in floating window open AND **Strip Mode follows focused plugin window** ticked, scrolling tracks made the chase path (`chaseFocusedFxWindow`) call `setFocus({domain, 0})` on every focus change — slot 0 is Bypass on most plug-ins, so V-Pots / UC1 focused-param row jumped to "Bypass" the instant the user scrolled. The v0.1.9 sticky-slot fix only covered `applyInstanceCycle_`; the chase path kept the old reset behaviour.

v0.1.11 applies the same slot-sticky logic to the chase: when the new focused FX is in the same domain as the current focus AND it exposes the current LinkSlot, the slot is preserved across the channel switch. Cross-domain chases (CS → BC) and unmapped slots still fall back to 0 because the slot index isn't meaningful there.

### Temp Selection Set — now honours Selection-Set Auto-Mode

The Temporary Selection Set introduced in v0.1.10 was missing from the **Settings → Modes → AUTO → Selection-Set auto-mode** hook. Recalling it in AUTO selection mode didn't force its member tracks into the configured automation mode; deactivating it didn't revert them. v0.1.11 wires the temp set through the same path as the 1..8 slots — recall arms, deactivate reverts to Trim/Read. Mutual-exclusion handoff (slot ↔ temp) also reverts the outgoing set's auto-mode before activating the incoming one.

## Known issues

Same as v0.1.10. Nothing new.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.1.11.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.1.11.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.1.11.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).
