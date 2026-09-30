// SslTrackIdentity: an SSL plug-in's track follows it when the track moves
// (Frank 30.09.2026: two tracks inserted above T1..T4, the gate GR showed two
// tracks further up until the project was reloaded). Replays that session:
// T1 connects on track 7, gets its port, the insert renumbers it to 9.

#include "SslTrackIdentity.h"

#include <cstdio>
#include <deque>
#include <string>

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

using sslcore::id::Announce;

int main()
{
    std::set<int> named;
    std::map<int, std::string> clientName;
    std::map<int, int> clientIndex;
    std::map<int, uint16_t> connPort;
    std::map<uint16_t, std::string> portName;
    std::map<uint16_t, int> portIndex;
    sslcore::id::TrackIdentity<int> id{ named, clientName, clientIndex,
                                        connPort, portName, portIndex };
    std::deque<int> pending;   // what the caller queues on Completed
    const int T1 = 40;         // connection fd
    const uint16_t port = 53198;

    // Connect: the plug-in sends its name twice and its index once.
    EXPECT(id.onName(T1, "T1") == Announce::Partial);
    EXPECT(id.onIndex(T1, 7) == Announce::Completed);
    pending.push_back(T1);
    EXPECT(id.onName(T1, "T1") == Announce::Unchanged);

    // The first datagram on the dedicated port binds it (the UDP side does this).
    connPort[T1] = port;
    portName[port] = clientName[T1];
    portIndex[port] = clientIndex[T1];
    EXPECT(portIndex[port] == 7);

    // Two tracks inserted above: the plug-in SETs its new index.
    EXPECT(id.onIndex(T1, 9) == Announce::Changed);
    EXPECT(clientIndex[T1] == 9);
    EXPECT(portIndex[port] == 9);      // at once, before the next datagram
    EXPECT(pending.size() == 1);       // no second entry for the timing fallback

    // The same value again changes nothing.
    EXPECT(id.onIndex(T1, 9) == Announce::Unchanged);

    // Renamed: the port's name follows, the index stays.
    EXPECT(id.onName(T1, "Tom 1") == Announce::Changed);
    EXPECT(portName[port] == "Tom 1");
    EXPECT(portIndex[port] == 9);

    // A move before the first datagram: only the connection knows it yet, and
    // the port picks it up when it binds.
    const int T2 = 41;
    EXPECT(id.onName(T2, "T2") == Announce::Partial);
    EXPECT(id.onIndex(T2, 8) == Announce::Completed);
    EXPECT(id.onIndex(T2, 10) == Announce::Changed);
    EXPECT(clientIndex[T2] == 10);
    EXPECT(portIndex.size() == 1);     // T2 has no port yet, nothing invented

    // The master reports -1 and stays -1.
    const int M = 42;
    id.onName(M, "HARDWARE OUTPUT");
    EXPECT(id.onIndex(M, -1) == Announce::Completed);
    EXPECT(id.onIndex(M, -1) == Announce::Unchanged);

    if (g_fail == 0) std::printf("ssl_track_identity: all passed\n");
    return g_fail == 0 ? 0 : 1;
}
