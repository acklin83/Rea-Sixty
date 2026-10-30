# Session 2026-10-30: MoreMe live, ORC 1.5.0

## Done
- MoreMe switch-over on the Mac Studio: stoerme's LaunchAgent stopped and its plist moved to
  `~/Documents/dev/stoerme/`; ORC holds TCP 8088 and UDP 7004. Before that, stoerme held 8088 and
  the phones showed stoerme's page (header "stoerme", footer "stoerme v0.4").
- `2e213896` MoreMe tab: logo reset button reads "Default Logo".
- `d7c20f81` page: old `stoerme.*` keys are moved, not copied (reset landed on the same mix).
- `3bd5b0ae` V-Pot speed per surface in ORC (rme.json `steps.uf1Speed/uf8Speed`,
  `rme::input::Host::potSpeed` as the LAST field); Rea-Sixty's UF8 V-Pot speed now reaches the
  TotalMix side-car.
- `f605b3d8` own names for Phones 1-4 (moreme.json `names`), page language picker de/en/fr/it/rm.
  Rumantsch Grischun words looked up in the Pledari Grond; wants a native speaker's read.
- `046eec3c` stereo pairs: the right half leaves the surfaces' channel list; a flip of a known
  `/stereo` asks `/sendall`. Confirmed by Frank on the device.
- `3d452903` Klingon on the page, picked by hand.
- `43f0cf1a` "Use the UF1" / "Use the UF8" (rme.json `surfaces`), menu line "off".
- Website: `/orc` page live (`eddf6305`), phone gutter fixed site-wide (`73b9e0f9`), MoreMe
  section for 1.5.0 (`cb7c4e51`). All on `website`, pushed to `private` only.
- ORC 1.5.0 (build 8, `9c162b7f`) released: https://github.com/acklin83/ORC/releases/tag/v1.5.0,
  manual and README in acklin83/ORC `a2d3abb`.

## Decisions
- When two channels are linked, TotalMix itself takes the higher of the two levels. Frank: leave
  it; ORC does not write the left channel's levels back.
- Each on/off switch on its own settings page (UF1, UF8, MoreMe).

## Open
- Website deploy of `cb7c4e51` (Frank runs the three commands; Claude Code's auto mode blocks
  production deploys).
- Rea-Sixty release for the side-car parts (stereo fix, UF8 V-Pot speed).
- A native speaker's read of the Rumantsch texts.
