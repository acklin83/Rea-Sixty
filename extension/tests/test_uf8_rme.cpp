// Uf8Rme: TotalMix on the UF8's eight strips (docs/orc-uf8-plan.md 2.1, 2.4,
// 2.6). Pinned: the window and that it follows the UF1's selection, SEL is the
// UF1's selection, what a fader / V-Pot / CUT / SOLO writes, the readouts, the
// channel view, Main on the channel encoder.

#include "Uf8Rme.h"
#include "RmeStrip.h"

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
    // ── SEND/PLUG-IN: the submix in the control room's order ─────────────────
    {
        EXPECT(u8::submixKeyLamp(st, in, 0) == 2);          // Phones 1, in play
        EXPECT(u8::submixKeyLamp(st, in, 5) == 1);          // Main, another output
        EXPECT(u8::submixKeyLamp(st, in, 1) == 0);          // Phones 2: no output
        EXPECT(u8::submixKeyLamp(st, in, 4) == 0);          // Main B: none
        EXPECT(u8::submixKeyLamp(st, in, 6) == 0 && u8::submixKeyLamp(st, in, 7) == 0);
        u8::pickSubmix(st, in, cfg, 5);
        EXPECT(in.submix.load() == 0);
        EXPECT(u8::frame(st, in, side, cfg, soft).strips[2].csType == "> Main");
        u8::pickSubmix(st, in, cfg, 1);                     // a dark key changes nothing
        EXPECT(in.submix.load() == 0);
        u8::pickSubmix(st, in, cfg, 0);
        EXPECT(in.submix.load() == 6);
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
        EXPECT(v.csType == "> Phones 1");                 // the submix, as on the UF1
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
    // ── the FX row: the effect's controls on all eight V-Pots (Frank 29.10.) ──
    {
        State fx = fixture();
        Channel rv; rv.name = "Reverb"; rv.colour = 4; rv.seen = true;
        for (const char* l : { "enable", "type", "predelay", "roomscale", "time", "lowcut",
                               "highcut", "highdamp", "smooth", "width", "attack", "hold",
                               "release" })
            rv.setLeaf(l, 0.0);
        rv.setLeaf("type", 12.0);                               // Envelope
        fx.fx[0] = rv;
        in_::State fin;
        fin.row.store(static_cast<int>(rmeu::Row::Fx));
        in_::select(fin, rmeu::Row::Fx, 0);
        u8::Side fs;
        u8::toggleChannelView(fx, fin, fs);
        EXPECT(fs.chanView.load());
        // What the UF1 would show, four to a view: the controls to expect.
        int pots = 0;
        for (const auto& v : reasixty::rme::strip::views(fx, rmeu::Row::Fx, 0, cfg.stripPages))
            for (const auto* q : v.pots) if (q) ++pots;
        EXPECT(pots > 8);
        const auto f = u8::frame(fx, fin, fs, cfg, soft);
        int shown = 0;
        for (int s = 0; s < 8; ++s) {
            EXPECT(f.strips[static_cast<size_t>(s)].valueLine.find("Reverb") != 0);   // on/off is CUT
            if (!f.strips[static_cast<size_t>(s)].valueLine.empty()) ++shown;
        }
        EXPECT(shown == 8);                                     // all eight V-Pots
        int pages = 0;
        for (int s = 0; s < 8; ++s) if (!f.strips[static_cast<size_t>(s)].slotLabel.empty()) ++pages;
        EXPECT(pages == (pots + 7) / 8 + 1);                    // eight to a page, and key 8
        // ⇨ SOFT KEY 8 IS THE EFFECT ON / OFF, dim while off (Frank 29.10.).
        EXPECT(f.strips[u8::kFxSwitchKey].slotLabel == "Reverb");
        EXPECT(f.strips[u8::kFxSwitchKey].topKey == 1);
        const auto sw = u8::pickPage(fx, fin, fs, cfg, u8::kFxSwitchKey);
        EXPECT(sw.size() == 1 && sw[0].first.find("enable") != std::string::npos && sw[0].second == 1.0f);
        // V-Pot 8 turns a control, not a switch.
        EXPECT(!u8::vpot(fx, fin, fs, cfg, 7, 1, 1.0, 0).empty());
    }
    // ── the channel encoder is Main ──────────────────────────────────────────
    {
        const auto w = u8::mainVolume(st, in, side, cfg, 2, 1.0);
        EXPECT(w.size() == 1 && w[0].first == "/output/0/volume");
        EXPECT(w.size() == 1 && std::abs(w[0].second - (-9.0f)) < 1e-4f);
        // ⇨ AND MAIN STANDS ON STRIP 8 meanwhile (Config::uf8ShowMainOnChange).
        EXPECT(u8::mainShowing(side));
        const auto f = u8::frame(st, in, side, cfg, soft);
        const auto& m = f.strips[u8::kMainStrip];
        EXPECT(m.upper == "Main" && m.csType == "OUTPUT" && m.drive && !m.sel);
        // Its fader writes Main, CUT mutes Main, SEL leaves the row alone.
        const auto fw = u8::fader(st, in, side, u8::kMainStrip, 0.5);
        EXPECT(fw.size() == 1 && fw[0].first.rfind("/output/0/", 0) == 0);
        const auto cw = u8::cut(st, in, side, u8::kMainStrip);
        EXPECT(cw.size() == 1 && cw[0].first.rfind("/output/0/", 0) == 0);
        const int rowBefore = in.row.load();
        u8::sel(st, in, side, u8::kMainStrip);
        EXPECT(in.row.load() == rowBefore);
        // Two seconds on, strip 8 is the row's again.
        side.mainUntilMs.store(0);
        EXPECT(!u8::mainShowing(side));
        EXPECT(u8::frame(st, in, side, cfg, soft).strips[u8::kMainStrip].upper != "Main");
        // Switched off, the encoder leaves strip 8 alone.
        Config off = cfg;
        off.uf8ShowMainOnChange = false;
        u8::mainVolume(st, in, side, off, 1, 1.0);
        EXPECT(!u8::mainShowing(side));
    }

    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("uf8_rme: all passed\n");
    return 0;
}
