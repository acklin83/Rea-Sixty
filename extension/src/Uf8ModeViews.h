#pragma once
//
// What a DynaMount stand and a Hue lamp show on a UF8 strip. Pulled verbatim out
// of main.cpp's pushZonesForVisibleSlots (the DynaMount and Hue branches,
// 29.10.2026, docs/orc-uf8-plan.md stage 6) so that ORC's side-cars show the
// same cells as Rea-Sixty's. Only the content: the caller keeps its caches and
// sends (main.cpp its g_last* arrays, ORC uf8strips::paintStrip).
// Test: tests/test_uf8_mode_views.cpp.

#include "DynaMountManager.h"
#include "HueManager.h"
#include "Protocol.h"
#include "TrackName.h"

#include <cstdint>
#include <cstdio>
#include <string>

namespace uf8modes {

struct Cells {
    std::string upper;       // name, Latin-1, <= kUf8ScribbleChars
    std::string csType;      // DYNA / LAMP / ZONE
    std::string chanNum;     // the row's 1-based number in Settings
    std::string faderText;   // readout, no unit (not a level)
    std::string valueLine;   // <= 19
    double      fader01 = 0; // where the motor puts the fader, 0..1
    double      ring    = 0; // the V-Pot ring, unipolar 0..1
};

// A palette index of Settings (selPaletteRgb) as 0xRRGGBB, clamped; 0 without
// a palette.
inline std::uint32_t paletteRgb(int ci)
{
    int count = 0;
    const uf8::PaletteRgb* pal = uf8::selPaletteRgb(&count);
    if (count <= 0) return 0;
    if (ci < 0) ci = 0;
    if (ci >= count) ci = count - 1;
    const uf8::PaletteRgb& p = pal[ci];
    return (std::uint32_t(p.r) << 16) | (std::uint32_t(p.g) << 8) | std::uint32_t(p.b);
}

// The colour bar of a stand: its configured palette colour (a stand has no
// colour of its own, so it borrows one). main.cpp reaperColorForVisibleSlot.
inline std::uint32_t dynaRgb(uf8::dynamount::DynaMountManager& dm, int mnt)
{
    return paletteRgb(dm.info(mnt).color);
}

// The colour bar of a lamp: the LAMP's live colour; the configured palette
// colour for a lamp that is off or not heard from yet.
inline std::uint32_t hueRgb(uf8::hue::HueManager& hm, int slot)
{
    const std::uint32_t live = hm.liveRgb24(slot);
    if (live != 0u) return live;
    return paletteRgb(hm.slot(slot).colour);
}

// A mount strip. `pending`: the fader is under the hand and has moved
// (send-on-release, Frank 2026-06-28): the screen follows the hand, the stand
// moves once on release. Fader axis: no FLIP = X (left/right, v), FLIP = Y
// (distance, h, inverted: top = nearest).
inline Cells dyna(uf8::dynamount::DynaMountManager& dm, int mnt, bool flipped,
                  bool pending, double pendingNorm)
{
    Cells c;
    const auto info = dm.info(mnt);
    const bool faderDist = flipped;
    int        hVal = dm.targetH(mnt);
    int        vVal = dm.targetV(mnt);
    const int  rVal = dm.targetR(mnt);
    if (pending) {
        double norm = pendingNorm;
        if (norm < 0.0) norm = 0.0;
        if (norm > 1.0) norm = 1.0;
        const auto& cal = info.cal;
        const int lo = faderDist ? cal.hMin : cal.vMin;
        const int hi = faderDist ? cal.hMax : cal.vMax;
        // Distance (Y) is inverted (top = nearest), as setDistance(1 - f01).
        const double useNorm = faderDist ? (1.0 - norm) : norm;
        const int live = lo + static_cast<int>(useNorm * (hi - lo) + 0.5);
        if (faderDist) hVal = live; else vVal = live;
    }
    c.upper = utf8ToLatin1(info.name);
    if (c.upper.empty()) c.upper = "MOUNT";
    if (c.upper.size() > uf8::kUf8ScribbleChars) c.upper.resize(uf8::kUf8ScribbleChars);
    c.csType  = "DYNA";
    c.chanNum = std::to_string(mnt + 1);
    char db[8];
    snprintf(db, sizeof(db), "%c%d", faderDist ? 'Y' : 'X', faderDist ? hVal : vVal);
    c.faderText = db;
    char vl[24];
    snprintf(vl, sizeof(vl), "X%-3d Y%-3d R%-3d%s", vVal, hVal, rVal, info.online ? "" : " OFF");
    c.valueLine = vl;
    if (c.valueLine.size() > 19) c.valueLine.resize(19);
    // faderNorm(idx, horizontal): true = X, false = Y (inverted).
    c.fader01 = dm.faderNorm(mnt, /*horizontal=*/!faderDist);
    c.ring    = static_cast<double>(rVal) / static_cast<double>(uf8::dynamount::kRMax);
    return c;
}

// A lamp strip. The pot's axis is the configured role (FLIP: the second one).
// Hue is a circle, so a unipolar ring is the honest picture of it.
inline Cells hue(uf8::hue::HueManager& hm, int slot, bool flipped)
{
    Cells c;
    const uf8::hue::SlotConfig cfg = hm.slot(slot);
    c.upper = utf8ToLatin1(cfg.label.empty() ? cfg.bridgeName : cfg.label);
    if (c.upper.empty()) c.upper = "LAMP";
    if (c.upper.size() > uf8::kUf8ScribbleChars) c.upper.resize(uf8::kUf8ScribbleChars);
    // A zone answers once a second where a lamp answers ten times.
    c.csType  = (cfg.kind == uf8::hue::TargetKind::Group) ? "ZONE" : "LAMP";
    c.chanNum = std::to_string(slot + 1);
    if (!hm.liveOn(slot)) {
        c.faderText = "OFF";
    } else {
        char db[8];
        snprintf(db, sizeof(db), "%d", static_cast<int>(hm.liveBri01(slot) * 100.0 + 0.5));
        c.faderText = db;
    }
    c.valueLine = hm.liveValueLine(slot);
    if (c.valueLine.size() > 19) c.valueLine.resize(19);
    c.fader01 = hm.liveBri01(slot);
    const uf8::hue::Controls ctl = hm.controls();
    switch (flipped ? ctl.potFlip : ctl.pot) {
        case uf8::hue::PotRole::Hue:        c.ring = hm.liveHueDeg(slot) / 360.0; break;
        case uf8::hue::PotRole::Saturation: c.ring = hm.liveSat(slot); break;
        case uf8::hue::PotRole::Warmth:     c.ring = hm.liveWarm(slot); break;
        case uf8::hue::PotRole::Off:
        default:                            c.ring = 0.0; break;
    }
    return c;
}

} // namespace uf8modes
