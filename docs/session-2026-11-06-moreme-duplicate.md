# Session 2026-11-06: MoreMe, duplicate performer, one output per performer, groups fold

Frank, 06.11.2026: a performer can be copied with all faders and settings; the groups in the left
column fold; "Dürfen nie 2 Performer denselben Output haben!"

## Built

- `applyDesk` message `copy` (`extension/orc/Performers.cpp`): everything of the performer except
  the link (new token), the colour (empty, so the palette tells the two apart) and the output (the
  next free of Phones 1-4, else none). The mix in TotalMix is not copied (Frank). The copy goes to
  the end of the list: in between, it would move the palette colour of everyone after it.
- One performer per output, on every way into the book: `add` and `copy` take a free headphone,
  `update` keeps the old output when the new one is taken (the rest of the edit lands), and a file
  from before the rule loses the output on every later performer that shares it, in snapshots too.
  "" (no output) may be shared.
- `PhonesServer.cpp`: a desk that sent a message gets the book again even when nothing changed,
  so a refused output snaps back on its page.
- `desk.html`: *Duplicate performer* next to *Delete performer*; taken outputs are disabled in the
  menu with the owner's name; each group is one line (triangle, colour, name, "n ch") and opens on
  click. Closed from the factory, a new group opens; which are open is per browser
  (`localStorage` `moreme.desk.groupsOpen`).
- Second round (Frank: dot and font changed on opening, pills untidy, name field too small, count too
  far from the edge): the head is the same closed and open (round dot, name, count at the right edge);
  open, the dot is the colour picker and the name a frameless field. The channels are a list as in the
  phone's settings (dot, name, Remove). The x left the head: *Delete group* below the list, with a
  question. Colour inputs are round in WebKit everywhere on the page (fader rows and performer too).
- Manual (`extension/orc/manual/template.html`): Output row, Duplicate row, groups as lines.

## Checked

- `test_orc_phones`: copy, refused output, four headphones full, old file with shared outputs.
- Full ctest 47/47.
- Desk page in the demo server (fake TotalMix), in Chromium and in WebKit (offscreen WKWebView
  snapshot): copy selected, outputs greyed, groups closed and open.

## Open

- The rule is on the output spec. `output:8` and `phones1` could be the same bus if TotalMix
  later gives output 8 the Phones 1 role; that case is not caught.
- Installed in /Applications on Frank's word (06.11., Developer ID, not notarised). ORC was not
  running at the time; the login item still points to /Applications/ORC.app.
