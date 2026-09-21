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

struct Config {
    bool        enabled  = false;
    std::string host     = "127.0.0.1";
    int         sendPort = 7005;   // TotalMix listens here (its "Port incoming")
    int         recvPort = 7006;   // TotalMix answers here (its "Port outgoing")
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
