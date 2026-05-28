# Rea-Sixty v0.1.12 — *Bizarre Gardening Accident*

UC1 finally gets full RME / TotalReaper integration in REC mode (preamp gain, 48V, Pad, Phase Invert all on hardware buttons + live readout on the LCD), and Nav Mode is rebuilt as a per-surface matrix so UF8 and UC1 can independently scroll Regions / Markers with live cross-coupling.

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

### REC + RME (TotalReaper) integration on UC1

Same dispatch model that v0.1.10 brought to UF8 V-Pots is now wired to the UC1's focused-track surface. Out of the box: **48V on Enc2 Push, Pad on Cut, Phase Invert on Polarity**. Encoder 2 rotation steps preamp gain ±1 dB; hold the Fine button (or keyboard Shift) and rotate to cycle the track's hardware input channel.

The CS readout zone shows the live preamp state continuously while REC + RME is active — `48V Pd Ph        12.0dB`-style flag line, mirrored from TotalReaper's `P_EXT` cache. Cut / Solo / Polarity LEDs mirror the same state. After a Shift+rotate input-channel change, the readout briefly swaps to the channel name (`In MA 5/6  12.0dB`) for ~1.5 s.

Settings → Modes → REC now has a parallel "UC1 — focused track" sub-section with the same picker model as the UF8 V-Pot block; any of the four TotalReaper toggles (48V / Pad / Phase / Autolevel) can land on Enc2 Push / Cut / Solo / Polarity.

### Fine button is momentary in REC mode

Tracking workflow refinement: while REC / RecMon selection mode is active, the UC1 Fine button switches from toggle to momentary (press = on, release = off) and stops flashing its "Fine On / Off" readout. Doubles as a hold-to-Shift modifier for the Enc2 input-channel cycle.

### CS-scroll carousel suppressed in REC + RME + faster everywhere

The 3-second channel-name carousel on Encoder 1 channel change was masking the live REC + RME readout update — preamp toggles felt "sticky" because the new track's flags only appeared after the carousel timed out. Now skipped entirely when REC + RME owns the readout. Outside REC + RME the carousel timeout drops from 3 s to 1 s.

### Nav Mode — per-surface Mode matrix

Settings → Modes → NAV is rewritten as a per-surface matrix (UF8 column, UC1 column). Each surface independently picks Regions or Markers; UC1 additionally has a "Mirror UF8" mode (default) that preserves the v0.1.11 single-overlay behaviour.

**Cross-coupling** — when one surface is in Regions and the other in Markers, the Markers surface auto-scopes to the Regions surface's currently-selected region. Typical tracking workflow:

- UF8 = Regions (song sections), UC1 = Markers → UC1 carousel shows the takes within UF8's cursor section, live-updates as UF8 scrolls.
- UF8 = Markers, UC1 = Regions → mirror image; UF8 strips show the markers within UC1's cursor region.

Either cursor moving to a new region snaps the other surface to the first marker of the new scope automatically.

Removed: the legacy 4-state "Default view on entry" radio (Regions / MarkersInRegion / Markers / Last used). The UF8 Mode picker is now both the entry default and the live state — one knob instead of two.

The push-action picker (Plain / Shift / Long) is now labelled as shared between UC1 Encoder 2 and the UF8 Channel encoder (it always was; the label was misleading).

### UF8 region drill suppressed when UC1 owns the coupling

Pressing a region top-soft-key on UF8 in the coupled-Markers configuration would drill UF8 into MarkersInRegion view — breaking UC1's coupling (which needs UF8 to stay in Regions view to know which region to scope to). Drill is now suppressed on UF8 when UC1 is in an independent Mode; the jump still fires.

## Known issues

- During playback, the UF8 region top-soft-key drill is gated by REAPER's smooth-seek queue — the display update + the playhead seek can lag by a region length. Stop transport for snappier drills.
- Acustica Audio (Acqua) plug-ins have a non-standard threading model and may crash REAPER when their GUI is opened while our extension is loaded. Workaround: disable Settings → Device → "GR source: Any FX" and "Pin FX GUI". Investigation in progress.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.1.12.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.1.12.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.1.12.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).
