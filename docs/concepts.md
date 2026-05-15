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

Three surface points × two cycle kinds = six bindable cycle actions.
Frank's symmetry (2026-05-15):

| Surface point          | Scope          | FX Cycle (all FX)                       | Instance Cycle (Instances only)            |
| ---                    | ---            | ---                                     | ---                                        |
| **V-Pot Sel-Mode**     | per-strip      | `selection_mode_instance` (display: "FX Cycle") | `selection_mode_instance_cycle` (NEW)   |
| **UF8 Channel Encoder**| focused track  | `encoder_fx_cycle` (NEW)                | `encoder_instance` ("Instance Cycle" mode) |
| **UC1 Encoder 2**      | focused track  | `fx_cycle` (bindable Plain/Shift)       | `instance_cycle` (bindable Plain/Shift)    |
| Any button             | focused track  | `fx_cycle`                              | `instance_cycle`, `instance_next/_prev`    |

Plus the hardware-mode anchors:

| Action / display name              | Walks                       | Scope                           |
| ---                                | ---                         | ---                             |
| **SSL Strip Mode**                 | The focused track's CS Instance | The fader maps to that Instance's Fader Level param |
| **UF8 Plugin Mode**                | The focused track's user-mapped Instance (`uf8Mode`) | All 8 strips drive parameters of one Instance |

## Cycle semantics — what changes when you cycle

**FX Cycle**: moves the per-track cursor `g_stripInstanceFxIdx[tr]`
through ALL FX on the track. Includes non-Instances (Tone Generator,
ReaEQ, etc.). If the landing FX happens to be a learned Instance,
`syncInstanceFromFxIdx_` promotes the move into a full Instance
selection — i.e. updates `csInstanceIndex` / `bcInstanceIndex` /
`uf8OnlyInstanceIndex` so hardware bindings (SSL Strip Mode, UF8
Plugin Mode, UC1 CS/BC encoder sections) react. On a non-Instance
landing the Instance indices stay put.

**Instance Cycle**: walks only the learned Instances on the track.
Always updates the matching Instance index and (for focused-track
callers, or focused-strip callers) shifts `focused.domain` to the
landed Instance's domain. The cursor also moves to the landed
Instance's FX index, so a follow-up FX Cycle picks up from there.

## Why the renames

Until 2026-05-14, the V-Pot per-strip cycle action was called
"V-POTS → Instance". That was misleading: it actually walks *every*
FX on the strip's track, not just learned Instances. The action's
display string is now **"Selection Mode → FX Cycle"** to match what
it does. Internal builtin name (`selection_mode_instance`) and enum
value (`SelectionMode::Instance`) are unchanged for binding-file
compatibility.

The 2026-05-15 symmetry pass added the real Instance-only V-Pot
cycle as `selection_mode_instance_cycle` (display: "Selection Mode →
Instance Cycle", enum `SelectionMode::InstanceCycle`), and an FX
counterpart for the UF8 Channel Encoder as `encoder_fx_cycle`
(display: "Encoder Mode → FX Cycle", enum `EncoderMode::FxCycle`).

## Reading code with these terms in mind

- `stripInstanceActiveFx_(tr)` and `g_stripInstanceFxIdx[tr]` — despite
  the name, this is the **FX-Cursor** (per-track FX index) that both
  cycle modes write to. Naming is historical; treat it as "the strip's
  current FX, whatever cycle put it there".
- `applyFxCycle_` — focused-track FX Cycle. Walks every FX on the
  focused track, updates the cursor, and calls `syncInstanceFromFxIdx_`
  to promote Instance landings.
- `applyInstanceCycle_` — focused-track Instance Cycle. Walks only
  Instances. Updates `csInstanceIndex` / `bcInstanceIndex` /
  `uf8OnlyInstanceIndex` and the cursor.
- `syncInstanceFromFxIdx_(tr, fxIdx, setFocusedDomain)` — shared
  helper. If `fxIdx` is a learned Instance on `tr`, syncs the matching
  Instance index. Used by FX Cycle (focused + per-strip) so cycling
  onto an Instance "activates" it.
- The V-Pot per-strip cycle dispatch lives in
  `PendingInput::StripInstanceDelta` (FX Cycle) and
  `PendingInput::StripInstanceCycleDelta` (Instance Cycle) inside
  `drainInputQueue`. Pushes share `StripInstanceOpen` — both modes
  toggle the same `g_instanceGuiOwnerStrip` ownership.
- `lookupPluginOnTrack(tr, domain)` — returns an Instance map (or
  null). Used to decide what variant label ("CS 2", "BC 1", "4K G",
  "Link") goes into the colour-bar Channel-Strip-Type zone.
