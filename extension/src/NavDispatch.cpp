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
        if (inRegions) GoToRegion(nullptr, jumpIdx, false);
        else            SetEditCurPos(jumpPos, true, true);
        ov.clearCursorPin();
        markDirty();
    };
    auto doDrill = [&]() {
        if (lock != ViewLock::None) return;
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

    // Rebuild the UC1 filtered list exactly as pushUc1NavCarousel did.
    // We re-enumerate here (rather than caching) so the push is always
    // dispatched against a fresh REAPER snapshot — the carousel + push
    // share the same source of truth on every press.
    std::vector<Item> items;
    if (uc1Mode == 1) {
        Overlay::enumerateFiltered(View::Regions, -1, &items);
    } else {
        int scopedRegionIdx = -1;
        if (ov.view() == View::Regions) {
            const auto& uf8Items = ov.items();
            const int uf8Ci      = ov.cursorIdx();
            if (uf8Ci >= 0
                && uf8Ci < static_cast<int>(uf8Items.size())
                && uf8Items[uf8Ci].isRegion)
            {
                scopedRegionIdx = uf8Items[uf8Ci].idx;
            }
        }
        if (scopedRegionIdx >= 0) {
            Overlay::enumerateFiltered(View::MarkersInRegion,
                                       scopedRegionIdx, &items);
        } else {
            Overlay::enumerateFiltered(View::MarkersAll, -1, &items);
        }
    }

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
        if (isRgn) GoToRegion(nullptr, jumpIdx, false);
        else        SetEditCurPos(jumpPos, true, true);
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
    case 4: // Toggle View: flip Regions ↔ Markers within UC1 scope
        reasixty_setNavUc1Mode(uc1Mode == 1 ? 2 : 1);
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

} // namespace uf8::nav
