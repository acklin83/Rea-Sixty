#if defined(_WIN32)
  // ⛔ winsock2.h BEFORE anything that drags in windows.h (JsonTree.h pulls in
  // WDL, and WDL pulls in windows.h), or the old winsock.h wins and every
  // socket type is defined twice. Same trap as WsClient.cpp. CI 2026-09-21.
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <unistd.h>
#endif

#include "RmeManager.h"

#include "JsonTree.h"
#include "RmeOsc.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
  using rme_socket_t = SOCKET;
  static constexpr rme_socket_t kNoSocket = INVALID_SOCKET;
  #define RME_CLOSE closesocket
#else
  using rme_socket_t = int;
  static constexpr rme_socket_t kNoSocket = -1;
  #define RME_CLOSE ::close
#endif

namespace reasixty::rme {

// ── rme.json ─────────────────────────────────────────────────────────────────

std::vector<StripPage> defaultStripPages()
{
    // Parameter ids: RmeStrip.cpp. Output page pot 4 = Ref Level, because the
    // Input page is inputs and playbacks only and an output's ref level would
    // otherwise have no place at all.
    return {
        { "Input",     "in,pb", { "gain", "fxsend", "reflevel", "width" },
                                { "48v", "pad", "phase", "phaseR" } },
        { "Input 2",   "in,pb", { "", "", "", "" },
                                { "stereo", "msproc", "instrument", "autoset" } },
        { "Low Cut",   "",      { "lc_freq", "lc_slope", "", "" },
                                { "lc_on", "", "", "" } },
        { "EQ 1",      "",      { "b1gain", "b1freq", "b1q", "b1type" },
                                { "eq_on", "", "", "" } },
        { "EQ 2",      "",      { "b2gain", "b2freq", "b2q", "" },
                                { "eq_on", "", "", "" } },
        { "EQ 3",      "",      { "b3gain", "b3freq", "b3q", "b3type" },
                                { "eq_on", "", "", "" } },
        { "Dyn",       "",      { "compthres", "compratio", "attack", "release" },
                                { "dyn_on", "", "", "" } },
        { "Expander",  "",      { "expthres", "expratio", "dyngain", "" },
                                { "dyn_on", "", "", "" } },
        { "AutoLevel", "",      { "maxgain", "headroom", "risetime", "" },
                                { "al_on", "", "", "" } },
        { "Output",    "out",   { "balpan", "crossfeed", "delay", "reflevel" },
                                { "loopback", "talkbacksel", "phase", "phaseR" } },
    };
}

std::string configToJson(const Config& c)
{
    // The host is the only free text. It is an address, so quotes and
    // backslashes are not expected; they are dropped rather than escaped, which
    // keeps the file readable and parseable either way.
    std::string host;
    for (const char ch : c.host)
        if (ch != '"' && ch != '\\' && static_cast<unsigned char>(ch) >= 0x20) host += ch;
    auto clean = [](const std::string& v) {
        std::string o;
        for (const char ch : v)
            if (ch != '"' && ch != '\\' && static_cast<unsigned char>(ch) >= 0x20) o += ch;
        return o;
    };
    std::string j;
    char buf[512];
    snprintf(buf, sizeof(buf),
        "{\n"
        "  \"version\": 4,\n"
        "  \"enabled\": %s,\n"
        "  \"connection\": { \"host\": \"%s\", \"send\": %d, \"receive\": %d },\n",
        c.enabled ? "true" : "false", host.c_str(), c.sendPort, c.recvPort);
    j += buf;
    j += "  \"vpots\": [\n";
    for (int i = 0; i < Config::kVpotSlots; ++i) {
        snprintf(buf, sizeof(buf),
            "    { \"target\": \"%s\", \"turn\": \"%s\", \"push\": \"%s\" }%s\n",
            clean(c.vpots[i].target).c_str(), clean(c.vpots[i].turn).c_str(),
            clean(c.vpots[i].push).c_str(), i < Config::kVpotSlots - 1 ? "," : "");
        j += buf;
    }
    j += "  ],\n";
    snprintf(buf, sizeof(buf),
        "  \"jog\": { \"target\": \"%s\", \"stepDb\": %.2f },\n"
        "  \"steps\": { \"vpotDb\": %.2f },\n",
        clean(c.jogTarget).c_str(), c.jogStepDb, c.vpotStepDb);
    j += buf;
    j += "  \"colours\": [";
    for (int i = 0; i < 9; ++i) {
        snprintf(buf, sizeof(buf), "%s%d", i ? ", " : "", c.colourMap[i]);
        j += buf;
    }
    j += "],\n";
    // The channel view, one page per line so it stays editable by hand.
    j += "  \"strip\": [\n";
    auto quad = [&](const std::string (&a)[4]) {
        std::string o = "[";
        for (int i = 0; i < 4; ++i)
            o += std::string(i ? ", " : "") + "\"" + clean(a[i]) + "\"";
        return o + "]";
    };
    for (std::size_t i = 0; i < c.stripPages.size(); ++i) {
        const StripPage& pg = c.stripPages[i];
        j += "    { \"name\": \"" + clean(pg.name) + "\", \"rows\": \"" + clean(pg.rows)
           + "\", \"pots\": " + quad(pg.pots) + ", \"keys\": " + quad(pg.keys) + " }"
           + (i + 1 < c.stripPages.size() ? ",\n" : "\n");
    }
    j += "  ]\n}\n";
    return j;
}

bool configFromJson(const std::string& json, Config& out)
{
    wdl_json_parser p;
    const wdl_json_element* root = p.parse(json.c_str(), static_cast<int>(json.size()));
    JsonTreeGuard guard{p, root};
    if (!root || !root->is_object()) return false;

    Config c = out;
    int version = 1;
    if (const char* v = root->get_string_by_name("version", true)) version = std::atoi(v);
    if (const char* v = root->get_string_by_name("enabled", true))
        c.enabled = (std::strcmp(v, "true") == 0 || std::strcmp(v, "1") == 0);
    if (const wdl_json_element* conn = root->get_item_by_name("connection");
        conn && conn->is_object()) {
        if (const char* v = conn->get_string_by_name("host")) c.host = v;
        if (const char* v = conn->get_string_by_name("send", true))    c.sendPort = std::atoi(v);
        if (const char* v = conn->get_string_by_name("receive", true)) c.recvPort = std::atoi(v);
    }
    if (const wdl_json_element* arr = root->get_item_by_name("vpots"); arr && arr->is_array()) {
        // v2 had four pots: they stay bank 1, and bank 2 keeps its defaults.
        for (int i = 0; i < Config::kVpotSlots; ++i) {
            const wdl_json_element* e = arr->enum_item(i);
            if (!e || !e->is_object()) continue;
            if (const char* v = e->get_string_by_name("target")) c.vpots[i].target = v;
            if (const char* v = e->get_string_by_name("turn"))   c.vpots[i].turn   = v;
            if (const char* v = e->get_string_by_name("push"))   c.vpots[i].push   = v;
            // v1 wrote the old default "select" for every pot. Frank 21.09.:
            // a push on Phones 1-4 must not move the fader, it picks the
            // submix. v1 had no way to say "select" on purpose, so all of it
            // goes.
            if (version < 2 && c.vpots[i].push == "select") c.vpots[i].push = "submix";
        }
    }
    if (const wdl_json_element* jog = root->get_item_by_name("jog"); jog && jog->is_object()) {
        if (const char* v = jog->get_string_by_name("target")) c.jogTarget = v;
        if (const char* v = jog->get_string_by_name("stepDb", true)) c.jogStepDb = std::atof(v);
    }
    if (const wdl_json_element* st = root->get_item_by_name("steps"); st && st->is_object())
        if (const char* v = st->get_string_by_name("vpotDb", true)) c.vpotStepDb = std::atof(v);
    if (const wdl_json_element* arr = root->get_item_by_name("colours"); arr && arr->is_array())
        for (int i = 0; i < 9; ++i)
            if (const wdl_json_element* e = arr->enum_item(i))
                if (const char* v = e->get_string_value(true)) c.colourMap[i] = std::atoi(v) & 0x0F;
    // A file without "strip" (v2 and older) keeps the factory pages.
    if (const wdl_json_element* arr = root->get_item_by_name("strip"); arr && arr->is_array()) {
        std::vector<StripPage> pages;
        for (int i = 0; ; ++i) {
            const wdl_json_element* e = arr->enum_item(i);
            if (!e) break;
            if (!e->is_object()) continue;
            StripPage pg;
            if (const char* v = e->get_string_by_name("name")) pg.name = v;
            if (const char* v = e->get_string_by_name("rows")) pg.rows = v;
            auto quad = [&](const char* key, std::string (&out)[4]) {
                const wdl_json_element* q = e->get_item_by_name(key);
                if (!q || !q->is_array()) return;
                for (int k = 0; k < 4; ++k)
                    if (const wdl_json_element* s = q->enum_item(k))
                        if (const char* v = s->get_string_value()) out[k] = v;
            };
            quad("pots", pg.pots);
            quad("keys", pg.keys);
            pages.push_back(std::move(pg));
        }
        if (!pages.empty()) c.stripPages = std::move(pages);
    }
    if (!(c.jogStepDb > 0.0 && c.jogStepDb <= 12.0))   c.jogStepDb  = Config{}.jogStepDb;
    if (!(c.vpotStepDb > 0.0 && c.vpotStepDb <= 12.0)) c.vpotStepDb = Config{}.vpotStepDb;
    if (c.sendPort <= 0 || c.sendPort > 65535) c.sendPort = Config{}.sendPort;
    if (c.recvPort <= 0 || c.recvPort > 65535) c.recvPort = Config{}.recvPort;
    if (c.host.empty()) c.host = Config{}.host;
    out = c;
    return true;
}

// ── the "no echo" workaround ─────────────────────────────────────────────────

Message localEcho(const std::string& address, float value)
{
    Message m;
    const std::string kLin = "/faderlin";
    if (address.size() > kLin.size()
        && address.compare(address.size() - kLin.size(), kLin.size(), kLin) == 0) {
        const std::string stem = address.substr(0, address.size() - kLin.size());
        // /output/<n>/faderlin reports as /output/<n>/volume,
        // /mix/<in|pb>/<n>/<bus>/faderlin as .../fader.
        m.address = (stem.rfind("/mix/", 0) == 0) ? stem + "/fader" : stem + "/volume";
        m.args.push_back(Arg::fromFloat(static_cast<float>(faderlinToDb(value))));
        return m;
    }
    m.address = address;
    m.args.push_back(Arg::fromFloat(value));
    return m;
}

// ── manager ──────────────────────────────────────────────────────────────────

Manager& manager()
{
    static Manager m;
    return m;
}

Manager::~Manager() { stop(); }

void Manager::start()
{
    if (run_.exchange(true)) return;
    worker_ = std::thread([this] { workerLoop(); });
}

void Manager::stop()
{
    if (!run_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();
}

Config Manager::config() const
{
    std::lock_guard<std::mutex> lk(mx_);
    return cfg_;
}

void Manager::setConfig(const Config& c)
{
    {
        std::lock_guard<std::mutex> lk(mx_);
        const std::string before = configToJson(cfg_);
        if (configToJson(c) == before) return;
        if (!c.sameConnection(cfg_)) ++cfgGen_;   // only then reconnect
        cfg_ = c;
    }
    cfgDirty_.store(true);
}

std::string Manager::status() const
{
    std::lock_guard<std::mutex> lk(mx_);
    return status_;
}

Manager::ControlRoom Manager::controlRoom() const
{
    std::lock_guard<std::mutex> lk(mx_);
    return { state_.dim, state_.mono, state_.speakerB, state_.talkback, state_.mainOut,
             state_.externalIn };
}

Manager::Scenes Manager::scenes() const
{
    std::lock_guard<std::mutex> lk(mx_);
    Scenes sc;
    for (int i = 0; i < 8; ++i) sc.snapshot[i] = state_.snapshot[i];
    sc.lastLayout = state_.lastLayout;
    return sc;
}

State Manager::snapshot() const
{
    std::lock_guard<std::mutex> lk(mx_);
    return state_;
}

void Manager::send(const std::string& address, float value)
{
    if (address.empty() || address[0] != '/') return;
    {
        std::lock_guard<std::mutex> lk(mx_);
        if (!cfg_.enabled) return;
        queue_.emplace_back(address, value);
        ingest(state_, localEcho(address, value));
    }
    rev_.fetch_add(1);
}

void Manager::setStatus(LinkState st, const std::string& text)
{
    link_.store(st);
    std::lock_guard<std::mutex> lk(mx_);
    status_ = text;
}

void Manager::workerLoop()
{
#if defined(_WIN32)
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    using clock = std::chrono::steady_clock;
    rme_socket_t  sock   = kNoSocket;
    std::uint32_t gen    = 0xFFFFFFFFu;
    Config        cfg;
    sockaddr_in   to{};
    auto          lastAsk     = clock::now() - std::chrono::hours(1);
    auto          lastRx      = clock::now() - std::chrono::hours(1);
    auto          retryAt     = clock::now();
    bool          heardSinceAsk = false;
    std::vector<std::uint8_t> buf(65536);

    auto closeSock = [&] {
        if (sock != kNoSocket) { RME_CLOSE(sock); sock = kNoSocket; }
    };
    auto ask = [&] {
        const auto pkt = encodeFloat("/sendall", 1.0f);
        ::sendto(sock, reinterpret_cast<const char*>(pkt.data()),
                 static_cast<int>(pkt.size()), 0,
                 reinterpret_cast<sockaddr*>(&to), sizeof(to));
        lastAsk = clock::now();
        heardSinceAsk = false;
    };

    while (run_.load()) {
        std::uint32_t curGen;
        {
            std::lock_guard<std::mutex> lk(mx_);
            curGen = cfgGen_;
            if (curGen != gen) { cfg = cfg_; }
        }
        if (curGen != gen) {
            gen = curGen;
            closeSock();
            {
                // A different mixer, or the same one on other ports: what we
                // knew no longer describes what is on the other end.
                std::lock_guard<std::mutex> lk(mx_);
                state_ = State{};
                queue_.clear();
            }
            rev_.fetch_add(1);
            retryAt = clock::now();
        }

        if (!cfg.enabled) {
            closeSock();
            if (link_.load() != LinkState::Off) setStatus(LinkState::Off, "off");
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        const auto now = clock::now();

        if (sock == kNoSocket) {
            if (now < retryAt) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            sock = ::socket(AF_INET, SOCK_DGRAM, 0);
            if (sock == kNoSocket) {
                setStatus(LinkState::PortBusy, "could not open a socket");
                retryAt = now + std::chrono::seconds(3);
                continue;
            }
            const bool local = (cfg.host == "127.0.0.1" || cfg.host == "localhost");
            sockaddr_in me{};
            me.sin_family      = AF_INET;
            me.sin_port        = htons(static_cast<std::uint16_t>(cfg.recvPort));
            // Loopback when TotalMix is on this machine, so the OS never asks
            // whether REAPER may accept connections from the network.
            me.sin_addr.s_addr = htonl(local ? INADDR_LOOPBACK : INADDR_ANY);
            if (::bind(sock, reinterpret_cast<sockaddr*>(&me), sizeof(me)) != 0) {
                closeSock();
                char t[160];
                snprintf(t, sizeof(t),
                    "port %d is in use by another program (stoerme, TotalReaper or a second REAPER?)",
                    cfg.recvPort);
                setStatus(LinkState::PortBusy, t);
                retryAt = now + std::chrono::seconds(3);
                continue;
            }
#if defined(_WIN32)
            DWORD tv = 100;
#else
            timeval tv{}; tv.tv_usec = 100000;
#endif
            ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                         reinterpret_cast<const char*>(&tv), sizeof(tv));
            to = sockaddr_in{};
            to.sin_family = AF_INET;
            to.sin_port   = htons(static_cast<std::uint16_t>(cfg.sendPort));
            const std::string h = (cfg.host == "localhost") ? "127.0.0.1" : cfg.host;
            if (::inet_pton(AF_INET, h.c_str(), &to.sin_addr) != 1) {
                closeSock();
                setStatus(LinkState::PortBusy, "host is not an IPv4 address: " + cfg.host);
                retryAt = now + std::chrono::seconds(3);
                continue;
            }
            char t[160];
            snprintf(t, sizeof(t), "waiting for TotalMix on %s:%d",
                          h.c_str(), cfg.sendPort);
            setStatus(LinkState::Waiting, t);
            ask();
        }

        // Outgoing first, so a fader move does not wait behind a /sendall reply.
        std::vector<std::pair<std::string, float>> out;
        {
            std::lock_guard<std::mutex> lk(mx_);
            out.swap(queue_);
        }
        for (const auto& [addr, v] : out) {
            const auto pkt = encodeFloat(addr, v);
            ::sendto(sock, reinterpret_cast<const char*>(pkt.data()),
                     static_cast<int>(pkt.size()), 0,
                     reinterpret_cast<sockaddr*>(&to), sizeof(to));
        }
        if (refreshReq_.exchange(false)) ask();

        const int n = ::recv(sock, reinterpret_cast<char*>(buf.data()),
                             static_cast<int>(buf.size()), 0);
        if (n > 0) {
            std::size_t got = 0;
            {
                std::lock_guard<std::mutex> lk(mx_);
                got = forEachMessage(
                    std::span<const std::uint8_t>(buf.data(), static_cast<std::size_t>(n)),
                    [&](const Message& m) { ingest(state_, m); });
            }
            if (got > 0) {
                rev_.fetch_add(1);
                lastRx = clock::now();
                heardSinceAsk = true;
                if (link_.load() != LinkState::Online) {
                    char t[160];
                    snprintf(t, sizeof(t), "connected to TotalMix (%s:%d, answers on %d)",
                                  cfg.host.c_str(), cfg.sendPort, cfg.recvPort);
                    setStatus(LinkState::Online, t);
                }
            }
        }

        // TotalMix only speaks when something changes, so silence is normal.
        // Every 30 s of it we ask once; no answer within 3 s means it is gone.
        const auto t = clock::now();
        const LinkState ls = link_.load();
        if (ls == LinkState::Waiting || ls == LinkState::Silent) {
            if (t - lastAsk > std::chrono::seconds(3)) ask();
        } else if (ls == LinkState::Online) {
            if (!heardSinceAsk && t - lastAsk > std::chrono::seconds(3)
                && lastAsk > lastRx) {
                setStatus(LinkState::Silent,
                          "TotalMix stopped answering (quit, or the remote is off)");
            } else if (t - lastRx > std::chrono::seconds(30)
                       && t - lastAsk > std::chrono::seconds(30)) {
                ask();
            }
        }
    }

    closeSock();
#if defined(_WIN32)
    WSACleanup();
#endif
}

}  // namespace reasixty::rme
