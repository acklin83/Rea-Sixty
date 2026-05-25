# Rea-Sixty v0.1.7

Surface polish + Nav-mode symmetry release. UC1 encoder calibration removes lost detents, Smart Abbreviate preserves trailing digit runs across UF8 + UC1, and Nav Mode now drills via the UF8 channel-encoder with the same plain/shift/long action picker as UC1 Encoder 2. Plus a palette quantizer pass (pink ≠ magenta now, dark greys go off instead of pale-violet), UF8 channel-number digits switched to REAPER's real track number (matches what UC1 already shows), and a new TCP-pinning behaviour so pinned tracks stay anchored to the leftmost strips through banking.

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

### UC1 encoder calibration

CHANNEL encoder `ticksPerStep` dropped 4 → 3, BC encoder dropped 3 → 2. The encoders fire 3 OR 4 (CHANNEL) / 2 OR 3 (BC) ticks per physical detent depending on rotation speed; the previous calibration ate the shorter-tick clicks. Both diagnosed with `/tmp/rea_sixty_encoder.log` instrumentation against hardware.

### Smart Abbreviate everywhere

Track-name shortening now lives in a single `TrackName` helper shared between UF8 (7-char scribble), UC1 carousels (CS=12, BC=14), and the REC-mode V-Pot label (7-char). Pass 4 reserves trailing 2+ digit runs as identity-like, so "MPEQ Main 2 M32" abbreviates to "MPM2M32" instead of "MPEM2M3". Pass 3 no longer collapses repeated digits ("M33" stays "M33", not "M3").

### Nav Mode — UF8 symmetry + shared push dispatcher

UF8 channel-encoder now mirrors UC1 Encoder 2 in Nav Mode:
- Rotation moves the drill cursor one step per detent (vs. paging 8 at a time) when **Settings → Nav → "UF8 encoder takes over in Nav"** is on.
- Push fires the same shared plain / shift / long action picker as UC1.
- New "**Keep track colour**" radio in the colour-bar source picker: LCD bar follows the track colour while the top-soft-key LEDs keep the marker / region colour as a visual cue.
- New "Show UF8 Nav overlay" toggle — render Nav-mode visuals on UF8 independently of UC1.

### Nav LED dedup fix

Channel-encoder-mode LEDs (Nav / Nudge / Focus) now refresh on per-cell state change instead of only on encoder-mode change. Previously a binding whose `stateOf` depended on something other than the encoder mode (e.g. `marker_overlay_toggle` on Nav) never re-triggered the refresh block and the LED froze.

### Encoder Mode exits Nav Mode automatically

Any user-initiated encoder-mode switch (Nav / Nudge / Mousewheel / Markers / BankBy1 / LastParam / Instance / FxCycle / SelsetCycle) now exits Nav Mode in one move. Project-load and setup-bundle paths bypass this so saved state survives.

### Palette quantizer — pink ≠ magenta, dark greys off

- **Pink** (hue ~330°, e.g. `FF0080`) now lands on `0x0B` light violet (LCD renders as lilac/pink) — was snapping to red because no pink anchor exists in the 11-entry palette.
- **Magenta** (hue ~300°, e.g. `FB02FF`) stays on `0x06` deep violet, so pink and magenta render as visually distinct cells. The SEL LEDs already differentiated; the LCD now matches.
- **Dark greys** (`mx - mn < 8`, e.g. `4C4C4C`) short-circuit to `0x00` OFF instead of `0x01` light violet. The LCD lights every quantised cell at constant brightness, so the old mapping made dark grey tracks look "white".

### UF8 channel-number digit = real REAPER track number

The tiny digit top-left of each strip's colour bar (and the "CH N" fallback when a track has no name) now reads `IP_TRACKNUMBER` — REAPER's 1-based actual track number, identical to what's shown in REAPER's track headers and what UC1's 7-segment indicator already displayed. Folder-collapsed sessions no longer show a confusing visible-slot index that diverges from REAPER's track list.

### Pinned tracks survive banking (TCP-mode)

REAPER's `B_TCPPIN` ("pin to top of arrange view") tracks now anchor to the leftmost strips on the surface, mirroring how REAPER's TCP itself renders them. Banking only moves the non-pinned strips. New checkbox **Settings → Modes → "Pinned tracks survive banking"** (default on, under the TCP/MCP "Surface mirrors:" radio). Sticky auto-disables when ≥ 8 tracks are pinned — pinned still sort first in the visible list and spill across banks rather than freezing the surface. MCP-mode is unaffected (MCP has no pin concept in REAPER).

### REC V-Pot keyboard-shift modifier

Shift+V-Pot for input-channel selection now honours keyboard Shift (gated by **Settings → Modes → Keyboard Options → "Use keyboard Shift as modifier"**). Previously only the UF8 hardware Shift button worked.

### Instance Cycle stale cursor + blank label for plug-in-less tracks

- UF8 csType: in Instance EncoderMode on the focused track, if the raw cursor points at an unmapped FX (stale from prior FxCycle), the strip falls back to the focused-Instance label. Matches UC1's existing behaviour.
- Tracks with zero plug-ins now render a **blank** slot on both UF8 and UC1 (was "REAPER" or "MAIN"). Clearer "nothing here" cue.

### Honorary-SSL spelling toggle

**Appearance → Spelling** radio — flips user-facing strings between "Colour"/"Grey" (British, SSL house style) and "Color"/"Gray" (American). Identifiers in code stay `color` regardless. Pure decoration.

## Bug fixes

- UC1 CHANNEL + BC encoders no longer skip detents on quick rotations.
- Nav-mode marker-overlay LEDs no longer freeze on encoder-mode-only refresh gating.
- UF8 channel-number digit no longer shows a misleading visible-slot index when folders are collapsed.
- Empty-track strips no longer retain stale "REAPER"/"MAIN" text from prior session via dedup-cache hit.

## Known issues

Same as v0.1.6. Nothing new.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.1.7.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.1.7.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.1.7.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).
