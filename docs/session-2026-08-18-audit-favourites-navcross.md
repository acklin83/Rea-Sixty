# 2026-08-18 — the audit, the Favourites bank, and the nav cross

Branch `main`, 22 commits, all pushed, nothing verified on hardware. Config went
from v25 to **v29** and the per-layer bindings file from v1 to **v2** along the
way. Each commit carries its own reasoning; this note covers what connects them
and what to be careful with next.

## The audit, finished

Four agents had produced 25 findings the day before; twelve were fixed that
morning and one turned out to be a misdiagnosis I had built anyway (`bde8286`).
`f0c555e` closes the remaining twelve plus the small print.

Frank's decision on the one that needed it: on a dynamic bank the modifiers
belong to the five FX-key gestures, not to a second set. The editor had been
saying so since `64932f6`; only the runtime disagreed, because it read the bank's
KIND from the held modifier and therefore answered "not dynamic" while Shift was
down. `kDynamicKindSet` now means every runtime path — dispatch and render alike
— reads it from Plain, and the editor writes it there too.

Four roots explained half the findings, and they are worth remembering because
each one is easy to walk into again:

- **A soft-key lives in no layer map.** It is in one of the two soft-key stores.
  Layer export, layer reset, the retire migrations and the reverse lookup had all
  walked `layers[L].bindings` only. `forEachActionSlot_` walks everything now.
- **A modifier set is a full bank**, so it owns its own colour as well. That was
  the last shared field; it needed a second storage dimension on the Sub-Bank
  cells (v27) and an `isSet` flag, because white/bright is a legitimate colour and
  cannot otherwise be told from "never touched".
- **"Empty" means something different when writing than when dispatching.**
  There are now three predicates and the file says so in capitals.
- **A recall on Plain must never assign the whole Binding**, or it takes the
  other set down with it.

## What Frank found while testing

Six of the day's commits came straight out of his live testing, and two of them
were mine from the same afternoon:

- The blue "selected" fill could sit on one Sub-Bank cell while the green
  "engaged" ring sat on another — and the cell editor took its sub-bank from the
  selection and its Quick from the engaged bank, so it edited a bank the surface
  was not on.
- In Plugin Mode the editor could not reach any bank, because the Quick/Bank
  builtins bail out there on purpose and the radios had become dispatches.
- **Bank ◄ ► stopped being the fader bank in Plugin Mode.** Not a regression from
  that day: the dynamic-bank paging intercept has sat in front of the dispatch
  since `3a1e865` (2026-06-27) and bites as soon as `dyn_bank_ctrl_1=4` is set.
  The older, narrower claim wins now.
- `switch_bc_8` on the LCD. A slot with an action but no name fell back to the
  raw builtin id, visible only on a modifier set because Plain quietly borrows the
  key's own name.
- "Sticky • Off" on every project open, from two independent causes at once: the
  project-load callbacks force the default on and then restore off, and the
  companion treated whatever was already published as fresh.

## CS / BC Favourites as a dynamic bank

The favourite banks were factory presets with the painter special-casing
`switch_cs_N` to resolve a plug-in name at draw time — a dynamic bank built by
hand, one per class, chosen in advance. `DynamicBankKind::Favourites` makes it
one bank that asks `favDomainIsBc_()`, the same resolver the actions use, so it
follows whichever class you last worked on.

Frank's correction during that conversation is the useful part: a favourite
**replaces**, it never inserts, so `switchCsTo_` bailed out on a bare track and
every favourite action was a silent no-op there. That is fixed for all of them,
not just the bank — the favourite goes in at the end of the chain on its own
settings, since there is no old strip to carry anything from.

## The nav cross, per Jog Mode

Seven commits. The design had been locked since 2026-08-06; three things changed
in the building:

1. **22 new builtins, not zero.** The plan said the existing `jog_nav_*` were
   good defaults, which is true for the seed but misses the point: they are
   collective actions carrying an axis and a direction, with the meaning living
   in the drain. Binding per mode means every meaning needs a name.
2. **Shift is a binding.** The selecting actions used to ask
   `modifierHeld(Shift)` themselves. Now each has an add-to-selection twin
   seeded on the key's Shift slot — the same mistake one level down, undone.
3. **The centre is one builtin.** Grabbing a content drag necessarily means the
   target is "whole", so it is one gesture with a side effect, not two bindings
   on one key. Frank's question ("wie willst du 2 belegungen auf einer taste
   darstellen?") is what settled it.

The migration matters more than usual here: since dispatch resolves the cross
through the active mode, a config without the 25 ids has **no binding to find**,
not a fallback. Hence v28 and the backfill running for anything below it.

## Careful next time

- **learnings #31 bit again.** The jog-mode picker sat above the UF1 editor for
  every selection and pushed its two 480 px `BeginChild` columns down a row;
  Frank got a settings crash on the SCRUB tile. A culled child makes ReaImGui
  reap the whole Settings window a defer cycle later. The picker is conditional
  now (`91dc31b`) — **not confirmed on the device**, and a static audit cannot
  confirm it; last time it took an on-device trace.
- **Config diffs are readable now** (`4187884`): bindings serialise in `kNames`
  order instead of `unordered_map` order. The first diff after updating is still
  large, because that is the reordering itself.
- **Settings text**: eight explanatory paragraphs went in and came back out
  (`8a15211`) against a rule that was already marked repeat-violated. The rule
  now carries a mechanical check to run before committing anything that touches
  `SettingsScreen.cpp`.

## Open

Nothing here is verified on hardware. The priority list is in the memory handoff;
the short version is: config survives a restart (again — v29 landed after the
last check), the four data-loss fixes one by one, favourites on a track without a
strip, then the cross through all five modes. Beyond that, 360 Link's EQ Type is
still the one device check left from the 17th, and the forum list stays locked
without Frank's per-item go-ahead.
