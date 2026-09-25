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
// Stage 1 (this file today): open the surface, hold it open, and print what it
// sends. No painting, no TotalMix. It exists so the USB half is proven on its
// own before anything is drawn on top of it — three times this week a display
// question turned out to be a device question underneath.
//

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <thread>

#include "UF1Device.h"
#include "UF1Protocol.h"

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

    std::printf("ORC: UF1 open, serial %s. Ctrl-C to stop.\n", dev.serial().c_str());
    std::fflush(stdout);

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
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    std::printf("\nORC: closing.\n");
    dev.close();
    return 0;
}
