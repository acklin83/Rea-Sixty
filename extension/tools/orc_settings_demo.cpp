// orc_settings_demo: ORC's settings page without ORC, to look at it in a browser or
// offscreen (docs/orc-settings-web-plan.md). Never touches a real mixer: HOME has
// to be a scratch folder, both TotalMix remotes talk to a fake one on ports the
// system handed out, and no surface, Hue, OBS or DynaMount worker runs.
//
// Put it in a folder laid out like a bundle (x.app/Contents/MacOS/demo, with
// Contents/Resources/moreme and settings), so MoreMe finds its page.
// Usage: HOME=<scratch> demo <resources/settings> <seconds>. Writes url.txt to HOME.

#include "../orc/OrcLight.h"
#include "Bindings.h"
#include "BindingsHost.h"
#include "OrcConfig.h"
#include "PhonesServer.h"
#include "RmeBuiltins.h"
#include "RmeInput.h"
#include "RmeManager.h"
#include "RmeOsc.h"
#include "SettingsServer.h"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <netinet/in.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace bnd = uf8::bindings;
namespace rme = reasixty::rme;
namespace ph  = orc::phones;

static std::atomic<bool> g_run{true};

// A TotalMix of a few channels, on a port of its own: answers whoever asks.
static int fakeMix()
{
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    socklen_t len = sizeof(a);
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&a), &len);
    const int port = ntohs(a.sin_port);
    std::thread([fd] {
        struct N { const char* name; const char* value; float colour; };
        const std::vector<std::pair<std::string, N>> chans = {
            { "/input/1",    { "", "Vocal", 1 } }, { "/input/2",    { "", "Guitar", 3 } },
            { "/input/3",    { "", "Bass", 3 } },  { "/input/4",    { "", "Kick", 4 } },
            { "/playback/1", { "", "DAW 1/2", 6 } }, { "/playback/3", { "", "Click", 7 } },
            { "/output/1",   { "", "AN 1/2", 1 } }, { "/output/3",   { "", "AN 3/4", 1 } },
            { "/output/5",   { "", "PH 5/6", 5 } }, { "/output/7",   { "", "PH 7/8", 5 } },
            { "/output/9",   { "", "PH 9/10", 5 } }, { "/output/11",  { "", "PH 11/12", 5 } },
            { "/output/13",  { "", "ADAT 1/2", 2 } } };
        while (g_run.load()) {
            pollfd p{ fd, POLLIN, 0 };
            if (::poll(&p, 1, 200) <= 0) continue;
            unsigned char buf[2048];
            sockaddr_in from{};
            socklen_t l = sizeof(from);
            if (::recvfrom(fd, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &l) <= 0) continue;
            const auto say = [&](const std::vector<std::uint8_t>& pkt) {
                ::sendto(fd, pkt.data(), pkt.size(), 0, reinterpret_cast<sockaddr*>(&from), sizeof(from));
            };
            say(rme::encodeFloat("/controlroom/main", 1));
            say(rme::encodeFloat("/controlroom/mainb", 3));
            say(rme::encodeFloat("/controlroom/phones1", 5));
            say(rme::encodeFloat("/controlroom/phones2", 7));
            say(rme::encodeFloat("/controlroom/phones3", 9));
            say(rme::encodeFloat("/controlroom/phones4", 11));
            for (const auto& [addr, n] : chans) {
                say(rme::encode(addr + "/name", { rme::Arg::fromString(n.value) }));
                say(rme::encodeFloat(addr + "/color", n.colour));
                say(rme::encodeFloat(addr + "/volume", -6.0f));
            }
            // A source is visible to MoreMe once a mix reports it.
            for (const int out : { 5, 7, 9, 11 }) {
                const std::string o = "/" + std::to_string(out) + "/fader";
                for (int i = 1; i <= 4; ++i)
                    say(rme::encodeFloat("/mix/in/" + std::to_string(i) + o, -6.0f - i));
                for (const int pb : { 1, 3 })
                    say(rme::encodeFloat("/mix/pb/" + std::to_string(pb) + o, -3.0f));
            }
        }
        ::close(fd);
    }).detach();
    return port;
}

static int freePort(int type)
{
    const int fd = ::socket(AF_INET, type, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    socklen_t len = sizeof(a);
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&a), &len);
    ::close(fd);
    return ntohs(a.sin_port);
}

int main(int argc, char** argv)
{
    const char* home = std::getenv("HOME");
    if (argc < 3 || !home || std::string(home).find("scratchpad") == std::string::npos) {
        std::fprintf(stderr, "HOME has to be a scratch folder\n");
        return 1;
    }
    bnd::Host bh;
    bh.configDir  = []() -> std::string { return orc::supportDir(); };
    bh.configFile = []() -> std::string { return "orc.json"; };
    bnd::setHost(std::move(bh));
    bnd::load();
    static rme::input::State in;
    static const rme::input::Host host{};
    rme::registerBuiltins(in, host);
    bnd::registerBuiltin("sleep_now", bnd::BuiltinDescriptor{
        [](bool, bool, int) {}, [](int) { return false; }, "Sleep the surface now", false });
    bnd::setBuiltinKind("sleep_now", bnd::BuiltinKind::Switch);
    orc::registerBrightnessBuiltins({});

    rme::Config c;
    c.enabled  = true;
    c.host     = "127.0.0.1";
    c.sendPort = fakeMix();
    c.recvPort = freePort(SOCK_DGRAM);
    rme::manager().setConfig(c);
    rme::manager().takeConfigDirty();
    rme::manager().start();

    ph::Settings ms;
    ms.enabled  = true;
    ms.webPort  = freePort(SOCK_STREAM);
    ms.sendPort = fakeMix();
    ms.recvPort = freePort(SOCK_DGRAM);
    ph::saveSettings(ms);
    ph::server().apply(ms, "4821");

    orc::settings::Host sh;
    const std::string url = orc::settings::server().start(argv[1], sh);
    if (FILE* f = std::fopen((std::string(home) + "/url.txt").c_str(), "w")) {
        std::fputs(url.c_str(), f);
        std::fclose(f);
    }
    std::printf("%s\n", url.c_str());
    std::fflush(stdout);
    std::this_thread::sleep_for(std::chrono::seconds(std::atoi(argv[2])));
    g_run.store(false);
    orc::settings::server().stop();
    ph::server().stop();
    rme::manager().stop();
    return 0;
}
