# Rea-Sixty v0.1.4

Settings polish + theming release. New Appearance tab with Dark / Light themes and font-size picker; keyboard Cmd / Ctrl as Shift-style modifiers; unified UC1 Encoder 2 push-action picker (7 options per gesture); folder-collapse track filter; large layout pass across the Bindings mockup and the Selection Sets / Parameter Groups tabs for pixel-stable table grids.

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

### Appearance tab — themes + font size

New `Settings → Appearance` tab with two pickers:

- **Theme:** Dark (default — the existing palette) or Light. The picker re-themes every Settings panel + the FX Learn schematic; hardware-face colours stay constant.
- **Font size:** Small / Medium / Large. Affects every Settings widget except the UF8 / UC1 schematic labels, which stay locked to 12 px so the mockups don't reflow when the picker changes.

GR-cal numeric inputs and the FX Learn binding column now scale with the font picker so layouts stay aligned across sizes.

### Keyboard Cmd / Ctrl as modifiers

New `Settings → Modes → Keyboard Options` tab (renamed from Faders). Three independent checkboxes — when ticked, holding **Shift**, **Cmd** (macOS) or **Ctrl** (Windows / Linux) on the host keyboard counts as a modifier for any binding's Plain / Shift / Cmd / Ctrl modifier slots. Mirrors the existing UF8-side modifier behaviour for users driving the surface alongside the host keyboard.

### Wrap Plugin Cycle toggle

`Settings → Device → Plug-ins → Wrap Plugin Cycle`. Default on (legacy behaviour). When off, both ends of the FX chain hard-stop on every cycle path (Channel-Encoder FX/Instance Cycle, per-strip V-Pot FX/Instance Cycle) and the UC1 carousel shows no neighbour name past the first / last FX.

### Hide tracks in collapsed folders

`Settings → Device → Tracks → Hide tracks in collapsed folders`. Independent surface-side mirror of REAPER's "hide children of collapsed folders" preference — when on, any track whose ancestor folder has `I_FOLDERCOMPACT == 2` (fully collapsed) drops from the UF8 strip list. Walks ancestry on every track-list rebuild so the filter follows live folder-state changes.

### UC1 Encoder 2 push actions — unified picker

`Settings → Modes → Nav → Plain push action / Shift + push action / Long-press action` now use the same 7-option enum and the same dropdown widget for every gesture:

- Jump + Drill
- Jump only
- Drill only
- Back
- Toggle View
- Add marker at playhead
- Disabled

View-locks (Markers-only / Regions-only) now suppress **Drill only** — every other action fires regardless of lock. Defaults match prior behaviour: Plain = Jump+Drill, Shift = Drill, Long = Back. The Shift and Long ExtState keys were migrated to `_v2` suffixes since the old 0/1/2 values mapped to different actions; existing Shift / Long config is reset on upgrade, Plain config carries over.

### Settings layout pass

- **Selection Sets** + **Parameter Groups** rebuilt as fixed-width tables — no more pixel drift when switching Snapshot ↔ Group, or when Active toggles. Save / Clear buttons share identical widths for clean grid alignment. The Selection-Set "Recall" button was removed — slot activation is hardware-driven (the `Recall Selection Slot` builtin, params 1..8); the `•` marker in column 1 shows the active slot.
- **Bindings → UF8 mockup**: silk-screen labels and section banners locked to 10 px; titles centred over their button blocks with consistent vertical spacing; LAYER / QUICK no longer overlap; Nav-cross down arrow aligned with FOOT 1 / 2 bottom.
- **Bindings → UC1 mockup**: ENCODER 2 banner centred over ROTATE / PUSH tiles.
- **LED override** (per-binding editor): colour swatch + Off/Dim/Bright radios inline to the right of the checkbox so Active and Inactive rows align vertically.
- **Pagination hints** on Nav-Mode UF8 strips 0 / 7 removed — `<<` / `>>` rendering and the toggle in Settings were vestigial.

## Upgrade notes

- **Shift + push / Long-press action defaults**: any custom config you'd set in v0.1.3 or earlier resets on first launch (storage-key bump). Re-pick from the new dropdown if you'd customised them.
- **Carousel scope** (independent UC1 enumeration): still placeholder-only in this build. The UI shows "not yet implemented" — the full implementation lands in v0.1.5.

## Known issues carried forward

- **UC1 stall-recovery on macOS** still unsolved (`clear_halt` returns 0 but next transfer STALL/IO). Workaround = power-cycle the UC1 at the unit; separate USB cable for UC1 (not through UF8 downstream) is the current hypothesis under test.
