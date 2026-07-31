# Rea-Sixty v0.4.3 — "The stickiest of the icky"

**The one where a V-Pot grabs one plug-in parameter and won't let go.** New **Sticky Pot**: pin any plug-in parameter to a track's V-Pot, and it stays there — following the track across banks, overriding the strip's normal pan/param view, until you clear it. Point it at a comp threshold, a de-esser frequency, whatever you keep reaching for. Plus two UF8 fixes found on the way: the Page arrows now work inside Quick banks, and the channel encoder stops offering LED colours it doesn't have.

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

### Sticky Pot — pin a plug-in parameter per track (UF8)

Pin one plug-in parameter to a track's V-Pot. The pin **moves with the track** across banks and **overrides** the strip's normal V-Pot target (focused param / pan) until you clear it — each track can hold its own. Two bindable actions (Settings → Bindings, category **"Sticky Pot"**):

- **Sticky Pot: Get next touched Parameter** — fire it, then touch the plug-in parameter you want (in the plug-in GUI or on a controller). It pins to the track that parameter lives on. While it's armed, a **V-Pot press clears** that strip's pin instead.
- **Sticky Pot: Toggle active/inactive** — momentarily suspend every pin (to see the normal V-Pot view) and bring them all back. Pins are kept.

A **V-Pot press** on a pinned strip resets the parameter (toggles flip). Pins are **saved with the project**. Sticky Pot steps aside automatically while a mode already owns the V-Pot (Plugin / SSL Strip Mode / FLIP / PAN-held / Instance modes). Moving a pinned V-Pot no longer drags every other strip's V-Pot onto the same parameter — the pin is fully independent of the focused-parameter follow.

*This release wires Sticky Pot on the UF8; the UF1 side follows with the native UF1 build.*

### Bug fixes

- **Page ◄ ► now navigates a Quick's sub-banks.** With a Quick engaged (e.g. Q3 = I/O), the Page arrows did nothing — they only moved the plug-in soft-key bank, not the Quick's active sub-bank. They now step the sub-bank (V-POT / Soft 1–5), matching the bank-select keys.
- **No LED options for the channel encoder in Bindings.** The binding editor showed an LED-colour block for the UF8 channel encoder (rotate **and** push) even though it has no button LED. Both are hidden now; the encoder push keeps its full modifier + long-press slots.

## Known issues

Same as v0.4. Sticky Pot v1 notes: while FLIP is engaged the pin steps aside (fader-swap comes later); the readout bar is a left-to-right sweep for every pinned parameter; a V-Pot press resets a continuous parameter to its centre.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.4.3.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.4.3.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.4.3.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately — it is not part of the ReaPack package.
