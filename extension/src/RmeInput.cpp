#include "RmeInput.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace reasixty::rme::input {

namespace {

// ⛔ THE CHANNEL ENCODER EMITS ABOUT FOUR COUNTS PER DETENT. REAPER's own path
// gathers them into whole steps with this; without it the row list jumped over
// Playback on a single detent (Frank 21.09.: "loest zu fein auf, komm nur auf
// outputs und inputs"). Same number as kChannelEncoderScale in main.cpp, and it
// moved here with the code that uses it.
constexpr double kChannelEncoderScale = 4.0;

bool modeMenu(const Host& h) { return h.modeMenuOpen && h.modeMenuOpen(); }
double knobScale(const Host& h) { return h.knobScale ? h.knobScale() : 1.0; }
bool online(const Host& h) { return !h.online || h.online(); }

int vpotSlot(const State& s, int pot)
{
    return pot + 4 * std::clamp(s.vpotBank.load(), 0, Config::kVpotBanks - 1);
}

// Gather counts into whole detents through `acc`, throwing the remainder away
// on a direction change. Returns 0 until a whole step has arrived.
int wholeSteps(std::atomic<double>& acc, int delta)
{
    double a = acc.load();
    if ((delta > 0 && a < 0.0) || (delta < 0 && a > 0.0)) a = 0.0;
    a += delta / kChannelEncoderScale;
    int steps = 0;
    if (a >=  1.0) { steps = static_cast<int>(a); a -= steps; }
    if (a <= -1.0) { steps = static_cast<int>(a); a -= steps; }
    acc.store(a);
    return steps;
}

void append(Writes& out, const Writes& w) { out.insert(out.end(), w.begin(), w.end()); }

// Level of a channel shifted by `detents`, in dB. Inputs and playbacks write
// their node in the current submix.
void nudge(const State& s, const Host& h, const rme::State& st, rmeu::Row r, int ch,
           int detents, double stepDb, Writes& out)
{
    stepDb *= knobScale(h);
    const int sub = rmeu::effectiveSubmix(st, s.submix.load());
    if (r != rmeu::Row::Output && sub < 0) return;
    bool known = false;
    double cur = rmeu::levelDb(st, r, ch, sub, known);
    if (!known) cur = kDbOff;
    const double v = rmeu::nudgeDb(cur, detents, stepDb);
    out.push_back({ rmeu::levelAddress(r, ch, sub, /*faderlin*/ false),
                    static_cast<float>(v) });
}

} // namespace

// ── queries ──────────────────────────────────────────────────────────────────

int selected(const State& s, const rme::State& st, rmeu::Row r)
{
    const int cur = s.sel[static_cast<int>(r)].load();
    const auto list = rmeu::visibleChannels(st, r);
    if (std::find(list.begin(), list.end(), cur) != list.end()) return cur;
    if (r == rmeu::Row::Output && st.outputForRole(st.mainOut)) return st.mainOut;
    return list.empty() ? -1 : list.front();
}

int stripPage(const State& s, const rme::State& st, rmeu::Row r, int sel,
              const Config& cfg)
{
    const auto pages = rmes::availablePages(st, r, sel, cfg.stripPages);
    if (pages.empty()) return -1;
    const int want = s.stripPage.load();
    if (std::find(pages.begin(), pages.end(), want) != pages.end()) return want;
    return pages.front();
}

const rmes::Param* stripParam(const Config& cfg, int page, bool key, int i)
{
    if (page < 0 || page >= static_cast<int>(cfg.stripPages.size()) || i < 0 || i > 3)
        return nullptr;
    const auto& pg = cfg.stripPages[static_cast<size_t>(page)];
    return rmes::find(key ? pg.keys[i] : pg.pots[i]);
}

void select(State& s, rmeu::Row r, int ch)
{
    s.row.store(static_cast<int>(r));
    s.sel[static_cast<int>(r)].store(ch);
    if (r == rmeu::Row::Output) s.submix.store(ch);
}

// The row one on, WITH WRAP at both ends (Frank 22.09.). One step mechanism for
// both ways there: the nav cross up/down and MODE plus the channel encoder.
void stepRow(State& s, int dir)
{
    const int n = rmeu::kRowCount;
    const int cur = std::clamp(s.row.load(), 0, n - 1);
    s.row.store((cur + (dir > 0 ? 1 : n - 1)) % n);
}

// ⇨ THE V-POT BANK FOLLOWS WHAT YOU STEPPED TO (Frank 25.09.: nav, then "mit
// encoder bitte auch"). If channel `ch` of row `r` sits on a pot of the other
// bank, 5-8's bank switches to it, so it is on the glass. The current bank wins
// a tie, so a channel on both banks never makes the row jump; a channel on no
// pot leaves the bank alone. One rule for both ways of stepping.
void followBank(State& s, const rme::State& st, const Config& cfg, rmeu::Row r, int ch)
{
    const int bankNow = std::clamp(s.vpotBank.load(), 0, Config::kVpotBanks - 1);
    int found = -1;
    for (int slot = 0; slot < Config::kVpotSlots; ++slot) {
        const auto& spec = cfg.vpots[slot].target;
        if (spec.empty()) continue;
        const rmeu::Target t = rmeu::resolveTarget(st, spec);
        if (!t.visible || t.row != r || t.ch != ch) continue;
        const int bank = slot / 4;
        if (bank == bankNow) { found = bankNow; break; }
        if (found < 0) found = bank;
    }
    if (found >= 0) s.vpotBank.store(found);
}

// The submix one place on, through the visible outputs, with wrap (Frank
// 23.09., nav left/right). The submix is the output inputs and playbacks write
// into; the row and the selection stay where they are.
void stepSubmix(State& s, const rme::State& st, const Config& cfg, int dir)
{
    const auto outs = rmeu::visibleChannels(st, rmeu::Row::Output);
    if (outs.empty()) return;
    const int cur = rmeu::effectiveSubmix(st, s.submix.load());
    const auto at  = std::find(outs.begin(), outs.end(), cur);
    const int  idx = (at == outs.end()) ? 0 : static_cast<int>(at - outs.begin());
    const int  n   = static_cast<int>(outs.size());
    const int  to  = outs[static_cast<size_t>((idx + (dir > 0 ? 1 : n - 1)) % n)];
    s.submix.store(to);
    followBank(s, st, cfg, rmeu::Row::Output, to);
}

void toggleWindow(State& s, const Host& h, Writes& out)
{
    if (!online(h)) return;
    const bool want = !s.windowShown.load();
    out.push_back({ "/showwindow", want ? 1.0f : 0.0f });
    s.windowShown.store(want);
}

void faderMainFire(State& s, const rme::State& st)
{
    if (st.mainOut >= 0) select(s, rmeu::Row::Output, st.mainOut);
}

bool faderMainActive(const State& s, const rme::State& st)
{
    const int sel = s.sel[static_cast<int>(rmeu::Row::Output)].load();
    return st.mainOut >= 0 && s.row.load() == static_cast<int>(rmeu::Row::Output)
        && (sel == st.mainOut || sel < 0);
}

// ── encoders ─────────────────────────────────────────────────────────────────

bool encoder(State& s, const Host& h, const rme::State& st, const Config& cfg,
             std::uint8_t id, int delta, Writes& out)
{
    if (id >= ::uf1::enc::kVpot1 && id <= ::uf1::enc::kVpot4 && s.strip.load()) {
        // STRIP: the pot turns its page's parameter on the fader channel.
        const auto r   = static_cast<rmeu::Row>(std::clamp(s.row.load(), 0, 2));
        const int  sel = selected(s, st, r);
        const int  pg  = sel >= 0 ? stripPage(s, st, r, sel, cfg) : -1;
        const int  pot = id - ::uf1::enc::kVpot1;
        const rmes::Param* p = stripParam(cfg, pg, false, pot);
        if (!p) return true;
        if (rmes::stepsWhole(*p)) {
            const int steps = wholeSteps(s.potAccum[pot], delta);
            if (steps != 0) append(out, rmes::nudge(st, r, sel, *p, steps));
            return true;
        }
        append(out, rmes::nudge(st, r, sel, *p, delta, knobScale(h)));
        return true;
    }
    if (id >= ::uf1::enc::kVpot1 && id <= ::uf1::enc::kVpot4) {
        const auto& slot = cfg.vpots[vpotSlot(s, id - ::uf1::enc::kVpot1)];
        if (slot.turn != "volume") return true;
        const rmeu::Target t = rmeu::resolveTarget(st, slot.target);
        if (t.visible) nudge(s, h, st, t.row, t.ch, delta, cfg.vpotStepDb, out);
        return true;
    }
    if (id == ::uf1::enc::kChannel) {
        const int steps = wholeSteps(s.chAccum, delta);
        if (steps == 0) return true;
        if (modeMenu(h)) {
            // MODE plus the channel encoder: the row (Input / Playback /
            // Output), with wrap like the nav cross (Frank 22.09.).
            stepRow(s, steps);
            return true;
        }
        const auto r = static_cast<rmeu::Row>(std::clamp(s.row.load(), 0, 2));
        const int n = rmeu::stepChannel(rmeu::visibleChannels(st, r),
                                        selected(s, st, r), steps);
        if (n >= 0) {
            select(s, r, n);
            followBank(s, st, cfg, r, n);
        }
        return true;
    }
    if (id == ::uf1::enc::kJog) {
        const rmeu::Target t = rmeu::resolveTarget(st, cfg.jogTarget);
        if (t.visible) nudge(s, h, st, t.row, t.ch, delta, cfg.jogStepDb, out);
        return true;
    }
    if (id == ::uf1::enc::kVpotAboveFader) {
        // ⇨ PAN OF THE FADER CHANNEL (Frank 22.09.: "der Pan auf UF1 Channel
        // macht noch nichts"). Input/playback into the submix, an output itself
        // (rmeu::panAddress). 2 % per count, fine as everywhere in the side-car.
        const auto r   = static_cast<rmeu::Row>(std::clamp(s.row.load(), 0, 2));
        const int  sel = selected(s, st, r);
        const int  sub = rmeu::effectiveSubmix(st, s.submix.load());
        const std::string a = rmeu::panAddress(r, sel, sub);
        if (a.empty()) return true;
        bool known = false;
        const double cur = rmeu::panValue(st, r, sel, sub, known);
        const double nv  = std::clamp(cur + delta * 0.02 * knobScale(h), -1.0, 1.0);
        if (nv != cur) out.push_back({ a, static_cast<float>(nv) });
        return true;
    }
    return true;
}

// ── the display soft keys while STRIP is open ────────────────────────────────
// ⇨ IN STRIP THE KEYS BELONG TO THE PAGE (Frank 21.09., plan 6a): the page's
// four switches, and < > page through the pages instead of the banks.

bool stripSoftKey(State& s, const Host& h, const rme::State& st, const Config& cfg,
                  const ::uf1::InputEvent& ev, Writes& out)
{
    const std::uint8_t id = ev.id;
    if (!s.strip.load()) return false;
    if (!((id >= ::uf1::btn::kDisplaySoft1 && id <= ::uf1::btn::kDisplaySoft4)
          || id == ::uf1::btn::kArrowLeft || id == ::uf1::btn::kArrowRight))
        return false;
    if (!ev.pressed || modeMenu(h)) return true;

    const auto r   = static_cast<rmeu::Row>(std::clamp(s.row.load(), 0, 2));
    const int  sel = selected(s, st, r);
    if (sel < 0) return true;
    const int  pg  = stripPage(s, st, r, sel, cfg);
    if (pg < 0) return true;

    if (id == ::uf1::btn::kArrowLeft || id == ::uf1::btn::kArrowRight) {
        const auto pages = rmes::availablePages(st, r, sel, cfg.stripPages);
        const int at  = static_cast<int>(std::find(pages.begin(), pages.end(), pg)
                                         - pages.begin());
        const int dir = (id == ::uf1::btn::kArrowRight) ? 1 : -1;
        // No wrap: the list has ends, like the row list.
        const int to  = std::clamp(at + dir, 0, static_cast<int>(pages.size()) - 1);
        s.stripPage.store(pages[static_cast<size_t>(to)]);
        return true;
    }
    if (const rmes::Param* p = stripParam(cfg, pg, true, id - ::uf1::btn::kDisplaySoft1))
        append(out, rmes::press(st, r, sel, *p));
    return true;
}

// ── buttons ──────────────────────────────────────────────────────────────────

bool button(State& s, const Host& h, const rme::State& st, const Config& cfg,
            const ::uf1::InputEvent& ev, Writes& out)
{
    const std::uint8_t id = ev.id;

    // ⇨ MASTER BESIDE THE FADER IS `rme_fader_main` IN THE SIDE-CAR (Frank
    // 22.09.: "master button auf dem UF1 Fader soll dasselbe wie die neue
    // Built-In MASTER auf softbank 2 machen"). Same function, not the same line
    // written twice.
    if (id == ::uf1::btn::kMaster) {
        if (ev.pressed && !modeMenu(h)) faderMainFire(s, st);
        return true;
    }
    // ⇨ NAV UP / DOWN PICKS THE ROW (Frank 22.09.), with wrap, in STRIP too:
    // there the view stays open and shows the new row's channel.
    if (id == ::uf1::btn::kNavUp || id == ::uf1::btn::kNavDown) {
        if (ev.pressed && !modeMenu(h)) stepRow(s, id == ::uf1::btn::kNavDown ? 1 : -1);
        return true;
    }
    // ⇨ LEFT AND RIGHT STEP THE SUBMIX, with wrap as well (Frank 23.09.).
    if (id == ::uf1::btn::kNavLeft || id == ::uf1::btn::kNavRight) {
        if (ev.pressed && !modeMenu(h))
            stepSubmix(s, st, cfg, id == ::uf1::btn::kNavRight ? 1 : -1);
        return true;
    }
    // ⇨ AND THE CENTRE SHOWS TOTALMIX, where the ARC has its dim (Frank 23.09.).
    if (id == ::uf1::btn::kNavCentre) {
        if (ev.pressed && !modeMenu(h)) toggleWindow(s, h, out);
        return true;
    }
    if (id == ::uf1::btn::kChannelPush) {
        // STRIP open and shut. Opening only when a channel is on the fader.
        if (ev.pressed && !modeMenu(h)) {
            if (s.strip.load()) {
                s.strip.store(false);
            } else {
                const auto r = static_cast<rmeu::Row>(std::clamp(s.row.load(), 0, 2));
                if (selected(s, st, r) >= 0) s.strip.store(true);
            }
        }
        return true;
    }
    // In STRIP a pot press puts its parameter back to the neutral value
    // (RmeStrip::resetWrites, Frank 22.09.); 5-8 does nothing there, the V-Pot
    // banks belong to the overview.
    if (s.strip.load() && id >= ::uf1::btn::kVpot1Push && id <= ::uf1::btn::kVpot4Push) {
        if (ev.pressed) {
            const auto r   = static_cast<rmeu::Row>(std::clamp(s.row.load(), 0, 2));
            const int  sel = selected(s, st, r);
            const int  pg  = sel >= 0 ? stripPage(s, st, r, sel, cfg) : -1;
            if (const rmes::Param* p = stripParam(cfg, pg, false,
                                                  id - ::uf1::btn::kVpot1Push))
                append(out, rmes::resetWrites(st, r, sel, *p));
        }
        return true;
    }
    if (s.strip.load() && id == ::uf1::btn::k5to8) return true;

    if (id >= ::uf1::btn::kVpot1Push && id <= ::uf1::btn::kVpot4Push) {
        if (!ev.pressed) return true;
        const auto& slot = cfg.vpots[vpotSlot(s, id - ::uf1::btn::kVpot1Push)];
        const rmeu::Target t = rmeu::resolveTarget(st, slot.target);
        if (!t.visible) return true;
        if (slot.push == "submix") {
            // Only outputs are a submix. With an input or a playback on the
            // fader it stays there and writes into this output from now on.
            // ⇨ WITH AN OUTPUT ON THE FADER THE FADER GOES ALONG (Frank 22.09.,
            // point 7): otherwise the fader showed output A while the submix
            // was B, two selections with two marks (* and >). Now there is one,
            // the submix, and it carries the white line.
            if (t.row == rmeu::Row::Output) {
                if (s.row.load() == static_cast<int>(rmeu::Row::Output))
                    select(s, t.row, t.ch);
                else
                    s.submix.store(t.ch);
            }
        } else if (slot.push == "select") {
            select(s, t.row, t.ch);
        } else if (slot.push == "mute") {
            const Channel* c = rmeu::channelOf(st, t.row, t.ch);
            out.push_back({ rmeu::muteAddress(t.row, t.ch),
                            (c && c->mute) ? 0.0f : 1.0f });
        }
        return true;
    }
    if (id == ::uf1::btn::kVpotAboveFaderPush || id == ::uf1::btn::kChannelSoftKey) {
        if (!ev.pressed) return true;
        const auto r   = static_cast<rmeu::Row>(std::clamp(s.row.load(), 0, 2));
        const int  sel = selected(s, st, r);
        if (sel < 0) return true;
        if (id == ::uf1::btn::kVpotAboveFaderPush) {
            // Press on the pan pot: centre, as in REAPER's channel view.
            const std::string a = rmeu::panAddress(r, sel,
                                      rmeu::effectiveSubmix(st, s.submix.load()));
            if (!a.empty()) out.push_back({ a, 0.0f });
            return true;
        }
        // ⇨ THE SOFT KEY ABOVE THE CHANNEL IS STEREO/MONO of the fader channel
        // (Frank 22.09.). /<sec>/<n>/stereo, written by TotalReaper itself
        // (Stereo-Pair Link). Afterwards fetch both halves again: on unlinking,
        // n+1 becomes a channel of its own, and TotalMix sends the remote that
        // wrote it no echo.
        const Channel* c = rmeu::channelOf(st, r, sel);
        const std::string sec = std::string("/") + rmes::section(r) + "/";
        out.push_back({ sec + std::to_string(sel) + "/stereo",
                        (c && c->stereo) ? 0.0f : 1.0f });
        out.push_back({ rmes::sendChanAddress(r, sel), 1.0f });
        out.push_back({ rmes::sendChanAddress(r, sel + 1), 1.0f });
        return true;
    }
    // 5-8 switches the two V-Pot banks, like the track group in the DAW view.
    if (id == ::uf1::btn::k5to8) {
        if (ev.pressed) s.vpotBank.store((s.vpotBank.load() + 1) % Config::kVpotBanks);
        return true;
    }

    // ⇨ SOLO, CUT, SEL BELONG TO THE FADER CHANNEL IN TOTALMIX (Frank 21.09.:
    // "sollten die nicht im Side-Car Mode komplett weg von Reaper? Sonst sind ja
    // Side-Car und standalone ORC nie dasselbe"). Until then they fell through
    // to REAPER and switched the focused track.
    if (id == ::uf1::btn::kCut || id == ::uf1::btn::kSolo || id == ::uf1::btn::kSel) {
        if (!ev.pressed) return true;
        const auto r   = static_cast<rmeu::Row>(std::clamp(s.row.load(), 0, 2));
        const int  sel = selected(s, st, r);
        if (sel < 0) return true;
        const int  sub = rmeu::effectiveSubmix(st, s.submix.load());
        if (id == ::uf1::btn::kCut) {
            const Channel* c = rmeu::channelOf(st, r, sel);
            out.push_back({ rmeu::muteAddress(r, sel), (c && c->mute) ? 0.0f : 1.0f });
        } else if (id == ::uf1::btn::kSolo) {
            // Solo sits in the routing: input/playback into the submix. An
            // output has none, so the key does nothing there.
            const std::string a = rmeu::soloAddress(r, sel, sub);
            if (!a.empty())
                out.push_back({ a, rmeu::soloed(st, r, sel, sub) ? 0.0f : 1.0f });
        } else if (r == rmeu::Row::Output) {
            // SEL on an output makes it the submix, like a click in TotalMix.
            s.submix.store(sel);
        }
        return true;
    }

    // ⇨ WAY b (Frank 21.09.): everything that concerns a channel goes to
    // TotalMix; the transport stays REAPER's, and in ORC these keys are
    // unbound. SHIFT stays a modifier. Everything else the side-car swallows
    // and does nothing with, so no key secretly switches a REAPER track.
    switch (id) {
        case ::uf1::btn::kShift:
        case ::uf1::btn::kRwd:  case ::uf1::btn::kFfw:  case ::uf1::btn::kStop:
        case ::uf1::btn::kPlay: case ::uf1::btn::kRec:
        case ::uf1::btn::kCycle: case ::uf1::btn::kClick:
        // ⇨ AND THE 360 KEY keeps its binding (Frank 23.09.). What sits there is
        // his call; an action on the time field stays ineffective while the
        // side-car holds the screen.
        case ::uf1::btn::k360:
            return false;
        default:
            return true;
    }
}

} // namespace reasixty::rme::input
