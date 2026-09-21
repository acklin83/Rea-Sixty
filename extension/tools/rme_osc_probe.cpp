// rme_osc_probe — talk to TotalMix FX's Global OSC and print what comes back.
//
// Exercises the half of the RME path that a unit test cannot: a real socket
// against a real mixer. Run it while TotalMix is up, with an OSC remote whose
// ports match. On Frank's Mac Studio on 2026-09-19 that was Remote 1, TotalMix
// receiving on 7001 and answering to 7002, and /sendall came back with 3595
// addresses — with the UFX+ itself DISCONNECTED, because TotalMix answers out
// of its stored state. That is also why this is a probe and not a test: what it
// prints depends on the room.
//
//   rme_osc_probe [send-port] [listen-port] [seconds] [filter]
//   defaults:      7001        7002          3
//
// With a FILTER, every message whose address contains it is printed raw, first
// the /sendall dump, then every CHANGE TotalMix reports while the probe runs.
// That is how an index is read against what the TotalMix window shows: run it
// for a minute with "input/0/", switch a type in TotalMix, read the line
// (2026-09-21, the colour palette and the EQ type indices).
//
//   rme_osc_probe <send> <listen> <seconds> <filter> /addr=value [/addr=value ...]
//
// ⛔ SETS VALUES IN TOTALMIX. Each /addr=value is sent as a float, one per second
// starting at t = 1 s, so every echo lands on its own line. Only for a channel
// someone has cleared for it (M2, 2026-09-21: Frank cleared Ph 11/12).
//
// ⛔ It sends only /sendall, which asks TotalMix to dump state. It sets nothing.
//
#include "RmeOsc.h"
#include "RmeState.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  using socket_t = SOCKET;
  static constexpr socket_t kInvalid = INVALID_SOCKET;
  #define RP_CLOSE closesocket
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <sys/time.h>
  using socket_t = int;
  static constexpr socket_t kInvalid = -1;
  #define RP_CLOSE ::close
#endif

using namespace reasixty::rme;

static const char* roleText(const State& st, int ch, char* buf, std::size_t n)
{
    if (!State::roleAssigned(ch)) { std::snprintf(buf, n, "unassigned"); return buf; }
    if (st.roleHidden(ch)) {
        std::snprintf(buf, n, "channel %d — HIDDEN from this OSC remote", ch);
        return buf;
    }
    const Channel* c = st.outputForRole(ch);
    std::snprintf(buf, n, "channel %d  \"%s\"  %.2f dB", ch,
                  c ? c->name.c_str() : "", c ? c->volume : 0.0);
    return buf;
}

int main(int argc, char** argv)
{
    const int sendPort   = (argc > 1) ? std::atoi(argv[1]) : 7001;
    const int listenPort = (argc > 2) ? std::atoi(argv[2]) : 7002;
    const double seconds = (argc > 3) ? std::atof(argv[3]) : 3.0;
    const std::string filter = (argc > 4) ? argv[4] : "";
    std::vector<std::pair<std::string, float>> writes;
    for (int a = 5; a < argc; ++a) {
        const std::string w = argv[a];
        const auto eq = w.find('=');
        if (w.empty() || w[0] != '/' || eq == std::string::npos) {
            std::printf("bad write '%s', want /addr=value\n", w.c_str());
            return 1;
        }
        writes.emplace_back(w.substr(0, eq),
                            static_cast<float>(std::atof(w.c_str() + eq + 1)));
    }
    std::size_t nextWrite = 0;
    std::setvbuf(stdout, nullptr, _IOLBF, 0);   // live lines, also into a pipe

#if defined(_WIN32)
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    socket_t s = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s == kInvalid) { std::printf("socket() failed\n"); return 1; }
    int yes = 1;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&yes), sizeof(yes));
    sockaddr_in me{};
    me.sin_family = AF_INET;
    me.sin_port   = htons(static_cast<std::uint16_t>(listenPort));
    me.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(s, reinterpret_cast<sockaddr*>(&me), sizeof(me)) != 0) {
        std::printf("cannot bind %d — something else is listening there "
                    "(another OSC client on the same remote?)\n", listenPort);
        RP_CLOSE(s);
        return 1;
    }
#if defined(_WIN32)
    DWORD tv = 200;
#else
    timeval tv{}; tv.tv_usec = 200000;
#endif
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&tv), sizeof(tv));

    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port   = htons(static_cast<std::uint16_t>(sendPort));
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    const auto ask = encodeFloat("/sendall", 1.0f);
    ::sendto(s, reinterpret_cast<const char*>(ask.data()),
             static_cast<int>(ask.size()), 0,
             reinterpret_cast<sockaddr*>(&to), sizeof(to));
    std::printf("sent /sendall to 127.0.0.1:%d, listening on %d for %.1fs\n",
                sendPort, listenPort, seconds);

    State st;
    std::map<std::string, int> unknown;
    std::map<std::string, std::string> last;   // filter mode: print changes only
    std::size_t packets = 0, messages = 0;
    std::vector<std::uint8_t> buf(65536);
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration<double>(
               std::chrono::steady_clock::now() - t0).count() < seconds) {
        const double el = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - t0).count();
        if (nextWrite < writes.size() && el >= 1.0 + static_cast<double>(nextWrite)) {
            const auto& w = writes[nextWrite++];
            const auto pkt = encodeFloat(w.first, w.second);
            ::sendto(s, reinterpret_cast<const char*>(pkt.data()),
                     static_cast<int>(pkt.size()), 0,
                     reinterpret_cast<sockaddr*>(&to), sizeof(to));
            std::printf("%7.2fs  SENT %-35s %g\n", el, w.first.c_str(), w.second);
        }
        const int n = ::recv(s, reinterpret_cast<char*>(buf.data()),
                             static_cast<int>(buf.size()), 0);
        if (n <= 0) continue;
        ++packets;
        messages += forEachMessage(
            std::span<const std::uint8_t>(buf.data(), static_cast<std::size_t>(n)),
            [&](const Message& m) {
                if (!ingest(st, m)) ++unknown[m.address];
                if (filter.empty() || m.address.find(filter) == std::string::npos)
                    return;
                std::string v;
                for (const Arg& a : m.args) {
                    char t[64];
                    if (a.type == Arg::Type::String)
                        std::snprintf(t, sizeof(t), " \"%s\"", a.s.c_str());
                    else
                        std::snprintf(t, sizeof(t), " %g", a.number());
                    v += t;
                }
                auto it = last.find(m.address);
                if (it != last.end() && it->second == v) return;
                const double at = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - t0).count();
                std::printf("%7.2fs  %-40s%s%s\n", at, m.address.c_str(), v.c_str(),
                            it == last.end() ? "" : "   <- changed");
                last[m.address] = v;
            });
    }
    RP_CLOSE(s);
#if defined(_WIN32)
    WSACleanup();
#endif

    std::printf("\n%zu packets, %zu messages, %zu we had no home for\n",
                packets, messages, unknown.size());
    if (messages == 0) {
        std::printf("\nNothing came back. In TotalMix: Options, Enable OSC Control,\n"
                    "and in Settings, OSC, a remote with these ports marked In Use.\n");
        return 2;
    }

    char b[128];
    std::printf("\nControl room\n");
    std::printf("  Main       %s\n", roleText(st, st.mainOut, b, sizeof(b)));
    std::printf("  Main B     %s\n", roleText(st, st.mainOutB, b, sizeof(b)));
    for (int i = 0; i < 4; ++i)
        std::printf("  Phones %d   %s\n", i + 1, roleText(st, st.phones[i], b, sizeof(b)));
    std::printf("  Talk       %s\n", roleText(st, st.talkChannel, b, sizeof(b)));
    std::printf("  dim %d  mono %d  speaker B %d  talkback %d  link A/B %d\n",
                int(st.dim), int(st.mono), int(st.speakerB),
                int(st.talkback), int(st.linkAB));
    std::printf("  dim reduction %.1f dB   recall %.1f dB\n",
                st.dimReduction, st.recallVolume);

    // ⛔ COUNT THE STRIPS, NOT THE MAP. The map also holds the right halves of
    // stereo pairs, which answer for the per-side parameters (phase, delay,
    // room EQ) and are not channels the mixer shows. Channel::seen is the
    // difference; printing the map size said 94 outputs where TotalMix shows 5.
    auto strips = [](const std::map<int, Channel>& m) {
        std::size_t n = 0;
        for (const auto& kv : m) if (kv.second.seen) ++n;
        return n;
    };
    std::printf("\nChannels:  %zu inputs, %zu playbacks, %zu outputs"
                "   (map holds %zu / %zu / %zu incl. stereo right halves)\n",
                strips(st.inputs), strips(st.playbacks), strips(st.outputs),
                st.inputs.size(), st.playbacks.size(), st.outputs.size());
    std::printf("Outputs:\n");
    for (const auto& [idx, c] : st.outputs) {
        if (!c.seen) continue;
        std::printf("  %3d  %-14s %8.2f dB  colour %d\n",
                    idx, c.name.c_str(), c.volume, c.colour);
    }

    std::printf("\nSnapshots: ");
    for (const SnapshotState v : st.snapshot)
        std::printf("%s ", v == SnapshotState::Active  ? "ACTIVE"
                         : v == SnapshotState::Changed ? "changed"
                                                       : "-");
    std::printf("\n");

    std::printf("\nLevels: %zu in, %zu playback, %zu out%s\n",
                st.levelIn.size(), st.levelPb.size(), st.levelOut.size(),
                st.levelOut.empty()
                    ? "   (Send Peak Level is off for this remote)" : "");
    return 0;
}
