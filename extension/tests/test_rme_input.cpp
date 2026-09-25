// What the UF1 does to TotalMix: src/RmeInput.cpp.
//
// ⇨ THE POINT OF THIS TEST IS THAT IT CAN EXIST. Until 2026-09-25 this logic sat
// inside the extension's main.cpp and could not be run without REAPER, a UF1 and
// a mixer. RmeInput takes a state and an event and fills a list of addresses, so
// a knob turn can be checked the way a painted frame can (test_orc_paint).
//
// ⛔ WHAT IS PINNED, AND WHY EACH ONE:
//  · a pot on a role TotalMix has not handed out writes NOTHING. A pot that
//    silently moved the wrong channel is the worst failure this code has.
//  · the address and the value of a turn, so a changed step or a swapped
//    faderlin/dB stands out.
//  · the channel encoder's four-counts-per-detent gathering, and that the
//    remainder lives in the caller's State. It used to be a function static,
//    which means two surfaces would have shared one remainder.
//  · that the host's fine factor reaches the step.
//  · that an unset host callback is a state and not a crash.

#include "RmeInput.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace reasixty::rme;
namespace in_ = reasixty::rme::input;

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

// A mixer with one output that carries Phones 1, one that carries Main, and two
// visible inputs. Enough for every path below.
static State fixture()
{
    State st;
    auto put = [](std::map<int, Channel>& m, int i, const char* n) {
        Channel c;
        c.name = n;
        c.colour = 4;       // any non-zero: 0 means hidden
        c.seen = true;
        m[i] = c;
    };
    put(st.outputs, 0, "Main");
    put(st.outputs, 6, "Phones 1");
    put(st.inputs, 30, "Kik");
    put(st.inputs, 31, "Snare");
    st.mainOut  = 0;
    st.phones[0] = 6;
    return st;
}

static Config config()
{
    Config c;
    c.vpots[0] = { "phones1", "volume", "submix" };
    c.vpots[1] = { "phones2", "volume", "mute" };   // phones2 is NOT handed out
    c.vpotStepDb = 0.5;
    c.jogTarget  = "main";
    c.jogStepDb  = 1.0;
    return c;
}

static ::uf1::InputEvent press(std::uint8_t id)
{
    ::uf1::InputEvent ev;
    ev.kind = ::uf1::InputKind::Button;
    ev.id = id;
    ev.pressed = true;
    return ev;
}

int main()
{
    const State st  = fixture();
    const Config cfg = config();
    const in_::Host bare{};   // nothing set: no mode menu, no fine, link up

    // ── a pot on a role the mixer has not handed out writes nothing ──────────
    {
        in_::State s;
        in_::Writes w;
        EXPECT(in_::encoder(s, bare, st, cfg, ::uf1::enc::kVpot2, +1, w));
        EXPECT(w.empty());
    }

    // ── a pot on a role it HAS handed out writes exactly one address ─────────
    {
        in_::State s;
        in_::Writes w;
        EXPECT(in_::encoder(s, bare, st, cfg, ::uf1::enc::kVpot1, +1, w));
        EXPECT(w.size() == 1);
        if (w.size() == 1) {
            // Phones 1 is output 6, and an output takes its level in dB.
            EXPECT(w[0].first.find("/output/6") != std::string::npos);
            // One detent up from 0 dB at 0.5 dB per detent.
            EXPECT(std::fabs(w[0].second - 0.5f) < 0.001f);
        }
    }

    // ── the fine factor from the host reaches the step ───────────────────────
    {
        in_::Host fine;
        fine.knobScale = [] { return 0.1; };
        in_::State s;
        in_::Writes w;
        in_::encoder(s, fine, st, cfg, ::uf1::enc::kVpot1, +1, w);
        EXPECT(w.size() == 1);
        if (w.size() == 1) EXPECT(std::fabs(w[0].second - 0.05f) < 0.001f);
    }

    // ── the channel encoder gathers four counts into one detent ──────────────
    {
        in_::State s;
        in_::Writes w;
        s.row.store(0);                       // Input: 30 and 31 are visible
        s.sel[0].store(30);
        for (int i = 0; i < 3; ++i)
            in_::encoder(s, bare, st, cfg, ::uf1::enc::kChannel, +1, w);
        EXPECT(s.sel[0].load() == 30);        // three counts is not a detent yet
        in_::encoder(s, bare, st, cfg, ::uf1::enc::kChannel, +1, w);
        EXPECT(s.sel[0].load() == 31);        // the fourth one moves it
        EXPECT(w.empty());                    // choosing a channel writes nothing
    }

    // ── ⛔ the remainder belongs to the State, not to the function ───────────
    // Two surfaces, one count each. If the accumulator were a function static,
    // the second call would see the first one's leftover and could step.
    {
        in_::State a, b;
        in_::Writes w;
        a.row.store(0); a.sel[0].store(30);
        b.row.store(0); b.sel[0].store(30);
        for (int i = 0; i < 3; ++i) {
            in_::encoder(a, bare, st, cfg, ::uf1::enc::kChannel, +1, w);
            in_::encoder(b, bare, st, cfg, ::uf1::enc::kChannel, +1, w);
        }
        EXPECT(a.sel[0].load() == 30);
        EXPECT(b.sel[0].load() == 30);
    }

    // ── a direction change throws the remainder away ─────────────────────────
    {
        in_::State s;
        in_::Writes w;
        s.row.store(0); s.sel[0].store(31);
        for (int i = 0; i < 3; ++i)
            in_::encoder(s, bare, st, cfg, ::uf1::enc::kChannel, +1, w);
        for (int i = 0; i < 3; ++i)
            in_::encoder(s, bare, st, cfg, ::uf1::enc::kChannel, -1, w);
        EXPECT(s.sel[0].load() == 31);   // neither direction reached a detent
    }

    // ── push = mute writes the mute address, inverted ────────────────────────
    {
        in_::State s;
        in_::Writes w;
        // vpots[1] is phones2, which is not handed out, so nothing happens.
        in_::button(s, bare, st, cfg, press(::uf1::btn::kVpot2Push), w);
        EXPECT(w.empty());
    }

    // ── the jog wheel moves its own target with its own step ─────────────────
    {
        in_::State s;
        in_::Writes w;
        EXPECT(in_::encoder(s, bare, st, cfg, ::uf1::enc::kJog, +1, w));
        EXPECT(w.size() == 1);
        if (w.size() == 1) {
            EXPECT(w[0].first.find("/output/0") != std::string::npos);  // Main
            EXPECT(std::fabs(w[0].second - 1.0f) < 0.001f);             // jogStepDb
        }
    }

    // ── nav up and down step the row, with wrap ──────────────────────────────
    {
        in_::State s;
        in_::Writes w;
        s.row.store(2);
        in_::button(s, bare, st, cfg, press(::uf1::btn::kNavDown), w);
        EXPECT(s.row.load() == 0);       // Output wraps to Input
        in_::button(s, bare, st, cfg, press(::uf1::btn::kNavUp), w);
        EXPECT(s.row.load() == 2);
    }

    // ── the nav centre toggles TotalMix' window and remembers it ─────────────
    {
        in_::State s;
        in_::Writes w;
        in_::button(s, bare, st, cfg, press(::uf1::btn::kNavCentre), w);
        EXPECT(s.windowShown.load());
        EXPECT(w.size() == 1);
        if (w.size() == 1) {
            EXPECT(w[0].first == "/showwindow");
            EXPECT(w[0].second == 1.0f);
        }
        w.clear();
        in_::button(s, bare, st, cfg, press(::uf1::btn::kNavCentre), w);
        EXPECT(!s.windowShown.load());
        EXPECT(w.size() == 1 && w[0].second == 0.0f);
    }

    // ── an offline link does not toggle the window ───────────────────────────
    {
        in_::Host down;
        down.online = [] { return false; };
        in_::State s;
        in_::Writes w;
        in_::button(s, down, st, cfg, press(::uf1::btn::kNavCentre), w);
        EXPECT(!s.windowShown.load());
        EXPECT(w.empty());
    }

    // ── while the mode menu is open, the keys are not ours ───────────────────
    {
        in_::Host menu;
        menu.modeMenuOpen = [] { return true; };
        in_::State s;
        in_::Writes w;
        s.row.store(2);
        in_::button(s, menu, st, cfg, press(::uf1::btn::kNavDown), w);
        EXPECT(s.row.load() == 2);
    }

    // ── the transport keys fall through, everything else is swallowed ────────
    {
        in_::State s;
        in_::Writes w;
        EXPECT(!in_::button(s, bare, st, cfg, press(::uf1::btn::kPlay), w));
        EXPECT(!in_::button(s, bare, st, cfg, press(::uf1::btn::kShift), w));
        EXPECT(in_::button(s, bare, st, cfg, press(::uf1::btn::kCut), w));
    }

    if (g_fail == 0) std::printf("test_rme_input: all good\n");
    return g_fail ? 1 : 0;
}
