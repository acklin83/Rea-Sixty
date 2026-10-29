#include "Uf8Rme.h"

#include "Protocol.h"
#include "RmeFace.h"
#include "TrackName.h"
#include "Uf1Text.h"
#include "Uf8Fader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace reasixty::rme::uf8side {

namespace {

int clampFirst(int first, int count)
{
    return std::clamp(first, 0, std::max(0, count - kStrips));
}

const char* rowTag(rmeu::Row r)
{
    switch (r) {
        case rmeu::Row::Input:    return "IN";
        case rmeu::Row::Playback: return "PB";
        case rmeu::Row::Output:   return "OUT";
        case rmeu::Row::Fx:       return "FX";
    }
    return "";
}

// The page the channel view shows, and every page the channel has.
struct Pages {
    std::vector<rmes::View> all;
    int at = -1;   // index into all
};

Pages pagesOf(const State& st, rmeu::Row r, int ch, const Config& cfg, int wantId)
{
    Pages p;
    if (ch < 0) return p;
    p.all = rmes::views(st, r, ch, cfg.stripPages);
    if (p.all.empty()) return p;
    p.at = 0;
    for (int i = 0; i < static_cast<int>(p.all.size()); ++i)
        if (p.all[static_cast<size_t>(i)].id == wantId) p.at = i;
    return p;
}

// The overview's pot on an Input strip: the preamp gain, where the channel has one.
const rmes::Param* gainParam(const State& st, rmeu::Row r, int ch)
{
    if (r != rmeu::Row::Input || ch < 0) return nullptr;
    const rmes::Param* p = rmes::find("gain");
    return (p && rmes::available(st, r, ch, *p)) ? p : nullptr;
}

int wholeSteps(std::atomic<double>& acc, int delta)
{
    // The UF8 V-Pot sends one count per detent, so a list or a switch moves one
    // entry per detent without gathering (the UF1's pots send four).
    (void)acc;
    return delta;
}

} // namespace

// ── which channels ───────────────────────────────────────────────────────────

std::array<int, kStrips> channels(const State& st, const rmei::State& in, const Side& u)
{
    std::array<int, kStrips> out;
    out.fill(-1);
    const auto r    = rmeu::rowAt(in.row.load());
    const auto list = rmeu::visibleChannels(st, r);
    const int  n    = static_cast<int>(list.size());
    const int  f    = clampFirst(u.first[static_cast<int>(r)].load(), n);
    for (int s = 0; s < kStrips && f + s < n; ++s) out[static_cast<size_t>(s)] = list[static_cast<size_t>(f + s)];
    return out;
}

void follow(const State& st, const rmei::State& in, Side& u)
{
    const auto r   = rmeu::rowAt(in.row.load());
    const int  ri  = static_cast<int>(r);
    const int  sel = rmei::selected(in, st, r);
    if (sel == u.followed[ri].load()) return;
    u.followed[ri].store(sel);
    const auto list = rmeu::visibleChannels(st, r);
    const auto it   = std::find(list.begin(), list.end(), sel);
    if (it == list.end()) return;
    const int k = static_cast<int>(it - list.begin());
    int f = clampFirst(u.first[ri].load(), static_cast<int>(list.size()));
    if (k < f) f = k;
    if (k >= f + kStrips) f = k - kStrips + 1;
    u.first[ri].store(clampFirst(f, static_cast<int>(list.size())));
}

void shiftWindow(const State& st, const rmei::State& in, Side& u, int by)
{
    const auto r  = rmeu::rowAt(in.row.load());
    const int  ri = static_cast<int>(r);
    const int  n  = static_cast<int>(rmeu::visibleChannels(st, r).size());
    u.first[ri].store(clampFirst(clampFirst(u.first[ri].load(), n) + by, n));
}

// ── what the strips show ─────────────────────────────────────────────────────

Frame frame(const State& st, const rmei::State& in, const Side& u, const Config& cfg,
            const std::array<SoftCell, kStrips>& softKeys)
{
    Frame f;
    const auto r   = rmeu::rowAt(in.row.load());
    const int  sel = rmei::selected(in, st, r);
    const int  sub = rmeu::effectiveSubmix(st, in.submix.load());
    const auto chs = channels(st, in, u);
    const bool cv  = u.chanView.load() && sel >= 0;
    const Pages pg = cv ? pagesOf(st, r, sel, cfg, u.chanPage.load()) : Pages{};
    const rmes::View* page = (pg.at >= 0) ? &pg.all[static_cast<size_t>(pg.at)] : nullptr;

    for (int s = 0; s < kStrips; ++s) {
        uf8strips::View& v = f.strips[static_cast<size_t>(s)];
        const int ch = chs[static_cast<size_t>(s)];
        // The soft keys: the bank's cells in the overview, the pages in the
        // channel view (key 1 = the first page of the current group of eight).
        if (cv) {
            const int pi = u.chanPageGroup.load() * kStrips + s;
            if (pi < static_cast<int>(pg.all.size())) {
                v.slotLabel = utf8ToLatin1(pg.all[static_cast<size_t>(pi)].name);
                v.topKey    = (pi == pg.at) ? 2 : 1;
            }
        } else {
            const SoftCell& k = softKeys[static_cast<size_t>(s)];
            v.slotLabel = utf8ToLatin1(k.label);
            v.topKey    = k.lamp;
            v.topKeyRgb = k.rgb;
        }
        if (v.slotLabel.size() > uf8::kUf8ScribbleChars) v.slotLabel.resize(uf8::kUf8ScribbleChars);
        if (ch < 0) {
            v.ringMode = 0x03;
            continue;   // an empty strip: dark, fader left alone
        }

        // Name, row, number, colour.
        v.upper = utf8ToLatin1(rmeu::displayName(st, r, ch));
        if (v.upper.size() > uf8::kUf8ScribbleChars) v.upper.resize(uf8::kUf8ScribbleChars);
        v.csType  = rowTag(r);
        v.chanNum = std::to_string(ch + 1);
        if (const rme::Channel* c = rmeu::channelOf(st, r, ch); c && c->colour >= 0 && c->colour < 9)
            f.colour[static_cast<size_t>(s)] = cfg.colourMap[c->colour];

        // Level: the readout, and where the motor puts the fader.
        bool known = false;
        const double db = rmeu::levelDb(st, r, ch, sub, known);
        v.faderText = known ? face::dbText(db) : std::string();
        v.faderUnit = (known && db > -99.0) ? "dB" : "";
        if (known) {
            const double lin = rme::dbToFaderlin(db);
            f.faderLin[static_cast<size_t>(s)] = lin;
            v.drive   = true;
            v.faderPb = static_cast<std::uint16_t>(std::lround(std::clamp(lin, 0.0, 1.0) * uf8fader::kPbMax));
        }

        // Lamps: SEL = the channel the UF1 shows, CUT = mute, SOLO = solo in the submix.
        v.sel  = (ch == sel);
        v.cut  = rmeu::muted(st, r, ch);
        v.solo = rmeu::soloed(st, r, ch, sub);

        // Meter: TotalMix' peak level, when it sends one.
        {
            const std::map<int, double>* lv = (r == rmeu::Row::Input) ? &st.levelIn
                                            : (r == rmeu::Row::Playback) ? &st.levelPb
                                            : (r == rmeu::Row::Output) ? &st.levelOut : nullptr;
            if (lv) {
                const auto it = lv->find(ch);
                const std::uint8_t b = (it != lv->end()) ? dbToVuByte_(it->second) : 0;
                f.vu[static_cast<size_t>(s * 2)]     = b;
                f.vu[static_cast<size_t>(s * 2 + 1)] = b;
            }
        }

        // The pot and the value line.
        if (cv) {
            // The channel view: pots 1-4 = the page's parameters, 5-8 = its
            // switches, all on the selected channel.
            const rmes::Param* p = page ? (s < 4 ? page->pots[s] : page->keys[s - 4]) : nullptr;
            if (p) {
                double val = 0.0;
                const bool k = rmes::value(st, r, sel, *p, val);
                v.valueLine = uf8ValueLine(utf8ToLatin1(rmes::label(st, r, sel, *p)),
                                           k ? rmes::format(*p, r, val) : std::string());
                if (s < 4) {
                    v.ring     = uf8::vpotPosUnipolar(k ? rmes::norm(*p, r, val) : 0.0);
                    v.ringMode = 0x01;
                } else {
                    v.ring     = uf8::vpotPosUnipolar((k && val >= 0.5) ? 1.0 : 0.0);
                    v.ringMode = 0x01;
                }
            } else {
                v.ringMode = 0x03;
            }
        } else if (const rmes::Param* g = gainParam(st, r, ch)) {
            double val = 0.0;
            const bool k = rmes::value(st, r, ch, *g, val);
            v.valueLine = uf8ValueLine(utf8ToLatin1(rmes::label(st, r, ch, *g)),
                                       k ? rmes::format(*g, r, val) : std::string());
            v.ring     = uf8::vpotPosUnipolar(k ? rmes::norm(*g, r, val) : 0.0);
            v.ringMode = 0x01;
        } else {
            const std::string a = rmeu::panAddress(r, ch, sub);
            if (!a.empty()) {
                bool pk = false;
                const double pan = rmeu::panValue(st, r, ch, sub, pk);
                if (rmeu::panIsWidth(r)) {
                    char b[16];
                    snprintf(b, sizeof b, "%.2f", pk ? pan : 0.0);
                    v.valueLine = uf8ValueLine("Width", pk ? b : "");
                    v.ring      = uf8::vpotPosUnipolar(pk ? pan : 0.0);
                } else {
                    v.valueLine = uf8ValueLine("Pan", pk ? formatPanReadout(pan) : "");
                    v.ring      = uf8::vpotPosPan(pk ? pan : 0.0);
                }
                v.ringMode = 0x01;   // pan is a line, unipolar (Frank 2026-08-19)
            } else {
                v.ringMode = 0x03;
            }
        }
    }
    return f;
}

// ── input ────────────────────────────────────────────────────────────────────

Writes fader(const State& st, const rmei::State& in, const Side& u, int strip, double lin)
{
    Writes out;
    if (strip < 0 || strip >= kStrips) return out;
    const int ch = channels(st, in, u)[static_cast<size_t>(strip)];
    if (ch < 0) return out;
    const auto r   = rmeu::rowAt(in.row.load());
    const int  sub = rmeu::effectiveSubmix(st, in.submix.load());
    // An effect's volume needs no submix, like an output's.
    if (r != rmeu::Row::Output && r != rmeu::Row::Fx && sub < 0) return out;
    const auto w = rmeu::levelFromFader(r, ch, sub, std::clamp(lin, 0.0, 1.0));
    if (!w.first.empty()) out.push_back(w);
    return out;
}

Writes vpot(const State& st, const rmei::State& in, Side& u, const Config& cfg,
            int strip, int delta, double scale, std::int64_t nowMs)
{
    Writes out;
    if (strip < 0 || strip >= kStrips || delta == 0) return out;
    // Just pushed back to its default: the twist riding along is not a turn.
    if (u.pushQuiet.quiet(strip, nowMs)) return out;
    const auto r   = rmeu::rowAt(in.row.load());
    const int  sel = rmei::selected(in, st, r);
    if (u.chanView.load() && sel >= 0) {
        if (strip >= 4) return out;   // the switches: a push, not a turn
        const Pages pg = pagesOf(st, r, sel, cfg, u.chanPage.load());
        if (pg.at < 0) return out;
        const rmes::Param* p = pg.all[static_cast<size_t>(pg.at)].pots[strip];
        if (!p) return out;
        if (rmes::stepsWhole(*p)) {
            const int steps = wholeSteps(u.potAccum[strip], delta);
            if (steps != 0) return rmes::nudge(st, r, sel, *p, steps);
            return out;
        }
        return rmes::nudge(st, r, sel, *p, delta, scale);
    }
    const int ch = channels(st, in, u)[static_cast<size_t>(strip)];
    if (ch < 0) return out;
    if (const rmes::Param* g = gainParam(st, r, ch)) {
        if (rmes::stepsWhole(*g)) return rmes::nudge(st, r, ch, *g, wholeSteps(u.potAccum[strip], delta));
        return rmes::nudge(st, r, ch, *g, delta, scale);
    }
    const int sub = rmeu::effectiveSubmix(st, in.submix.load());
    const std::string a = rmeu::panAddress(r, ch, sub);
    if (a.empty()) return out;
    bool known = false;
    const double cur = rmeu::panValue(st, r, ch, sub, known);
    // 2 % per count, as the UF1's pot above the fader. On the FX row the pot is
    // the width, 0 mono .. 1 stereo.
    const double lo = rmeu::panIsWidth(r) ? 0.0 : -1.0;
    const double nv = std::clamp(cur + delta * 0.02 * scale, lo, 1.0);
    if (nv != cur) out.push_back({ a, static_cast<float>(nv) });
    return out;
}

Writes vpotPush(const State& st, const rmei::State& in, Side& u, const Config& cfg,
                int strip, std::int64_t nowMs)
{
    Writes out;
    if (strip < 0 || strip >= kStrips) return out;
    u.pushQuiet.arm(strip, nowMs);
    const auto r   = rmeu::rowAt(in.row.load());
    const int  sel = rmei::selected(in, st, r);
    if (u.chanView.load() && sel >= 0) {
        const Pages pg = pagesOf(st, r, sel, cfg, u.chanPage.load());
        if (pg.at < 0) return out;
        const rmes::View& view = pg.all[static_cast<size_t>(pg.at)];
        if (strip < 4) {
            if (const rmes::Param* p = view.pots[strip]) return rmes::resetWrites(st, r, sel, *p);
            return out;
        }
        if (const rmes::Param* p = view.keys[strip - 4]) return rmes::press(st, r, sel, *p);
        return out;
    }
    const int ch = channels(st, in, u)[static_cast<size_t>(strip)];
    if (ch < 0) return out;
    if (const rmes::Param* g = gainParam(st, r, ch)) return rmes::resetWrites(st, r, ch, *g);
    const int sub = rmeu::effectiveSubmix(st, in.submix.load());
    const std::string a = rmeu::panAddress(r, ch, sub);
    if (!a.empty()) out.push_back({ a, rmeu::panIsWidth(r) ? 1.0f : 0.0f });
    return out;
}

void sel(const State& st, rmei::State& in, const Side& u, int strip)
{
    if (strip < 0 || strip >= kStrips) return;
    const int ch = channels(st, in, u)[static_cast<size_t>(strip)];
    if (ch < 0) return;
    rmei::select(in, rmeu::rowAt(in.row.load()), ch);
}

Writes cut(const State& st, const rmei::State& in, const Side& u, int strip)
{
    Writes out;
    if (strip < 0 || strip >= kStrips) return out;
    const int ch = channels(st, in, u)[static_cast<size_t>(strip)];
    if (ch < 0) return out;
    const auto w = rmeu::muteToggle(st, rmeu::rowAt(in.row.load()), ch);
    if (!w.first.empty()) out.push_back(w);
    return out;
}

Writes solo(const State& st, const rmei::State& in, const Side& u, int strip)
{
    Writes out;
    if (strip < 0 || strip >= kStrips) return out;
    const int ch = channels(st, in, u)[static_cast<size_t>(strip)];
    if (ch < 0) return out;
    const auto r   = rmeu::rowAt(in.row.load());
    const int  sub = rmeu::effectiveSubmix(st, in.submix.load());
    const std::string a = rmeu::soloAddress(r, ch, sub);
    if (a.empty()) return out;   // outputs have no solo
    out.push_back({ a, rmeu::soloed(st, r, ch, sub) ? 0.0f : 1.0f });
    return out;
}

void toggleChannelView(const State& st, const rmei::State& in, Side& u)
{
    if (u.chanView.load()) { u.chanView.store(false); return; }
    const auto r = rmeu::rowAt(in.row.load());
    if (rmei::selected(in, st, r) < 0) return;
    u.chanPageGroup.store(0);
    u.chanView.store(true);
}

void pickPage(const State& st, const rmei::State& in, Side& u, const Config& cfg, int key)
{
    const auto r   = rmeu::rowAt(in.row.load());
    const int  sel = rmei::selected(in, st, r);
    const Pages pg = pagesOf(st, r, sel, cfg, u.chanPage.load());
    const int pi = u.chanPageGroup.load() * kStrips + key;
    if (pi < 0 || pi >= static_cast<int>(pg.all.size())) return;
    u.chanPage.store(pg.all[static_cast<size_t>(pi)].id);
}

void stepPageGroup(const State& st, const rmei::State& in, Side& u, const Config& cfg, int dir)
{
    const auto r   = rmeu::rowAt(in.row.load());
    const int  sel = rmei::selected(in, st, r);
    const Pages pg = pagesOf(st, r, sel, cfg, u.chanPage.load());
    const int groups = std::max(1, (static_cast<int>(pg.all.size()) + kStrips - 1) / kStrips);
    u.chanPageGroup.store(std::clamp(u.chanPageGroup.load() + dir, 0, groups - 1));
}

Writes mainVolume(const State& st, const rmei::State& in, const Config& cfg, int detents,
                  double scale)
{
    Writes out;
    const rmeu::Target t = rmeu::resolveTarget(st, cfg.jogTarget);
    if (t.visible) rmei::nudgeLevel(in, st, t.row, t.ch, detents, cfg.jogStepDb, scale, out);
    return out;
}

} // namespace reasixty::rme::uf8side
