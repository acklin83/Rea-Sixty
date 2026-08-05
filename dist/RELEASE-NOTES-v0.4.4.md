# Rea-Sixty v0.4.4 — "It's just a flesh wound"

**The one where the Linux install was never actually dead.** On a minimal Linux box, installing Rea-Sixty via ReaPack could leave it *installed but invisible* — no Control Surface entry, no actions — because the `.so` needed a system library (`libhidapi-hidraw0`) that most desktops don't ship. It looked fatal; it was a flesh wound. Rea-Sixty now **bundles libusb + hidapi** alongside the plugin and loads them from its own folder, so a ReaPack install just works — no `apt install`, nothing to place by hand. Plus a new **FX-Learn "apply this feel to all mappings"** action, Sticky Pot riding the fader under FLIP, and a Bindings-editor tidy-up.

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

### Linux: self-contained install (no more "installed but invisible")

The Linux package now ships `libusb-1.0.so.0` and `libhidapi-hidraw.so.0` next to `reaper_rea-sixty.so`, and the plugin carries an `$ORIGIN` run-path so it loads them from its own directory. Previously the `.so` depended on those libraries being installed system-wide; `libhidapi-hidraw0` almost never is on a minimal Kubuntu / MX install, so REAPER's loader silently dropped the plugin — it showed as installed in ReaPack but never appeared under **Control/OSC/Web** and registered no actions. With the libraries bundled, a plain ReaPack install works out of the box.

- **No `apt install` step anymore.** (The libraries still use your system's `libudev`, present on every desktop Linux.)
- Existing installs pick this up on the next ReaPack update; if you were stuck, no manual dependency install is needed after updating.
- Mirrors how the macOS and Windows packages already bundled their libusb / hidapi.

### FX Learn — "Apply this feel to all mappings"

Unify knob feel across a whole plug-in map in one action. Right-click an FX-Learn slot → **"Apply this feel to all mappings"**: it snapshots that slot's feel (invert / range / curve / sensitivity / polarity / push-default) and writes it onto every mapped slot in the current map, on the layer you're editing. Pot controls only (toggles have no travel). The same action is on the **Learn HUD** too (Feel presets → "Apply this feel to all mappings"), so it works whichever way you tune.

### Sticky Pot under FLIP now drives the fader (UF8)

With a Sticky-Pot pin active and **FLIP** engaged, the fader now follows the **pinned** parameter instead of falling back to the focused V-Pot param. FLIP means "put the current V-Pot parameter on the fader" — and the pin *is* the V-Pot's parameter, so the pinned value, its touch/motor follow and the fader's value label all track the pin. Value routing only; no change to fader protocol, motor, touch or calibration. *(The UF1 half follows with the native UF1 build.)*

### Bug fixes

- **Bindings editor: "Display label" appears only where it renders.** The field showed on every control's action step, but it only draws on controls that actually have an on-surface text label — the UF8 top-soft-keys. It's now hidden everywhere else (other UF8 buttons, all UF1 controls) where it did nothing. User-Quick bank slots keep their own "Label" field, unaffected.

## Known issues

Same as v0.4.3. Sticky Pot v1 notes still apply on the V-Pot side (readout bar is a left-to-right sweep; a V-Pot press resets a continuous parameter to its centre).

## Manual install

If ReaPack isn't an option:

- **macOS:** `rea-sixty-mac-v0.4.4.zip` → unzip the three `.dylib` files into `~/Library/Application Support/REAPER/UserPlugins/`.
- **Windows:** `rea-sixty-win-v0.4.4.zip` → unzip the three `.dll` files into `%APPDATA%\REAPER\UserPlugins\`. Run the WinUSB driver installer from `Settings → About` on first launch.
- **Linux:** `rea-sixty-linux-v0.4.4.tar.gz` → unpack **all three** files (`reaper_rea-sixty.so`, `libusb-1.0.so.0`, `libhidapi-hidraw.so.0`) into `~/.config/REAPER/UserPlugins/`, keeping them together. Apply the bundled `99-rea-sixty.rules` udev rule (or use the in-app button). No separate dependency install needed.

The **Stream Deck Companion** plugin (`com.reasixty.companion.streamDeckPlugin`) is attached to this release separately — it is not part of the ReaPack package.
