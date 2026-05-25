#include "NavDispatch.h"

#include "MarkerOverlay.h"
#include "reaper_plugin_functions.h"

extern "C" void reasixty_markNavOverlayDirty();

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

} // namespace uf8::nav
