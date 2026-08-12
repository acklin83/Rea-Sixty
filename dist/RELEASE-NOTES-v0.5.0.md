# Rea-Sixty v0.5.0, "Welcome to the party, pal"

The release the UF1 has been waiting for. Everything below the "UF1" heading is new.
Up to v0.4.x there was no UF1 device code at all, so plugging one in did nothing. It is now a
first-class surface next to the UF8 and UC1, with its own maps, its own feel settings and
its own screen.

294 commits since v0.4.5.

## Install via ReaPack

Add this repository URL under Extensions, ReaPack, Import repositories:

```
https://github.com/acklin83/reaper-scripts/raw/main/index.xml
```

Then open Extensions, ReaPack, Browse packages and install **Rea-Sixty**. Restart REAPER.

## UF1, native support

**Channel strip, DAW and Plug-in views.** The small LCD paints track name, output level,
pan line and bar, channel number and colour, and Solo, Cut and Sel light in the track's own colour.
The channel encoder steps one track per click, Fine works in every channel mode, and the
DAW view follows the selection instead of dragging it around.

**Plug-in mode.** Four V-Pots drive the focused SSL channel strip (CS 2, 4K B, 4K E,
4K G, 360 Link) or a standalone Bus Comp, with the display soft-keys on the plug-in's own
toggles. The 360-Link Bus Compressor is recognised as its own type. The EQ graph draws
SSL's own curve, and for a channel strip you learned yourself it draws ours from
REAPER's parameters.

**Meter view.** Overview bargraphs, the analogue VU/PPM needles, the goniometer, the RTA
spectrum and the full Loudness page set, fed live from the SSL 360° plug-ins. Meter
instances can be pinned, and a track with two meters no longer draws one while editing the
other. The needles read the plug-in's own meter rather than a derived peak.

The meter view carries its own soft-keys as well: RESET for the holds, FINE for a
finer resolution on continuous parameters, and PRESETS, which browses the plug-in's
own preset library read straight from SSL's on-disk XML. The channel strip follows
the selection in that view and keeps showing levels.

**Jog Mode.** The jog wheel no longer has one job. It has an **object**, and the object
decides what turning it means. Hold `SCRUB` and turn the wheel to pick between
**Playhead**, **Scrub** (audible scrubbing), **Items**, **Envelope** and **Razor**.

The split that makes it work: **the arrow cross selects, the wheel moves.** You never let
go of the wheel to change what you are working on. The cross follows the object too. In
Items it walks item to item (left and right) and track to track (up and down); in
Envelope it steps
point to point and switches lane, and its centre toggles whether the wheel edits the
points or the playhead; In Razor it aims at the whole area or one of its four edges, and the cross keys light to
show which.

Three modifiers stack on the wheel:

- **Shift** is fine, dividing the step (a quarter by default).
- **Cmd** copies instead of moving.
- **Ctrl** is the **cross axis**. In Items that is track by track instead of in time. In
  Razor it moves the area to the next lane, media lane or envelope lane, rather than
  along the timeline. The vertical axis is discrete, so it steps one at a time.

In Items and Envelope, `SHIFT` with left and right **adds** to the selection instead of replacing
it, so you can walk a run of items or points onto the wheel.

**Razor's centre key is a held gesture.** Hold it and the razor area drags its content as
one continuous move, and releasing commits. The content is grabbed once at the start, so
the area cannot sweep up the material it is dragging across. Hold `Ctrl` as well and the
whole thing, content included, moves to the lane above or below.

Every speed is adjustable per object in *Settings, Bindings, UF1*. The manual's **Jog
Mode** chapter has the full cross-and-wheel table.

**The MODE key is a hold, and it holds two pickers.** Keep `MODE` down and the four
display soft-keys pick the view (Plug-in, DAW, Meter, Sends). At the same time the
`CHANNEL` encoder steps the **Encoder Mode**, with SK4's label showing the live mode name
as you turn, so one held key gives you both. Let go and the choice sticks.

The encoder mode decides what the big notched `CHANNEL` encoder does when nothing is held:
track selection, playhead nudge, a synthesised mouse wheel, marker stepping, banking by one
strip, the last-touched parameter, walking FX or Instances on the focused track or across
tracks, or stepping the Selection Sets. Twelve in all. `Shift` with the rotation always
re-banks by one strip, whatever mode is set.

Which modes appear in the ring, and in what order, is yours to set in *Settings, Bindings,
UF1*, so you can leave out the ones you never use. **The UF1 keeps its own encoder mode**,
separate from the UF8's, so the two surfaces do not fight over one setting.

**Editing that behaves like the mouse.** Item and razor drops trim behind and crossfade on
release, following REAPER's own auto-crossfade / trim-behind / fade-length / fade-shape
preferences rather than a policy of ours. Razor edits work on **envelope lanes** too: the
area can span several lanes and move between tracks, opening the matching envelope on the
destination track and handing it back when the drag moves on, with the points coming
along. The drag stays non-destructive until you let go, so jogging back and forth loses
nothing, and the whole gesture undoes in one step.

**FX Learn on the UF1, with nothing to maintain twice.** A plug-in you already mapped for
the UC1 is **already mapped on the UF1**: leave the map's `UF1 layer` off and the UF1 fills
itself from the UC1 assignments. Turn the layer on only when the UF1 should differ, then
*Fill from UC1* to start from the UC1 mapping, or *Fill: Replace* / *Append* from the
plug-in's parameters. (One warning: an **empty** UF1 layer is authoritative, and clearing it
leaves the UF1 empty rather than handing it back to the UC1 fill. Untick `UF1 layer` to go
back to inheriting.)

Beyond that: its own explicit map next to the UC1's, click-and-turn learning,
Touch-to-Learn, per-page grids, layer actions (Fill Replace / Append / From UC1 / Unbind
all), soft-key push cycles and knob-travel curves. The Learn-HUD has a UF1 tab that follows
the hardware page. Maps can be sent to the UC1 and back.

**The SOFT key** above the fader display: name it yourself, give it a backlight, and it
pins the shown channel and engages in one press. Defaults to Pin Set.

**Extender.** The UF1 can act as the 9th strip of the UF8 bank, for tracks and for sends,
either side, behind a setting.

**Held-Track and Focus Set scope.** Park on a Focus-Set member and scroll the set
independently. The Focus Set scope can be Both, UF1-only or UF8-only.

Plus: per-surface encoder modes, dynamic soft-key banks with long-press paging, a Bus-Comp
gain-reduction readout on the four display soft-key LEDs, Sticky Pot on the above-fader
V-Pot, its own V-Pot speed and Fine factor, and a mode-change banner on screen.

## Names

**One name per parameter, across UC1, UF8 and UF1.** Rename a parameter once and every
surface, HUD and panel agrees.

**A user rename of a plug-in wins everywhere.** The colour bar, the focused panel and the
Learn-HUD used to show the factory name in places while other surfaces showed the rename.

**Longer names.** The colour-bar zone holds twelve characters, not the seven it had been
capped at, so the FX-Learn Short User Name, its auto-suggestion and the per-slot Display
label all take twelve now. Existing shorter names are unaffected.

## Sends and receives

The colour bar can name the route's **source track**, as `S:<track>` or `R:<track>`, in
send and receive modes where it otherwise has no plug-in to name. Off by default:
*Settings, Appearance, Surface display*.

## Settings

**The Device pane was split** into **Devices**, **Appearance** and **Behaviour**. At 890
lines and nine sections it had become "everything else". Metering settings that lived in
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
- Gate gain-reduction picks the meter by strip model rather than port order, so two strips
  on one channel no longer swap their readouts.
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
