#include "SslCoreImpersonator.h"

// Socket plumbing mirrors StreamDeckBridge.cpp — socket headers FIRST so Winsock2
// wins over the legacy <winsock.h> that WDL pulls in transitively.
#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  using socket_t = SOCKET;
  static constexpr socket_t kInvalid = INVALID_SOCKET;
  #define SC_CLOSE closesocket
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netinet/tcp.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <errno.h>
  using socket_t = int;
  static constexpr socket_t kInvalid = -1;
  #define SC_CLOSE ::close
#endif

#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <cstring>
#include <array>
#include <vector>

namespace sslcore {
namespace {

// ---------------------------------------------------------------- shared state
std::atomic<bool>      g_running{false};
std::thread            g_worker;
std::atomic<bool>      g_connected{false};
std::atomic<long long> g_lastDataMs{0};

struct Slot { std::vector<float> current, peak; bool have = false; };
std::mutex                                     g_meterMx;
std::array<Slot, int(sslmeter::DataType::Count)> g_meter;

long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void netInit()    { }
void netCleanup() { }

// ---------------------------------------------------------- protocol constants
constexpr uint32_t kScopeId = 0x5f7a0579;
constexpr uint32_t kMsgId   = 284;
const char*        kScope   = "PluginControls.PerSslMeterProPlugin";

void putU32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8));
    v.push_back(uint8_t(x >> 16)); v.push_back(uint8_t(x >> 24));
}
void putVarint(std::vector<uint8_t>& v, uint64_t x) {
    while (x >= 0x80) { v.push_back(uint8_t(x) | 0x80); x >>= 7; }
    v.push_back(uint8_t(x));
}
std::vector<uint8_t> ctrlFrame(uint32_t type, const std::vector<uint8_t>& pb, uint32_t seq = 0) {
    std::vector<uint8_t> body;
    putU32(body, 16); putU32(body, 1); putU32(body, seq);
    putU32(body, uint32_t(12 + pb.size()));
    putU32(body, type); putU32(body, kScopeId); putU32(body, kMsgId);
    body.insert(body.end(), pb.begin(), pb.end());
    std::vector<uint8_t> f;
    f.insert(f.end(), sslmeter::kMagic, sslmeter::kMagic + 4);
    putU32(f, uint32_t(body.size()));
    f.insert(f.end(), body.begin(), body.end());
    return f;
}
std::vector<uint8_t> serverConfig(int port) {
    std::vector<uint8_t> pb;
    if (port >= 0) { putVarint(pb, (1 << 3) | 0); putVarint(pb, uint64_t(port)); }
    putVarint(pb, (2 << 3) | 2); putVarint(pb, std::strlen(kScope));
    pb.insert(pb.end(), kScope, kScope + std::strlen(kScope));
    putVarint(pb, (3 << 3) | 0); putVarint(pb, 1);
    return ctrlFrame(19, pb);
}
std::vector<uint8_t> openingSequence(int dataPort) {
    std::vector<uint8_t> out;
    auto add = [&](const std::vector<uint8_t>& f){ out.insert(out.end(), f.begin(), f.end()); };
    add(serverConfig(-1));
    add(ctrlFrame(6, {}));
    { std::vector<uint8_t> pb; putVarint(pb, (1<<3)|0); putVarint(pb, 0x9abc); add(ctrlFrame(7, pb)); }
    add(serverConfig(dataPort));
    return out;
}
std::vector<uint8_t> heartbeat() {
    std::vector<uint8_t> pb;                       // f1 = "server heartbeat"
    putVarint(pb, (1<<3)|2); putVarint(pb, 16);
    const char* s = "server heartbeat";
    pb.insert(pb.end(), s, s + 16);
    return ctrlFrame(10, pb);
}
std::vector<uint8_t> announcement(uint16_t tcpPort) {
    std::vector<uint8_t> pb;                       // LgxPropertyConnectionAnnouncementData
    putVarint(pb, (1<<3)|0); putVarint(pb, 2);                       // AppVerMajor
    const char* ip = "127.0.0.1";
    putVarint(pb, (3<<3)|2); putVarint(pb, std::strlen(ip)); pb.insert(pb.end(), ip, ip+std::strlen(ip));
    putVarint(pb, (4<<3)|0); putVarint(pb, tcpPort);                 // Port
    const char* mn = "Rea-Sixty";
    putVarint(pb, (5<<3)|2); putVarint(pb, std::strlen(mn)); pb.insert(pb.end(), mn, mn+std::strlen(mn));
    // announce header uses type-field 12 (vs 3/19 for data/config)
    std::vector<uint8_t> body(28, 0);
    auto pU = [](std::vector<uint8_t>& v, size_t o, uint32_t x){ std::memcpy(v.data()+o, &x, 4); };
    pU(body,0,16); pU(body,4,1); pU(body,12,uint32_t(pb.size()+8)); pU(body,16,12);
    body.insert(body.end(), pb.begin(), pb.end());
    std::vector<uint8_t> f;
    f.insert(f.end(), sslmeter::kMagic, sslmeter::kMagic + 4);
    putU32(f, uint32_t(body.size()));
    f.insert(f.end(), body.begin(), body.end());
    return f;
}

bool setNonBlocking(socket_t s) {
#if defined(_WIN32)
    u_long m = 1; return ioctlsocket(s, FIONBIO, &m) == 0;
#else
    int fl = fcntl(s, F_GETFL, 0); return fl >= 0 && fcntl(s, F_SETFL, fl | O_NONBLOCK) == 0;
#endif
}

socket_t makeUdp(uint16_t port, bool reuse) {
    socket_t s = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s == kInvalid) return kInvalid;
    if (reuse) { int y = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&y), sizeof(y)); }
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (port && ::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) { SC_CLOSE(s); return kInvalid; }
    setNonBlocking(s);
    return s;
}

// -------------------------------------------------------------------- worker
void workerMain(uint16_t tcpPort, uint16_t dataPort) {
    netInit();

    socket_t listenFd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenFd == kInvalid) { g_running = false; return; }
    int yes = 1; setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&yes), sizeof(yes));
    sockaddr_in la{}; la.sin_family = AF_INET; la.sin_port = htons(tcpPort); la.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(listenFd, reinterpret_cast<sockaddr*>(&la), sizeof(la)) != 0 || ::listen(listenFd, 8) != 0) {
        SC_CLOSE(listenFd); g_running = false; return;
    }
    // Read back the actual TCP port (if tcpPort was 0) to announce it.
    sockaddr_in bound{}; socklen_t bl = sizeof(bound);
    getsockname(listenFd, reinterpret_cast<sockaddr*>(&bound), &bl);
    const uint16_t actualTcp = ntohs(bound.sin_port);
    setNonBlocking(listenFd);

    socket_t dataFd = makeUdp(dataPort, true);
    if (dataFd == kInvalid) { SC_CLOSE(listenFd); g_running = false; return; }
    socket_t annFd = makeUdp(0, false);

    std::vector<socket_t> clients;
    const std::vector<uint8_t> hb  = heartbeat();
    const std::vector<uint8_t> ann = announcement(actualTcp);
    double lastAnn = 0, lastHb = 0;
    auto secs = []{ return nowMs() / 1000.0; };
    uint8_t buf[65536];

    while (g_running.load()) {
        const double t = secs();
        if (annFd != kInvalid && t - lastAnn > 1.0) {
            lastAnn = t;
            for (uint16_t dp : {16008, 16009}) {
                sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(dp); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                ::sendto(annFd, reinterpret_cast<const char*>(ann.data()), int(ann.size()), 0,
                         reinterpret_cast<sockaddr*>(&a), sizeof(a));
            }
        }
        if (!clients.empty() && t - lastHb > 0.25) {
            lastHb = t;
            for (socket_t c : clients) ::send(c, reinterpret_cast<const char*>(hb.data()), int(hb.size()), 0);
        }

        fd_set rset; FD_ZERO(&rset);
        FD_SET(listenFd, &rset); FD_SET(dataFd, &rset);
        socket_t maxfd = listenFd > dataFd ? listenFd : dataFd;
        for (socket_t c : clients) { FD_SET(c, &rset); if (c > maxfd) maxfd = c; }
        timeval tv{0, 40 * 1000};
        if (::select(int(maxfd) + 1, &rset, nullptr, nullptr, &tv) <= 0) continue;

        if (FD_ISSET(listenFd, &rset)) {
            socket_t c = ::accept(listenFd, nullptr, nullptr);
            if (c != kInvalid) {
                setNonBlocking(c);
                int one = 1; setsockopt(c, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&one), sizeof(one));
                auto seq = openingSequence(dataPort);
                ::send(c, reinterpret_cast<const char*>(seq.data()), int(seq.size()), 0);
                clients.push_back(c);
                g_connected.store(true);
            }
        }
        if (FD_ISSET(dataFd, &rset)) {
            int n = int(::recvfrom(dataFd, reinterpret_cast<char*>(buf), sizeof(buf), 0, nullptr, nullptr));
            if (n > 0) {
                std::vector<sslmeter::Update> ups;
                if (sslmeter::parseDatagram(buf, size_t(n), ups) > 0) {
                    g_lastDataMs.store(nowMs());
                    std::lock_guard<std::mutex> lk(g_meterMx);
                    for (auto& u : ups) {
                        if (u.dataType < 0 || u.dataType >= int(sslmeter::DataType::Count)) continue;
                        Slot& s = g_meter[u.dataType];
                        s.current = std::move(u.current); s.peak = std::move(u.peak); s.have = true;
                    }
                }
            }
        }
        for (size_t i = 0; i < clients.size();) {
            socket_t c = clients[i];
            if (FD_ISSET(c, &rset)) {
                int n = int(::recv(c, reinterpret_cast<char*>(buf), sizeof(buf), 0));
                if (n == 0) { SC_CLOSE(c); clients.erase(clients.begin() + long(i)); continue; }
                // else: drain plugin control frames; we don't need their content yet.
            }
            ++i;
        }
        g_connected.store(!clients.empty());
    }

    for (socket_t c : clients) SC_CLOSE(c);
    if (annFd != kInvalid) SC_CLOSE(annFd);
    SC_CLOSE(dataFd);
    SC_CLOSE(listenFd);
    g_connected.store(false);
    netCleanup();
}

} // namespace

// ------------------------------------------------------------------- public
bool start(uint16_t tcpPort, uint16_t dataPort) {
    if (g_running.load()) return true;
    { std::lock_guard<std::mutex> lk(g_meterMx); for (auto& s : g_meter) s = Slot{}; }
    g_lastDataMs.store(0);
    g_running.store(true);
    try { g_worker = std::thread(workerMain, tcpPort, dataPort); }
    catch (...) { g_running.store(false); return false; }
    return true;
}

void stop() {
    if (!g_running.exchange(false)) { if (g_worker.joinable()) g_worker.join(); return; }
    if (g_worker.joinable()) g_worker.join();
}

bool isRunning()      { return g_running.load(); }
bool pluginConnected(){ return g_connected.load(); }

bool getMeter(int dataType, std::vector<float>& current, std::vector<float>& peak) {
    if (dataType < 0 || dataType >= int(sslmeter::DataType::Count)) return false;
    std::lock_guard<std::mutex> lk(g_meterMx);
    const Slot& s = g_meter[dataType];
    if (!s.have) return false;
    current = s.current; peak = s.peak;
    return true;
}

long long msSinceLastData() {
    const long long last = g_lastDataMs.load();
    if (last == 0) return INT64_MAX;
    return nowMs() - last;
}

} // namespace sslcore
