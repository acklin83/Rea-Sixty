# Rea-Sixty v0.1.2

Feature release adding three Device-level toggles, several SEL-Mode polish fixes, an FX/Instance Cycle dispatch bug fix, and a settings re-organisation.

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

### Device pane — three new toggles

All under **Settings → Device** (sidebar), grouped into new **Tracks** / **Plug-ins** / **Faders** sections:

- **TCP follows UF8 selection** — REAPER's arrange-view track panel auto-scrolls to keep the UF8-selected track in view. MCP follow stays always-on (separate scroll surface).
- **Show tracks hidden in TCP / MCP** — two independent toggles, default both OFF, so the UF8 mirrors REAPER's panel visibility. Flip a toggle ON to keep tracks on the surface even when that view hides them.
- **Don't show offline FX** — skipped by every cycle ring (Channel-Encoder FX/Instance Cycle, per-strip V-Pot FX/Instance Cycle) AND by the UF8 colour-bar default cursor. Offline-only tracks show a `-` placeholder.

### SEL-Mode polish

- **Auto exit refocuses the selected track** — leaving Auto SEL mode (whether via the Auto button toggle or the Norm button) now scrolls the UF8 bank so the selected track is in view again. Same behaviour when deactivating a Selection Set.
- **Auto exit reverts selset auto-arming** — when Settings → Modes → Auto's "Selection-Set auto-mode" is set to anything other than "None" and a Selection Set is active, leaving Auto SEL mode now reverts the set's tracks back to Trim/Read. Previously the revert only fired on explicit selset deactivation or auto-mode dropdown change.
- **Selset activation snaps bank to first channel** — recalling a Selection Set with more tracks than fit in 8 strips now positions strip 0 on the first set track.

### Bug fixes

- **FX / Instance Cycle dispatch swap** in `reasixty_dispatchSelModeCycle`. With "UF8 Ch. Encoder drives Sel-Mode cycle" enabled (Settings → Modes → FX/Instance Cycle), the V-Pot Sel-Mode "Instance Cycle" was routing to the FX-Cycle handler (and vice versa) — so Instance Cycle walked through every FX instead of only Instances. The five other cycle bindings (V-Pot rotation, Channel Encoder mode dispatch, UC1 Encoder 2) were already correct.
- **UF8 colour-bar follows Channel-Encoder FX Cycle cursor** — the focused-track strip now renders the cycle cursor's FX name through ReaEQ, ReaComp, etc., parallel to UC1's carousel. Previously the strip stayed pinned to the focused-domain Instance label on non-Instance landings.
- **UC1 carousel shows single-plugin name** — earlier, a track with only 1 cycle target (or only 1 online cycle target with hide-offline enabled) silently skipped the carousel feed. The cycle step is still a no-op but the carousel now displays the lone Instance/FX name.

### Settings re-organisation

- All Device controls moved into the **Settings → Device** sidebar pane. The orphan "Device" sub-tab under **Settings → Modes** is gone (there is no SEL Mode called "Device"; its two checkboxes — *Auto-engage UF8 Plugin Mode* and *Alt-drag fader snap-back* — now live in the main Device pane under Plug-ins and Faders).
- **UC1 GR calibration** moved to the very bottom of the Device pane — niche hardware-trim workflow, no reason to sit above the common settings.

### Plug-in identity (also from v0.1.1's WIP)

- **User-renamed FX instances** now win over hardcoded `displayShort` everywhere — primarily for SSL 360 Link, where the family-level "Link" / "L-BC" abbreviation hid what the wrapped plug-in actually was.

## Known issues

- **Mid-session USB disconnect on macOS** (Mac + UC1 + UF8 combo): occasionally both endpoints fail within ~3 ms of each other on a sustained host-side condition that even `libusb_reset_device` can't escape. Diagnostic logging is in place but no fix yet — physical replug recovers. See [handoff doc](https://github.com/acklin83/Rea-Sixty/blob/main/.claude/memory/handoff-2026-05-20-uc1-mac-disconnect.md) for state.
- **Linux:** plug UF8 + UC1 into separate PC USB ports. Daisy-chaining still triggers `xhci_hcd` port-cycling on Linux 6.17. Unchanged from v0.1.0.

## Manual install

Download the matching archive and follow the platform install docs:
- Mac: `rea-sixty-mac-v0.1.2.zip` (Developer-ID signed + Apple-notarised)
- Win: `rea-sixty-win-v0.1.2.zip` — drop all three DLLs into `%APPDATA%\REAPER\UserPlugins\`
- Linux: `rea-sixty-linux-v0.1.2.tar.gz` — see `INSTALL.txt` inside
