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

## CI red on macos-x86_64: rme_osc "a late round value" (07.11.)

Red in 4 of 6 Build runs since `23b0a7b6` (05.11.), ORC 1.6.3's session commit included; nobody
looked. Not flaky by chance: reproduced locally every time by moving the write 100 to 250 ms before
the follow-up ask ORC sends 10 s after connecting (`kFollowUps`). The slower Intel runner put it
there often.

Real bug, not only the test: `ask()` cleared `writtenSinceAsk_` on every ask, also while the previous
round was still being collected. TotalMix sends a round over up to two seconds, so a late packet of
that round, carrying the value from before a fader write, overwrote the write after any second ask
(follow-ups, a stereo flip, a layout load, `refresh()`). Fixed in `RmeManager.cpp`: an ask while a
round is under way keeps the list; it ends with the round's commit. Shared code, so Rea-Sixty's RME
link has the same fix.

New check in `test_rme_osc`: a round whose old volume arrives 300 ms late, a write, `refresh()`;
fails 3 of 3 without the fix, passes 3 of 3 with it. The test's "all checks passed" line moved to
the end; it used to print above a FAIL from the later sections.

Not changed: the follow-up asks still ignore `kRoundQuietMs` (they start a round under a moving
fader in the first 10 s after connecting). With the guard kept, that no longer loses a write.
