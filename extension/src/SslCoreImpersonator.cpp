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
#include <deque>
#include <set>
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
// Timestamp of the most recent NEW plugin TCP connection. A project load makes
// every SSL plug-in connect in a ~1/s burst, and each one selects its own track
// on connect → REAPER's selection "counts through" the channels. The extension
// watches this to freeze the selection during the burst. 0 = none yet.
std::atomic<long long> g_lastNewConnMs{0};

struct Slot { std::vector<float> current, peak; bool have = false; };

// SSL plug-ins number their meters PER FAMILY, and the datagram does not say
// which family it came from: PluginMeterDataMessage.DataType is a plain int32
// (schema), not a typed enum, and its vocabulary depends on the plug-in —
// MeterPluginDataType (0..27) for Meter/MeterPro, ChannelStripMeterType (0..6)
// for a channel strip, where 4 = CompGain and 5 = GateGain. The PluginType field
// would disambiguate, but MEASURED 2026-07-14 it is simply absent: every frame
// arrived with f1 omitted. So the Meter plug-in's TextRms(5) and a channel
// strip's GateGain(5) are the same index and must NOT share a slot — with both
// plug-ins loaded they overwrite each other (observed in the trace: dt=4/5
// flipping between vals=2 and vals=1).
//
// Each plug-in instance streams from its own UDP socket, so the datagram's
// SOURCE PORT identifies the instance. One slot array per instance.
enum class Kind : uint8_t { Unknown, Meter, ChannelStrip };
struct Instance {
    std::array<Slot, int(sslmeter::DataType::Count)> meter;
    Kind      kind   = Kind::Unknown;
    long long lastMs = 0;
};
std::mutex                   g_meterMx;
std::map<uint16_t, Instance> g_inst;      // key = UDP source port = one plug-in

// Per-instance track identity — the fix for "GR follows the wrong channel".
// Each plug-in announces, on its OWN TCP control connection at connect, its
// HostTrackName + HostTrackIndex, but the UDP meter datagram carries no track
// id (see the Instance comment). So we CORRELATE BY TIMING: capture {name,index}
// per client, queue it once both are in, and the next NEW UDP source port claims
// the front of the queue → g_portName / g_portIndex. That lets a track-keyed
// getChannelStripMeter pick the FOCUSED track's channel strip instead of just
// the first one. g_client*/g_pending/g_namedClients are worker-thread-only
// (the select loop); g_portName/g_portIndex are read under g_meterMx.
struct PendingInst { std::string name; int index; };
std::deque<PendingInst>          g_pending;        // announced, not yet port-tied
std::set<socket_t>               g_namedClients;   // clients already queued
std::map<socket_t, std::string>  g_clientName;     // per-client, awaiting its index
std::map<socket_t, int>          g_clientIndex;    // per-client, awaiting its name
std::map<uint16_t, std::string>  g_portName;       // UDP port -> track name  (g_meterMx)
std::map<uint16_t, int>          g_portIndex;      // UDP port -> HostTrackIndex (g_meterMx)

// Classify an instance from what it emits. ChannelStripMeterType only spans
// 0..6, so ANY DataType >= 7 can only be a Meter/MeterPro plug-in (Rta,
// Lissajous, Loudness…) — authoritative and sticky. Conversely a channel strip's
// CompGain/GateGain carry exactly ONE value where the Meter plug-in's
// TextPeak/TextRms carry two (L,R) — measured, and only used while still
// unclassified, so a late DataType>=7 still wins.
void classify_(Instance& in, int dataType, size_t nvals)
{
    if (dataType >= 7) { in.kind = Kind::Meter; return; }
    if (in.kind == Kind::Unknown && (dataType == 4 || dataType == 5) && nvals == 1)
        in.kind = Kind::ChannelStrip;
}

const char* kindName_(Kind k)
{
    return k == Kind::Meter ? "Meter" : (k == Kind::ChannelStrip ? "ChanStrip" : "?");
}

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
    // FIX 2026-07-26 (Frank HW-confirmed): DROP the two non-config "command"
    // frames ctrlFrame(6) + ctrlFrame(7). One of them is a "focus/activate this
    // channel" command; replaying it verbatim to EVERY connecting plug-in made
    // each select its OWN track in REAPER → the arrange "counted through" the
    // channels 14→2 at project load. Real Core presumably sends it to only the
    // one focused channel. Port assignment (serverConfig) + the meter subscribes
    // remain, so metering is unaffected (verified: ChanStrip CompGain/GateGain
    // still stream). NOT isolated to 6 vs 7 — both dropped; both proven
    // unnecessary for meters. See memory REGRESSION-load-countdown-2026-07-26.
    // add(ctrlFrame(6, {}));
    // { std::vector<uint8_t> pb; putVarint(pb, (1<<3)|0); putVarint(pb, 0x9abc); add(ctrlFrame(7, pb)); }
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
                g_lastNewConnMs.store(nowMs());
                slog("[%.1f] plugin CONNECTED (fd=%d), sent opening (%zu B) + subscribe "
                     "(%zu B), data port %u", t, int(c), seq.size(), sub.size(), unsigned(dataPort));
            }
        }
        for (socket_t d : dataFds) {
          if (!FD_ISSET(d, &rset)) continue;
          // Keep the sender: its source port is what tells two plug-in instances
          // apart (see the Instance comment — the wire carries no PluginType).
          sockaddr_in from{}; socklen_t fromLen = sizeof(from);
          int n = int(::recvfrom(d, reinterpret_cast<char*>(buf), sizeof(buf), 0,
                                 reinterpret_cast<sockaddr*>(&from), &fromLen));
          if (n > 0) {
                std::vector<sslmeter::Update> ups;
                if (sslmeter::parseDatagram(buf, size_t(n), ups) > 0) {
                    g_lastDataMs.store(nowMs());
                    std::lock_guard<std::mutex> lk(g_meterMx);
                    const uint16_t sp = ntohs(from.sin_port);
                    if (g_inst.find(sp) == g_inst.end() && !g_pending.empty()) {
                        // NEW instance. The datagram carries no track id, so tie
                        // this port to the {name,index} the just-connected plug-in
                        // announced on its control socket (queued in connect order).
                        g_portName[sp]  = g_pending.front().name;
                        g_portIndex[sp] = g_pending.front().index;
                        slog("[corr] UDP src=%u -> track %d (%s)", unsigned(sp),
                             g_pending.front().index, g_pending.front().name.c_str());
                        g_pending.pop_front();
                    }
                    Instance& inst = g_inst[sp];
                    inst.lastMs = nowMs();
                    for (auto& u : ups) {
                        if (u.dataType < 0 || u.dataType >= int(sslmeter::DataType::Count)) continue;
                        classify_(inst, u.dataType, u.current.size());
                        Slot& s = inst.meter[u.dataType];
                        s.current = std::move(u.current); s.peak = std::move(u.peak);
                        s.have = true;
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
                            // One line per plug-in instance (UDP source port),
                            // its classification, and its live DataTypes. A
                            // channel-strip line's dt4/dt5 are Comp/Gate GR.
                            for (const auto& kv : g_inst) {
                                char line[512]; int off = 0;
                                off += std::snprintf(line + off, sizeof(line) - off,
                                    "[%.1f] src=%-5u %-9s:", t, unsigned(kv.first),
                                    kindName_(kv.second.kind));
                                for (int d = 0; d < int(sslmeter::DataType::Count); ++d) {
                                    const Slot& s = kv.second.meter[d];
                                    if (!s.have || s.current.empty()) continue;
                                    off += std::snprintf(line + off, sizeof(line) - off,
                                        " %d[%zu]=%.1f", d, s.current.size(),
                                        double(s.current[0]));
                                    if (off > int(sizeof(line)) - 32) break;
                                }
                                slog("%s", line);
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
                if (n == 0) {
                    SC_CLOSE(c);
                    g_namedClients.erase(c);
                    g_clientName.erase(c);
                    g_clientIndex.erase(c);
                    clients.erase(clients.begin() + long(i));
                    continue;
                }
                // The plug-in announces its HostTrackName + HostTrackIndex over
                // this control connection at connect. We used to bin these unread;
                // capture them so a UDP source port can be tied to a REAPER track
                // (the datagram itself carries no track id). TCP is a stream, so
                // accumulate per client and only parse whole frames. Frame layout
                // (matches subscribeInitial): efbc5100 | len32 | 12-B hdr |
                // paylen32 | type32 | payload(8-B objId + protobuf).
                if (n > 0) {
                    static std::map<socket_t, std::vector<uint8_t>> sAcc;
                    auto& acc = sAcc[c];
                    acc.insert(acc.end(), buf, buf + n);
                    size_t off = 0;
                    for (;;) {
                        if (acc.size() - off < 8) break;
                        // resync: the stream must start on the frame magic
                        if (!(acc[off] == 0xef && acc[off+1] == 0xbc &&
                              acc[off+2] == 0x51 && acc[off+3] == 0x00)) { ++off; continue; }
                        uint32_t flen = 0;
                        std::memcpy(&flen, &acc[off + 4], 4);
                        if (flen > 65536) { ++off; continue; }          // junk guard
                        if (acc.size() - off < size_t(8 + flen)) break;  // incomplete
                        const uint8_t* body = &acc[off + 8];
                        if (flen >= 20) {
                            uint32_t ftype = 0;
                            std::memcpy(&ftype, body + 16, 4);
                            const uint8_t* pay = body + 20;
                            const size_t   avail = (flen > 20) ? size_t(flen - 20) : 0;
                            // type-18 SET of HostTrackName (obj f0630f41c7667f0c,
                            // pb 0a<len><ascii>) + HostTrackIndex (obj cb39da8c9c8c43ee,
                            // pb 08<varint>, 1-based). Take each ONCE per client;
                            // g_namedClients gates later re-sends (on selection change)
                            // so identity is tied only at connect.
                            static const uint8_t kHostTrackNameObj[8] =
                                { 0xf0,0x63,0x0f,0x41,0xc7,0x66,0x7f,0x0c };
                            static const uint8_t kHostTrackIndexObj[8] =
                                { 0xcb,0x39,0xda,0x8c,0x9c,0x8c,0x43,0xee };
                            if (ftype == 18 && avail >= 10 &&
                                g_namedClients.find(c) == g_namedClients.end()) {
                                if (std::memcmp(pay, kHostTrackNameObj, 8) == 0 &&
                                    pay[8] == 0x0a) {                 // 0a <len> <ascii>
                                    const size_t slen = pay[9];
                                    if (10 + slen <= avail && slen > 0)
                                        g_clientName[c].assign(
                                            reinterpret_cast<const char*>(pay + 10), slen);
                                } else if (std::memcmp(pay, kHostTrackIndexObj, 8) == 0 &&
                                           pay[8] == 0x08) {          // 08 <varint>
                                    g_clientIndex[c] = int(pay[9] & 0x7f);   // 1-based
                                }
                                // Queue once this client announced BOTH; the UDP
                                // block above ties them to the next new port.
                                auto itN = g_clientName.find(c);
                                auto itI = g_clientIndex.find(c);
                                if (itN != g_clientName.end() && itI != g_clientIndex.end()) {
                                    g_pending.push_back({ itN->second, itI->second });
                                    g_namedClients.insert(c);
                                    g_clientName.erase(itN);
                                    g_clientIndex.erase(itI);
                                }
                            }
                        }
                        off += 8 + flen;
                    }
                    acc.erase(acc.begin(), acc.begin() + long(off));
                    if (acc.size() > (1u << 20)) acc.clear();   // runaway guard
                }
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
    { std::lock_guard<std::mutex> lk(g_meterMx); g_inst.clear(); }
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
    // Prefer a positively-identified Meter plug-in: a channel strip on the same
    // track publishes Output(2)/OutputRms(3) into the very indices the UF1 meter
    // view reads as BarPeak(2)/BarRms(3), so taking "whatever arrived last"
    // shows the strip's output instead of the Meter plug-in's bars.
    const Instance* fallback = nullptr;
    for (const auto& kv : g_inst) {
        const Instance& in = kv.second;
        if (in.kind == Kind::Meter) {
            const Slot& s = in.meter[dataType];
            if (s.have) { current = s.current; peak = s.peak; return true; }
        } else if (in.kind == Kind::Unknown && !fallback) {
            fallback = &in;
        }
    }
    // Nothing classified yet (single plug-in, first datagrams) — old behaviour.
    if (fallback) {
        const Slot& s = fallback->meter[dataType];
        if (s.have) { current = s.current; peak = s.peak; return true; }
    }
    return false;
}

bool getChannelStripMeter(int csType, std::vector<float>& current) {
    if (csType < 0 || csType > int(ChannelStripMeter::MicPreSaturation)) return false;
    std::lock_guard<std::mutex> lk(g_meterMx);
    for (const auto& kv : g_inst) {
        if (kv.second.kind != Kind::ChannelStrip) continue;
        const Slot& s = kv.second.meter[csType];
        if (s.have && !s.current.empty()) { current = s.current; return true; }
    }
    return false;
}

bool getChannelStripMeterForTrackIndex(int csType, int trackIndex,
                                       std::vector<float>& current) {
    if (csType < 0 || csType > int(ChannelStripMeter::MicPreSaturation)) return false;
    if (trackIndex <= 0) return false;
    std::lock_guard<std::mutex> lk(g_meterMx);
    for (const auto& kv : g_inst) {
        if (kv.second.kind != Kind::ChannelStrip) continue;
        // Only the channel strip that announced THIS (1-based) track index —
        // so the GR follows the focused channel instead of the first instance.
        auto pi = g_portIndex.find(kv.first);
        if (pi == g_portIndex.end() || pi->second != trackIndex) continue;
        const Slot& s = kv.second.meter[csType];
        if (s.have && !s.current.empty()) { current = s.current; return true; }
    }
    return false;
}

long long msSinceLastData() {
    const long long last = g_lastDataMs.load();
    if (last == 0) return INT64_MAX;
    return nowMs() - last;
}

long long msSinceLastNewConn() {
    const long long last = g_lastNewConnMs.load();
    if (last == 0) return INT64_MAX;
    return nowMs() - last;
}

} // namespace sslcore
