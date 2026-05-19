// MarkerOverlay — Phase 2.8 Nav Mode data layer.
//
// Drives the 8 UF8 scribble strips as a live marker/region jump panel
// when active. Pure state + REAPER queries here; surface integration
// lives in main.cpp and Bindings.cpp.
//
// ROADMAP.md Phase 2.8a defines the spec. This file owns:
//   - View state (Regions / MarkersInRegion / MarkersAll)
//   - 2-level drill: regions, then markers geometrically inside one region
//   - Cursor + page-offset + auto-follow flags
//   - Project-change invalidation so stale indices don't survive a tab
//     switch or project reload
//
// No allocations on the audio thread — enumerate() is main-thread only.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class MediaTrack;

namespace uf8::nav {

enum class View : uint8_t {
    Regions = 0,
    MarkersInRegion = 1,
    MarkersAll = 2,
};

// One row in the current view's flat list. `idx` is the REAPER
// EnumProjectMarkers3 index (stable per enum pass). `isRegion` distinguishes
// the API call needed at jump time (GoToRegion vs SetEditCurPos).
struct Item {
    int         idx        = 0;     // REAPER marker/region number
    int         enumPos    = 0;     // 0-based EnumProjectMarkers3 index
    bool        isRegion   = false;
    double      pos        = 0.0;   // seconds
    double      rgnEnd     = 0.0;   // seconds (regions only; 0 for markers)
    int         color      = 0;     // REAPER native colour (0 = no override)
    std::string name;
};

// Singleton — one overlay state per REAPER instance.
class Overlay {
public:
    static Overlay& instance();

    bool active() const { return active_.load(); }
    void setActive(bool on);
    void toggle();

    View view() const { return view_; }
    void setView(View v);

    int  filterRegionIdx() const { return filterRegionIdx_; }
    int  pageOffset()      const { return pageOffset_;      }
    int  cursorIdx()       const { return cursorIdx_;       }
    bool autoFollow()      const { return autoFollow_;      }
    void setAutoFollow(bool on)  { autoFollow_ = on;        }

    // Rebuild current-view item list. Idempotent; safe to call every
    // frame. Detects project changes and resets cursor/page when the
    // marker layout has shifted under us.
    void enumerate();

    // Items in the current view, in display order.
    const std::vector<Item>& items() const { return items_; }

    // The 8-window for the strips (may be shorter than 8 near list end).
    void window(Item const** out, int& outCount) const;

    // Paging.
    int  pageCount() const;
    void pageNext();
    void pagePrev();

    // Drill helpers.
    void drillIntoRegion(int enumPos);   // switch to MarkersInRegion for items_[enumPos]
    void backToRegions();

    // Diagnostic — prints current 8-window to REAPER console.
    void dumpWindow() const;

private:
    Overlay() = default;

    std::atomic<bool> active_{false};
    View              view_         = View::Regions;
    int               filterRegionIdx_ = -1;   // REAPER region idx (not enumPos)
    int               pageOffset_   = 0;
    int               cursorIdx_    = 0;       // within items_
    bool              autoFollow_   = false;

    std::vector<Item> items_;

    // Project-change detection. If either changes between enumerate()
    // calls, cursor/page reset and view falls back to Regions.
    void*             lastProj_     = nullptr;
    int               lastMarkers_  = -1;
    int               lastRegions_  = -1;
};

} // namespace uf8::nav
