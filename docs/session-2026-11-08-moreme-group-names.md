# Session 2026-11-08: MoreMe, names for a group's channels

Frank: "Wie können wir Channels in Groups renamen? Ich seh nur immer die Namen aus Totalmix, nur bei
einzel-channels kann ich renamen." Two ways put to him; he chose the group, for everyone.

## Built

- `Group::names` (`extension/orc/Performers.h`): member src to name, saved with the group in the
  snapshot (`"names":{"in:1":"Kick"}`). Names of non-members and empty names are dropped on reading;
  old files read without names.
- `Phones.cpp` `sourcesOf_`: `asMember` gives a member the group's name, on a performer's own list
  (group listed) and on TotalMix layout / Phones 1-4. A channel listed on its own keeps TotalMix' name
  or its fader's. A performer's own fader name for a member still comes first.
- `desk.html`: in an open group each channel row is a frameless name field, TotalMix' name as the
  placeholder (grey), an own name white. Remove also drops the name.
- Manual: a paragraph under Groups.

## Checked

- `test_orc_phones`: names in the file (non-member and empty dropped, round trip); Phones 1-4 shows
  the group's name and keeps the other member's; a performer through the group; their own fader name
  first; the channel alone keeps TotalMix' name. Without `asMember` two checks fail.
- Full ctest 47/47.
- Demo: name typed on the desk in Chromium, saved through ORC, shown in the preview; desk in WebKit;
  phone page in the iOS Simulator (iPhone 18 Pro), horizontal and vertical faders.

## Slip

- One `open_url` without a device name went to "Studionizer Vergleich iPhone", another session's
  simulator, and opened the demo address there. Every simulator call needs `device`.
