# Session 2026-08-19 — REC + RME on the UF1, and five defects it flushed out

Commits: `0724a9e`, `9590d85`, `520619e`, `0ac9fb1` (Rea-Sixty) and `7810f42`
(TotalReaper v0.2.2). All hardware-verified by Frank during the session.

## What was asked

"Sollen wir mal die REC und RME mode für UF1 angucken? Ich glaub das können wir
ziemlich 1:1 vom UF8 übernehmen, oder?"

Answer: not from the UF8. The UF8 code is per-strip and resolves its track from
strip index plus bank offset, which a one-channel surface cannot use. The **UC1**
had already solved exactly this port, so that block was the template.

## The bug that had to go first

Planning the RME controls exposed that the UF1's left half was addressing the
wrong track in Extender mode. The panel is split: the fader side (fader, meter,
small LCD, SOLO, CUT, SEL, above-fader V-Pot, the soft-key above the channel)
belongs to the bank slot, the plug-in side (4 V-Pots, 4 display soft-keys, EQ
graph) follows the selection. The *painting* had been moved across on 2026-08-13,
the *actions* had not, so CUT lit for one track and muted another.

Fixed in `0724a9e`, plus MASTER now outranking the Extender for both halves.
The rule is now written down — see the `uf1-panel-halves-rule` memory — because
this was the fourth round in which one caller was left behind.

## REC and RME

REC is a real mode on the UF1, as on the UF8 (Frank: "Wir reden hier von einem
MODE!"): SEL arms the channel the fader side shows, and the SEL lamp goes red,
bright when armed. No new builtin — the existing `selection_mode_*` ones switch
it, and nothing is seeded into the user's secondary row.

RME drives the above-fader V-Pot (gain, Shift for input channel), CUT and SOLO
against that same channel, with five settings of its own under the UC1 block.
Three helpers that existed twice (readout, input-channel step, gate) are now one
implementation shared by all three surfaces.

**Both rotation toggles default ON here, unlike the UF8's and UC1's.** They were
off at first and Frank found the knob still panning: everything without a
checkbox worked, everything behind one did not. On a surface with exactly one
knob above the fader, in a mode you switch on deliberately, "pan anyway" is a
dead control rather than a preference.

## The five defects found while testing

1. **The small display's bar never worked, for anything.** The init replay
   latches `0x000d` (bar style) to `0x03` = off and nothing ever corrected it, so
   every position sent to `0x000f` drew nothing — not pan, not a Sticky Pot, not
   the gain. Style codes measured on the device: `01` pointer, `02` fill from
   left, `04` off, `08` fill from centre. Now owned, set to `01`.
2. **The frame trace latched at device open**, so only the input log followed the
   flag live and the OUT log needed a REAPER restart. Cost one round.
3. **Both settings tab restores were dead.** A fresh ImGui context per Settings
   open restarts the frame counter, and the re-arm test was `frame > last + 1`.
   A backwards jump is a gap too.
4. **Turning the gain knob flashed TRIM and the channel number on the UF8.** The
   selection swap that targets a TotalReaper action reports `SetSurfaceSelected`
   twice per detent, and the Sel branch fires `sendSelRenderTrigger`, whose cell
   `0x24` IS the AutoTrim lamp. The first fix guarded the timer and did nothing,
   because those frames go out straight from the callback.
5. **Pan felt sluggish on the UF8**: 1/128 per raw count against the UF1's 1/64.
   Same base law now, and pan renders as a single travelling line on both
   surfaces instead of filling from the middle.

## TotalReaper v0.2.2

Changing a track's input left the preamp readout describing the previous
channel, because TotalMix only reports a channel when something on it moves and
`/sendall` ran once, at enable. TotalReaper now watches `I_RECINPUT` itself
(Frank's call: "TotalReaper merkt den Wechsel selbst") and asks for a fresh dump
250 ms after a change, holding the two-way fader path off while it arrives.

Integrating TotalReaper into Rea-Sixty was considered and dropped: it is useful
on its own without an SSL controller, and a double install would put two OSC
clients on one port. Settings → Modes → REC now reports whether it was detected
and hands the user to ReaPack when it is missing; both packages ship from the
same repository, so that is one click.

## Open

* Nothing from this round. The LMF gain behaviour Frank mentioned is a separate
  path (plug-in params ride `kVpotBoost`, not the pan law) and was not touched.
