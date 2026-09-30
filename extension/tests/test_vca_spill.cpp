// VcaSpill: members, top leads and the chain of spilled leads (Frank
// 30.09.2026). A small project as a table of VCA lead / follow bits.

#include "VcaSpill.h"

#include <cstdio>
#include <map>

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

using vca::Groups;
using vca::Step;

static Groups g(int a, int b = 0)
{
    Groups r;
    for (int n : {a, b}) if (n >= 1 && n <= 128) r.w[(n - 1) / 32] |= 1u << ((n - 1) % 32);
    return r;
}

int main()
{
    // Tracks 1..9:
    //   1 Drums VCA     leads 1
    //   2 Kick          follows 1
    //   3 Snare         follows 1
    //   4 Toms VCA      leads 2, follows 1      (chained)
    //   5 Tom 1         follows 2
    //   6 Tom 2         follows 2
    //   7 Vox VCA       leads 100               (a high group)
    //   8 Vox           follows 100 and 1       (in two VCAs)
    //   9 Bass          nothing
    std::map<int, Groups> lead{ {1, g(1)}, {4, g(2)}, {7, g(100)} };
    std::map<int, Groups> follow{ {2, g(1)}, {3, g(1)}, {4, g(1)}, {5, g(2)},
                                  {6, g(2)}, {8, g(100, 1)} };
    auto leadOf   = [&](int t) { auto it = lead.find(t);   return it == lead.end()   ? Groups{} : it->second; };
    auto followOf = [&](int t) { auto it = follow.find(t); return it == follow.end() ? Groups{} : it->second; };
    const std::vector<int> all{1, 2, 3, 4, 5, 6, 7, 8, 9};

    // Group bits.
    EXPECT(g(100).has(100) && !g(100).has(99) && g(100).w[3] == (1u << 3));
    EXPECT(!Groups{}.any() && g(128).any() && g(128).has(128));
    EXPECT(g(1, 100).meets(g(100)) && !g(1).meets(g(2)));

    // Members in track order, the lead itself excluded, chained leads included.
    EXPECT((vca::members(all, 1, leadOf, followOf) == std::vector<int>{2, 3, 4, 8}));
    EXPECT((vca::members(all, 4, leadOf, followOf) == std::vector<int>{5, 6}));
    EXPECT((vca::members(all, 7, leadOf, followOf) == std::vector<int>{8}));
    EXPECT(vca::members(all, 9, leadOf, followOf).empty());

    // VCA Mode: the chained Toms VCA hides under Drums.
    EXPECT((vca::topLeads(all, leadOf, followOf) == std::vector<int>{1, 7}));

    // The chain.
    std::vector<int> c;
    EXPECT(vca::press(c, 9, false, false) == Step::None && c.empty());
    EXPECT(vca::press(c, 1, true, false) == Step::Enter && (c == std::vector<int>{1}));
    EXPECT(vca::press(c, 3, false, true) == Step::None && (c == std::vector<int>{1}));
    EXPECT(vca::press(c, 4, true, true) == Step::Deeper && (c == std::vector<int>{1, 4}));
    EXPECT(vca::press(c, 4, true, false) == Step::Back && (c == std::vector<int>{1}));
    EXPECT(vca::press(c, 4, true, true) == Step::Deeper && (c == std::vector<int>{1, 4}));
    EXPECT(vca::press(c, 1, true, false) == Step::Exit && c.empty());

    // Three levels: jump back to the middle one.
    c = {1, 4, 5};
    EXPECT(vca::press(c, 4, true, false) == Step::Jump && (c == std::vector<int>{1, 4}));
    // A lead outside the deepest level starts a new chain.
    EXPECT(vca::press(c, 7, true, false) == Step::Enter && (c == std::vector<int>{7}));

    // Prune at the first lead that went away.
    c = {1, 4, 5};
    EXPECT(vca::prune(c, [](int t) { return t != 4; }) && (c == std::vector<int>{1}));
    EXPECT(!vca::prune(c, [](int) { return true; }) && (c == std::vector<int>{1}));

    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("vca_spill: all passed\n");
    return 0;
}
