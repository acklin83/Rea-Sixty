// orc/PhonesServer end to end (docs/orc-phones-plan.md, stages 2 and 3): a
// browser's WebSocket in, a TotalMix remote out, with everything in between
// running as ORC runs it.
//
// ⛔ ONLY PORTS THIS TEST WAS GIVEN BY THE SYSTEM. The "TotalMix" here is a UDP
// socket this test binds on loopback port 0, and the server's own ports are
// picked the same way, so nothing it sends can reach a real program (memory
// never-send-osc-on-franks-mac: a test once sent a gain to a running TotalMix).

#include "OrcSock.h"

#include "PhonesServer.h"
#include "RmeOsc.h"
#include "TestScratch.h"
#include "WebSocket.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <cstdlib>
#include <thread>
#include <vector>

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

namespace rme = reasixty::rme;
namespace ph  = orc::phones;
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

// A port nobody holds: bind 0 on loopback, read what the system chose, let go.
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

// The fake TotalMix: bound on its own port 0, answers whoever asked.
struct FakeMix {
    sock_t      fd = kNone;
    int         port = 0;
    sockaddr_in from{};
    bool        heard = false;
    std::vector<std::string> got;   // "address value"

    explicit FakeMix(int wanted = 0)
    {
        fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(static_cast<std::uint16_t>(wanted));
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a));
        socklen_t len = sizeof(a);
        ::getsockname(fd, reinterpret_cast<sockaddr*>(&a), &len);
        port = ntohs(a.sin_port);
    }
    ~FakeMix() { orc::sock::closeSock(fd); }
    // Read what arrived for up to `ms`.
    void pump(int ms)
    {
        const auto end = clk::now() + std::chrono::milliseconds(ms);
        while (clk::now() < end) {
            pollfd p{ fd, POLLIN, 0 };
            if (pollOne(p, 20) <= 0) continue;
            unsigned char buf[2048];
            socklen_t len = sizeof(from);
            const long n = ::recvfrom(fd, reinterpret_cast<char*>(buf), sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &len);
            if (n <= 0) continue;
            heard = true;
            rme::forEachMessage(std::span<const std::uint8_t>(buf, static_cast<std::size_t>(n)),
                                [&](const rme::Message& m) {
                char v[32];
                snprintf(v, sizeof(v), " %g", m.args.empty() ? 0.0 : m.args[0].number());
                got.push_back(m.address + v);
            });
        }
    }
    bool saw(const std::string& line) const
    {
        for (const auto& g : got) if (g == line) return true;
        return false;
    }
    void say(const std::string& address, float v) { send(rme::encodeFloat(address, v)); }
    void sayName(const std::string& address, const std::string& s)
    {
        send(rme::encode(address, { rme::Arg::fromString(s) }));
    }
    void send(const std::vector<std::uint8_t>& pkt)
    {
        ::sendto(fd, reinterpret_cast<const char*>(pkt.data()), static_cast<int>(pkt.size()), 0, reinterpret_cast<sockaddr*>(&from), sizeof(from));
    }
};

// A browser: TCP, the upgrade, masked text frames.
struct Browser {
    sock_t      fd = kNone;
    std::string in;
    bool        open = false;

    bool connect(int port, const std::string& extraHeader = {})
    {
        fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(static_cast<std::uint16_t>(port));
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) return false;
        const std::string req = "GET /ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\n"
                                "Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                                "Sec-WebSocket-Version: 13\r\n" + extraHeader + "\r\n";
        orc::sock::sendSome(fd, req.data(), req.size());
        const auto end = clk::now() + std::chrono::seconds(3);
        while (clk::now() < end && in.find("\r\n\r\n") == std::string::npos) readSome(50);
        const std::size_t e = in.find("\r\n\r\n");
        if (e == std::string::npos) return false;
        open = in.compare(0, 12, "HTTP/1.1 101") == 0
            && in.find("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos;
        in.erase(0, e + 4);
        return open;
    }
    void readSome(int ms)
    {
        pollfd p{ fd, POLLIN, 0 };
        if (pollOne(p, ms) <= 0) return;
        char buf[8192];
        const long n = orc::sock::recvSome(fd, buf, sizeof(buf));
        if (n > 0) in.append(buf, static_cast<std::size_t>(n));
        else open = false;
    }
    void sendText(const std::string& s)
    {
        const unsigned char mask[4] = { 1, 2, 3, 4 };
        std::string f;
        f.push_back(static_cast<char>(0x81));
        if (s.size() < 126) f.push_back(static_cast<char>(0x80 | s.size()));
        else { f.push_back(static_cast<char>(0x80 | 126)); f.push_back(static_cast<char>(s.size() >> 8)); f.push_back(static_cast<char>(s.size() & 0xFF)); }
        for (const unsigned char m : mask) f.push_back(static_cast<char>(m));
        for (std::size_t i = 0; i < s.size(); ++i) f.push_back(static_cast<char>(s[i] ^ mask[i & 3]));
        orc::sock::sendSome(fd, f.data(), f.size());
    }
    // Text messages that arrived within `ms`, unmasked server frames.
    std::string messages(int ms)
    {
        std::string all;
        const auto end = clk::now() + std::chrono::milliseconds(ms);
        while (clk::now() < end) {
            readSome(20);
            for (;;) {
                if (in.size() < 2) break;
                const auto b1 = static_cast<unsigned char>(in[1]);
                std::size_t len = b1 & 0x7F, off = 2;
                if (len == 126) { if (in.size() < 4) break; len = (static_cast<unsigned char>(in[2]) << 8) | static_cast<unsigned char>(in[3]); off = 4; }
                if (in.size() < off + len) break;
                if ((static_cast<unsigned char>(in[0]) & 0x0F) == 0x1) all += in.substr(off, len) + "\n";
                in.erase(0, off + len);
            }
        }
        return all;
    }
    ~Browser() { if (fd != kNone) orc::sock::closeSock(fd); }
};

// One HTTP GET, with its answer.
static std::string httpGet(int port, const std::string& path, const std::string& headers)
{
    const sock_t fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<std::uint16_t>(port));
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    std::string in;
    if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0) {
        const std::string req = "GET " + path + " HTTP/1.1\r\n" + headers + "\r\n";
        orc::sock::sendSome(fd, req.data(), req.size());
        const auto end = clk::now() + std::chrono::seconds(2);
        while (clk::now() < end) {
            pollfd p{ fd, POLLIN, 0 };
            if (pollOne(p, 50) <= 0) continue;
            char buf[4096];
            const long n = orc::sock::recvSome(fd, buf, sizeof(buf));
            if (n <= 0) break;
            in.append(buf, static_cast<std::size_t>(n));
        }
    }
    orc::sock::closeSock(fd);
    return in;
}

static bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

int main()
{
    orc::sock::startup();
    // ⛔ A test on this computer must not read Frank's performers, nor anything
    // write to them.
    testscratch::useAsHome(testscratch::makeDir("orc-phones-test-"));

    FakeMix mix;
    ph::Settings s;
    s.enabled  = true;
    s.webPort  = freePort(SOCK_STREAM);
    s.sendPort = mix.port;
    s.recvPort = freePort(SOCK_DGRAM);
    ph::Server srv;
    srv.apply(s, "2468");
    // ⛔ Studio Control: refused through a proxy, whichever way it comes in.
    {
        const auto t0 = clk::now();
        while (clk::now() - t0 < std::chrono::seconds(2) && !srv.status().listening)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const std::string direct = httpGet(s.webPort, "/desk", "Host: 127.0.0.1\r\n");
        // No page in this test's bundle: 404. What counts is that it isn't 403.
        EXPECT(direct.compare(0, 12, "HTTP/1.1 403") != 0);
        EXPECT(httpGet(s.webPort, "/desk", "Host: 127.0.0.1\r\nX-Forwarded-For: 8.8.8.8\r\n").compare(0, 12, "HTTP/1.1 403") == 0);
        EXPECT(httpGet(s.webPort, "/desk", "Host: 127.0.0.1\r\nX-Real-IP: 8.8.8.8\r\n").compare(0, 12, "HTTP/1.1 403") == 0);
        EXPECT(httpGet(s.webPort, "/desk", "Host: phones.example.com\r\n").compare(0, 12, "HTTP/1.1 403") == 0);
        const std::string up = "Host: 127.0.0.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                               "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n";
        EXPECT(httpGet(s.webPort, "/deskws", up + "Origin: http://evil.example.com\r\n").compare(0, 12, "HTTP/1.1 403") == 0);
        EXPECT(httpGet(s.webPort, "/deskws", up + "X-Forwarded-For: 8.8.8.8\r\n").compare(0, 12, "HTTP/1.1 403") == 0);
        EXPECT(httpGet(s.webPort, "/p/..%2f", "Host: x\r\n").compare(0, 12, "HTTP/1.1 404") == 0);
    }

    // The second remote asks at once and every 3 s.
    mix.pump(1500);
    EXPECT(mix.heard && mix.saw("/sendall 1"));
    // TotalMix answers: Phones 1 on output 8, input 1 visible.
    mix.say("/controlroom/phones1", 8);
    mix.sayName("/output/8/name", "Drums");
    mix.sayName("/input/1/name", "Kick");
    mix.say("/mix/in/1/8/fader", -6.0f);

    {
        Browser b;
        EXPECT(b.connect(s.webPort));
        b.sendText(R"({"t":"hello","pin":"1111"})");
        EXPECT(has(b.messages(400), "\"wrong\":1"));
        b.sendText(R"({"t":"hello","pin":"2468"})");
        const std::string cfg = b.messages(400);
        EXPECT(has(cfg, "\"t\":\"config\""));
        EXPECT(has(cfg, "\"label\":\"Drums\""));
        EXPECT(has(cfg, "\"tmSeen\":true"));

        // The round after the next /sendall says which inputs are visible:
        // answer the next ask with input 1 again.
        mix.got.clear();
        const auto end = clk::now() + std::chrono::seconds(5);
        while (clk::now() < end && !mix.saw("/sendall 1")) mix.pump(100);
        mix.say("/mix/in/1/8/fader", -6.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(600));   // the 400 ms window

        b.sendText(R"({"t":"select","mix":"phone1"})");
        const std::string snap = b.messages(500);
        EXPECT(has(snap, "\"t\":\"snapshot\""));
        EXPECT(has(snap, R"("in:1":{"db":-6,)"));
        EXPECT(has(snap, R"("label":"Kick")"));

        // A fader on the phone reaches TotalMix.
        mix.got.clear();
        b.sendText(R"({"t":"set","mix":"phone1","src":"in:1","ctrl":"fader","val":-3})");
        mix.pump(400);
        EXPECT(mix.saw("/mix/in/1/8/fader -3"));
        EXPECT(has(b.messages(200), R"("src":"in:1","db":-3)"));

        // TotalMix moves it: the phone follows.
        mix.say("/mix/in/1/8/fader", -12.0f);
        EXPECT(has(b.messages(400), R"("src":"in:1","db":-12)"));

        // A second phone sees it taken.
        Browser c;
        EXPECT(c.connect(s.webPort));
        c.sendText(R"({"t":"hello","pin":"2468"})");
        EXPECT(has(c.messages(400), "\"locked\":true"));
        EXPECT(srv.status().phones == 2 && srv.status().held == 1);

        // A new PIN: both back to the PIN page.
        srv.apply(s, "1357");
        EXPECT(has(b.messages(400), "\"t\":\"pin\""));
    }
    // Both closed: nobody left.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT(srv.status().phones == 0);

    // A plain page request: no page in a test binary, a clean 404 and a close.
    {
        const sock_t fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(static_cast<std::uint16_t>(s.webPort));
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        EXPECT(::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0);
        const std::string req = "GET /nothing HTTP/1.1\r\nHost: x\r\n\r\n";
        orc::sock::sendSome(fd, req.data(), req.size());
        std::string resp;
        const auto end = clk::now() + std::chrono::seconds(2);
        while (clk::now() < end) {
            pollfd p{ fd, POLLIN, 0 };
            if (pollOne(p, 50) <= 0) continue;
            char buf[1024];
            const long n = orc::sock::recvSome(fd, buf, sizeof(buf));
            if (n <= 0) break;   // closed after the answer
            resp.append(buf, static_cast<std::size_t>(n));
        }
        EXPECT(resp.compare(0, 22, "HTTP/1.1 404 Not Found") == 0);
        orc::sock::closeSock(fd);
    }

    // Off: the port is free again.
    s.enabled = false;
    srv.apply(s, "1357");
    {
        Browser d;
        EXPECT(!d.connect(s.webPort));
    }

    // TotalMix not there yet: sends go to a closed port, which Windows answers with
    // WSAECONNRESET on the next read. The link has to outlive that and find the
    // TotalMix that turns up.
    {
        const int late = freePort(SOCK_DGRAM);
        ph::Settings t = s;
        t.enabled  = true;
        t.webPort  = freePort(SOCK_STREAM);
        t.sendPort = late;
        t.recvPort = freePort(SOCK_DGRAM);
        ph::Server waits;
        waits.apply(t, "1357");
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        FakeMix arrives(late);
        arrives.pump(4000);
        EXPECT(arrives.heard && arrives.saw("/sendall 1"));
        t.enabled = false;
        waits.apply(t, "1357");
    }

    if (g_fail) { std::printf("test_orc_phones_server: %d failure(s)\n", g_fail); return 1; }
    std::printf("test_orc_phones_server: ok\n");
    return 0;
}
