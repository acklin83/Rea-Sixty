//
// ORC — Open Remote Control. The UF1 as a monitor controller for RME TotalMix,
// without a DAW.
//
// ⛔ THE WALL. This program's sources are listed one by one in
// extension/CMakeLists.txt and main.cpp is not among them. ORC shares the model
// with Rea-Sixty (the protocol, the device, the TotalMix state, the strip
// catalogue, the EQ curve) because those files ask REAPER nothing; it shares
// nothing else. If a future change makes ORC reach into the extension, the link
// fails, which is the point: the boundary is checked on every build rather than
// remembered.
//
// Stage 1: open the surface, hold it open, print what it sends.
// Stage 2: bring up the TotalMix link and print the model it answers with.
// Stage 3 (added here): draw SPREAD on the surface, through the shared painter
// in src/Uf1Spread.{h,cpp}. The renderer is the one Rea-Sixty will use as well;
// what lives here is the gatherer, the half that knows TotalMix.
//

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <thread>

#include "RmeManager.h"
#include "RmeState.h"
#include "RmeStrip.h"
#include "RmeUf1.h"
#include "Uf1Spread.h"
#include "UF1Device.h"
#include "UF1Protocol.h"

namespace rme  = reasixty::rme;
namespace rmeu = reasixty::rme::uf1;
namespace rmes = reasixty::rme::strip;

namespace {

std::atomic<bool> g_quit{false};

// Which of the two views has the screen. The input handler runs on the device's
// worker thread and the tick reads it, so it is atomic rather than a plain bool.
// Stage 5 puts this on a binding; until then the channel-encoder push is the way
// between the views, hard-wired, so stage 4 can be seen on the glass.
std::atomic<bool> g_showStrip{false};

void onSignal(int) { g_quit.store(true); }

const char* kindName(uf1::InputKind k)
{
    switch (k) {
        case uf1::InputKind::FaderTouch:    return "fader-touch";
        case uf1::InputKind::FaderPosition: return "fader-pos";
        case uf1::InputKind::EncoderRotate: return "encoder";
        case uf1::InputKind::EncoderTouch:  return "encoder-touch";
        case uf1::InputKind::Button:        return "button";
    }
    return "?";
}

const char* linkName(rme::LinkState st)
{
    switch (st) {
        case rme::LinkState::Off:      return "off";
        case rme::LinkState::PortBusy: return "port busy";
        case rme::LinkState::Waiting:  return "waiting";
        case rme::LinkState::Online:   return "online";
        case rme::LinkState::Silent:   return "silent";
    }
    return "?";
}

// What TotalMix has told us so far. Printed once when the link comes up and
// again whenever the state revision moves, so a snapshot recall or a renamed
// channel is visible without a display.
void dumpModel(const rme::State& st)
{
    const auto cr = rme::manager().controlRoom();
    std::printf("\n-- control room -------------------------------------------\n");
    std::printf("   main out %d   main B %d   phones %d %d %d %d\n",
                st.mainOut, st.mainOutB, st.phones[0], st.phones[1],
                st.phones[2], st.phones[3]);
    std::printf("   dim %d  mono %d  speaker B %d  talkback %d\n",
                cr.dim, cr.mono, cr.speakerB, cr.talkback);

    // ⛔ visibleChannels, not the raw map. A parameter that exists per side of a
    // stereo pair arrives under index+1, so counting anything that ever sent a
    // value reported 94 outputs where the mixer shows 5. A strip announces
    // itself with a NAME; that is what makes it a channel.
    for (auto row : { rmeu::Row::Input, rmeu::Row::Playback, rmeu::Row::Output }) {
        const auto list = rmeu::visibleChannels(st, row);
        std::printf("-- %-8s %2zu channels", rmeu::rowName(row), list.size());
        for (size_t i = 0; i < list.size() && i < 6; ++i)
            std::printf("  %d:%s", list[i], rmeu::displayName(st, row, list[i]).c_str());
        std::printf("%s\n", list.size() > 6 ? "  ..." : "");
    }
    std::fflush(stdout);
}

// ⇨ THE GATHERER. It knows TotalMix; the renderer it hands this to knows the
// UF1 and nothing else. Same split as Uf1EqCurve, for the same reason: the half
// that can be tested should not be the half that needs a mixer attached.
uf1spread::View viewFor(const rme::State& st, const rme::Config& cfg, int row)
{
    uf1spread::View v;
    const auto r = static_cast<rmeu::Row>(row);
    const int submix = rmeu::effectiveSubmix(st, -1);

    const auto list = rmeu::visibleChannels(st, r);
    const int sel = list.empty() ? -1 : list.front();

    char hdr[64];
    std::snprintf(hdr, sizeof hdr, "%-8s %s", rmeu::rowName(r),
                  submix >= 0 ? rmeu::displayName(st, rmeu::Row::Output, submix).c_str() : "");
    v.header   = hdr;
    v.timecode = sel >= 0 ? rmeu::displayName(st, r, sel) : std::string();

    // The four pots carry the control-room roles the config puts on them. A role
    // TotalMix has not assigned leaves its pot empty rather than letting the
    // others move up: a snapshot can reassign roles under the hand, and a pot
    // that wanders is worse than one that is blank.
    for (int i = 0; i < 4; ++i) {
        const auto& slot = cfg.vpots[i];
        auto& pot = v.pots[static_cast<std::size_t>(i)];
        if (slot.target.empty()) continue;
        const auto t = rmeu::resolveTarget(st, slot.target);
        if (!t.assigned || !t.visible) continue;
        bool known = false;
        const double db = rmeu::levelDb(st, t.row, t.ch, submix, known);
        if (!known) continue;
        pot.name  = rmeu::displayName(st, t.row, t.ch).substr(0, 8);
        char val[24];
        std::snprintf(val, sizeof val, "%.1fdB", db <= rme::kDbOff ? -99.0 : db);
        pot.line  = val;
        pot.norm  = rme::dbToFaderlin(db);
        pot.empty = false;
    }

    if (sel >= 0) {
        v.chName   = rmeu::displayName(st, r, sel);
        v.chNumber = std::to_string(sel);
        v.chActive = true;
        if (const auto* ch = rmeu::channelOf(st, r, sel))
            v.palette = cfg.colourMap[std::clamp(ch->colour, 0, 8)];
        bool known = false;
        const double db = rmeu::levelDb(st, r, sel, submix, known);
        if (known) {
            char t[16];
            std::snprintf(t, sizeof t, "%.1f", db <= rme::kDbOff ? -99.0 : db);
            v.chDb = t;
            v.faderPos = static_cast<int>(std::lround(rme::dbToFaderlin(db) * 0x7FFF));
        }
    }
    return v;
}

// STRIP, the channel view. Same split: this half knows TotalMix, the renderer
// it feeds knows the UF1.
uf1spread::StripView stripViewFor(const rme::State& st, const rme::Config& cfg,
                                  int row, int sel)
{
    uf1spread::StripView v;
    const auto r = static_cast<rmeu::Row>(row);
    if (sel < 0) return v;

    v.name   = rmeu::displayName(st, r, sel);
    v.number = std::to_string(sel);
    v.active = true;
    if (const auto* ch = rmeu::channelOf(st, r, sel))
        v.palette = cfg.colourMap[std::clamp(ch->colour, 0, 8)];

    const int submix = rmeu::effectiveSubmix(st, -1);
    bool known = false;
    const double db = rmeu::levelDb(st, r, sel, submix, known);
    if (known) {
        char t[16];
        std::snprintf(t, sizeof t, "%.1f", db <= rme::kDbOff ? -99.0 : db);
        v.db = t;
    }

    // The page the channel can actually show. A page that does not apply to this
    // row is not offered, so the type cell never names something the channel
    // has not got.
    const auto pages = rmes::availablePages(st, r, sel, cfg.stripPages);
    if (!pages.empty()) {
        const auto& pg = cfg.stripPages[static_cast<std::size_t>(pages.front())];
        v.csType = pg.name;
        if (const auto* p = rmes::find(pg.pots[0])) {
            double raw = 0.0;
            if (rmes::value(st, r, sel, *p, raw)) {
                v.line = rmes::label(st, r, sel, *p) + "  " + rmes::format(*p, r, raw);
                v.barPos = static_cast<int>(std::lround(rmes::norm(*p, r, raw) * 100.0));
            }
        }
    }

    // ⛔ eqModel answers for THIS channel or says the EQ is out. A flat graph is
    // the honest answer to "no EQ here"; the wrong channel's curve never is.
    v.eq = rmeu::eqModel(st, r, sel);
    return v;
}

} // namespace

int main()
{
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    uf1::UF1Device dev;

    // The handler fires on the device's worker thread. Printing is all this
    // stage does, so there is nothing here that needs the main thread yet; the
    // tick that will own the painting comes in stage 2.
    dev.setInputHandler([](const uf1::InputEvent& ev) {
        if (ev.kind == uf1::InputKind::Button && ev.id == uf1::btn::kChannelPush
            && ev.pressed) {
            g_showStrip.store(!g_showStrip.load());
            std::printf("ORC: view -> %s\n", g_showStrip.load() ? "STRIP" : "SPREAD");
        }
        switch (ev.kind) {
            case uf1::InputKind::FaderPosition:
                std::printf("%-14s pos=%5u\n", kindName(ev.kind), ev.position);
                break;
            case uf1::InputKind::EncoderRotate:
                std::printf("%-14s id=0x%02X delta=%+d\n", kindName(ev.kind), ev.id, ev.delta);
                break;
            default:
                std::printf("%-14s id=0x%02X %s\n", kindName(ev.kind), ev.id,
                            ev.pressed ? "down" : "up");
                break;
        }
        std::fflush(stdout);
    });

    if (!dev.open()) {
        std::fprintf(stderr, "ORC: cannot open the UF1: %s\n", dev.lastError().c_str());
        std::fprintf(stderr, "     The surface belongs to one program at a time. Quit SSL 360,\n"
                             "     or REAPER if Rea-Sixty is holding it, and try again.\n");
        return 1;
    }

    std::printf("ORC: UF1 open, serial %s.\n", dev.serial().c_str());

    // The link runs its own socket and worker, exactly as it does inside the
    // extension. ORC only asks it for a snapshot when the revision moves.
    rme::Config cfg;
    cfg.enabled = true;
    rme::manager().setConfig(cfg);
    rme::manager().start();
    std::printf("ORC: TotalMix on %s, send %d, receive %d. Ctrl-C to stop.\n",
                cfg.host.c_str(), cfg.sendPort, cfg.recvPort);
    std::fflush(stdout);

    auto lastLink = rme::LinkState::Off;
    std::uint64_t lastRev = 0;
    auto lastDump = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    uf1spread::Cache paintCache;
    uf1spread::StripCache stripCache;
    long painted = 0;
    const int row = static_cast<int>(rmeu::Row::Output);

    while (!g_quit.load()) {
        // Same rule as the extension's tick: a stale handle after a USB
        // re-enumeration is reopened rather than nursed.
        if (dev.needsReopen()) {
            std::printf("ORC: the handle went stale, reopening\n");
            std::fflush(stdout);
            dev.close();
            if (!dev.open()) {
                std::fprintf(stderr, "ORC: reopen failed: %s\n", dev.lastError().c_str());
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }
        const auto link = rme::manager().link();
        if (link != lastLink) {
            std::printf("ORC: link %s (%s)\n", linkName(link),
                        rme::manager().status().c_str());
            std::fflush(stdout);
            lastLink = link;
        }

        // ⛔ revision() counts every meter value too — 741 bumps in 30 seconds
        // on a quiet mixer. It answers "did anything arrive", not "did anything
        // change that is worth drawing", so the painter in stage 3 cannot use it
        // as its repaint trigger. Here it only gates a once-a-second dump.
        // ⇨ DRAW. The view is rebuilt every tick and the painter decides what
        // actually goes on the wire, which is why revision() being useless as a
        // trigger costs nothing: an unchanged picture writes zero frames, and
        // the test pins that.
        if (link == rme::LinkState::Online) {
            const auto st = rme::manager().snapshot();
            auto sink = [&dev](std::vector<std::uint8_t> f) { dev.send(std::move(f)); };
            if (g_showStrip.load()) {
                const auto list = rmeu::visibleChannels(st, static_cast<rmeu::Row>(row));
                painted += uf1spread::paintStrip(
                    stripViewFor(st, cfg, row, list.empty() ? -1 : list.front()),
                    stripCache, sink);
            } else {
                painted += uf1spread::paint(viewFor(st, cfg, row), paintCache, sink);
            }
        }

        const auto rev = rme::manager().revision();
        const auto now = std::chrono::steady_clock::now();
        if (link == rme::LinkState::Online && rev != lastRev
            && now - lastDump >= std::chrono::seconds(1)) {
            lastRev  = rev;
            lastDump = now;
            dumpModel(rme::manager().snapshot());
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    std::printf("\nORC: closing. %ld frames painted.\n", painted);
    rme::manager().stop();
    dev.close();
    return 0;
}
