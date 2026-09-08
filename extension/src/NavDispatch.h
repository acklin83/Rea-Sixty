// Phase 2.8c — Nav Mode push-gesture action dispatch.
//
// One free function consumed by both UC1 Encoder 2 push and UF8 Channel
// Encoder push handlers. Resolves the action enum against current
// overlay state and performs the underlying REAPER action (jump, drill,
// back, view-toggle, add-marker).
//
// Action enum (also used by Settings UI):
//   0 Jump + Drill   1 Jump only     2 Drill only
//   3 Back           4 Toggle View   5 Add marker @ playhead
//   6 Disabled

#pragma once

#include <vector>

#include "MarkerOverlay.h"

namespace uf8::nav {

// Returns true if a meaningful action ran (false for no-op cases like
// Disabled, lock-suppression, empty cursor, etc.). Caller is responsible
// for any surface-specific bookkeeping (stats counters etc.).
bool dispatchPushAction(int actionEnum);

// UC1-specific variant — operates on the UC1's independent filtered
// list + g_navUc1Cursor when g_navUc1Mode != 0. In independent modes
// Drill / Back are no-ops, Jump+Drill degrades to Jump only, Toggle
// View flips g_navUc1Mode between Regions (1) and Markers (2).
// In Mirror mode (g_navUc1Mode == 0) this delegates directly to
// dispatchPushAction so legacy behaviour is preserved.
bool dispatchPushActionUc1(int actionEnum);

// The region the UF8's cursor is sitting on, or -1 when the UF8 is not
// showing regions or has no region under the cursor. `nameOut`, when
// given, receives that region's name. THE ONE PLACE that decides what
// "in region" is scoped to — it used to be inlined in three.
int uf8ScopedRegion(std::string* nameOut = nullptr);

// The list a following surface (UC1, UF1) shows for `mode`:
//   1 Regions           every region
//   2 Markers           every marker in the project, never scoped
//   3 Markers in region the markers inside uf8ScopedRegion(), and every
//                       marker when the UF8 is not on a region
// Mode 0 (Mirror) leaves `out` empty; that surface reads Overlay::items().
//
// ⚠ Modes 2 and 3 used to be one entry whose meaning depended on what the
// UF8 happened to be showing. Frank, 2026-09-08: make it a choice.
void buildFollowerList(int mode, std::vector<Item>& out);

// UF1 variant of the push dispatch, same contract as the UC1's: Mirror
// delegates, independent modes jump within buildFollowerList and make Drill /
// Back no-ops.
bool dispatchPushActionUf1(int actionEnum);

} // namespace uf8::nav
