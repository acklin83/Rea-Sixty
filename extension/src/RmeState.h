#pragma once
//
// RmeState — what TotalMix has told us, kept as a cache and never as a config.
//
// ⛔ THE ROLES MOVE. "Which output is Phones 1" is not a setting the user types
// once: TotalMix stores mainout / mainoutb / phones1..4 / talkchannel PER
// SNAPSHOT (read out of Frank's own device file on 2026-09-19: Phones3Chan is
// 2, 78, 78, 2, 2, 2, 78, 78, 78 across the slots). Loading a snapshot moves
// them, and TotalMix says so over OSC. So this follows the wire and nothing
// here is ever written by hand.
//
// ⛔ AND A ROLE CAN POINT AT A CHANNEL THIS REMOTE CANNOT SEE. On Frank's rig
// phones3 = output 2, and output 2 is filtered out of OSC Remote 1 entirely:
// asked directly with /sendchan/output/2 it stays silent, while output 8
// answers with 73 addresses. That is a STATE, not an error — the surface leaves
// the cell empty and says why, rather than writing to a channel that does not
// exist for it.
//
// Pure: ingest() takes decoded messages and updates the struct. No sockets, no
// REAPER, no threads, so it is unit-testable (tests/test_rme_osc.cpp).
//

#include <cstdint>
#include <map>
#include <string>

#include "RmeOsc.h"

namespace reasixty::rme {

// Which of the three sections a channel index belongs to. The three index
// spaces are independent: /input/0 and /playback/0 are different channels.
enum class Bus : std::uint8_t { Input, Playback, Output };

struct Channel {
    std::string name;
    int         colour = -1;   // TotalMix palette INDEX, not RGB. 0 = hidden.
    // ⛔ TRUE ONLY ONCE THE CHANNEL HAS ANNOUNCED A NAME. Not "we heard from
    // it": parameters that exist per side (phase, delay, gain, room-EQ bands)
    // are addressed on the right half of a stereo pair by index + 1, so
    // "anything arrived" counts 94 outputs where the mixer shows 5. See the
    // note in ingest().
    bool        seen   = false;
    double      volume = 0.0;  // outputs: dB. -300 is -inf.
    bool        mute   = false;
};

// A snapshot slot's state as TotalMix reports it back on /snapshot/load/<n>.
enum class SnapshotState : std::uint8_t { Off = 0, Active = 2, Changed = 3 };

struct State {
    // ── control room, all of it a cache ──────────────────────────────────────
    int    mainOut      = -1;
    int    mainOutB     = -1;
    int    phones[4]    = { -1, -1, -1, -1 };
    int    talkChannel  = -1;
    int    cueChannel   = -1;
    int    extInChannel = -1;
    bool   dim          = false;
    bool   mono         = false;
    bool   speakerB     = false;
    bool   talkback     = false;
    bool   externalIn   = false;
    bool   muteFx       = false;
    bool   linkAB       = false;
    double dimReduction = 0.0;
    double recallVolume = 0.0;
    double extInGain    = 0.0;
    bool   globalMute   = false;
    bool   globalSolo   = false;

    SnapshotState snapshot[8] = {};

    std::map<int, Channel> inputs;
    std::map<int, Channel> playbacks;
    std::map<int, Channel> outputs;

    // Peak level in dB per channel, sent only when it changes and only when
    // "Send Peak Level" is enabled for this remote. Empty is the normal state
    // with that switch off, which is how Frank's rig stood on 2026-09-19.
    std::map<int, double> levelIn, levelPb, levelOut;

    // How many messages we have ever taken in. Zero after a /sendall is the
    // single most useful diagnostic there is: it means the remote is not "In
    // Use", or the ports are crossed, or TotalMix is not running.
    std::uint64_t ingested = 0;

    // The channel a role names, or nullptr when the role is unassigned (-1) or
    // names a channel this remote cannot see. Those two are different answers
    // and the caller is expected to tell them apart: roleAssigned() says whether
    // TotalMix named one at all.
    const Channel* outputForRole(int channelIndex) const;
    static bool    roleAssigned(int channelIndex) { return channelIndex >= 0; }

    // True when the role names a channel that this OSC remote never reports.
    bool roleHidden(int channelIndex) const
    {
        return roleAssigned(channelIndex) && outputForRole(channelIndex) == nullptr;
    }
};

// Fold one decoded message into the state. Returns true when the address was
// one we know. An unknown address is not a failure: TotalMix sends thousands of
// them and the ones we ignore today are the ones we grow into.
bool ingest(State& st, const Message& m);

}  // namespace reasixty::rme
