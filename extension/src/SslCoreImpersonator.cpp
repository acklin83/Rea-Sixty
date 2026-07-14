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
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <array>
#include <vector>
#include <string>
#include <map>
#include <utility>

namespace sslcore {
namespace {

// Diagnostic log — the impersonator is otherwise blind. Enabled by the env var
// REASIXTY_SSLCORE_TRACE; writes to /tmp/reaper_sslcore.log. Logs the lifecycle
// (bind, announce, plugin connect/disconnect) and a periodic summary of which
// meter DataTypes are arriving with sample values, so we can see end-to-end that
// the plugin is streaming to us.
bool  g_trace = false;
void  slog(const char* fmt, ...) {
    if (!g_trace) return;
    FILE* f = std::fopen("/tmp/reaper_sslcore.log", "a");
    if (!f) return;
    va_list ap; va_start(ap, fmt);
    std::vfprintf(f, fmt, ap);
    va_end(ap);
    std::fputc('\n', f);
    std::fclose(f);
}

// ---------------------------------------------------------------- shared state
std::atomic<bool>      g_running{false};
std::thread            g_worker;
std::atomic<bool>      g_connected{false};
std::atomic<long long> g_lastDataMs{0};

// `pluginType` is the message's PluginType (f1) — kept because DataType alone is
// AMBIGUOUS. Schema (AssignerArgsTypes): PluginMeterDataMessage.DataType is a
// plain int32, not a typed enum, and its vocabulary depends on PluginType:
// MeterPluginDataType for Meter/MeterPro, ChannelStripMeterType for a channel
// strip (where 4 = CompGain, 5 = GateGain), BusCompMeterType for a bus comp.
// So a channel strip's GateGain(5) and the Meter plug-in's TextRms(5) are the
// SAME index here and would silently overwrite each other.
struct Slot { std::vector<float> current, peak; bool have = false; int pluginType = -1; };
std::mutex                                     g_meterMx;
std::array<Slot, int(sslmeter::DataType::Count)> g_meter;

// Census of every (PluginType, DataType) pair actually seen on the wire, with a
// sample value. This is the diagnostic that tells us WHICH plug-in families
// stream and whether two of them collide in g_meter above — e.g. whether an SSL
// channel strip publishes ChannelStripMeterType_GateGain(5), which is the
// long-missing Gate-GR source. Guarded by g_meterMx (same lock as g_meter).
struct Census { long long n = 0; size_t nvals = 0; float first = 0.f; };
std::map<std::pair<int, int>, Census> g_census;

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
    // Replay Core's captured "server heartbeat" frame VERBATIM — this is the
    // exact byte string the standalone probe used when it got the plugin to
    // stream end-to-end (ssl_core_probe --serve, 2026-07-10). Rebuilding it via
    // ctrlFrame(10, …) with seq=0 was NOT enough: the plugin reconnected in a
    // loop and never sent meter UDP (observed in-extension 2026-07-14). The
    // offset-8 seq/time field (0x0e4baf54) is part of what the plugin accepts.
    static const char* hx =
        "efbc51002e000000"                                          // magic + len(46)
        "100000000100000054af4b0e1e0000000a00000079057a5f1c010000"  // 28-B header
        "0a1073657276657220686561727462656174";                     // pb f1 "server heartbeat"
    std::vector<uint8_t> v;
    for (const char* p = hx; p[0] && p[1]; p += 2) {
        auto nyb = [](char c){ return (c <= '9') ? c - '0' : (c | 0x20) - 'a' + 10; };
        v.push_back(uint8_t(nyb(p[0]) << 4 | nyb(p[1])));
    }
    return v;
}
// Hex -> bytes helper for verbatim frame replay.
std::vector<uint8_t> fromHex(const char* hx) {
    std::vector<uint8_t> v;
    for (const char* p = hx; p[0] && p[1]; p += 2) {
        auto nyb = [](char c){ return (c <= '9') ? c - '0' : (c | 0x20) - 'a' + 10; };
        v.push_back(uint8_t(nyb(p[0]) << 4 | nyb(p[1])));
    }
    return v;
}

// After the opening handshake, Core registers 3 objects (type-2) and subscribes
// to 3 meter-data streams (type-18), then RE-subscribes every ~6.6s. Without
// this the plugin disconnects after ~6s and reconnects in a loop, so no
// continuous meter stream (observed in-extension 2026-07-14; the missing
// "type-2/type-18 per-object subscribe frames" flagged in the 2026-07-10 notes).
// Extracted verbatim from the cold-connect capture (Core :52143 -> plugin).
std::vector<uint8_t> subscribeInitial() {
    std::vector<uint8_t> out;
    auto add = [&](const char* hx){ auto f = fromHex(hx); out.insert(out.end(), f.begin(), f.end()); };
    add("efbc51001c000000100000000100000028e0c7450c0000000200000038f0291a5ed5dbff");
    add("efbc51001c000000100000000100000028e0c7450c000000020000000196ce3d09ab3dfe");
    add("efbc51001e000000100000000100000028e0c7450e0000000200000038f0291a5ed5dbff0801");
    add("efbc51002d000000100000000100000028e0c7451d00000012000000636a1bfcb188080c0801120d0a0b08ffffffffffffffffff01");
    add("efbc51002e000000100000000100000028e0c7451e00000012000000b740ee1d4943a586088010120d0a0b08ffffffffffffffffff01");
    add("efbc510030000000100000000100000028e0c74520000000120000002ba7d9fe60d5ec5408f6cbb302120d0a0b08ffffffffffffffffff01");
    // 4th object (fc74d763) — seen once in another stream; likely a further meter
    // stream (RTA t8/t9 never arrived because only the first 3 were subscribed).
    // Cheap to include; if RTA data appears it was this.
    add("efbc51002f0000001000000001000000083fd2371f00000012000000fc74d76393cb200008c1d828120d0a0b08ffffffffffffffffff01");
    return out;
}
// The type-18 subscribe frames, replayed periodically to keep the streams alive.
// (The capture increments a counter each round; the plugin accepts the verbatim
// round-1 frames on repeat — good enough to hold the connection.)
std::vector<uint8_t> subscribeRefresh() {
    std::vector<uint8_t> out;
    auto add = [&](const char* hx){ auto f = fromHex(hx); out.insert(out.end(), f.begin(), f.end()); };
    add("efbc51002d000000100000000100000028e0c7451d00000012000000636a1bfcb188080c0801120d0a0b08ffffffffffffffffff01");
    add("efbc51002e000000100000000100000028e0c7451e00000012000000b740ee1d4943a586088010120d0a0b08ffffffffffffffffff01");
    add("efbc510030000000100000000100000028e0c74520000000120000002ba7d9fe60d5ec5408f6cbb302120d0a0b08ffffffffffffffffff01");
    add("efbc51002f0000001000000001000000083fd2371f00000012000000fc74d76393cb200008c1d828120d0a0b08ffffffffffffffffff01");
    return out;
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
    if (listenFd == kInvalid) { slog("[err] TCP socket() failed"); g_running = false; return; }
    int yes = 1; setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&yes), sizeof(yes));
    sockaddr_in la{}; la.sin_family = AF_INET; la.sin_port = htons(tcpPort); la.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(listenFd, reinterpret_cast<sockaddr*>(&la), sizeof(la)) != 0 || ::listen(listenFd, 8) != 0) {
        slog("[err] TCP bind/listen on port %u failed (in use? 360 running?)", unsigned(tcpPort));
        SC_CLOSE(listenFd); g_running = false; return;
    }
    // Read back the actual TCP port (if tcpPort was 0) to announce it.
    sockaddr_in bound{}; socklen_t bl = sizeof(bound);
    getsockname(listenFd, reinterpret_cast<sockaddr*>(&bound), &bl);
    const uint16_t actualTcp = ntohs(bound.sin_port);
    setNonBlocking(listenFd);

    // Bind a RANGE of UDP data ports, not just the one we assign. Multi-instance
    // Meter plugins each stream to a different Core port (cap87: 16010 =
    // master+loudness, 50881 = stereo), and a plugin may ignore our assigned
    // port and use its own. The standalone probe that worked bound 16010-16011 +
    // 50881; binding only the assigned 16010 meant we missed the stereo
    // instance's BarPeak/BarRms/RTA entirely (2026-07-14). Bind a spread.
    std::vector<socket_t> dataFds;
    std::vector<uint16_t> dataPorts;
    for (uint16_t dp : {uint16_t(dataPort), uint16_t(16011), uint16_t(16012),
                        uint16_t(16013), uint16_t(50881), uint16_t(50882)}) {
        socket_t fd = makeUdp(dp, true);
        if (fd != kInvalid) { dataFds.push_back(fd); dataPorts.push_back(dp); }
    }
    if (dataFds.empty()) {
        slog("[err] no UDP data port could bind (in use? 360 running?)");
        SC_CLOSE(listenFd); g_running = false; return;
    }
    { std::string ps; for (auto p : dataPorts) ps += " " + std::to_string(p);
      slog("[worker] up: TCP :%u  UDP data:%s  announcing on 16008/16009",
           unsigned(actualTcp), ps.c_str()); }
    socket_t annFd = makeUdp(0, false);

    std::vector<socket_t> clients;
    const std::vector<uint8_t> hb  = heartbeat();
    const std::vector<uint8_t> ann = announcement(actualTcp);
    double lastAnn = 0, lastHb = 0, lastSub = 0;
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
        // Re-subscribe every 5s (capture cadence ~6.6s) so the plugin keeps the
        // meter streams open instead of dropping the connection.
        if (!clients.empty() && t - lastSub > 5.0) {
            lastSub = t;
            const auto sub = subscribeRefresh();
            for (socket_t c : clients) ::send(c, reinterpret_cast<const char*>(sub.data()), int(sub.size()), 0);
        }

        fd_set rset; FD_ZERO(&rset);
        FD_SET(listenFd, &rset);
        socket_t maxfd = listenFd;
        for (socket_t d : dataFds) { FD_SET(d, &rset); if (d > maxfd) maxfd = d; }
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
                auto sub = subscribeInitial();
                ::send(c, reinterpret_cast<const char*>(sub.data()), int(sub.size()), 0);
                clients.push_back(c);
                g_connected.store(true);
                slog("[%.1f] plugin CONNECTED (fd=%d), sent opening (%zu B) + subscribe "
                     "(%zu B), data port %u", t, int(c), seq.size(), sub.size(), unsigned(dataPort));
            }
        }
        for (socket_t d : dataFds) {
          if (!FD_ISSET(d, &rset)) continue;
          int n = int(::recvfrom(d, reinterpret_cast<char*>(buf), sizeof(buf), 0, nullptr, nullptr));
          if (n > 0) {
                std::vector<sslmeter::Update> ups;
                if (sslmeter::parseDatagram(buf, size_t(n), ups) > 0) {
                    g_lastDataMs.store(nowMs());
                    std::lock_guard<std::mutex> lk(g_meterMx);
                    for (auto& u : ups) {
                        if (u.dataType < 0 || u.dataType >= int(sslmeter::DataType::Count)) continue;
                        // Census BEFORE the move — records what really arrived,
                        // including pairs that collide in g_meter.
                        {
                            Census& c = g_census[{u.pluginType, u.dataType}];
                            ++c.n;
                            c.nvals = u.current.size();
                            if (!u.current.empty()) c.first = u.current[0];
                        }
                        Slot& s = g_meter[u.dataType];
                        s.current = std::move(u.current); s.peak = std::move(u.peak);
                        s.have = true; s.pluginType = u.pluginType;
                    }
                    // Periodic summary: which DataTypes are live + a sample value.
                    // We ALREADY hold g_meterMx here (lk above) — std::mutex is
                    // NOT recursive, so re-locking self-deadlocks the worker
                    // while it holds the lock, which then hangs every main-thread
                    // getMeter() and freezes REAPER. Read the slots directly.
                    if (g_trace) {
                        static double sLastLog = 0;
                        if (t - sLastLog > 2.0) {
                            sLastLog = t;
                            // Census dump: one line per (PluginType, DataType)
                            // pair seen. PluginType is what disambiguates a
                            // channel strip's GateGain(5) from the Meter
                            // plug-in's TextRms(5) — see the Slot comment.
                            // pt=-1 means the plug-in omitted the field.
                            slog("[%.1f] --- census (pluginType, dataType) ---", t);
                            for (const auto& kv : g_census) {
                                slog("   pt=%-3d dt=%-3d n=%-7lld vals=%-5zu first=%.2f",
                                     kv.first.first, kv.first.second,
                                     kv.second.n, kv.second.nvals,
                                     double(kv.second.first));
                            }
                        }
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
    for (socket_t d : dataFds) SC_CLOSE(d);
    SC_CLOSE(listenFd);
    g_connected.store(false);
    netCleanup();
}

} // namespace

// ------------------------------------------------------------------- public
bool start(uint16_t tcpPort, uint16_t dataPort) {
    if (g_running.load()) return true;
    g_trace = std::getenv("REASIXTY_SSLCORE_TRACE") != nullptr;
    { std::lock_guard<std::mutex> lk(g_meterMx); for (auto& s : g_meter) s = Slot{}; }
    g_lastDataMs.store(0);
    g_running.store(true);
    slog("[start] tcpPort=%u dataPort=%u", unsigned(tcpPort), unsigned(dataPort));
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
