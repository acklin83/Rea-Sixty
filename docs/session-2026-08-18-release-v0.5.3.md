# Session, 2026-08-18 evening: the manual caught up, then v0.5.3 shipped

Follows `session-2026-08-18-audit-favourites-navcross.md`, which built the
soft-key modifier sets and the per-mode nav cross and verified none of it.

## The manual, against the code rather than against itself

Frank confirmed the whole morning's priority list on the device, which also
settles the settings crash fix and makes the zoom cross in Playhead and Scrub a
decision rather than a regression. That freed the documentation work.

Four passages were wrong, and all four were found by reading the seed table and
the builtin registrations rather than the previous prose:

- The per-object cross table still described the collective behaviour: Playhead
  arrows moving the cursor by a grid step, the centre toggling a spotlight zoom.
  The factory cross in Playhead and Scrub is the plain zoom cross, the centre in
  Items and Razor is a Hold that drags content, and Shift is a binding of its own.
- The Native actions chapter had no Jog entry at all, so the six mode setters and
  the twenty-two Jog Actions were undocumented. Nothing told a reader that "Zoom
  to selection" exists but ships unbound, or that "hold to drag content" cannot
  work on Momentary.
- The Bindings pane's click-to-edit list did not know about the cross tiles or
  the jog wheel, and the Label bullet claimed every UF1 control has the field.
  Five keys have it, because five keys print a name.
- The UF1 secondary row's own chapter said it ships unbound and is yours, while
  the Plugin view table two chapters earlier documents key 1 as Unsolo all and
  key 2 as Fine. The seed settles it: **Shift on all six keys carries the six
  REAPER automation modes**, in the order the SSL silk reads.

That last one is why the UF1 needs no AUTO mode, which turned out to matter an
hour later.

## The UF1 modes plan, and why it shrank

Frank asked for a plan to put AUTO, FX Cycle, REC, NAV, Nudge and DynaMount on
the UF1. The plan is in memory (`uf1-modes-plan`) and is parked at his request.

The finding that made it smaller: on the UF8 a mode exists **because eight SEL
keys and eight V-Pots have to be reinterpreted**. There are no automation keys
there, so AUTO must hijack SEL. The UF1 has one channel, one SEL and a row of
keys with the right silk on them, so the same function is a binding. AUTO, FX
Cycle and Nudge already exist on the UF1 in exactly that shape; REC needs one new
builtin, because no per-track arm builtin exists anywhere in the project; only
NAV and DynaMount need a flag, because only they seize controls that already have
a job.

## v0.5.3 "Hold my Shift"

Frank first said to tag v0.5.2. That version has been out since 2026-08-14 with a
GitHub Release, a bumped ReaPack metapackage and a deployed website, so re-tagging
it would have shipped 87 commits under a version every user already has and
offered nobody an update. Raised once; he moved to v0.5.3. (He also chose the
patch number over the v0.6.0 this diff would justify. His call, and recorded.)

Shipped: tag, CI green on four jobs, both Macs notarised, Linux packed from the
CI container with the ABI floor check passing, a GitHub release with sixteen
assets, and a ReaPack index whose twelve download URLs were each checked for 200
rather than trusted from the action's green tick.

Two things the runbook did not know and now does: the manual carries two version
strings that nothing generates (the frontmatter said v0.5.1, the Versioning
chapter said v0.3.2), and the metapackage commit has to happen before the rebase,
not after. The website redeploy, which had lived only in memory across two
releases, is step 11 now, with the verify line that guards against publishing an
empty `/manual` and with the real route names.

reasixty.com is redeployed and checked live: eight routes 200, the manual reading
v0.5.3, thirty screenshots still in place, `website` pushed to `private` only.

## Open

Unchanged by this session: 360 Link's EQ Type on the device, the `0x010d` meter
burst, and the forum list, which stays locked without Frank's per-item go-ahead.
New: a reported bug where the UF8's other two Quick keys are dead after a fresh
Mac boot until an SSL strip is used. The code reading points at UF8 Plugin Mode,
which locks out all three Quick actions and is restored from persisted state at
startup, but the reporter's own account of the workaround does not fit that
cleanly, so it waits on one question to them.
