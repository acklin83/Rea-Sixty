#pragma once
//
// Uf8Units: one or two UF8s as one surface. Pure arithmetic, no REAPER, no device;
// main.cpp feeds it the live state (how many units carry strips, the UF1 Extender,
// the Master pin), tests/test_uf8_units.cpp checks it.
//
// Plan docs/dual-uf8-plan.md, stage 2. Two UF8s were first built by Tim Shadgett
// (PR #10); this is the core of our own version, his diff used as the map.
//
// ⇨ ONE UF8 IS THE CASE THAT MUST NOT CHANGE. Every function here, called with
// units = 1, gives what main.cpp computed before this file existed; the test
// holds that line by line. A second unit only ever adds strips 8..15 and slots
// after the existing ones, nothing that one UF8 uses moves.
//
// Strip = surface strip, 0..stripCount()-1: 0..7 on the left unit, 8..15 on the
// right one. Unit 0 = left (g_dev), unit 1 = right (g_dev2).
//
#include <algorithm>

namespace uf8units {

constexpr int kStripsPerUnit = 8;
constexpr int kMaxUnits      = 2;
constexpr int kMaxStrips     = kStripsPerUnit * kMaxUnits;

inline int clampUnits(int units) { return std::clamp(units, 1, kMaxUnits); }
inline int stripCount(int units) { return kStripsPerUnit * clampUnits(units); }

// ---------------------------------------------------------------------------
// Strip <-> (unit, local strip on that unit)
// ---------------------------------------------------------------------------
struct UnitStrip {
    int unit  = -1;   // -1 = no such strip on this surface
    int local = -1;   // 0..7 on its unit
};

inline UnitStrip unitStripOf(int strip, int units)
{
    if (strip < 0 || strip >= stripCount(units)) return {};
    return { strip / kStripsPerUnit, strip % kStripsPerUnit };
}

inline int surfaceStrip(int unit, int local, int units)
{
    if (unit < 0 || unit >= clampUnits(units)) return -1;
    if (local < 0 || local >= kStripsPerUnit) return -1;
    return unit * kStripsPerUnit + local;
}

// ---------------------------------------------------------------------------
// Slots shared with the UF1
// ---------------------------------------------------------------------------
// The pot-touch and send-gesture tables in main.cpp hold the UF8's eight strips
// first and the UF1's slots after them. The right unit's strips go AFTER the last
// UF1 slot, so no slot the UF1 or the left unit uses ever moves. The sizes are
// today's table sizes; main.cpp static_asserts that they still are.
constexpr int kPanSlotsOneUnit  = 9;    // 0..7 UF8, 8 = UF1 pot above the fader
constexpr int kVolSlotsOneUnit  = 13;   // 0..7 UF8, 8 = UF1 above, 9..12 UF1 DAW pots
constexpr int kSendSlotsOneUnit = 14;   // 0..7 UF8, 8..13 UF1 (13 = the Extender)

inline int sharedSlot(int strip, int oneUnitSize)
{
    return strip < kStripsPerUnit ? strip : oneUnitSize + (strip - kStripsPerUnit);
}
inline int panSlot(int strip)  { return sharedSlot(strip, kPanSlotsOneUnit); }
inline int volSlot(int strip)  { return sharedSlot(strip, kVolSlotsOneUnit); }
inline int sendSlot(int strip) { return sharedSlot(strip, kSendSlotsOneUnit); }

// Table sizes with `units` UF8s.
constexpr int panSlotCount(int units)
{ return kPanSlotsOneUnit + (units > 1 ? kStripsPerUnit : 0); }
constexpr int volSlotCount(int units)
{ return kVolSlotsOneUnit + (units > 1 ? kStripsPerUnit : 0); }
constexpr int sendSlotCount(int units)
{ return kSendSlotsOneUnit + (units > 1 ? kStripsPerUnit : 0); }

// ---------------------------------------------------------------------------
// UF1 Extender: where the UF1 sits in the bank
// ---------------------------------------------------------------------------
// Stored values (ExtState uf1ExtenderSide): 0 = left, 1 = right keep the meaning
// they always had; 2 = middle (UF8 L | UF1 | UF8 R) is new. Middle needs two
// units; with one it behaves as right.
enum class ExtSide : int { Left = 0, Right = 1, Middle = 2 };

inline ExtSide extSideFromStored(int v)
{
    if (v == 0) return ExtSide::Left;
    if (v == 2) return ExtSide::Middle;
    return ExtSide::Right;
}

inline ExtSide effectiveSide(ExtSide s, int units)
{
    return (s == ExtSide::Middle && clampUnits(units) < 2) ? ExtSide::Right : s;
}

// The side switch: left, middle, right, left. With one unit middle is skipped,
// so the switch toggles left / right exactly as it always did. A stored middle
// with one unit counts as right, so the next press goes left.
inline int nextExtSide(int stored, int units)
{
    const ExtSide s = effectiveSide(extSideFromStored(stored), units);
    if (clampUnits(units) < 2) return s == ExtSide::Left ? 1 : 0;
    switch (s) {
        case ExtSide::Left:   return 2;
        case ExtSide::Middle: return 1;
        default:              return 0;
    }
}

struct Layout {
    int     units    = 1;               // UF8s carrying strips
    bool    extender = false;           // UF1 Extender active (setting AND UF1 open)
    ExtSide side     = ExtSide::Right;  // as stored
};

// +1 for a strip that sits to the right of the UF1 in the bank, else 0. One UF8:
// every strip with the Extender left, none otherwise (uf8StripBase_). Middle: the
// right unit's strips.
inline int stripBase(const Layout& l, int strip)
{
    if (!l.extender) return 0;
    switch (effectiveSide(l.side, l.units)) {
        case ExtSide::Left:   return 1;
        case ExtSide::Middle: return strip >= kStripsPerUnit ? 1 : 0;
        default:              return 0;
    }
}

// Tracks the bank window shows at once: the UF8 strips that show a track
// (effectiveStripCount_), plus the UF1, minus the strips DynaMount / Hue hold.
inline int bankWidth(const Layout& l, int effectiveStrips, int reservedStrips)
{
    return effectiveStrips + (l.extender ? 1 : 0) - reservedStrips;
}

// The UF1's place in the bank window, counted from the bank offset. Left: 0.
// Right: after the last UF8 track strip (`effectiveStrips`, which already lacks
// a Master pin's strip). Middle: after the left unit's track strips, one fewer
// when the Master is pinned (shift) to a strip of the left unit.
inline int uf1BankSlot(const Layout& l, int effectiveStrips, int masterShiftStrip)
{
    if (!l.extender) return -1;
    switch (effectiveSide(l.side, l.units)) {
        case ExtSide::Left:   return 0;
        case ExtSide::Middle:
            return kStripsPerUnit
                 - ((masterShiftStrip >= 0 && masterShiftStrip < kStripsPerUnit) ? 1 : 0);
        default:              return effectiveStrips;
    }
}

// Sends on the faders: `l.extender` here means "the UF1 is a send fader too"
// (uf1ExtenderRouteFader_), not just "the Extender is on".
inline int sendWindowWidth(const Layout& l)
{
    return stripCount(l.units) + (l.extender ? 1 : 0);
}

// The UF1's send slot in the send window: Left 0, Middle 8, Right after the last
// UF8 strip.
inline int uf1SendSlot(const Layout& l)
{
    if (!l.extender) return -1;
    switch (effectiveSide(l.side, l.units)) {
        case ExtSide::Left:   return 0;
        case ExtSide::Middle: return kStripsPerUnit;
        default:              return stripCount(l.units);
    }
}

// ---------------------------------------------------------------------------
// Pages on two units (soft-key pages, the next bank of a set, CC pages)
// ---------------------------------------------------------------------------
// The right unit always shows the page after the left one; past the last page
// its row is empty. A page key picks the page of the unit it is pressed on, the
// other unit follows. The page key for page 1 on the right unit cannot put the
// left one on page 0: then left shows 1 and right 2 (Frank 28.10.2026). PAGE
// < > moves by one. Every move clamps, none wraps.
struct UnitPages {
    int left  = 0;
    int right = -1;   // -1 = empty row (one unit, or past the last page)
};

inline UnitPages pagesForLeft(int leftPage, int pageCount, int units)
{
    UnitPages p;
    p.left = std::clamp(leftPage, 0, std::max(pageCount - 1, 0));
    if (clampUnits(units) >= 2 && p.left + 1 < pageCount) p.right = p.left + 1;
    return p;
}

inline UnitPages pagesAfterPress(int unit, int page, int pageCount, int units)
{
    const int left = (unit == 1 && clampUnits(units) >= 2) ? page - 1 : page;
    return pagesForLeft(std::max(left, 0), pageCount, units);
}

inline UnitPages pagesAfterStep(int leftPage, int delta, int pageCount, int units)
{
    return pagesForLeft(leftPage + delta, pageCount, units);
}

// The page a unit shows (for its page LEDs); -1 = none.
inline int pageOnUnit(const UnitPages& p, int unit)
{
    return unit == 0 ? p.left : (unit == 1 ? p.right : -1);
}

// ---------------------------------------------------------------------------
// FX and Lanes banks: the only dynamic banks with more than eight entries
// ---------------------------------------------------------------------------
// With two units the right row continues the list (entries 9..16) and a page is
// sixteen wide. Every other dynamic bank has eight entries and follows the page
// rule above (the right row is the next bank of the set).
inline int dynBankWindow(int units) { return stripCount(units); }

// First entry on `unit`'s row at bank page `page`.
inline int dynBankSlotBase(int page, int units, int unit)
{
    return page * dynBankWindow(units) + unit * kStripsPerUnit;
}

inline int dynBankMaxPage(int count, int units)
{
    return count > 0 ? (count - 1) / dynBankWindow(units) : 0;
}

// The page that shows entry `entry`.
inline int dynBankPageOf(int entry, int units)
{
    return entry < 0 ? 0 : entry / dynBankWindow(units);
}

} // namespace uf8units
