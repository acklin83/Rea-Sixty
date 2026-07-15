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

struct Slot {
    std::vector<float>   current, peak;
    std::vector<uint8_t> overload;      // f5 OverloadValues     — flashes, per channel
    std::vector<uint8_t> overloadHold;  // f6 OverloadInfHoldValues — latched, per channel
    bool have = false;
    // Chunk reassembly buffer (the Lissajous arrives in pieces; see
    // sslmeter::Update). `current` is only replaced once the array is complete.
    std::vector<float> asm_;
    size_t             asmGot_ = 0;
};

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
// Which meter view the plug-in should compute. See setView() in the header for
// why this gates the data rather than just the plug-in's own GUI.
std::atomic<int>  g_view{0};
std::atomic<bool> g_viewDirty{false};

// Lissajous geometry dump (REASIXTY_T10_DUMP). Separate from the trace flag: it
// writes every frame at ~25 Hz, which is the point — see the dump site.
bool g_t10Dump = false;

// 360SelectedView (= c5ea04de4990b792) = `view` as a double.
//
// Read the four identity frames in subscribeInitial() again: they are NOT
// "subscribes to meter data", whatever the old comment claimed. Resolved against
// the object map the plug-in announces about itself, they are —
//     636a1bfc… = GuiSlotIndex     b740ee1d… = PluginIdent
//     2ba7d9fe… = SessionDataId    fc74d763… = UniqueId
// and the trailing `08<varint>` is that property's VALUE, not a stream selector.
// type-18 is SET-PROPERTY, not subscribe. We never subscribed to anything: we
// say who we are, and the plug-in streams its meters at us unprompted. That is
// why hunting for "the missing RTA subscribe" got nowhere — there is no such
// frame. RTA was withheld because the plug-in only computes what its SELECTED
// VIEW needs, and we never set the view, so it sat on 0.0 = Overview.
//
// Property values are protobuf field 1, wire type 1 = little-endian double
// (09 + 8 B), NOT a varint — RtaPeakHold reads 090000000000001040 = 4.0. The
// varint reading was a half-decoded double.
std::vector<uint8_t> viewFrame(int view) {
    static const char* kHdr =
        "efbc510025000000100000000100000028e0c74515000000"
        "12000000" "c5ea04de4990b792" "09";
    auto f = fromHex(kHdr);
    const double d = double(view);
    uint8_t db[8];
    std::memcpy(db, &d, 8);                 // x86/ARM: already little-endian
    f.insert(f.end(), db, db + 8);
    return f;
}

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
    auto v = viewFrame(g_view.load());
    out.insert(out.end(), v.begin(), v.end());
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
    // Re-assert the view with the rest — see viewFrame(). Sending it on every
    // refresh is also how a view CHANGE reaches the plug-in: setView() only
    // stores, the worker carries it within 5 s.
    auto v = viewFrame(g_view.load());
    out.insert(out.end(), v.begin(), v.end());
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
        // A view change must not wait for the next 5 s refresh — the user has
        // already switched the UF1 screen and would stare at a dead element
        // until the plug-in starts computing that view's meters.
        if (!clients.empty() && g_viewDirty.exchange(false)) {
            const auto v = viewFrame(g_view.load());
            for (socket_t c : clients) ::send(c, reinterpret_cast<const char*>(v.data()), int(v.size()), 0);
            if (g_trace) slog("[%.1f] view -> %d", t, g_view.load());
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
                    Instance& inst = g_inst[ntohs(from.sin_port)];
                    inst.lastMs = nowMs();
                    for (auto& u : ups) {
                        if (u.dataType < 0 || u.dataType >= int(sslmeter::DataType::Count)) continue;
                        classify_(inst, u.dataType, u.current.size());
                        Slot& s = inst.meter[u.dataType];
                        bool completed = false;   // this message finished an array
                        if (u.chunked()) {
                            // Reassemble. Publish only once the whole array is in,
                            // so getMeter() never hands out a half-built image.
                            // f7 (the total) is usually ABSENT, so grow the buffer
                            // as chunks land and close it on the short tail chunk.
                            const size_t at = u.chunkStartIndex();
                            if (at == 0) s.asm_.clear();        // f9==0 starts an image
                            if (s.asm_.size() < at + u.current.size())
                                s.asm_.resize(at + u.current.size(), 0.f);
                            for (size_t q = 0; q < u.current.size(); ++q)
                                s.asm_[at + q] = u.current[q];
                            if (u.isTailChunk()) {
                                const size_t total = u.totalCount();
                                if (total && s.asm_.size() >= total) {
                                    s.asm_.resize(total);
                                    s.current = s.asm_;         // one complete array
                                    s.peak    = std::move(u.peak);
                                    s.have    = true;
                                    completed = true;
                                }
                            }
                        } else {
                            s.current = std::move(u.current); s.peak = std::move(u.peak);
                            s.have = true;
                            completed = true;
                        }
                        if (!u.overload.empty())     s.overload     = std::move(u.overload);
                        if (!u.overloadHold.empty()) s.overloadHold = std::move(u.overloadHold);
                        // Lissajous geometry dump — EVERY frame, deliberately NOT
                        // inside the 2 s summary throttle below. The stream runs at
                        // ~25 Hz, so throttling it sampled a 27 s correlation sweep
                        // ~30 times and caught the +1/-1 extremes only by luck. The
                        // extremes are the whole experiment: corr +1 lights one cell
                        // per row (row starts), corr -1 lights a whole row (row
                        // width). Opt-in via REASIXTY_T10_DUMP so it cannot bloat a
                        // normal trace run.
                        //
                        // This dumps the REASSEMBLED array (17113 floats). It used
                        // to fire per MESSAGE, i.e. per chunk, so every "frame" in
                        // cap97 was one chunk of the image — 5000 or the 2113-float
                        // remainder — and every geometry ever fitted to that log was
                        // fitted to a chunk boundary. Only complete arrays reach
                        // here now (s.have is set once, when the last chunk lands).
                        // ---- FULL METER DUMP (REASIXTY_T10_DUMP=1) -------------
                        // Log EVERY DataType with EVERY field. The last probe only
                        // dumped t10 and only its f3, which is exactly why four
                        // separate faults had to be chased one capture at a time.
                        // One run of the probe WAV must answer all of them.
                        //   MSG  = one message, all scalar fields (small arrays in
                        //          full: that is TextPeak/TextRms/VuPpm/overload).
                        //   T10  = one COMPLETE reassembled Lissajous array, sparse
                        //          (index value), throttled to ~2 Hz so 17113 floats
                        //          x 25 Hz cannot bury the log. Each probe section is
                        //          held 4 s, so 2 Hz still catches every one.
                        if (g_t10Dump) {
                            static FILE* mf = nullptr;
                            if (!mf) {
                                // /tmp, NOT ~/Desktop. The Desktop is TCC-protected:
                                // REAPER can write there, but the tooling that has to
                                // READ the dump afterwards cannot open a file another
                                // app created — cost a round trip on 2026-07-15. /tmp
                                // does get reaped eventually, so copy anything worth
                                // keeping into captures/ as soon as the run is done.
                                mf = std::fopen("/tmp/reasixty_meter_dump.log", "a");
                                if (mf) std::fprintf(mf, "\n==== RUN START ====\n");
                            }
                            if (mf) {
                                const unsigned sp = unsigned(ntohs(from.sin_port));
                                std::fprintf(mf, "MSG t=%.3f src=%u dt=%d n3=%zu n4=%zu n5=%zu "
                                                 "f7=%d f8=%d f9=%d",
                                             t, sp, u.dataType, s.current.size(), s.peak.size(),
                                             s.overload.size(), u.maxCount, u.chunkSize, u.chunkOffset);
                                if (s.current.size() <= 16) {
                                    std::fprintf(mf, " cur=[");
                                    for (size_t q = 0; q < s.current.size(); ++q)
                                        std::fprintf(mf, "%s%.3f", q ? "," : "", double(s.current[q]));
                                    std::fprintf(mf, "]");
                                }
                                if (s.peak.size() <= 16 && !s.peak.empty()) {
                                    std::fprintf(mf, " pk=[");
                                    for (size_t q = 0; q < s.peak.size(); ++q)
                                        std::fprintf(mf, "%s%.3f", q ? "," : "", double(s.peak[q]));
                                    std::fprintf(mf, "]");
                                }
                                if (!s.overload.empty()) {
                                    std::fprintf(mf, " ovl=[");
                                    for (size_t q = 0; q < s.overload.size(); ++q)
                                        std::fprintf(mf, "%s%u", q ? "," : "", unsigned(s.overload[q]));
                                    std::fprintf(mf, "]");
                                }
                                std::fprintf(mf, "\n");

                                if (u.dataType == int(sslmeter::DataType::Lissajous) &&
                                    completed && !s.current.empty()) {
                                    static double sLastT10 = 0;
                                    if (t - sLastT10 > 0.5) {
                                        sLastT10 = t;
                                        size_t nz = 0;
                                        for (float v : s.current) if (v != 0.f) ++nz;
                                        std::fprintf(mf, "T10 t=%.3f src=%u n=%zu nz=%zu\n",
                                                     t, sp, s.current.size(), nz);
                                        for (size_t k = 0; k < s.current.size(); ++k)
                                            if (s.current[k] != 0.f)
                                                std::fprintf(mf, "%zu %.3f\n", k, double(s.current[k]));
                                    }
                                }
                                std::fflush(mf);
                            }
                        }
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
                            // Lissajous (t10) structure probe. Its length CHANGES
                            // frame to frame (2113, 5000, …), which a fixed
                            // intensity raster cannot do — so the "t10 is a raster"
                            // reading in the 2026-07-14 notes looks wrong, and the
                            // 5000-floats-to-8560-byte-diamond mapping that was
                            // called unsolved may be the wrong question entirely.
                            // Dump the head so the real shape is visible.
                            for (const auto& kv : g_inst) {
                                const Slot& s = kv.second.meter[int(sslmeter::DataType::Lissajous)];
                                if (!s.have || s.current.empty()) continue;
                                char line[512]; int o = 0;
                                float mn = s.current[0], mx = s.current[0];
                                size_t nz = 0;
                                for (float v : s.current) {
                                    if (v < mn) mn = v;
                                    if (v > mx) mx = v;
                                    if (v != 0.f) ++nz;
                                }
                                o += std::snprintf(line + o, sizeof(line) - o,
                                    "[%.1f] t10 src=%u n=%zu nonzero=%zu min=%.3f max=%.3f",
                                    t, unsigned(kv.first), s.current.size(), nz,
                                    double(mn), double(mx));
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
                if (n == 0) { SC_CLOSE(c); clients.erase(clients.begin() + long(i)); continue; }
                // The plug-in TALKS BACK here, and we used to bin it unread
                // ("we don't need their content yet"). It does need reading: the
                // frames it sends carry the object ids it has registered, which
                // is exactly what the RTA hunt is missing. We replay 4 subscribe
                // frames lifted from an old capture; the real Core sends 178, and
                // RTA is one we never ask for. Rather than re-capture a cold
                // connect (the 880 MB pcap that had them lived in /tmp and is
                // gone), log what the plug-in tells us and read the ids off that.
                //
                // TCP is a stream, so accumulate per client and only parse whole
                // frames. Layout, confirmed against the frames we send in
                // subscribeInitial():
                //   efbc5100 | len32 | 12-B hdr | paylen32 | type32 | payload
                //   type 2 = register, 18 = subscribe, 17 = prepare (names the
                //   meter), 19 = data-port assignment. payload = 8-B objId + pb.
                if (n > 0 && g_trace) {
                    static std::map<socket_t, std::vector<uint8_t>> sAcc;
                    auto& acc = sAcc[c];
                    acc.insert(acc.end(), buf, buf + n);
                    size_t off = 0;
                    for (;;) {
                        if (acc.size() - off < 8) break;
                        // resync: the stream must start on the magic
                        if (!(acc[off] == 0xef && acc[off+1] == 0xbc &&
                              acc[off+2] == 0x51 && acc[off+3] == 0x00)) { ++off; continue; }
                        uint32_t flen = 0;
                        std::memcpy(&flen, &acc[off + 4], 4);
                        if (flen > 65536) { ++off; continue; }          // junk guard
                        if (acc.size() - off < size_t(8 + flen)) break;  // frame incomplete
                        const uint8_t* body = &acc[off + 8];
                        if (flen >= 20) {
                            uint32_t paylen = 0, ftype = 0;
                            std::memcpy(&paylen, body + 12, 4);
                            std::memcpy(&ftype,  body + 16, 4);
                            char line[512]; int o = 0;
                            o += std::snprintf(line + o, sizeof(line) - o,
                                               "[%.1f] PLUGIN->us type=%-3u paylen=%-4u", t,
                                               unsigned(ftype), unsigned(paylen));
                            const uint8_t* pay = body + 20;
                            const size_t   avail = (flen > 20) ? size_t(flen - 20) : 0;
                            if (avail >= 8) {
                                o += std::snprintf(line + o, sizeof(line) - o, " obj=");
                                for (int k = 0; k < 8; ++k)
                                    o += std::snprintf(line + o, sizeof(line) - o, "%02x", pay[k]);
                            }
                            // Remaining protobuf, hex + any printable run (the
                            // type-17 prepares carry meter NAMES in clear text).
                            if (avail > 8) {
                                o += std::snprintf(line + o, sizeof(line) - o, " pb=");
                                for (size_t k = 8; k < avail && o < int(sizeof(line)) - 96; ++k)
                                    o += std::snprintf(line + o, sizeof(line) - o, "%02x", pay[k]);
                                std::string txt;
                                for (size_t k = 8; k < avail; ++k)
                                    txt += (pay[k] >= 0x20 && pay[k] < 0x7f)
                                             ? char(pay[k]) : '.';
                                o += std::snprintf(line + o, sizeof(line) - o, " |%s|", txt.c_str());
                            }
                            slog("%s", line);
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
    g_trace   = std::getenv("REASIXTY_SSLCORE_TRACE") != nullptr;
    g_t10Dump = std::getenv("REASIXTY_T10_DUMP") != nullptr;
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

void setView(int view)
{
    if (view < 0) view = 0;
    if (g_view.exchange(view) != view) g_viewDirty.store(true);
}

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

bool getOverload(int dataType, std::vector<uint8_t>& ovl, std::vector<uint8_t>& ovlHold) {
    if (dataType < 0 || dataType >= int(sslmeter::DataType::Count)) return false;
    std::lock_guard<std::mutex> lk(g_meterMx);
    const Instance* fallback = nullptr;
    for (const auto& kv : g_inst) {
        const Instance& in = kv.second;
        if (in.kind == Kind::Meter) {
            const Slot& s = in.meter[dataType];
            if (s.have) { ovl = s.overload; ovlHold = s.overloadHold; return true; }
        } else if (in.kind == Kind::Unknown && !fallback) {
            fallback = &in;
        }
    }
    if (fallback) {
        const Slot& s = fallback->meter[dataType];
        if (s.have) { ovl = s.overload; ovlHold = s.overloadHold; return true; }
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

long long msSinceLastData() {
    const long long last = g_lastDataMs.load();
    if (last == 0) return INT64_MAX;
    return nowMs() - last;
}

} // namespace sslcore
