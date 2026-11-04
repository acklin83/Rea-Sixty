# MoreMe: groups and mute on vertical faders (04.11.2026)

All in `extension/orc/moreme/moreme.html`, after ORC 1.6.2, not released yet.

Fixed:
- Collapsing a group did nothing on vertical faders.
- Mute moved a thin strip: wider padding, the value wrapped, the fader slid down.
- Cut-off dB marks on thin strips.
- On a phone on its side, the meter of a thin strip ran over EQ and DYN.
- Faders of different lengths after turning the phone back upright.
- The group triangle was too small and too pale: now drawn, with a larger tap area.
- A performer without a master still showed it on strips: `.card.nomaster` lost to `#mix.wide .card` as well.
- List view: on a muted channel, M lay over DYN. The card now folds to one line again.

Cause of most of them: `#mix.wide .card` beats any rule made of classes alone. The muted
fold now applies to `#mix:not(.wide)` only, instead of being undone strip by strip.

Checked in the iOS Simulator (iPhone 18 Pro, Safari) against a fake TotalMix on system
ports: upright and on its side, list view, a long group name, a performer without a master.
The iPad Pro 13-inch too, upright and on its side.

Installed in /Applications as a development build of 4b5d9343: version 1.6.2 build 12,
Developer ID, not notarised. The next release brings the notarised app back.
