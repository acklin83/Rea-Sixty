# Rea-Sixty v0.1.6

Display-and-toggle overhaul. The surface (UF8 colour bar + UC1 central LCD) and the Toggle Plugin UI action target are now unified around one concept: **the strip's active FX** (cursor-tracked, defaults to FX[0] when no cursor explicitly set). What you see on the surface is what the push action opens — across every cycle path, encoder mode, and surface-knob touch.

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

### Plug-in name display — focused strip follows the cursor

When you cycle through plug-ins on the focused strip (via any encoder cycle, V-Pot SEL mode, or `Encoder: cycle FX` binding), the colour bar and UC1 central LCD now update **immediately** with the cycle target's name — including unmapped plug-ins (ReaEQ, third-party plug-ins) and cross-domain landings. v0.1.5 only updated for SSL Instances; unmapped FX silently stayed on the previous Instance's name.

UC1 LCD: when no SSL plug-in is on the focused track, the label reads "REAPER" (renamed from "MAIN") and falls through to the channel's active FX in CS focus. BC focus stays strict — BC anchor remains a narrow concept.

Non-focused strips are unaffected: their colour bar still shows the per-track CS Instance shortName when present, or "REAPER" when not. The cycle visual is scoped to the focused strip.

### Toggle Plugin UI — true toggle, surface-aware

The `show_focused_plugin_gui` builtin (Toggle Plugin UI) is now a clean toggle:

- **Open**: resolves the target from the surface (active-FX cursor → focused-domain Instance → cursor-default-to-FX[0]). REAPER's `GetFocusedFX2` only used as a no-surface-target fallback.
- **Close**: a press while a window is owned closes it, regardless of where the cursor has moved. Avoids the "press 1× moves cursor, press 2× would open a different window" trap.
- **Mode auto-disengage**: pressing Toggle UI while UF8 Plugin Mode (with GUI) OR SSL Strip Mode (with GUI) is active disengages the mode AND closes the window in one press. Track-change refocus no longer leaves a stuck window requiring a second press.

### Surface-knob touch updates the cursor + follows window

Turning a knob on a UC1 CS / BC or UF8-mapped V-Pot now:
1. Moves the strip's FX cursor to the touched plug-in (any PluginMap match — built-in SSL Instance, user-mapped CS/BC, UF8-only).
2. Re-targets the Toggle UI window to that plug-in if one is open and "Plugin GUI follows active Instance" is on.

So touching a CS knob after a UF8 plug-in was open swaps the GUI to CS without any extra action.

### Encoder 2 BC track-scroll flips focus to BC

When Encoder 2 (default binding) scrolls to a BC-bearing track, focus now flips to BusComp. Toggle UI then opens the BC instance instead of trying to find a CS that may not exist on the new anchor track.

### Carousel: no track-name flash, edge shows name

The UC1 instance carousel was briefly showing the track name (header) before the plug-in name (body) on each detent. Fixed by reordering carousel frames before the refresh re-render. The hard-stop edge (CCW past the first plug-in, CW past the last) now still shows the current plug-in name in the carousel instead of silent no-op.

### SSL Strip Mode is CS-only

The SSL Strip Mode (with GUI) drain no longer falls back to opening a UF8-only plug-in's GUI when the focused track has no CS. The mode is conceptually for CS plug-ins (the fader routes to CS Output Gain); UF8-only plug-ins belong to UF8 Plugin Mode, which has its own drain.

### Bank scroll clamp

`Bank ←` / `Bank →` and the per-strip bank-by-1 actions now clamp at `trackCount - 8` instead of `trackCount - 1`. A 3-track project can no longer be scrolled so the last track sits on strip 0 with empty strips to its right. UF8 Plugin Mode's fader-bank navigation (separate state) is unaffected.

### Keyboard Shift multi-select

The host-keyboard Shift modifier (Settings → Modes → Keyboard Options → "Use keyboard Shift as modifier") now counts for additive track-select via successive SEL presses. Hold Shift on the keyboard, tap SEL on strip 1, tap SEL on strip 3 → both tracks selected. v0.1.5 only honoured the UF8 hardware Shift here.

## Bug fixes

- `Encoder: cycle FX` landing on an unmapped or cross-domain plug-in now updates the colour bar immediately.
- Toggle UI no longer opens duplicate windows when the cursor has moved between presses.
- SSL Strip Mode (with GUI) no longer pops a UF8-only plug-in GUI when the track has no CS — it just toggles the mode flag without opening anything.
- Carousel no longer flashes the track name between scroll detents.

## Known issues

Same as v0.1.5. Nothing new.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.1.6.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.1.6.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.1.6.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).
