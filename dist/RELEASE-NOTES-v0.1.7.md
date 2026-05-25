# Rea-Sixty v0.1.7

Big release: AutoLearn matching engine maps a freshly-loaded plug-in's parameters to UC1 slots and UF8 V-Pot banks automatically, with a slot-editor preview where you accept / reject each suggestion. FX Learn Editor was overhauled to be the single landing page (no more master-view), with per-slot editable scribble labels via right-click and a new per-fader scribble override. Plus Shift-Fine mode (Shift = 0.25× step on every encoder + V-Pot), USB-driver uninstall buttons on Windows/Linux, UC1 encoder calibration that no longer eats short detents, Smart Abbreviate that preserves trailing digit runs ("M33" stays "M33"), Nav Mode symmetry between UF8 and UC1, a palette quantizer pass (pink ≠ magenta, dark greys go off), real REAPER track numbers on the UF8 header, and pinned tracks that survive banking in TCP-mirror mode.

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

### AutoLearn — automatic plug-in mapping

New matching engine + Preview UI that takes a plug-in's parameter list and suggests UC1 slots + UF8 V-Pot bank assignments. Two-layer dictionary (hardcoded SSL seeds + patterns learned from existing user maps), three-pass matching (exact → substring → token), per-suggestion confidence score colour-coded in the UI. Open the **FX Learn Editor**, pick a plug-in (live FX on the focused track, or a stored snapshot), hit **AutoLearn** — modal popup shows every topology slot with the matched parameter, editable customLabel inline, Param dropdown with inline filter + `(unmapped)` entry, conflict highlighting for duplicate-param assignments, and confidence-score column. Accept / reject individual rows, then apply. Pre-flight Setup modal asks for the target domain (CS / BC / UF8-only) + UF8 V-Pot bank + UF8 Strip targets before running. Engine guards: strict keyword match for Bypass / Pan so "Bypass" no longer steals "Meter Scale" and "Out Trim" no longer steals "Output Pan"; CH-N-prefixed params detected and routed to UF8 strips / channel-positioned V-Pots rather than the generic V-Pot banks.

### FX Learn Editor overhaul

The editor is now the only landing page — no more master-view detour. Map picker combo + **Default** / **Short** / **+ New** / **Delete** / **Export** / **Import** all live in the editor header. Last-edited map persists across sessions via ExtState so the picker re-opens where you left it. The `+ New` popup adapts: **Create + AutoLearn** when the FX isn't in the session, **Insert + AutoLearn** when it is. The GR-meter VST3-param picker is gone — runtime always uses the PreSonus `GainReduction_dB` host extension now (introduced last release). All modals anchor to the parent host window's screen-centre via a new `centerNextPopupOnDisplay_` helper, so wide popups no longer slam into the top-left corner on this ReaImGui build.

### Per-slot custom display labels

New `customLabel` field on every user-mapped slot: right-click any slot in the FX Learn Editor → **Edit display label** → set a short string that overrides the parameter's default name on UF8/UC1 scribble strips. Empty string = fall back to the parameter name. Canonical name shows as placeholder while editing. JSON-persisted as an additive field (no format version bump, backward-compatible with v0.1.6 maps).

### Per-fader scribble label

New `faderLabel` field on each UserUf8StripBinding — scribble-strip override per fader. UF8 plug-in mode reads it when the V-Pot bank slot for that strip is unmapped, so a fader that controls a custom parameter can be labelled independently of the V-Pot row.

### Shift activates Fine mode

New **Settings → Modes → "Shift activates Fine mode"** toggle. With it on, holding the keyboard Shift key OR pressing the UF8 Shift button momentarily engages Fine mode (0.25× step) on every V-Pot and UC1 encoder. Releases back to normal step on release. Faders are unaffected. Works alongside the existing UC1 Fine button toggle — Shift wins while held.

### USB driver uninstall buttons

**Settings → About** gains **Uninstall** buttons next to the existing Install buttons:
- **Windows:** removes the WinUSB driver + signing cert via PowerShell (UAC prompt).
- **Linux:** removes `/etc/udev/rules.d/99-rea-sixty.rules` via pkexec.

Use case: switching back to SSL 360° temporarily, troubleshooting, or uninstalling cleanly.

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

- AutoLearn engine: exact-match confidence capped at 1.0 (was rendering as 105%); single-source user-learned entries capped at 0.75 (yellow "verify this" signal); CH-N params no longer pollute the V-Pot bank suggestions.
- AutoLearn domain-filtered slot-name lookup — linkIdx isn't unique across domains, the old code returned BC Threshold's name for any CS linkIdx=1 binding (Out Gain @ 100% bug gone).
- ReaImGui vendor binding: `ImGui_InputText` signature corrected to 6 args (callbackInOptional trailer was added in v0.10) — fixes the invisible-widget bug on the Display-label InputText in the FX Learn right-click menu.
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
