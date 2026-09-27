#pragma once
//
// PotTouch — how long a pot's Touch-automation edit lasts (Frank 27.09.2026).
//
// A pot has no touch sensor. While the transport rolls, its edit holds until
// Stop, like REAPER's Latch, so Touch never takes the value back under a hand
// that merely stopped turning. Stopped, a short window as before. Only a fader
// with a sensor lets go when the hand does. Pure, so a test can pin it; the
// extension passes REAPER's play state in (main.cpp, transportRolling_).
//
#include <cstdint>
#include <limits>

namespace reasixty::pot_touch {

constexpr std::int64_t kHeld = std::numeric_limits<std::int64_t>::max();

// Until when an edit made at `now` holds.
constexpr std::int64_t holdUntil(std::int64_t now, std::int64_t windowMs, bool rolling)
{
    return rolling ? kHeld : now + windowMs;
}

// Whether an edit that holds until `until` is still on. 0 = never armed.
constexpr bool live(std::int64_t now, std::int64_t until, bool rolling)
{
    if (until == kHeld) return rolling;
    return until != 0 && now <= until;
}

}  // namespace reasixty::pot_touch
