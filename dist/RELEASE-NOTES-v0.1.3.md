# Rea-Sixty v0.1.3

Small feature release: track-name abbreviation, Reverse LED toggle for plug-in-bound Solo/Cut/Sel, full 8-strip addressability in UF8 Plugin Mode and This-Track Sends/Receives, and richer Fill-sequential propagation in the FX Learn editor.

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

### Track-name abbreviation modes

`Settings → Device → Tracks → Long track-name handling` — new combo with two choices:

- **Truncate** *(default — keeps the legacy behaviour)*: track names longer than the 7-char scribble-strip slot get cut after the seventh character. "Background Vocals" → "Backgro".
- **Smart abbreviate**: drops separators, then vowels after the first letter of each word, then collapses repeated consonants, and finally distributes the remaining char budget proportionally across tokens so every word stays visible. Short all-caps tokens (DI, FX, EQ, …) survive untouched. "Background Vocals" → "BckgVcl", "Lead Vocal" → "LdVcl", "Drums Bus" → "DrmsBs", "Bass DI" → "BassDI".

Mode switch repaints all 8 strips immediately.

### Reverse LED per Solo / Cut / Sel binding

Some plug-ins expose their Cut/Bypass parameter with inverted semantics — value 1 = "function inactive", 0 = "active" — and the strip's LED ends up bright when the function is off. New per-binding toggle to fix that case:

FX Learn editor → right-click a Solo / Cut / Sel button on the UF8 schematic → **Reverse LED [off/on]**. The render layer XORs the LED on/off bit before painting; existing LED-colour overrides are unaffected. Saved per `(fader-bank, strip, button)` in `user_plugins.json`.

### All 8 strips addressable in Plugin Mode + This-Track routing

Sessions with fewer than 8 REAPER tracks were silently disabling the right-most strips. Two scenarios that now work:

- **UF8 Plugin Mode** with a learned plug-in spanning 8 channels across the active fader-bank: all 8 strips drive the bound params on the focused plug-in instance regardless of how many REAPER tracks are visible. Previously a 4-track session showed Solo/Cut/Sel/Fader/V-Pot live only on strips 1..4; strips 5..8 went blank.
- **Sends / Receives** in "This Track" mode with a Selection Set of fewer than 8 tracks: all 8 strips show the focused track's first 8 sends, gated by send-slot existence (not by track count). Previously the strips past the visible-track count were eaten.

In both cases the strip's content is sourced from the focused FX (PM) or route target (sends), so the bank-track being absent no longer empties the strip.

### Fill sequential propagates source-strip attributes

FX Learn editor → right-click a mapped V-Pot / Fader / Solo / Cut / Sel → "Fill sequential (right)" now carries the source strip's modifier attributes onto every filled strip:

- **Fader / V-Pot / TopSoftKey**: `inverted`
- **V-Pot / TopSoftKey**: `vpotMode` (Toggle / Value), `defaultNorm` (V-Pot push reset), `stripColour` (colour-bar override)
- **Solo / Cut / Sel**: colour override + Reverse LED flag

A CS-row fill now matches the source strip end-to-end without you touching every strip after the fact.

## Manual install fallback

Per-platform zips at the bottom of this release page if ReaPack isn't an option:

- `rea-sixty-mac-v0.1.3.zip` — three notarized dylibs → `~/Library/Application Support/REAPER/UserPlugins/`
- `rea-sixty-win-v0.1.3.zip` — three DLLs → `%APPDATA%\REAPER\UserPlugins\`
- `rea-sixty-linux-v0.1.3.tar.gz` — `.so` + udev rule + INSTALL.txt

## Known issues

- **UC1 stall recovery on macOS** — same as v0.1.2. When the OUT endpoint stalls (LIBUSB_ERROR_PIPE), `libusb_clear_halt` reports success but the next transfer stalls again; `libusb_reset_device` only buys a few seconds. Symptom: UC1 freezes, knobs stop responding, while UF8 stays alive. Workaround: power-cycle the UC1 at the unit (USB replug doesn't help — see `uc1-hardware-quirk` memory). Diagnostic continues; suspected firmware state corruption rather than client-side bug.
- **Linux daisy-chain** — UF8 and UC1 must be on separate host USB ports. Daisy-chaining UC1 through UF8's downstream port triggers `xhci_hcd` port-resets on Linux 6.17.
