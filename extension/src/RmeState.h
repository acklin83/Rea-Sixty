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
#include <memory>
#include <string>

#include "RmeOsc.h"

namespace reasixty::rme {

// Which of the three sections a channel index belongs to. The three index
// spaces are independent: /input/0 and /playback/0 are different channels.
enum class Bus : std::uint8_t { Input, Playback, Output };

// The channel EQ (not Room EQ): three bands and a low cut, on inputs and
// outputs. Playbacks have none (dump 2026-09-21). Indices measured the same
// day: band1type 0 Bell, 1 Shelf, 2 HiPass, 3 LoPass; band3type 0 Bell, 1 Shelf,
// 2 LoPass, 3 HiPass — indices 2 and 3 are NOT shared, see RmeUf1.cpp (band 2 is
// always a bell); lowcut/slope 0..3 = 6/12/18/24 dB/oct.
struct ChannelEq {
    bool   seen   = false;   // TotalMix sent at least one EQ value for this strip
    bool   on     = false;   // eq/enable
    double freq[3] = { 80.0, 1000.0, 5000.0 };
    double gain[3] = { 0.0, 0.0, 0.0 };
    double q[3]    = { 1.0, 1.0, 1.0 };
    int    type1  = 1;       // band 1
    int    type3  = 1;       // band 3
    bool   lcOn   = false;   // lowcut/enable
    double lcFreq = 20.0;
    int    lcSlope = 1;
};

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
    // /<section>/<n>/stereo. A stereo strip's right half is index + 1; a mono
    // one's index + 1 is the NEXT channel (inputs 30 and 31 on Frank's rig).
    bool        stereo = false;
    ChannelEq   eq;
    // ⇨ EVERY NUMERIC LEAF THE STRIP HAS REPORTED, by its address below the
    // channel ("gain", "48v", "dynamics/compthres", "eq/band1gain" ...). The
    // channel view builds its pages from this: WHICH parameters a channel has
    // is TotalMix' answer, not a table per device (a MADI channel has a pad
    // when an RME preamp sits in front of it). The typed fields above stay:
    // they are what the fader, the graph and the role check read.
    // Right halves of stereo pairs (index + 1) keep their own map, which is
    // where Phase R and the right-hand gain live.
    //
    // ⛔ SHARED, NOT COPIED. Manager::snapshot() copies the whole State on every
    // paint tick, and all leaves together are ~3600 entries. So the map sits
    // behind a shared_ptr: a snapshot copies pointers, and ingest() clones only
    // the ONE channel it writes to, and only while a snapshot still holds it.
    using LeafMap = std::map<std::string, double>;
    std::shared_ptr<LeafMap> leaves;
    const double* leaf(const std::string& k) const
    {
        if (!leaves) return nullptr;
        const auto it = leaves->find(k);
        return it == leaves->end() ? nullptr : &it->second;
    }
    void setLeaf(const std::string& k, double v)
    {
        if (!leaves)                      leaves = std::make_shared<LeafMap>();
        else if (leaves.use_count() > 1)  leaves = std::make_shared<LeafMap>(*leaves);
        (*leaves)[k] = v;
    }
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
    // The layout last sent from here, 0..7, -1 = none yet. Ours, not TotalMix':
    // /layout/load has no answer (RmeState.cpp).
    int           lastLayout  = -1;

    std::map<int, Channel> inputs;
    std::map<int, Channel> playbacks;
    std::map<int, Channel> outputs;

    // ⇨ AN INPUT'S OR A PLAYBACK'S LEVEL IS ITS NODE IN A SUBMIX: /mix/in|pb/
    // <channel>/<submix output>/fader, in dB. /sendall reports these for exactly
    // the channels that are visible on this remote, into every visible output
    // (2026-09-21), which is the set the UF1 walks. Missing is not "off": use
    // mixFader(), which says whether TotalMix told us at all.
    std::map<std::uint64_t, double> mix;
    static std::uint64_t mixKey(Bus b, int ch, int sub)
    {
        return (static_cast<std::uint64_t>(b) << 40)
             | (static_cast<std::uint64_t>(static_cast<std::uint32_t>(ch)) << 20)
             | static_cast<std::uint64_t>(static_cast<std::uint32_t>(sub));
    }
    const double* mixFader(Bus b, int ch, int sub) const
    {
        const auto it = mix.find(mixKey(b, ch, sub));
        return it == mix.end() ? nullptr : &it->second;
    }
    // Solo of the same node, /mix/<in|pb>/<channel>/<submix>/solo. TotalMix has
    // no solo per strip and none on outputs; solo lives on the routing (TotalReaper
    // docs/osc-paths-discovered.md). Missing reads as off.
    std::map<std::uint64_t, bool> mixSolo;
    // Pan of the same node, /mix/<in|pb>/<channel>/<submix>/balpan, -1..+1.
    // An output's own pan is a strip leaf ("balpan").
    std::map<std::uint64_t, double> mixPan;
    bool mixSoloed(Bus b, int ch, int sub) const
    {
        const auto it = mixSolo.find(mixKey(b, ch, sub));
        return it != mixSolo.end() && it->second;
    }

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

// ── TotalMix' fader curve ────────────────────────────────────────────────────
// `faderlin` is TotalMix' own fader position, 0..1. It is accepted on outputs
// and on submix nodes, but TotalMix only ever REPORTS dB (`volume`, `fader`),
// so a motor fader that wants to stand where the TotalMix fader stands needs
// the curve in both directions. RME publishes it (CalcFaderDB / CalcFaderLin in
// the protocol sheet); 0 is off, 1.0 is +6 dB, the same for outputs, inputs
// and playbacks.
constexpr double kDbOff = -300.0;
double faderlinToDb(double x);
double dbToFaderlin(double db);

// Fold one decoded message into the state. Returns true when the address was
// one we know. An unknown address is not a failure: TotalMix sends thousands of
// them and the ones we ignore today are the ones we grow into.
bool ingest(State& st, const Message& m);

}  // namespace reasixty::rme
