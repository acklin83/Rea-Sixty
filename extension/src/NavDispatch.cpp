#include "NavDispatch.h"

#include <vector>

#include "MarkerOverlay.h"
#include "reaper_plugin_functions.h"

extern "C" void reasixty_markNavOverlayDirty();
// Per-surface Nav state (defined in main.cpp).
int  reasixty_navUc1Mode();
void reasixty_setNavUc1Mode(int v);
extern "C" int reasixty_navUc1CursorGet();
extern "C" void reasixty_navUc1CursorSet(int v);
extern "C" int  reasixty_navUf1Mode();
void reasixty_setNavUf1Mode(int v);
extern "C" int  reasixty_navUf1CursorGet();
extern "C" void reasixty_navUf1CursorSet(int v);
extern "C" int  reasixty_navUc1MarkersFlavour();
extern "C" int  reasixty_navUf1MarkersFlavour();

namespace uf8::nav {

bool dispatchPushAction(int act)
{
    auto& ov = Overlay::instance();
    if (!ov.active()) return false;

    const auto& items = ov.items();
    const int ci = ov.cursorIdx();
    if (ci < 0 || ci >= static_cast<int>(items.size())) {
        // Toggle View / Add marker still make sense without a valid
        // cursor; the other actions need a target item.
        if (act != 4 && act != 5) return false;
    }

    // Snapshot the cursor item's fields by value. Drill / Back can
    // re-run enumerate() which reuses items_'s storage — a captured
    // reference would dangle.
    int    jumpIdx = -1;
    double jumpPos = 0.0;
    if (ci >= 0 && ci < static_cast<int>(items.size())) {
        jumpIdx = items[ci].idx;
        jumpPos = items[ci].pos;
    }
    const auto lock      = ov.viewLock();
    const auto curView   = ov.view();
    const bool inRegions = (curView == View::Regions);

    auto markDirty = []{ reasixty_markNavOverlayDirty(); };

    auto doJump = [&]() {
        if (jumpIdx < 0) return;
        // ⛔ GoToRegion ONLY SMOOTH-SEEKS DURING PLAYBACK. Stopped it queues
        // "seek at the end of the current region" and nothing happens, so the
        // push looked dead. Measured on 2026-07-27 with a cursor before/after
        // trace: SetEditCurPos moves and sticks, GoToRegion stopped does not.
        // The soft-key path and the UC1 path were fixed then; THIS one, the
        // shared push, kept the old line and was still broken on 2026-09-08.
        // Four jump sites, three fixed, and the one left over is the one Frank
        // was pressing.
        if (inRegions && (GetPlayState() & 1)) GoToRegion(nullptr, jumpIdx, false);
        else                                   SetEditCurPos(jumpPos, true, true);
        ov.clearCursorPin();
        markDirty();
    };
    auto doDrill = [&]() {
        if (lock != ViewLock::None) return;
        if (couplingHoldsDrill()) return;
        if (!inRegions) return;
        if (ci < 0) return;
        ov.drillIntoRegion(ci);
        markDirty();
    };

    switch (act) {
    case 0: // Jump + Drill
        doJump();
        doDrill();
        return true;
    case 1: // Jump only
        doJump();
        return true;
    case 2: // Drill only
        doDrill();
        return true;
    case 3: // Back
        if (!inRegions) {
            ov.backToRegions();
            markDirty();
            return true;
        }
        return false;
    case 4: // Toggle View (Regions <-> MarkersAll)
        ov.setView(inRegions ? View::MarkersAll : View::Regions);
        markDirty();
        return true;
    case 5: { // Add marker at playhead / edit cursor
        const int    ps  = GetPlayState();
        const double pos = (ps & 1) ? GetPlayPosition() : GetCursorPosition();
        AddProjectMarker(nullptr, false, pos, 0.0, "", -1);
        markDirty();
        return true;
    }
    case 6: // Disabled
    default:
        return false;
    }
}

bool dispatchPushActionUc1(int act)
{
    const int uc1Mode = reasixty_navUc1Mode();
    if (uc1Mode == 0) {
        // Mirror UF8 — legacy path.
        return dispatchPushAction(act);
    }

    auto& ov = Overlay::instance();
    if (!ov.active()) return false;

    // Re-enumerated on every press rather than cached, so the carousel and
    // the push always dispatch against the same fresh REAPER snapshot.
    std::vector<Item> items;
    buildFollowerList(uc1Mode, items);

    int ci = reasixty_navUc1CursorGet();
    const int last = static_cast<int>(items.size()) - 1;
    if (last < 0) ci = -1;
    else if (ci < 0) ci = 0;
    else if (ci > last) ci = last;

    int    jumpIdx = -1;
    double jumpPos = 0.0;
    bool   isRgn   = false;
    if (ci >= 0) {
        jumpIdx = items[ci].idx;
        jumpPos = items[ci].pos;
        isRgn   = items[ci].isRegion;
    }

    auto markDirty = []{ reasixty_markNavOverlayDirty(); };
    auto doJump = [&]() {
        if (jumpIdx < 0) return;
        // GoToRegion only smooth-seeks DURING PLAYBACK (queues "seek at end of
        // current region"); with the transport STOPPED it does nothing, so the
        // UC1 nav push looked dead (Frank 2026-07-27, VERIFIED via a cursor
        // before/after trace: SetEditCurPos moves + sticks, GoToRegion stopped
        // did not). Move the edit cursor to the region start when stopped so the
        // jump is always visible; keep GoToRegion while playing for the
        // smooth-seek-at-region-end behaviour. Markers always use SetEditCurPos.
        if (isRgn) {
            if (GetPlayState() & 1) GoToRegion(nullptr, jumpIdx, false);
            else                    SetEditCurPos(jumpPos, true, true);
        } else {
            SetEditCurPos(jumpPos, true, true);
        }
        markDirty();
    };

    switch (act) {
    case 0: // Jump + Drill → Jump only (drill is implicit via coupling)
    case 1: // Jump only
        doJump();
        return true;
    case 2: // Drill only — no-op in independent mode
    case 3: // Back — no-op in independent mode
        return false;
    case 4: // Toggle View: flip Regions ↔ the markers flavour the user picked,
            // so a round trip does not silently turn "Markers in region" into
            // plain "Markers" behind their back.
        reasixty_setNavUc1Mode(uc1Mode == 1 ? reasixty_navUc1MarkersFlavour() : 1);
        markDirty();
        return true;
    case 5: { // Add marker at playhead / edit cursor (project-global)
        const int    ps  = GetPlayState();
        const double pos = (ps & 1) ? GetPlayPosition() : GetCursorPosition();
        AddProjectMarker(nullptr, false, pos, 0.0, "", -1);
        markDirty();
        return true;
    }
    case 6: // Disabled
    default:
        return false;
    }
}

bool couplingHoldsDrill()
{
    return reasixty_navUc1Mode() == 3 || reasixty_navUf1Mode() == 3;
}

int uf8ScopedRegion(std::string* nameOut)
{
    if (nameOut) nameOut->clear();
    auto& ov = Overlay::instance();
    if (ov.view() != View::Regions) return -1;
    const auto& items = ov.items();
    const int ci = ov.cursorIdx();
    if (ci < 0 || ci >= static_cast<int>(items.size())) return -1;
    if (!items[ci].isRegion) return -1;
    if (nameOut) *nameOut = items[ci].name;
    return items[ci].idx;
}

void buildFollowerList(int mode, std::vector<Item>& out)
{
    out.clear();
    switch (mode) {
    case 1:
        Overlay::enumerateFiltered(View::Regions, -1, &out);
        break;
    case 2:
        Overlay::enumerateFiltered(View::MarkersAll, -1, &out);
        break;
    case 3: {
        const int rgn = uf8ScopedRegion();
        if (rgn >= 0) Overlay::enumerateFiltered(View::MarkersInRegion, rgn, &out);
        else          Overlay::enumerateFiltered(View::MarkersAll, -1, &out);
        break;
    }
    default:
        break;   // Mirror — the Overlay's own list, not ours to build
    }
}

bool dispatchPushActionUf1(int act)
{
    const int uf1Mode = reasixty_navUf1Mode();
    if (uf1Mode == 0) {
        // Mirror UF8 — the shared cursor, the shared rules.
        return dispatchPushAction(act);
    }

    auto& ov = Overlay::instance();
    if (!ov.active()) return false;

    std::vector<Item> items;
    buildFollowerList(uf1Mode, items);

    int ci = reasixty_navUf1CursorGet();
    const int last = static_cast<int>(items.size()) - 1;
    if (last < 0) ci = -1;
    else if (ci < 0) ci = 0;
    else if (ci > last) ci = last;

    int    jumpIdx = -1;
    double jumpPos = 0.0;
    bool   isRgn   = false;
    if (ci >= 0) {
        jumpIdx = items[ci].idx;
        jumpPos = items[ci].pos;
        isRgn   = items[ci].isRegion;
    }

    switch (act) {
    case 0: // Jump + Drill → Jump only: there is nothing to drill into when
            // the list is already filtered to one kind.
    case 1: // Jump only
        if (jumpIdx < 0) return false;
        // Same stopped-transport rule as everywhere else: GoToRegion only
        // smooth-seeks during playback, so move the edit cursor when stopped.
        if (isRgn && (GetPlayState() & 1)) GoToRegion(nullptr, jumpIdx, false);
        else                               SetEditCurPos(jumpPos, true, true);
        reasixty_markNavOverlayDirty();
        return true;
    case 2: // Drill only — no-op in independent mode
    case 3: // Back — no-op in independent mode
        return false;
    case 4: // Toggle View, same rule as the UC1's above
        reasixty_setNavUf1Mode(uf1Mode == 1 ? reasixty_navUf1MarkersFlavour() : 1);
        reasixty_markNavOverlayDirty();
        return true;
    case 5: { // Add marker at playhead / edit cursor (project-global)
        const int    ps  = GetPlayState();
        const double pos = (ps & 1) ? GetPlayPosition() : GetCursorPosition();
        AddProjectMarker(nullptr, false, pos, 0.0, "", -1);
        reasixty_markNavOverlayDirty();
        return true;
    }
    case 6: // Disabled
    default:
        return false;
    }
}

} // namespace uf8::nav
