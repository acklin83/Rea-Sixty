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

} // namespace uf8::nav
