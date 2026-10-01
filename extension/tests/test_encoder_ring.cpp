// EncoderRing: the picker ring of the channel encoder's modes (UF1 and UF8).
// Pins the UF1 behaviour it replaced, and that a mode added to the enum does not
// throw a stored ring away.

#include "EncoderRing.h"

#include <cstdio>

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

// The UF1 factory ring of 15 modes: the kUf1EncoderModes order (BankBy1 = 7 left out),
// BankBy1 appended and hidden.
static void uf1Factory(encring::Ring<15>& r)
{
    static const int order[] = { 0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14 };
    r.setDefaults(order, 14, [](int m) { return m != 7; }, 0);
}

int main()
{
    {   // Factory: the order, BankBy1 last and hidden; the CSV the UF1 always wrote.
        encring::Ring<15> r;
        uf1Factory(r);
        EXPECT(r.seq[7].load() == 8);
        EXPECT(r.seq[14].load() == 7);
        EXPECT(!r.visible(7));
        EXPECT(r.seqCsv() == "0,1,2,3,4,5,6,8,9,10,11,12,13,14,7");
        EXPECT(r.visCsv() == "1,1,1,1,1,1,1,0,1,1,1,1,1,1,1");
        int v[15];
        EXPECT(r.visibleList(v) == 14);
    }
    {   // Stepping walks the visible ring and wraps, BankBy1 skipped.
        encring::Ring<15> r;
        uf1Factory(r);
        EXPECT(r.step(0, +1) == 1);
        EXPECT(r.step(6, +1) == 8);
        EXPECT(r.step(0, -1) == 14);
        EXPECT(r.step(14, +1) == 0);
        EXPECT(r.step(7, +1) == 1);          // hidden live mode counts as the first
        EXPECT(r.step(0, 15) == 1);          // more than a full turn
    }
    {   // A full stored ring round-trips.
        encring::Ring<15> r;
        uf1Factory(r);
        EXPECT(r.loadSeq("14,13,12,11,10,9,8,7,6,5,4,3,2,1,0"));
        EXPECT(r.seq[0].load() == 14 && r.seq[14].load() == 0);
        EXPECT(r.loadVis("0,1,1,1,1,1,1,1,1,1,1,1,1,1,0"));
        EXPECT(!r.visible(0) && r.visible(7) && !r.visible(14));
    }
    {   // Malformed: unknown mode, a mode twice, junk, too many. Nothing changes.
        encring::Ring<15> r;
        uf1Factory(r);
        const std::string before = r.seqCsv();
        EXPECT(!r.loadSeq("0,1,15"));
        EXPECT(!r.loadSeq("0,1,1"));
        EXPECT(!r.loadSeq("0,x"));
        EXPECT(!r.loadSeq("0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,0"));
        EXPECT(!r.loadSeq(""));
        EXPECT(!r.loadSeq(nullptr));
        EXPECT(r.seqCsv() == before);
        EXPECT(!r.loadVis("1,x"));
        EXPECT(r.visCsv() == "1,1,1,1,1,1,1,0,1,1,1,1,1,1,1");
    }
    {   // ⛔ A mode appended to the enum (16 now): the ring stored with 15 survives,
        // the new mode goes behind it with its factory flag.
        encring::Ring<16> r;
        static const int order[] = { 0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15 };
        r.setDefaults(order, 15, [](int m) { return m != 7; }, 0);
        EXPECT(r.loadSeq("14,13,12,11,10,9,8,7,6,5,4,3,2,1,0"));
        EXPECT(r.seq[0].load() == 14);
        EXPECT(r.seq[14].load() == 0);
        EXPECT(r.seq[15].load() == 15);
        EXPECT(r.loadVis("0,1,1,1,1,1,1,0,1,1,1,1,1,1,1"));
        EXPECT(!r.visible(0));
        EXPECT(r.visible(15));
    }
    {   // Hiding everything keeps the fallback; hiding the live mode moves it on.
        encring::Ring<4> r;
        static const int order[] = { 2, 0, 1, 3 };
        r.setDefaults(order, 4, [](int) { return true; }, 0);
        EXPECT(r.setVisible(0, false, 0) == 1);        // ring 2,0,1,3: after 0 comes 1
        EXPECT(r.setVisible(3, false, 1) == 1);        // not the live one: stays
        EXPECT(r.setVisible(1, false, 1) == 2);        // wraps to 2
        EXPECT(r.setVisible(2, false, 2) == 0);        // last one: fallback comes back
        EXPECT(r.visible(0));
        EXPECT(r.loadVis("0,0,0,0"));
        EXPECT(r.visible(0));
        int v[4];
        EXPECT(r.visibleList(v) == 1 && v[0] == 0);
    }
    {   // Reorder.
        encring::Ring<4> r;
        static const int order[] = { 0, 1, 2, 3 };
        r.setDefaults(order, 4, [](int) { return true; }, 0);
        EXPECT(r.move(1, -1));
        EXPECT(r.seqCsv() == "1,0,2,3");
        EXPECT(!r.move(0, -1));
        EXPECT(!r.move(3, +1));
        EXPECT(r.nextVisibleAfter(0) == 2);
    }

    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("encoder_ring: all passed\n");
    return 0;
}
