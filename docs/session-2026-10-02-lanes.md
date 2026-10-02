# Session 02.10.2026: Fixed lanes F.1, F.2, C1

Plan: `docs/fixed-lanes-plan.md`. Frank tested step 4 (B+E+G) at the surface: works.

## Built
- `fc0fe06` SHIFT + centre = play the comp lane. `lane_play_toggle` could only silence
  the one lane a step leaves playing. Bindings v53.
- `6df9193` F.1 painting + F.2 live comping. Centre = hold builtin `lane_comp_paint`
  (tap Comp here, hold + jog/encoder paints). Stroke plays back with 1 s either side,
  cursor to the stroke's end, source lane plays again. Live comping: every lane change
  while playing is a cut; loop jump closes at the loop end and opens nothing; a lane
  heard < 0.5 s was passed on the way and is not written. Behaviour > Fixed lanes
  (6 settings). Bindings v54.
- `391eeda` After a research pass on natural gestures: a hold without a turn does
  nothing (it toggled live comping, also when trying to paint while the comp lane
  plays). UF8 ENC PUSH held and let go unturned writes nothing in Lanes. Live comping
  only via `lane_paint_live` on a key of the user's choice.
- `c19ffbd` SHIFT + jog/encoder moving a comp area = one undo step per turn.
- `fbab791` C1 dynamic bank "Lanes" (kind 13): push = lane alone, Shift = layer,
  Cmd = Comp here; pages like the FX bank; per-kind tables grown to every kind.
- Manual: Fixed lanes chapter (bank, painting, live comping, workflows), Behaviour
  table, jog cross table; "FIT" removed (it was a binding name, not a key name).

- `b707e4f` C2: the UF8 Lanes mode overlays the Lanes bank on the top keys.
- `036b3ba` Centre tap = play the comp lane, Shift + centre = Comp here (needed once),
  UF8 ENC PUSH in Lanes = A/B. UF8 ENC PUSH has no long press in any mode. Bindings v55.

## Decisions (Frank)
Cursor after the audition: stroke end. Post-roll: yes. Loop jump: close, open nothing.
Centre lamp: live comping. No "Lanes Live" mode for now. SHIFT + jog: keep, one undo
per turn. Bank: no long press, lane track not focused track, comp green.

## Open
- D strips + F.3 SEL as cut keys, takes in items, UF8 value line with lane names.
- Unmeasured: hold edges on the UF8 per-mode centre, CSurf_ScrubAmt while painting,
  42475 during live comping with comping off, undo block merging 42707/42708,
  I_FREEMODE 2 on a track with items.
