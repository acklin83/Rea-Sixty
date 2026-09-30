#pragma once
//
// SslTrackIdentity — which REAPER track an SSL plug-in instance sits on.
//
// Each plug-in SETs its HostTrackName and HostTrackIndex (type 18) on its own
// control connection. The UDP meter stream carries no track id; it is tied to
// the connection through the dedicated port, and everything track-keyed in the
// impersonator (gate GR on all three surfaces, the UF1 EQ curve and meter
// instance, the HQ and A/B switch presses) looks the track up by that port.
//
// ⇨ THE PLUG-INS RE-SEND WHEN THE TRACK MOVES (Frank 30.09.2026: two tracks
// inserted above, the gate GR of T1..T4 showed two tracks further up, and a
// reload put it right). The raw log of 27.09.2026 has the burst: some 70
// instances, no new connection, each SETting its own new HostTrackIndex at the
// same moment, all different. The first capture (87dd3a9, 22.07.2026) took the
// pair ONCE per connection, because then a re-send would have re-queued it for
// the timing correlation and handed the next new port a stale track. The
// dedicated ports made that queue a fallback, so now:
//   - the FIRST complete pair is queued once, as before (Completed);
//   - a later change updates the connection AND the port it is bound to,
//     at once, so a switch press is right before the next datagram (Changed);
//   - a re-send of the same value does nothing (Unchanged).
//
// Pure and header-only: the maps are the impersonator's own, passed in. The
// caller holds g_meterMx (the port maps are read by the paint thread).
//
#include <cstdint>
#include <map>
#include <set>
#include <string>

namespace sslcore::id {

enum class Announce { Partial, Completed, Changed, Unchanged };

template <class Conn>
struct TrackIdentity {
    std::set<Conn>&                   named;        // completed their first pair
    std::map<Conn, std::string>&      clientName;
    std::map<Conn, int>&              clientIndex;
    const std::map<Conn, uint16_t>&   connPort;     // bound on the first datagram
    std::map<uint16_t, std::string>&  portName;
    std::map<uint16_t, int>&          portIndex;

    Announce onName(Conn c, const std::string& name) { return set_(c, &name, nullptr); }
    Announce onIndex(Conn c, int index) { return set_(c, nullptr, &index); }

private:
    Announce set_(Conn c, const std::string* name, const int* index)
    {
        if (named.count(c) == 0) {
            if (name)  clientName[c]  = *name;
            if (index) clientIndex[c] = *index;
            if (clientName.count(c) == 0 || clientIndex.count(c) == 0) return Announce::Partial;
            named.insert(c);
            return Announce::Completed;
        }
        const bool same = name ? clientName[c] == *name : clientIndex[c] == *index;
        if (same) return Announce::Unchanged;
        if (name)  clientName[c]  = *name;
        if (index) clientIndex[c] = *index;
        if (auto p = connPort.find(c); p != connPort.end()) {
            if (name)  portName[p->second]  = *name;
            if (index) portIndex[p->second] = *index;
        }
        return Announce::Changed;
    }
};

}  // namespace sslcore::id
