#pragma once
//
// ORC MoreMe: the headphone mixer in the browser (docs/orc-phones-plan.md).
//
// Every musician opens the page on a phone, picks a headphone (Phones 1-4) and
// mixes the sources into it. This file is the mixer's logic and nothing else:
// the JSON the page speaks, who holds which headphone, the mute that TotalMix
// does not have, and the PIN. No socket, no thread, no clock of its own, so all
// of it is tested (tests/test_orc_phones.cpp).
//
// ⛔ THE PROTOCOL IS STOERME'S, UNCHANGED. The page is stoerme's page
// (orc/moreme/moreme.html), and ~/Documents/dev/stoerme/bridge.js is the
// reference for every message and every rule below. A behaviour that differs
// from the bridge is a bug here, not a feature, unless the comment says why.
//
// Where the values come from: a second rme::Manager (orc/PhonesServer.cpp) on
// its own TotalMix remote. Its State is the mirror stoerme kept by hand, and its
// rounds (Manager::setRounds) say which channels the remote can see. sync()
// compares that State with what the phones were last told and sends only what
// changed: a /sendall round repeats every value, and repeating them to the
// phones would make every fader on every phone jitter every three seconds.
//
//   page ──hello/select/set/mute/solo/master──▶ message() ──▶ takeOsc() ──▶ TotalMix
//   page ◀──config/snapshot/update/master/...── takeOut() ◀── sync(State)
//

#include "RmeManager.h"
#include "RmeState.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace orc::phones {

// ── the PIN (stoerme src/pin.js, test/pin.test.js) ───────────────────────────
//
// ⛔ The page can be reached from the internet (behind a reverse proxy). Without
// a PIN anyone with the address could pull a musician's headphones up while
// they wear them. ORC makes one mandatory as soon as MoreMe is on (Frank
// 29.10.2026); the checker keeps stoerme's "no PIN, open" for the ported test.

constexpr int       kMaxFails = 5;                    // wrong PINs ...
constexpr long long kWindowMs = 10LL * 60 * 1000;     // ... within this window ...
constexpr long long kLockMs   = 10LL * 60 * 1000;     // ... lock that client out this long

// SHA-256 of both sides and a compare that takes the same time whatever
// matches, so the answer time says nothing about how many digits were right.
class PinChecker {
  public:
    explicit PinChecker(const std::string& pin = {});
    bool required() const { return required_; }
    bool ok(const std::string& given) const;

  private:
    bool          required_ = false;
    unsigned char want_[32] = {};
};

// One client's guessing record. Pure functions, as in pin.js.
struct FailRecord {
    int       fails       = 0;
    long long since       = 0;
    long long lockedUntil = 0;
};
long long  lockLeft(const FailRecord* rec, long long nowMs);
FailRecord afterFail(const FailRecord* rec, long long nowMs);

// Who is asking. Behind the reverse proxy every request comes from the proxy,
// which passes the real client in X-Real-IP; the header counts only from a
// peer inside a private network, from anywhere else it could be forged to
// dodge the limiter.
// ⇨ AND X-FORWARDED-FOR (Frank 30.10.2026): many proxies and tunnels send only
// that one, and without it every phone looked like the proxy, so one guesser
// locked every musician out. Same rule: only from a private peer. Its LAST
// entry is taken, the address the proxy itself saw; anything to the left the
// client could have written.
bool        isPrivate(const std::string& ip);
std::string clientKey(const std::string& peer, const std::string& xRealIp,
                      const std::string& xForwardedFor = {});

// ── TotalMix colour index to the page's swatch ───────────────────────────────
// stoerme's table (bridge.js TMFX_COLORS, marked "to calibrate" there): 1 is
// TotalMix' default and stays absent, so an uncoloured channel takes the
// positional palette.
std::string colourHex(int index);   // "" = no custom colour

// ── the mixer ────────────────────────────────────────────────────────────────

struct Osc {
    std::string address;
    float       value = 0.0f;
};

class Mixer {
  public:
    explicit Mixer(const std::string& pin = {});

    // A new PIN. Everybody inside is sent back to the PIN page: whoever was told
    // the old one is not told the new one by this.
    void setPin(const std::string& pin);

    // The version the page shows under its settings.
    void setVersion(const std::string& v) { version_ = v; }

    // The studio's own names for Phones 1-4 (ORC's MoreMe page, Frank
    // 30.10.2026). "" falls back to TotalMix' name of the output, then to
    // "Phones N". Everybody inside sees the new name at once.
    void setNames(const std::array<std::string, 4>& n) { names_ = n; pushConfig_(); }

    // A WebSocket opened. `key` is clientKey() of the connection.
    int  open(const std::string& key);
    // It closed. Its headphone is free at once.
    void close(int id);
    // One text message from the page.
    void message(int id, const std::string& json, long long nowMs);

    // The second remote's state, its last round and whether TotalMix has ever
    // answered. Called often (every few tens of ms); does nothing when nothing
    // the phones show has changed.
    void sync(const reasixty::rme::State& st, const reasixty::rme::Manager::Visible& vis,
              bool tmSeen);

    // What to send, and to whom.
    std::vector<std::pair<int, std::string>> takeOut();
    std::vector<Osc>                         takeOsc();
    // True once after a page selected a headphone: stoerme re-reads the
    // layout then, so a channel shown or hidden in TotalMix appears on
    // (re)select.
    bool takeRefresh();

    // For the menu: phones that are past the PIN, and headphones in use.
    int clients() const;
    int held() const;

    // Log lines (a wrong PIN, a lock-out), for orc.log.
    void setLog(std::function<void(const std::string&)> f) { log_ = std::move(f); }

  private:
    struct Client {
        std::string key;
        bool        authed = false;
        std::string mix;        // "phone<N>", "" = none
        std::string cfgJson;    // the last config this client was sent
    };
    // ⇨ MUTE THAT TOTALMIX DOES NOT HAVE. Global OSC has no mute per routing,
    // so the fader rides to -inf and the level the phones SHOW stays the one it
    // had (stoerme's slot.db). That level lives here while muted.
    struct MuteMemo {
        bool   muted = false;
        double db    = -300.0;
    };

    void        tryPin_(Client& c, const std::string& given, long long nowMs, int id);
    std::string configJson_(int forId) const;
    std::string snapshotJson_(int phone) const;
    std::string sourcesJson_() const;
    void        pushConfig_();
    void        resnapViewers_();
    void        broadcastBus_(int bus, const std::function<std::string(const std::string&)>& make);
    int         busOfPhone_(int phone) const;
    int         holderOf_(const std::string& mixId) const;
    double      shownDb_(int bus, const std::string& kind, int n) const;
    void        send_(int id, const std::string& json);
    void        osc_(const std::string& address, float v);

    PinChecker                         pin_;
    std::string                        version_ = "1";
    std::array<std::string, 4>         names_;
    std::map<int, Client>              clients_;   // by id = by arrival, as stoerme's Set
    int                                nextId_ = 1;
    std::map<std::string, FailRecord>  fails_;
    std::function<void(const std::string&)> log_;

    // What the phones were last told, in the remote's own shape: every value
    // this mixer sends is folded in at once (rme::localEcho), so sync() sees a
    // change only when TotalMix made one.
    reasixty::rme::State               st_;
    std::vector<int>                   visIn_, visPb_;
    std::uint64_t                      round_  = 0;
    bool                               tmSeen_ = false;
    std::map<std::string, MuteMemo>    mute_;      // "bus:in:3"
    std::string                        lastSources_;

    std::vector<std::pair<int, std::string>> outbox_;
    std::vector<Osc>                         oscQ_;
    bool                                     refresh_ = false;
};

} // namespace orc::phones
