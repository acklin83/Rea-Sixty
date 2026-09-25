#pragma once
//
// RmeManager — the TotalMix FX Global OSC link, running inside REAPER.
//
// One UDP socket on its own worker thread, the state cache from RmeState, and a
// send queue. No REAPER API anywhere in here, on purpose: the standalone ORC is
// meant to carry the same link without REAPER, and the file this reads and
// writes (rme.json) is meant to be the same file.
//
// ⇨ FOUR THINGS THAT WERE MEASURED, NOT ASSUMED (2026-09-21, Frank's rig):
//
//  · Ports. TotalMix has four OSC remotes. Remote 1 is TotalReaper (7001/7002),
//    Remote 2 is stoerme (7003/7004), so Rea-Sixty takes Remote 3: we SEND to
//    7005, TotalMix answers on 7006. Remote 3 answers /sendall exactly like 1.
//  · ⛔ TotalMix does NOT echo a change back to the remote that sent it. Only the
//    other remotes hear it. So every value we send is folded into our own state
//    here, the moment it is queued. Waiting for the echo would leave the fader
//    and the readout on the old value for good.
//  · A port someone else holds is a STATE, not an error: stoerme runs on the
//    same machine and holds 7004, and a second REAPER would hold 7006.
//  · TotalMix reports dB and accepts faderlin; the curve between the two is in
//    RmeState.h.
//
// Threading: the worker owns the socket. The main thread reads snapshots and
// queues sends; both go through one mutex. Nothing here blocks the main thread
// for longer than a copy of the state.
//
// ⛔ No select() (FD_SETSIZE killed REAPER once, see SslCoreImpersonator) and no
// SO_REUSEADDR (on Windows it lets a second socket steal a bound port).

#include "RmeState.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace reasixty::rme {

enum class LinkState {
    Off,        // switched off in Settings
    PortBusy,   // the receive port is held by another program
    Waiting,    // socket up, asked /sendall, nothing back yet
    Online,     // TotalMix answers
    Silent,     // it answered once and stopped (TotalMix quit, remote switched off)
};

// One V-Pot of the UF1 side-car. `target` is a role or a fixed channel (see
// RmeUf1.h), `turn` what rotating does, `push` what pressing does.
struct VpotSlot {
    std::string target = "phones1";
    std::string turn   = "volume";    // "volume" | "none"
    // "submix" (default since 21.09.: this output becomes the submix the
    // inputs and playbacks on the fader write into; the fader stays put),
    // "select" (this channel onto the fader), "mute", "none".
    std::string push   = "submix";
};

// One page of the channel view (STRIP): four pots and four soft keys, each a
// parameter id from RmeStrip.cpp's catalogue, "" for nothing. `rows` limits the
// page to input / playback / output strips ("in,pb,out"; empty = all). Which of
// these a channel actually SHOWS is TotalMix' answer: a page appears only when
// the channel reported at least one of its parameters (RmeStrip.h).
struct StripPage {
    std::string name;
    std::string rows;
    std::string pots[4];
    std::string keys[4];
};
// The factory pages, docs/rme-strip-and-uf8-plan.md 3.2 (Frank 21.09.).
std::vector<StripPage> defaultStripPages();

struct Config {
    bool        enabled  = false;
    std::string host     = "127.0.0.1";
    int         sendPort = 7005;   // TotalMix listens here (its "Port incoming")
    int         recvPort = 7006;   // TotalMix answers here (its "Port outgoing")

    // ── the UF1 side-car, everything a user may move (Frank 21.09.: "Muss
    // ALLES customizeable sein, was wo erscheint"). Defaults: Phones 1-4 on the
    // four pots of bank 1, Main A/B on bank 2, Main on the jog.
    // Two banks of four, 5-8 switches (Frank 21.09.: the control room has at
    // most six outputs, Phones 1-4 and Main A/B). Fixed places: a role without
    // an output leaves its pot empty rather than letting the others move up,
    // because a snapshot can reassign roles under the hand. An empty target is
    // an empty pot.
    static constexpr int kVpotBanks = 2;
    static constexpr int kVpotSlots = 4 * kVpotBanks;
    VpotSlot    vpots[kVpotSlots] = { {"phones1"}, {"phones2"}, {"phones3"}, {"phones4"},
                                      {"main"},    {"mainB"},   {""},        {""} };
    std::string jogTarget  = "main";
    double      jogStepDb  = 0.5;
    double      vpotStepDb = 0.5;
    // TotalMix colour index -> UF1 palette index for the colour bar. The nine
    // names are in RmeUf1.h (colourName), read off TotalMix' own menu on
    // 2026-09-25: hidden, white, grey, orange, red, blue, green, yellow, pink.
    // The defaults below are the nearest entry in Palette.cpp for each, which
    // is exact for orange, red, blue and green and an approximation for the
    // rest, because the surface palette has no white, no grey and no yellow.
    int         colourMap[9] = { 0x00, 0x01, 0x0C, 0x08, 0x02, 0x04, 0x03, 0x07, 0x0B };

    // The channel view's pages. Edited in rme.json for now (Frank 21.09., "a":
    // no Settings UI until the layout has proven itself on the device).
    std::vector<StripPage> stripPages = defaultStripPages();

    // Host and ports only. A pot or a colour changing must not drop the link.
    bool sameConnection(const Config& o) const
    {
        return enabled == o.enabled && host == o.host
            && sendPort == o.sendPort && recvPort == o.recvPort;
    }
};

// rme.json. Unknown fields are not an error; a file that does not parse leaves
// `out` untouched and returns false.
std::string configToJson(const Config& c);
bool        configFromJson(const std::string& json, Config& out);

class Manager {
  public:
    Manager() = default;
    ~Manager();
    Manager(const Manager&)            = delete;
    Manager& operator=(const Manager&) = delete;

    void start();
    void stop();

    Config config() const;
    void   setConfig(const Config& c);
    // True once after every config change, so the main thread knows to write
    // rme.json. The worker never writes files.
    bool   takeConfigDirty() { return cfgDirty_.exchange(false); }

    LinkState   link() const { return link_.load(); }
    std::string status() const;

    // A copy of the whole cache. Cheap enough per tick (a few hundred strips),
    // and callers that only care about change compare revision() first.
    State         snapshot() const;
    std::uint64_t revision() const { return rev_.load(); }

    // Queue one float for TotalMix and fold it into our own state at once.
    // `faderlin` addresses are folded as the dB value TotalMix would report,
    // because that is the only form the state knows.
    void send(const std::string& address, float value);

    // Ask TotalMix for everything again.
    void refresh() { refreshReq_.store(true); }

    // The control-room switches, without copying the whole cache: surface LEDs
    // ask this every tick.
    struct ControlRoom {
        bool dim = false, mono = false, speakerB = false, talkback = false;
        int  mainOut = -1;
    };
    ControlRoom controlRoom() const;

    // The snapshot and layout keys, same reason: eight LEDs per tick must not
    // copy the whole cache.
    struct Scenes {
        SnapshotState snapshot[8] = {};
        int           lastLayout  = -1;
    };
    Scenes scenes() const;

  private:
    void workerLoop();
    void setStatus(LinkState st, const std::string& text);

    std::thread       worker_;
    std::atomic<bool> run_{false};

    mutable std::mutex mx_;
    Config             cfg_;
    std::uint32_t      cfgGen_ = 0;
    State              state_;
    std::string        status_ = "off";
    std::vector<std::pair<std::string, float>> queue_;

    std::atomic<LinkState>     link_{LinkState::Off};
    std::atomic<std::uint64_t> rev_{0};
    std::atomic<bool>          cfgDirty_{false};
    std::atomic<bool>          refreshReq_{false};
};

Manager& manager();

// The message TotalMix would send back for a value we sent, in the form
// ingest() understands, or an empty address when there is none. Exposed for the
// test: this is the whole "no echo" workaround, and it is pure.
Message localEcho(const std::string& address, float value);

}  // namespace reasixty::rme
