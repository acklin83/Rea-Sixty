# Screenshots

UI screenshots of Rea-Sixty, macOS. Two capture rounds:

- **2026-07-19**, `v0.3.2-8-g940a43f` — the Learn-HUD shots and everything
  under Modes / Favourites / Selection Sets / Parameter Groups / Manual / About.
- **2026-08-10**, `v0.4.4-215-ge52b288` through `fd8898e` — eleven Settings shots re-taken
  after the Device pane was split (Devices, Appearance, Behaviour, the three
  Bindings sub-tabs, FX Learn, Exchange).

Source for the website (`reasixty.com`) — the site references these directly.

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
| `settings-devices.png` | Hardware serial numbers of the author's UF8, UC1 and UF1 | Filled with the panel background (26,26,26); all three rows now read `<device> [connected]`, with the Identify buttons left intact |
| `settings-modes-dynamount.png` | Two live studio LAN addresses in the IP column | Filled with the field background, so both rows read as empty IP fields like the six below them |

The unedited originals are not in the repo. If a screenshot needs re-taking,
re-capture rather than trying to restore these — and redact the serials again,
on all three device rows.

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

Eight files were re-captured 2026-08-10 against the Settings reorganisation
(`e52b288`, branch `uf1-native-build`): the rail read **Devices ·
Appearance · Behaviour · Bindings · Modes · FX Learn · Favourites · Selection
Sets · Parameter Groups · Exchange · Manual · About** — twelve entries — with a
**Search settings** field above it. A thirteenth, **Sticky Pot**, was added
after Selection Sets on 2026-09-04 and is not in any shot yet. `settings-device-1/2/3.png` were removed in
the same commit; that pane no longer exists and there is nothing to re-point
them at.

The rows still marked *Re-shoot* below predate the reorganisation. They are
Modes / Favourites / Selection Sets / Parameter Groups / Manual / About, whose
CONTENT is unchanged — only the rail down the left edge is two entries short
and lacks the search field. Cosmetic, but visible on a full-window shot.

**Nothing here can be regenerated from the repo.** Screenshots have to be
re-taken by hand in REAPER, on macOS, at Retina scale. Every filename below
names a `.png` that is in this folder today.

**These do not ship yet.** Per Frank 2026-08-10 the manual and the website —
including everything UF1 — go public at tag **v0.5**. `settings-devices.png`,
`settings-bindings-uf1.png` and `settings-fx-learn.png` all show UF1 on screen,
so none of them may be published before that tag.

| File | Shows | Status |
| --- | --- | --- |
| `settings-devices.png` | **Devices** pane — connected devices (UF8 / UC1 / UF1), LED + LCD brightness, Metering, V-Pot / encoder feel | Serials redacted, see above. Ends below *Notch hold*; the rest of the pane is the next row |
| `settings-devices-gr-cal.png` | **Devices → UC1 GR calibration** — the two per-tick offset tables (BC VU meter, CS DYN GR LEDs) | Scrolled continuation of the row above |
| `settings-appearance.png` | **Appearance** pane — On-screen (overlay, focused-track panel, mode banner, colours, geometry), Surface display, Theme, Font Size, Spelling, Settings window | Whole pane in one shot |
| `settings-behaviour.png` | **Behaviour** pane — Tracks, Master track, Plug-ins, Soft-keys, Keyboard | Whole pane in one shot |
| `settings-bindings-uf8.png` | Bindings → **UF8** — full surface mockup, layer / quick selectors, scroll-bank radio | |
| `settings-bindings-uc1.png` | Bindings → **UC1** — UC1 mockup with the Encoder 1 rotate binding open, modifier rows below | |
| `settings-bindings-uf1.png` | Bindings → **UF1** — UF1 mockup: soft-keys, four V-Pots, channel encoder, jog wheel, secondary transport | **v0.5 only** |
| `settings-exchange.png` | **Exchange** pane — the live Mapping Exchange listing with vendor, map count, surface and coverage | |
| `settings-modes-auto.png` | Modes → AUTO — automation-arm filtering, fill direction, selection-set auto-mode | |
| `settings-modes-fx-cycle.png` | Modes → FX / Cycle — which controls drive the FX cycle, V-Pot push behaviour | |
| `settings-modes-rec.png` | Modes → REC — RME / TotalReaper preamp mapping (gain, 48V, pad, phase) | |
| `settings-modes-nav.png` | Modes → NAV — marker and region navigation, per-surface overlay, encoder push actions | |
| `settings-modes-nudge.png` | Modes → Nudge — playhead nudge unit and amount per detent | |
| `settings-modes-dynamount.png` | Modes → Dynamount — up to 8 robotic mic stands, fill direction, calibration | |
| `settings-fx-learn.png` | **FX Learn** pane — a bx_console SSL 4000 G snapshot on the UF1 mockup, Modifier layers header, page tabs, parameter list, GR Cal | **v0.5 only** — shows the UF1 layer |
| `settings-fx-learn-modifier-layers.png` | **FX Learn → Modifier layers** expanded — the three hold-to-switch checkboxes that moved here out of the old Keyboard Options | |
| `settings-favourites.png` | Favourites tab — copy behaviour plus the Channel Strip and Bus Compressor set lists | |
| `settings-selection-sets.png` | Selection Sets tab — eight slots, six bound to REAPER track groups | |
| `settings-parameter-groups.png` | Parameter Groups tab — eight slots, Slot 1 populated | |
| `settings-manual.png` | Manual tab — the manual rendered in-app, chapter list on the left | |
| `settings-about.png` | About tab — version, codename, build, repository, setup export/import, log paths | |
| `settings-search.png` | The rail **search** with `rme` typed — two hits, each two lines (setting, then `Pane › Section`), and the pane behind it switched to Modes → REC | **2000 px wide, not Retina** — supplied as a re-encoded webp and converted, so it is the one shot that breaks the rule below. Re-take at full scale when convenient |

### Panes with no screenshot at all

None. Every rail entry and every section that needed one has a capture.

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
