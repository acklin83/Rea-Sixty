// Uf8ModeViews.h: what a DynaMount stand and a Hue lamp show on a UF8 strip,
// shared by Rea-Sixty and ORC (docs/orc-uf8-plan.md stage 6). The texts are the
// ones main.cpp painted before 29.10.2026.

#include "Uf8ModeViews.h"

#include <cstdio>

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

int main()
{
    // ── a stand ──────────────────────────────────────────────────────────────
    {
        uf8::dynamount::DynaMountManager dm;
        dm.setMount(2, true, "Kick In", "10.0.0.3", 1);
        uf8::dynamount::Calibration cal;
        cal.hMin = 10; cal.hMax = 90; cal.vMin = 0; cal.vMax = 100; cal.valid = true;
        dm.setCalibration(2, cal);
        dm.setTargets(2, /*h*/30, /*r*/90, /*v*/40, false);

        const auto c = uf8modes::dyna(dm, 2, /*flipped*/ false, false, 0.0);
        EXPECT(c.upper == "Kick In");
        EXPECT(c.csType == "DYNA");
        EXPECT(c.chanNum == "3");
        EXPECT(c.faderText == "X40");                        // no FLIP: X on the fader
        EXPECT(c.valueLine == "X40  Y30  R90  OFF");         // never detected: offline
        EXPECT(c.ring == 0.5);                               // 90 of 180

        const auto f = uf8modes::dyna(dm, 2, /*flipped*/ true, false, 0.0);
        EXPECT(f.faderText == "Y30");                        // FLIP: distance

        // Under the hand: the readout follows the fader, not the stand.
        const auto p = uf8modes::dyna(dm, 2, false, /*pending*/ true, 0.25);
        EXPECT(p.faderText == "X25");
        const auto q = uf8modes::dyna(dm, 2, true, true, 1.0);   // top = nearest
        EXPECT(q.faderText == "Y10");

        // A long name is cut to the scribble, an empty one says MOUNT.
        dm.setMount(3, true, "Overhead Left", "10.0.0.4", 1);
        EXPECT(uf8modes::dyna(dm, 3, false, false, 0.0).upper.size() == uf8::kUf8ScribbleChars);
        dm.setMount(4, true, "", "10.0.0.5", 1);
        EXPECT(uf8modes::dyna(dm, 4, false, false, 0.0).upper == "MOUNT");
    }
    // ── a lamp ───────────────────────────────────────────────────────────────
    {
        uf8::hue::HueManager hm;
        uf8::hue::SlotConfig lamp;
        lamp.enabled = true; lamp.label = "Desk"; lamp.kind = uf8::hue::TargetKind::Light;
        lamp.rid = "abc";
        hm.setSlot(0, lamp);
        uf8::hue::SlotConfig zone;
        zone.enabled = true; zone.kind = uf8::hue::TargetKind::Group; zone.rid = "def";
        hm.setSlot(1, zone);

        const auto a = uf8modes::hue(hm, 0, false);
        EXPECT(a.upper == "Desk");
        EXPECT(a.csType == "LAMP");
        EXPECT(a.chanNum == "1");
        EXPECT(a.faderText == "OFF");                        // nothing heard: off
        const auto b = uf8modes::hue(hm, 1, false);
        EXPECT(b.csType == "ZONE");
        EXPECT(b.upper == "LAMP");                           // no label, no bridge name
        EXPECT(b.valueLine.size() <= 19);
    }

    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("uf8_mode_views: all passed\n");
    return 0;
}
