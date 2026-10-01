// LaneModel: the play set, stepping, A/B, the razor on one lane and the comp map.
// Chunk lines and razor strings as REAPER 7.81 wrote them in the probe of 01.10.2026
// (rea_sixty_lanes_sonde.lua).

#include "LaneModel.h"

#include <cmath>
#include <cstdio>

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

using namespace lanes;

static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }
static PlaySet S(std::initializer_list<int> v) { PlaySet s; for (int x : v) s.push_back((char)x); return s; }

int main()
{
    // ---- play set <-> C_LANEPLAYS ------------------------------------------------
    // Probe: one lane plays = 1, two lanes (Cmd-click) = 2 each.
    EXPECT(same(fromLanePlays({1, 0, 0, 0}), S({1, 0, 0, 0})));
    EXPECT(same(fromLanePlays({0, 2, 2, 0}), S({0, 1, 1, 0})));
    EXPECT((toLanePlays(S({0, 1, 0, 0})) == std::vector<int>{0, 1, 0, 0}));
    EXPECT((toLanePlays(S({1, 0, 1, 0})) == std::vector<int>{2, 0, 2, 0}));
    EXPECT((toLanePlays(S({0, 0, 0})) == std::vector<int>{0, 0, 0}));
    EXPECT((toLanePlays(all(3)) == std::vector<int>{2, 2, 2}));
    EXPECT((toLanePlays(only(4, 2)) == std::vector<int>{0, 0, 1, 0}));
    EXPECT(countOn(only(4, 7)) == 0);
    EXPECT(countOn(none(5)) == 0);

    // Silent lanes past the end do not make two sets different.
    EXPECT(same(S({1, 0}), S({1, 0, 0, 0})));
    EXPECT(!same(S({1, 0}), S({1, 0, 0, 1})));

    // Toggle: exclusive becomes a set, all but one out is exclusive again, the last
    // one out leaves nothing.
    PlaySet t = only(4, 0);
    t = toggled(t, 2);
    EXPECT(same(t, S({1, 0, 1, 0})));
    EXPECT((toLanePlays(t) == std::vector<int>{2, 0, 2, 0}));
    t = toggled(t, 0);
    EXPECT((toLanePlays(t) == std::vector<int>{0, 0, 1, 0}));
    t = toggled(t, 2);
    EXPECT(countOn(t) == 0);
    EXPECT(same(toggled(S({1}), 3), S({1, 0, 0, 1})));

    // Group pattern: lanes 1, 3, 4 on a mate with 3 lanes = 1 and 3; on a mate with
    // one lane that never had lane 0 on, nothing to do.
    EXPECT(same(pattern(S({1, 0, 1, 1}), 3), S({1, 0, 1})));
    EXPECT(pattern(S({0, 0, 1, 1}), 2).empty());
    EXPECT(same(pattern(S({0, 1}), 4), S({0, 1, 0, 0})));

    // ---- stepping -----------------------------------------------------------------
    EXPECT(heardLane(S({0, 1, 1, 0}), 2) == 2);        // remembered and still on
    EXPECT(heardLane(S({0, 1, 1, 0}), 3) == 1);        // remembered went silent
    EXPECT(heardLane(S({0, 0, 0, 0}), 1) == -1);

    // 4 lanes, no comp lane, no wrap.
    EXPECT(stepLane(4, 0, +1, -1, true) == 1);
    EXPECT(stepLane(4, 3, +1, -1, true) == -1);
    EXPECT(stepLane(4, 0, -1, -1, true) == -1);
    EXPECT(stepLane(4, 2, -1, -1, true) == 1);
    // Nothing plays: down starts at the top, up at the bottom.
    EXPECT(stepLane(4, -1, +1, -1, true) == 0);
    EXPECT(stepLane(4, -1, -1, -1, true) == 3);
    // Comp lane 0 (probe): skipped when asked, reachable when not.
    EXPECT(stepLane(4, 1, -1, 0, true) == -1);
    EXPECT(stepLane(4, 1, -1, 0, false) == 0);
    EXPECT(stepLane(4, -1, +1, 0, true) == 1);
    EXPECT(stepLane(5, 1, +1, 2, true) == 3);
    EXPECT(stepLane(0, -1, +1, -1, true) == -1);
    EXPECT(stepLane(1, 0, +1, 0, true) == -1);

    // ---- A/B ----------------------------------------------------------------------
    {   // Versions: Finger, Pick, Synth (Fall 6). A/B swaps the last two.
        LaneAb ab;
        EXPECT(ab.recall(3).empty());
        ab.observe(only(3, 0));
        EXPECT(ab.recall(3).empty());
        ab.observe(only(3, 1));
        ab.observe(only(3, 2));
        EXPECT(same(ab.recall(3), only(3, 1)));
        ab.observe(only(3, 1));                         // our own write comes back
        EXPECT(same(ab.recall(3), only(3, 2)));
        ab.observe(only(3, 2));
        EXPECT(same(ab.recall(3), only(3, 1)));
    }
    {   // Doubling 1 + 3, preview 4 and 2 with the wheel, A/B = the doubling (Fall 7).
        LaneAb ab;
        ab.observe(S({1, 0, 1, 0}));
        ab.observe(only(4, 3));
        ab.observe(only(4, 1));
        EXPECT(same(ab.recall(4), S({1, 0, 1, 0})));
        ab.observe(S({1, 0, 1, 0}));
        // And back to the lane last previewed.
        EXPECT(same(ab.recall(4), only(4, 1)));
    }
    {   // A new layering replaces the old one as partner.
        LaneAb ab;
        ab.observe(S({1, 0, 1, 0}));
        ab.observe(only(4, 0));
        ab.observe(S({1, 1, 0, 0}));
        EXPECT(same(ab.recall(4), only(4, 0)));
    }
    {   // Punch-in: REAPER adds lane 7 and plays it; A/B brings the comp lane back
        // (Fall 8). The set grew from 6 to 7 lanes.
        LaneAb ab;
        ab.observe(only(6, 0));
        ab.observe(only(7, 6));
        EXPECT(same(ab.recall(7), only(7, 0)));
    }
    {   // The partner lane was deleted meanwhile: nothing to go back to.
        LaneAb ab;
        ab.observe(only(4, 3));
        ab.observe(only(4, 0));
        EXPECT(ab.recall(2).empty());
        EXPECT(same(ab.cur, only(4, 0)));               // and nothing was swapped
    }

    // ---- razor on one lane --------------------------------------------------------
    // Probe: lane 1 of 4 = 0.25 .. 0.5, lane 2 of 4 = 0.5 .. 0.75.
    EXPECT(near(laneSpan(1, 4).top, 0.25) && near(laneSpan(1, 4).bottom, 0.5));
    EXPECT(near(laneSpan(2, 4).top, 0.5) && near(laneSpan(2, 4).bottom, 0.75));
    EXPECT(near(laneSpan(0, 3).bottom, 1.0 / 3));
    EXPECT(razorEntry(1.2898918058, 2.7540933152, 1, 4)
           == "1.2898918058 2.7540933152 \"\" 0.250000 0.500000");
    EXPECT(razorEntry(0, 4, 2, 4) == "0 4 \"\" 0.500000 0.750000");

    // ---- the comp map -------------------------------------------------------------
    // Probe, "Comping aus": LANEREC -1 -1 -1 0, no areas.
    const char* off =
        "<TRACK\n  NAME Test\n  FREEMODE 2\n  FIXEDLANES 0 1 0 0 0\n"
        "  LANESOLO 4294967281 4294967295 4294967295 4294967295\n"
        "  LANEREC -1 -1 -1 0\n  LANENAME 1 2 3 4\n  ITEMLANES 4\n"
        "  <ITEM\n    POSITION 0\n    LENGTH 4.061\n  >\n>\n";
    CompInfo ci = compFromChunk(off);
    EXPECT(ci.compLane == -1);
    EXPECT(ci.areas.empty());

    // Probe, "Comping an": comp lane 0, three areas from lanes 1, 2, 3.
    const char* on =
        "<TRACK\r\n  NAME Test\r\n  FREEMODE 2\r\n  LANEREC -1 0 -1 0\r\n"
        "  LINKEDLANE 0.73210075466899 2.38061196749959 2 0 -1 0.01 0.01\r\n"
        "  LINKEDLANE 0.34861940698523 0.74210075466899 1 0 -1 0.01 0.01\r\n"
        "  LINKEDLANE 2.37061196749959 4.07884706172724 3 0 -1 0.01 0.01\r\n"
        "  <FXCHAIN\n    LINKEDLANE 9 10 0 0 -1 0.01 0.01\n    LANEREC -1 3 -1 0\n  >\n"
        "  <ITEM\n    POSITION 0.349\n  >\n>\n";
    ci = compFromChunk(on);
    EXPECT(ci.compLane == 0);
    EXPECT(ci.areas.size() == 3);
    if (ci.areas.size() == 3) {
        EXPECT(near(ci.areas[0].start, 0.34861940698523) && ci.areas[0].srcLane == 1);
        EXPECT(ci.areas[1].srcLane == 2 && ci.areas[2].srcLane == 3);
        EXPECT(near(ci.areas[2].end, 4.07884706172724));
    }
    EXPECT(compFromChunk(nullptr).compLane == -1);
    EXPECT(compFromChunk("").areas.empty());
    EXPECT(compFromChunk("<TRACK\nLANEREC\nLINKEDLANE 1\n>").areas.empty());
    EXPECT(compFromChunk("<TRACK\nNAME \"LANEREC -1 2\"\n>").compLane == -1);

    // Which area plays: the later one inside the 10 ms overlap.
    EXPECT(areaAt(ci.areas, 0.2) == -1);
    EXPECT(areaAt(ci.areas, 0.5) == 0);
    EXPECT(areaAt(ci.areas, 0.735) == 1);
    EXPECT(areaAt(ci.areas, 0.745) == 1);
    EXPECT(areaAt(ci.areas, 3.0) == 2);
    EXPECT(areaAt(ci.areas, 4.1) == -1);

    // ← / → stops: one per cut, on the later area's start.
    const std::vector<double> es = edges(ci.areas, {0.0});
    EXPECT(es.size() == 5);
    if (es.size() == 5) {
        EXPECT(near(es[0], 0.0));
        EXPECT(near(es[1], 0.34861940698523));
        EXPECT(near(es[2], 0.73210075466899));
        EXPECT(near(es[3], 2.37061196749959));
        EXPECT(near(es[4], 4.07884706172724));
    }
    EXPECT(near(nextEdge(es, 0.5), 0.73210075466899));
    EXPECT(near(nextEdge(es, 0.73210075466899), 2.37061196749959));   // not stuck
    EXPECT(near(prevEdge(es, 0.73210075466899), 0.34861940698523));
    EXPECT(near(prevEdge(es, 0.74), 0.73210075466899));
    EXPECT(near(nextEdge(es, 5.0), 5.0));                               // past the end
    EXPECT(near(prevEdge(es, 0.0), 0.0));

    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("lane_model: all passed\n");
    return 0;
}
