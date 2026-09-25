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
// Stage 2 (added here): bring up the TotalMix link and print the model it
// answers with. Still no painting. Each half is proven on its own before
// anything is drawn on top of them — three times this week a display question
// turned out to be a device question underneath.
//

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <thread>

#include "RmeManager.h"
#include "RmeUf1.h"
#include "UF1Device.h"
#include "UF1Protocol.h"

namespace rme  = reasixty::rme;
namespace rmeu = reasixty::rme::uf1;

namespace {

std::atomic<bool> g_quit{false};

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

    std::printf("\nORC: closing.\n");
    rme::manager().stop();
    dev.close();
    return 0;
}
