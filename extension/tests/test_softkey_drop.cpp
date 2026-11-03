// Dragging in Settings, Bindings: banks, whole sets and single keys swap or copy
// through the store, on Plain with Shift along and on Shift alone, and what lands
// survives a reload. Runs the real bindings engine on a temp directory.

#include "Bindings.h"
#include "BindingsHost.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

namespace bnd = uf8::bindings;
using bnd::SoftKeyDrop;

static int g_fail = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

// A key with a label on Plain and, optionally, one on Shift. Labels because a
// label-only slot is real data and needs no registered built-in to survive a load.
static bnd::Binding key(const std::string& plain, const std::string& shift = "")
{
    bnd::Binding b;
    b.label = plain;
    b.labelIsUserSet = !plain.empty();
    b.color[0] = 0x11;
    b.shortPress[1].label = shift;
    return b;
}
static std::string uf8Plain(int l, int q, int sb, int s) { return bnd::getUserQuickSlot(l, q, sb, s).label; }
static std::string uf8Shift(int l, int q, int sb, int s) { return bnd::getUserQuickSlot(l, q, sb, s).shortPress[1].label; }
static std::string uf1Plain(int b, int s) { return bnd::getUf1SoftBankSlot(b, s).label; }
static std::string uf1Shift(int b, int s) { return bnd::getUf1SoftBankSlot(b, s).shortPress[1].label; }

static bnd::SubBankLed led(uint8_t r)
{
    bnd::SubBankLed l;
    l.color[0] = r;
    return l;
}

// Layer 2 (index 1), Quicks 0 and 1: nothing factory lives there.
static const int L = 1, A = 0, B = 1;

static void seedBanks()
{
    for (int q : {A, B}) for (int sb = 0; sb < 2; ++sb) for (int m = 0; m < 2; ++m) {
        for (int s = 0; s < bnd::kSlotsPerSubBank; ++s)
            bnd::setUserQuickSlot(L, q, sb, s, bnd::Binding{});
        bnd::setSubBankName(L, q, sb, m, "");
        bnd::setSubBankDynamic(L, q, sb, m, bnd::DynamicBankKind::None);
    }
    bnd::setUserQuickSlot(L, A, 0, 0, key("A0", "A0s"));
    bnd::setUserQuickSlot(L, A, 0, 7, key("A7"));
    bnd::setSubBankName(L, A, 0, 0, "Bank A");
    bnd::setSubBankDynamic(L, A, 0, 1, bnd::DynamicBankKind::TrackColours);
    bnd::setSubBankName(L, A, 0, 1, "Bank A S");     // after the kind, which clears it
    bnd::setSubBankLed(L, A, 0, 0, led(0x10));
    bnd::setSubBankLed(L, A, 0, 1, led(0x20));

    bnd::setUserQuickSlot(L, B, 1, 0, key("B0", "B0s"));
    bnd::setSubBankName(L, B, 1, 0, "Bank B");
    bnd::setSubBankLed(L, B, 1, 0, led(0x30));
}

static void testBankSwapPlain()
{
    seedBanks();
    EXPECT(bnd::dropBank(L, A, 0, L, B, 1, 0, SoftKeyDrop::Swap));
    // The dragged bank arrives whole, Shift and all.
    EXPECT(uf8Plain(L, B, 1, 0) == "A0");
    EXPECT(uf8Shift(L, B, 1, 0) == "A0s");
    EXPECT(uf8Plain(L, B, 1, 7) == "A7");
    EXPECT(bnd::getSubBankName(L, B, 1, 0) == "Bank A");
    EXPECT(bnd::getSubBankName(L, B, 1, 1) == "Bank A S");
    EXPECT(bnd::getSubBankDynamic(L, B, 1, 1) == bnd::DynamicBankKind::TrackColours);
    EXPECT(bnd::getSubBankLed(L, B, 1, 0).color[0] == 0x10);
    EXPECT(bnd::getSubBankLed(L, B, 1, 1).color[0] == 0x20);
    // And the other one is not lost.
    EXPECT(uf8Plain(L, A, 0, 0) == "B0");
    EXPECT(uf8Shift(L, A, 0, 0) == "B0s");
    EXPECT(uf8Plain(L, A, 0, 7).empty());
    EXPECT(bnd::getSubBankName(L, A, 0, 0) == "Bank B");
    EXPECT(bnd::getSubBankName(L, A, 0, 1).empty());
    EXPECT(bnd::getSubBankDynamic(L, A, 0, 1) == bnd::DynamicBankKind::None);
    EXPECT(bnd::getSubBankLed(L, A, 0, 0).color[0] == 0x30);

    // Swapping back restores it: nothing leaks between the two.
    EXPECT(bnd::dropBank(L, B, 1, L, A, 0, 0, SoftKeyDrop::Swap));
    EXPECT(uf8Plain(L, A, 0, 0) == "A0");
    EXPECT(uf8Plain(L, B, 1, 0) == "B0");
}

static void testBankSwapShift()
{
    seedBanks();
    EXPECT(bnd::dropBank(L, A, 0, L, B, 1, 1, SoftKeyDrop::Swap));
    // Only the Shift half moved.
    EXPECT(uf8Shift(L, B, 1, 0) == "A0s");
    EXPECT(uf8Shift(L, A, 0, 0) == "B0s");
    EXPECT(uf8Plain(L, A, 0, 0) == "A0");
    EXPECT(uf8Plain(L, B, 1, 0) == "B0");
    EXPECT(bnd::getSubBankName(L, B, 1, 1) == "Bank A S");
    EXPECT(bnd::getSubBankName(L, B, 1, 0) == "Bank B");
    EXPECT(bnd::getSubBankName(L, A, 0, 0) == "Bank A");
    EXPECT(bnd::getSubBankLed(L, B, 1, 1).color[0] == 0x20);
    EXPECT(bnd::getSubBankLed(L, B, 1, 0).color[0] == 0x30);
}

static void testBankCopy()
{
    seedBanks();
    bnd::copyBankToClipboard(L, B, 1, 0, "held");
    EXPECT(bnd::dropBank(L, A, 0, L, B, 0, 0, SoftKeyDrop::Copy));
    // Target is the source's bank, source is untouched.
    EXPECT(uf8Plain(L, B, 0, 0) == "A0");
    EXPECT(uf8Shift(L, B, 0, 0) == "A0s");
    EXPECT(bnd::getSubBankName(L, B, 0, 0) == "Bank A");
    EXPECT(bnd::getSubBankDynamic(L, B, 0, 1) == bnd::DynamicBankKind::TrackColours);
    EXPECT(bnd::getSubBankLed(L, B, 0, 0).color[0] == 0x10);
    EXPECT(bnd::getSubBankLed(L, B, 0, 1).color[0] == 0x20);
    EXPECT(uf8Plain(L, A, 0, 0) == "A0");
    // The clipboard the user was holding is still what they copied.
    EXPECT(bnd::bankClipboardLabel() == "held");

    // A source with nothing on Shift leaves the target's Shift alone, like Paste.
    seedBanks();
    bnd::setUserQuickSlot(L, A, 1, 0, key("onlyPlain"));
    EXPECT(bnd::dropBank(L, A, 1, L, B, 1, 0, SoftKeyDrop::Copy));
    EXPECT(uf8Plain(L, B, 1, 0) == "onlyPlain");
    EXPECT(uf8Shift(L, B, 1, 0) == "B0s");

    // Dragged on Shift, Plain stays.
    seedBanks();
    EXPECT(bnd::dropBank(L, A, 0, L, B, 1, 1, SoftKeyDrop::Copy));
    EXPECT(uf8Shift(L, B, 1, 0) == "A0s");
    EXPECT(uf8Plain(L, B, 1, 0) == "B0");
    EXPECT(bnd::getSubBankName(L, B, 1, 1) == "Bank A S");
    EXPECT(bnd::getSubBankName(L, B, 1, 0) == "Bank B");
}

static void testSets()
{
    seedBanks();
    // Sets 2 and 3 are Layer 2's Quick 1 and 2.
    int l = 0, q = 0;
    EXPECT(bnd::softKeySetToLayerQuick(2, l, q) && l == L && q == A);
    EXPECT(bnd::softKeySetToLayerQuick(3, l, q) && l == L && q == B);
    bnd::setSoftKeySetName(2, "Drums");
    bnd::setSoftKeySetName(3, "Vocals");

    EXPECT(bnd::dropSoftKeySet(2, 3, SoftKeyDrop::Swap));
    EXPECT(bnd::getSoftKeySetName(3) == "Drums");
    EXPECT(bnd::getSoftKeySetName(2) == "Vocals");
    EXPECT(uf8Plain(L, B, 0, 0) == "A0");     // same bank number, other set
    EXPECT(uf8Shift(L, B, 0, 0) == "A0s");
    EXPECT(uf8Plain(L, A, 1, 0) == "B0");
    EXPECT(bnd::getSubBankLed(L, B, 0, 0).color[0] == 0x10);
    EXPECT(bnd::getSubBankLed(L, A, 1, 0).color[0] == 0x30);

    EXPECT(bnd::dropSoftKeySet(3, 2, SoftKeyDrop::Copy));
    EXPECT(bnd::getSoftKeySetName(2) == "Drums");
    EXPECT(bnd::getSoftKeySetName(3) == "Drums");
    EXPECT(uf8Plain(L, A, 0, 0) == "A0");
    EXPECT(uf8Plain(L, B, 0, 0) == "A0");
    EXPECT(uf8Plain(L, A, 1, 0).empty());     // the target is overwritten whole

    // SSL's rows stay where they are, and a drop on itself writes nothing.
    EXPECT(!bnd::dropSoftKeySet(8, 2, SoftKeyDrop::Swap));
    EXPECT(!bnd::dropSoftKeySet(2, 9, SoftKeyDrop::Copy));
    EXPECT(!bnd::dropSoftKeySet(2, 2, SoftKeyDrop::Swap));
    EXPECT(!bnd::dropSoftKeySet(0, 2, SoftKeyDrop::Swap));
    bnd::setSoftKeySetName(2, "");
    bnd::setSoftKeySetName(3, "");
}

static void testUf8Keys()
{
    seedBanks();
    bnd::Binding k = key("long", "longS");
    k.hasLongPress = true;
    k.longPress[0].label = "held";
    k.longPress[1].label = "heldS";
    bnd::setUserQuickSlot(L, A, 0, 3, k);

    // Plain: the whole key, Long Press and Shift with it, onto an empty place.
    EXPECT(bnd::dropUf8Key(L, A, 0, 3, 4, 0, SoftKeyDrop::Swap));
    EXPECT(uf8Plain(L, A, 0, 4) == "long");
    EXPECT(uf8Shift(L, A, 0, 4) == "longS");
    EXPECT(bnd::getUserQuickSlot(L, A, 0, 4).hasLongPress);
    EXPECT(bnd::getUserQuickSlot(L, A, 0, 4).longPress[0].label == "held");
    EXPECT(bnd::getUserQuickSlot(L, A, 0, 4).color[0] == 0x11);
    EXPECT(uf8Plain(L, A, 0, 3).empty());
    EXPECT(!bnd::getUserQuickSlot(L, A, 0, 3).hasLongPress);

    // Shift: the half alone. The key that gets the long press shows the column.
    EXPECT(bnd::dropUf8Key(L, A, 0, 4, 0, 1, SoftKeyDrop::Swap));
    EXPECT(uf8Shift(L, A, 0, 0) == "longS");
    EXPECT(uf8Shift(L, A, 0, 4) == "A0s");
    EXPECT(uf8Plain(L, A, 0, 0) == "A0");
    EXPECT(uf8Plain(L, A, 0, 4) == "long");
    EXPECT(bnd::getUserQuickSlot(L, A, 0, 0).longPress[1].label == "heldS");
    EXPECT(bnd::getUserQuickSlot(L, A, 0, 0).hasLongPress);

    // Copy on Plain takes Shift along, and the source stays.
    EXPECT(bnd::dropUf8Key(L, A, 0, 4, 2, 0, SoftKeyDrop::Copy));
    EXPECT(uf8Plain(L, A, 0, 2) == "long");
    EXPECT(uf8Shift(L, A, 0, 2) == "A0s");
    EXPECT(uf8Plain(L, A, 0, 4) == "long");
    // Copy on Shift leaves the target's Plain.
    EXPECT(bnd::dropUf8Key(L, A, 0, 4, 7, 1, SoftKeyDrop::Copy));
    EXPECT(uf8Shift(L, A, 0, 7) == "A0s");
    EXPECT(uf8Plain(L, A, 0, 7) == "A7");

    EXPECT(!bnd::dropUf8Key(L, A, 0, 2, 2, 0, SoftKeyDrop::Swap));
    EXPECT(!bnd::dropUf8Key(L, A, 0, 2, 8, 0, SoftKeyDrop::Swap));
    EXPECT(!bnd::dropUf8Key(L, A, 0, 2, 3, 2, SoftKeyDrop::Swap));
}

static void testUf1()
{
    for (int b = 8; b < 10; ++b) for (int m = 0; m < 2; ++m) {
        for (int s = 0; s < bnd::kUf1SoftBankSlots; ++s)
            bnd::setUf1SoftBankSlot(b, s, bnd::Binding{});
        bnd::setUf1SoftBankName(b, m, "");
        bnd::setUf1SoftBankDynamic(b, m, bnd::DynamicBankKind::None);
    }
    bnd::setUf1SoftBankSlot(8, 0, key("U0", "U0s"));
    bnd::setUf1SoftBankSlot(8, 3, key("U3"));
    bnd::setUf1SoftBankName(8, 0, "Eight");
    bnd::setUf1SoftBankDynamic(8, 1, bnd::DynamicBankKind::TrackColours);

    // Whole bank, onto an empty one.
    EXPECT(bnd::dropUf1Bank(8, 9, 0, SoftKeyDrop::Swap));
    EXPECT(uf1Plain(9, 0) == "U0");
    EXPECT(uf1Shift(9, 0) == "U0s");
    EXPECT(bnd::getUf1SoftBankName(9, 0) == "Eight");
    EXPECT(bnd::getUf1SoftBankDynamic(9, 1) == bnd::DynamicBankKind::TrackColours);
    EXPECT(uf1Plain(8, 0).empty());
    EXPECT(bnd::getUf1SoftBankName(8, 0).empty());

    // Shift half, onto the bank that has nothing there now.
    EXPECT(bnd::dropUf1Bank(9, 8, 1, SoftKeyDrop::Swap));
    EXPECT(uf1Shift(8, 0) == "U0s");
    EXPECT(uf1Plain(8, 0).empty());
    EXPECT(uf1Plain(9, 0) == "U0");
    EXPECT(bnd::getUf1SoftBankDynamic(8, 1) == bnd::DynamicBankKind::TrackColours);

    // Copy: the target becomes it, the source stays.
    EXPECT(bnd::dropUf1Bank(9, 8, 0, SoftKeyDrop::Copy));
    EXPECT(uf1Plain(8, 0) == "U0");
    EXPECT(uf1Plain(9, 0) == "U0");
    EXPECT(bnd::getUf1SoftBankName(8, 0) == "Eight");

    // A single key, across banks.
    EXPECT(bnd::dropUf1Key(9, 0, 8, 2, 0, SoftKeyDrop::Swap));
    EXPECT(uf1Plain(8, 2) == "U0");
    EXPECT(uf1Plain(9, 0).empty());
    EXPECT(bnd::dropUf1Key(8, 2, 9, 1, 0, SoftKeyDrop::Copy));
    EXPECT(uf1Plain(9, 1) == "U0");
    EXPECT(uf1Plain(8, 2) == "U0");
    bnd::setUf1SoftBankSlot(8, 1, key("x", "xS"));
    EXPECT(bnd::dropUf1Key(8, 1, 9, 1, 1, SoftKeyDrop::Swap));
    EXPECT(uf1Shift(9, 1) == "xS");
    EXPECT(uf1Plain(9, 1) == "U0");
    EXPECT(uf1Plain(8, 1) == "x");

    // The side-car's banks are ORC's.
    EXPECT(!bnd::dropUf1Bank(8, bnd::kUf1RmeBankBase, 0, SoftKeyDrop::Swap));
    EXPECT(!bnd::dropUf1Key(bnd::kUf1RmeBankBase, 0, 8, 0, 0, SoftKeyDrop::Swap));
    EXPECT(!bnd::dropUf1Bank(8, 8, 0, SoftKeyDrop::Swap));
    EXPECT(!bnd::dropUf1Key(8, 0, 8, 0, 0, SoftKeyDrop::Swap));
}

static void testReload()
{
    seedBanks();
    EXPECT(bnd::dropBank(L, A, 0, L, B, 1, 0, SoftKeyDrop::Swap));
    bnd::load();
    EXPECT(uf8Plain(L, B, 1, 0) == "A0");
    EXPECT(uf8Shift(L, B, 1, 0) == "A0s");
    EXPECT(uf8Plain(L, A, 0, 0) == "B0");
    EXPECT(bnd::getSubBankName(L, B, 1, 1) == "Bank A S");
    EXPECT(bnd::getSubBankLed(L, B, 1, 1).color[0] == 0x20);
    EXPECT(bnd::getSubBankDynamic(L, B, 1, 1) == bnd::DynamicBankKind::TrackColours);
}

static void testContent()
{
    EXPECT(!bnd::softKeyHasContent(bnd::Binding{}, 0));
    EXPECT(!bnd::softKeyHasContent(bnd::Binding{}, 1));
    EXPECT(bnd::softKeyHasContent(key("p"), 0));
    EXPECT(!bnd::softKeyHasContent(key("p"), 1));
    bnd::Binding shiftOnly;
    shiftOnly.shortPress[1].label = "s";
    EXPECT(bnd::softKeyHasContent(shiftOnly, 0));     // Plain carries the whole key
    EXPECT(bnd::softKeyHasContent(shiftOnly, 1));
}

int main()
{
    char tmpl[] = "/tmp/rs_skdropXXXXXX";
    static std::string dir = mkdtemp(tmpl);
    bnd::Host h;
    h.configDir  = [] { return dir; };
    h.configFile = [] { return std::string("bindings.json"); };
    bnd::setHost(std::move(h));
    bnd::load();

    testBankSwapPlain();
    testBankSwapShift();
    testBankCopy();
    testSets();
    testUf8Keys();
    testUf1();
    testReload();
    testContent();

    std::system(("rm -rf " + dir).c_str());
    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("softkey_drop: all passed\n");
    return 0;
}
