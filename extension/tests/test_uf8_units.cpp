// Uf8Units: one or two UF8s as one surface (docs/dual-uf8-plan.md, stage 2).
// First half: with one UF8 every function gives what main.cpp computed before
// Uf8Units.h existed. The reference formulas below are copied from main.cpp as it
// stood on 28.10.2026 (uf8StripBase_, uf8SendStripBase_, bankWidth_,
// sendWindowWidth_, uf1FaderTrack_, uf1ExtenderSendRoute_, the extender toggle,
// dynBankSlotBase_, pageDynBank_, lanes::bank::pageOf(lane, 8), the slot tables).
// Second half: the rules for two units.

#include "Uf8Units.h"

#include <cstdio>

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

using namespace uf8units;

// ---- main.cpp before Uf8Units.h -------------------------------------------
// extActive = uf1ExtenderActive_() (or uf1ExtenderRouteFader_() for sends),
// side = g_uf1ExtenderSide (1 = right, 0 = left).
static int oldStripBase(bool extActive, int side) { return (extActive && side == 0) ? 1 : 0; }
static int oldBankWidth(int esc, bool extActive, int reserved)
{ return esc + (extActive ? 1 : 0) - reserved; }
static int oldSendWindowWidth(bool routeFader) { return 8 + (routeFader ? 1 : 0); }
static int oldUf1BankSlot(int side, int esc) { return side ? esc : 0; }
// uf1ExtenderSendRoute_: strip `side ? 8 : -1`, then + uf8SendStripBase_().
static int oldUf1SendSlot(int side) { return (side ? 8 : -1) + oldStripBase(true, side); }
static int oldToggle(int side) { return side ? 0 : 1; }
static int oldDynSlotBase(int page) { return page * 8; }
static int oldDynMaxPage(int count) { return count > 0 ? (count - 1) / 8 : 0; }
static int oldPageOf(int lane) { return lane < 0 ? 0 : lane / 8; }

static void oneUnitIsUnchanged()
{
    for (int stored = 0; stored <= 1; ++stored)
    for (int ext = 0; ext <= 1; ++ext) {
        const Layout l{ 1, ext != 0, extSideFromStored(stored) };
        for (int s = 0; s < 8; ++s)
            EXPECT(stripBase(l, s) == oldStripBase(ext != 0, stored));
        for (int esc = 7; esc <= 8; ++esc)
            for (int res = 0; res < esc; ++res)
                EXPECT(bankWidth(l, esc, res) == oldBankWidth(esc, ext != 0, res));
        EXPECT(sendWindowWidth(l) == oldSendWindowWidth(ext != 0));
        if (ext) {
            for (int esc = 7; esc <= 8; ++esc)
                for (int mps : { -1, 0, 7 })
                    EXPECT(uf1BankSlot(l, esc, mps) == oldUf1BankSlot(stored, esc));
            EXPECT(uf1SendSlot(l) == oldUf1SendSlot(stored));
        }
        EXPECT(nextExtSide(stored, 1) == oldToggle(stored));
    }
    // A middle stored with one unit (set with two, then one unplugged) is right.
    for (int ext = 0; ext <= 1; ++ext) {
        const Layout m{ 1, ext != 0, ExtSide::Middle };
        const Layout r{ 1, ext != 0, ExtSide::Right };
        for (int s = 0; s < 8; ++s) EXPECT(stripBase(m, s) == stripBase(r, s));
        EXPECT(uf1BankSlot(m, 8, -1) == uf1BankSlot(r, 8, -1));
        EXPECT(uf1BankSlot(m, 7, 0) == uf1BankSlot(r, 7, 0));
        EXPECT(uf1SendSlot(m) == uf1SendSlot(r));
    }
    EXPECT(nextExtSide(2, 1) == 0);   // counts as right, so the press goes left

    // Slots: the left unit's strips keep their slot, the table sizes stay.
    for (int s = 0; s < 8; ++s) {
        EXPECT(panSlot(s) == s);
        EXPECT(volSlot(s) == s);
        EXPECT(sendSlot(s) == s);
    }
    EXPECT(panSlotCount(1) == 9);
    EXPECT(volSlotCount(1) == 13);
    EXPECT(sendSlotCount(1) == 14);

    // FX / Lanes banks.
    for (int page = 0; page < 20; ++page) EXPECT(dynBankSlotBase(page, 1, 0) == oldDynSlotBase(page));
    for (int count = 0; count < 70; ++count) EXPECT(dynBankMaxPage(count, 1) == oldDynMaxPage(count));
    for (int lane = -2; lane < 70; ++lane) EXPECT(dynBankPageOf(lane, 1) == oldPageOf(lane));

    // Pages: a key picks its page, PAGE < > clamps, nothing on a right row.
    for (int count = 1; count <= 6; ++count) {
        for (int p = 0; p < count; ++p) {
            const UnitPages up = pagesAfterPress(0, p, count, 1);
            EXPECT(up.left == p);
            EXPECT(up.right == -1);
            EXPECT(pagesAfterStep(p, -1, count, 1).left == (p > 0 ? p - 1 : 0));
            EXPECT(pagesAfterStep(p, +1, count, 1).left == (p < count - 1 ? p + 1 : count - 1));
        }
    }

    // Strip mapping: one unit is unit 0, strip == local.
    for (int s = 0; s < 8; ++s) {
        EXPECT(unitStripOf(s, 1).unit == 0);
        EXPECT(unitStripOf(s, 1).local == s);
        EXPECT(surfaceStrip(0, s, 1) == s);
    }
    EXPECT(unitStripOf(8, 1).unit == -1);
    EXPECT(surfaceStrip(1, 0, 1) == -1);
    EXPECT(stripCount(1) == 8);
}

static void twoUnits()
{
    // Strips 8..15 are the right unit.
    EXPECT(stripCount(2) == 16);
    for (int s = 0; s < 16; ++s) {
        const UnitStrip us = unitStripOf(s, 2);
        EXPECT(us.unit == s / 8);
        EXPECT(us.local == s % 8);
        EXPECT(surfaceStrip(us.unit, us.local, 2) == s);
    }
    EXPECT(unitStripOf(16, 2).unit == -1);
    EXPECT(unitStripOf(-1, 2).unit == -1);

    // Slots: the right unit comes after every UF1 slot.
    for (int s = 8; s < 16; ++s) {
        EXPECT(panSlot(s)  == 9  + (s - 8));
        EXPECT(volSlot(s)  == 13 + (s - 8));
        EXPECT(sendSlot(s) == 14 + (s - 8));
        EXPECT(panSlot(s)  < panSlotCount(2));
        EXPECT(volSlot(s)  < volSlotCount(2));
        EXPECT(sendSlot(s) < sendSlotCount(2));
    }
    EXPECT(panSlotCount(2) == 17);
    EXPECT(volSlotCount(2) == 21);
    EXPECT(sendSlotCount(2) == 22);

    // Extender left: UF1 slot 0, all sixteen shift by one.
    {
        const Layout l{ 2, true, ExtSide::Left };
        for (int s = 0; s < 16; ++s) EXPECT(stripBase(l, s) == 1);
        EXPECT(uf1BankSlot(l, 16, -1) == 0);
        EXPECT(uf1SendSlot(l) == 0);
        EXPECT(bankWidth(l, 16, 0) == 17);
        EXPECT(sendWindowWidth(l) == 17);
    }
    // Extender right: UF1 after strip 16 (after 15 with a Master pin).
    {
        const Layout l{ 2, true, ExtSide::Right };
        for (int s = 0; s < 16; ++s) EXPECT(stripBase(l, s) == 0);
        EXPECT(uf1BankSlot(l, 16, -1) == 16);
        EXPECT(uf1BankSlot(l, 15, 15) == 15);
        EXPECT(uf1SendSlot(l) == 16);
    }
    // Extender middle: UF8 L | UF1 | UF8 R.
    {
        const Layout l{ 2, true, ExtSide::Middle };
        for (int s = 0; s < 8; ++s)  EXPECT(stripBase(l, s) == 0);
        for (int s = 8; s < 16; ++s) EXPECT(stripBase(l, s) == 1);
        EXPECT(uf1BankSlot(l, 16, -1) == 8);
        EXPECT(uf1BankSlot(l, 15, 0) == 7);    // Master on strip 1: seven tracks left
        EXPECT(uf1BankSlot(l, 15, 15) == 8);   // Master on the last strip: eight left
        EXPECT(uf1SendSlot(l) == 8);
        EXPECT(bankWidth(l, 16, 0) == 17);
    }
    // Every bank slot is taken exactly once: sixteen strips and the UF1.
    for (int side = 0; side <= 2; ++side) {
        const Layout l{ 2, true, extSideFromStored(side) };
        bool seen[17] = {};
        for (int s = 0; s < 16; ++s) {
            const int slot = s + stripBase(l, s);
            EXPECT(slot >= 0 && slot < 17 && !seen[slot]);
            if (slot >= 0 && slot < 17) seen[slot] = true;
        }
        const int u = uf1BankSlot(l, 16, -1);
        EXPECT(u >= 0 && u < 17 && !seen[u]);
    }
    // The side switch with two units: left, middle, right, left.
    EXPECT(nextExtSide(0, 2) == 2);
    EXPECT(nextExtSide(2, 2) == 1);
    EXPECT(nextExtSide(1, 2) == 0);
    // Extender off: no shift, no UF1 slot.
    {
        const Layout l{ 2, false, ExtSide::Middle };
        for (int s = 0; s < 16; ++s) EXPECT(stripBase(l, s) == 0);
        EXPECT(uf1BankSlot(l, 16, -1) == -1);
        EXPECT(uf1SendSlot(l) == -1);
        EXPECT(sendWindowWidth(l) == 16);
    }

    // Pages, six of them (CC layer): the right unit shows the next page.
    {
        UnitPages p = pagesAfterPress(0, 2, 6, 2);   // left "2" key
        EXPECT(p.left == 2 && p.right == 3);
        p = pagesAfterPress(1, 2, 6, 2);             // right "2" key
        EXPECT(p.left == 1 && p.right == 2);
        p = pagesAfterPress(1, 0, 6, 2);             // right V-POT: left 1, right 2
        EXPECT(p.left == 0 && p.right == 1);
        p = pagesAfterPress(0, 5, 6, 2);             // last page on the left
        EXPECT(p.left == 5 && p.right == -1);        // right row empty
        p = pagesAfterPress(1, 5, 6, 2);             // last page on the right
        EXPECT(p.left == 4 && p.right == 5);
        p = pagesAfterStep(4, +1, 6, 2);
        EXPECT(p.left == 5 && p.right == -1);
        p = pagesAfterStep(5, +1, 6, 2);             // clamps, no wrap
        EXPECT(p.left == 5);
        p = pagesAfterStep(0, -1, 6, 2);
        EXPECT(p.left == 0 && p.right == 1);
        EXPECT(pageOnUnit(p, 0) == 0);
        EXPECT(pageOnUnit(p, 1) == 1);
        EXPECT(pageOnUnit(p, 2) == -1);
    }
    // One page only: the right row is empty, whatever is pressed.
    {
        const UnitPages p = pagesAfterPress(1, 0, 1, 2);
        EXPECT(p.left == 0 && p.right == -1);
    }

    // FX / Lanes: the right row continues the list, a page is sixteen wide.
    EXPECT(dynBankWindow(2) == 16);
    EXPECT(dynBankSlotBase(0, 2, 0) == 0);
    EXPECT(dynBankSlotBase(0, 2, 1) == 8);
    EXPECT(dynBankSlotBase(1, 2, 0) == 16);
    EXPECT(dynBankSlotBase(1, 2, 1) == 24);
    EXPECT(dynBankMaxPage(0, 2) == 0);
    EXPECT(dynBankMaxPage(16, 2) == 0);
    EXPECT(dynBankMaxPage(17, 2) == 1);
    EXPECT(dynBankPageOf(15, 2) == 0);
    EXPECT(dynBankPageOf(16, 2) == 1);
}

int main()
{
    oneUnitIsUnchanged();
    twoUnits();
    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("uf8_units: all passed\n");
    return 0;
}
