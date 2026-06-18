#include "VirtualNotch.h"

#include <algorithm>  // std::clamp
#include <cmath>

namespace uf8 {

double applyVirtualNotch(double cur, double delta, double center,
                         double zone, double lo, double hi)
{
    const double next = std::clamp(cur + delta, lo, hi);

    // Fast-spin: a single rotation crossed through the centre. Snap
    // so the user lands ON the notch instead of skipping past it.
    if ((cur - center) * (next - center) < 0.0) return center;

    // Any inward move that ends inside the zone snaps to the centre.
    // Moving AWAY from the centre stays free, so you can still leave the
    // notch and dial small off-centre values (e.g. -0.2 dB) by turning
    // outward from 0. The earlier "snap only on first entry from outside"
    // rule let a slow approach (or a small over/undershoot correction)
    // park inside the zone at e.g. -0.2 dB — this makes hitting exactly
    // 0 reliable while staying magnet-only (no sticky accumulator).
    const double dCur  = std::abs(cur  - center);
    const double dNext = std::abs(next - center);
    if (dNext <= zone && dNext < dCur) return center;

    return next;
}

double applyNotchHold(int slot, double cur, double delta, double center,
                      double zone, double hold, double lo, double hi)
{
    if (hold <= 0.0)
        return applyVirtualNotch(cur, delta, center, zone, lo, hi);

    constexpr int kSlots = 32;
    static bool   held[kSlots] = {false};
    static double acc [kSlots] = {0.0};
    if (slot < 0 || slot >= kSlots) slot = 0;

    const double next = std::clamp(cur + delta, lo, hi);
    const double dCur = std::abs(cur - center);

    // External move (mouse / automation) pulled us off centre → drop hold.
    if (held[slot] && dCur > zone) { held[slot] = false; acc[slot] = 0.0; }

    if (held[slot]) {
        // Parked on the detent: absorb rotation until it exceeds `hold`.
        acc[slot] += delta;
        if (std::abs(acc[slot]) < hold) return center;
        // Release outward, carrying the overshoot past the threshold.
        const double over = acc[slot] - (acc[slot] > 0.0 ? hold : -hold);
        held[slot] = false;
        acc[slot]  = 0.0;
        return std::clamp(center + over, lo, hi);
    }

    // Not held → magnet. Snap on cross or inward-into-zone, then latch.
    const double dNext = std::abs(next - center);
    if ((cur - center) * (next - center) < 0.0
        || (dNext <= zone && dNext < dCur)) {
        held[slot] = true;
        acc[slot]  = 0.0;
        return center;
    }
    return next;
}

} // namespace uf8
