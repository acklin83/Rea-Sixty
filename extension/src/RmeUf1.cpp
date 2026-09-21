#include "RmeUf1.h"

#include <algorithm>
#include <cstdlib>

namespace reasixty::rme::uf1 {

const char* rowName(Row r)
{
    switch (r) {
        case Row::Input:    return "INPUT";
        case Row::Playback: return "PLAYBACK";
        case Row::Output:   return "OUTPUT";
    }
    return "";
}

static const std::map<int, Channel>& mapOf(const State& st, Row r)
{
    switch (r) {
        case Row::Input:    return st.inputs;
        case Row::Playback: return st.playbacks;
        case Row::Output:   break;
    }
    return st.outputs;
}

const Channel* channelOf(const State& st, Row r, int ch)
{
    const auto& m = mapOf(st, r);
    const auto it = m.find(ch);
    if (it == m.end() || !it->second.seen) return nullptr;
    return &it->second;
}

std::vector<int> visibleChannels(const State& st, Row r)
{
    std::vector<int> v;
    for (const auto& [idx, c] : mapOf(st, r))
        if (c.seen && c.colour != 0) v.push_back(idx);   // std::map: ascending
    return v;
}

int stepChannel(const std::vector<int>& list, int cur, int delta)
{
    if (list.empty()) return -1;
    auto it = std::find(list.begin(), list.end(), cur);
    int pos;
    if (it != list.end()) {
        pos = static_cast<int>(it - list.begin()) + delta;
    } else {
        // Not in the list (hidden meanwhile, or never set): the first entry past
        // it in the direction of travel, so one detent is never a no-op.
        const auto up = std::upper_bound(list.begin(), list.end(), cur);
        const int  at = static_cast<int>(up - list.begin());
        pos = (delta > 0) ? at + (delta - 1) : at - 1 + (delta + 1);
    }
    pos = std::clamp(pos, 0, static_cast<int>(list.size()) - 1);
    return list[static_cast<std::size_t>(pos)];
}

Target resolveTarget(const State& st, const std::string& spec)
{
    Target t;
    auto role = [&](int ch) {
        t.row = Row::Output;
        t.ch = ch;
        t.assigned = State::roleAssigned(ch);
        t.visible  = t.assigned && st.outputForRole(ch) != nullptr;
    };
    if (spec == "main")  { role(st.mainOut);  return t; }
    if (spec == "mainB") { role(st.mainOutB); return t; }
    if (spec == "talk")  { role(st.talkChannel); return t; }
    if (spec.size() == 7 && spec.rfind("phones", 0) == 0 && spec[6] >= '1' && spec[6] <= '4') {
        role(st.phones[spec[6] - '1']);
        return t;
    }
    const auto colon = spec.find(':');
    if (colon != std::string::npos) {
        const std::string kind = spec.substr(0, colon);
        const int ch = std::atoi(spec.c_str() + colon + 1);
        if (kind == "output")   t.row = Row::Output;
        else if (kind == "input")    t.row = Row::Input;
        else if (kind == "playback") t.row = Row::Playback;
        else return t;
        t.ch = ch;
        t.assigned = true;
        const Channel* c = channelOf(st, t.row, ch);
        t.visible = c && c->colour != 0;
    }
    return t;
}

int effectiveSubmix(const State& st, int selectedOutput)
{
    if (const Channel* c = channelOf(st, Row::Output, selectedOutput); c && c->colour != 0)
        return selectedOutput;
    if (st.outputForRole(st.mainOut)) return st.mainOut;
    const auto v = visibleChannels(st, Row::Output);
    return v.empty() ? -1 : v.front();
}

double levelDb(const State& st, Row r, int ch, int submix, bool& known)
{
    known = false;
    if (r == Row::Output) {
        if (const Channel* c = channelOf(st, Row::Output, ch)) { known = true; return c->volume; }
        return kDbOff;
    }
    const Bus b = (r == Row::Input) ? Bus::Input : Bus::Playback;
    if (const double* v = st.mixFader(b, ch, submix)) { known = true; return *v; }
    return kDbOff;
}

std::string levelAddress(Row r, int ch, int submix, bool faderlin)
{
    if (r == Row::Output)
        return "/output/" + std::to_string(ch) + (faderlin ? "/faderlin" : "/volume");
    return std::string("/mix/") + (r == Row::Input ? "in/" : "pb/") + std::to_string(ch)
         + "/" + std::to_string(submix) + (faderlin ? "/faderlin" : "/fader");
}

uf1eq::Model eqModel(const State& st, Row r, int ch)
{
    uf1eq::Model m;
    if (r == Row::Playback) return m;             // no EQ on a playback
    const Channel* c = channelOf(st, r, ch);
    if (!c || !c->eq.seen) return m;
    const ChannelEq& e = c->eq;
    using K = uf1eq::Band::Kind;
    // band1: 0 Bell, 1 Shelve (low), 2 Hipass, 3 Low-Pass. band3 the same with
    // a HIGH shelf. Measured 2026-09-21; Frank: the shelf side is obvious.
    auto kindOf = [](int type, bool low) {
        switch (type) {
            case 1:  return low ? K::LowShelf : K::HighShelf;
            case 2:  return K::HighPass;
            case 3:  return K::LowPass;
            default: return K::Bell;
        }
    };
    if (e.on) {
        m.on = true;
        m.bands.push_back({kindOf(e.type1, true),  e.freq[0], e.gain[0], e.q[0]});
        m.bands.push_back({K::Bell,                e.freq[1], e.gain[1], e.q[1]});
        m.bands.push_back({kindOf(e.type3, false), e.freq[2], e.gain[2], e.q[2]});
    }
    // The low cut is its own switch in TotalMix and draws with the EQ off too.
    if (e.lcOn) {
        m.on = true;
        uf1eq::Band hp{K::HighPass, e.lcFreq, 0.0, 0.7};
        hp.order = std::clamp(e.lcSlope, 0, 3) + 1;   // 0..3 = 6/12/18/24 dB/oct
        m.bands.push_back(hp);
    }
    return m;
}

double nudgeDb(double cur, int detents, double stepDb)
{
    if (detents == 0) return cur;
    if (cur <= -99.0) {
        if (detents < 0) return kDbOff;
        cur = -60.0;
        --detents;
    }
    double v = cur + detents * stepDb;
    if (v < -99.0) return kDbOff;
    if (v > 6.0) v = 6.0;
    return v;
}

}  // namespace reasixty::rme::uf1
