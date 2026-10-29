// Uf8Rme: TotalMix on the UF8's eight strips (docs/orc-uf8-plan.md 2.1, 2.4,
// 2.6). Pinned: the window and that it follows the UF1's selection, SEL is the
// UF1's selection, what a fader / V-Pot / CUT / SOLO writes, the readouts, the
// channel view, Main on the channel encoder.

#include "Uf8Rme.h"

#include <cstdio>
#include <string>

using namespace reasixty::rme;
namespace u8 = reasixty::rme::uf8side;
namespace in_ = reasixty::rme::input;
namespace rmeu = reasixty::rme::uf1;

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

static bool has(const u8::Writes& w, const std::string& addr)
{
    for (const auto& p : w) if (p.first == addr) return true;
    return false;
}
static float val(const u8::Writes& w, const std::string& addr)
{
    for (const auto& p : w) if (p.first == addr) return p.second;
    return -12345.0f;
}

// Twelve inputs, Main and Phones 1 as outputs.
static State fixture()
{
    State st;
    auto put = [](std::map<int, Channel>& m, int i, const std::string& n, int colour = 4) {
        Channel c; c.name = n; c.colour = colour; c.seen = true; m[i] = c;
    };
    for (int i = 0; i < 12; ++i) put(st.inputs, i, "In " + std::to_string(i + 1));
    put(st.outputs, 0, "Main");
    put(st.outputs, 6, "Phones 1");
    st.mainOut   = 0;
    st.phones[0] = 6;
    st.outputs[0].volume = -10.0;
    st.mix[State::mixKey(Bus::Input, 2, 6)] = -6.0;      // In 3 into Phones 1
    st.inputs[2].setLeaf("gain", 20.0);                   // In 3 has a preamp
    return st;
}

int main()
{
    const State st = fixture();
    Config cfg;
    cfg.jogTarget = "main";
    cfg.jogStepDb = 0.5;
    in_::State in;
    in.row.store(static_cast<int>(rmeu::Row::Input));
    in.submix.store(6);
    u8::Side side;
    const std::array<u8::SoftCell, 8> soft{};

    // ── the window: strips 1-8 = the first eight inputs ───────────────────────
    {
        const auto ch = u8::channels(st, in, side);
        EXPECT(ch[0] == 0 && ch[7] == 7);
        u8::shiftWindow(st, in, side, 8);                 // Bank >
        const auto c2 = u8::channels(st, in, side);
        EXPECT(c2[0] == 4 && c2[7] == 11);                // clamped: the last eight
        EXPECT(u8::channels(st, in, side)[3] == 7);
        u8::shiftWindow(st, in, side, -100);              // clamped at the start
        EXPECT(u8::channels(st, in, side)[0] == 0);
        u8::shiftWindow(st, in, side, 1);                 // Page >
        EXPECT(u8::channels(st, in, side)[0] == 1);
        u8::shiftWindow(st, in, side, -1);
    }
    // ── the window follows a channel the UF1 picks ────────────────────────────
    {
        in_::select(in, rmeu::Row::Input, 10);
        u8::follow(st, in, side);
        const auto ch = u8::channels(st, in, side);
        EXPECT(ch[7] == 10 || ch[6] == 10 || ch[5] == 10 || ch[4] == 10);
        bool shown = false;
        for (int c : ch) shown = shown || c == 10;
        EXPECT(shown);
        // Unchanged selection: Bank moves the window and follow leaves it.
        u8::shiftWindow(st, in, side, -8);
        u8::follow(st, in, side);
        EXPECT(u8::channels(st, in, side)[0] == 0);
    }
    // ── SEL is the UF1's selection ───────────────────────────────────────────
    {
        u8::sel(st, in, side, 2);
        EXPECT(in.sel[static_cast<int>(rmeu::Row::Input)].load() == 2);
        EXPECT(in_::selected(in, st, rmeu::Row::Input) == 2);
    }
    // ── what the strips show ──────────────────────────────────────────────────
    {
        const auto f = u8::frame(st, in, side, cfg, soft);
        const auto& v = f.strips[2];
        EXPECT(v.upper == "In 3");
        EXPECT(v.csType == "IN");
        EXPECT(v.chanNum == "3");
        EXPECT(v.sel);
        EXPECT(!f.strips[1].sel);
        EXPECT(v.drive);                                   // level known: -6 into Phones 1
        EXPECT(!f.strips[1].drive);                        // never reported: motor left alone
        EXPECT(v.valueLine.find("Gain") == 0);             // an input's pot: preamp gain
        EXPECT(v.ringMode == 0x01);
        EXPECT(f.colour[2] == cfg.colourMap[4]);
    }
    // ── a fader writes faderlin into the submix ───────────────────────────────
    {
        const auto w = u8::fader(st, in, side, 2, 0.5);
        EXPECT(w.size() == 1);
        EXPECT(has(w, "/mix/in/2/6/faderlin"));
        EXPECT(val(w, "/mix/in/2/6/faderlin") == 0.5f);
        // No submix, no write: an input fader needs one.
        in_::State none;
        none.row.store(static_cast<int>(rmeu::Row::Input));
        State noOut = st;
        noOut.outputs.clear();
        noOut.mainOut = -1;
        EXPECT(u8::fader(noOut, none, side, 2, 0.5).empty());
        // An empty strip writes nothing.
        in_::State outs;
        outs.row.store(static_cast<int>(rmeu::Row::Output));
        u8::Side s2;
        EXPECT(u8::fader(st, outs, s2, 5, 0.5).empty());
        // An output writes its own faderlin.
        EXPECT(has(u8::fader(st, outs, s2, 0, 0.7), "/output/0/faderlin")
            || has(u8::fader(st, outs, s2, 0, 0.7), "/output/6/faderlin"));
    }
    // ── the V-Pot: gain on an input that has one, pan otherwise ──────────────
    {
        const auto g = u8::vpot(st, in, side, cfg, 2, 1, 1.0, 0);
        EXPECT(!g.empty());
        EXPECT(g.empty() || g[0].first.find("/input/2/gain") == 0);
        const auto p = u8::vpot(st, in, side, cfg, 1, 5, 1.0, 0);   // In 2: no gain leaf
        EXPECT(p.size() == 1 && p[0].first.find("balpan") != std::string::npos);
        EXPECT(p.size() == 1 && std::abs(p[0].second - 0.1f) < 1e-4f);
        // Pushed: back to the middle, and the twist riding along is ignored.
        const auto c = u8::vpotPush(st, in, side, cfg, 1, 1000);
        EXPECT(c.size() == 1 && c[0].second == 0.0f);
        EXPECT(u8::vpot(st, in, side, cfg, 1, 3, 1.0, 1100).empty());
    }
    // ── CUT and SOLO ─────────────────────────────────────────────────────────
    {
        const auto m = u8::cut(st, in, side, 2);
        EXPECT(m.size() == 1 && m[0].first == "/input/2/mute" && m[0].second == 1.0f);
        const auto s = u8::solo(st, in, side, 2);
        EXPECT(s.size() == 1 && s[0].second == 1.0f);
        in_::State outs;
        outs.row.store(static_cast<int>(rmeu::Row::Output));
        u8::Side s3;
        EXPECT(u8::solo(st, outs, s3, 0).empty());          // outputs have no solo
    }
    // ── the channel view ──────────────────────────────────────────────────────
    {
        u8::toggleChannelView(st, in, side);
        EXPECT(side.chanView.load());
        const auto f = u8::frame(st, in, side, cfg, soft);
        EXPECT(!f.strips[0].slotLabel.empty());            // soft keys = pages
        EXPECT(f.strips[0].topKey == 2);                   // the first page is on
        u8::toggleChannelView(st, in, side);
        EXPECT(!side.chanView.load());
        // Nothing selected on a row: no channel view.
        in_::State empty;
        empty.row.store(static_cast<int>(rmeu::Row::Playback));
        u8::Side s4;
        u8::toggleChannelView(st, empty, s4);
        EXPECT(!s4.chanView.load());
    }
    // ── the channel encoder is Main ──────────────────────────────────────────
    {
        const auto w = u8::mainVolume(st, in, cfg, 2, 1.0);
        EXPECT(w.size() == 1 && w[0].first == "/output/0/volume");
        EXPECT(w.size() == 1 && std::abs(w[0].second - (-9.0f)) < 1e-4f);
    }

    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("uf8_rme: all passed\n");
    return 0;
}
