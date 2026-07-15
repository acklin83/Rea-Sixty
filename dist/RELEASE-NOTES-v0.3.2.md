# Rea-Sixty v0.3.2 — "It can only be attributable to human error."

The **Gate gain-reduction meter finally works**. It had been dark since April for want of a data source — REAPER's API exposes one gain-reduction figure per plug-in and nothing at all for the gate. The SSL plug-ins publish it themselves, so Rea-Sixty now reads it straight from them. Alongside: an action to re-open the hardware without the Preferences round-trip, two Stream Deck meter layout options, and a batch of platform fixes that had quietly made Windows a second-class citizen.

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

### The Gate GR meter works
- The **UC1 Channel Strip Gate gain-reduction strip** is live. It had never had a source: REAPER's `GainReduction_dB` returns a single number per plug-in — the compressor's — and nothing for the gate.
- Rea-Sixty now stands in for SSL 360°Core, so the SSL plug-ins stream their own meter data to it, gate included. Verified on the wire: 0 dB with the gate open through −42 dB fully closed, with the attack/release ramp in between, while the compressor's own reduction tracks independently.
- **Opt-in, and SSL 360° must be quit** — Rea-Sixty announces itself on the ports 360° uses, so the two cannot run together. Enable with:
  ```lua
  reaper.SetExtState("rea_sixty", "ssl_core", "1", true)
  ```
  then restart REAPER. Left off, the Gate strip stays dark exactly as before.
- This also fixes meters misbehaving when an SSL Channel Strip and the SSL Meter plug-in were loaded together: their meter streams share numbering and were overwriting each other.

### Restart Rea-Sixty without Preferences
- New action **"Rea-Sixty: Restart Rea-Sixty (re-open devices)"** — in REAPER's Action list (shortcut-able) and as a binding under `Settings → Bindings → Hardware Modes` for a hardware key or a Stream Deck tile. Closes the UF8 / UC1 / MIDI ports and re-opens them, so a wedged surface no longer means a trip through Preferences → Control Surface.
- It re-opens the **hardware**; it does not reload the extension. New code still needs a REAPER restart.

### Stream Deck meter tile
- **Name position** — put the track name at the **top** of the key instead of the bottom. If your deck sits at an angle, the bottom of the key is the part you cannot read; the native text box would cover the meter.
- **Wrap name** — allow a second line, so a longer name stays legible at a larger font size.
- **Smart abbrev.** — per-tile switch. Off shows the full track name, which is worth having now that the tile can wrap. On abbreviates properly regardless of your global Track-name mode.
- The name's space is carved out of the bar area rather than drawn over it, and the default (bottom, single line) renders exactly as before.

### Console output is now optional
- `Settings → Logs → **Console output**`, **default off**. REAPER pops its Console window open for every message, which is noise when a device simply isn't connected. The same text still goes to the log files, so nothing is lost — switch it on when diagnosing.

## Bug fixes

- **About-page links now work on Windows and Linux.** They were opening via a macOS-only command, so the click did nothing at all elsewhere.
- **Log files are written on Windows.** Every log was hardcoded to `/tmp`, which does not exist there — the frame trace and the startup logs silently went nowhere. They now use `%TEMP%` on Windows, and the Settings page shows the real path instead of claiming `/tmp` on every platform.
- **MCP Inserts overlay no longer paints onto the wrong row.** When plug-in parameters or a send list cut the inserts area short, the highlight for a plug-in past the list's last visible row was drawn onto whatever sat below it. It is now bounded to the FX list.

## Known issues

Same as v0.2.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.3.2.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.3.2.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.3.2.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately — it is not part of the ReaPack package.
