// ORC's settings page (docs/orc-settings-web-plan.md): the model against the files
// in a scratch HOME, the server over a real socket.
//
// ⛔ ONLY PORTS THIS TEST WAS GIVEN BY THE SYSTEM, AND HOME ON A SCRATCH FOLDER. No
// rme::manager link, no surface, no Hue, OBS or DynaMount worker: nothing here can
// reach a real program (memory never-send-osc-on-franks-mac). MoreMe's remote only
// starts on ports from freePort, nobody listening.

#include "OrcSock.h"

#include "../orc/OrcLight.h"
#include "Bindings.h"
#include "BindingsHost.h"
#include "DynaMountManager.h"
#include "HueManager.h"
#include "ObsManager.h"
#include "OrcConfig.h"
#include "OrcHandover.h"
#include "OrcModes.h"
#include "OrcSettingsModel.h"
#include "Performers.h"
#include "PhonesServer.h"
#include "RmeBuiltins.h"
#include "RmeInput.h"
#include "RmeManager.h"
#include "RmeSoftKeys.h"
#include "SettingsServer.h"

#include "TestScratch.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

namespace bnd  = uf8::bindings;
namespace dyna = uf8::dynamount;
namespace hue  = uf8::hue;
namespace ph   = orc::phones;
namespace rme  = reasixty::rme;
namespace set  = orc::settings;
using clk = std::chrono::steady_clock;
using orc::sock::kNone;
using orc::sock::sock_t;

static int pollOne(pollfd& p, int ms)
{
    std::vector<pollfd> one{ p };
    const int r = orc::sock::pollAll(one, ms);
    p = one[0];
    return r;
}

static bool has(const std::string& s, const std::string& what) { return s.find(what) != std::string::npos; }
static bool fileExists(const std::string& p) { struct stat sb{}; return ::stat(p.c_str(), &sb) == 0; }
static std::string file(const std::string& p) { return orc_handover::readFile(p); }

static int freePort(int type)
{
    const sock_t fd = ::socket(AF_INET, type, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    socklen_t len = sizeof(a);
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&a), &len);
    orc::sock::closeSock(fd);
    return ntohs(a.sin_port);
}

static sock_t connectTo(int port)
{
    const sock_t fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<std::uint16_t>(port));
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) { orc::sock::closeSock(fd); return kNone; }
    return fd;
}

// One request, with its answer.
static std::string http(int port, const std::string& path, const std::string& headers)
{
    const sock_t fd = connectTo(port);
    if (fd == kNone) return {};
    const std::string req = "GET " + path + " HTTP/1.1\r\n" + headers + "\r\n";
    orc::sock::sendSome(fd, req.data(), req.size());
    std::string in;
    const auto end = clk::now() + std::chrono::seconds(3);
    while (clk::now() < end) {
        pollfd p{ fd, POLLIN, 0 };
        if (pollOne(p, 50) <= 0) continue;
        char buf[8192];
        const long n = orc::sock::recvSome(fd, buf, sizeof(buf));
        if (n <= 0) break;
        in.append(buf, static_cast<std::size_t>(n));
    }
    orc::sock::closeSock(fd);
    return in;
}

// A browser: the upgrade, masked text frames.
struct Browser {
    sock_t fd = kNone;
    std::string in;
    bool connect(int port, const std::string& path, const std::string& host)
    {
        fd = connectTo(port);
        if (fd == kNone) return false;
        const std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\nUpgrade: websocket\r\n"
            "Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
        orc::sock::sendSome(fd, req.data(), req.size());
        const auto end = clk::now() + std::chrono::seconds(3);
        while (clk::now() < end && in.find("\r\n\r\n") == std::string::npos) readSome(50);
        const std::size_t e = in.find("\r\n\r\n");
        if (e == std::string::npos) return false;
        const bool ok = in.compare(0, 12, "HTTP/1.1 101") == 0;
        in.erase(0, e + 4);
        return ok;
    }
    void readSome(int ms)
    {
        pollfd p{ fd, POLLIN, 0 };
        if (pollOne(p, ms) <= 0) return;
        char buf[65536];
        const long n = orc::sock::recvSome(fd, buf, sizeof(buf));
        if (n > 0) in.append(buf, static_cast<std::size_t>(n));
    }
    void sendText(const std::string& s)
    {
        const unsigned char mask[4] = { 1, 2, 3, 4 };
        std::string f;
        f.push_back(static_cast<char>(0x81));
        f.push_back(static_cast<char>(0x80 | s.size()));
        for (const unsigned char m : mask) f.push_back(static_cast<char>(m));
        for (std::size_t i = 0; i < s.size(); ++i) f.push_back(static_cast<char>(s[i] ^ mask[i & 3]));
        orc::sock::sendSome(fd, f.data(), f.size());
    }
    // The newest whole text message within `ms`, "" for none.
    std::string message(int ms)
    {
        std::string got;
        const auto end = clk::now() + std::chrono::milliseconds(ms);
        while (clk::now() < end) {
            readSome(20);
            for (;;) {
                if (in.size() < 2) break;
                const auto b1 = static_cast<unsigned char>(in[1]);
                std::size_t len = b1 & 0x7F, off = 2;
                if (len == 126) {
                    if (in.size() < 4) break;
                    len = (static_cast<unsigned char>(in[2]) << 8) | static_cast<unsigned char>(in[3]);
                    off = 4;
                } else if (len == 127) {
                    if (in.size() < 10) break;
                    len = 0;
                    for (int i = 2; i < 10; ++i) len = (len << 8) | static_cast<unsigned char>(in[static_cast<std::size_t>(i)]);
                    off = 10;
                }
                if (in.size() < off + len) break;
                got = in.substr(off, len);
                in.erase(0, off + len);
            }
            if (!got.empty()) return got;
        }
        return got;
    }
    ~Browser() { if (fd != kNone) orc::sock::closeSock(fd); }
};

static void testAllowed()
{
    using set::requestAllowed;
    EXPECT(requestAllowed("127.0.0.1", "", "", "127.0.0.1:5000", "", 5000));
    EXPECT(requestAllowed("127.0.0.1", "", "", "127.0.0.1:5000", "http://127.0.0.1:5000", 5000));
    EXPECT(!requestAllowed("192.168.1.5", "", "", "127.0.0.1:5000", "", 5000));                      // from the network
    EXPECT(!requestAllowed("127.0.0.1", "8.8.8.8", "", "127.0.0.1:5000", "", 5000));                 // a proxy on this Mac
    EXPECT(!requestAllowed("127.0.0.1", "", "8.8.8.8", "127.0.0.1:5000", "", 5000));
    EXPECT(!requestAllowed("127.0.0.1", "", "", "localhost:5000", "", 5000));                        // a name that could be rebound
    EXPECT(!requestAllowed("127.0.0.1", "", "", "evil.example.com:5000", "", 5000));
    EXPECT(!requestAllowed("127.0.0.1", "", "", "127.0.0.1:5001", "", 5000));
    EXPECT(!requestAllowed("127.0.0.1", "", "", "127.0.0.1:5000", "http://127.0.0.1:8088", 5000));   // another page on this Mac
    EXPECT(!requestAllowed("127.0.0.1", "", "", "127.0.0.1:5000", "http://evil.example.com", 5000));
    EXPECT(!requestAllowed("127.0.0.1", "", "", "127.0.0.1:5000", "null", 5000));
}

static void testRmeEdits()
{
    set::Context c;
    set::apply(R"({"t":"rme","f":"host","v":"10.0.0.7"})", c);
    EXPECT(rme::manager().config().host == "10.0.0.7");
    EXPECT(has(file(orc::rmeConfigPath()), "10.0.0.7"));

    // Nothing changed, nothing written.
    std::remove(orc::rmeConfigPath().c_str());
    set::apply(R"({"t":"rme","f":"host","v":"10.0.0.7"})", c);
    EXPECT(!fileExists(orc::rmeConfigPath()));

    // Ports: a refused one leaves the old and says why.
    const int before = rme::manager().config().sendPort;
    set::apply(R"({"t":"rme","f":"sendPort","v":"70000"})", c);
    EXPECT(rme::manager().config().sendPort == before);
    EXPECT(has(c.error, "1 to 65535"));
    set::apply(R"({"t":"rme","f":"sendPort","v":"7111"})", c);
    EXPECT(rme::manager().config().sendPort == 7111);
    EXPECT(c.error.empty());

    // The clamps.
    set::apply(R"({"t":"rme","f":"uf1PotSpeed","v":"5"})", c);
    EXPECT(rme::manager().config().uf1PotSpeed == rme::Config::kPotSpeedMax);
    set::apply(R"({"t":"rme","f":"uf8PotSpeed","v":"0.01"})", c);
    EXPECT(rme::manager().config().uf8PotSpeed == rme::Config::kPotSpeedMin);
    set::apply(R"({"t":"rme","f":"sleepMinutes","v":"0"})", c);
    EXPECT(rme::manager().config().sleepMinutes == 1);
    set::apply(R"({"t":"rme","f":"sleepMinutes","v":"500"})", c);
    EXPECT(rme::manager().config().sleepMinutes == 99);
    // Letters are no number: nothing changes.
    const double step = rme::manager().config().jogStepDb;
    set::apply(R"({"t":"rme","f":"jogStepDb","v":"abc"})", c);
    EXPECT(rme::manager().config().jogStepDb == step);

    // The switches.
    set::apply(R"({"t":"rme","f":"uf8Enabled","v":"0"})", c);
    EXPECT(!rme::manager().config().uf8Enabled);
    set::apply(R"({"t":"rme","f":"uf8Enabled","v":"1"})", c);
    EXPECT(rme::manager().config().uf8Enabled);

    // ⛔ A target the live list cannot contain stays in the file and shows nothing.
    set::apply(R"({"t":"pot","i":0,"f":"target","v":"input:99"})", c);
    EXPECT(rme::manager().config().vpots[0].target == "input:99");
    const std::string st = set::stateJson(c);
    EXPECT(has(st, R"("pots":[{"target":null)"));
    set::apply(R"({"t":"rme","f":"host","v":"10.0.0.8"})", c);
    EXPECT(has(file(orc::rmeConfigPath()), "input:99"));

    // A push that is no entry is refused.
    const std::string push = rme::manager().config().vpots[0].push;
    set::apply(R"({"t":"pot","i":0,"f":"push","v":"explode"})", c);
    EXPECT(rme::manager().config().vpots[0].push == push);
    set::apply(R"({"t":"pot","i":0,"f":"push","v":"mute"})", c);
    EXPECT(rme::manager().config().vpots[0].push == "mute");
    // Out of range does nothing, and does not crash.
    set::apply(R"({"t":"pot","i":99,"f":"target","v":"main"})", c);
    set::apply(R"({"t":"submixKey","i":99,"v":"main"})", c);
    set::apply(R"({"t":"colour","i":99,"v":"1"})", c);
    set::apply("not json at all", c);

    set::apply(R"({"t":"submixKey","i":2,"v":"phones1"})", c);
    EXPECT(rme::manager().config().uf8SubmixKeys[2] == "phones1");
    set::apply(R"({"t":"colour","i":3,"v":"5"})", c);
    EXPECT(rme::manager().config().colourMap[3] == 5);
}

static void testKeys()
{
    set::Context c;
    const int layer = bnd::getActiveLayer();
    set::apply(R"({"t":"keyAction","k":0,"v":"sleep_now"})", c);
    bnd::Binding bd = bnd::getBinding(layer, bnd::ButtonId::Uf1Rwd);
    EXPECT(bd.shortPress[0].type == bnd::ActionType::Builtin);
    EXPECT(bd.shortPress[0].action == "sleep_now");
    EXPECT(has(set::stateJson(c), R"("keys":[{"n":"Rewind","action":"sleep_now")"));

    // Off is the lamp's own Off; a colour is bright when active, dim otherwise.
    set::apply(R"({"t":"keyLed","k":0,"active":1,"v":"2"})", c);
    bd = bnd::getBinding(layer, bnd::ButtonId::Uf1Rwd);
    EXPECT(bd.brightness == bnd::Brightness::Bright);
    EXPECT(bd.color[0] == 0xFF && bd.color[1] == 0);
    set::apply(R"({"t":"keyLed","k":0,"active":0,"v":"3"})", c);
    bd = bnd::getBinding(layer, bnd::ButtonId::Uf1Rwd);
    EXPECT(bd.inactiveBrightness == bnd::Brightness::Dim);
    EXPECT(bd.inactiveColor[1] == 0xFF);
    set::apply(R"({"t":"keyLed","k":0,"active":1,"v":"0"})", c);
    EXPECT(bnd::getBinding(layer, bnd::ButtonId::Uf1Rwd).brightness == bnd::Brightness::Off);

    // A key's action cleared is None; so is an action ORC cannot run.
    set::apply(R"({"t":"keyAction","k":0,"v":""})", c);
    EXPECT(has(set::stateJson(c), R"("keys":[{"n":"Rewind","action":"")"));
    bd = bnd::getBinding(layer, bnd::ButtonId::Uf1Rwd);
    bd.shortPress[0].type = bnd::ActionType::Builtin;
    bd.shortPress[0].action = "some_reaper_thing";
    bnd::setBinding(layer, bnd::ButtonId::Uf1Rwd, bd);
    EXPECT(has(set::stateJson(c), R"("keys":[{"n":"Rewind","action":"")"));
    set::apply(R"({"t":"keyAction","k":99,"v":"sleep_now"})", c);
}

static void testSoftKeys()
{
    set::Context c;
    const int bank = bnd::kUf1RmeBankBase + 5;
    const auto slot = [&](int i) { return bnd::getUf1SoftBankSlot(bank, i); };

    set::apply(R"({"t":"skFactory","b":5})", c);
    // The first half's label is the key's own, the second half's its set's.
    set::apply(R"({"t":"skLabel","b":5,"h":0,"i":1,"v":"One"})", c);
    EXPECT(slot(1).label == "One");
    EXPECT(slot(1).labelIsUserSet);
    set::apply(R"({"t":"skLabel","b":5,"h":1,"i":1,"v":"Two"})", c);
    EXPECT(slot(1).shortPress[1].label == "Two");
    EXPECT(slot(1).label == "One");

    // A label nobody typed follows the action; a typed one stays.
    set::apply(R"({"t":"skAction","b":5,"h":0,"i":1,"v":"sleep_now"})", c);
    EXPECT(slot(1).shortPress[0].action == "sleep_now");
    EXPECT(slot(1).label == "One");
    set::apply(R"({"t":"skLabel","b":5,"h":0,"i":1,"v":""})", c);
    EXPECT(!slot(1).labelIsUserSet);

    // The colour belongs to the key: a new action keeps it.
    set::apply(R"({"t":"skLed","b":5,"h":0,"i":1,"active":1,"v":"2"})", c);
    EXPECT(slot(1).shortPress[0].led.hasActive);
    set::apply(R"({"t":"skAction","b":5,"h":0,"i":1,"v":""})", c);
    EXPECT(slot(1).shortPress[0].led.hasActive);
    EXPECT(slot(1).shortPress[0].led.color[0] == 0xFF);
    // Clearing the second half's action clears its label too.
    set::apply(R"({"t":"skAction","b":5,"h":1,"i":1,"v":"sleep_now"})", c);
    set::apply(R"({"t":"skAction","b":5,"h":1,"i":1,"v":""})", c);
    EXPECT(slot(1).shortPress[1].label.empty());

    // One name, both halves.
    set::apply(R"({"t":"skName","b":5,"v":"Cue"})", c);
    EXPECT(bnd::getUf1SoftBankName(bank, 0) == "Cue");
    EXPECT(bnd::getUf1SoftBankName(bank, 1) == "Cue");

    // A submix bank: the action is the output. Leaving it takes the outputs along.
    set::apply(R"({"t":"skKind","b":5,"v":"4"})", c);
    EXPECT(bnd::getUf1SoftBankDynamic(bank, 0) == bnd::DynamicBankKind::RmeSubmixes);
    EXPECT(bnd::getUf1SoftBankDynamic(bank, 1) == bnd::DynamicBankKind::None);
    set::apply(R"({"t":"skAction","b":5,"h":1,"i":2,"v":"phones2"})", c);
    EXPECT(rme::softkeys::submixSpec(bank, 6) == "phones2");
    set::apply(R"({"t":"skKind","b":5,"v":"0"})", c);
    EXPECT(bnd::getUf1SoftBankDynamic(bank, 0) == bnd::DynamicBankKind::None);
    EXPECT(rme::softkeys::submixSpec(bank, 6).empty());

    // A bank of snapshots has no label or action of its own.
    set::apply(R"({"t":"skKind","b":5,"v":"1"})", c);
    EXPECT(has(set::stateJson(c), R"("kind":1,"name":"Cue")"));
    set::apply(R"({"t":"skKind","b":5,"v":"0"})", c);

    // Factory: this bank only.
    const int other = bnd::kUf1RmeBankBase + 6;
    set::apply(R"({"t":"skName","b":6,"v":"Stays"})", c);
    set::apply(R"({"t":"skFactory","b":5})", c);
    EXPECT(bnd::getUf1SoftBankName(bank, 0).empty());
    EXPECT(bnd::getUf1SoftBankName(other, 0) == "Stays");
    // Out of range does nothing.
    set::apply(R"({"t":"skName","b":99,"v":"x"})", c);
    set::apply(R"({"t":"skLabel","b":5,"h":7,"i":1,"v":"x"})", c);
    set::apply(R"({"t":"skLabel","b":5,"h":0,"i":7,"v":"x"})", c);

    // What was edited is in orc.json.
    EXPECT(has(file(bnd::configPath()), "Stays"));
}

static void testModes()
{
    set::Context c;
    // ⛔ The password goes in, never out.
    set::apply(R"({"t":"obsPassword","v":"s3cret-word"})", c);
    EXPECT(reasixty::obs::manager().password() == "s3cret-word");
    const std::string st = set::stateJson(c);
    EXPECT(!has(st, "s3cret-word"));
    EXPECT(has(st, R"("hasPassword":true)"));
    set::apply(R"({"t":"obsPassword","v":""})", c);
    EXPECT(has(set::stateJson(c), R"("hasPassword":false)"));
    const int port = reasixty::obs::manager().config().port;
    set::apply(R"({"t":"obs","f":"port","v":"99999"})", c);
    EXPECT(reasixty::obs::manager().config().port == port);
    EXPECT(has(c.error, "1 to 65535"));
    set::apply(R"({"t":"obs","f":"host","v":"10.1.1.1"})", c);
    EXPECT(reasixty::obs::manager().config().host == "10.1.1.1");

    // One field of a stand changes that field.
    auto& dm = dyna::manager();
    set::apply(R"({"t":"dyna","i":2,"f":"name","v":"Vocal"})", c);
    set::apply(R"({"t":"dyna","i":2,"f":"ip","v":"10.2.2.2"})", c);
    set::apply(R"({"t":"dyna","i":2,"f":"colour","v":"3"})", c);
    set::apply(R"({"t":"dyna","i":2,"f":"on","v":"1"})", c);
    const auto in = dm.info(2);
    EXPECT(in.enabled && in.name == "Vocal" && in.ip == "10.2.2.2" && in.color == 3);
    EXPECT(has(file(orc::modes::dir() + "/dynamount_devices"), "10.2.2.2"));
    set::apply(R"({"t":"dyna","i":2,"f":"on","v":"0"})", c);
    EXPECT(!dm.info(2).enabled);
    set::apply(R"({"t":"dynaFill","v":"1"})", c);
    EXPECT(dm.fillDir() == dyna::FillDir::Right);
    EXPECT(orc::modes::read("dynamount_fill") == "R");
    set::apply(R"({"t":"presetName","i":1,"v":"Take"})", c);
    EXPECT(dm.preset(1).name == "Take");
    set::apply(R"({"t":"dyna","i":99,"f":"name","v":"x"})", c);

    // Hue: the labels' lengths, the roles.
    auto& hm = hue::manager();
    set::apply(R"({"t":"hueSlot","i":0,"f":"label","v":"Much too long a label"})", c);
    EXPECT(hm.slot(0).label.size() == static_cast<std::size_t>(hue::kLabelChars));
    set::apply(R"({"t":"hueSlot","i":0,"f":"on","v":"1"})", c);
    EXPECT(hm.slot(0).enabled);
    set::apply(R"({"t":"hueSceneLabel","i":0,"v":"Much too long a label"})", c);
    EXPECT(hm.sceneSlot(0).label.size() == 12);
    set::apply(R"({"t":"hueControls","f":"pot","v":"2"})", c);
    EXPECT(hm.controls().pot == hue::PotRole::Warmth);
    set::apply(R"({"t":"hueControls","f":"flip","v":"3"})", c);
    EXPECT(hm.controls().potFlip == hue::PotRole::Off);
    set::apply(R"({"t":"hueFill","v":"1"})", c);
    EXPECT(hm.fillDir() == hue::HueManager::FillDir::Right);
    set::apply(R"({"t":"hueTarget","i":0,"v":""})", c);
    set::apply(R"({"t":"hueScene","i":0,"v":""})", c);
    // No file for REAPER: nothing imported, and it says so.
    set::apply(R"({"t":"import"})", c);
    EXPECT(c.importResult == "0 values imported");
}

static void testMoreme()
{
    set::Context c;
    c.ownPort = freePort(SOCK_STREAM);
    ph::Settings s;
    s.webPort  = freePort(SOCK_STREAM);
    s.sendPort = freePort(SOCK_DGRAM);   // ⛔ nobody there: never 7003/7004
    s.recvPort = freePort(SOCK_DGRAM);
    ph::server().apply(s, "");

    // The first switch-on makes a PIN.
    set::apply(R"({"t":"mmOn","v":"1"})", c);
    const std::string pin = ph::server().pin();
    EXPECT(pin.size() == 4);
    EXPECT(ph::loadPin() == pin);
    EXPECT(ph::server().settings().enabled);
    EXPECT(has(file(ph::settingsPath()), "\"enabled\": true"));

    // A PIN that is none: the old one stays, the page says why.
    set::apply(R"({"t":"mmPin","v":"12"})", c);
    EXPECT(ph::server().pin() == pin);
    EXPECT(has(c.error, "4 to 8 digits"));
    set::apply(R"({"t":"mmPin","v":"123a"})", c);
    EXPECT(ph::server().pin() == pin);
    set::apply(R"({"t":"mmPin","v":"123456"})", c);
    EXPECT(ph::server().pin() == "123456");
    EXPECT(c.error.empty());

    // The window's own port is not for MoreMe.
    const int web = ph::server().settings().webPort;
    set::apply(("{\"t\":\"mmPort\",\"f\":\"webPort\",\"v\":\"" + std::to_string(c.ownPort) + "\"}").c_str(), c);
    EXPECT(ph::server().settings().webPort == web);
    EXPECT(!c.error.empty());
    set::apply(R"({"t":"mmPort","f":"webPort","v":"99999"})", c);
    EXPECT(ph::server().settings().webPort == web);

    // Studio Control's address comes once the page is served.
    {
        const auto end = clk::now() + std::chrono::seconds(3);
        while (clk::now() < end && !ph::server().status().listening)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        EXPECT(has(set::stateJson(c), "/desk?embed=1"));
    }

    // What a page sends: quotes escaped, the rest as UTF-8, a backslash as \\u005c
    // because wdl_json_parser reads two backslashes as a quote.
    set::apply(R"({"t":"mmStudio","v":"Störsender \"A\" \u005c B"})", c);
    EXPECT(ph::server().settings().studio == "Störsender \"A\" \\ B");
    // And back out, so the page reads what it wrote.
    EXPECT(has(set::stateJson(c), R"("studio":"Störsender \"A\" \\ B")"));
    // ⛔ Names for Phones 1-4 are gone: a file from before is read without them, and
    // the next save drops them.
    {
        const std::string before = file(ph::settingsPath());
        const std::size_t end = before.rfind('}');
        orc_handover::writeFile(ph::settingsPath(), before.substr(0, end) + ",\n  \"names\": [\"Adi\", \"\", \"\", \"\"]\n}\n");
        ph::Settings old;
        EXPECT(ph::loadSettings(old));
        EXPECT(old.studio == ph::server().settings().studio);
        ph::saveSettings(old);
        EXPECT(!has(file(ph::settingsPath()), "Adi"));
        EXPECT(!has(set::stateJson(c), "\"names\""));
    }

    // The logo: through importLogo unchanged; a wrong type says why.
    set::logoChosen("/tmp/not-a-picture.txt", c);
    EXPECT(has(c.error, "PNG"));
    const std::string png = orc::supportDir() + "/source.png";
    orc_handover::writeFile(png, "not really a png");
    const int stamp = c.logoStamp;
    set::logoChosen(png, c);
    EXPECT(c.error.empty());
    EXPECT(ph::server().settings().logo == "moreme-logo.png");
    EXPECT(c.logoStamp == stamp + 1);
    std::string type;
    EXPECT(ph::logoData(type) == "not really a png");
    EXPECT(type == "image/png");
    set::apply(R"({"t":"mmLogoDefault"})", c);
    EXPECT(ph::server().settings().logo.empty());
    EXPECT(ph::logoData(type).empty());

    set::apply(R"({"t":"mmOn","v":"0"})", c);
    EXPECT(!ph::server().settings().enabled);
    EXPECT(!has(set::stateJson(c), "/desk?embed=1"));
    ph::server().stop();
}

// ⛔ rme.json: Rea-Sixty reads it about once a second while ORC writes it, from two
// threads at times (the settings, the surface loop). A reader only ever sees a
// whole file, and no temporary file stays behind.
static void testRmeJsonWhole()
{
    rme::Config one = rme::manager().config(), two = one;
    one.host = "one.example";
    two.host = "two.example";
    EXPECT(orc::saveRmeConfig(one));   // what is in the file before the race starts
    std::atomic<bool> run{true};
    std::thread w1([&] { while (run.load()) orc::saveRmeConfig(one); });
    std::thread w2([&] { while (run.load()) orc::saveRmeConfig(two); });
    int reads = 0, broken = 0;
    const auto end = clk::now() + std::chrono::milliseconds(400);
    while (clk::now() < end) {
        rme::Config got;
        const std::string json = file(orc::rmeConfigPath());
        if (json.empty() || !rme::configFromJson(json, got)
            || (got.host != one.host && got.host != two.host)) ++broken;
        ++reads;
    }
    run.store(false);
    w1.join();
    w2.join();
    EXPECT(reads > 10);
    if (broken) std::printf("FAIL %d of %d reads of rme.json were not a whole file\n", broken, reads);
    EXPECT(broken == 0);
    EXPECT(orc::saveRmeConfig(rme::manager().config()));
    bool leftover = false;
    for (const auto& e : std::filesystem::directory_iterator(orc::supportDir()))
        if (e.path().filename().string().find("rme.json.") == 0) leftover = true;
    EXPECT(!leftover);
}

// ⛔ A backslash through the files: wdl_json_parser reads two of them as a quote.
static void testBackslashInFiles()
{
    ph::Settings a;
    a.studio = "A\\B \"C\"";
    EXPECT(ph::saveSettings(a));
    ph::Settings b;
    EXPECT(ph::loadSettings(b));
    EXPECT(b.studio == a.studio);

    // ⛔ The logo's name is a file in ORC's folder and nothing else: under Windows
    // a backslash and a drive are paths too.
    for (const char* bad : { "../x.png", "..\\x.png", "C:x.png", "sub\\x.png", "..", "." }) {
        ph::Settings l;
        l.logo = bad;
        EXPECT(ph::saveSettings(l));
        ph::Settings r;
        EXPECT(ph::loadSettings(r));
        EXPECT(r.logo.empty());
    }
    {
        ph::Settings l;
        l.logo = "moreme-logo.png";
        EXPECT(ph::saveSettings(l));
        ph::Settings r;
        EXPECT(ph::loadSettings(r));
        EXPECT(r.logo == "moreme-logo.png");
    }
    EXPECT(ph::saveSettings(a));

    ph::Book book;
    ph::Performer p;
    p.id = "abcd1234";
    p.name = "Back\\slash";
    book.performers.push_back(p);
    ph::Book back;
    EXPECT(ph::parseBook(ph::bookJson(book), back));
    EXPECT(back.performers.size() == 1 && back.performers[0].name == p.name);
}

static void testServer(const std::string& res)
{
    bool asked = false;
    set::Host h;
    h.chooseLogo = [&] { asked = true; };
    const std::string url = set::server().start(res, h);
    EXPECT(!url.empty());
    const int port = set::server().port();
    EXPECT(port > 0);
    const std::string me = "127.0.0.1:" + std::to_string(port);
    const std::string base = url.substr(url.find("/s/"));
    const std::string goodHost = "Host: " + me + "\r\n";

    // The page, the font, the logo; only with the token, only under this address.
    const std::string page = http(port, base, goodHost);
    EXPECT(has(page, "HTTP/1.1 200"));
    EXPECT(has(page, "ORC Settings"));
    EXPECT(has(http(port, base + "fonts/inter-latin-400.woff2", goodHost), "font/woff2"));
    EXPECT(has(http(port, base + "fonts/../settings.html", goodHost), "404"));
    EXPECT(has(http(port, base + "logo", goodHost), "image/svg+xml"));
    EXPECT(has(http(port, "/s/0000000000000000/", goodHost), "404"));
    EXPECT(has(http(port, "/", goodHost), "404"));
    EXPECT(has(http(port, base, "Host: evil.example.com\r\n"), "403"));
    EXPECT(has(http(port, base, goodHost + "X-Forwarded-For: 8.8.8.8\r\n"), "403"));
    EXPECT(has(http(port, base, goodHost + "Origin: http://127.0.0.1:8088\r\n"), "403"));

    // ⛔ Not on the network: the Mac's own address does not answer.
    {
        const std::string addr = ph::pageAddress(port);
        if (!addr.empty()) {
            const std::string ip = addr.substr(7, addr.rfind(':') - 7);
            const sock_t fd = ::socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_port = htons(static_cast<std::uint16_t>(port));
            ::inet_pton(AF_INET, ip.c_str(), &a.sin_addr);
            EXPECT(::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0);
            orc::sock::closeSock(fd);
        }
    }

    // The WebSocket: the state at once, an edit answered at once.
    Browser b;
    EXPECT(b.connect(port, base + "ws", me));
    const std::string first = b.message(3000);
    EXPECT(has(first, "\"t\":\"state\""));
    b.sendText(R"({"t":"rme","f":"host","v":"10.9.9.9"})");
    std::string next;
    const auto end = clk::now() + std::chrono::seconds(3);
    while (clk::now() < end && !has(next, "10.9.9.9")) next = b.message(200);
    EXPECT(has(next, "10.9.9.9"));
    EXPECT(has(file(orc::rmeConfigPath()), "10.9.9.9"));

    // The file dialog goes to the host, on the server's thread here.
    b.sendText(R"({"t":"chooseLogo"})");
    const auto end2 = clk::now() + std::chrono::seconds(3);
    while (clk::now() < end2 && !asked) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT(asked);

    // What the platform answers comes in by post.
    set::server().post([](set::Context& c) { c.loginAvailable = true; c.loginOn = true; });
    std::string login;
    const auto end3 = clk::now() + std::chrono::seconds(3);
    while (clk::now() < end3 && !has(login, R"("loginOn":true)")) login = b.message(200);
    EXPECT(has(login, R"("loginOn":true)"));

    // A wrong token never upgrades.
    Browser wrong;
    EXPECT(!wrong.connect(port, "/s/nonsense/ws", me));

    // Nothing listens once the window has closed.
    set::server().stop();
    EXPECT(set::server().port() == 0);
    EXPECT(connectTo(port) < 0);
    // And a new start is a new address.
    const std::string again = set::server().start(res, set::Host{});
    EXPECT(!again.empty() && again != url);
    set::server().stop();
}

// Every control of the old window (the plan's list) is on the page.
static void testPageHasEverything(const std::string& res)
{
    const std::string page = file(res + "/settings.html");
    for (const char* word : {
        "General", "Soft Keys", "Colours", "UF8 Modes", "MoreMe", "Studio Control", "Setup",
        "Talk to TotalMix", "Open at login", "Host", "Send to port", "Listen on port", "Status",
        "Ask TotalMix again", "Sleep when idle", "After", "minutes",
        "Use the UF1", "Target", "Push", "Jog wheel", "Jog step", "Pot step", "V-Pot speed", "Transport",
        "Use the UF8", "Show Main volume on change", "SEND/PLUG-IN", "Output", "Keys",
        "Banks", "Half", "Keys 1-4", "Keys 5-8", "Shows", "Name", "Label", "Action", "Active", "Inactive",
        "Factory set for this bank", "TotalMix snapshots", "TotalMix layouts", "OBS scenes", "TotalMix submixes",
        "Channels", "Import from REAPER", "Talk to OBS", "Port", "Password",
        "DynaMount, LAYER 3", "Fill from left", "Fill from right", "IP address", "Colour", "Detect", "Home",
        "Calibrate", "Presets", "Stands", "Go", "Store", "Clear",
        "Hue, LAYER 2", "Find bridge", "Pair", "Cancel", "Refresh lights + scenes", "Forget", "Scenes", "Key label",
        "V-Pot + FLIP", "Saturation", "Warmth", "mic name", "192.168.1.50",
        "Headphone mixes in the browser", "Address", "PIN", "Page port", "TotalMix send to port",
        "TotalMix listen on port", "Studio name", "Logo", "Choose", "Default Logo",
        "Only for the ORC in Applications." }) {
        if (!has(page, word)) { std::printf("FAIL the page has no \"%s\"\n", word); ++g_fail; }
    }
    EXPECT(page.find("confirm(") == std::string::npos);
    EXPECT(page.find("prompt(") == std::string::npos);
}

int main()
{
    orc::sock::startup();
    // ⛔ The scratch folder first: orc::supportDir() reads it.
    const std::string home = testscratch::makeDir("orc_settings_test");
    if (home.empty()) return 1;
    testscratch::useAsHome(home);

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

    testAllowed();
    testRmeEdits();
    testKeys();
    testSoftKeys();
    testModes();
    testRmeJsonWhole();
    testMoreme();
    testBackslashInFiles();
    testServer(ORC_SETTINGS_DIR);
    testPageHasEverything(ORC_SETTINGS_DIR);

    if (g_fail) { std::printf("test_orc_settings: %d failed\n", g_fail); return 1; }
    std::printf("test_orc_settings: ok\n");
    return 0;
}
