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

    // ---- loop around the cut ----------------------------------------------------
    {
        const std::vector<double> es = { 0.0, 18.98, 20.98, 22.98 };
        EXPECT(near(nearestEdge(es, 20.98), 20.98));     // on a cut
        EXPECT(near(nearestEdge(es, 21.5), 20.98));      // between: the nearer
        EXPECT(near(nearestEdge(es, 60.0), 22.98));      // far away: still the nearest
        EXPECT(near(nearestEdge({}, 5.0), 5.0));         // none: the cursor
        double s = 0, e = 0;
        loopAround(20.98, 2.0, s, e);
        EXPECT(near(s, 19.98) && near(e, 21.98));
        loopAround(0.5, 2.0, s, e);                      // not before the project
        EXPECT(near(s, 0.0) && near(e, 1.5));
        loopAround(10.0, 0.0, s, e);                     // nonsense length: 2 s
        EXPECT(near(s, 9.0) && near(e, 11.0));

        // Again at the same cut: off, repeat back to what it was.
        LoopToggle lt;
        EXPECT(lt.press(0, 0, 19.98, 21.98, 0) == LoopToggle::Act::Set);
        EXPECT(lt.press(19.98, 21.98, 19.98, 21.98, 1) == LoopToggle::Act::Clear);
        EXPECT(lt.repeatBefore == 0);
        // At another cut: it moves, and repeat still goes back to the first state.
        EXPECT(lt.press(0, 0, 19.98, 21.98, 1) == LoopToggle::Act::Set);
        EXPECT(lt.repeatBefore == 1);
        EXPECT(lt.press(19.98, 21.98, 21.98, 23.98, 1) == LoopToggle::Act::Set);
        EXPECT(lt.repeatBefore == 1);
        EXPECT(lt.press(21.98, 23.98, 21.98, 23.98, 1) == LoopToggle::Act::Clear);
        // A loop moved by hand in between: the next press sets, it does not clear.
        EXPECT(lt.press(0, 0, 19.98, 21.98, 0) == LoopToggle::Act::Set);
        EXPECT(lt.press(5.0, 9.0, 19.98, 21.98, 1) == LoopToggle::Act::Set);
        EXPECT(lt.repeatBefore == 1);
    }

    // ---- no slivers (Test.RPP, 02.10.2026) ------------------------------------
    {
        // After stroke 1 (lane 4 of the file's 0-based count = "5"): lane 7 goes on
        // 10 ms early. Stroke 2 starts where stroke 1 ended.
        const std::vector<CompArea> before = {
            {0.0, 18.99, 7}, {18.98, 20.98, 4}, {20.97, 85.37, 7} };
        double s = 20.98, e = 22.98;
        absorbSlivers(before, 5, s, e);
        EXPECT(near(s, 20.97) && near(e, 22.98));       // takes lane 7's 10 ms head
        // A stroke in the middle of a long area changes nothing.
        s = 30.0; e = 32.0;
        absorbSlivers(before, 5, s, e);
        EXPECT(near(s, 30.0) && near(e, 32.0));
        // Painted backwards up to the next area: the tail of the old one goes too.
        const std::vector<CompArea> after = {
            {0.0, 40.01, 7}, {40.0, 45.0, 3} };
        s = 38.0; e = 39.99;
        absorbSlivers(after, 5, s, e);
        EXPECT(near(s, 38.0) && near(e, 40.01));
        // The stroke's own lane is never a sliver to take.
        s = 20.98; e = 22.98;
        absorbSlivers({ {20.97, 85.37, 5} }, 5, s, e);
        EXPECT(near(s, 20.98));
    }

    // ---- painting --------------------------------------------------------------
    {
        double s = 0, e = 0;
        // Forward, 20 ms lead-in on both cuts.
        EXPECT(strokeRange(2.0, 3.5, 0.02, s, e));
        EXPECT(near(s, 1.98) && near(e, 3.48));
        // Backwards: the same stretch.
        EXPECT(strokeRange(3.5, 2.0, 0.02, s, e));
        EXPECT(near(s, 1.98) && near(e, 3.48));
        // Turned back onto the anchor: nothing to paint.
        EXPECT(!strokeRange(2.0, 2.0, 0.02, s, e));
        // At the project start the lead-in stops at zero.
        EXPECT(strokeRange(0.01, 1.0, 0.02, s, e));
        EXPECT(near(s, 0.0) && near(e, 0.98));
        // No lead-in.
        EXPECT(strokeRange(1.0, 2.0, 0.0, s, e));
        EXPECT(near(s, 1.0) && near(e, 2.0));
    }

    // ---- live comping ----------------------------------------------------------
    {
        LiveComp lc;
        LiveSection sec;
        // Fall 4: lane 2 at bar 1 (t 10), lane 5 at t 20, lane 1 at t 30, stop at 40.
        EXPECT(!lc.change(1, 10.0, sec));                 // first change only opens
        EXPECT(lc.change(4, 20.0, sec));
        EXPECT(sec.lane == 1 && near(sec.start, 10.0) && near(sec.end, 20.0));
        EXPECT(lc.change(0, 30.0, sec));
        EXPECT(sec.lane == 4 && near(sec.start, 20.0) && near(sec.end, 30.0));
        EXPECT(lc.close(40.0, sec));                      // stop closes the last one
        EXPECT(sec.lane == 0 && near(sec.start, 30.0) && near(sec.end, 40.0));
        EXPECT(!lc.open);
        EXPECT(!lc.close(41.0, sec));                     // nothing open, nothing written

        // A loop jump closes at the loop end and opens nothing: a pass without a
        // change writes nothing.
        EXPECT(!lc.change(2, 12.0, sec));
        EXPECT(lc.close(16.0, sec));
        EXPECT(sec.lane == 2 && near(sec.start, 12.0) && near(sec.end, 16.0));
        EXPECT(!lc.close(16.0, sec));

        // Several lanes (or the comp lane) play: the open section ends, none opens.
        EXPECT(!lc.change(3, 5.0, sec));
        EXPECT(lc.change(-1, 7.0, sec));
        EXPECT(sec.lane == 3 && near(sec.end, 7.0));
        EXPECT(!lc.open);
        EXPECT(!lc.change(1, 8.0, sec));                  // and the next change opens
        EXPECT(lc.open && lc.lane == 1);

        // A cut at the same moment it opened writes nothing.
        LiveComp z;
        EXPECT(!z.change(1, 3.0, sec));
        EXPECT(!z.change(2, 3.0, sec));
        EXPECT(z.lane == 2);

        // A cut that lies before the start (a seek back the tick did not see yet)
        // writes nothing either.
        EXPECT(!z.change(0, 2.0, sec));
    }
    // Passing lanes on the way (the wheel from take 2 to take 5): a lane shorter
    // than `settle` is not written, the one you stop on starts where it started.
    {
        LiveComp lc;
        LiveSection sec;
        EXPECT(!lc.change(1, 10.0, sec, 0.5));
        EXPECT(lc.change(2, 20.0, sec, 0.5));             // lane 1 ran 10 s: written
        EXPECT(sec.lane == 1 && near(sec.end, 20.0));
        EXPECT(!lc.change(3, 20.2, sec, 0.5));            // passed
        EXPECT(!lc.change(4, 20.4, sec, 0.5));            // passed
        EXPECT(lc.lane == 4 && near(lc.start, 20.0));
        EXPECT(lc.change(0, 30.0, sec, 0.5));
        EXPECT(sec.lane == 4 && near(sec.start, 20.0) && near(sec.end, 30.0));
        // Passing into a layered set drops the short one.
        EXPECT(!lc.change(-1, 30.2, sec, 0.5));
        EXPECT(!lc.open);
        // Stop writes what is open, however short.
        EXPECT(!lc.change(2, 31.0, sec, 0.5));
        EXPECT(lc.close(31.2, sec));
        EXPECT(sec.lane == 2 && near(sec.start, 31.0) && near(sec.end, 31.2));
    }
    // ---- the Lanes bank -----------------------------------------------------
    {
        using namespace lanes::bank;
        // A take with a name of its own, one without (REAPER gives its number),
        // the comp lane, past the end.
        Key k = key(0, 4, true, "Vox take 1", true, 3);
        EXPECT(k.present && k.label == "Vox take 1" && k.led == 2 && !k.comp);
        k = key(1, 4, true, "2", false, 3);
        EXPECT(k.present && k.label == "Lane 2" && k.led == 1);
        k = key(2, 4, true, "", false, 3);
        EXPECT(k.label == "Lane 3");
        k = key(3, 4, true, "1", true, 3);              // comp lane keeps "1" (probe)
        EXPECT(k.present && k.comp && k.label == "Comp" && k.led == 2);
        EXPECT(!key(4, 4, true, "", false, 3).present);
        // No lanes: key 0 offers them, the rest stay dark.
        k = key(0, 0, false, "", false, -1);
        EXPECT(k.present && k.label == "Lanes on" && k.led == 1);
        EXPECT(!key(1, 0, false, "", false, -1).present);

        EXPECT(op(0, 2, 4, true, 3) == Op::Solo);
        EXPECT(op(1, 2, 4, true, 3) == Op::Toggle);
        EXPECT(op(2, 2, 4, true, 3) == Op::CompHere);
        EXPECT(op(2, 3, 4, true, 3) == Op::None);       // no comp from the comp lane
        EXPECT(op(3, 2, 4, true, 3) == Op::None);
        EXPECT(op(4, 2, 4, true, 3) == Op::None);       // long: not yet
        EXPECT(op(0, 5, 4, true, 3) == Op::None);
        EXPECT(op(0, 3, 4, true, 3) == Op::Solo);       // push the comp key: hear the comp
        EXPECT(op(0, 0, 0, false, -1) == Op::LanesOn);
        EXPECT(op(1, 0, 0, false, -1) == Op::None);
        EXPECT(op(0, 1, 0, false, -1) == Op::None);

        // Comping: the takes light by the area under the cursor, not by playing.
        k = key(1, 4, true, "Rec", false, 3, /*comping*/ true, /*areaSrc*/ 1);
        EXPECT(k.led == 2);
        k = key(0, 4, true, "Edit", false, 3, true, 1);
        EXPECT(k.led == 1);
        k = key(3, 4, true, "1", true, 3, true, 1);      // the comp key: as it plays
        EXPECT(k.comp && k.led == 2);
        EXPECT(op(0, 1, 4, true, 3, true) == Op::AreaTake);
        EXPECT(op(1, 1, 4, true, 3, true) == Op::Audition);
        EXPECT(op(2, 1, 4, true, 3, true) == Op::CompHere);
        EXPECT(op(0, 3, 4, true, 3, true) == Op::Solo);   // push Comp: hear the comp
        EXPECT(op(4, 1, 4, true, 3, true) == Op::None);

        EXPECT(itemCount(14, true) == 14 && itemCount(0, false) == 1);
        EXPECT(pageOf(9, 8) == 1 && pageOf(7, 8) == 0 && pageOf(5, 4) == 1 && pageOf(-1, 4) == 0);
    }
    EXPECT(jumpedBack(16.0, 8.0));
    EXPECT(!jumpedBack(16.0, 15.98));                     // jitter
    EXPECT(!jumpedBack(16.0, 16.03));

    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("lane_model: all passed\n");
    return 0;
}
