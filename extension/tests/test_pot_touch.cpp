// A pot's Touch edit holds until Stop while the transport rolls (PotTouch.h).
#include "PotTouch.h"

#include <cstdio>

namespace pt = reasixty::pot_touch;

static int g_fail = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

int main()
{
    // Rolling: the edit holds however long the hand rests, and ends with Stop.
    const auto held = pt::holdUntil(1000, 250, /*rolling*/ true);
    EXPECT(held == pt::kHeld);
    EXPECT(pt::live(1000 + 60000, held, true));    // a minute later, still held
    EXPECT(!pt::live(1000 + 60000, held, false));  // transport stopped: let go

    // Stopped: the short window, as before.
    const auto win = pt::holdUntil(1000, 250, /*rolling*/ false);
    EXPECT(win == 1250);
    EXPECT(pt::live(1200, win, false));
    EXPECT(!pt::live(1300, win, false));
    // A window armed while stopped does not turn into a hold when play starts.
    EXPECT(!pt::live(1300, win, true));

    // Never armed.
    EXPECT(!pt::live(5, 0, true) && !pt::live(5, 0, false));

    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("test_pot_touch: all good\n");
    return 0;
}
