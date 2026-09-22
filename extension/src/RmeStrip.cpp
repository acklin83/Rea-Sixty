#include "RmeStrip.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reasixty::rme::strip {
namespace {

// ⇨ THE CATALOGUE. Leaf = RME's sheet, range/step = UFX+ manual (plan 5b).
// Not in the manual and therefore NOT belegt: the OSC values of `crossfeed`,
// the ref-level list order (taken as the manual lists them), FX send range.
const std::vector<Param>& catalogue()
{
    static const std::vector<Param> k = {
        // ── preamp / input ──────────────────────────────────────────────────
        { "gain",        "gain",               "Gain",     Kind::Db,     0.0, 75.0, 1.0,
          Need::Any, false, /*bothSides*/ true },
        { "fxsend",      "fxsend",             "FX Send",  Kind::Db,   -65.0,  6.0, 0.5 },
        { "fxreturn",    "fxreturn",           "FX Ret",   Kind::Db,   -65.0,  6.0, 0.5 },
        { "reflevel",    "reflevel",           "Ref Lvl",  Kind::List,   0.0,  0.0, 1.0,
          Need::Any, false, false,
          { "+4 dBu", "LoGain" },
          { "-10 dBV", "+4 dBu", "HiGain", "+24 dBu" } },
        { "width",       "width",              "Width",    Kind::Width,  0.0,  1.0, 0.02,
          Need::Stereo },
        { "48v",         "48v",                "48V",      Kind::Toggle, 0, 1, 1 },
        { "pad",         "pad",                "Pad",      Kind::Toggle, 0, 1, 1 },
        { "instrument",  "instrument",         "Inst",     Kind::Toggle, 0, 1, 1 },
        { "autoset",     "autoset",            "AutoSet",  Kind::Toggle, 0, 1, 1 },
        { "stereo",      "stereo",             "Stereo",   Kind::Toggle, 0, 1, 1 },
        { "msproc",      "msproc",             "M/S",      Kind::Toggle, 0, 1, 1,
          Need::Stereo },
        { "phase",       "phase",              "Phase L",  Kind::Toggle, 0, 1, 1 },
        { "phaseR",      "phase",              "Phase R",  Kind::Toggle, 0, 1, 1,
          Need::Stereo, /*right*/ true },
        // ── low cut + EQ ────────────────────────────────────────────────────
        { "lc_on",       "lowcut/enable",      "Low Cut",  Kind::Toggle, 0, 1, 1 },
        { "lc_freq",     "lowcut/freq",        "LC Freq",  Kind::Hz,    20.0, 500.0, 1.0 },
        { "lc_slope",    "lowcut/slope",       "Slope",    Kind::List,   0, 0, 1,
          Need::Any, false, false, { "6 dB", "12 dB", "18 dB", "24 dB" } },
        { "eq_on",       "eq/enable",          "EQ",       Kind::Toggle, 0, 1, 1 },
        { "b1gain",      "eq/band1gain",       "Gain 1",   Kind::Db,   -20.0, 20.0, 0.5 },
        { "b1freq",      "eq/band1freq",       "Freq 1",   Kind::Hz,    20.0, 20000.0, 1.0 },
        { "b1q",         "eq/band1q",          "Q 1",      Kind::Q,      0.4, 9.9, 0.1 },
        // 0 Bell, 1 Shelve, 2 Hipass, 3 Low-Pass, measured 2026-09-21.
        { "b1type",      "eq/band1type",       "Type 1",   Kind::List,   0, 0, 1,
          Need::Any, false, false, { "Bell", "Shelf", "HiPass", "LoPass" } },
        { "b2gain",      "eq/band2gain",       "Gain 2",   Kind::Db,   -20.0, 20.0, 0.5 },
        { "b2freq",      "eq/band2freq",       "Freq 2",   Kind::Hz,    20.0, 20000.0, 1.0 },
        { "b2q",         "eq/band2q",          "Q 2",      Kind::Q,      0.4, 9.9, 0.1 },
        { "b3gain",      "eq/band3gain",       "Gain 3",   Kind::Db,   -20.0, 20.0, 0.5 },
        { "b3freq",      "eq/band3freq",       "Freq 3",   Kind::Hz,    20.0, 20000.0, 1.0 },
        { "b3q",         "eq/band3q",          "Q 3",      Kind::Q,      0.4, 9.9, 0.1 },
        { "b3type",      "eq/band3type",       "Type 3",   Kind::List,   0, 0, 1,
          Need::Any, false, false, { "Bell", "Shelf", "HiPass", "LoPass" } },
        // ── dynamics ────────────────────────────────────────────────────────
        { "dyn_on",      "dynamics/enable",    "Dyn",      Kind::Toggle, 0, 1, 1 },
        { "compthres",   "dynamics/compthres", "Thresh",   Kind::Db,   -60.0,  0.0, 0.5 },
        { "compratio",   "dynamics/compratio", "Ratio",    Kind::Ratio,  1.0, 10.0, 0.1 },
        { "attack",      "dynamics/attack",    "Attack",   Kind::Ms,     0.0, 200.0, 1.0 },
        { "release",     "dynamics/release",   "Release",  Kind::Ms,   100.0, 999.0, 5.0 },
        { "expthres",    "dynamics/expthres",  "Exp Thr",  Kind::Db,   -99.0, -20.0, 0.5 },
        { "expratio",    "dynamics/expratio",  "Exp Rat",  Kind::Ratio,  1.0, 10.0, 0.1 },
        { "dyngain",     "dynamics/gain",      "Make-up",  Kind::Db,   -30.0, 30.0, 0.5 },
        // ── auto level ──────────────────────────────────────────────────────
        { "al_on",       "autolevel/enable",   "AutoLvl",  Kind::Toggle, 0, 1, 1 },
        { "maxgain",     "autolevel/maxgain",  "Max Gain", Kind::Db,     0.0, 18.0, 0.5 },
        { "headroom",    "autolevel/headroom", "Headroom", Kind::Db,     3.0, 12.0, 0.5 },
        { "risetime",    "autolevel/risetime", "Rise",     Kind::Sec,    0.1,  9.9, 0.1 },
        // ── outputs ─────────────────────────────────────────────────────────
        { "balpan",      "balpan",             "Pan",      Kind::Pan,   -1.0,  1.0, 0.02 },
        { "crossfeed",   "crossfeed",          "Xfeed",    Kind::Int,    0.0,  5.0, 1.0 },
        { "delay",       "delay",              "Delay",    Kind::Ms,     0.0, 42.0, 0.1,
          Need::Any, false, /*bothSides*/ true },
        { "loopback",    "loopback",           "Loopback", Kind::Toggle, 0, 1, 1 },
        { "talkbacksel", "talkbacksel",        "TB Sel",   Kind::Toggle, 0, 1, 1 },
    };
    return k;
}

const Channel* chan(const State& st, Row r, int ch)
{
    return uf1::channelOf(st, r, ch);
}

// ⛔ THE RIGHT HALF IS NEVER `seen`: it announces no name (RmeState.h), so
// channelOf() rightly refuses it as a strip. Its leaves (Phase R, the right
// gain) are still real, so the right-half lookup goes to the map directly.
const Channel* half(const State& st, Row r, int ch, bool right)
{
    if (!right) return chan(st, r, ch);
    if (!chan(st, r, ch)) return nullptr;       // the left half must be a strip
    const auto& m = r == Row::Input ? st.inputs : r == Row::Playback ? st.playbacks
                                                                     : st.outputs;
    const auto it = m.find(ch + 1);
    return it == m.end() ? nullptr : &it->second;
}

bool isStereo(const State& st, Row r, int ch)
{
    const Channel* c = chan(st, r, ch);
    return c && c->stereo;
}

const std::vector<const char*>& namesFor(const Param& p, Row r)
{
    return (r == Row::Output && !p.namesOut.empty()) ? p.namesOut : p.names;
}

double clampTo(const Param& p, Row r, double v)
{
    if (p.kind == Kind::List) {
        const int n = static_cast<int>(namesFor(p, r).size());
        return std::clamp(v, 0.0, static_cast<double>(std::max(0, n - 1)));
    }
    if (p.kind == Kind::Toggle) return v >= 0.5 ? 1.0 : 0.0;
    return std::clamp(v, p.lo, p.hi);
}

std::string addr(Row r, int ch, const char* leaf)
{
    return std::string("/") + section(r) + "/" + std::to_string(ch) + "/" + leaf;
}

Writes writesFor(const State& st, Row r, int ch, const Param& p, double v)
{
    Writes w;
    const float f = static_cast<float>(v);
    const int target = p.right ? ch + 1 : ch;
    w.emplace_back(addr(r, target, p.leaf), f);
    if (p.bothSides && !p.right && isStereo(st, r, ch))
        w.emplace_back(addr(r, ch + 1, p.leaf), f);
    return w;
}

}  // namespace

const Param* find(const std::string& id)
{
    if (id.empty()) return nullptr;
    for (const Param& p : catalogue())
        if (id == p.id) return &p;
    return nullptr;
}

const char* section(Row r)
{
    return r == Row::Input ? "input" : r == Row::Playback ? "playback" : "output";
}

bool pageAllowsRow(const StripPage& pg, Row r)
{
    if (pg.rows.empty()) return true;
    const char* want = r == Row::Input ? "in" : r == Row::Playback ? "pb" : "out";
    std::size_t s = 0;
    while (s <= pg.rows.size()) {
        std::size_t e = pg.rows.find(',', s);
        if (e == std::string::npos) e = pg.rows.size();
        std::string tok = pg.rows.substr(s, e - s);
        tok.erase(std::remove(tok.begin(), tok.end(), ' '), tok.end());
        if (tok == want) return true;
        s = e + 1;
    }
    return false;
}

bool available(const State& st, Row r, int ch, const Param& p)
{
    if (ch < 0) return false;
    if (p.need == Need::Stereo && !isStereo(st, r, ch)) return false;
    const Channel* c = half(st, r, ch, p.right);
    return c && c->leaf(p.leaf) != nullptr;
}

std::vector<int> availablePages(const State& st, Row r, int ch,
                                const std::vector<StripPage>& pages)
{
    std::vector<int> out;
    for (int i = 0; i < static_cast<int>(pages.size()); ++i) {
        const StripPage& pg = pages[static_cast<std::size_t>(i)];
        if (!pageAllowsRow(pg, r)) continue;
        bool any = false;
        for (int k = 0; k < 4 && !any; ++k) {
            const Param* a = find(pg.pots[k]);
            const Param* b = find(pg.keys[k]);
            any = (a && available(st, r, ch, *a)) || (b && available(st, r, ch, *b));
        }
        if (any) out.push_back(i);
    }
    return out;
}

bool pageShowsGraph(const StripPage& pg)
{
    auto eqish = [](const std::string& id) {
        const Param* p = find(id);
        if (!p) return false;
        const std::string leaf = p->leaf;
        return leaf.rfind("eq/", 0) == 0 || leaf.rfind("lowcut/", 0) == 0;
    };
    for (int k = 0; k < 4; ++k)
        if (eqish(pg.pots[k]) || eqish(pg.keys[k])) return true;
    return false;
}

bool value(const State& st, Row r, int ch, const Param& p, double& out)
{
    const Channel* c = half(st, r, ch, p.right);
    const double* v = c ? c->leaf(p.leaf) : nullptr;
    if (!v) return false;
    out = *v;
    return true;
}

std::string label(const State& st, Row r, int ch, const Param& p)
{
    if (std::string(p.id) == "phase" && !isStereo(st, r, ch)) return "Phase";
    return p.label;
}

std::string format(const Param& p, Row r, double v)
{
    char b[32];
    switch (p.kind) {
        case Kind::Toggle: return v >= 0.5 ? "on" : "off";
        case Kind::List: {
            const auto& n = namesFor(p, r);
            const int i = static_cast<int>(std::lround(v));
            if (i >= 0 && i < static_cast<int>(n.size())) return n[static_cast<std::size_t>(i)];
            std::snprintf(b, sizeof(b), "%d", i);
            return b;
        }
        case Kind::Db:
            if (v <= -64.9 && p.lo <= -60.0) return "-inf";
            std::snprintf(b, sizeof(b), p.step >= 1.0 ? "%+.0f dB" : "%+.1f dB", v);
            return b;
        case Kind::Hz:
            if (v >= 1000.0) std::snprintf(b, sizeof(b), "%.2f kHz", v / 1000.0);
            else             std::snprintf(b, sizeof(b), "%.0f Hz", v);
            return b;
        case Kind::Q:     std::snprintf(b, sizeof(b), "%.1f", v); return b;
        case Kind::Ms:
            std::snprintf(b, sizeof(b), p.step >= 1.0 ? "%.0f ms" : "%.1f ms", v);
            return b;
        case Kind::Sec:   std::snprintf(b, sizeof(b), "%.1f s", v); return b;
        case Kind::Ratio: std::snprintf(b, sizeof(b), "%.1f:1", v); return b;
        case Kind::Width: std::snprintf(b, sizeof(b), "%.2f", v); return b;
        case Kind::Pan: {
            const int pc = static_cast<int>(std::lround(v * 100.0));
            if (pc == 0) return "C";
            std::snprintf(b, sizeof(b), "%c%d", pc < 0 ? 'L' : 'R', pc < 0 ? -pc : pc);
            return b;
        }
        case Kind::Int:   std::snprintf(b, sizeof(b), "%.0f", v); return b;
    }
    return {};
}

double norm(const Param& p, Row r, double v)
{
    if (p.kind == Kind::Toggle) return v >= 0.5 ? 1.0 : 0.0;
    if (p.kind == Kind::List) {
        const int n = static_cast<int>(namesFor(p, r).size());
        return n > 1 ? std::clamp(v / (n - 1), 0.0, 1.0) : 0.0;
    }
    if (p.kind == Kind::Hz) {
        const double lo = std::log(p.lo), hi = std::log(p.hi);
        return std::clamp((std::log(std::max(v, p.lo)) - lo) / (hi - lo), 0.0, 1.0);
    }
    return p.hi > p.lo ? std::clamp((v - p.lo) / (p.hi - p.lo), 0.0, 1.0) : 0.0;
}

Writes nudge(const State& st, Row r, int ch, const Param& p, int detents)
{
    if (detents == 0 || !available(st, r, ch, p) || p.kind == Kind::Toggle) return {};
    double v = 0.0;
    if (!value(st, r, ch, p, v)) return {};
    double nv;
    if (p.kind == Kind::Hz)
        nv = std::max(v, p.lo) * std::pow(2.0, detents / 12.0);   // 1/12 octave
    else
        nv = v + detents * p.step;
    // Snap linear values onto the step grid, so a value TotalMix reported off
    // the grid lands on it with the first detent.
    if (p.kind != Kind::Hz && p.step > 0.0)
        nv = std::round(nv / p.step) * p.step;
    nv = clampTo(p, r, nv);
    if (nv == v) return {};
    return writesFor(st, r, ch, p, nv);
}

Writes press(const State& st, Row r, int ch, const Param& p)
{
    if (!available(st, r, ch, p)) return {};
    double v = 0.0;
    if (!value(st, r, ch, p, v)) return {};
    if (p.kind == Kind::Toggle) return writesFor(st, r, ch, p, v >= 0.5 ? 0.0 : 1.0);
    if (p.kind == Kind::List) {
        const int n = static_cast<int>(namesFor(p, r).size());
        if (n < 2) return {};
        const int i = (static_cast<int>(std::lround(v)) + 1) % n;
        return writesFor(st, r, ch, p, i);
    }
    return {};
}

std::string sendChanAddress(Row r, int ch)
{
    return std::string("/sendchan/") + section(r) + "/" + std::to_string(ch);
}

}  // namespace reasixty::rme::strip
