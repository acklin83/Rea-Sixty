// SelsetBank: labels, lamps and gestures of the "Selection Sets" dynamic bank
// (Frank 01.10.2026).

#include "SelsetBank.h"

#include <cstdio>

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

using selbank::Key;
using selbank::Op;
using selbank::Slot;

int main()
{
    // Empty slot: dark, no label.
    Slot empty;
    Key k = selbank::key(empty, 3);
    EXPECT(!k.present && k.led == 0 && k.label.empty());

    // Snapshot without a name: "Set 3", dim; recalled: lit.
    Slot snap; snap.populated = true;
    k = selbank::key(snap, 3);
    EXPECT(k.present && k.led == 1 && k.label == "Set 3");
    snap.active = true;
    EXPECT(selbank::key(snap, 3).led == 2);

    // Named wins; an unnamed group shows its REAPER group.
    Slot named; named.populated = true; named.name = "Drums";
    EXPECT(selbank::key(named, 1).label == "Drums");
    Slot grp; grp.populated = true; grp.group = true; grp.groupIdx = 70;
    EXPECT(selbank::key(grp, 5).label == "Grp 70");

    // Gestures on an empty slot: only a save does anything.
    EXPECT(selbank::op(0, false) == Op::None);
    EXPECT(selbank::op(4, false) == Op::Save);
    EXPECT(selbank::op(1, false) == Op::Save);
    EXPECT(selbank::op(2, false) == Op::None);
    EXPECT(selbank::op(3, false) == Op::None);

    // On a populated slot: holding never overwrites, Shift does.
    EXPECT(selbank::op(0, true) == Op::Recall);
    EXPECT(selbank::op(4, true) == Op::None);
    EXPECT(selbank::op(1, true) == Op::Save);
    EXPECT(selbank::op(2, true) == Op::Select);
    EXPECT(selbank::op(3, true) == Op::Clear);
    EXPECT(selbank::op(7, true) == Op::None);

    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("selset_bank: all passed\n");
    return 0;
}
