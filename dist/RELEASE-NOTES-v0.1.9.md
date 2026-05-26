# Rea-Sixty v0.1.9

Ergonomics + stepped-param release. v0.1.8 brought knob travel to every user-mapped control; v0.1.9 makes it actually useful on the *stepped* parameters that v0.1.8 silently ignored — Townhouse attack/release time selectors, HPF slope pickers, oversampling toggles, anything REAPER reports with a per-step size. Sensitivity becomes "detent speed", Min/Max snap to the step grid, push-reset lands on a real value. The curve editor's right-click Min/Max rows are now a fixed-width 4-column table (label · slider · input · **Set**) so they line up pixel-perfect, and **Set** captures whatever the plug-in is currently showing into that range edge. Plus three small but daily-felt UC1 fixes: Encoder 2 stops double-stepping, deleting the active track snaps the carousel to the track directly above (instead of jumping to project track 1), and cycling between Instances no longer resets the focused parameter to slot 0 / Bypass.

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

### Knob travel for stepped parameters

The v0.1.8 knob-travel feature (range / curve / sensitivity) was designed for continuous params; on a *stepped* enum (PSP Townhouse attack-time picker, HPF slope, oversampling, etc.) the customizations silently did nothing because the plug-in re-quantized whatever the curve produced to the nearest step. v0.1.9 adds a step-aware path on every encoder site — UF8 V-Pots (Plugin Mode and focused CS/BC mirroring), UC1 channel-strip / bus-comp knobs, and the UC1 Extended-Functions encoder all detect stepped params via REAPER's `TrackFX_GetParameterStepSizes` and switch to a fractional accumulator:

- **Sensitivity becomes "Detent speed"** in the editor. `1.0×` = 2 detents per step (matches the legacy UC1 stepped feel). `2.0×` = 1 detent per step. `0.5×` = 4 detents per step. `4.0×` = 2 steps per detent. Shift = Fine still quarters on top — at slider min (`0.1×`) Shift gives an effective `0.025×` ≈ 40 detents per step for ultra-precise crawling.
- **Min / Max snap to the step grid** on commit, so the encoder always traverses real values within the user's chosen window. A hint line below the table reads `~K steps reachable in this range`.
- **Push reset** snaps the chosen default to the nearest step.
- **Curve editor swaps UI** when the bound param is stepped: canvas + preset row disappear, replaced by a single info line — `Stepped parameter — N values (~X.XXX per step). Curve disabled; Min/Max snap to the step grid.` REAPER's per-step size feeds the count automatically, no user entry needed.

A 150 ms idle accumulator decay keeps a slow turn from leaving residual fractional steps fighting a direction reversal.

### Curve editor — pixel-perfect Min/Max + Set capture

The per-slot right-click menu's Min/Max controls were inline sliders that drifted alignment as widget widths changed. They're now a fixed-width 4-column table: **label · slider · numeric input · Set button**. Both rows line up to the pixel, and the new **Set** button snaps that range edge to the plug-in's *current* parameter value — handy when you've dialled the FX to the desired upper or lower bound and just want to capture it as the limit. Sensitivity row in the curve editor popup got the same single-row layout treatment (label on its own line, slider + input + 1x reset on the next), and the help texts were shortened so they fit the popup width.

### UC1 Encoder 2 — no more double-steps

Encoder 2 occasionally fired two BC-track steps for one physical click — the threshold was tuned for "2-tick lone detent" but the hardware sometimes fires a 3-tick detent followed by a 1-tick stutter, which the threshold-2 accumulator collapsed into two clean step events. Threshold bumped to 3 (same as Encoder 1); no more drift.

### UC1 track delete — carousel snaps to the track above

Deleting the track that was active on UC1 used to leave the carousel showing the deleted track's name, and the next Encoder 1 turn jumped to the first (or last) track in the project because `stepVisibleTrack` got handed a dangling pointer. v0.1.9 caches the focused track's project index every poll tick; on invalidation it snaps focus to `cachedIdx − 1` (clamped — deleting track 1 lands you on the new track 1), triggers a refresh, and the carousel + zone 0x02 recover immediately. The Encoder 1 handler self-heals via the same path if a click lands between the delete and the next poll.

### Instance Cycle — focused parameter slot is sticky

Cycling Instances (Encoder 1 in Instance Sel-Mode, or any `instance_cycle` binding) used to reset the focused parameter slot to `0` on every step — for most plug-ins that's the Bypass / FX In toggle, which made the UC1 BC/CS encoder row and any V-Pot mirroring the focused param visually snap to "Bypass" on every cycle event. v0.1.9 preserves the current `slotIdx` across an Instance Cycle when the new instance offers the same LinkSlot (same domain, same parameter convention). Cross-domain cycles (CS → BC) and Domain::None (UF8-only user maps) still reset to `0` because the slot index isn't meaningful there.

## Bug fixes

- Curve editor popup grew 380 → 390 px height (cosmetic — was clipping the Close button on tighter rows).
- `tickStepped`'s lower sensitivity clamp dropped from `0.1` to `0.001` so Shift-Fine on the editor's slider minimum still slows further instead of getting swallowed by the clamp.

## Known issues

Same as v0.1.8. Nothing new.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.1.9.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.1.9.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.1.9.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).
