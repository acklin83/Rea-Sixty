# Session 30.09.2026: Folder collapse, Folder Mode memory, VCA spill

## MCP folder collapse (`9b167b5`)
- Trigger: YouTube comment, surface did not follow a folder collapsed in the Mixer.
- Probe `rea_sixty_ordner_sonde.lua` on REAPER 7.81: `IsTrackVisible(tr, true)` ignores
  a Mixer collapse, `IsTrackVisible(tr, false)` keeps TCP "small" children visible.
- Mixer collapse lives only in the folder's chunk (`BUSCOMP` field 2). Read for folder
  parents when a signature changes (`I_MCPW` zero-ness is the trigger).
- Frank: only hidden is hidden. TCP stays as it was.

## Folder Mode memory
- Folder Mode off no longer forgets the spilled folders.
- Saved per project tab: projectconfig `FOLDERSPILL "guid\tguid"`, `ScopedProjConfig` +
  `ProjectScoped`, not in undo states. Pointer cache re-resolved on load, tab switch,
  and a changed track count.
- Pin head for spilled folders was considered and dropped (Frank: scrolling out of a
  folder would leave the parent stuck on the left).

## VCA
- Plan and research: `.local-docs/vca-spill-plan.md`.
- `VcaSpill.h` (+ ctest `vca_spill`): members, top leads, chain moves.
- Long SEL on a lead spills: chain pinned left (regardless of "Pinned tracks survive
  banking"), followers after it, other filters rest. Deepest lead = back one level,
  outermost = leave, middle = jump. Bank restored on leave, spill ends on tab switch.
- `vca_mode`, `vca_spill_selected`, `vca_spill_exit`. Setting "VCA spill shows hidden
  tracks" (default on).
- Value line "VCA Lead", spilled chain "VCA Lead ... Spill" (label half is 8 chars).
- 128 groups: `trackGroups_` / `trackGroupWord_`. Words 0/1 via the old calls, 2/3 via
  `GetSetTrackGroupMembershipEx` with BIT offsets 64/96 (ReaTeam az_ scripts; one other
  project uses word indices). Selection-Set group slots and razor media-edit grouping
  use it too.

## Open
- Offset semantics of `GetSetTrackGroupMembershipEx` for groups 65..128 not measured.
