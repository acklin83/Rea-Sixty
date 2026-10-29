#include "PhonesServer.h"

#include "JsonTree.h"
#include "OrcConfig.h"
#include "OrcHandover.h"
#include "OrcLog.h"
#include "Phones.h"
#include "WebSocket.h"

#include <CoreFoundation/CoreFoundation.h>
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <ifaddrs.h>
#include <mach-o/dyld.h>
#include <map>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace orc::phones {

namespace rme = reasixty::rme;

// ── the files ────────────────────────────────────────────────────────────────

std::string settingsPath() { return orc::supportDir() + "/moreme.json"; }
static std::string pinPath() { return orc::supportDir() + "/moreme.pin"; }

namespace {

std::string jsonStr(const std::string& s)
{
    std::string o = "\"";
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
        else o.push_back(ch);
    }
    return o + "\"";
}

std::string lowerExt(const std::string& path)
{
    const std::size_t dot = path.find_last_of('.');
    const std::size_t slash = path.find_last_of('/');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
    std::string e = path.substr(dot + 1);
    for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

const char* imageType(const std::string& ext)
{
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "svg") return "image/svg+xml";
    return nullptr;
}

bool validPort(int p) { return p > 0 && p <= 65535; }

} // namespace

bool loadSettings(Settings& out)
{
    const std::string json = orc_handover::readFile(settingsPath());
    if (json.empty()) return false;
    wdl_json_parser p;
    const wdl_json_element* root = p.parse(json.c_str(), static_cast<int>(json.size()));
    JsonTreeGuard rootGuard{p, root};
    if (!root || !root->is_object()) return false;
    Settings s;
    auto num = [&](const char* k, int& dst) {
        if (const char* v = root->get_string_by_name(k, true)) {
            const int n = std::atoi(v);
            if (validPort(n)) dst = n;
        }
    };
    if (const char* v = root->get_string_by_name("enabled", true)) s.enabled = std::strcmp(v, "true") == 0;
    num("webPort", s.webPort);
    num("sendPort", s.sendPort);
    num("recvPort", s.recvPort);
    if (const char* v = root->get_string_by_name("studio")) s.studio = v;
    if (const char* v = root->get_string_by_name("logo")) {
        // A file name in ORC's folder and nothing else: no path from a file
        // that somebody else might have written.
        const std::string l = v;
        if (l.find('/') == std::string::npos && l != "." && l != "..") s.logo = l;
    }
    out = s;
    return true;
}

bool saveSettings(const Settings& s)
{
    const std::string json = std::string("{\n  \"version\": 1,\n")
        + "  \"enabled\": " + (s.enabled ? "true" : "false") + ",\n"
        + "  \"webPort\": " + std::to_string(s.webPort) + ",\n"
        + "  \"sendPort\": " + std::to_string(s.sendPort) + ",\n"
        + "  \"recvPort\": " + std::to_string(s.recvPort) + ",\n"
        + "  \"studio\": " + jsonStr(s.studio) + ",\n"
        + "  \"logo\": " + jsonStr(s.logo) + "\n}\n";
    return orc_handover::writeFile(settingsPath(), json);
}

std::string loadPin()
{
    std::string p = orc_handover::readFile(pinPath());
    while (!p.empty() && (p.back() == '\n' || p.back() == '\r' || p.back() == ' ')) p.pop_back();
    return p;
}

bool savePin(const std::string& pin)
{
    const std::string path = pinPath();
    // Readable by the user only, before anything is in it.
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return false;
    ::fchmod(fd, 0600);
    const std::string text = pin + "\n";
    const bool ok = ::write(fd, text.data(), text.size()) == static_cast<ssize_t>(text.size());
    ::close(fd);
    return ok;
}

std::string generatePin()
{
    char b[8];
    std::snprintf(b, sizeof(b), "%04u", arc4random_uniform(10000));
    return b;
}

std::string importLogo(const std::string& sourcePath, std::string& err)
{
    const std::string ext = lowerExt(sourcePath);
    if (!imageType(ext)) { err = "PNG, JPG or SVG"; return {}; }
    struct stat sb{};
    if (::stat(sourcePath.c_str(), &sb) != 0 || !S_ISREG(sb.st_mode)) { err = "Cannot read the file"; return {}; }
    if (sb.st_size > 5 * 1024 * 1024) { err = "Larger than 5 MB"; return {}; }
    std::string data;
    if (FILE* f = std::fopen(sourcePath.c_str(), "rb")) {
        char buf[16384];
        std::size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) data.append(buf, n);
        std::fclose(f);
    }
    if (data.empty()) { err = "Cannot read the file"; return {}; }
    const std::string name = "moreme-logo." + (ext == "jpeg" ? std::string("jpg") : ext);
    for (const char* old : { "moreme-logo.png", "moreme-logo.jpg", "moreme-logo.svg" })
        if (name != old) std::remove((orc::supportDir() + "/" + old).c_str());
    if (!orc_handover::writeFile(orc::supportDir() + "/" + name, data)) {
        err = "Cannot write to " + orc::supportDir();
        return {};
    }
    err.clear();
    return name;
}

// ── what the page is made of ─────────────────────────────────────────────────

namespace {

// ORC.app/Contents/Resources/moreme, where the build puts the page and the mark.
std::string resourceDir()
{
    char exe[PATH_MAX];
    std::uint32_t size = sizeof(exe);
    if (_NSGetExecutablePath(exe, &size) != 0) return {};
    char real[PATH_MAX];
    if (!::realpath(exe, real)) return {};
    std::string p = real;                          // .../ORC.app/Contents/MacOS/ORC
    p = p.substr(0, p.find_last_of('/'));          // .../Contents/MacOS
    p = p.substr(0, p.find_last_of('/'));          // .../Contents
    return p + "/Resources/moreme";
}

std::string orcVersion()
{
    CFBundleRef b = CFBundleGetMainBundle();
    if (!b) return "?";
    const void* v = CFBundleGetValueForInfoDictionaryKey(b, CFSTR("CFBundleShortVersionString"));
    if (!v || CFGetTypeID(v) != CFStringGetTypeID()) return "?";
    char buf[64];
    if (!CFStringGetCString(static_cast<CFStringRef>(v), buf, sizeof(buf), kCFStringEncodingUTF8)) return "?";
    return buf;
}

std::string htmlAttr(const std::string& s)
{
    std::string o;
    for (const char c : s) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            case '\'': o += "&#39;"; break;
            default: o.push_back(c);
        }
    }
    return o;
}

void replaceOnce(std::string& s, const std::string& what, const std::string& with)
{
    const std::size_t at = s.find(what);
    if (at != std::string::npos) s.replace(at, what.size(), with);
}

std::string serve(const ws::Request& r, const Settings& s, const std::string& res)
{
    if (r.method != "GET" && r.method != "HEAD")
        return ws::response(405, "text/plain; charset=utf-8", "method not allowed");
    if (r.path == "/" || r.path == "/index.html" || r.path == "/moreme.html") {
        std::string page = orc_handover::readFile(res + "/moreme.html");
        if (page.empty()) return ws::response(404, "text/plain; charset=utf-8", "not found");
        // The studio's name and ORC's version go into two meta tags the page
        // reads at start.
        replaceOnce(page, "<meta name=\"moreme-studio\" content=\"\">",
                    "<meta name=\"moreme-studio\" content=\"" + htmlAttr(s.studio) + "\">");
        replaceOnce(page, "<meta name=\"moreme-version\" content=\"\">",
                    "<meta name=\"moreme-version\" content=\"" + htmlAttr(orcVersion()) + "\">");
        return ws::response(200, "text/html; charset=utf-8", page);
    }
    if (r.path == "/logo") {
        if (!s.logo.empty()) {
            const std::string data = orc_handover::readFile(orc::supportDir() + "/" + s.logo);
            if (const char* t = imageType(lowerExt(s.logo)); t && !data.empty())
                return ws::response(200, t, data);
        }
        const std::string mark = orc_handover::readFile(res + "/moreme-logo.svg");
        if (!mark.empty()) return ws::response(200, "image/svg+xml", mark);
    }
    return ws::response(404, "text/plain; charset=utf-8", "not found");
}

std::string peerAddress(const sockaddr_in& a)
{
    char b[INET_ADDRSTRLEN] = {};
    ::inet_ntop(AF_INET, &a.sin_addr, b, sizeof(b));
    return b;
}

constexpr std::size_t kMaxConns   = 48;          // phones, plus their reconnects
constexpr std::size_t kMaxHead    = 16 * 1024;
constexpr std::size_t kMaxPending = 1024 * 1024; // a peer that stopped reading

struct Conn {
    int            fd = -1;
    std::string    peer;
    std::string    in, out;
    bool           ws      = false;
    bool           closing = false;   // close once `out` is sent
    bool           dead    = false;
    int            mixId   = 0;
    ws::Reader     reader;
    std::chrono::steady_clock::time_point lastRx;
};

} // namespace

// ── the server ───────────────────────────────────────────────────────────────

Server& server()
{
    static Server s;
    return s;
}

Server::~Server() { stop(); }

void Server::apply(const Settings& s, const std::string& pin)
{
    {
        std::lock_guard<std::mutex> lk(mx_);
        want_ = s;
        pin_  = pin;
        ++gen_;
    }
    if (!s.enabled) { stop(); return; }
    if (!run_.exchange(true)) {
        link_.setRounds(3000);   // stoerme asks every 3 s
        link_.start();
        worker_ = std::thread([this] { loop(); });
    }
}

void Server::stop()
{
    if (!run_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();
    link_.stop();
    std::lock_guard<std::mutex> lk(mx_);
    status_ = Status{};
}

Settings Server::settings() const
{
    std::lock_guard<std::mutex> lk(mx_);
    return want_;
}

std::string Server::pin() const
{
    std::lock_guard<std::mutex> lk(mx_);
    return pin_;
}

Status Server::status() const
{
    std::lock_guard<std::mutex> lk(mx_);
    Status st = status_;
    st.link = link_.link();
    return st;
}

void Server::loop()
{
    using clock = std::chrono::steady_clock;
    auto ms = [](clock::time_point t) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(t.time_since_epoch()).count();
    };

    const std::string res = resourceDir();
    Mixer mixer;
    mixer.setVersion(orcVersion());
    mixer.setLog([](const std::string& line) { orc::logLine("%s\n", line.c_str()); });

    int           listenFd   = -1;
    int           listenPort = 0;
    std::string   listenErr;
    auto          retryAt    = clock::now();
    std::uint32_t gen        = ~0u;
    Settings      cur;
    std::string   curPin;
    bool          firstPin   = true;
    std::string   tmHost;
    auto          lastHost   = clock::now() - std::chrono::hours(1);
    auto          lastSync   = clock::now() - std::chrono::hours(1);
    auto          lastPing   = clock::now();
    std::map<int, Conn> conns;          // by fd
    std::map<int, int>  fdOfMix;        // mixer id -> fd
    bool                tmSeen = false;

    auto closeListen = [&] {
        if (listenFd >= 0) { ::close(listenFd); listenFd = -1; }
    };
    auto openListen = [&](int port) {
        closeListen();
        listenPort = port;
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) { listenErr = "could not open a socket"; return; }
        // ⇨ macOS's SO_REUSEADDR: past a TIME_WAIT left by the last run, never
        // next to a live listener (that still fails with EADDRINUSE). Windows
        // reads the flag the other way round (memory windows-so-reuseaddr-
        // hijacks-port); ORC is macOS only.
        const int one = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in me{};
        me.sin_family      = AF_INET;
        me.sin_port        = htons(static_cast<std::uint16_t>(port));
        me.sin_addr.s_addr = htonl(INADDR_ANY);   // the phones are on the network
        if (::bind(fd, reinterpret_cast<sockaddr*>(&me), sizeof(me)) != 0 || ::listen(fd, 16) != 0) {
            ::close(fd);
            char t[96];
            std::snprintf(t, sizeof(t), "port %d is in use by another program", port);
            listenErr = t;
            return;
        }
        ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
        listenFd = fd;
        listenErr.clear();
        orc::logLine("MoreMe: page on port %d\n", port);
    };
    auto linkConfig = [&] {
        rme::Config c;
        c.enabled  = true;
        c.host     = tmHost.empty() ? std::string("127.0.0.1") : tmHost;
        c.sendPort = cur.sendPort;
        c.recvPort = cur.recvPort;
        link_.setConfig(c);
        link_.takeConfigDirty();   // nothing to write: this remote has no rme.json
    };
    auto queue = [&](Conn& c, const std::string& bytes) {
        c.out += bytes;
        if (c.out.size() > kMaxPending) c.dead = true;
    };
    auto flush = [&](Conn& c) {
        while (!c.out.empty()) {
            const ssize_t n = ::send(c.fd, c.out.data(), c.out.size(), 0);
            if (n > 0) { c.out.erase(0, static_cast<std::size_t>(n)); continue; }
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return;
            c.dead = true;   // the peer is gone
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
        if (!ws::parseRequest(head, r)) {
            queue(c, ws::response(400, "text/plain; charset=utf-8", "bad request"));
            c.closing = true;
            return;
        }
        if (r.path == "/ws" && ws::isUpgrade(r)) {
            queue(c, ws::upgradeResponse(r));
            c.ws    = true;
            c.mixId = mixer.open(clientKey(c.peer, r.header("x-real-ip")));
            fdOfMix[c.mixId] = c.fd;
            return;
        }
        std::string resp = serve(r, cur, res);
        if (r.method == "HEAD") resp = resp.substr(0, resp.find("\r\n\r\n") + 4);
        queue(c, resp);
        c.closing = true;
    };
    auto onFrames = [&](Conn& c, long long nowMs) {
        for (;;) {
            ws::Frame f;
            const ws::Take t = ws::takeFrame(c.in, f);
            if (t == ws::Take::Need) return;
            if (t == ws::Take::Bad) { c.dead = true; return; }
            std::string text;
            switch (c.reader.feed(f, text)) {
                case ws::Reader::Got::Nothing: break;
                case ws::Reader::Got::Message: mixer.message(c.mixId, text, nowMs); break;
                case ws::Reader::Got::Ping:    queue(c, ws::encodeFrame(ws::kPong, text)); break;
                case ws::Reader::Got::Close:
                    queue(c, ws::encodeFrame(ws::kClose, {}));
                    c.closing = true;
                    return;
                case ws::Reader::Got::Bad: c.dead = true; return;
            }
        }
    };

    while (run_.load()) {
        const auto now = clock::now();

        // Settings and PIN, when the page changed them.
        {
            std::lock_guard<std::mutex> lk(mx_);
            if (gen_ != gen) {
                gen = gen_;
                const bool ports = cur.sendPort != want_.sendPort || cur.recvPort != want_.recvPort;
                const bool web   = cur.webPort != want_.webPort || listenFd < 0;
                const bool pin   = curPin != pin_;
                cur = want_;
                if (pin) {
                    curPin = pin_;
                    // The first one is the PIN the mixer starts with; a change
                    // sends everybody inside back to the PIN page.
                    mixer.setPin(curPin);
                    if (!firstPin) orc::logLine("MoreMe: new PIN, phones asked again\n");
                    firstPin = false;
                }
                if (web) retryAt = now;
                if (ports) lastHost = now - std::chrono::hours(1);
                if (web && listenFd >= 0 && listenPort != cur.webPort) closeListen();
            }
        }
        // The mixer is the one the surfaces talk to: same host, other ports.
        // Followed once a second, so a host changed on the General page moves
        // MoreMe too.
        if (now - lastHost >= std::chrono::seconds(1)) {
            lastHost = now;
            const std::string h = rme::manager().config().host;
            const rme::Config lc = link_.config();
            if (h != tmHost || lc.sendPort != cur.sendPort || lc.recvPort != cur.recvPort || !lc.enabled) {
                tmHost = h;
                linkConfig();
            }
        }
        // ⛔ No PIN, no page (Frank 29.10.2026: a PIN as soon as it is on).
        // The settings page never leaves it empty; this is the second lock.
        if (curPin.empty()) {
            if (listenFd >= 0) closeListen();
            listenErr = "no PIN set";
        } else if (listenFd < 0 && now >= retryAt) {
            openListen(cur.webPort);
            if (listenFd < 0) retryAt = now + std::chrono::seconds(3);
        }

        std::vector<pollfd> fds;
        fds.reserve(conns.size() + 1);
        if (listenFd >= 0) fds.push_back({ listenFd, POLLIN, 0 });
        for (auto& [fd, c] : conns)
            fds.push_back({ fd, static_cast<short>(POLLIN | (c.out.empty() ? 0 : POLLOUT)), 0 });
        const int ready = fds.empty() ? (std::this_thread::sleep_for(std::chrono::milliseconds(50)), 0)
                                      : ::poll(fds.data(), static_cast<nfds_t>(fds.size()), 50);
        const auto t = clock::now();
        if (ready > 0) {
            for (const pollfd& p : fds) {
                if (!p.revents) continue;
                if (p.fd == listenFd) {
                    for (;;) {
                        sockaddr_in peer{};
                        socklen_t len = sizeof(peer);
                        const int fd = ::accept(listenFd, reinterpret_cast<sockaddr*>(&peer), &len);
                        if (fd < 0) break;
                        if (conns.size() >= kMaxConns) { ::close(fd); continue; }
                        ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
                        // ⛔ A write to a phone that has gone would raise SIGPIPE and
                        // end ORC; this turns it into an error we read.
                        const int one = 1;
                        ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
                        Conn c;
                        c.fd     = fd;
                        c.peer   = peerAddress(peer);
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
                        const ssize_t n = ::recv(c.fd, buf, sizeof(buf), 0);
                        if (n > 0) { c.in.append(buf, static_cast<std::size_t>(n)); c.lastRx = t; continue; }
                        // ⛔ 0 AND a real error both end the connection: once, the
                        // impersonator cleaned up only on 0 and leaked a socket per
                        // reset until the process died (memory sslcore-fd-setsize).
                        if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
                            c.dead = true;
                        break;
                    }
                    if (!c.ws) onRequest(c);
                    if (c.ws && !c.dead && !c.closing) onFrames(c, ms(t));
                } else if (p.revents & (POLLHUP | POLLERR | POLLNVAL)) {
                    c.dead = true;
                }
                if ((p.revents & POLLOUT) && !c.dead) flush(c);
            }
        }

        // TotalMix to the phones, 20 times a second at most.
        if (t - lastSync >= std::chrono::milliseconds(50)) {
            lastSync = t;
            const rme::State st = link_.snapshot();
            tmSeen = st.ingested > 0;
            mixer.sync(st, link_.visible(), tmSeen);
        }
        for (auto& [id, json] : mixer.takeOut()) {
            const auto f = fdOfMix.find(id);
            if (f == fdOfMix.end()) continue;
            const auto c = conns.find(f->second);
            if (c != conns.end() && !c->second.dead) queue(c->second, ws::encodeFrame(ws::kText, json));
        }
        for (const Osc& o : mixer.takeOsc()) link_.send(o.address, o.value);
        if (mixer.takeRefresh()) link_.refresh();

        // ⇨ A PHONE THAT WENT TO SLEEP never says goodbye, and its headphone
        // would stay taken for whoever picks the phone up again. A ping every
        // 15 s (browsers answer on their own); nothing back in 45 s ends it.
        if (t - lastPing >= std::chrono::seconds(15)) {
            lastPing = t;
            for (auto& [fd, c] : conns) {
                if (!c.ws || c.dead) continue;
                if (t - c.lastRx > std::chrono::seconds(45)) { c.dead = true; continue; }
                queue(c, ws::encodeFrame(ws::kPing, {}));
            }
        }
        // HTTP that never finishes its request.
        for (auto& [fd, c] : conns)
            if (!c.ws && t - c.lastRx > std::chrono::seconds(10)) c.dead = true;

        for (auto& [fd, c] : conns)
            if (!c.dead && !c.out.empty()) flush(c);
        for (auto it = conns.begin(); it != conns.end();) {
            if (!it->second.dead) { ++it; continue; }
            if (it->second.mixId) {
                mixer.close(it->second.mixId);
                fdOfMix.erase(it->second.mixId);
            }
            ::close(it->first);
            it = conns.erase(it);
        }

        {
            std::lock_guard<std::mutex> lk(mx_);
            status_.enabled   = true;
            status_.listening = listenFd >= 0;
            status_.port      = cur.webPort;
            status_.error     = listenErr;
            status_.tmSeen    = tmSeen;
            status_.phones    = mixer.clients();
            status_.held      = mixer.held();
        }
    }

    for (auto& [fd, c] : conns) ::close(fd);
    closeListen();
}

// ── for the menu ─────────────────────────────────────────────────────────────

std::string summary(const Status& st)
{
    if (!st.enabled) return "off";
    if (!st.error.empty()) return st.error;
    if (st.link == rme::LinkState::PortBusy) return "TotalMix port in use by another program";
    if (!st.tmSeen) return "TotalMix does not answer";
    if (st.phones == 0) return "no phones connected";
    if (st.phones == 1) return "1 phone connected";
    return std::to_string(st.phones) + " phones connected";
}

std::string pageAddress(int port)
{
    ifaddrs* list = nullptr;
    if (::getifaddrs(&list) != 0) return {};
    std::string best, any;
    for (ifaddrs* i = list; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
        if (!(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK)) continue;
        const auto* a = reinterpret_cast<const sockaddr_in*>(i->ifa_addr);
        const std::string ip = peerAddress(*a);
        if (ip.rfind("169.254.", 0) == 0) continue;   // no DHCP answer: unreachable
        // The wired and the wireless port first: the phones are on that
        // network, not on a VPN tunnel.
        if (std::strncmp(i->ifa_name, "en", 2) == 0) { if (best.empty()) best = ip; }
        else if (any.empty()) any = ip;
    }
    ::freeifaddrs(list);
    const std::string ip = best.empty() ? any : best;
    if (ip.empty()) return {};
    return "http://" + ip + (port == 80 ? std::string() : ":" + std::to_string(port));
}

} // namespace orc::phones
