# Rea-Sixty v0.6.4 (draft, not released)

Codename and date come with the release. Everything below is on main after v0.6.3.

## VCA spill and VCA Mode

**Long-press `SEL` on a VCA lead in VCA Mode and its followers take the surface.** The lead sits on strip 1 and stays there, its followers follow in track order and bank as usual. While the spill is on, nothing else is on the surface: Folder Mode, Selection Sets, Show Only Selected and the AUTO filter rest until it ends.

- A follower that is itself a VCA lead goes one level deeper on long-press `SEL`; the leads line up on the left, outermost first. Long-press the deepest lead to go back one level, a lead in between to jump to its level, the outermost to leave.
- Leaving returns to the bank you were on. Switching project tabs, or the lead disappearing, ends the spill too.
- The value line reads **VCA Lead** on leads, with **Spill** beside it on the spilled ones. Turning the V-Pot shows the real value for three seconds, as on a Folder Mode parent.
- **VCA Mode** shows only the leads that follow no other VCA.
- *Settings, Behaviour, Tracks, VCA spill shows hidden tracks* (on by default): followers hidden in the TCP or the Mixer come along.
- New actions: **Toggle VCA Mode (top leads only)**, **VCA Spill (selected track)** (in any mode, for the UF1 and keyboards), **Leave VCA Spill**. The two spill actions light while a spill is on.

## Long-press SEL is a binding

**Settings, Bindings, `SEL` has a LONG PRESS column now, on the UF8 and the UF1.** Its factory action is **Spill (folder / VCA)**: in Folder Mode it opens or closes a folder, in VCA Mode it spills a VCA lead. On a UF8 `SEL` it acts on that strip's track, from any other key on the selected track. Bindings files from before get the default filled in; nothing changes until you rebind it.

- The double press moved into the SHORT column, next to the description of the built-in select.
- On the UF1 in REC and REC + MON, long-press `SEL` does nothing, as on the UF8. The double press still fires.

## Folders

- **Surface mirrors: MCP follows a folder collapsed in the Mixer.** Its children leave the surface. The Mixer shows the folder button after a right-click on an empty spot and *Clickable icon for folder tracks to show/hide children*. REAPER itself reports those children as visible, which is why the surface kept them before.
- In TCP mode, children of a folder collapsed to *hidden* leave the surface as before; children collapsed to *small* stay, because the TCP still draws them.
- **Folder Mode keeps its open folders.** Switching Folder Mode off and on again shows them as they were, and they are saved with the project, one set per project tab.

## Groups

- Selection Set group slots reach all 128 track groups of REAPER 7.23 and later (64 before).
- The UF1's razor area spreads across media-edit groups in all 128 groups too.

## REAPER actions

- *Rea-Sixty: Folder Mode (parents only) (toggle)*, *Rea-Sixty: VCA Mode (top leads only) (toggle)*, *Rea-Sixty: VCA spill (selected track)*, *Rea-Sixty: Leave VCA spill*. A toolbar button or key bound to one of them lights like the Rea-Sixty action of the same name.

## Fixes

- The mode banner names the UF1's RME side-car. With the UF1 set to start in RME it said "UF1 Mode • DAW" at REAPER start, and entering or leaving the side-car with SHIFT + MODE showed nothing.
- Settings text named a REAPER preference that does not exist ("Hide children of collapsed folders"). It now names the real one, *Folder collapse button cycles track heights*.

## Changelog (for ReaPack, without the two-space indent)

```
- VCA spill: long-press SEL on a VCA lead in VCA Mode; lead stays on strip 1, followers after it, nested VCAs level by level
- VCA Mode: only the top VCA leads on the surface
- Setting "VCA spill shows hidden tracks" (default on)
- Actions: VCA Mode, VCA Spill (selected track), Leave VCA Spill, also in REAPER's Action List
- Long-press SEL is a binding on UF8 and UF1, factory action "Spill (folder / VCA)"
- SEL editor: double press in the SHORT column
- UF1: long-press SEL does nothing in REC and REC + MON
- Surface mirrors MCP: a folder collapsed in the Mixer takes its children off the surface
- Folder Mode keeps its open folders, saved per project tab
- Selection Set group slots and UF1 razor grouping reach all 128 groups (REAPER 7.23+)
- REAPER action for Folder Mode
- Mode banner names the UF1 RME side-car
```
