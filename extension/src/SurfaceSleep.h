#pragma once
//
// SurfaceSleep — the surfaces go dark when nobody has touched them for a while,
// and the first touch only wakes them. Shared by Rea-Sixty (main.cpp, UF8, UC1
// and UF1) and ORC (orc/Surface.cpp, the UF1), so both sleep the same way
// (Frank 02.10.2026: "einen Sleep wie in Rea-Sixty"). No REAPER here: what
// counts as "something is running" (REAPER's transport) is the host's answer.
//
// ⇨ SLEEP IS DARKNESS, NOT SILENCE. Every firmware falls back to its splash
// screen when the heartbeat stops, so sleep may not stop writing. It takes the
// brightness to zero and lets every other stream run, which is also why waking
// needs no repaint: the picture was never cleared, only unlit.
//
// Threading: wake() runs on the input threads (atomics only). tick() and set()
// run on the thread that owns the devices; `push(asleep)` is called there and
// does the device writes.

#include "UF1Protocol.h"

#include <atomic>
#include <cstdint>
#include <vector>

namespace surfsleep {

constexpr int kMinutesMin = 1;               // SSL's own range is 1..99
constexpr int kMinutesMax = 99;              // (uf8-manual-reference.md:55)

struct Clock {
    std::atomic<bool>    enabled{false};     // factory off
    std::atomic<int>     minutes{15};
    std::atomic<int64_t> lastInputMs{0};     // 0 = not stamped yet
    std::atomic<bool>    asleep{false};
    std::atomic<bool>    wakeRequest{false};   // the input event that woke, for tick()
    std::atomic<bool>    toggleRequest{false}; // "Sleep now", from a key on an input thread

    // An input event (a hand, never the fader's motor). True when THIS event woke
    // the surfaces: the caller drops it, so a hand reaching for a dark panel does
    // nothing it cannot see. The exchange elects exactly one waker.
    bool wake(int64_t nowMs)
    {
        lastInputMs.store(nowMs);
        if (!asleep.exchange(false)) return false;
        wakeRequest.store(true);
        return true;
    }

    // Dark or lit from an action rather than the clock. Pushes only on a change.
    template <class Push>
    void set(bool a, int64_t nowMs, Push&& push)
    {
        if (asleep.exchange(a) == a) return;
        lastInputMs.store(nowMs);
        push(a);
    }

    // Every tick. `busy`: something runs that counts as activity (REAPER's
    // transport); it holds the countdown, so it starts at zero when it stops.
    // ⇨ "Sleep now" is ahead of `busy`: asking for the dark by hand wins over a
    // rolling transport, which is the point of having the key during a take.
    template <class Push>
    void tick(int64_t nowMs, bool busy, Push&& push)
    {
        if (wakeRequest.exchange(false)) push(false);
        if (toggleRequest.exchange(false)) set(!asleep.load(), nowMs, push);
        if (busy) { lastInputMs.store(nowMs); return; }
        if (!enabled.load() || asleep.load()) return;
        const int64_t last = lastInputMs.load();
        if (last == 0) { lastInputMs.store(nowMs); return; }   // first tick
        if (nowMs - last < static_cast<int64_t>(minutes.load()) * 60000) return;
        asleep.store(true);
        push(true);
    }
};

// ⇨ THE UF1's THREE LIGHTS. The LED master (0x2D), the colour screen (0x4F),
// and the panel master (0x47), which is the only one that darkens the small
// channel LCD beside the fader (proven at the device 2026-09-07). Sleep owns the
// panel master: 0 asleep, full awake. `led` / `lcd` are the levels to wake to.
constexpr uint8_t kUf1MasterFull = 0xFF;
// SSL's cold-start levels (uf1_init_sequence.inc:158-159, UF1Protocol.h): what
// the UF1 shows when nothing else sets a level, ORC's case.
constexpr uint8_t kUf1InitLed = 0x10;
constexpr uint8_t kUf1InitLcd = 0x32;

inline std::vector<std::vector<uint8_t>> uf1Frames(bool asleep, uint8_t led, uint8_t lcd)
{
    return { uf1::buildMasterBrightness(asleep ? 0x00 : kUf1MasterFull),
             uf1::buildLedBrightness(asleep ? 0x00 : led),
             uf1::buildLcdBrightness(asleep ? 0x00 : lcd) };
}

// The press that woke the surface is swallowed with its release. Keyed by the
// device's button id. A press that did not wake clears a stale mark, because
// the UF1 is known to drop releases (Bindings.cpp, wakeSwallow_).
struct WakeSwallow {
    std::atomic<bool> held[256] = {};
    // True when this button event must not reach anything.
    bool button(uint8_t id, bool pressed, bool woke)
    {
        if (pressed) {
            held[id].store(woke);
            return woke;
        }
        return held[id].exchange(false);
    }
};

} // namespace surfsleep
