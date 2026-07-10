//
// ssl_core_probe — standalone harness to reverse-engineer / test the SSL 360°
// plugin<->Core handshake, without touching the extension or REAPER.
//
// Run it with SSL 360° NOT running, then insert an SSL Meter (Pro) plugin in a
// DAW. The plugin, unable to find the real Core, will probe/announce — this tool
// listens on the known SSL ports and logs everything, decoding meter data
// (SslMeterProtocol) and connection announcements as it sees them. Optionally it
// can also *emit* a Core announcement and accept the plugin's TCP connection, so
// we can learn (empirically, on a live plugin) exactly what makes the plugin
// start streaming.
//
// Usage:
//   ssl_core_probe [--udp P ...] [--tcp P] [--announce TCPPORT] [--secs N]
//
// Defaults: listen UDP 16008 16009 16010 16011 50881; TCP server 0 (off).
// This is a POSIX (mac/linux) dev tool — not built on Windows.
//
// See analysis/ssl360-protobuf/ + captures/cap87 for the decoded protocol.

#include "SslMeterProtocol.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

namespace {

double nowSecs() { timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }

const char* METNAME(int dt) {
    static const char* n[] = {"VuPpm","TextVuPpm","BarPeak","BarRms","TextPeak",
        "TextRms","Correlation","StereoBalance","Rta31Band","TextRta","Lissajous",
        "Loud_Momentary","Loud_ShortTerm","Loud_RangeLow","Loud_RangeHigh",
        "Loud_Readout1","Loud_Readout2","Loud_Readout3","Loud_Readout4","Loud_Readout5",
        "Loud_Readout6","Loud_Readout7","Loud_Readout8","Loud_Readout9","Loud_Readout10",
        "Loud_CompleteHistory","Loud_ScrollableHistory","Loud_Histogram"};
    return (dt >= 0 && dt < 28) ? n[dt] : "?";
}

void hexdump(const uint8_t* b, size_t n, size_t cap = 48) {
    for (size_t i = 0; i < n && i < cap; ++i) std::printf("%02x", b[i]);
    if (n > cap) std::printf("...(%zu)", n);
}

int makeUdp(uint16_t port) {
    int s = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return -1;
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
        std::printf("  [udp %u] bind FAILED: %s (port in use — real Core / plugin owns it?)\n",
                    port, std::strerror(errno));
        ::close(s);
        return -1;
    }
    int fl = fcntl(s, F_GETFL, 0); fcntl(s, F_SETFL, fl | O_NONBLOCK);
    std::printf("  [udp %u] listening\n", port);
    return s;
}

// Faithfully rebuild the connection announcement seen in cap87 (efbc frame +
// 28-byte Lgx header + protobuf{ f1 AppVerMajor=2, f3 IpAddress, f4 Port,
// f5 MachineName }), advertising OUR tcp port.
std::vector<uint8_t> buildAnnouncement(uint16_t tcpPort, const char* ip, const char* machine) {
    auto putVarint = [](std::vector<uint8_t>& v, uint64_t x) {
        while (x >= 0x80) { v.push_back(uint8_t(x) | 0x80); x >>= 7; }
        v.push_back(uint8_t(x));
    };
    auto putLenDelim = [&](std::vector<uint8_t>& v, int field, const uint8_t* d, size_t n) {
        putVarint(v, (uint64_t(field) << 3) | 2); putVarint(v, n);
        v.insert(v.end(), d, d + n);
    };
    // protobuf body
    std::vector<uint8_t> pb;
    putVarint(pb, (1 << 3) | 0); putVarint(pb, 2);                              // f1 AppVerMajor=2
    putLenDelim(pb, 3, reinterpret_cast<const uint8_t*>(ip), std::strlen(ip));  // f3 IpAddress
    putVarint(pb, (4 << 3) | 0); putVarint(pb, tcpPort);                        // f4 Port
    putLenDelim(pb, 5, reinterpret_cast<const uint8_t*>(machine), std::strlen(machine)); // f5 MachineName
    // 28-byte Lgx header (announce pattern from cap87: 16,1,0,innerLen,12,0,0)
    std::vector<uint8_t> body(28, 0);
    auto putU32 = [](std::vector<uint8_t>& v, size_t off, uint32_t x) { std::memcpy(v.data() + off, &x, 4); };
    putU32(body, 0, 16); putU32(body, 4, 1); putU32(body, 8, 0);
    putU32(body, 12, uint32_t(pb.size() + 8)); putU32(body, 16, 12);
    body.insert(body.end(), pb.begin(), pb.end());
    // efbc frame
    std::vector<uint8_t> frame;
    frame.insert(frame.end(), sslmeter::kMagic, sslmeter::kMagic + 4);
    uint32_t fl = uint32_t(body.size());
    frame.insert(frame.end(), reinterpret_cast<uint8_t*>(&fl), reinterpret_cast<uint8_t*>(&fl) + 4);
    frame.insert(frame.end(), body.begin(), body.end());
    return frame;
}

// The exact "server heartbeat" frame Core sends to a connected plugin
// (captured 49586->56704). type=10, scope=0x5f7a0579, msgid=284, protobuf
// f1="server heartbeat". Replayed verbatim; the offset-8 seq/time field is left
// as captured (refine if the plugin rejects a static value).
std::vector<uint8_t> serverHeartbeat() {
    static const char* hx =
        "efbc51002e000000"                                   // magic + len(46)
        "100000000100000054af4b0e1e0000000a00000079057a5f1c010000"  // 28-B header
        "0a1073657276657220686561727462656174";              // pb f1 "server heartbeat"
    std::vector<uint8_t> v;
    for (const char* p = hx; p[0] && p[1]; p += 2) {
        auto nyb = [](char c){ return (c<='9')?c-'0':(c|0x20)-'a'+10; };
        v.push_back(uint8_t(nyb(p[0]) << 4 | nyb(p[1])));
    }
    return v;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<uint16_t> udpPorts;
    for (uint16_t p = 16008; p <= 16030; ++p) udpPorts.push_back(p);   // Core data-port range
    udpPorts.push_back(50881);
    uint16_t tcpPort = 0;
    int announceTo = 0;       // if set, advertise this TCP port to 16008/16009
    bool serve = false;       // reply to connected plugins with server-heartbeat
    double secs = 30.0;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--udp") { udpPorts.clear(); while (i + 1 < argc && argv[i+1][0] != '-') udpPorts.push_back(uint16_t(std::atoi(argv[++i]))); }
        else if (a == "--tcp" && i + 1 < argc) tcpPort = uint16_t(std::atoi(argv[++i]));
        else if (a == "--announce" && i + 1 < argc) announceTo = std::atoi(argv[++i]);
        else if (a == "--serve") serve = true;
        else if (a == "--secs" && i + 1 < argc) secs = std::atof(argv[++i]);
    }

    std::printf("ssl_core_probe — run with SSL 360° OFF, then insert an SSL Meter plugin.\n");
    std::printf("listening %zu UDP port(s), tcp=%d, announce=%d, %.0fs\n\n",
                udpPorts.size(), tcpPort, announceTo, secs);

    std::vector<int> udp;
    for (uint16_t p : udpPorts) { int s = makeUdp(p); if (s >= 0) udp.push_back(s); }

    int announceSock = -1;
    if (announceTo) { announceSock = ::socket(AF_INET, SOCK_DGRAM, 0); }

    int listenFd = -1;
    if (tcpPort) {
        listenFd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        int yes = 1; setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(tcpPort); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(listenFd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || ::listen(listenFd, 8) != 0) {
            std::printf("  [tcp %u] bind/listen FAILED: %s\n", tcpPort, std::strerror(errno)); ::close(listenFd); listenFd = -1;
        } else {
            int fl = fcntl(listenFd, F_GETFL, 0); fcntl(listenFd, F_SETFL, fl | O_NONBLOCK);
            std::printf("  [tcp %u] listening\n", tcpPort);
        }
    }
    std::vector<int> tcpClients;

    // Per (srcPort,DataType) rate-limit so the log stays readable.
    std::map<std::pair<int,int>, double> lastLogged;

    const double t0 = nowSecs();
    double lastAnnounce = 0;
    double lastHeartbeat = 0;
    const std::vector<uint8_t> hb = serverHeartbeat();
    uint8_t buf[65536];

    while (nowSecs() - t0 < secs) {
        // periodic announcement
        if (announceSock >= 0 && nowSecs() - lastAnnounce > 1.0) {
            lastAnnounce = nowSecs();
            auto ann = buildAnnouncement(uint16_t(announceTo), "127.0.0.1", "Rea-Sixty");
            for (uint16_t dp : {16008, 16009}) {
                sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(dp); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                ::sendto(announceSock, ann.data(), ann.size(), 0, reinterpret_cast<sockaddr*>(&a), sizeof(a));
            }
        }

        // periodic server-heartbeat to every connected plugin
        if (serve && !tcpClients.empty() && nowSecs() - lastHeartbeat > 0.25) {
            lastHeartbeat = nowSecs();
            for (int c : tcpClients) ::send(c, hb.data(), hb.size(), 0);
        }

        fd_set rset; FD_ZERO(&rset); int maxfd = -1;
        for (int s : udp) { FD_SET(s, &rset); maxfd = std::max(maxfd, s); }
        if (listenFd >= 0) { FD_SET(listenFd, &rset); maxfd = std::max(maxfd, listenFd); }
        for (int c : tcpClients) { FD_SET(c, &rset); maxfd = std::max(maxfd, c); }
        timeval tv{0, 50 * 1000};
        if (::select(maxfd + 1, &rset, nullptr, nullptr, &tv) <= 0) continue;

        // UDP
        for (int s : udp) {
            if (!FD_ISSET(s, &rset)) continue;
            sockaddr_in from{}; socklen_t fl = sizeof(from);
            int n = int(::recvfrom(s, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fl));
            if (n <= 0) continue;
            const uint16_t sport = ntohs(from.sin_port);
            std::vector<sslmeter::Update> ups;
            int got = sslmeter::parseDatagram(buf, size_t(n), ups);
            if (got > 0) {
                for (const auto& u : ups) {
                    auto key = std::make_pair(int(sport), u.dataType);
                    double& last = lastLogged[key];
                    if (nowSecs() - last < 0.5) continue;   // ≤2 lines/s per stream
                    last = nowSecs();
                    std::printf("[UDP src:%u] %-16s cur=%zu peak=%zu", sport, METNAME(u.dataType), u.current.size(), u.peak.size());
                    for (size_t k = 0; k < u.current.size() && k < 6; ++k) std::printf(" %.2f", u.current[k]);
                    std::printf("\n");
                }
            } else {
                // Rate-limit non-meter chatter (our own announce loopback etc.)
                // to one line / 2 s per source so real meter data stays visible.
                auto key = std::make_pair(int(sport), -1);
                double& last = lastLogged[key];
                if (nowSecs() - last >= 2.0) {
                    last = nowSecs();
                    std::printf("[UDP src:%u] %d bytes (not meter): ", sport, n); hexdump(buf, size_t(n)); std::printf("\n");
                }
            }
        }
        // TCP accept
        if (listenFd >= 0 && FD_ISSET(listenFd, &rset)) {
            sockaddr_in from{}; socklen_t fl = sizeof(from);
            int c = ::accept(listenFd, reinterpret_cast<sockaddr*>(&from), &fl);
            if (c >= 0) {
                int f = fcntl(c, F_GETFL, 0); fcntl(c, F_SETFL, f | O_NONBLOCK);
                std::printf("[TCP] plugin connected from :%u%s\n", ntohs(from.sin_port),
                            serve ? " (serving heartbeats)" : "");
                if (serve) ::send(c, hb.data(), hb.size(), 0);
                tcpClients.push_back(c);
            }
        }
        // TCP reads
        for (size_t i = 0; i < tcpClients.size();) {
            int c = tcpClients[i];
            if (FD_ISSET(c, &rset)) {
                int n = int(::recv(c, buf, sizeof(buf), 0));
                if (n <= 0) { std::printf("[TCP] client closed\n"); ::close(c); tcpClients.erase(tcpClients.begin() + long(i)); continue; }
                std::printf("[TCP] %d bytes from plugin: ", n); hexdump(buf, size_t(n)); std::printf("\n");
            }
            ++i;
        }
    }

    for (int s : udp) ::close(s);
    for (int c : tcpClients) ::close(c);
    if (listenFd >= 0) ::close(listenFd);
    if (announceSock >= 0) ::close(announceSock);
    std::printf("\ndone.\n");
    return 0;
}
