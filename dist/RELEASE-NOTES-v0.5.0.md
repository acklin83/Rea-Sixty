# Rea-Sixty v0.5.0 — DRAFT

> Draft. Version and codename not final; `extension/version-names.tsv` still needs its row.

The release the UF1 has been waiting for. Everything below the "UF1" heading is new —
v0.4.x had no UF1 device code at all, so plugging one in did nothing. It is now a
first-class surface next to the UF8 and UC1, with its own maps, its own feel settings and
its own screen.

294 commits since v0.4.5.

## Install via ReaPack

Add this repository URL in *Extensions → ReaPack → Import repositories*:

```
https://github.com/acklin83/reaper-scripts/raw/main/index.xml
```

Then *Extensions → ReaPack → Browse packages* → install **Rea-Sixty**. Restart REAPER.

## UF1 — native support

**Channel strip, DAW and Plug-in views.** The small LCD paints track name, output level,
pan line and bar, channel number and colour; Solo/Cut/Sel light in the track's own colour.
The channel encoder steps one track per click, Fine works in every channel mode, and the
DAW view follows the selection instead of dragging it around.

**Plug-in mode.** Four V-Pots drive the focused SSL channel strip (CS 2 / 4K B / 4K E /
4K G / 360 Link) or a standalone Bus Comp, with the display soft-keys on the plug-in's own
toggles. The 360-Link Bus Compressor is recognised as its own type. The EQ graph draws
SSL's own curve — and, for a channel strip you learned yourself, ours from REAPER's
parameters.

**Meter view.** Overview bargraphs, the analogue VU/PPM needles, the goniometer, the RTA
spectrum and the full Loudness page set, fed live from the SSL 360° plug-ins. Meter
instances can be pinned, and a track with two meters no longer draws one while editing the
other. The needles read the plug-in's own meter rather than a derived peak.

**Jog Mode.** The jog wheel no longer has one job — it has an **object**, and the object
decides what turning it means. Hold `SCRUB` and turn the wheel to pick between
**Playhead**, **Scrub** (audible scrubbing), **Items**, **Envelope** and **Razor**.

The split that makes it work: **the arrow cross selects, the wheel moves.** You never let
go of the wheel to change what you are working on. The cross follows the object too — in
Items it walks item to item (`←` `→`) and track to track (`↑` `↓`); in Envelope it steps
point to point and switches lane, and its centre toggles whether the wheel edits the
points or the playhead; in Razor it aims at the whole area or one of its four edges, and
the cross keys light to show which.

Three modifiers stack on the wheel:

- **Shift** — fine, divides the step (÷4 by default).
- **Cmd** — copy instead of move.
- **Ctrl** — the **cross axis**. In Items that is track by track instead of in time; in
  Razor it moves the area to the next lane — media lane or envelope lane — instead of
  along the timeline. The vertical axis is discrete, so it steps one at a time.

In Items and Envelope, `SHIFT` + `←` `→` **adds** to the selection instead of replacing
it, so you can walk a run of items or points onto the wheel.

**Razor's centre key is a held gesture.** Hold it and the razor area drags its content as
one continuous move; release commits. The content is grabbed once at the start, so the
area cannot sweep up the material it is dragging across — and with `Ctrl` the whole thing,
content included, moves to the lane above or below.

Every speed is adjustable per object in *Settings → Bindings → UF1*. The manual's **Jog
Mode** chapter has the full cross-and-wheel table.

**Editing that behaves like the mouse.** Item and razor drops trim behind and crossfade on
release, following REAPER's own auto-crossfade / trim-behind / fade-length / fade-shape
preferences rather than a policy of ours. Razor edits work on **envelope lanes** too: the
area can span several lanes, move between tracks — opening the matching envelope on the
destination track, and handing it back when the drag moves on — and the points come with
it. The drag stays non-destructive until you let go, so jogging back and forth loses
nothing, and the whole gesture undoes in one step.

**FX Learn on the UF1 — and nothing to maintain twice.** A plug-in you already mapped for
the UC1 is **already mapped on the UF1**: leave the map's `UF1 layer` off and the UF1 fills
itself from the UC1 assignments. Turn the layer on only when the UF1 should differ, then
*Fill from UC1* to start from the UC1 mapping, or *Fill: Replace* / *Append* from the
plug-in's parameters. (One warning: an **empty** UF1 layer is authoritative — clearing it
leaves the UF1 empty rather than handing it back to the UC1 fill. Untick `UF1 layer` to go
back to inheriting.)

Beyond that: its own explicit map next to the UC1's, click-and-turn learning,
Touch-to-Learn, per-page grids, layer actions (Fill Replace / Append / From UC1 / Unbind
all), soft-key push cycles and knob-travel curves. The Learn-HUD has a UF1 tab that follows
the hardware page. Maps can be sent to the UC1 and back.

**The SOFT key** above the fader display: name it yourself, give it a backlight, and it
pins the shown channel and engages in one press. Defaults to Pin Set.

**Extender.** The UF1 can act as the 9th strip of the UF8 bank — track case and sends,
either side, behind a setting.

**Held-Track and Focus Set scope.** Park on a Focus-Set member and scroll the set
independently; the Focus Set scope can be Both, UF1-only or UF8-only.

Plus: per-surface encoder modes, dynamic soft-key banks with long-press paging, a Bus-Comp
gain-reduction readout on the four display soft-key LEDs, Sticky Pot on the above-fader
V-Pot, its own V-Pot speed and Fine factor, and a mode-change banner on screen.

## Names

**One name per parameter, across UC1, UF8 and UF1.** Rename a parameter once and every
surface, HUD and panel agrees.

**A user rename of a plug-in wins everywhere.** The colour bar, the focused panel and the
Learn-HUD used to show the factory name in places while other surfaces showed the rename.

**Longer names.** The colour-bar zone holds twelve characters, not the seven it had been
capped at — so the FX-Learn Short User Name, its auto-suggestion and the per-slot Display
label all take twelve now. Existing shorter names are unaffected.

## Sends and receives

The colour bar can name the route's **source track** — `S:<track>` / `R:<track>` — in send
and receive modes, where it otherwise has no plug-in to name. Off by default:
*Settings → Appearance → Surface display*.

## Settings

**The Device pane was split** into **Devices**, **Appearance** and **Behaviour** — 890
lines and nine sections had grown into "everything else". Metering settings that lived in
three places are together; V-Pot and encoder feel is one section.

**Search** now covers every pane control by control, shows two-line results (the setting,
then where it lives), and no longer tears the window down while you type.

## Mapping Exchange

Shared maps carry their UF1 layer, and the browser shows it before you take one.

## Fixes

- SEL double-press opens the FX chain, on UF8 and UF1.
- FLIP moves a Sticky-Pot pin onto the fader (UF1 + UF8).
- Renaming a plug-in no longer crashes the catalog mid-frame.
- Our REAPER actions report their state, so bound toggles light their LEDs.
- Gate gain-reduction picks the meter by strip model rather than port order — two strips on
  one channel no longer swap their readouts.
- UC1 encoder acceleration was squaring the step; it is out again.
- An FX-Learn label longer than its limit is trimmed instead of blanking the field.
- Linux: built against an older glibc baseline so the extension loads on Debian 12 / MX 23.

## Known issues

- A UF8 plug-in-mode fader assigned to an FX parameter does not return to its envelope in
  Touch during playback.

## Manual install

Download the platform archive from the GitHub release, unpack, and copy
`reaper_rea-sixty.<ext>` into REAPER's `UserPlugins` folder. macOS builds are signed and
notarised. Restart REAPER.
