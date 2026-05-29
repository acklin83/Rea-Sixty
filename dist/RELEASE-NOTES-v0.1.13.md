# Rea-Sixty v0.1.13 — *Monday is my favorite time of year*

Mapping a new plug-in just got a lot less manual: **Quick Learn** sweeps the whole project (or a single track) and guides you through learning every unmapped FX, and AutoLearn can now lay an entire plug-in's parameters straight onto the UF8 faders. Plus a critical V-Pot crash fix and a clutch of routing / matching corrections.

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

### Quick Learn — guided FX mapping

A new guided flow that sweeps the project for unmapped plug-ins and walks you through learning them one at a time, instead of opening the mapping editor per plug-in by hand. It ships in two scopes:

- **Quick Learn (Project)** — every unmapped FX across the project.
- **Quick Learn (Track)** — limits the sweep to the focused / selected track.

Each scope is exposed three ways: a REAPER action, a bindable native action, and a button in the FX-Learn pane next to AutoLearn. Both no-op (won't even open the window) when there's nothing left to learn.

### AutoLearn — map all params to UF8 faders

A new Setup checkbox lays a plug-in's parameters, category-sorted, 1:1 across the UF8 faders — so a plug-in can be mapped onto the surface even with the V-Pot target turned off. Default on when V-Pots are off.

### AutoLearn preview polish

The Param dropdown and the per-row Learn column are now consistent across every preview table. REAPER's injected MIDI-learn parameters ("MIDI CC 0|0", "MIDI Pitch", …) no longer clutter the param picker — they're filtered out everywhere.

### V-Pot Fine + push-reset

- **UC1 Fine** now also applies to UF8 V-Pot rotation, not just the UC1's own encoders.
- **Push-reset safety net:** after a V-Pot push, rotation on that strip is swallowed for 250 ms so a tiny twist-on-press can't bump the parameter back off the value you just reset.

### Configurable encoder nudge

Settings → Modes → Nudge: the channel-encoder nudge step is now configurable (unit + amount) instead of a hardcoded 1 second per detent.

## Bug fixes

- **V-Pot turn no longer crashes REAPER.** A self-recursion in the Fine-mode check was lowered by the compiler to a hard trap and fired on every V-Pot parameter move. Fixed.
- **AutoLearn matched Bus-Comp params to the wrong slot** (e.g. a plug-in's "Release" landing on the "Input" slot). The generic-naming dictionary no longer leaks Channel-Strip slot indices into the Bus-Comp namespace, so a literal "Release" param now maps to Release.
- **UC1 routing diagram** now paints the moment you press the Routing button, instead of staying blank until your first encoder turn.
- **Developer grouping** vendor fallback now resolves for non-SSL maps too (previously only SSL plug-ins got a developer group).
- **V-Pot stale-track-pointer guards** in the input drain + plug-in lookup, closing a rare crash on track delete / re-bank.
- **SEL-LED magenta/pink** colours were swapped — corrected.
- **Acustica Audio (Acqua) plug-ins** are no longer host-polled for gain reduction and their GUIs are never auto-opened, removing the crash path their non-standard threading model triggered.

## Known issues

- During playback, the UF8 region top-soft-key drill is gated by REAPER's smooth-seek queue — the display update + the playhead seek can lag by a region length. Stop transport for snappier drills.

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.1.13.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.1.13.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.1.13.tar.gz` → unpack `reaper_rea-sixty.so` into `~/.config/REAPER/UserPlugins/`. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button).
