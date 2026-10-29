#include "Uf8Rme.h"

#include "Protocol.h"
#include "RmeFace.h"
#include "TrackName.h"
#include "Uf1Text.h"
#include "Uf8Fader.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>

namespace reasixty::rme::uf8side {

namespace {

std::int64_t steadyMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

int clampFirst(int first, int count)
{
    return std::clamp(first, 0, std::max(0, count - kStrips));
}

// One page of the channel view on the eight V-Pots: what each V-Pot is, and
// whether it is a switch (a push switches it, a turn does nothing).
struct Page {
    int                id = -1;   // the strip view's id (RmeStrip::View), stable
    std::string        name;
    const rmes::Param* p[kStrips]   = {};
    bool               key[kStrips] = {};
};
// The page the channel view shows, and every page the channel has.
struct Pages {
    std::vector<Page> all;
    int at = -1;   // index into all
};

Pages pagesOf(const State& st, rmeu::Row r, int ch, const Config& cfg, int wantId)
{
    Pages p;
    if (ch < 0) return p;
    const auto views = rmes::views(st, r, ch, cfg.stripPages);
    if (views.empty()) return p;
    if (r == rmeu::Row::Fx) {
        // ⇨ REVERB AND ECHO ON ALL EIGHT V-POTS (Frank 29.10.2026: "kein Grund
        // nur die ersten zu belegen"). The UF1's pages are four pots and four
        // switches; the effect has almost no switches, and its on/off is soft key
        // 8 (kFxSwitchKey) and CUT. So here every control of the effect in page
        // order, eight to a page, then any switch other than on/off.
        struct Item { const rmes::Param* p; bool key; };
        std::vector<Item> items;
        for (const auto& v : views)
            for (const rmes::Param* q : v.pots)
                if (q) items.push_back({ q, false });
        for (const auto& v : views)
            for (const rmes::Param* q : v.keys)
                if (q && std::string(q->id) != "rev_on" && std::string(q->id) != "echo_on")
                    items.push_back({ q, true });
        for (std::size_t at = 0; at < items.size(); at += kStrips) {
            Page pg;
            // Page j is named and keyed after the effect's view j (Reverb,
            // Reverb 2 ...), not after the view its first control came from:
            // that gave "Reverb 3" for the second page (Frank 29.10.2026).
            const std::size_t j = std::min(at / kStrips, views.size() - 1);
            pg.id   = views[j].id;
            pg.name = views[j].name;
            for (std::size_t k = 0; k < static_cast<std::size_t>(kStrips) && at + k < items.size(); ++k) {
                pg.p[k]   = items[at + k].p;
                pg.key[k] = items[at + k].key;
            }
            p.all.push_back(pg);
        }
    } else {
        // Every other row as on the UF1: V-Pots 1-4 the view's controls, 5-8
        // its switches.
        for (const auto& v : views) {
            Page pg;
            pg.id = v.id; pg.name = v.name;
            for (int k = 0; k < 4; ++k) {
                pg.p[k]     = v.pots[k];
                pg.p[k + 4] = v.keys[k];
                pg.key[k + 4] = true;
            }
            p.all.push_back(pg);
        }
    }
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

namespace {
// The soft keys that pick pages in the channel view: seven on the FX row, where
// key 8 is the effect's on/off (kFxSwitchKey), eight elsewhere.
int pageKeys(rmeu::Row r) { return r == rmeu::Row::Fx ? kFxSwitchKey : kStrips; }
// The effect's on/off on the FX row (rev_on / echo_on), nullptr elsewhere.
const rmes::Param* fxSwitch(const State& st, rmeu::Row r, int ch)
{
    if (r != rmeu::Row::Fx || ch < 0) return nullptr;
    for (const char* id : { "rev_on", "echo_on" })
        if (const rmes::Param* p = rmes::find(id); p && rmes::available(st, r, ch, *p)) return p;
    return nullptr;
}
} // namespace

bool mainShowing(const Side& u)
{
    return u.mainCh.load() >= 0 && steadyMs() < u.mainUntilMs.load();
}

namespace {
// What a strip acts on: its channel of the row, or on strip 8 the CHANNEL
// encoder's target while it stands there (mainShowing). `held` says the second.
struct Target { rmeu::Row r; int ch; bool held; };
Target targetOf(const State& st, const rmei::State& in, const Side& u, int strip)
{
    if (strip == kMainStrip && mainShowing(u))
        return { rmeu::rowAt(u.mainRow.load()), u.mainCh.load(), true };
    return { rmeu::rowAt(in.row.load()), channels(st, in, u)[static_cast<size_t>(strip)], false };
}
// A hand on strip 8 while Main stands there keeps it there.
void keepMain(const Side& u, const Target& t)
{
    if (t.held) const_cast<Side&>(u).mainUntilMs.store(steadyMs() + kMainShowMs);
}
} // namespace

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
    const Page* page = (pg.at >= 0) ? &pg.all[static_cast<size_t>(pg.at)] : nullptr;

    // The channel view on V-Pot `s`: the page's control or switch (pagesOf),
    // on the selected channel.
    auto cvPot = [&](uf8strips::View& v, int s) {
        const rmes::Param* p = page ? page->p[s] : nullptr;
        if (!p) { v.ringMode = 0x03; return; }
        double val = 0.0;
        const bool k = rmes::value(st, r, sel, *p, val);
        v.valueLine = uf8ValueLine(utf8ToLatin1(rmes::label(st, r, sel, *p)),
                                   k ? rmes::format(*p, r, val) : std::string());
        v.ring     = page->key[s] ? uf8::vpotPosUnipolar((k && val >= 0.5) ? 1.0 : 0.0)
                                  : uf8::vpotPosUnipolar(k ? rmes::norm(*p, r, val) : 0.0);
        v.ringMode = 0x01;
    };

    const bool mainOn = mainShowing(u);
    for (int s = 0; s < kStrips; ++s) {
        uf8strips::View& v = f.strips[static_cast<size_t>(s)];
        // Strip 8 shows Main while the CHANNEL encoder moves it (mainShowing):
        // its own row and channel there, everything else as for any strip.
        const bool held = mainOn && s == kMainStrip;
        const auto rs = held ? rmeu::rowAt(u.mainRow.load()) : r;
        const int  ch = held ? u.mainCh.load() : chs[static_cast<size_t>(s)];
        // The soft keys: the bank's cells in the overview, the pages in the
        // channel view (key 1 = the first page of the current group of eight).
        if (cv) {
            const int pk = pageKeys(r);
            const int pi = u.chanPageGroup.load() * pk + s;
            if (s >= pk) {
                // Soft key 8 on the FX row: the effect on (bright) or off (dim).
                if (const rmes::Param* sw = fxSwitch(st, r, sel)) {
                    double on = 0.0;
                    const bool k = rmes::value(st, r, sel, *sw, on);
                    v.slotLabel = utf8ToLatin1(rmes::label(st, r, sel, *sw));
                    v.topKey    = (k && on >= 0.5) ? 2 : 1;
                }
            } else if (pi < static_cast<int>(pg.all.size())) {
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
            // ⛔ THE CHANNEL VIEW'S V-POTS DO NOT NEED A CHANNEL ON THEIR STRIP:
            // they are the selected channel's. The FX row has two channels, so
            // V-Pots 3-8 stayed blank there (Frank 29.10.2026, "nur die ersten
            // 2"), and any row shorter than eight lost its last V-Pots.
            if (cv) cvPot(v, s);
            continue;   // otherwise an empty strip: dark, fader left alone
        }

        // Name, row, number, colour.
        v.upper = utf8ToLatin1(rmeu::displayName(st, rs, ch));
        if (v.upper.size() > uf8::kUf8ScribbleChars) v.upper.resize(uf8::kUf8ScribbleChars);
        // The colour bar: where the fader's level goes, as on the UF1 (Frank
        // 29.10.2026: the cell takes twelve characters, Protocol.cpp).
        v.csType  = utf8ToLatin1(rmeu::barText(st, rs, sub));
        if (v.csType.size() > 12) v.csType.resize(12);
        v.chanNum = std::to_string(ch + 1);
        if (const rme::Channel* c = rmeu::channelOf(st, rs, ch); c && c->colour >= 0 && c->colour < 9)
            f.colour[static_cast<size_t>(s)] = cfg.colourMap[c->colour];

        // Level: the readout, and where the motor puts the fader.
        bool known = false;
        const double db = rmeu::levelDb(st, rs, ch, sub, known);
        v.faderText = known ? face::dbText(db) : std::string();
        v.faderUnit = (known && db > -99.0) ? "dB" : "";
        if (known) {
            const double lin = rme::dbToFaderlin(db);
            f.faderLin[static_cast<size_t>(s)] = lin;
            v.drive   = true;
            v.faderPb = static_cast<std::uint16_t>(std::lround(std::clamp(lin, 0.0, 1.0) * uf8fader::kPbMax));
        }

        // Lamps: SEL = the channel the UF1 shows, CUT = mute, SOLO = solo in the submix.
        v.sel  = !held && (ch == sel);
        v.cut  = rmeu::muted(st, rs, ch);
        v.solo = rmeu::soloed(st, rs, ch, sub);

        // Meter: TotalMix' peak level, when it sends one.
        {
            const std::map<int, double>* lv = (rs == rmeu::Row::Input) ? &st.levelIn
                                            : (rs == rmeu::Row::Playback) ? &st.levelPb
                                            : (rs == rmeu::Row::Output) ? &st.levelOut : nullptr;
            if (lv) {
                const auto it = lv->find(ch);
                const std::uint8_t b = (it != lv->end()) ? dbToVuByte_(it->second) : 0;
                f.vu[static_cast<size_t>(s * 2)]     = b;
                f.vu[static_cast<size_t>(s * 2 + 1)] = b;
            }
        }

        // The pot and the value line.
        if (cv) {
            cvPot(v, s);
        } else if (const rmes::Param* g = gainParam(st, rs, ch)) {
            double val = 0.0;
            const bool k = rmes::value(st, rs, ch, *g, val);
            v.valueLine = uf8ValueLine(utf8ToLatin1(rmes::label(st, rs, ch, *g)),
                                       k ? rmes::format(*g, rs, val) : std::string());
            v.ring     = uf8::vpotPosUnipolar(k ? rmes::norm(*g, rs, val) : 0.0);
            v.ringMode = 0x01;
        } else {
            const std::string a = rmeu::panAddress(rs, ch, sub);
            if (!a.empty()) {
                bool pk = false;
                const double pan = rmeu::panValue(st, rs, ch, sub, pk);
                if (rmeu::panIsWidth(rs)) {
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
    const Target t = targetOf(st, in, u, strip);
    const int  ch  = t.ch;
    if (ch < 0) return out;
    keepMain(u, t);
    const auto r   = t.r;
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
        const Pages pg = pagesOf(st, r, sel, cfg, u.chanPage.load());
        if (pg.at < 0) return out;
        const Page& page = pg.all[static_cast<size_t>(pg.at)];
        if (page.key[strip]) return out;   // a switch: a push, not a turn
        const rmes::Param* p = page.p[strip];
        if (!p) return out;
        if (rmes::stepsWhole(*p)) {
            const int steps = wholeSteps(u.potAccum[strip], delta);
            if (steps != 0) return rmes::nudge(st, r, sel, *p, steps);
            return out;
        }
        return rmes::nudge(st, r, sel, *p, delta, scale);
    }
    const Target t = targetOf(st, in, u, strip);
    const int ch = t.ch;
    if (ch < 0) return out;
    keepMain(u, t);
    const auto rs = t.r;
    if (const rmes::Param* g = gainParam(st, rs, ch)) {
        if (rmes::stepsWhole(*g)) return rmes::nudge(st, rs, ch, *g, wholeSteps(u.potAccum[strip], delta));
        return rmes::nudge(st, rs, ch, *g, delta, scale);
    }
    const int sub = rmeu::effectiveSubmix(st, in.submix.load());
    const std::string a = rmeu::panAddress(rs, ch, sub);
    if (a.empty()) return out;
    bool known = false;
    const double cur = rmeu::panValue(st, rs, ch, sub, known);
    // 2 % per count, as the UF1's pot above the fader. On the FX row the pot is
    // the width, 0 mono .. 1 stereo.
    const double lo = rmeu::panIsWidth(rs) ? 0.0 : -1.0;
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
        const Page& page = pg.all[static_cast<size_t>(pg.at)];
        const rmes::Param* p = page.p[strip];
        if (!p) return out;
        return page.key[strip] ? rmes::press(st, r, sel, *p) : rmes::resetWrites(st, r, sel, *p);
    }
    const Target t = targetOf(st, in, u, strip);
    const int ch = t.ch;
    if (ch < 0) return out;
    keepMain(u, t);
    const auto rs = t.r;
    if (const rmes::Param* g = gainParam(st, rs, ch)) return rmes::resetWrites(st, rs, ch, *g);
    const int sub = rmeu::effectiveSubmix(st, in.submix.load());
    const std::string a = rmeu::panAddress(rs, ch, sub);
    if (!a.empty()) out.push_back({ a, rmeu::panIsWidth(rs) ? 1.0f : 0.0f });
    return out;
}

void sel(const State& st, rmei::State& in, const Side& u, int strip)
{
    if (strip < 0 || strip >= kStrips) return;
    const Target t = targetOf(st, in, u, strip);
    // Main standing on strip 8 is not on the row: SEL there would move the UF8
    // to the output row under the hand.
    if (t.ch < 0 || t.held) return;
    rmei::select(in, t.r, t.ch);
}

Writes cut(const State& st, const rmei::State& in, const Side& u, int strip)
{
    Writes out;
    if (strip < 0 || strip >= kStrips) return out;
    const Target t = targetOf(st, in, u, strip);
    if (t.ch < 0) return out;
    keepMain(u, t);
    const auto w = rmeu::muteToggle(st, t.r, t.ch);
    if (!w.first.empty()) out.push_back(w);
    return out;
}

Writes solo(const State& st, const rmei::State& in, const Side& u, int strip)
{
    Writes out;
    if (strip < 0 || strip >= kStrips) return out;
    const Target t = targetOf(st, in, u, strip);
    const int ch = t.ch;
    if (ch < 0) return out;
    const auto r   = t.r;
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

Writes pickPage(const State& st, const rmei::State& in, Side& u, const Config& cfg, int key)
{
    const auto r   = rmeu::rowAt(in.row.load());
    const int  sel = rmei::selected(in, st, r);
    const int  pk  = pageKeys(r);
    if (key >= pk) {
        if (const rmes::Param* sw = fxSwitch(st, r, sel)) return rmes::press(st, r, sel, *sw);
        return {};
    }
    const Pages pg = pagesOf(st, r, sel, cfg, u.chanPage.load());
    const int pi = u.chanPageGroup.load() * pk + key;
    if (pi < 0 || pi >= static_cast<int>(pg.all.size())) return {};
    u.chanPage.store(pg.all[static_cast<size_t>(pi)].id);
    return {};
}

void stepPage(const State& st, const rmei::State& in, Side& u, const Config& cfg, int dir)
{
    const auto r   = rmeu::rowAt(in.row.load());
    const int  sel = rmei::selected(in, st, r);
    const Pages pg = pagesOf(st, r, sel, cfg, u.chanPage.load());
    if (pg.at < 0) return;
    const int n  = static_cast<int>(pg.all.size());
    const int to = std::clamp(pg.at + (dir > 0 ? 1 : -1), 0, n - 1);
    u.chanPage.store(pg.all[static_cast<size_t>(to)].id);
    u.chanPageGroup.store(to / pageKeys(r));
}

PageLamps pageLamps(const State& st, const rmei::State& in, const Side& u, const Config& cfg)
{
    PageLamps l;
    const auto r = rmeu::rowAt(in.row.load());
    const int sel = rmei::selected(in, st, r);
    if (u.chanView.load() && sel >= 0) {
        const Pages pg = pagesOf(st, r, sel, cfg, u.chanPage.load());
        if (pg.at < 0) return l;
        l.left  = pg.at > 0;
        l.right = pg.at < static_cast<int>(pg.all.size()) - 1;
        return l;
    }
    return bankLamps(st, in, u);
}

PageLamps bankLamps(const State& st, const rmei::State& in, const Side& u)
{
    PageLamps l;
    const auto r = rmeu::rowAt(in.row.load());
    const int n = static_cast<int>(rmeu::visibleChannels(st, r).size());
    const int f = clampFirst(u.first[static_cast<int>(r)].load(), n);
    l.left  = f > 0;
    l.right = f < clampFirst(n, n);
    return l;
}

Writes mainVolume(const State& st, const rmei::State& in, Side& u, const Config& cfg,
                  int detents, double scale)
{
    Writes out;
    const rmeu::Target t = rmeu::resolveTarget(st, cfg.jogTarget);
    if (!t.visible) return out;
    rmei::nudgeLevel(in, st, t.row, t.ch, detents, cfg.jogStepDb, scale, out);
    if (cfg.uf8ShowMainOnChange) {
        u.mainRow.store(static_cast<int>(t.row));
        u.mainCh.store(t.ch);
        u.mainUntilMs.store(steadyMs() + kMainShowMs);
    }
    return out;
}

namespace {
int submixKeyOutput(const State& st, int key)
{
    if (key < 0 || key >= rmeu::kControlRoomSlots) return -1;
    return rmeu::controlRoomOutputs(st)[static_cast<size_t>(key)];
}
} // namespace

int submixKeyLamp(const State& st, const rmei::State& in, int key)
{
    const int out = submixKeyOutput(st, key);
    if (out < 0) return 0;
    return out == rmeu::effectiveSubmix(st, in.submix.load()) ? 2 : 1;
}

void pickSubmix(const State& st, rmei::State& in, const Config& cfg, int key)
{
    rmei::setSubmix(in, st, cfg, submixKeyOutput(st, key));
}

} // namespace reasixty::rme::uf8side
