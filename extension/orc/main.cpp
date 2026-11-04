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
//   orc/OrcWebWindow.mm       the window around the settings page. AppKit.
//   orc/SettingsServer.cpp    serves that page, this computer only. Its own thread.
//   orc/OrcSettingsModel.cpp  what the page shows and changes. Plain C++.
//

#include "OrcLog.h"
#include <atomic>
#include <csignal>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>

#if !defined(_WIN32)
#  include <unistd.h>
#endif

#include "Bindings.h"
#include "BindingsHost.h"
#include "LogPath.h"
#include "OrcConfig.h"
#include "OrcFiles.h"
#include "OrcModes.h"
#include "ModeBuiltins.h"
#include "OrcUi.h"
#include "PhonesServer.h"
#include "RmeManager.h"
#include "SettingsServer.h"
#include "Surface.h"

#if defined(_WIN32)
#  include <windows.h>
#  include <timeapi.h>
#endif

namespace rme = reasixty::rme;

namespace {

std::atomic<bool> g_interrupted{false};

// ⛔ A handler does the least it can: one store. Closing a window from inside a
// signal handler would be a call into AppKit from whatever thread the signal
// landed on. The frame callback notices the flag and closes properly.
void onSignal(int) { g_interrupted.store(true); }

orc::Surface* g_surface = nullptr;

// What main() does after the loop, in that order. Once, whoever gets there first:
// the end of the loop, or the session ending under it (OrcUi.h).
void closeDown()
{
    static std::atomic<bool> done{false};
    if (done.exchange(true)) return;
    // First: its thread may be calling into everything below.
    orc::settings::server().stop();
    g_surface->stop();
    orc::phones::server().stop();
    rme::manager().stop();
    orc::modes::saveIfDirty();
    orc::modes::stopWorkers();
    orc::removeOrcPid();
    orc::clearReleased();
    orc::logLine("ORC: closed.\n");
}

} // namespace

int main()
{
    if (orc::handedToRunningCopy()) return 0;

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    // ⇨ ORC ALWAYS WRITES ITS LOG TO A FILE (Frank 26.09.2026). Started from
    // Finder or at login there is no terminal, and on 26.09. the reason the UF1
    // stayed on its boot screen was lost that way. Same folder as Rea-Sixty's
    // log (LogPath), orc.log; over 10 MB at start it is moved to orc.log.1.
    {
        const std::string path = uf8::logPath("orc.log");
        std::error_code ec;
        const auto bytes = std::filesystem::file_size(path, ec);
        if (!ec && bytes > 10 * 1024 * 1024) orc_files::replaceFile(path, path + ".1");
#if defined(_WIN32)
        // ⛔ Not the Mac's lines. ORC has no console, so stdout and stderr have no
        // descriptor, and _isatty or _dup2 on that end the process. And setvbuf with _IOLBF
        // and size 0 is an invalid parameter there, which ends it as well; _IOLBF
        // would be full buffering anyway (Microsoft, setvbuf).
        if (std::freopen(path.c_str(), "a", stdout)) std::setvbuf(stdout, nullptr, _IONBF, 0);
        if (std::freopen(path.c_str(), "a", stderr)) std::setvbuf(stderr, nullptr, _IONBF, 0);
#else
        if (::isatty(STDOUT_FILENO)) std::printf("ORC: logging to %s\n", path.c_str());
        if (std::freopen(path.c_str(), "a", stdout)) {
            ::dup2(::fileno(stdout), STDERR_FILENO);
            std::setvbuf(stdout, nullptr, _IOLBF, 0);
        }
#endif
        const std::time_t now = std::time(nullptr);
        char when[32];
        std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
        std::printf("\n=== ORC start %s ===\n", when);
    }

#if defined(_WIN32)
    // ⛔ A 2 MS SLEEP TAKES 15.5 MS HERE, 2.4 MS WITH THIS (StoerPC, 04.11.2026). The
    // devices start with about a thousand frames, each followed by 2 ms: the UF8
    // took some 15 s to come up. The UF1's pacer and the 33 ms loops wait the same
    // way. Windows 11 may drop the request for a process without a visible window,
    // and all ORC has is a tray icon, so it asks to have it honoured (Microsoft,
    // SetProcessInformation). Earlier systems refuse that, which changes nothing.
    {
        PROCESS_POWER_THROTTLING_STATE honour{};
        honour.Version     = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        honour.ControlMask = PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
        honour.StateMask   = 0;
        ::SetProcessInformation(::GetCurrentProcess(), ProcessPowerThrottling, &honour, sizeof(honour));
    }
    ::timeBeginPeriod(1);
    struct TimerEnd { ~TimerEnd() { ::timeEndPeriod(1); } } timerEnd;
#endif

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
    orc::logLine("ORC: bindings from %s\n", uf8::bindings::configPath().c_str());

    // ⇨ HUE, OBS, DYNAMOUNT (docs/orc-uf8-plan.md stage 6): ORC's own files,
    // and the actions the extension registers, from the same file. No
    // recording light: it follows REAPER's record state (Frank 29.10.2026).
    orc::modes::load();
    reasixty::modes::registerBuiltins(reasixty::modes::BuiltinHost{});

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
    // A channel that leaves TotalMix' layout has to leave the surfaces.
    rme::manager().setRounds(3000);
    // ⛔ Not while REAPER holds the UF1: the TotalMix port goes with the surface
    // (see Surface.cpp, the handover). The surface loop starts the link the
    // moment REAPER lets go.
    if (!orc::reaperWantsUf1()) rme::manager().start();
    orc::logLine("ORC: TotalMix on %s, send %d, receive %d. Settings in %s\n",
                cfg.host.c_str(), cfg.sendPort, cfg.recvPort,
                orc::rmeConfigPath().c_str());
    std::fflush(stdout);

    // ⇨ ORC MOREME, the headphone mixer in the browser (docs/orc-phones-plan.md).
    // Off from the factory. Its own TotalMix remote, which the handover to
    // REAPER leaves alone (PhonesServer.h).
    {
        namespace ph = orc::phones;
        ph::Settings ps;
        ph::loadSettings(ps);
        std::string pin = ph::loadPin();
        // ⛔ On means a PIN. A file deleted by hand gets a new one.
        if (ps.enabled && pin.empty()) {
            pin = ph::generatePin();
            ph::savePin(pin);
        }
        ph::server().apply(ps, pin);
        if (ps.enabled)
            orc::logLine("MoreMe: on, page port %d, TotalMix send %d, receive %d\n",
                         ps.webPort, ps.sendPort, ps.recvPort);
    }

    // ORC runs: Rea-Sixty waits for the receipt only while this pid lives.
    orc::writeOrcPid();

    orc::Surface surface;
    g_surface = &surface;
    surface.start();

    // The platform owns the main thread from here until the user quits.
    orc::runApp(surface, [] { return g_interrupted.load(); }, [] { closeDown(); });
    closeDown();
    return 0;
}
