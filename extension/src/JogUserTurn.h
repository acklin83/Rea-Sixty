#pragma once
//
// JogUserTurn: the arithmetic of a turn in one of the UF1's own jog modes
// (docs/uf1-user-jog-modes-plan.md). Pure; main.cpp fires the bindings,
// tests/test_jog_user_turn.cpp is the table.
//
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace jogusr {

// Detents in, firings out. `accum` carries the remainder between calls, so a
// divider of 3 fires once every three detents however they arrive. A turn the
// other way drops the remainder: half a step left over from the right must not
// fire the first step back to the left. Returns signed firings.
inline int divide(double& accum, int count, double divider)
{
    if (count == 0) return 0;
    if (!(divider >= 1.0)) divider = 1.0;
    if ((count > 0 && accum < 0.0) || (count < 0 && accum > 0.0)) accum = 0.0;
    accum += count / divider;
    // A hair under a whole step still counts (3 x 1/3 is 0.999…).
    constexpr double kEps = 1e-9;
    int fires = 0;
    if (accum >= 1.0 - kEps)       fires = static_cast<int>(std::floor(accum + kEps));
    else if (accum <= -1.0 + kEps) fires = static_cast<int>(std::ceil(accum - kEps));
    accum -= fires;
    if (std::fabs(accum) < kEps) accum = 0.0;
    return fires;
}

// One relative adjustment as REAPER's own OSC surface sends it to
// KBD_OnMainActionEx with relmode 1 (reaper_csurf/csurf_osc.cpp,
// encode_relmode1_extended, for whole steps): the step as a 7-bit two's
// complement (+1 = 1, -1 = 127), valhw -1. One call carries -64..+63.
struct Rel1 { int val; int valhw; };
inline Rel1 relmode1(int step)
{
    const int v = std::clamp(step, -64, 63);
    return { v & 0x7F, -1 };
}

// A larger step in calls of at most what one call carries, same sign. Writes
// up to `cap` steps into out and returns how many.
inline int splitRel(int step, int* out, int cap)
{
    int n = 0;
    while (step != 0 && n < cap) {
        const int part = std::clamp(step, -64, 63);
        out[n++] = part;
        step -= part;
    }
    return n;
}

} // namespace jogusr
