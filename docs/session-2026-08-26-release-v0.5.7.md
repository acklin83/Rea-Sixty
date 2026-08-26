# Session 2026-08-26 — v0.5.7 "Say my name."

22 commits, tagged and shipped the same day. Full user-facing list in
`dist/RELEASE-NOTES-v0.5.7.md`; this note carries the decisions and what is
still open.

## What the day actually was

One feature (a soft-key bank announces its name on the UF1's time display) and
a run of defects that turned out to share a shape: **something on the surface
knew a fact and did not say it.** An unnamed marker arrived as a blank strip
though its number was in the channel-number zone. The pager line named one of
two keys. SCRUB looked bindable and silently swallowed actions. The Envelope
centre LED was lit in both halves of its toggle. The panel had no readout for
the UF1's two mode rings.

## Decisions worth keeping

**The time display took four attempts, and the first three were one mistake.**
Frank asked for "exactly the same value as REAPER". I built a *coupling* where
he wanted a *reading*, then spent three rounds looking for the right thing to
couple to: h:m:s:f (which decomposes a negative position with floor, so a
cursor 26 ms before a negative project start read `-1:59:59:29`), then
`format_timestr_pos(-1)` (the ruler, not the transport), then `projtimemode2`
(the transport, whose combined unit hands over two readings for ten cells).
The answer was independence, which he had said at the start. The question to
ask at the front of a feature like this is not "which variable holds REAPER's
format" but "should this move when you change REAPER".

**The preview belongs on the device.** The bank-name field first drew ten
seven-segment cells in ImGui. Frank: "ich hätte die vorschau lieber direkt auf
dem gerät." The UF1 now paints the name while the field is active. The drawn
cells stay for the one case the panel cannot cover: no UF1 attached. Rule for
next time: when the real thing is in the room, a drawing of it is a second
opinion rather than a preview.

**Announce on difference, not on gesture.** The bank flash was first limited to
a change of bank number, on the grounds that SHIFT is held constantly for other
things and blinking a word over the clock on every press would be a nuisance.
Frank wanted SHIFT to count. The fix was not to restrict the trigger but to
compare the *announcement*: a Shift set with no bank of its own takes Plain's,
so its name is Plain's name and nothing flashes. The noisy case falls away on
its own.

**A mark that contradicts the binding it lights is an OR, not a second
opinion.** The nav cross lights a key either from the mode's own selection or
from the bound action's state. In Envelope those two were inverses, so between
them the centre key was lit always. Dropped the mark rather than flipping it,
so the key still reads correctly when bound to something else.

**Release notes are verified against the code, not the commit range.** A user
read in the v0.5.3 notes that a soft-key slot takes "all four modifier slots".
That came from a commit title; it was cut to two the same day, and two
paragraphs further down those same notes said "There is no Cmd or Ctrl set". A
commit range records what was tried, not what shipped. The step is now in
`release-process.md` step 2, and this release's notes were read whole before
committing, which caught one imprecision in the opening paragraph.

## An ImGui trap worth remembering

An active `InputText` keeps its own copy of the text and writes it back into the
caller's buffer on the next frame, reported as an edit. Re-seeding that buffer
from underneath while the cursor is in it therefore loses: ImGui wins and the
write looks like the user typed it. A bank name walked from bank to bank as the
UF1 paged. **Fix: put the object in the widget id.** A `!editing` guard around
the re-seed does not work, because the switch frame is exactly the one that must
re-seed and still has the cursor in the field.

Walking all nine `InputText` call sites found two more with the same shape: FX
Learn's Display label (seeded per control *and* per edit layer, and the layer
follows a held modifier, so it changes with no click to deactivate the field)
and the UF1 cell's Display name (seeded per position, and the position carries
the page, which the UF1's own arrows move).

## Open

- **Scrub is not in the Cmd time-selection pull.** It moves the cursor through
  `CSurf_ScrubAmt`, and whether `GetCursorPosition()` tracks that is not
  something the code says. Measure at the device before adding it.
- **Sliders bound to the selected object** have the same mid-gesture hazard as
  the text fields did: while dragging, the value belongs to ImGui, and a
  hardware-driven object switch would apply the drag to the new object. Needs
  the mouse held *and* a switch from the device, so it was left alone.
- **The `SCRUB` tile is locked, so it has no LED colour editor any more.** Its
  LED rode the binding table and is dark without one. Frank: don't care.
- **`dist/release-win.ps1` still argues about code signing on SmartScreen
  grounds only**, and says nothing about antivirus heuristics, which is what a
  user actually hit (Defender, `Trojan:Win32/Sabsik.TE.A!ml`). Both shipped
  DLLs are unsigned, verified by parsing the PE certificate table. Not changed,
  because the decision is Frank's.
- **The v0.5.3 correction is in the repo only.** The GitHub release page for
  v0.5.3 and the website changelog still carry the old sentence; both are
  separate copies of that file.
