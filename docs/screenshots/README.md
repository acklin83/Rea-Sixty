# Screenshots

UI screenshots of Rea-Sixty, captured 2026-07-19 on macOS against a
`v0.3.2-8-g940a43f` build. Source for the website (`reasixty.com`) — the site
references these directly.

**These are published on a public website.** Anything captured here is public.
Two files were edited before landing; see *Redactions* below.

`docs/user-manual.md` deliberately does **not** reference these. It is baked
into the extension as `kUserManual` and rendered by `ManualView.cpp`, which has
no image support, so an embedded `![](…)` would show as raw markdown in the
in-app manual. The website injects screenshots per chapter at render time
instead, which keeps the manual source valid for all three outputs (in-app,
PDF, web).

## Redactions

| File | What was removed | How |
| --- | --- | --- |
| `settings-device-1.png` | Hardware serial numbers of the author's UF8 and UC1 | Filled with the panel background; the row now reads `UF8 [connected] [Identify]` |
| `settings-modes-dynamount.png` | Two live studio LAN addresses in the IP column | Filled with the field background, so both rows read as empty IP fields like the six below them |

The unedited originals are not in the repo. If a screenshot needs re-taking,
re-capture rather than trying to restore these.

## Index

### On-screen display (Learn-HUD)

| File | Shows |
| --- | --- |
| `hud-channel-strip.png` | Learn-HUD, Channel Strip tab — full CS mockup driving bx_console SSL 4000 G, with the parameter list and mapped-parameter dots |
| `hud-channel-strip-rightclick-pot.png` | Same, with the right-click menu open on the LO-PASS pot (polarity, knob travel, push reset, feel presets) |
| `hud-channel-strip-rightclick-advanced.png` | The **Advanced** popup — knob-travel curve editor with sensitivity and Linear / Log / Exp presets |
| `hud-bus-comp.png` | Learn-HUD, Bus Comp tab — Bus Comp section lit, GR needle meter, bx_townhouse parameter list |
| `hud-uf8.png` | Learn-HUD, UF8 tab — eight strips (pan, solo, cut, mono, faders) against SSL Delta Control 16 |
| `hud-uf8-rightclick-pot.png` | Same, with the V-Pot 2 right-click menu open |
| `hud-uf8-rightclick-softkey.png` | Same, with the V-Pot Bank 2 right-click menu open showing the colour palette |

### Settings window

| File | Shows |
| --- | --- |
| `settings-device-1.png` | Device tab — connected devices, LED/LCD brightness, display behaviour, overlay colours |
| `settings-device-2.png` | Device tab scrolled — master track, meter fall rates, track and plug-in options, keyboard modifiers |
| `settings-device-3.png` | Device tab scrolled further — V-Pot resolution, virtual notch, UC1 GR calibration tables |
| `settings-appearance.png` | Appearance tab — theme, font size, spelling, settings-window options |
| `settings-bindings-uf8.png` | Bindings tab, UF8 sub-tab — full surface mockup plus the short-press / long-press editor |
| `settings-bindings-uc1.png` | Bindings tab, UC1 sub-tab — UC1 mockup with encoder rotate / push targets |
| `settings-modes-auto.png` | Modes → AUTO — automation-arm filtering, fill direction, selection-set auto-mode |
| `settings-modes-fx-cycle.png` | Modes → FX / Cycle — which controls drive the FX cycle, V-Pot push behaviour |
| `settings-modes-rec.png` | Modes → REC — RME / TotalReaper preamp mapping (gain, 48V, pad, phase) |
| `settings-modes-nav.png` | Modes → NAV — marker and region navigation, per-surface overlay, encoder push actions |
| `settings-modes-nudge.png` | Modes → Nudge — playhead nudge unit and amount per detent |
| `settings-modes-dynamount.png` | Modes → Dynamount — up to 8 robotic mic stands, fill direction, calibration |
| `settings-fx-learn.png` | FX Learn tab — a ReaComp snapshot bound to the BC mockup, parameter list, VU calibration |
| `settings-favourites.png` | Favourites tab — copy behaviour plus the Channel Strip and Bus Compressor set lists |
| `settings-selection-sets.png` | Selection Sets tab — eight slots, six bound to REAPER track groups |
| `settings-parameter-groups.png` | Parameter Groups tab — eight slots, Slot 1 populated |
| `settings-manual.png` | Manual tab — the manual rendered in-app, chapter list on the left |
| `settings-about.png` | About tab — version, codename, build, repository, setup export/import, log paths |

## Notes for reuse

- **`settings-about.png` and `settings-manual.png` show v0.3.2**, captured
  before the v0.3.3 tag. They date faster than the rest — re-take them after a
  release if the version string is legible at the size used.
- `settings-about.png` carries the author's real name, `stoersender-studio.ch`
  and a `paypal.me` link. That is intentional attribution in an open-source
  about box, not a leak, but it is the one screenshot that publishes a name and
  a payment link together — leave it out of contexts where that is not wanted.
- Captured at Retina scale (roughly 2300–2700 px wide). Metadata is stripped.
  Let the site's image pipeline downscale; do not commit pre-scaled variants.
