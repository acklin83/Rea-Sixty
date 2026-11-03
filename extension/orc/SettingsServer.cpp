// ⛔ First: winsock2.h before anything that brings windows.h in.
#include "OrcSock.h"

#include "SettingsServer.h"

#include "OrcConfig.h"
#include "OrcHandover.h"
#include "OrcLog.h"
#include "PhonesServer.h"
#include "WebSocket.h"

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <map>

namespace orc::settings {

namespace ph = orc::phones;
using orc::sock::kNone;
using orc::sock::sock_t;

namespace {

constexpr std::size_t kMaxConns   = 8;
constexpr std::size_t kMaxHead    = 16 * 1024;
constexpr std::size_t kMaxPending = 4 * 1024 * 1024;

struct Conn {
    sock_t      fd = kNone;
    std::string peer;
    std::string in, out;
    bool        ws      = false;
    bool        closing = false;   // close once `out` is sent
    bool        dead    = false;
    std::string sent;              // the state it was last sent
    ws::Reader  reader;
    std::chrono::steady_clock::time_point lastRx;
};

std::string lower(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string makeToken()
{
    unsigned char b[16];
    orc::randomBytes(b, sizeof(b));
    static const char k[] = "0123456789abcdef";
    std::string o;
    for (const unsigned char c : b) { o.push_back(k[c >> 4]); o.push_back(k[c & 15]); }
    return o;
}

const char* fontType(const std::string& name)
{
    // A name from this list and nothing else: the path comes from the network.
    for (const char* f : { "inter-latin-400.woff2", "inter-latin-500.woff2", "inter-latin-700.woff2",
                           "inter-latin-900.woff2", "inter-latin-ext-400.woff2", "inter-latin-ext-500.woff2",
                           "inter-latin-ext-700.woff2", "inter-latin-ext-900.woff2" })
        if (name == f) return "font/woff2";
    return nullptr;
}

std::string serve(const ws::Request& r, const std::string& base, const std::string& res)
{
    const std::string notFound = ws::response(404, "text/plain; charset=utf-8", "not found");
    if (r.method != "GET") return ws::response(405, "text/plain; charset=utf-8", "method not allowed");
    if (r.path == base) {
        const std::string page = orc_handover::readFile(res + "/settings.html");
        return page.empty() ? notFound : ws::response(200, "text/html; charset=utf-8", page);
    }
    if (r.path == base + "logo") {
        std::string type;
        std::string data = ph::logoData(type);
        if (data.empty()) {
            data = orc_handover::readFile(res + "/../moreme/moreme-logo.svg");
            type = "image/svg+xml";
        }
        return data.empty() ? notFound : ws::response(200, type, data);
    }
    const std::string fonts = base + "fonts/";
    if (r.path.rfind(fonts, 0) == 0) {
        const std::string name = r.path.substr(fonts.size());
        if (const char* t = fontType(name)) {
            const std::string data = orc_handover::readFile(res + "/fonts/" + name);
            if (!data.empty()) return ws::response(200, t, data);
        }
    }
    return notFound;
}

} // namespace

bool requestAllowed(const std::string& peer, const std::string& xRealIp, const std::string& xForwardedFor,
                    const std::string& host, const std::string& origin, int port)
{
    if (peer != "127.0.0.1") return false;
    if (!xRealIp.empty() || !xForwardedFor.empty()) return false;
    const std::string me = "127.0.0.1:" + std::to_string(port);
    if (lower(host) != me) return false;
    return origin.empty() || lower(origin) == "http://" + me;
}

Server& server()
{
    static Server s;
    return s;
}

Server::~Server() { stop(); }

std::string Server::start(const std::string& resources, const Host& host)
{
    stop();
    orc::sock::startup();
    const sock_t fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == kNone) return {};
    sockaddr_in me{};
    me.sin_family      = AF_INET;
    me.sin_port        = 0;                         // the system picks
    me.sin_addr.s_addr = htonl(INADDR_LOOPBACK);    // ⛔ never the network
    socklen_t len = sizeof(me);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&me), sizeof(me)) != 0 || ::listen(fd, 8) != 0
        || ::getsockname(fd, reinterpret_cast<sockaddr*>(&me), &len) != 0) {
        orc::sock::closeSock(fd);
        orc::logLine("Settings: could not open a socket\n");
        return {};
    }
    orc::sock::setNonBlocking(fd);
    const int port = ntohs(me.sin_port);
    port_.store(port);
    const std::string token = makeToken();
    const bool loginAvailable = ctx_.loginAvailable;
    const bool loginOn        = ctx_.loginOn;
    ctx_ = Context{};
    ctx_.host           = host;
    ctx_.ownPort        = port;
    ctx_.loginAvailable = loginAvailable;
    ctx_.loginOn        = loginOn;
    run_.store(true);
    worker_ = std::thread([this, fd, token, resources] { loop(static_cast<std::uintptr_t>(fd), token, resources); });
    return "http://127.0.0.1:" + std::to_string(port) + "/s/" + token + "/";
}

void Server::stop()
{
    if (run_.exchange(false) && worker_.joinable()) worker_.join();
    port_.store(0);
    std::lock_guard<std::mutex> lk(mx_);
    queue_.clear();
}

void Server::post(std::function<void(Context&)> fn)
{
    std::lock_guard<std::mutex> lk(mx_);
    queue_.push_back(std::move(fn));
}

void Server::loop(std::uintptr_t listenValue, std::string token, std::string resources)
{
    const sock_t listenFd = static_cast<sock_t>(listenValue);
    using clock = std::chrono::steady_clock;
    const std::string base = "/s/" + token + "/";
    const std::string wsPath = base + "ws";
    const int myPort = port_.load();
    std::map<sock_t, Conn> conns;
    auto lastState = clock::now() - std::chrono::hours(1);
    bool force = false;   // an edit: the page hears back even if nothing changed

    auto queue = [](Conn& c, const std::string& bytes) {
        c.out += bytes;
        if (c.out.size() > kMaxPending) c.dead = true;
    };
    auto flush = [](Conn& c) {
        while (!c.out.empty()) {
            const long n = orc::sock::sendSome(c.fd, c.out.data(), c.out.size());
            if (n > 0) { c.out.erase(0, static_cast<std::size_t>(n)); continue; }
            if (n < 0 && orc::sock::wouldBlock()) return;
            c.dead = true;
            return;
        }
        if (c.closing) c.dead = true;
    };
    auto onRequest = [&](Conn& c) {
        const std::size_t end = c.in.find("\r\n\r\n");
        if (end == std::string::npos) {
            if (c.in.size() > kMaxHead) c.dead = true;
            return;
        }
        ws::Request r;
        const std::string head = c.in.substr(0, end + 4);
        c.in.erase(0, end + 4);
        c.closing = true;
        if (!ws::parseRequest(head, r)) {
            queue(c, ws::response(400, "text/plain; charset=utf-8", "bad request"));
            return;
        }
        if (!requestAllowed(c.peer, r.header("x-real-ip"), r.header("x-forwarded-for"),
                            r.header("host"), r.header("origin"), myPort)) {
            orc::logLine("Settings: refused a request from %s\n", c.peer.c_str());
            queue(c, ws::response(403, "text/plain; charset=utf-8", "this window only"));
            return;
        }
        if (r.path == wsPath && ws::isUpgrade(r)) {
            queue(c, ws::upgradeResponse(r));
            c.ws = true;
            c.closing = false;
            force = true;
            return;
        }
        queue(c, serve(r, base, resources));
    };
    auto onFrames = [&](Conn& c) {
        for (;;) {
            ws::Frame f;
            const ws::Take t = ws::takeFrame(c.in, f);
            if (t == ws::Take::Need) return;
            if (t == ws::Take::Bad) { c.dead = true; return; }
            std::string text;
            switch (c.reader.feed(f, text)) {
                case ws::Reader::Got::Nothing: break;
                case ws::Reader::Got::Message:
                    orc::settings::apply(text, ctx_);
                    force = true;
                    break;
                case ws::Reader::Got::Ping: queue(c, ws::encodeFrame(ws::kPong, text)); break;
                case ws::Reader::Got::Close:
                    queue(c, ws::encodeFrame(ws::kClose, {}));
                    c.closing = true;
                    return;
                case ws::Reader::Got::Bad: c.dead = true; return;
            }
        }
    };

    while (run_.load()) {
        std::vector<pollfd> fds;
        fds.reserve(conns.size() + 1);
        fds.push_back({ listenFd, POLLIN, 0 });
        for (auto& [fd, c] : conns)
            fds.push_back({ fd, static_cast<short>(POLLIN | (c.out.empty() ? 0 : POLLOUT)), 0 });
        const int ready = orc::sock::pollAll(fds, 50);
        const auto t = clock::now();
        if (ready > 0) {
            for (const pollfd& p : fds) {
                if (!p.revents) continue;
                if (p.fd == listenFd) {
                    for (;;) {
                        sockaddr_in peer{};
                        socklen_t len = sizeof(peer);
                        const sock_t fd = ::accept(listenFd, reinterpret_cast<sockaddr*>(&peer), &len);
                        if (fd == kNone) break;
                        if (conns.size() >= kMaxConns) { orc::sock::closeSock(fd); continue; }
                        orc::sock::setNonBlocking(fd);
                        orc::sock::noSigPipe(fd);
                        char ip[INET_ADDRSTRLEN] = {};
                        ::inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
                        Conn c;
                        c.fd = fd;
                        c.peer = ip;
                        c.lastRx = t;
                        conns[fd] = std::move(c);
                    }
                    continue;
                }
                const auto it = conns.find(p.fd);
                if (it == conns.end()) continue;
                Conn& c = it->second;
                if (p.revents & POLLIN) {
                    char buf[16384];
                    for (;;) {
                        const long n = orc::sock::recvSome(c.fd, buf, sizeof(buf));
                        if (n > 0) { c.in.append(buf, static_cast<std::size_t>(n)); c.lastRx = t; continue; }
                        if (n == 0 || !orc::sock::wouldBlock())
                            c.dead = true;
                        break;
                    }
                    if (!c.ws && !c.closing) onRequest(c);
                    if (c.ws && !c.dead && !c.closing) onFrames(c);
                } else if (p.revents & (POLLHUP | POLLERR | POLLNVAL)) {
                    c.dead = true;
                }
                if ((p.revents & POLLOUT) && !c.dead) flush(c);
            }
        }

        // What the platform answered (the login switch, the logo dialog).
        {
            std::vector<std::function<void(Context&)>> jobs;
            {
                std::lock_guard<std::mutex> lk(mx_);
                jobs.swap(queue_);
            }
            for (auto& j : jobs) { j(ctx_); force = true; }
        }

        // The state once a second, compared as refresh did it; at once after an edit.
        if (force || t - lastState >= std::chrono::seconds(1)) {
            lastState = t;
            std::string state;
            for (auto& [fd, c] : conns) {
                if (!c.ws || c.dead || c.closing) continue;
                if (state.empty()) state = stateJson(ctx_);
                if (!force && c.sent == state) continue;
                c.sent = state;
                queue(c, ws::encodeFrame(ws::kText, state));
            }
            force = false;
        }

        // HTTP that never finishes its request.
        for (auto& [fd, c] : conns)
            if (!c.ws && t - c.lastRx > std::chrono::seconds(10)) c.dead = true;
        for (auto& [fd, c] : conns)
            if (!c.dead && !c.out.empty()) flush(c);
        for (auto it = conns.begin(); it != conns.end();) {
            if (!it->second.dead) { ++it; continue; }
            orc::sock::closeSock(it->first);
            it = conns.erase(it);
        }
    }
    for (auto& [fd, c] : conns) orc::sock::closeSock(fd);
    orc::sock::closeSock(listenFd);
}

} // namespace orc::settings
