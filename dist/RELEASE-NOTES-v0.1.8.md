# Rea-Sixty v0.1.8

Knob-travel release: every user-mapped control on UF8 and UC1 — FX-Learn slots, UC1 channel-strip / bus-comp knobs, the UC1 Extended-Functions encoder, and UF8 V-Pots in Plugin Mode — can now be given a custom range, response curve, and sensitivity. A new V-Pot polarity switch (Unipolar / Bipolar) makes Pan-like parameters render centre-out on the LCD ring and gives Log/Exp curve presets that mirror around 0.5. The shared curve editor was rewritten to be target-driven, so the same modal serves slots and V-Pots, and the canvas Linear preset is now a clean 45° diagonal regardless of range. Plus a new GR-meter override popup in the FX Learn editor, two `fx_param_inc/dec` button actions that step the same slot a V-Pot is bound to, and a version tag in Settings → About.

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

### Per-slot knob travel — range / curve / sensitivity

Every user-learned FX-Learn slot gains four new fields stored alongside its existing inverted / customLabel state:

- **Min / Max** — clamp the effective parameter range to a sub-interval of `[0..1]`. A V-Pot turned all the way left lands at Min instead of 0; turned all the way right lands at Max instead of 1. Useful for plug-ins whose useful range only covers part of the parameter sweep.
- **Sensitivity** — encoder-delta multiplier `0.1×..4×`. Combines with the existing Shift = Fine modifier (Shift still quarters on top of the user-set sensitivity).
- **Curve** — piecewise-linear response curve drawn in the per-slot **Advanced…** editor. Draggable breakpoints; **Linear**, **Log**, **Exp**, and **Reset all** presets. Adds a non-linear feel without forcing the user to draw every segment.

Defaults (`Min=0, Max=1, sensitivity=1, no curve`) make the maths byte-identical to the previous linear path — existing setups behave exactly as before. JSON is additive; v0.1.7 maps load unchanged.

In the FX-Learn schematic, slots with customised travel show:
- Two radial ticks at the Min / Max angles on the on-screen knob (7 o'clock → 12 → 5).
- A small centre dot when a curve is set.

### Knob travel works on UC1 too

The UC1 channel-strip / bus-comp knobs and the EXT_FUNCS encoder now honour the same per-slot range / curve / sensitivity as the UF8 V-Pots. A UC1 knob and a UF8 V-Pot bound to the same parameter stay in lock-step — both go through `sensitivity → inverseCurve → step → applyCurve`. Built-in SSL CS/BC slots are untouched and keep the legacy linear + EQ-gain virtual-notch path; knob travel only kicks in when a user-learned slot is present for the focused plug-in.

### UF8 V-Pot knob travel (Plugin Mode, user-map)

UF8 V-Pots in Plugin Mode now expose the same Min / Max / Advanced… controls on right-click as the FX-Learn schematic. Range and curve are stored per-binding `(faderBank, vpotBank, strip)` so each binding can carry its own travel — different V-Pots can drive the same plug-in parameter with different feels if you want. **Fill Sequential** copies travel verbatim alongside the existing inverted / vpotMode / colour fields, so customising Strip 1 once and filling rightward propagates the curve too.

The LED ring on the LCD V-Pot bar now follows the encoder's t-space (inverse-curved) rather than raw parameter value, so a tight range no longer compresses the ring into a sliver of its sweep.

> **Note:** UF8 *faders* in Plugin Mode were tried with this feature and reverted same day. Absolute-position + motor-feedback creates round-trip races with plug-in quantisation (fader jumps during user motion, snaps on release). The plug-in's own taper is the right place for fader-side shaping.

### V-Pot polarity — Unipolar / Bipolar

New per-V-Pot polarity toggle (right-click any V-Pot binding → **Polarity: Unipolar (0 → 1)** / **Polarity: Bipolar (− ◂ 0.5 ▸ +)**). Default Unipolar — opt in per binding; existing setups behave as before.

**Bipolar** changes three things at once:
- **LED ring** renders centre-out (mode register `0x08`) — the same encoding SSL Pan / Gain / Trim slots use — instead of the L→R sweep (`0x01`).
- **Log / Exp presets** in the curve editor produce shapes that mirror around `0.5` — gentle ramp near the centre + coarse at the edges (Log), or fine control at the centre + rush to the extremes (Exp). Polarity is re-read on every preset click, so flipping it in the parent menu takes effect without re-opening the editor.
- **Push reset** slider shows a non-binding hint with a `0.5` quick-set button when the slider isn't already at centre (you keep full control of the actual value).

Made for Pan, EQ gains, mid-range freq sweeps — anything where "neutral" sits in the middle.

### Curve editor — Linear is now actually 45°

The curve editor canvas was painting the Linear preset as a shallow line whenever Min / Max were tight (e.g. Min=0, Max=0.5 produced a half-height ramp). The Y-axis is now normalised within `[rangeMin..rangeMax]` — Linear is always a 45° diagonal regardless of how the range is trimmed, and points display as their relative position within the envelope. Click / drag maps canvas Y back to absolute parameter space, so the stored data and the `applyCurve` / `inverseCurve` semantics are unchanged.

### Curve editor — one modal, multiple targets

The "Curve editor" popup is now target-driven. The same modal serves FX-Learn slots and UF8 V-Pot bindings, parameterised by a small read + four-setter callback set. Sensitivity row hides when the target doesn't support it (faders in earlier prototypes used this; left in for future targets).

### `fx_param_inc` / `fx_param_dec` button actions

Two new builtin actions in the Bindings action picker — step the same FX-Learn slot a V-Pot is bound to from a button. Configurable step size + wrap; respects the slot's range / curve / sensitivity so a button bound to `fx_param_inc` and a V-Pot bound to the same slot stay in sync. Useful for one-shot "+1 dB" or "next preset value" buttons.

### GR-meter override — compact popup next to AutoLearn

The FX-Learn editor regains an explicit gain-reduction override picker, this time as a small **GR** button next to **AutoLearn**. Click it to designate a VST3 parameter as the GR readout for plug-ins where the PreSonus `GainReduction_dB` host extension doesn't reach (or reads the wrong value). The override flows through to the UC1 BC VU motor calibration tables and the DYN GR LED strip. Defaults to "use host extension" — opt-in per learned plug-in.

### Settings → About — Version line

Adds a `Version: …` line above `Build:` in Settings → About. Sourced from `git describe --tags --always --dirty` via the existing `commit_count.h` header generator. On a release tag: `v0.1.8`. Between tags: `v0.1.8-5-g<sha>`. Dirty working tree: trailing `-dirty`. Makes it obvious which build is loaded when triaging an issue.

## Bug fixes

- FX-Learn schematic knob ticks: the range arc was previously starting at 10:30 and sweeping through 3 o'clock (off-centre). Corrected to a 300° sweep from 7 o'clock to 5 o'clock, dead-zone at the bottom where the physical knob's gap is.
- UC1 knobs were silently ignoring user-set range / curve / sensitivity — the v0.1.7 commit added the data and the UF8 dispatch but never wired UC1Surface. Now resolved.

## Known issues

Same as v0.1.7. Nothing new.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.1.8.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.1.8.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.1.8.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).
