// MarkerOverlay — Phase 2.8 Nav Mode data layer.
//
// See MarkerOverlay.h for the spec. This translation unit owns the
// singleton, the REAPER enumeration pass, and the project-change
// invalidation logic.

#include "MarkerOverlay.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "reaper_plugin_functions.h"

namespace uf8::nav {

namespace {
constexpr int kPageSize = 8;
}

Overlay& Overlay::instance()
{
    static Overlay s_overlay;
    return s_overlay;
}

void Overlay::setActive(bool on)
{
    // Input-thread safe: only flips the atomic flag. The main-thread
    // strip-render tick calls enumerate() lazily when active() so the
    // first post-toggle frame still gets a fresh marker list. Callers
    // on the main thread can also invoke enumerate() directly.
    active_.store(on);
}

void Overlay::toggle()
{
    setActive(!active_.load());
}

void Overlay::setView(View v)
{
    if (view_ == v) return;
    view_ = v;
    // Switching views invalidates the cursor — different items_ list.
    cursorIdx_ = 0;
    pageOffset_ = 0;
    enumerate();
}

void Overlay::enumerate()
{
    items_.clear();

    // Project-change detection. EnumProjectMarkers3(nullptr, ...) uses
    // the currently-focused project; the pointer changes when the user
    // switches project tabs or opens a new project. Marker/region count
    // changes also count as a layout shift even within the same project.
    void* proj = static_cast<void*>(EnumProjects(-1, nullptr, 0));
    int nmarkers = 0, nregions = 0;
    CountProjectMarkers(nullptr, &nmarkers, &nregions);
    const bool projChanged = (proj != lastProj_)
                          || (nmarkers != lastMarkers_)
                          || (nregions != lastRegions_);
    if (projChanged) {
        lastProj_     = proj;
        lastMarkers_  = nmarkers;
        lastRegions_  = nregions;
        // Cursor / page / drill state are all keyed by item indices in
        // the previous enumeration. Reset rather than try to remap —
        // a project change is rare and a clean reset is more honest
        // than guessing which region survived.
        cursorIdx_       = 0;
        pageOffset_      = 0;
        filterRegionIdx_ = -1;
        if (view_ == View::MarkersInRegion) {
            view_ = View::Regions;
        }
    }

    // Single pass through EnumProjectMarkers3 — REAPER returns markers
    // and regions interleaved in timeline order. We split them by view.
    const int total = nmarkers + nregions;

    // For MarkersInRegion we need the region's [start, end] window.
    double filterStart = 0.0, filterEnd = 0.0;
    bool   haveFilter  = false;
    if (view_ == View::MarkersInRegion && filterRegionIdx_ >= 0) {
        for (int i = 0; i < total; ++i) {
            bool isrgn = false;
            double pos = 0.0, rgnend = 0.0;
            const char* name = nullptr;
            int idx = 0, color = 0;
            if (!EnumProjectMarkers3(nullptr, i, &isrgn, &pos, &rgnend,
                                     &name, &idx, &color)) continue;
            if (isrgn && idx == filterRegionIdx_) {
                filterStart = pos;
                filterEnd   = rgnend;
                haveFilter  = true;
                break;
            }
        }
        // Region disappeared (deleted while drilled in) → fall back.
        if (!haveFilter) {
            view_ = View::Regions;
            filterRegionIdx_ = -1;
            cursorIdx_ = 0;
            pageOffset_ = 0;
        }
    }

    for (int i = 0; i < total; ++i) {
        bool isrgn = false;
        double pos = 0.0, rgnend = 0.0;
        const char* name = nullptr;
        int idx = 0, color = 0;
        if (!EnumProjectMarkers3(nullptr, i, &isrgn, &pos, &rgnend,
                                 &name, &idx, &color)) continue;

        switch (view_) {
        case View::Regions:
            if (!isrgn) continue;
            break;
        case View::MarkersInRegion:
            if (isrgn) continue;
            // Geometric filter — boundary points count as inside.
            if (pos < filterStart || pos > filterEnd) continue;
            break;
        case View::MarkersAll:
            if (isrgn) continue;
            break;
        }

        Item it;
        it.idx      = idx;
        it.enumPos  = i;
        it.isRegion = isrgn;
        it.pos      = pos;
        it.rgnEnd   = rgnend;
        it.color    = color;
        if (name) it.name = name;
        items_.push_back(std::move(it));
    }

    // Clamp cursor and page to the new list size.
    if (items_.empty()) {
        cursorIdx_ = 0;
        pageOffset_ = 0;
    } else {
        if (cursorIdx_ >= static_cast<int>(items_.size())) {
            cursorIdx_ = static_cast<int>(items_.size()) - 1;
        }
        // SWELL defines max/min as macros — keep the math inline.
        const int last = static_cast<int>(items_.size()) - 1;
        const int maxPage = (last < 0) ? 0 : (last / kPageSize);
        if (pageOffset_ > maxPage) pageOffset_ = maxPage;
    }
}

void Overlay::window(Item const** out, int& outCount) const
{
    outCount = 0;
    const int start = pageOffset_ * kPageSize;
    for (int s = 0; s < kPageSize; ++s) {
        const int idx = start + s;
        if (idx >= static_cast<int>(items_.size())) {
            out[s] = nullptr;
        } else {
            out[s] = &items_[idx];
            outCount = s + 1;
        }
    }
}

int Overlay::pageCount() const
{
    if (items_.empty()) return 1;
    return (static_cast<int>(items_.size()) + kPageSize - 1) / kPageSize;
}

void Overlay::pageNext()
{
    if (pageOffset_ + 1 < pageCount()) ++pageOffset_;
}

void Overlay::pagePrev()
{
    if (pageOffset_ > 0) --pageOffset_;
}

void Overlay::drillIntoRegion(int enumPos)
{
    if (enumPos < 0 || enumPos >= static_cast<int>(items_.size())) return;
    const Item& it = items_[enumPos];
    if (!it.isRegion) return;
    filterRegionIdx_ = it.idx;
    view_ = View::MarkersInRegion;
    cursorIdx_  = 0;
    pageOffset_ = 0;
    enumerate();
}

void Overlay::backToRegions()
{
    view_ = View::Regions;
    filterRegionIdx_ = -1;
    cursorIdx_  = 0;
    pageOffset_ = 0;
    enumerate();
}

void Overlay::dumpWindow() const
{
    char hdr[160];
    const char* viewName =
        view_ == View::Regions          ? "Regions"
      : view_ == View::MarkersInRegion ? "MarkersInRegion"
                                       : "MarkersAll";
    std::snprintf(hdr, sizeof(hdr),
        "[Nav] view=%s items=%zu page=%d/%d cursor=%d filterRgn=%d "
        "active=%d autoFollow=%d\n",
        viewName, items_.size(), pageOffset_ + 1, pageCount(),
        cursorIdx_, filterRegionIdx_,
        active_.load() ? 1 : 0, autoFollow_ ? 1 : 0);
    ShowConsoleMsg(hdr);

    Item const* win[kPageSize] = {};
    int n = 0;
    window(win, n);
    for (int s = 0; s < kPageSize; ++s) {
        char line[256];
        if (!win[s]) {
            std::snprintf(line, sizeof(line), "  strip %d: --\n", s);
        } else {
            std::snprintf(line, sizeof(line),
                "  strip %d: %s%d %-20.20s pos=%8.3f%s color=0x%06X\n",
                s,
                win[s]->isRegion ? "R" : "M",
                win[s]->idx,
                win[s]->name.c_str(),
                win[s]->pos,
                win[s]->isRegion
                    ? (std::snprintf(nullptr, 0, "..%8.3f", win[s]->rgnEnd), "")
                    : "",
                win[s]->color & 0xFFFFFF);
        }
        ShowConsoleMsg(line);
    }
}

} // namespace uf8::nav
