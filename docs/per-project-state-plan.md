# Selection Sets per project tab: plan (29.09.2026)

**Built 29.09.2026, option b (Frank: "b, bau"):** all seven hooks. `src/ProjectScoped.h`,
`ScopedProjConfig` and `switchProjectState_` in main.cpp, `tests/test_project_scoped.cpp`.
Threads read before building: the containers are main-thread only (the input thread
posts requests and reads atomics or mirrors the timer recomputes); Parameter Groups
swap through their own API under their mutex. ⛔ A NEW projectconfig hook goes through
`ScopedProjConfig` with its own store and a line in `switchProjectState_`.

Forum, timothys_monster: set saved in project A (project scope), new tab with empty
project B, back to A: the slot is empty, its name gone, the UF8 shows all tracks.
Frank: "Should be session aware, of course."

## Cause (read in main.cpp)

- Project-scoped slots live in ONE array, `g_selsets[8]`, for all open projects.
- The projectconfig hook `g_slotsProjConfig` fills it on load and writes it on save.
  Its begin-load (`slotsBeginLoad_`, main.cpp:3077) empties every project-scoped
  slot. The report shows that this also runs when the new tab B is created.
- On a tab switch, `drainSelsets_` (main.cpp:3416) calls `loadSelsetsFromProject_`,
  which for project-scoped slots only says "the hook already filled them"
  (main.cpp:3285). A tab switch loads nothing, so A's sets stay empty.
- **Worse, on disk:** `slotsSaveExt_` writes whatever is in `g_selsets`. Saving A
  after the switch writes no `SELSET_<N>_DATA` lines, and the sets are gone from
  the .rpp too. Until A is saved, the .rpp still holds them.
- The active slot `g_selsetActive` is one value for all tabs as well.

## The same mechanism in six more places

Every projectconfig hook keeps its state in globals shared by all tabs, and each
begin-load clears them (main.cpp:57875-57889):

| Hook | State | Cleared by a new tab |
|---|---|---|
| `g_slotsProjConfig` | Selection Set slots 1-8 (project scope) | yes, this report |
| `g_tempSelsetProjConfig` | Temporary Selection Set + its recall flag | `tempSelsetBeginLoad_` |
| `g_pgActiveProjConfig` | Parameter Group on/off + names | `pgActiveBeginLoad_` |
| `g_stickyProjConfig` | Sticky Pot pins + Sticky on/off | `stickyBeginLoad_` |
| `g_csFavMemProjConfig` | CS favourite value memory | `csFavMemBeginLoad_` |
| `g_bcFavMemProjConfig` | BC favourite value memory | `bcFavMemBeginLoad_` |
| `g_favBankProjConfig` | project favourite bank, per-track CS/BC set | `favBankBeginLoad_` |

Same symptom, same data loss when A is saved after a detour through another tab.

## Fix: one store per project tab

1. **`src/ProjectScoped.h`**, a small template: a map from `ReaProject*` to a state
   struct, plus the working copy that the existing code keeps reading
   (`g_selsets` and friends stay where they are, 27 readers of `g_selsets` alone).
   - `switchTo(cur)`: working copy goes to the map under the old project, the new
     project's entry becomes the working copy. Called on the main thread where
     `drainSelsets_` already notices the tab change.
   - `forLoadSave(p)`: the hook asks for the state of the project REAPER is
     loading or saving, from `GetCurrentProjectInLoadSave()` (REAPER SDK,
     reaper_plugin_functions.h:1570, "usually only used from
     project_config_extension_t"). The working copy if `p` is the active tab,
     otherwise its map entry.
   - `beginLoad(p)`: resets only `p`'s state.
   - `prune(valid)`: drops entries of closed tabs (`ValidatePtr2(.., "ReaProject*")`),
     so a new project that gets the address of a closed one starts empty.
2. **Selection Sets first** (this report): slots 1-8 and the temporary set in one
   state struct, the active slot too, so tab B does not inherit A's filter and A
   shows its set again when you come back. The active slot is remembered per tab
   while it is open, not written to the file (as today).
3. **Test `tests/test_project_scoped.cpp`**, the report as a test: A has a set, B is
   created (begin-load for B), switch back to A: set there; save A while B is
   active: A's lines; close B, a new project gets B's address: empty.
4. Global-scope slots stay as they are (ExtState, the same in every project).

## Decision for Frank

- **a) Only Selection Sets** (slots, temporary set, active slot).
- **b) All seven hooks** on the same helper. My recommendation: same mechanism,
  same data loss on save, and after a) the helper is already there.

## Not checked

- Whether REAPER calls a hook's begin-load for File, New project in the same tab
  as it does for a new tab. The report shows it for a new tab only.
- Whether `SaveExtensionConfig` also runs for undo points (`isUndo`); with the store
  per project it does no harm either way.
- Which thread reads `g_selsets`, `g_stickyPins` and the favourite maps; the swap
  has to happen where the readers are safe. I read that before building.

## Workaround for the user until then (from the code, not tried)

If project A was saved after the set was stored, close A without saving and open
it again: the set is still in the .rpp. Saving A after the detour loses it.
