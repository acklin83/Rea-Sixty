# Rea-Sixty Concepts & Terminology

The terms below are used throughout the codebase, the Settings UI, and
the action / builtin display names. Getting them right matters because
the V-Pot and the Channel Encoder both have "cycle" actions that look
similar but operate on **different sets** of plug-ins.

## FX vs. Instance

**FX** — any audio effect on a REAPER track. VST3, VST2, JS, AU, the
lot. If REAPER's `TrackFX_GetCount` sees it, it is an FX.

**Instance** — the subset of FX that Rea-Sixty surface-maps directly.
Specifically:

- SSL Channel Strip 2 (and the four 4K variants: 4K B / 4K E / 4K G)
- SSL Bus Compressor 2
- SSL 360° Link
- **Combo** plug-ins that combine the above (e.g. CS 2 + BC 2 in one
  rack)
- User-mapped UF8-only plug-ins learned via FX Learn (`uf8Mode` set)

Equivalent shorthand: **an Instance is anything that gets mapped onto
the controller surface.** Plain ReaEQ / FabFilter / any JS plug-in is
*an FX*, never *an Instance*, no matter how many parameters you bind
to it via FX Learn — unless you also mark it `uf8Mode` so all eight
UF8 strips become its controls.

Every Instance is an FX. Most FX are not Instances.

## Where the distinction shows up

| Action / display name              | Walks                       | Scope                           |
| ---                                | ---                         | ---                             |
| **Toggle V-POTS → FX Cycle**       | *All FX on the strip's track* | Per-strip; V-Pot rotates this strip's track's FX list, wraps |
| **Encoder → Instance Cycle**       | *Instances on the focused track* | Channel-encoder action; cycles only learned CS/BC/UF8 hits and refocuses UC1 onto them |
| **SSL Strip Mode**                 | The focused track's CS Instance | The fader maps to that Instance's Fader Level param |
| **UF8 Plugin Mode**                | The focused track's user-mapped Instance (`uf8Mode`) | All 8 strips drive parameters of one Instance |
| **Selection Mode**                 | Per-strip override of the V-Pot's role (Norm/Rec/REC+MON/Auto/FX-Cycle) | Global selection-mode state |

## Why the rename

Until 2026-05-14, the V-Pot per-strip cycle action was called
"V-POTS → Instance". That was misleading: it actually walks *every*
FX on the strip's track, not just learned Instances. The action was
renamed to **V-POTS → FX Cycle** to match what it actually does.

The internal builtin name (`selection_mode_instance`) and the enum
value (`SelectionMode::Instance`) are unchanged for binding-file
compatibility; only the user-visible display string moved.

The Channel-Encoder cycle (`encoder_instance` / `applyInstanceCycle_`)
*does* walk only Instances, so its display name remains "Encoder →
Instance Cycle".

## Reading code with these terms in mind

- `stripInstanceActiveFx_(tr)` and `g_stripInstanceFxIdx[tr]` — despite
  the name, this is the **FX index** for the V-Pot FX-Cycle. Naming is
  historical; treat it as "the strip's active FX in FX-Cycle mode".
- `applyInstanceCycle_` — true to its name: walks only Instances on the
  focused track. Updates `csInstanceIndex` / `bcInstanceIndex` /
  `uf8OnlyInstanceIndex` and **also** `g_stripInstanceFxIdx[focusedTrack]`
  so the focused strip's colour-bar FX-Cycle readout follows the
  encoder.
- `lookupPluginOnTrack(tr, domain)` — returns an Instance map (or
  null). Used to decide what variant label ("CS 2", "BC 1", "4K G",
  "Link") goes into the colour-bar Channel-Strip-Type zone.
