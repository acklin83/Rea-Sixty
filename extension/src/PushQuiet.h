#pragma once
//
// PushQuiet — after a pot is pushed, its turns are ignored for a moment.
//
// ⇨ WHY (Frank 2026-05-29 on the UF8, 2026-09-29 for the UF1 and the RME
// side-car): the small twist that rides along with a press nudges the value
// straight back off the default the push just set. The UF8 has had its own
// 250 ms window since May (g_vpotPushSuppressUntilMs in main.cpp); this is the
// same rule for the pots that had none, one place for all of them.
//
// Pure and header-only: no REAPER, no clock of its own. The caller passes the
// time in ms, so tests can drive it. Atomics, because the side-car's push and
// turn arrive on the device thread while a painter may read nothing of it.
//
#include <array>
#include <atomic>
#include <cstdint>

namespace reasixty {

// The one window, the same as the UF8's since 2026-05-29.
constexpr std::int64_t kPushQuietMs = 250;

template <int N>
struct PushQuiet {
    std::array<std::atomic<std::int64_t>, N> until{};

    // Pot `i` was pushed at `nowMs`: its turns are ignored until the window ends.
    void arm(int i, std::int64_t nowMs)
    {
        if (i >= 0 && i < N) until[static_cast<std::size_t>(i)].store(nowMs + kPushQuietMs);
    }
    // True while a turn of pot `i` is to be ignored.
    bool quiet(int i, std::int64_t nowMs) const
    {
        return i >= 0 && i < N && nowMs < until[static_cast<std::size_t>(i)].load();
    }
};

}  // namespace reasixty
