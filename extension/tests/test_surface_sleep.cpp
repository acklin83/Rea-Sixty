// SurfaceSleep: the clock both programs sleep by, the UF1's three lights and the
// swallowed waking press. Time is handed in, so nothing here waits.

#include "SurfaceSleep.h"

#include <cstdio>
#include <vector>

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

int main()
{
    using surfsleep::Clock;
    std::vector<int> pushes;                       // 1 = dark, 0 = lit
    auto push = [&](bool a) { pushes.push_back(a ? 1 : 0); };
    const int64_t min = 60000;

    // ---- off from the factory: never dark ----------------------------------------
    {
        Clock c;
        c.tick(1000, false, push);
        c.tick(1000 + 120 * min, false, push);
        EXPECT(pushes.empty() && !c.asleep.load());
    }

    // ---- on, 15 minutes: dark after 15 minutes without a touch ---------------------
    {
        Clock c;
        c.enabled = true;
        pushes.clear();
        c.tick(1000, false, push);                 // first tick stamps
        c.tick(1000 + 14 * min, false, push);
        EXPECT(pushes.empty());
        c.tick(1000 + 15 * min, false, push);
        EXPECT(pushes.size() == 1 && pushes[0] == 1 && c.asleep.load());
        c.tick(1000 + 30 * min, false, push);      // stays dark, no second push
        EXPECT(pushes.size() == 1);

        // The first touch wakes and is the waker; the next one is not.
        EXPECT(c.wake(1000 + 31 * min));
        EXPECT(!c.wake(1000 + 31 * min + 10));
        c.tick(1000 + 31 * min + 20, false, push);
        EXPECT(pushes.size() == 2 && pushes[1] == 0 && !c.asleep.load());
        // And the countdown starts again from the touch.
        c.tick(1000 + 45 * min, false, push);
        EXPECT(pushes.size() == 2);
        c.tick(1000 + 46 * min + 10, false, push);
        EXPECT(pushes.size() == 3 && pushes[2] == 1);
    }

    // ---- busy (a rolling transport) holds the countdown; it starts at zero on stop -
    {
        Clock c;
        c.enabled = true;
        c.minutes = 1;
        pushes.clear();
        c.tick(0 + 1, false, push);
        c.tick(5 * min, true, push);               // rolled for five minutes
        EXPECT(pushes.empty());
        c.tick(5 * min + 30000, false, push);      // stopped half a minute ago
        EXPECT(pushes.empty());
        c.tick(6 * min + 1, false, push);
        EXPECT(pushes.size() == 1 && pushes[0] == 1);
    }

    // ---- a release is activity, never a wake ----------------------------------------
    {
        Clock c;
        pushes.clear();
        c.toggleRequest = true;                    // Sleep now, pressed
        c.tick(1000, false, push);
        EXPECT(c.asleep.load());
        EXPECT(!c.wake(1100, /*arrives*/ false));  // its key let go
        EXPECT(c.asleep.load() && c.lastInputMs.load() == 1100);
        c.tick(1200, false, push);
        EXPECT(pushes.size() == 1 && pushes[0] == 1);
        EXPECT(c.wake(1300));                      // the next press wakes
    }

    // ---- "Sleep now" wins over busy and toggles back -------------------------------
    {
        Clock c;
        pushes.clear();
        c.toggleRequest = true;
        c.tick(1000, true, push);
        EXPECT(pushes.size() == 1 && pushes[0] == 1 && c.asleep.load());
        c.toggleRequest = true;
        c.tick(2000, true, push);
        EXPECT(pushes.size() == 2 && pushes[1] == 0 && !c.asleep.load());
        // set() pushes only on a change.
        c.set(false, 3000, push);
        EXPECT(pushes.size() == 2);
    }

    // ---- the UF1's three lights ----------------------------------------------------
    {
        const auto dark = surfsleep::uf1Frames(true, 0x10, 0x32);
        EXPECT(dark.size() == 3);
        EXPECT(dark[0] == uf1::buildMasterBrightness(0x00));
        EXPECT(dark[1] == uf1::buildLedBrightness(0x00));
        EXPECT(dark[2] == uf1::buildLcdBrightness(0x00));
        const auto lit = surfsleep::uf1Frames(false, surfsleep::kUf1InitLed, surfsleep::kUf1InitLcd);
        EXPECT(lit[0] == uf1::buildMasterBrightness(0xFF));
        EXPECT(lit[1] == uf1::buildLedBrightness(0x10));
        EXPECT(lit[2] == uf1::buildLcdBrightness(0x32));
    }

    // ---- the waking press is swallowed with its release ----------------------------
    {
        surfsleep::WakeSwallow s;
        EXPECT(s.button(0x30, true, true));        // woke
        EXPECT(s.button(0x30, false, false));      // its release
        EXPECT(!s.button(0x30, true, false));      // the next press works
        EXPECT(!s.button(0x30, false, false));
        // A lost release does not eat the next one: the next press clears it.
        EXPECT(s.button(0x31, true, true));
        EXPECT(!s.button(0x31, true, false));
        EXPECT(!s.button(0x31, false, false));
    }

    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("surface_sleep: all passed\n");
    return 0;
}
