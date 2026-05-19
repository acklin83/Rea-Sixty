# Plan — Stable FX Identity (rename- & reorder-safe)

**Status:** draft 2026-05-19, awaiting Frank-OK.
**Successor to:** "Plugin reordering im FX-Chain" entry in
`plan-fx-learn-and-multi-instance.md:439` ("dokumentiert, kein Fix"),
which was based on the false assumption that REAPER FX have no GUIDs.

## Problem

Two unrelated brittlenesses, same root cause — we identify FX
slots by **position** and **current display name** rather than by
stable identity.

### A) Rename breakage
REAPER lets the user "Rename FX" in the FX chain (right-click →
Rename FX). Once done, `TrackFX_GetFXName(tr, fx, …)` returns the
*renamed* name. Today that name is the primary identity key for:

- `uf8::lookupPluginOnTrack` (`PluginMap.cpp:424, 457`) — feeds every
  CS/BC builtin path, every UC1 strip render, every UF8 strip render.
- `user_plugins::lookupByName / lookupOwnedByName` (`UserPluginCatalog.cpp`) —
  substring match for User-Maps.
- `broadcastUserParam` (`ParameterGroups.cpp:356, 366`) — substring
  match against `UserPluginMap::match` for fan-out targets.
- The `synthesizeUserBinding_` UC1 bridge in `main.cpp` (touches
  `uf8::lookupOwnedByName` indirectly).

Symptom: user renames "FabFilter Pro-Q 3" → "Vox Top". Group fan-out
stops finding the FX on the member tracks. Strip mode falls back to
default. No diagnostic — silently no-ops.

### B) Reorder breakage
`g_stripInstanceFxIdx` (`main.cpp:2129`) stores an **integer index**
per track-GUID for the V-Pot Strip FX-cycle position. The
BC/CS/UF8-only instance counters in `UC1PluginMap.cpp:617-619` store
**ordinal positions among matches** ("3rd CS on this track" = 2).

Symptom: user has Pro-Q at fx=0 and SSL CS at fx=1. Cursor sits on
SSL CS (idx 1). User drags Pro-Q below SSL CS. Cursor still says
"idx 1" — now Pro-Q. UC1 carousel + colour-bar follow the wrong FX.

For ordinal counters (BC/CS) the same happens when the user reorders
*among* matching instances of the same domain. Less common, still
real.

## Solution

Two thin abstractions, no data-model upheaval.

### 1) FX-identity name = `original_name`, not display name

`TrackFX_GetNamedConfigParm(tr, fx, "original_name", buf, sz)` returns
the original plug-in name regardless of any Rename FX. Already
documented in the FX-Learn plan (line 398) but only used there for
short-label fallback, not for identity.

Add helper in `PluginMap.cpp` (file-local, exposed via header):

```cpp
// Returns the original plug-in name — survives "Rename FX". Falls
// back to TrackFX_GetFXName when the API isn't available (older
// REAPER) or returns empty.
bool fxIdentityName(MediaTrack* tr, int fx, char* buf, size_t sz);
```

Switch identity-call-sites from `TrackFX_GetFXName` to
`fxIdentityName`:

- `PluginMap.cpp:424` and `:457` (the two `lookupPluginOnTrack`
  overloads).
- `ParameterGroups.cpp:356, 366` (`broadcastUserParam` member walk).
- Anywhere in `UserPluginCatalog.cpp` where `lookupByName` /
  `lookupOwnedByName` get fed a fresh name from `TrackFX_GetFXName`.
  (Audit pass — they take `string_view` so the call-site decides.)

**Keep** `TrackFX_GetFXName` at display-only sites:
- `fxCycleDisplayName_` (`main.cpp:3028, 3054`).
- UC1 carousel labels.
- Console log lines.
- The `displayShort` derivation in FX-cursor follow.
  …unless the user has set Rename FX *for display purposes* — in
  which case the renamed name is what they want to see. Display
  stays on `GetFXName`.

This is a one-line change at each identity site. No JSON migration,
no in-memory data change.

### 2) FX-cursor keyed by FX-GUID, not by integer index

`TrackFX_GetFXGUID(tr, fx)` returns a `GUID*` (header line 7291).
Stable across reorder; stable across project save/reload. **Not
stable across chunk replace or "Replace FX"** — call this out, fall
back to existing clamp-to-0 behaviour.

Two cursor storages to convert:

**(a) `g_stripInstanceFxIdx`** (`main.cpp:2129`) — currently
`unordered_map<trackGuid, int>`. Change to
`unordered_map<trackGuid, string fxGuid>`. Update the three accessors:

- `setStripInstanceFx_(tr, idx)` — resolve `idx → TrackFX_GetFXGUID
  → string`, store the string. Empty string allowed (means "no cursor").
- `stripInstanceFxRaw_(tr)` — load stored fxGuid string, walk
  `0..TrackFX_GetCount(tr)`, return the index where
  `TrackFX_GetFXGUID(tr, i)` matches. Returns -1 if not found (FX
  was deleted / chunk-replaced).
- `stripInstanceActiveFx_` — unchanged contract (clamp to 0 when
  raw is -1).

All ~12 call-sites of `setStripInstanceFx_` / `stripInstanceFxRaw_`
keep their integer arguments — the GUID translation is hidden
inside the helpers.

**(b) UC1 instance counters** (`UC1PluginMap.cpp:617-619` — `g_bcInstanceMap`,
`g_csInstanceMap`, `g_uf8OnlyInstanceMap`). Today these are "ordinal
position among same-domain matches". Two options:

- **Option A (minimal):** leave the maps as ordinals. On every
  read, walk the FX chain in domain order, build the
  `[domain-ordinal] → fxIdx → fxGuid` mapping; if the previously
  active ordinal still resolves to the *same fxGuid*, no change.
  If a reorder happened, find the new ordinal of the stored fxGuid
  and rewrite the map. This is cheap (FX counts are small) and
  preserves "active = nth instance" semantics across reorder.
- **Option B (parallel):** add `unordered_map<trackGuid, string fxGuid>`
  alongside ordinals; ordinals stay for fast first-time defaulting,
  GUID is the disambiguator after the user has interacted.

Go with **A** — fewer moving parts, ordinals stay the on-the-wire
contract. The "look up by FX-GUID after reorder, rewrite ordinal"
logic lives in one place: `cycleInstanceImpl_` (or whatever drains
the encoder action) on commit, plus a read-side fix-up in
`bcInstanceIdxFor_` / `csInstanceIdxFor_` so reads observe the
correction even if commit was missed.

### 3) Helper: `findFxIndexByGuid`

```cpp
// Walk the chain, return the first FX whose GUID-string matches.
// Returns -1 if not found. Cost is O(TrackFX_GetCount) — fine.
int findFxIndexByGuid(MediaTrack* tr, const std::string& fxGuidStr);
```

Lives in `PluginMap.cpp` next to `fxIdentityName`, exposed in
`PluginMap.h`.

Convert REAPER `GUID*` to canonical string with the same
`GetSetMediaTrackInfo_String` style helper already in
`UC1PluginMap.cpp:621` — extract `guidToString(GUID*)` as a
reusable helper:

```cpp
// Format a REAPER GUID* into a canonical "{XXXXXXXX-...}" string.
// Returns empty string if guid is null.
std::string guidToString(const GUID* g);
```

(REAPER SDK exports `guidToString` already in `wdltypes.h` /
`reaper_plugin_functions.h` — verify and use directly if available;
otherwise hand-format.)

## Out of scope (explicitly)

- **Plug-in family builtins** (`plugin_move_up/down`, etc.) — they
  operate on the current FX index imperatively. After move, REAPER
  itself moves the FX; our cursor will resolve correctly on the next
  read via the GUID. No change needed.
- **Selection Sets** — already track-GUID-keyed.
- **Parameter Groups membership** — `P_EXT` bitmask on the track.
  Track rename doesn't change track GUID; group membership survives.
- **PluginMap user-map persistence** (`user_plugins.json`) — keyed
  by `match` substring against the user-given pattern, not by the
  current FX name on any specific track. User changes the substring
  in the editor if they want.

## Verification matrix (hardware UAT)

After build + deploy, with debug log open:

1. **Rename FX:** Right-click SSL CS → Rename FX → "Test123".
   UF8 colour-bar still shows CS label. UC1 still drives the strip.
   V-Pot Strip mode still works. Parameter Group fan-out still
   broadcasts to this track when it's a member.

2. **Reorder within domain:** Track with two SSL CS instances.
   Cycle V-Pot Strip cursor to instance 2 (UC1 carousel confirms).
   Drag the second CS above the first in the FX chain. UC1
   carousel should still show the *same* CS instance (same
   `original_name` + same internal state), not jump to the other.

3. **Reorder across plug-ins:** Pro-Q at fx=0, SSL CS at fx=1.
   Strip cursor on SSL CS. Drag Pro-Q below CS. Cursor still on
   SSL CS (now at fx=0).

4. **Delete:** Cursor on instance N. Delete that FX. Cursor
   resolves to -1 → clamp-to-0 → next read shows whatever's at
   fx=0. Same as today.

5. **Chunk-replace edge case:** Right-click → Save FX chain →
   Load FX chain (replaces). FX-GUIDs change (likely). Cursor
   resolves to -1 → clamp-to-0. Acceptable degradation; log a
   line for diagnostics.

6. **Save/reload project:** Cursor position survives REAPER
   restart. (Already the case for track-GUID keying; verify the
   fx-GUID round-trips too.)

## Commit shape

Aim for 3 small commits, each independently testable:

1. **`fxIdentityName` + identity-site swap.** Adds the helper,
   converts `lookupPluginOnTrack` (both overloads) and
   `broadcastUserParam`. Behaviour change: rename now survives.
   Reorder still broken.

2. **`guidToString` + `findFxIndexByGuid` + `g_stripInstanceFxIdx`
   GUID conversion.** UF8 V-Pot Strip cursor survives reorder.

3. **UC1 instance counter GUID-disambiguation.** UC1 carousel +
   BC anchor track survive intra-domain reorder.

## Risks

- **`TrackFX_GetNamedConfigParm("original_name", …)` API age** —
  ships in REAPER 6.43+ (mid-2022). Frank's on current REAPER; no
  back-compat concern, but `fxIdentityName` should fall back to
  `GetFXName` defensively so a load on a hypothetical older REAPER
  doesn't crash, just regresses to today's behaviour.

- **FX-GUID stability across "Replace FX"** — unverified. If
  Replace creates a new GUID, the cursor falls back to 0 — same
  as deleting + adding, acceptable.

- **Container FX (REAPER 7.06+ subcontainers, `0x2000000` index
  bit)** — none of our paths touch container indexing today; the
  helpers should treat container-flagged indices the same as
  flat indices (the GUID API handles them transparently per the
  SDK comment on line 7289). No special-case needed in v1.

- **Performance** — `findFxIndexByGuid` is O(n) with n = FX count
  on the track. Called per-touch / per-encoder-tick, but n is
  small (typical track has <20 FX). No cache needed in v1; if a
  hot path shows up in profiling, cache `<trackGuid, lastIdx>`
  with a generation-counter invalidation.

## References

- SDK: `extension/vendor/reaper-sdk/sdk/reaper_plugin_functions.h:7286-7292`
  (`TrackFX_GetFXGUID`), `:7294+` (`TrackFX_GetFXName`).
- Existing GUID-string pattern: `UC1PluginMap.cpp:621-628` (`trackGuid_`).
- Existing `original_name` use: `plan-fx-learn-and-multi-instance.md:398`.
- Cursor storage today: `main.cpp:2125-2165`.
- Instance counters today: `UC1PluginMap.cpp:613-619`.
- Identity lookup today: `PluginMap.cpp:416-466`.
- User-map substring match today: `ParameterGroups.cpp:353-369`.
