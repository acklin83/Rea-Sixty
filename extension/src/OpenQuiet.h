#pragma once
//
// OpenQuiet: right after a surface opens, its turns are ignored for a moment.
//
// ⇨ WHY (Frank 25.11.2026, "jetzt sind die pans wieder verschoben, hab nix
// gemacht"): when the USB tree came back at 13:38:30 the UF1's jog reported 16
// detents within its first 1.6 s and the UF8 one or two on V-Pots 2-6 three
// seconds after ORC opened it. rme_pan.log showed ORC writing those as pans of
// five outputs at once; nobody had touched a thing. The window is open while
// the device's init sequence runs and for kOpenQuietMs after it ends, so a long
// init cannot outrun it.
//
// Only turns: keys, pushes and a fader under the hand work at once.
//
// Pure and header-only: no clock of its own except steadyNowMs(), the steady
// clock in ms every caller here already uses. UF8Device and UF1Device own one
// each and mark the input they deliver (InputSettling below), so a handler on
// the device's own thread asks inputSettling() and needs no pointer to the
// device (Rea-Sixty's g_uf1_dev may be swapped under the main thread's lock).
//
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace reasixty {

constexpr std::int64_t kOpenQuietMs = 5000;

inline std::int64_t steadyNowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct OpenQuiet {
    std::atomic<bool>         init{false};
    std::atomic<std::int64_t> openedMs{-1};
    std::atomic<std::int64_t> untilMs{0};

    // The device opened at `nowMs`; its init sequence starts.
    void opened(std::int64_t nowMs)
    {
        openedMs.store(nowMs);
        untilMs.store(nowMs + kOpenQuietMs);
        init.store(true);
    }
    // Its init sequence ended (done, failed or cut short) at `nowMs`.
    void initDone(std::int64_t nowMs)
    {
        untilMs.store(std::max(untilMs.load(), nowMs + kOpenQuietMs));
        init.store(false);
    }
    // True while a turn from this device is to be ignored.
    bool quiet(std::int64_t nowMs) const
    {
        return openedMs.load() >= 0 && (init.load() || nowMs < untilMs.load());
    }
    // Milliseconds since the last open, -1 if it never opened.
    std::int64_t sinceOpen(std::int64_t nowMs) const
    {
        const std::int64_t o = openedMs.load();
        return o < 0 ? -1 : nowMs - o;
    }
};

// What the device delivering input on this thread said about itself. Set by
// the device's read callback around its handlers, read by the handlers.
struct SettlingInput {
    bool         settling = false;
    std::int64_t sinceOpenMs = -1;
};
inline thread_local SettlingInput t_settlingInput;

// The device's read callback holds one of these while its handlers run.
class InputSettling {
public:
    explicit InputSettling(const OpenQuiet& q)
        : prev_(t_settlingInput)
    {
        const std::int64_t now = steadyNowMs();
        t_settlingInput = { q.quiet(now), q.sinceOpen(now) };
    }
    ~InputSettling() { t_settlingInput = prev_; }
    InputSettling(const InputSettling&) = delete;
    InputSettling& operator=(const InputSettling&) = delete;
private:
    SettlingInput prev_;
};

// True when the input being handled comes from a surface that just opened.
inline bool inputSettling() { return t_settlingInput.settling; }
inline std::int64_t inputSinceOpenMs() { return t_settlingInput.sinceOpenMs; }

}  // namespace reasixty
