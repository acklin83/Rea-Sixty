#include "SslCoreImpersonator.h"
#include "LogPath.h"

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
#include <cctype>     // std::isspace / std::toupper — squashModel_ (explicit:
                      // clang pulls it in transitively, GCC does not)
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cmath>       // std::isfinite — GCC/libstdc++ needs this explicitly
#include <array>
#include <vector>
#include <string>
#include <map>
#include <deque>
#include <set>
#include <algorithm>
#include <climits>
#include <utility>

namespace sslcore {
namespace {

// Diagnostic log — the impersonator is otherwise blind. Enabled by the env var
// REASIXTY_SSLCORE_TRACE; writes to /tmp/reaper_sslcore.log. Logs the lifecycle
// (bind, announce, plugin connect/disconnect) and a periodic summary of which
// meter DataTypes are arriving with sample values, so we can see end-to-end that
// the plugin is streaming to us.
bool  g_trace = false;
// Hard ceiling. The log grows at ~25 Hz × one line per live plug-in and had
// reached 823 MB unnoticed on Frank's machine (2026-08-10). At the cap it
// starts over rather than rotating: a live diagnosis reads the NEWEST lines,
// and a second file on disk is another thing to forget about.
constexpr long kTraceMaxBytes = 100L * 1024 * 1024;
void  slog(const char* fmt, ...) {
    if (!g_trace) return;
    const std::string path = uf8::logPath("reaper_sslcore.log");
    FILE* f = std::fopen(path.c_str(), "a");
    if (!f) return;
    va_list ap; va_start(ap, fmt);
    std::vfprintf(f, fmt, ap);
    va_end(ap);
    std::fputc('\n', f);
    const long size = std::ftell(f);       // append stream → this IS the size
    std::fclose(f);
    if (size > kTraceMaxBytes) {
        if (FILE* w = std::fopen(path.c_str(), "w")) {
            std::fprintf(w, "[trace restarted — hit the %ld MB cap]\n",
                         kTraceMaxBytes / (1024 * 1024));
            std::fclose(w);
        }
    }
}

// ---------------------------------------------------------------- shared state
std::atomic<bool>      g_running{false};
std::thread            g_worker;
std::atomic<bool>      g_connected{false};
std::atomic<long long> g_lastDataMs{0};
// Timestamp of the most recent NEW plugin TCP connection. A project load makes
// every SSL plug-in connect in a ~1/s burst, and each one selects its own track
// on connect → REAPER's selection "counts through" the channels. The extension
// watches this to freeze the selection during the burst. 0 = none yet.
std::atomic<long long> g_lastNewConnMs{0};
// Per-connection unique object id for the type-7 handshake frame. Real Core
// assigns a DISTINCT id to every plug-in (CAPTURED 2026-07-27, real SSL 360 on
// lo0: 12 plug-ins → 12 different type-7 payloads). Our old hardcoded 0x9abc
// went to every plug-in → all thought they were the SAME focused object and each
// selected its own track = the load countdown. A fresh id per connect matches Core.
std::atomic<uint32_t>  g_connIdCounter{0};

struct Slot {
    std::vector<float>   current, peak;
    std::vector<uint8_t> overload;      // f5 OverloadValues     — flashes, per channel
    std::vector<uint8_t> overloadHold;  // f6 OverloadInfHoldValues — latched, per channel
    bool have = false;
    long long lastMs = 0;   // wall-clock of the last COMPLETED store. A LEVEL slot
                            // not refreshed within kSlotStaleMs is stale — the
                            // plug-in stopped sending it (transport stop) and its
                            // value is frozen; the meter then reads silence so the
                            // bars/needle fall like SSL, not a frozen phantom.
    long long lastChangedMs = 0;  // wall-clock the VALUE last CHANGED. A track that
                            // played then STOPPED keeps streaming a frozen scale-floor
                            // (~-27 dBFS, moves with Digital Type). If this is stale
                            // while the transport is stopped, read as silence so the
                            // bars fall like SSL — and switching to an already-frozen
                            // instance blanks at once (no 1 s phantom flash).
    uint64_t seq = 0;   // bumped on every completed store — lets callers paint
                        // DATA-driven (once per plugin frame) instead of
                        // timer-driven, the way SSL does (cap89: 24.5 Hz, never
                        // a repeated/stale image between plugin frames)
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
    // A Meter PRO instance streams Loudness DataTypes (LoudMomentary=11 .. Histogram
    // =27); a plain Meter (and a channel strip) never does. Sticky once seen — it is
    // what tells the UF1 whether to offer the Loudness screen in the Screen-Selector
    // cycle (there is no PluginType on the wire; MEASURED absent, see the classify
    // note below). Whether the plug-in streams Loudness on ALL views or only while
    // the Loudness view is selected is verified on the Mac; if the latter, detection
    // would need the control-socket object announcement instead (flagged in the
    // loudness memory). The UDP-DataType route is what's wired here.
    bool      isPro  = false;
    long long lastMs = 0;
    // Last time this instance carried actual SIGNAL (any level meter above the
    // silence floor). With several Meter instances loaded, the silent ones
    // stream floor values just as diligently as the live one — getMeter must
    // prefer a LIVE instance, not the first in port order (2026-07-17: two
    // MeterPro instances, the UF1 was wired to the silent one all evening;
    // Frank called the instance question hours before the trace proved it).
    long long lastLiveMs = 0;
};
std::mutex                   g_meterMx;
std::map<uint16_t, Instance> g_inst;      // key = UDP source port = one plug-in
// The instance the meter view is currently reading. Held across calls so every
// DataType comes from the SAME plug-in and the choice cannot flap; see getMeter.
// 0 = none chosen yet. Guarded by g_meterMx.
uint16_t                     g_srcPort = 0;
// UF1 V-Pot1 instance PIN: -1 = auto (the liveness pick in getMeter); >= 0 pins
// the g_meterSel-th Meter instance in port order, overriding the auto-pick.
// Guarded by g_meterMx.
int                          g_meterSel = -1;
// AUTO-MODE follow (2026-07-29, Frank "der selektierten Spur folgen"): 1-based
// HostTrackIndex of REAPER's SELECTED track, set each paint by the display. When
// there is NO V-Pot1 pin (g_meterSel < 0), the read is steered to the Meter
// instance on THIS track; if it has no live Meter, the sticky first-live pick
// stands. 0 = none. Atomic — read under g_meterMx paths, written lock-free.
std::atomic<int>             g_autoTrackIdx{0};
// Transport is stopped/paused (set by the render each paint). Gates the frozen-at-
// stop blanking of the LEVEL meters so playback + live input-monitoring are never
// touched — only a value frozen WHILE stopped reads as silence.
std::atomic<bool>            g_transportStopped{false};

// Instance identity mapping for the V-Pot1 label + cycle order. Each plug-in
// announces, on its own control connection at connect, its HostTrackName AND
// HostTrackIndex — but the UDP datagram carries no track id, so we CORRELATE BY
// TIMING: once a client has announced BOTH, queue {name,index}; the next NEW UDP
// source port claims the front (g_portName / g_portIndex). The index is what
// orders the instances so V-Pot1 cycles in TRACK order, not arbitrary port
// order. Per-client temp state + the queue are touched only on the worker's
// select-loop thread; g_portName/g_portIndex are read by the label thread too,
// guarded by g_meterMx. See [[uf1-meter-instance-track-mapping]].
struct PendingInst { std::string name; int index; };
std::deque<PendingInst>          g_pending;        // announced, not yet tied to a port
std::set<socket_t>               g_namedClients;   // clients already queued
std::map<socket_t, std::string>  g_clientName;     // per-client, awaiting its index
std::map<socket_t, int>          g_clientIndex;    // per-client, awaiting its name
std::map<uint16_t, std::string>  g_portName;       // UDP port -> track name  (g_meterMx)
// ★ Which channel-strip MODEL each connection is — the only thing on the wire
// that tells two strips on ONE track apart (TRACED 2026-08-10, see the note on
// getChannelStripMeterForTrackModel). The plug-in declares its EQ-curve object
// under a model-prefixed name — "4KEEQCurveData" / "4KBEQCurveData" — in a
// type=16 frame at connect. Everything else it announces (SlotIndex, PluginIdent,
// UniqueId, SessionDataId) is IDENTICAL across instances, because a 4K E and a
// 4K B are the same plug-in binary with a different analogue type.
std::map<socket_t, std::string>  g_clientModel;    // per-client, e.g. "4KE"
std::map<uint16_t, std::string>  g_portModel;      // UDP port -> model (g_meterMx)

// ── Parameter fingerprint ────────────────────────────────────────────────────
// The model above separates a 4K E from a 4K B. TWO STRIPS OF THE SAME MODEL it
// cannot separate — and Frank's point stands: they ARE separable, or they could
// not draw different EQ curves. What separates them is their SETTINGS, and the
// plug-in streams those: every parameter arrives on the instance's own control
// connection as a real-unit double (type=18, pb `09 <double LE>`), addressed by
// an object id whose wire short-id was declared earlier in a type=16 frame.
//
// ★ Those short-ids are LITERALLY the ids in our own LinkSlot tables —
// "GateThreshold", "CompThreshold", "InputTrim" … (PluginMap.cpp) — which also
// carry the VST3 index per model. So the caller can read the same parameter off
// the candidate FX in REAPER and compare. That is the bridge; nothing has to be
// guessed or ordered.
//
// Only these are captured — enough to tell two strips apart in practice, few
// enough to cost nothing. Anything the user is likely to set differently.
// The list itself lives in the header (fingerprintIds) so PluginMap can read the
// same parameters out of REAPER without linking this file.
inline const char* const* fpIds_() {
    const char* const* p = nullptr; fingerprintIds(p); return p;
}
inline int fpCount_() {
    const char* const* p = nullptr; return fingerprintIds(p);
}
constexpr int kFpMax = 16;   // storage bound; the list is well under this

int fpIndexOf_(const std::string& id) {
    const char* const* ids = fpIds_();
    for (int i = 0, n = fpCount_(); i < n; ++i)
        if (id == ids[i]) return i;
    return -1;
}

struct FpSet {
    double val[kFpMax] = {};
    bool   have[kFpMax] = {};
};
// objId -> fingerprint slot, learned per connection from the type=16 declarations.
std::map<socket_t, std::map<uint64_t, int>> g_clientFpObj;
std::map<socket_t, uint16_t>                g_connPort;   // conn -> its UDP port (g_meterMx)
std::map<uint16_t, FpSet>                   g_portFp;     // UDP port -> values (g_meterMx)
std::map<uint16_t, int>          g_portIndex;      // UDP port -> HostTrackIndex (g_meterMx)
// Order in which UDP ports were first seen — the instance ORDER. REAPER builds
// an FX chain slot by slot, so the plug-ins connect to Core in chain order and
// the n-th SSL stream on a track is the n-th SSL plug-in on it. That is what
// lets a caller ask for the ACTIVE instance's meters instead of "any strip on
// this track" (see getChannelStripMeterForTrackInstance). Guarded by g_meterMx;
// stable for the life of a connection (MEASURED 2026-08-09: within one session
// the port set per track does not churn — 3 ports per track, start to finish).
std::map<uint16_t, uint64_t>     g_portSeq;        // UDP port -> first-seen seq
uint64_t                         g_portSeqNext = 0;

// Per-instance track identity — the fix for "GR follows the wrong channel".
// Each plug-in announces, on its OWN TCP control connection at connect, its
// HostTrackName + HostTrackIndex, but the UDP meter datagram carries no track
// id (see the Instance comment). So we CORRELATE BY TIMING: capture {name,index}
// per client, queue it once both are in, and the next NEW UDP source port claims
// the front of the queue → g_portName / g_portIndex. That lets a track-keyed
// getChannelStripMeter pick the FOCUSED track's channel strip instead of just
// the first one. g_client*/g_pending/g_namedClients are worker-thread-only
// (the select loop); g_portName/g_portIndex are read under g_meterMx.
// (declared above: g_pending / g_namedClients / g_clientName / g_clientIndex /
//  g_portName / g_portIndex — one mechanism, used both for V-Pot1 TRACK-order
//  instance cycling and for GR channel-follow.)

// Classify an instance from what it emits. ChannelStripMeterType only spans
// 0..6, so ANY DataType >= 7 can only be a Meter/MeterPro plug-in (Rta,
// Lissajous, Loudness…) — authoritative and sticky. Conversely a channel strip's
// CompGain/GateGain carry exactly ONE value where the Meter plug-in's
// TextPeak/TextRms carry two (L,R) — measured, and only used while still
// unclassified, so a late DataType>=7 still wins.
void classify_(Instance& in, int dataType, size_t nvals)
{
    // Any Loudness DataType marks a Meter PRO (plain Meter never streams these).
    if (dataType >= int(sslmeter::DataType::LoudMomentary)) in.isPro = true;
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
std::vector<uint8_t> openingSequence(int dataPort, uint32_t connId) {
    std::vector<uint8_t> out;
    auto add = [&](const std::vector<uint8_t>& f){ out.insert(out.end(), f.begin(), f.end()); };
    add(serverConfig(-1));
    // Opening handshake: BOTH ctrlFrame(6) + ctrlFrame(7) are required before the
    // plug-in streams meter UDP (proven: dropping them → 0 datagrams, dark gate LED).
    // ctrlFrame(6) is empty and identical for every plug-in — matches real Core
    // byte-for-byte (CAPTURED 2026-07-27, real SSL 360 on lo0).
    add(ctrlFrame(6, {}));
    // ctrlFrame(7) carries a per-connection UNIQUE object id (field 1 varint) and
    // its frame seq = that id. Real Core assigns a DISTINCT id to each plug-in (the
    // capture showed 12 plug-ins → 12 different type-7 payloads). Our old hardcoded
    // 0x9abc went to EVERY plug-in — that value was one focused channel's id from
    // the original capture, so every plug-in thought it was THE focused object and
    // each selected its own track in REAPER = the load-time countdown. A fresh
    // connId per connection matches Core and removes the collision. (Real Core also
    // sends type-7 AFTER the plug-in's type-4/5; we still send it in the opening —
    // if the countdown persists, reorder to respond post-type-4.)
    { std::vector<uint8_t> pb; putVarint(pb, (1<<3)|0); putVarint(pb, connId);
      add(ctrlFrame(7, pb, connId)); }
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
// REASIXTY_FORCE_VIEW: pin the meter view the plug-in computes, overriding whatever the
// UF1 screen asks for. Trace tool — drives view 3 (Loudness history, DataTypes 25/26)
// without a UF1 sitting on that screen. -1 = off (normal setView behaviour).
int g_forceView = -1;

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
    // Always bind — port 0 lets the OS assign an ephemeral port (used for the
    // per-connection dedicated data sockets; getsockname reads back the number).
    if (::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) { SC_CLOSE(s); return kInvalid; }
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
    // Clients we've already replied to. Real Core does NOT speak first: the
    // plug-in sends type-4/type-5 (its identity), THEN Core sends the opening.
    // We defer our opening until a client's first bytes arrive (see recv loop).
    std::set<socket_t>    greeted;
    // Dedicated UDP data socket per plug-in connection (like the real SSL Core:
    // 2026-07-27 capture shows 12 plug-ins → 12 DISTINCT assigned ports). Maps the
    // per-connection UDP dataFd → its TCP connection fd. A datagram's DEST socket
    // then identifies the instance authoritatively — no stream-order timing guess.
    std::map<socket_t, socket_t> dataFdConn;
    const std::vector<uint8_t> hb  = heartbeat();
    const std::vector<uint8_t> ann = announcement(actualTcp);
    double lastAnn = 0, lastHb = 0, lastSub = 0;
    auto secs = []{ return nowMs() / 1000.0; };
    uint8_t buf[65536];

    while (g_running.load()) {
        const double t = secs();
        // Announce fast (was 1.0s). The plug-ins connect on the announce, so a
        // 1 s cadence spaced their connects exactly 1 s apart → each selected its
        // track 1/s = the visible ~12 s load countdown. Real Core's plug-ins
        // connect ~40 ms apart. Announcing ~20/s compresses the connect burst so
        // the selection sweep (which we can't suppress at the surface layer — the
        // plug-in selects externally and REAPER paints it immediately) collapses
        // into a brief flash instead of a slow count. Just more loopback UDP;
        // does not touch metering or cause reconnects.
        if (annFd != kInvalid && t - lastAnn > 0.05) {
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
                clients.push_back(c);
                // REACTIVE handshake (2026-07-27, CAPTURED from real Core): do NOT
                // send the opening here. Real Core waits for the plug-in's type-4/
                // type-5 hello, then replies. Sending type-7 ("you are object X")
                // BEFORE the plug-in registers made every plug-in activate in the
                // default context and select its track = the load countdown. We
                // greet on the client's first bytes below instead.
                slog("[%.1f] plugin CONNECTED (fd=%d), awaiting hello", t, int(c));
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
                    const uint16_t sp = ntohs(from.sin_port);
                    // Correlate the stream to its track. The datagram carries no
                    // track id, but each plug-in was given a DEDICATED UDP port at
                    // greet, so the receiving socket `d` names the instance
                    // AUTHORITATIVELY (this is how the real Core does it). Timing on
                    // the shared ports is only a last-resort fallback.
                    if (auto dc = dataFdConn.find(d); dc != dataFdConn.end()) {
                        auto itN = g_clientName.find(dc->second);
                        auto itI = g_clientIndex.find(dc->second);
                        if (itN != g_clientName.end() && itI != g_clientIndex.end()) {
                            if (g_portName.find(sp) == g_portName.end())
                                slog("[corr] UDP src=%u on dedicated port -> track %d (%s)",
                                     unsigned(sp), itI->second, itN->second.c_str());
                            g_portName[sp]  = itN->second;   // authoritative; overrides any guess
                            g_portIndex[sp] = itI->second;
                            // …and the model, from the same connection, so the
                            // reader can pick the 4K E's gate off the 4K E.
                            if (auto itM = g_clientModel.find(dc->second);
                                itM != g_clientModel.end())
                                g_portModel[sp] = itM->second;
                            // Bind the connection to its port so the parameter
                            // frames arriving on the TCP side (worker thread,
                            // no port in hand) can be filed under this stream.
                            g_connPort[dc->second] = sp;
                        }
                    } else if (g_portName.find(sp) == g_portName.end() && !g_pending.empty()) {
                        // Shared/fallback port, not yet named — old timing correlation.
                        g_portName[sp]  = g_pending.front().name;
                        g_portIndex[sp] = g_pending.front().index;
                        slog("[corr] UDP src=%u -> track %d (%s) [timing fallback]", unsigned(sp),
                             g_pending.front().index, g_pending.front().name.c_str());
                        g_pending.pop_front();
                    }
                    if (g_portSeq.find(sp) == g_portSeq.end())
                        g_portSeq[sp] = g_portSeqNext++;   // instance order
                    Instance& inst = g_inst[sp];
                    inst.lastMs = nowMs();
                    // Signal detector for instance preference: any level-type
                    // meter (VuPpm..TextRms, 0..5) above -120 dBFS = live.
                    for (const auto& u : ups) {
                        if (u.dataType >= 0 && u.dataType <= 5)
                            for (float v : u.current)
                                if (std::isfinite(v) && v > -120.f) {
                                    inst.lastLiveMs = inst.lastMs;
                                    break;
                                }
                    }
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
                            // ★ DO NOT CLEAR on chunk 0 (2026-07-18). The chunks
                            // are ~20 kB UDP datagrams, i.e. IP-FRAGMENTED — lose
                            // one fragment and the whole chunk is gone. Clearing
                            // first meant a lost chunk published a BAND OF ZEROS
                            // through an otherwise good image: on the UF1 that is
                            // a stripe of the goniometer blinking out for one
                            // frame while the rest keeps fading. Frank: "die
                            // Punkte flackern UND werden langsam dunkler" — the
                            // fade was ours all along, the flicker was dropouts.
                            // Overwriting in place instead leaves a lost region
                            // showing the PREVIOUS frame — one frame stale beats
                            // one frame black, and at 25 Hz nobody can see it.
                            if (s.asm_.size() < at + u.current.size())
                                s.asm_.resize(at + u.current.size(), 0.f);
                            for (size_t q = 0; q < u.current.size(); ++q)
                                s.asm_[at + q] = u.current[q];
                            s.asmGot_ += u.current.size();
                            if (u.isTailChunk()) {
                                const size_t total = u.totalCount();
                                if (total && s.asm_.size() >= total) {
                                    s.asm_.resize(total);
                                    s.current = s.asm_;         // one complete array
                                    s.peak    = std::move(u.peak);
                                    s.have    = true;
                                    ++s.seq;
                                    completed = true;
                                    // Cells actually received for this frame vs
                                    // the array size — a short count IS a dropout.
                                    if (g_trace && s.asmGot_ < total)
                                        slog("[chunk] type=%d got %zu of %zu cells "
                                             "(dropped datagram)", u.dataType,
                                             s.asmGot_, total);
                                }
                                s.asmGot_ = 0;
                            }
                        } else {
                            if (s.current != u.current) s.lastChangedMs = nowMs();  // value moved
                            s.current = std::move(u.current); s.peak = std::move(u.peak);
                            s.have = true;
                            ++s.seq;
                            completed = true;
                        }
                        if (completed) s.lastMs = nowMs();   // freshness for stale-expiry
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
                                mf = std::fopen(uf8::logPath("reasixty_meter_dump.log").c_str(), "a");
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
                if (n == 0) {
                    SC_CLOSE(c);
                    greeted.erase(c);
                    g_namedClients.erase(c);
                    g_clientName.erase(c);
                    g_clientIndex.erase(c);
                    g_clientModel.erase(c);
                    g_clientFpObj.erase(c);
                    {
                        std::lock_guard<std::mutex> lk(g_meterMx);
                        if (auto it = g_connPort.find(c); it != g_connPort.end()) {
                            g_portFp.erase(it->second);   // stale settings help nobody
                            g_connPort.erase(it);
                        }
                    }
                    // Tear down this connection's dedicated UDP data socket.
                    for (auto it = dataFdConn.begin(); it != dataFdConn.end(); ++it) {
                        if (it->second != c) continue;
                        const socket_t ufd = it->first;
                        for (size_t k = 0; k < dataFds.size(); ++k)
                            if (dataFds[k] == ufd) { dataFds.erase(dataFds.begin() + long(k));
                                                     dataPorts.erase(dataPorts.begin() + long(k)); break; }
                        SC_CLOSE(ufd);
                        dataFdConn.erase(it);
                        break;
                    }
                    clients.erase(clients.begin() + long(i));
                    continue;
                }
                // First bytes from this client = the plug-in's type-4/type-5
                // hello. Reply with the opening NOW (reactive handshake, like real
                // Core) — this is what keeps the plug-in from selecting its track.
                if (n > 0 && greeted.find(c) == greeted.end()) {
                    const uint32_t connId = 0x20000000u + g_connIdCounter.fetch_add(1);
                    // Bind a DEDICATED UDP data port for THIS plug-in and advertise
                    // exactly it, like the real Core (12 plug-ins → 12 distinct ports,
                    // 2026-07-27 capture). The plug-in streams to it, so the receiving
                    // socket names the instance — no timing guess. Fall back to the
                    // shared dataPort only if the ephemeral bind fails.
                    uint16_t udpPort = dataPort;
                    socket_t udp = makeUdp(0, false);
                    if (udp != kInvalid) {
                        sockaddr_in ua{}; socklen_t ul = sizeof(ua);
                        getsockname(udp, reinterpret_cast<sockaddr*>(&ua), &ul);
                        udpPort = ntohs(ua.sin_port);
                        dataFds.push_back(udp);
                        dataPorts.push_back(udpPort);
                        dataFdConn[udp] = c;
                    }
                    auto seq = openingSequence(udpPort, connId);
                    ::send(c, reinterpret_cast<const char*>(seq.data()), int(seq.size()), 0);
                    auto sub = subscribeInitial();
                    ::send(c, reinterpret_cast<const char*>(sub.data()), int(sub.size()), 0);
                    greeted.insert(c);
                    g_connected.store(true);
                    g_lastNewConnMs.store(nowMs());
                    slog("[%.1f] greeted fd=%d connId=0x%08x (opening %zu B + subscribe %zu B), dedicated UDP port %u",
                         t, int(c), connId, seq.size(), sub.size(), unsigned(udpPort));
                }
                // The plug-in announces its HostTrackName + HostTrackIndex over
                // this control connection at connect. We used to bin these unread;
                // capture them so a UDP source port can be tied to a REAPER track
                // (the datagram itself carries no track id). TCP is a stream, so
                // accumulate per client and only parse whole frames. Frame layout
                // (matches subscribeInitial): efbc5100 | len32 | 12-B hdr |
                // paylen32 | type32 | payload(8-B objId + protobuf).
                if (n > 0) {
                    static std::map<socket_t, std::vector<uint8_t>> sAcc;
                    auto& acc = sAcc[c];
                    acc.insert(acc.end(), buf, buf + n);
                    size_t off = 0;
                    for (;;) {
                        if (acc.size() - off < 8) break;
                        // resync: the stream must start on the frame magic
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
                            const uint8_t* pay = body + 20;
                            const size_t   avail = (flen > 20) ? size_t(flen - 20) : 0;

                            // ── Instance NAME correlation ──────────────────────
                            // Each plug-in SETs (type 18) its own HostTrackName
                            // (obj f0630f41c7667f0c, pb 0a <len> <ascii>) over its
                            // connection at connect. Take it ONCE per client and
                            // queue it; the next new UDP source port claims it (the
                            // datagram carries no track id). Later re-sends of the
                            // shared object (on selection change) are ignored via
                            // g_namedClients so a name is only tied at connect.
                            // ── Instance MODEL correlation ─────────────────────
                            // A type=16 frame declares an object's wire name as
                            // pb field 2 (`12 <len> <ascii>`). The channel strip
                            // declares its EQ-curve object model-prefixed —
                            // "4KEEQCurveData", "4KBEQCurveData" — which is the
                            // ONLY per-instance discriminator on this protocol.
                            // Keep the prefix ("4KE"); first one wins, so a later
                            // re-declaration cannot flip a live connection.
                            if (ftype == 16 && avail > 8) {
                                for (size_t k = 8; k + 1 < avail; ++k) {
                                    if (pay[k] != 0x12) continue;
                                    const size_t sl = pay[k + 1];
                                    if (sl == 0 || k + 2 + sl > avail) break;
                                    const std::string nm(
                                        reinterpret_cast<const char*>(pay + k + 2), sl);
                                    const std::string suf = "EQCurveData";
                                    if (nm.size() > suf.size() &&
                                        nm.compare(nm.size() - suf.size(), suf.size(), suf) == 0) {
                                        if (g_clientModel.find(c) == g_clientModel.end()) {
                                            g_clientModel[c] = nm.substr(0, nm.size() - suf.size());
                                            slog("[corr] client model = %s",
                                                 g_clientModel[c].c_str());
                                        }
                                    } else if (const int fi = fpIndexOf_(nm); fi >= 0) {
                                        // Remember which object id carries this
                                        // parameter ON THIS CONNECTION, so its
                                        // value frames can be recognised below.
                                        uint64_t oid = 0;
                                        std::memcpy(&oid, pay, 8);
                                        g_clientFpObj[c][oid] = fi;
                                    }
                                    break;
                                }
                            }

                            // Fingerprint VALUE frames: `09 <8-byte double LE>`
                            // on an object this connection declared as one of
                            // kFpIds. Stored against the connection's UDP port so
                            // the reader can match a stream to the FX in REAPER
                            // that holds the same settings.
                            if (ftype == 18 && avail >= 17 && pay[8] == 0x09) {
                                if (auto itO = g_clientFpObj.find(c);
                                    itO != g_clientFpObj.end()) {
                                    uint64_t oid = 0;
                                    std::memcpy(&oid, pay, 8);
                                    if (auto itF = itO->second.find(oid);
                                        itF != itO->second.end()) {
                                        double v = 0;
                                        std::memcpy(&v, pay + 9, 8);
                                        std::lock_guard<std::mutex> lk(g_meterMx);
                                        if (auto itP = g_connPort.find(c);
                                            itP != g_connPort.end()) {
                                            FpSet& fs = g_portFp[itP->second];
                                            fs.val[itF->second]  = v;
                                            fs.have[itF->second] = true;
                                        }
                                    }
                                }
                            }

                            static const uint8_t kHostTrackNameObj[8] =
                                { 0xf0,0x63,0x0f,0x41,0xc7,0x66,0x7f,0x0c };
                            static const uint8_t kHostTrackIndexObj[8] =
                                { 0xcb,0x39,0xda,0x8c,0x9c,0x8c,0x43,0xee };
                            if (ftype == 18 && avail >= 10 &&
                                g_namedClients.find(c) == g_namedClients.end()) {
                                if (std::memcmp(pay, kHostTrackNameObj, 8) == 0 &&
                                    pay[8] == 0x0a) {                 // pb: 0a <len> <ascii>
                                    const size_t slen = pay[9];
                                    if (10 + slen <= avail && slen > 0)
                                        g_clientName[c].assign(
                                            reinterpret_cast<const char*>(pay + 10), slen);
                                } else if (std::memcmp(pay, kHostTrackIndexObj, 8) == 0 &&
                                           pay[8] == 0x08) {          // pb: 08 <varint>
                                    g_clientIndex[c] = int(pay[9] & 0x7f);  // 1-based track idx
                                }
                                // Queue once this client has announced BOTH; the
                                // port claim below ties them to the next new port.
                                auto itN = g_clientName.find(c);
                                auto itI = g_clientIndex.find(c);
                                if (itN != g_clientName.end() && itI != g_clientIndex.end()) {
                                    g_pending.push_back({ itN->second, itI->second });
                                    g_namedClients.insert(c);
                                    // KEEP g_clientName/g_clientIndex keyed by this
                                    // connection fd — the dedicated-port correlation
                                    // reads them by fd; they are no longer a
                                    // consume-once queue. Cleared on disconnect.
                                }
                            }

                            if (g_trace) {
                                char line[512]; int o = 0;
                                o += std::snprintf(line + o, sizeof(line) - o,
                                                   "[%.1f] PLUGIN->us type=%-3u paylen=%-4u", t,
                                                   unsigned(ftype), unsigned(paylen));
                                if (avail >= 8) {
                                    o += std::snprintf(line + o, sizeof(line) - o, " obj=");
                                    for (int k = 0; k < 8; ++k)
                                        o += std::snprintf(line + o, sizeof(line) - o, "%02x", pay[k]);
                                }
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
    if (const char* fv = std::getenv("REASIXTY_FORCE_VIEW")) {
        g_forceView = std::atoi(fv);
        g_view.store(g_forceView); g_viewDirty.store(true);
    }
    { std::lock_guard<std::mutex> lk(g_meterMx);
      g_inst.clear(); g_portSeq.clear(); g_portSeqNext = 0; }
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
    if (g_forceView >= 0) return;   // pinned by REASIXTY_FORCE_VIEW (trace tool)
    if (view < 0) view = 0;
    if (g_view.exchange(view) != view) g_viewDirty.store(true);
}

// Alive Meter instances — those that streamed within the last 3 s — in port
// order. A loaded Meter plug-in streams ~25 Hz even when silent, so a STALE
// entry (a plug-in that changed socket, was removed, or a port g_inst never
// evicted) has an old lastMs and drops out. Without this, a dead port still
// counted and the UF1 showed "1/3" for two real meters (Frank, 2026-07-22).
// Caller holds g_meterMx.
static std::vector<uint16_t> aliveMeterPorts_() {
    const long long cutoff = nowMs() - 3000;
    std::vector<uint16_t> out;
    for (const auto& kv : g_inst)
        if (kv.second.kind == Kind::Meter && kv.second.lastMs >= cutoff)
            out.push_back(kv.first);
    // Order by the plug-in's HostTrackIndex so V-Pot1 cycles in TRACK order, not
    // by the arbitrary UDP source-port number (that was "content right, ORDER
    // wrong"). Ports with no known index sort last, stable by port.
    std::sort(out.begin(), out.end(), [](uint16_t a, uint16_t b) {
        auto ia = g_portIndex.find(a), ib = g_portIndex.find(b);
        const int va = (ia != g_portIndex.end()) ? ia->second : INT_MAX;
        const int vb = (ib != g_portIndex.end()) ? ib->second : INT_MAX;
        return (va != vb) ? (va < vb) : (a < b);
    });
    return out;
}

// The user-pinned Meter instance (g_meterSel-th ALIVE Meter in port order), or
// nullptr when auto (-1) or the pin is out of range. Sets g_srcPort so the
// overload path reads the same instance. Caller MUST hold g_meterMx.
static Instance* pinnedMeterInstance_() {
    if (g_meterSel < 0) return nullptr;
    const auto ports = aliveMeterPorts_();
    if (g_meterSel >= int(ports.size())) return nullptr;   // pin out of range → auto
    g_srcPort = ports[g_meterSel];
    return &g_inst[ports[g_meterSel]];
}

int meterInstanceCount() {
    std::lock_guard<std::mutex> lk(g_meterMx);
    return int(aliveMeterPorts_().size());
}

int meterSelection() {
    std::lock_guard<std::mutex> lk(g_meterMx);
    return g_meterSel;
}

void cycleMeterSelection(int delta) {
    std::lock_guard<std::mutex> lk(g_meterMx);
    const int n = int(aliveMeterPorts_().size());
    if (n <= 1) { g_meterSel = -1; return; }   // nothing to choose between
    // States [auto(-1), 0, 1, .., n-1] map to indices 0..n; advance and wrap.
    int idx = (g_meterSel < 0) ? 0 : (g_meterSel + 1);
    idx = ((idx + delta) % (n + 1) + (n + 1)) % (n + 1);
    g_meterSel = (idx == 0) ? -1 : (idx - 1);
}

// AUTO-MODE source steering (g_autoTrackIdx). Caller MUST hold g_meterMx. When
// not pinned, point g_srcPort at the SELECTED track's live Meter instance so the
// whole meter view — data, V-Pot1 label, track resolution — follows the selection
// instead of sticking to whichever instance streamed signal first. If the selected
// track has no live Meter, g_srcPort is left untouched (the sticky first-live
// fallback stands), so an un-metered selection never blanks the view.
static void steerAutoPort_() {
    if (g_meterSel >= 0) return;                     // manual V-Pot1 pin wins
    const int want = g_autoTrackIdx.load();
    if (want <= 0) return;
    const long long cut = nowMs() - 2000;
    for (const auto& kv : g_inst) {
        if (kv.second.kind != Kind::Meter || kv.second.lastMs < cut) continue;
        auto pi = g_portIndex.find(kv.first);
        if (pi != g_portIndex.end() && pi->second == want) { g_srcPort = kv.first; return; }
    }
}

void setAutoTrackIndex(int trackIndex1) { g_autoTrackIdx.store(trackIndex1); }
void setTransportStopped(bool stopped)  { g_transportStopped.store(stopped); }

// Stale-slot expiry for the LEVEL meters (bars + needle). On transport STOP the
// plug-in stops sending BarPeak/BarRms/VuPpm and their last value freezes; the
// instance still "lives" via the goniometer idle cycle, so getMeter would keep
// handing out the frozen level ("phantom" bars/needle, Frank 2026-07-29). SSL
// falls to silence instead. A level slot not refreshed within kSlotStaleMs reads
// as absent so the view drops to the floor. Non-level types (readouts, loudness,
// RTA, goniometer) keep their own hold/idle behaviour.
static constexpr long long kSlotStaleMs = 400;
static constexpr long long kFrozenMs    = 300;   // value unchanged this long while
                                                 // stopped = a frozen scale floor
static bool isLevelMeter_(int dt) {
    return dt == int(sslmeter::DataType::VuPpm)
        || dt == int(sslmeter::DataType::BarPeak)
        || dt == int(sslmeter::DataType::BarRms);
}
static bool slotUsable_(const Slot& s, int dataType) {
    if (!s.have) return false;
    if (isLevelMeter_(dataType)) {
        const long long now = nowMs();
        if (s.lastMs < now - kSlotStaleMs) return false;          // plug-in stopped sending
        if (g_transportStopped.load() && s.lastChangedMs < now - kFrozenMs)
            return false;                                         // frozen at stop → silence
    }
    return true;
}

std::string currentMeterName() {
    std::lock_guard<std::mutex> lk(g_meterMx);
    steerAutoPort_();   // auto-mode: label follows the selected track
    uint16_t port = 0;
    if (g_meterSel >= 0) {
        const auto ports = aliveMeterPorts_();
        if (g_meterSel < int(ports.size())) port = ports[g_meterSel];
    } else {
        port = g_srcPort;                     // the auto-picked instance
        // On view entry the auto-pick has not run yet (g_srcPort stale/unset), so
        // the label would flash a bare "AUTO" until the first scroll. Fall back to
        // the first instance (Track order) so a name shows immediately instead.
        if (g_portName.find(port) == g_portName.end()) {
            const auto ports = aliveMeterPorts_();
            if (!ports.empty()) port = ports.front();
        }
    }
    auto it = g_portName.find(port);
    return (it != g_portName.end()) ? it->second : std::string();
}

// The 1-based HostTrackIndex of the instance currently being READ (the pin, or the
// auto-picked one) — mirrors currentMeterName() so the UF1 can route V-Pot2/3/4
// edits + their displayed values to the SAME instance V-Pot1 selected, instead of
// whatever track happens to be focused. 0 = unknown/none. Thread-safe.
int currentMeterTrackIndex() {
    std::lock_guard<std::mutex> lk(g_meterMx);
    steerAutoPort_();   // auto-mode: resolve to the selected track's instance
    uint16_t port = 0;
    if (g_meterSel >= 0) {
        const auto ports = aliveMeterPorts_();
        if (g_meterSel < int(ports.size())) port = ports[g_meterSel];
    } else {
        port = g_srcPort;
        if (g_portIndex.find(port) == g_portIndex.end()) {
            const auto ports = aliveMeterPorts_();
            if (!ports.empty()) port = ports.front();
        }
    }
    auto it = g_portIndex.find(port);
    return (it != g_portIndex.end()) ? it->second : 0;
}

bool meterProAvailable() {
    std::lock_guard<std::mutex> lk(g_meterMx);
    steerAutoPort_();   // auto-mode: follow the selected track
    // Prefer the instance the meter view is actually reading (pin, else the sticky
    // source, else the first alive Meter) — same resolution as currentMeterTrackIndex.
    uint16_t port = 0;
    if (g_meterSel >= 0) {
        const auto ports = aliveMeterPorts_();
        if (g_meterSel < int(ports.size())) port = ports[g_meterSel];
    } else {
        port = g_srcPort;
        if (!port) { const auto ports = aliveMeterPorts_(); if (!ports.empty()) port = ports.front(); }
    }
    auto it = g_inst.find(port);
    if (it != g_inst.end() && it->second.isPro) return true;
    // Fallback so the Loudness screen is offered even before the pin/sticky settles:
    // any Meter Pro instance that is still streaming.
    const long long cut = nowMs() - 2000;
    for (const auto& kv : g_inst)
        if (kv.second.isPro && kv.second.lastMs >= cut) return true;
    return false;
}

bool getMeter(int dataType, std::vector<float>& current, std::vector<float>& peak,
              uint64_t* seq) {
    if (dataType < 0 || dataType >= int(sslmeter::DataType::Count)) return false;
    std::lock_guard<std::mutex> lk(g_meterMx);
    // User pinned a specific instance (UF1 V-Pot1) — read ONLY that one, never
    // leak another instance's data into the pinned view.
    if (Instance* pin = pinnedMeterInstance_()) {
        const Slot& s = pin->meter[dataType];
        if (slotUsable_(s, dataType)) { current = s.current; peak = s.peak; if (seq) *seq = s.seq; return true; }
        return false;
    }
    steerAutoPort_();   // auto-mode: hold the SELECTED track's instance if it's live
    // Prefer a positively-identified Meter plug-in: a channel strip on the same
    // track publishes Output(2)/OutputRms(3) into the very indices the UF1 meter
    // view reads as BarPeak(2)/BarRms(3), so taking "whatever arrived last"
    // shows the strip's output instead of the Meter plug-in's bars.
    // Prefer a Meter instance that carried SIGNAL recently: silent instances
    // stream floor values just as diligently, and taking the first in port
    // order wired the UF1 to a silent one for a whole evening (2026-07-17).
    const long long now = nowMs();
    const long long liveCutoff = now - 2000;
    // ★ STICKY (2026-07-18). Choosing per call let the source FLIP between two
    // streaming instances, and it did so twice over: while both carry signal, a
    // level dipping under the live threshold for one hands the view to the
    // other, and on STOP the 2 s window lapses for everyone at once so the
    // second pass takes whatever comes first in port order. Both show up as the
    // goniometer jumping to a different cloud — "wenn ich stop drücke, dann
    // bleibt die wolke im plugin wie sie ist und fadet aus. unsere auf dem uf1
    // bewegt sich immer noch während dem ausblenden" (Frank). It also meant
    // different DataTypes could come from different plug-ins in the same frame,
    // so the bars and the image disagreed about what they were showing.
    //
    // Decide once, then hold it while it still streams AT ALL (signal or not).
    // Only a genuinely dead instance releases the choice.
    if (g_srcPort) {
        auto it = g_inst.find(g_srcPort);
        if (it == g_inst.end() || it->second.lastMs < now - 2000) {
            g_srcPort = 0;                      // stopped streaming — re-choose
        } else if (it->second.kind == Kind::Meter) {
            const Slot& s = it->second.meter[dataType];
            if (slotUsable_(s, dataType)) {
                current = s.current; peak = s.peak;
                if (seq) *seq = s.seq;
                return true;
            }
        }
    }
    const Instance* fallback = nullptr;
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto& kv : g_inst) {
            const Instance& in = kv.second;
            if (in.kind != Kind::Meter) {
                if (in.kind == Kind::Unknown && !fallback) fallback = &in;
                continue;
            }
            if (pass == 0 && in.lastLiveMs < liveCutoff) continue;
            const Slot& s = in.meter[dataType];
            if (slotUsable_(s, dataType)) {
                g_srcPort = kv.first;           // hold this one from now on
                current = s.current; peak = s.peak;
                if (seq) *seq = s.seq;
                return true;
            }
        }
    }
    // Nothing classified yet (single plug-in, first datagrams) — old behaviour.
    if (fallback) {
        const Slot& s = fallback->meter[dataType];
        if (slotUsable_(s, dataType)) {
            current = s.current; peak = s.peak;
            if (seq) *seq = s.seq;
            return true;
        }
    }
    return false;
}

bool getOverload(int dataType, std::vector<uint8_t>& ovl, std::vector<uint8_t>& ovlHold) {
    if (dataType < 0 || dataType >= int(sslmeter::DataType::Count)) return false;
    std::lock_guard<std::mutex> lk(g_meterMx);
    // Same pin as getMeter — the overload LEDs must come from the pinned instance.
    if (Instance* pin = pinnedMeterInstance_()) {
        const Slot& s = pin->meter[dataType];
        if (s.have) { ovl = s.overload; ovlHold = s.overloadHold; return true; }
        return false;
    }
    steerAutoPort_();   // auto-mode: follow the selected track (mirror getMeter)
    // Same STICKY instance as getMeter (see there) — the overload LEDs must
    // come from the plug-in whose meters are on screen, not from whichever one
    // happens to be first in port order this millisecond.
    const long long now = nowMs();
    const long long liveCutoff = now - 2000;
    if (g_srcPort) {
        auto it = g_inst.find(g_srcPort);
        if (it != g_inst.end() && it->second.lastMs >= now - 2000
            && it->second.kind == Kind::Meter) {
            const Slot& s = it->second.meter[dataType];
            if (s.have) { ovl = s.overload; ovlHold = s.overloadHold; return true; }
        }
    }
    const Instance* fallback = nullptr;
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto& kv : g_inst) {
            const Instance& in = kv.second;
            if (in.kind != Kind::Meter) {
                if (in.kind == Kind::Unknown && !fallback) fallback = &in;
                continue;
            }
            if (pass == 0 && in.lastLiveMs < liveCutoff) continue;
            const Slot& s = in.meter[dataType];
            if (s.have) { ovl = s.overload; ovlHold = s.overloadHold; return true; }
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

bool getChannelStripMeterForTrackIndex(int csType, int trackIndex,
                                       std::vector<float>& current) {
    if (csType < 0 || csType > int(ChannelStripMeter::MicPreSaturation)) return false;
    if (trackIndex <= 0) return false;
    std::lock_guard<std::mutex> lk(g_meterMx);
    // Reconnect churn (MEASURED in /tmp/reaper_sslcore.log 2026-07-27): over a
    // session the SAME track gets tied to 6-7 different UDP source ports as its
    // plug-in reconnects, and dead ports LINGER in g_inst frozen at their last
    // value (they're never pruned — the TCP close handler has no socket->port
    // map). Taking the FIRST match returned the lowest-numbered port, which was
    // usually a DEAD reconnect-orphan stuck at 0.0 → the gate LED stayed dark
    // though the LIVE port for that track was streaming real reduction. So every
    // candidate is tested for freshness INDIVIDUALLY and orphans drop out.
    //
    // ★ A track can carry SEVERAL live channel strips (MEASURED 2026-08-09 in the
    // same trace: Frank's project runs TWO strip instances on every one of its 17
    // tracks — e.g. src=60709 and src=64220 both correlate to track 5 on their own
    // dedicated ports, and they DISAGREE: 5[1]=-25.2 vs 5[1]=0.0, i.e. one strip's
    // gate is engaged and closed, the other's is off). Picking the FRESHEST of the
    // two made the winner alternate at ~25 Hz between -25.2 dB and 0 dB — that is
    // the gate-GR "shake" Frank saw on ALL THREE displays at once (UF8 gate row,
    // UC1 LED strip, UF1), because all three read through this one function. Comp
    // GR looked fine only because it comes from REAPER's GainReduction_dB, not
    // from here — the flicker was never about DataType 5 being the ambiguous index.
    //
    // Which of the strips is right is NOT this layer's guess to make: it is the
    // instance the user made active. Callers pass that instance's ordinal to
    // getChannelStripMeterForTrackInstance below; this un-keyed entry point keeps
    // the first instance on the track for callers that have no instance context.
    return getChannelStripMeterForTrackInstance(csType, trackIndex, 0, current);
}

bool getChannelStripMeterForTrackInstance(int csType, int trackIndex,
                                          int instanceOrdinal,
                                          std::vector<float>& current) {
    if (csType < 0 || csType > int(ChannelStripMeter::MicPreSaturation)) return false;
    if (trackIndex <= 0 || instanceOrdinal < 0) return false;
    std::lock_guard<std::mutex> lk(g_meterMx);
    // Every live channel strip announcing THIS track, in instance order (the
    // order their plug-ins connected = FX-chain order, see g_portSeq). The
    // caller's ordinal is the position of the ACTIVE instance among the track's
    // SSL plug-ins, so index N here is the stream of the plug-in the surfaces
    // are showing — never another strip's gate, and never a per-frame coin toss.
    // ★ The list must hold EVERY live strip on the track, NOT only the ones that
    // have this meter — otherwise the ordinal counts a different set than the
    // caller does. The caller's ordinal comes from sslCoreInstanceOrdinal(), the
    // position among the track's SSL plug-ins in FX-CHAIN order, and it counts
    // all of them. Filtering by `meter[csType].have` here used to drop the strips
    // that never streamed this type, so the survivors slid down into the free
    // slots: with a 4K E in slot 1 whose gate never emits and a 4K B in slot 2
    // whose gate does, `live` held only the 4K B and ordinal 0 — the 4K E —
    // returned the 4K B's gate. Every surface showed the second strip's gate
    // reduction on the first strip (Frank 2026-08-10, UC1 + UF1 + UF8 alike).
    // Comp GR was unaffected because both strips always stream CompGain, so the
    // filtered list happened to match the real one.
    const long long now = nowMs();
    std::vector<std::pair<uint64_t, const Instance*>> live;
    for (const auto& kv : g_inst) {
        if (kv.second.kind != Kind::ChannelStrip) continue;
        auto pi = g_portIndex.find(kv.first);
        if (pi == g_portIndex.end() || pi->second != trackIndex) continue;
        // Dead reconnect-orphans linger in g_inst frozen at their last value
        // (see above), so each candidate is tested for freshness on its own.
        if (now - kv.second.lastMs > 1000) continue;
        auto sq = g_portSeq.find(kv.first);
        live.emplace_back(sq == g_portSeq.end() ? UINT64_MAX : sq->second, &kv.second);
    }
    if (live.empty()) return false;
    std::sort(live.begin(), live.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    if (size_t(instanceOrdinal) >= live.size()) return false;  // dark, not wrong
    // …and only NOW ask whether that particular strip carries this meter. It may
    // not — a strip whose gate has never moved has no GateGain slot yet. Dark is
    // the correct answer there; borrowing a neighbour's value is what this whole
    // function exists to prevent.
    const Slot& s = live[size_t(instanceOrdinal)].second->meter[csType];
    if (!s.have || s.current.empty()) return false;
    current = s.current;
    return true;
}

// Squash a strip name to the wire's model spelling: "4K E" -> "4KE". Both sides
// are compared this way so REAPER's short name and SSL's object prefix meet.
static std::string squashModel_(const char* s)
{
    std::string o;
    for (; s && *s; ++s)
        if (!std::isspace(static_cast<unsigned char>(*s)))
            o += char(std::toupper(static_cast<unsigned char>(*s)));
    return o;
}

bool getChannelStripMeterForTrackModel(int csType, int trackIndex,
                                       const char* model, int instanceOrdinal,
                                       std::vector<float>& current) {
    // ★ Why this exists (Frank, HW 2026-08-10: "4K B hat Gate-GR, wird aber in
    // der 4K E Instanz angezeigt" — on UC1, UF1 and UF8 alike).
    //
    // The ordinal route below can only be right if the UDP ports of a track sort
    // into FX-CHAIN order. They sort by g_portSeq = the order the ports were
    // first seen, and the trace shows plug-ins reconnecting constantly (a track
    // collects dozens of ports over a session). One reconnect of the FIRST strip
    // puts it BEHIND the second, and from then on ordinal 0 reads the other
    // plug-in's gate. Comp GR never showed it because that reading does not come
    // through here at all — it is GainReduction_dB, read from REAPER.
    //
    // So match on identity, not order. Traced 2026-08-10: everything a strip
    // announces about itself — SlotIndex(1), PluginIdent(2048), UniqueId,
    // SessionDataId — is IDENTICAL for both instances, because a 4K E and a 4K B
    // ARE the same plug-in with a different analogue type. The one thing that
    // differs is the name it declares its EQ-curve object under:
    // "4KEEQCurveData" vs "4KBEQCurveData" (type=16 frame, at connect). That
    // prefix is captured per connection into g_portModel.
    //
    // Falls back to the ordinal when the model is unknown or both strips are the
    // same model — there the wire genuinely cannot tell them apart, and order is
    // all there is.
    if (csType < 0 || csType > int(ChannelStripMeter::MicPreSaturation)) return false;
    if (trackIndex <= 0) return false;
    const std::string want = squashModel_(model);
    if (!want.empty()) {
        std::lock_guard<std::mutex> lk(g_meterMx);
        const long long now = nowMs();
        const Slot* hit = nullptr;
        int matches = 0;
        for (const auto& kv : g_inst) {
            if (kv.second.kind != Kind::ChannelStrip) continue;
            auto pi = g_portIndex.find(kv.first);
            if (pi == g_portIndex.end() || pi->second != trackIndex) continue;
            if (now - kv.second.lastMs > 1000) continue;
            auto pm = g_portModel.find(kv.first);
            if (pm == g_portModel.end() || squashModel_(pm->second.c_str()) != want) continue;
            ++matches;
            const Slot& s = kv.second.meter[csType];
            if (s.have && !s.current.empty() && !hit) hit = &s;
        }
        // Exactly one strip of this model on the track → unambiguous, take it.
        // Two of the same model → ambiguous, drop through to the ordinal.
        if (matches == 1) {
            if (!hit) return false;      // it is the right strip; it has no such meter
            current = hit->current;
            return true;
        }
    }
    return getChannelStripMeterForTrackInstance(csType, trackIndex,
                                                instanceOrdinal, current);
}

bool getChannelStripMeterForTrackStrip(int csType, int trackIndex,
                                       const char* model,
                                       const StripParam* fp, int nfp,
                                       int instanceOrdinal,
                                       std::vector<float>& current) {
    // Identity by SETTINGS — the last rung, and the only one that separates two
    // strips of the SAME model. Frank, 2026-08-10: "zwei strips des selben
    // models sind sehr wohl trennbar, sonst hätten sie ja auch dieselbe
    // EQ-Graph." Exactly: what differs is what the user dialled in, and each
    // instance streams its own parameter values on its own connection.
    //
    // The caller passes the ACTIVE FX's values, read from REAPER through the
    // LinkSlot table whose ids are the same strings the wire uses. Score each
    // live stream on how many of them agree; a strict winner takes it. No
    // winner (nothing captured yet, or two strips genuinely set the same) →
    // fall through to the model, then to the ordinal. Every rung is a narrowing,
    // never a guess: an ambiguous answer defers instead of picking.
    if (csType < 0 || csType > int(ChannelStripMeter::MicPreSaturation)) return false;
    if (trackIndex <= 0) return false;
    if (fp && nfp > 0) {
        std::lock_guard<std::mutex> lk(g_meterMx);
        const long long now = nowMs();
        const Slot* best = nullptr;
        int bestScore = 0, bestCount = 0;
        for (const auto& kv : g_inst) {
            if (kv.second.kind != Kind::ChannelStrip) continue;
            auto pi = g_portIndex.find(kv.first);
            if (pi == g_portIndex.end() || pi->second != trackIndex) continue;
            if (now - kv.second.lastMs > 1000) continue;
            auto pf = g_portFp.find(kv.first);
            if (pf == g_portFp.end()) continue;
            int score = 0;
            for (int i = 0; i < nfp; ++i) {
                const int fi = fpIndexOf_(fp[i].id ? fp[i].id : "");
                if (fi < 0 || !pf->second.have[fi]) continue;
                // Relative tolerance: the wire carries dB, Hz and ratios in one
                // set, and REAPER's formatted value is rounded for display.
                const double a = pf->second.val[fi], b = fp[i].value;
                const double tol = 0.02 * std::max(1.0, std::max(std::fabs(a),
                                                                 std::fabs(b)));
                if (std::fabs(a - b) <= tol) ++score;
            }
            if (score > bestScore) { bestScore = score; best = &kv.second.meter[csType];
                                     bestCount = 1; }
            else if (score == bestScore && score > 0) ++bestCount;
        }
        if (bestScore > 0 && bestCount == 1 && best) {
            if (!best->have || best->current.empty()) return false;  // right strip, no such meter
            current = best->current;
            return true;
        }
    }
    return getChannelStripMeterForTrackModel(csType, trackIndex, model,
                                             instanceOrdinal, current);
}

long long msSinceLastData() {
    const long long last = g_lastDataMs.load();
    if (last == 0) return INT64_MAX;
    return nowMs() - last;
}

long long msSinceLastNewConn() {
    const long long last = g_lastNewConnMs.load();
    if (last == 0) return INT64_MAX;
    return nowMs() - last;
}

} // namespace sslcore
