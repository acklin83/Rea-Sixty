//
// ORC — Open Remote Control. The UF1 as a monitor controller for RME TotalMix,
// without a DAW.
//
// ⛔ THE WALL. This program's sources are listed one by one in
// extension/CMakeLists.txt and the extension's main.cpp is not among them. ORC
// shares the model with Rea-Sixty (the protocol, the device, the TotalMix state,
// the strip catalogue, the EQ curve, the bindings engine) because those files
// ask REAPER nothing; it shares nothing else. If a future change makes ORC reach
// into the extension, the link fails, which is the point: the boundary is
// checked on every build rather than remembered.
//
// Stage 1: open the surface, hold it open, print what it sends.
// Stage 2: bring up the TotalMix link and print the model it answers with.
// Stage 3: draw SPREAD through the shared painter in src/Uf1Spread.{h,cpp}.
// Stage 4: STRIP and the EQ curve, same painter, same caller-owned cache.
// Stage 6: run the extension's bindings engine, through four host callbacks.
// Stage 7 (this file's shape): a face on the Mac. macOS gives the main thread
//   to the event loop, so the surface tick moved to orc/Surface.cpp and what is
//   left here is the order things start and stop in.
//
// ⛔ The Mac side is a status item and a settings page, not a mixer on screen.
// Frank, 25.09.2026: the controls are the UF1. What the Mac has to answer is
// who holds the surface, whether TotalMix is talking, and the handful of
// settings a knob cannot reach.
//
// The pieces and what each is allowed to know:
//   orc/Surface.cpp           the UF1 and TotalMix. Its own thread. No AppKit.
//   orc/OrcConfig.cpp         where ORC's own files live. Plain C++.
//   orc/OrcApp.mm             the status item and its menu. AppKit.
//   orc/OrcSettingsWindow.mm  the settings page. AppKit.
//

#include <atomic>
#include <csignal>
#include <cstdio>
#include <ctime>
#include <string>

#include <sys/stat.h>
#include <unistd.h>

#include "Bindings.h"
#include "BindingsHost.h"
#include "LogPath.h"
#include "OrcConfig.h"
#include "OrcUi.h"
#include "RmeManager.h"
#include "Surface.h"

namespace rme = reasixty::rme;

namespace {

std::atomic<bool> g_interrupted{false};

// ⛔ A handler does the least it can: one store. Closing a window from inside a
// signal handler would be a call into AppKit from whatever thread the signal
// landed on. The frame callback notices the flag and closes properly.
void onSignal(int) { g_interrupted.store(true); }

} // namespace

int main()
{
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    // ⇨ ORC ALWAYS WRITES ITS LOG TO A FILE (Frank 26.09.2026). Started from
    // Finder or at login there is no terminal, and on 26.09. the reason the UF1
    // stayed on its boot screen was lost that way. Same folder as Rea-Sixty's
    // log (LogPath), orc.log; over 10 MB at start it is moved to orc.log.1.
    {
        const std::string path = uf8::logPath("orc.log");
        struct stat sb{};
        if (::stat(path.c_str(), &sb) == 0 && sb.st_size > 10 * 1024 * 1024)
            std::rename(path.c_str(), (path + ".1").c_str());
        if (::isatty(STDOUT_FILENO)) std::printf("ORC: logging to %s\n", path.c_str());
        if (std::freopen(path.c_str(), "a", stdout)) {
            ::dup2(::fileno(stdout), STDERR_FILENO);
            std::setvbuf(stdout, nullptr, _IOLBF, 0);
        }
        const std::time_t now = std::time(nullptr);
        char when[32];
        std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
        std::printf("\n=== ORC start %s ===\n", when);
    }

    // ⇨ ORC'S BINDINGS HOST. The engine is the extension's, unchanged and
    // unforked; what differs is the four answers. Two of them ORC does not have:
    // there is no REAPER to look a command id up in, and no window to type a key
    // chord at, so both stay unset and a binding that asks for them does nothing
    // — which is the honest outcome, not a failure.
    {
        uf8::bindings::Host bh;
        bh.configDir  = []() -> std::string { return orc::supportDir(); };
        bh.configFile = []() -> std::string { return "orc.json"; };
        uf8::bindings::setHost(std::move(bh));
    }
    uf8::bindings::load();
    std::printf("ORC: bindings from %s\n", uf8::bindings::configPath().c_str());

    // The link's settings, ORC's own copy of them. ⛔ Not the extension's file:
    // one writer per file (Frank, 2026-09-25). See OrcConfig.h.
    rme::Config cfg;
    const bool hadFile = orc::loadRmeConfig(cfg);
    // A fresh install has no file and the extension's default is "off", because
    // there the TotalMix link is a side-car. In ORC it is the whole job, so the
    // first run comes up talking.
    if (!hadFile) cfg.enabled = true;
    // ⇨ AND WRITE IT DOWN AT ONCE. Rea-Sixty reads this file for its RME
    // side-car and treats a missing one as "no ORC here" (25.09.2026). ORC used
    // to save only after an edit, so a fresh install would have looked absent
    // to REAPER until someone touched a setting.
    if (!hadFile) orc::saveRmeConfig(cfg);
    rme::manager().setConfig(cfg);
    // Loading is not an edit. Clearing the flag here is what stops the first
    // frame from writing the file back out over a file nobody touched.
    rme::manager().takeConfigDirty();
    // ⛔ Not while REAPER holds the UF1: the TotalMix port goes with the surface
    // (see Surface.cpp, the handover). The surface loop starts the link the
    // moment REAPER lets go.
    if (!orc::reaperWantsUf1()) rme::manager().start();
    std::printf("ORC: TotalMix on %s, send %d, receive %d. Settings in %s\n",
                cfg.host.c_str(), cfg.sendPort, cfg.recvPort,
                orc::rmeConfigPath().c_str());
    std::fflush(stdout);

    orc::Surface surface;
    surface.start();

    // AppKit owns the main thread from here until the user quits.
    orc::runApp(surface, [] { return g_interrupted.load(); });

    surface.stop();
    rme::manager().stop();
    std::printf("ORC: closed.\n");
    return 0;
}
