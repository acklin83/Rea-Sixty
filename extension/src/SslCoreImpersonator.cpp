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
  // WSAPoll wants Vista+; the SDK's sdkddkver.h already defaults _WIN32_WINNT to
  // the newest supported target, so it needs no define of our own here.
  using pollfd_t = WSAPOLLFD;
  #define SC_POLL WSAPoll
  inline bool scWouldBlock() {
      const int e = WSAGetLastError();
      return e == WSAEWOULDBLOCK || e == WSAEINTR;
  }
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netinet/tcp.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <errno.h>
  #include <poll.h>
  using socket_t = int;
  static constexpr socket_t kInvalid = -1;
  #define SC_CLOSE ::close
  using pollfd_t = struct pollfd;
  #define SC_POLL ::poll
  inline bool scWouldBlock() {
      return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
  }
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
// The selected instance's UDP port. Defined far below with the rest of the
// selection logic; declared here because the worker resolves a RESET command to
// the connection that owns that port. Caller must hold g_meterMx.
static uint16_t currentMeterPortLocked_();
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
// ── Object-declaration dump ────────────────────────────────────────────────
// The plug-in NAMES every object it uses: a type-16 frame carries the object's
// 8-byte id and its wire name as protobuf field 2. That is the whole reason a
// property can be found WITHOUT a USB capture — we are the Core, both ends of
// this TCP connection are ours. Turn the dump on, press the control in the
// plug-in's own GUI, and the name plus the value it writes are in the log.
// Separate from g_trace on purpose: the trace is a firehose, this is a list.
std::atomic<bool> g_objDump{false};

void objLog(const char* fmt, ...) {
    static std::mutex mx;
    std::lock_guard<std::mutex> lk(mx);
    FILE* f = std::fopen(uf8::logPath("reasixty_ssl_objects.log").c_str(), "a");
    if (!f) return;
    va_list ap; va_start(ap, fmt);
    std::vfprintf(f, fmt, ap);
    va_end(ap);
    std::fputc('\n', f);
    std::fclose(f);
}

// ⇨ THE THINGS THAT EXPLAIN A DEAD METER MUST NOT BE BEHIND THE TRACE SWITCH.
// Whether the worker came up, whether a port was already taken, and what each
// plug-in announced are not firehose material — they are three or four lines per
// session, and without them a surface that shows nothing looks identical to one
// that is working on the wrong stream. They went to reaper_sslcore.log through
// slog(), which writes nothing unless REASIXTY_SSLCORE_TRACE is on, so on a
// machine where the meter is broken there was no record at all. These go to the
// ordinary rea_sixty.log, where the rest of the session already is.
void slogAlways(const char* fmt, ...) {
    static std::mutex mx;
    std::lock_guard<std::mutex> lk(mx);
    FILE* f = std::fopen(uf8::logPath("rea_sixty.log").c_str(), "a");
    if (!f) return;
    std::fputs("[sslcore] ", f);
    va_list ap; va_start(ap, fmt);
    std::vfprintf(f, fmt, ap);
    va_end(ap);
    std::fputc('\n', f);
    std::fclose(f);
}

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
// ★ Values are collected against the CONNECTION first, not the port. The plug-in
// dumps its whole parameter set on TCP at connect, and the UDP stream — which is
// what gives the connection a port — starts only AFTER that. Writing straight to
// the port dropped the entire opening set, so the fingerprint stayed empty until
// the user happened to move a control (Frank 2026-08-10: "er machts richtig, aber
// erst wenn ich mit einem parameter kurz was mache"). The port copy is taken when
// the port appears, and kept in step afterwards.
std::map<socket_t, FpSet>                   g_clientFp;   // conn -> values   (g_meterMx)

// ── The same thing for the METER ─────────────────────────────────────────────
// Two Meters on one track are as indistinguishable on the wire as two strips of
// the same model were, and for the same reason: everything the datagram carries
// is shared. Their SETTINGS are not, and the Meter announces them on its own
// control connection exactly like the strip does — so this is the strip
// fingerprint again, over a different id list (meterFingerprintIds, header).
//
// Kept SEPARATE from the strip set rather than merged into one table: a Meter and
// a strip share no parameter, and one combined list would let a strip id score
// against a Meter stream. Same storage shape, same lifecycle.
inline const MeterParamId* mfIds_() {
    const MeterParamId* p = nullptr; meterFingerprintIds(p); return p;
}
inline int mfCount_() {
    const MeterParamId* p = nullptr; return meterFingerprintIds(p);
}
constexpr int kMeterFpMax = 16;   // storage bound; the list is well under this

int mfIndexOf_(const std::string& id) {
    const MeterParamId* ids = mfIds_();
    for (int i = 0, n = mfCount_(); i < n; ++i)
        if (id == ids[i].wireId) return i;
    return -1;
}

struct MeterFpSet {
    double val[kMeterFpMax] = {};
    bool   have[kMeterFpMax] = {};
};
std::map<socket_t, std::map<uint64_t, int>> g_clientMfObj;  // conn: objId -> slot
std::map<socket_t, MeterFpSet>              g_clientMf;   // conn -> values (g_meterMx)
std::map<uint16_t, MeterFpSet>              g_portMf;     // port -> values (g_meterMx)

// ── The plug-in's OWN EQ curve ───────────────────────────────────────────────
// PluginEQCurveDataValueEventArgs { repeated float m_dBValues = 1; } — the
// reconstructed schema (analysis/ssl360-protobuf) settles what the numbers are:
// dB, one per frequency point, exactly what the plug-in's own GUI plots. The
// companion Prepare message carries MinFrequencyHz / MaxFrequencyHz, so the axis
// is known too rather than assumed.
//
// This has been on the wire the whole time and was unusable for one reason: with
// several strips streaming you could not say which curve belonged to which FX.
// That is what the identification chain now answers, so the UF1 can draw SSL's
// curve instead of our parametric approximation of it.
struct EqCurve {
    std::vector<float> db;
    float fMin = 0.0f, fMax = 0.0f;   // 0 = not announced, caller assumes 20..20k
};
std::map<socket_t, uint64_t>  g_clientEqObj;   // conn -> its EQCurveData object id
std::map<socket_t, EqCurve>   g_clientEq;      // conn -> curve      (g_meterMx)
std::map<uint16_t, EqCurve>   g_portEq;        // UDP port -> curve  (g_meterMx)
std::map<socket_t, uint16_t>                g_connPort;   // conn -> its UDP port (g_meterMx)
std::map<uint16_t, FpSet>                   g_portFp;     // UDP port -> values (g_meterMx)
std::map<uint16_t, int>          g_portIndex;      // UDP port -> HostTrackIndex (g_meterMx)

// ── The plug-in's OWN preset list and selection (2026-08-20) ───────────────
// Both are properties the plug-in ANNOUNCES, and keeps announcing: PresetList
// is an XML document of everything it can load, PresetSelection is the full path
// of what is loaded right now. That makes the disk scan and the chunk read the
// surface used before into detours — this is the plug-in's own answer, in its
// own order, including user presets wherever they live, on any platform.
// Per connection while it is being learned, mirrored per UDP port so the meter
// selection can address it like every other per-instance fact.
std::map<socket_t, std::string>  g_clientPresetList, g_clientPresetSel;
std::map<uint16_t, std::string>  g_portPresetList,   g_portPresetSel;
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
// Momentary RESET commands for the SELECTED instance. Bit 0 = peak holds and
// overloads, bit 1 = the loudness measurements. Raised from the surface thread,
// drained by the worker (which owns the sockets).
std::atomic<int>  g_resetReq{0};

// Outbound property writes with a payload (the reset commands need none, a
// preset path does). Queued from the surface thread, drained by the worker,
// which owns the sockets. port 0 = whichever instance the meter view has
// selected.
std::mutex g_cmdMx;
std::vector<std::pair<uint16_t, std::vector<uint8_t>>> g_cmdQueue;
std::atomic<bool> g_viewDirty{false};
// REASIXTY_FORCE_VIEW: pin the meter view the plug-in computes, overriding whatever the
// UF1 screen asks for. Trace tool — drives view 3 (Loudness history, DataTypes 25/26)
// without a UF1 sitting on that screen. -1 = off (normal setView behaviour).
int g_forceView = -1;

// Lissajous geometry dump (REASIXTY_T10_DUMP). Separate from the trace flag: it
// writes every frame at ~25 Hz, which is the point — see the dump site.
bool g_t10Dump = false;

// Analogue-needle probe (REASIXTY_NDL_PROBE). One line per CHANGE of VuPpm /
// TextVuPpm on the instance being read — see the probe site for what it settles.
// It writes through slog, so it turns the trace on with it.
bool g_ndlProbe = false;

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

// A type-18 SET-PROPERTY on any object. Same framing as viewFrame above, with
// both length fields computed rather than baked: verified by rebuilding the
// captured view frame from it byte for byte.
std::vector<uint8_t> propFrame(const uint8_t obj[8], const std::vector<uint8_t>& val)
{
    std::vector<uint8_t> f = { 0xef, 0xbc, 0x51, 0x00 };
    auto put32 = [&](uint32_t v) {
        f.push_back(uint8_t( v        & 0xff)); f.push_back(uint8_t((v >> 8)  & 0xff));
        f.push_back(uint8_t((v >> 16) & 0xff)); f.push_back(uint8_t((v >> 24) & 0xff));
    };
    put32(uint32_t(28 + val.size()));
    put32(0x10);
    put32(1);
    f.push_back(0x28); f.push_back(0xe0); f.push_back(0xc7); f.push_back(0x45);
    put32(uint32_t(12 + val.size()));
    put32(18);                                   // SET-PROPERTY
    f.insert(f.end(), obj, obj + 8);
    f.insert(f.end(), val.begin(), val.end());
    return f;
}

// The two RESET commands, straight out of the plug-in's own declarations
// (rea_sixty/sslcore_obj_dump, 2026-08-20):
//   756491f36a84ec3d  GlobalResetPeakHoldsAndOverloads
//   fd868ce4ddef814f  ResetLoudnessMeasurements
// Neither is a host parameter — they are nowhere in the 57-parameter dump — so
// this is the only way to reach them. They announce themselves as `08 01 12 03
// "OFF"`, i.e. an enum at index 1 with its label, the same shape LoudnessPlay
// uses (`08 02 12 04 "Play"`). So: index 2 to press, index 1 to release, and
// only the VALUE is sent — the label is the plug-in's own annotation, exactly as
// the view property takes a bare double.
static const uint8_t kObjResetPeakHolds[8] =
    { 0x75, 0x64, 0x91, 0xf3, 0x6a, 0x84, 0xec, 0x3d };
static const uint8_t kObjResetLoudness[8] =
    { 0xfd, 0x86, 0x8c, 0xe4, 0xdd, 0xef, 0x81, 0x4f };

// The plug-in's own preset library. Same shape on every SSL plug-in (the ids are
// name hashes), which is why the channel strips get this for free.
//   5b8dee912da184cf  PresetList       an XML document of everything it can load
//   c1b39cd2945eb9be  PresetSelection  the full path of what is loaded now
static const uint8_t kObjPresetList[8] =
    { 0x5b, 0x8d, 0xee, 0x91, 0x2d, 0xa1, 0x84, 0xcf };
static const uint8_t kObjPresetSel[8] =
    { 0xc1, 0xb3, 0x9c, 0xd2, 0x94, 0x5e, 0xb9, 0xbe };

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

// Every write to a plug-in goes through here, never through a bare ::send(). A
// write into a socket whose peer is already gone raises SIGPIPE, whose default
// disposition kills the PROCESS — and the process is REAPER, not us. macOS/BSD
// suppress it per socket (SO_NOSIGPIPE, set at accept); Linux only offers it per
// call, which is what this wrapper is for.
int sendTo(socket_t s, const std::vector<uint8_t>& b) {
#if defined(MSG_NOSIGNAL)
    const int flags = MSG_NOSIGNAL;
#else
    const int flags = 0;
#endif
    return int(::send(s, reinterpret_cast<const char*>(b.data()), int(b.size()), flags));
}

bool setNonBlocking(socket_t s) {
#if defined(_WIN32)
    u_long m = 1; return ioctlsocket(s, FIONBIO, &m) == 0;
#else
    int fl = fcntl(s, F_GETFL, 0); return fl >= 0 && fcntl(s, F_SETFL, fl | O_NONBLOCK) == 0;
#endif
}

// The last socket error, as a number, for the log. errno and WSAGetLastError are
// different wells and the wrong one reads as 0 = "no error" on the other platform.
int sockErr() {
#if defined(_WIN32)
    return WSAGetLastError();
#else
    return errno;
#endif
}

// ⛔ SO_REUSEADDR MEANS SOMETHING ELSE ON WINDOWS, AND IT IS NOT WHAT THIS FILE
// WANTS. On macOS/Linux it only lets a listener past a TIME_WAIT leftover: a
// second bind next to a LIVE listener still fails with EADDRINUSE, which is
// exactly the signal the caller below reports as "in use? 360 running?".
// On Windows the same flag "allows a socket to forcibly bind to a port in use by
// another socket", and then, in Microsoft's own words, "the behavior for all
// sockets bound to that port is indeterminate […] incoming TCP connection
// requests over the port cannot be guaranteed to be handled by the correct
// socket" — the second bind "will 'hijack' the port and the application will be
// unable to determine which of the two sockets received specific packets"
// (learn.microsoft.com, "Using SO_REUSEADDR and SO_EXCLUSIVEADDRUSE", read
// 2026-08-28). Same user account, so nothing stops it.
//
// ⇨ So with a real SSL 360 Core (or a second REAPER) already holding these
// ports, Windows does NOT report the conflict the way every other platform
// does. We bind alongside it and the plug-in connections and meter datagrams
// split between the two receivers at random — some instances stream to us, some
// to the other process. That is a per-instance, per-restart failure, which is
// what "auf Windows stimmt beim Meter Mode gar nichts" looks like from outside.
//
// ⇨ On Windows the fix is to set NOTHING. Windows' DEFAULT bind is already the
// behaviour this code was written against: a port someone else is actively bound
// to comes back WSAEADDRINUSE, and TIME_WAIT leftovers from our own previous run
// do not block a fresh LISTEN, so a REAPER restart is unaffected.
// ⛔ NOT SO_EXCLUSIVEADDRUSE, tempting as Microsoft's advice is — it carries its
// own trap on exactly our path: "if a listening socket with SO_EXCLUSIVEADDRUSE
// set accepts a connection and is then subsequently closed, another socket (also
// with SO_EXCLUSIVEADDRUSE) cannot bind to the same port … until the original
// connection becomes inactive". We accept a connection per plug-in and close on
// every reload, so that would have traded a silent hijack for a minutes-long
// dead port after each restart. Its only extra benefit is keeping a HOSTILE
// process off the port, which is not the problem here.
bool setPortExclusive(socket_t s) {
#if defined(_WIN32)
    (void)s;
    return true;      // default bind = fail-on-conflict, which is what we want
#else
    int y = 1;
    return setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
                      reinterpret_cast<char*>(&y), sizeof(y)) == 0;
#endif
}

socket_t makeUdp(uint16_t port, bool reuse) {
    socket_t s = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s == kInvalid) return kInvalid;
    // `reuse` only ever means the FIXED data ports; the ephemeral ones (port 0)
    // never ask for it, and must not — see setPortExclusive.
    if (reuse) setPortExclusive(s);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    // Always bind — port 0 lets the OS assign an ephemeral port (used for the
    // per-connection dedicated data sockets; getsockname reads back the number).
    if (::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
        if (port) slogAlways("[err] UDP bind :%u failed (err %d) — port in use? SSL 360 running?",
                             unsigned(port), sockErr());
        SC_CLOSE(s);
        return kInvalid;
    }
    setNonBlocking(s);
    return s;
}

// -------------------------------------------------------------------- worker
void workerMain(uint16_t tcpPort, uint16_t dataPort) {
    netInit();

    socket_t listenFd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenFd == kInvalid) { slogAlways("[err] TCP socket() failed (err %d)", sockErr());
                                g_running = false; return; }
    setPortExclusive(listenFd);
    sockaddr_in la{}; la.sin_family = AF_INET; la.sin_port = htons(tcpPort); la.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(listenFd, reinterpret_cast<sockaddr*>(&la), sizeof(la)) != 0 || ::listen(listenFd, 8) != 0) {
        slogAlways("[err] TCP bind/listen :%u failed (err %d) — port in use? "
                   "SSL 360 running, or a second REAPER? The impersonator is OFF, "
                   "so no meter, no strip data.", unsigned(tcpPort), sockErr());
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
        slogAlways("[err] no UDP data port could bind — port in use? SSL 360 running? "
                   "The impersonator is OFF, so no meter data at all.");
        SC_CLOSE(listenFd); g_running = false; return;
    }
    // A PARTIAL bind is the interesting case and used to be invisible: the meter
    // instance that streams to a port we did not get simply never appears, and
    // everything else looks normal. Name what we hold, so a missing port is one
    // grep away instead of a guess about the plug-in.
    { std::string ps; for (auto p : dataPorts) ps += " " + std::to_string(p);
      slogAlways("worker up: TCP :%u  UDP data:%s (%d of 6)  announcing on 16008/16009",
                 unsigned(actualTcp), ps.c_str(), int(dataFds.size())); }
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
    // Per-client TCP reassembly buffer. Function-scope, NOT the function-static it
    // used to be: a static outlives a stop/start of the impersonator and would hand
    // the next run a dead socket's half-frame, and nothing ever erased its entries.
    std::map<socket_t, std::vector<uint8_t>> acc_;
    // Runaway backstop ONLY — deliberately far above any real session, and NOT a
    // budget. Frank's mix on 2026-08-17 had 69 tracks each carrying a 4K E, so 69
    // SIMULTANEOUS connections is not pathological, it is Tuesday. This started
    // life at 64 on the mistaken reading that one plug-in had reconnected 70 times;
    // the log says otherwise (69 DISTINCT tracks, one stream each, every one
    // healthy — exactly what real Core sees: 12 plug-ins, 12 connections).
    // ⛔ A cap a real project can REACH is not protection, it is a new bug that
    // silently costs the user their last plug-ins. So: 2048 (Frank's call,
    // 2026-08-17). Nothing here scales with it — poll() has no ceiling, and if the
    // host's own descriptor limit runs out first, accept() simply fails and we skip
    // that connection instead of dying.
    constexpr size_t kMaxClients = 2048;
    bool capLogged = false;

    // Full teardown for ONE client, by index. This used to live inline in the
    // recv()==0 branch, so a connection that ended any OTHER way — reset, POLLERR,
    // anything that makes recv() return −1 — would have stayed in `clients` forever
    // with its socket open. Hardening, not the cause of the 2026-08-17 kill: there
    // the plug-in never closed anything, it simply kept opening MORE
    // ([[sslcore-fd-setsize-kills-reaper]]).
    auto dropClient = [&](size_t i) {
        const socket_t c = clients[i];
        SC_CLOSE(c);
        greeted.erase(c);
        g_namedClients.erase(c);
        g_clientName.erase(c);
        g_clientIndex.erase(c);
        g_clientModel.erase(c);
        g_clientFpObj.erase(c);
        g_clientMfObj.erase(c);
        g_clientPresetList.erase(c);
        g_clientPresetSel.erase(c);
        acc_.erase(c);
        {
            std::lock_guard<std::mutex> lk(g_meterMx);
            if (auto it = g_connPort.find(c); it != g_connPort.end()) {
                g_portFp.erase(it->second);   // stale settings help nobody
                g_portMf.erase(it->second);
                g_portEq.erase(it->second);
                g_connPort.erase(it);
            }
            g_clientFp.erase(c);
            g_clientMf.erase(c);
            g_clientEq.erase(c);
            g_clientEqObj.erase(c);
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
    };

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
        // …but announce fast only while plug-ins are still ARRIVING. The burst
        // cadence is what collapses the load countdown, so it is kept for the first
        // plug-in and re-armed by EVERY new connection (3 s of quiet ends it) — a
        // project full of strips still connects in one burst. Once nothing new has
        // arrived for 3 s we fall back to the pre-countdown 1 s, which is still
        // prompt for a plug-in added later in the session.
        // NB this is housekeeping, not a bug fix: it went in believing the plug-ins
        // were accepting the same invitation repeatedly, and the log disproved that
        // (69 tracks, 69 connections, no duplicates). It stands because 20 loopback
        // datagrams a second, forever, buys nothing once everyone is connected.
        const double annPeriod =
            (greeted.empty() || nowMs() - g_lastNewConnMs.load() < 3000) ? 0.05 : 1.0;
        if (annFd != kInvalid && t - lastAnn > annPeriod) {
            lastAnn = t;
            for (uint16_t dp : {16008, 16009}) {
                sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(dp); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                ::sendto(annFd, reinterpret_cast<const char*>(ann.data()), int(ann.size()), 0,
                         reinterpret_cast<sockaddr*>(&a), sizeof(a));
            }
        }
        if (!clients.empty() && t - lastHb > 0.25) {
            lastHb = t;
            for (socket_t c : clients) sendTo(c, hb);
        }
        // Re-subscribe every 5s (capture cadence ~6.6s) so the plugin keeps the
        // meter streams open instead of dropping the connection.
        if (!clients.empty() && t - lastSub > 5.0) {
            lastSub = t;
            const auto sub = subscribeRefresh();
            for (socket_t c : clients) sendTo(c, sub);
        }
        // A view change must not wait for the next 5 s refresh — the user has
        // already switched the UF1 screen and would stare at a dead element
        // until the plug-in starts computing that view's meters.
        if (const int rq = clients.empty() ? 0 : g_resetReq.exchange(0)) {
            // To THIS meter, not to every meter in the session: resolve the
            // selected instance's UDP port back to the connection that owns it.
            // No match means we cannot tell which one is meant, and then nothing
            // is sent — a reset on the wrong instance is worse than none.
            socket_t target = kInvalid;
            {
                std::lock_guard<std::mutex> lk(g_meterMx);
                const uint16_t port = currentMeterPortLocked_();
                for (const auto& kv : g_connPort)
                    if (kv.second == port) { target = kv.first; break; }
            }
            if (target != kInvalid) {
                auto press = [&](const uint8_t* obj) {
                    sendTo(target, propFrame(obj, { 0x08, 0x02 }));   // ON
                    sendTo(target, propFrame(obj, { 0x08, 0x01 }));   // back to OFF
                };
                if (rq & 1) press(kObjResetPeakHolds);
                if (rq & 2) press(kObjResetLoudness);
                if (g_trace) slog("[%.1f] reset req=%d -> conn", t, rq);
            }
        }
        if (!clients.empty()) {
            std::vector<std::pair<uint16_t, std::vector<uint8_t>>> q;
            { std::lock_guard<std::mutex> lk(g_cmdMx); q.swap(g_cmdQueue); }
            for (auto& item : q) {
                socket_t target = kInvalid;
                {
                    std::lock_guard<std::mutex> lk(g_meterMx);
                    const uint16_t port = item.first ? item.first
                                                     : currentMeterPortLocked_();
                    for (const auto& kv : g_connPort)
                        if (kv.second == port) { target = kv.first; break; }
                }
                if (target != kInvalid) sendTo(target, item.second);
                else if (g_trace) slog("[%.1f] cmd dropped: no conn for port %u",
                                       t, unsigned(item.first));
            }
        }
        if (!clients.empty() && g_viewDirty.exchange(false)) {
            const auto v = viewFrame(g_view.load());
            for (socket_t c : clients) sendTo(c, v);
            if (g_trace) slog("[%.1f] view -> %d", t, g_view.load());
        }

        // ⛔ poll(), NEVER select(). On macOS an fd_set is a fixed bitmap indexed by
        // the fd NUMBER, and FD_SET() with an fd >= FD_SETSIZE (1024) does not fail
        // or clamp — it calls __darwin_check_fd_set_overflow, which faults the whole
        // PROCESS via EXC_GUARD. That is what killed REAPER on 2026-08-17: no
        // dialog, no crash report from REAPER, audio still playing, window simply
        // gone, because the kernel shot the host and REAPER never got to run its own
        // handler. The fd number is process-wide, so this was never ours to bound —
        // whatever REAPER itself has open pushes ours up. poll() takes the fds as a
        // LIST and has no such ceiling. Windows select() has the mirror-image limit
        // (fd_set there is a count-capped array, FD_SETSIZE = 64, silently dropping
        // the rest), which WSAPoll avoids too. [[sslcore-fd-setsize-kills-reaper]]
        std::vector<pollfd_t> pfds;
        pfds.reserve(1 + dataFds.size() + clients.size());
        auto watch = [&](socket_t s) {
            pollfd_t p{}; p.fd = s; p.events = POLLIN; pfds.push_back(p);
        };
        watch(listenFd);
        for (socket_t d : dataFds) watch(d);
        for (socket_t c : clients) watch(c);
        if (SC_POLL(pfds.data(), static_cast<unsigned>(pfds.size()), 40) <= 0) continue;
        // POLLHUP / POLLERR / POLLNVAL count as readable on purpose: recv() then
        // returns 0 or −1 and dropClient reclaims the socket. Under select() those
        // ends were invisible, so they were never reclaimed at all.
        std::set<socket_t> readable;
        for (const auto& p : pfds)
            if (p.revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL))
                readable.insert(p.fd);

        if (readable.count(listenFd)) {
            socket_t c = ::accept(listenFd, nullptr, nullptr);
            if (c != kInvalid && clients.size() >= kMaxClients) {
                // Refuse rather than serve a runaway. poll() means a flood can no
                // longer kill the host, but every extra connection still costs a
                // heartbeat every 0.25 s and a re-subscribe every 5 s.
                SC_CLOSE(c);
                if (!capLogged) {
                    capLogged = true;
                    slog("[%.1f] ⛔ %zu clients — refusing further connections", t, clients.size());
                }
            } else if (c != kInvalid) {
                capLogged = false;
                setNonBlocking(c);
                // Never let a write into a dead socket signal the HOST. We heartbeat
                // every client every 0.25 s, so we write into peers that have just
                // gone away as a matter of course, and a write to a reset socket
                // raises SIGPIPE — default disposition: kill the process. REAPER
                // happens to ignore SIGPIPE, which is the only reason 150 s of
                // writing into 70 dead connections merely leaked instead of killing
                // it outright on 2026-08-17. Measured, not assumed: the same load
                // takes down a test binary that does not ignore it. Our own signal
                // disposition is not ours to set inside a host, so suppress at the
                // socket instead (Linux has no SO_NOSIGPIPE; see the send sites).
              #if defined(SO_NOSIGPIPE)
                int nosig = 1;
                setsockopt(c, SOL_SOCKET, SO_NOSIGPIPE,
                           reinterpret_cast<char*>(&nosig), sizeof(nosig));
              #endif
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
          if (!readable.count(d)) continue;
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
                            // Anything already learned on the connection follows it
                            // to the port (both arrive before the first datagram).
                            if (auto itL = g_clientPresetList.find(dc->second);
                                itL != g_clientPresetList.end()) g_portPresetList[sp] = itL->second;
                            if (auto itS = g_clientPresetSel.find(dc->second);
                                itS != g_clientPresetSel.end()) g_portPresetSel[sp] = itS->second;
                            // Flush whatever this connection already told us —
                            // that is the opening parameter dump, which arrived
                            // before any port existed to file it under.
                            if (auto itF = g_clientFp.find(dc->second);
                                itF != g_clientFp.end())
                                g_portFp[sp] = itF->second;
                            if (auto itM = g_clientMf.find(dc->second);
                                itM != g_clientMf.end())
                                g_portMf[sp] = itM->second;
                            if (auto itE = g_clientEq.find(dc->second);
                                itE != g_clientEq.end())
                                g_portEq[sp] = itE->second;
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
                    // ── NEEDLE PROBE (REASIXTY_NDL_PROBE / ExtState uf1_ndl_probe)
                    // The one open question about the analogue needle: VuPpm(0) is
                    // the value the plug-in's own needle rides — fast, and floored
                    // at the FACEPLATE (-36 in VU, measured) — while TextVuPpm(1)
                    // is the numeric readout, unclamped and floored at -139.2. The
                    // needle renders TextVuPpm today, which is why its ballistic
                    // does not match the plug-in (Frank 2026-08-11).
                    //
                    // Two things have to be measured, and this line has both:
                    //   UNIT — in PPM mode, does VuPpm carry MARKS (silence floors
                    //          at 0) or still VU dB (floors at -36)? Silence alone
                    //          answers it; no signal needed.
                    //   RATE — one line per CHANGE, so the line rate IS the update
                    //          rate of each type. Nothing is logged while a value
                    //          sits still, so silence costs nothing.
                    // Deliberately not folded into the 2 s summary: at 2 s per
                    // sample a ballistic cannot be seen at all.
                    if (g_ndlProbe && sp == g_srcPort) {
                        const Slot& s0 = inst.meter[int(sslmeter::DataType::VuPpm)];
                        const Slot& s1 = inst.meter[int(sslmeter::DataType::TextVuPpm)];
                        const float v0 = (s0.have && !s0.current.empty()) ? s0.current[0] : 0.f;
                        const float v1 = (s1.have && !s1.current.empty()) ? s1.current[0] : 0.f;
                        static float sV0 = 1e9f, sV1 = 1e9f;
                        if (v0 != sV0 || v1 != sV1) {
                            sV0 = v0; sV1 = v1;
                            slog("[ndl] %.3f src=%u vuppm=%.2f textvuppm=%.2f", t,
                                 unsigned(sp), double(v0), double(v1));
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
            if (readable.count(c)) {
                int n = int(::recv(c, reinterpret_cast<char*>(buf), sizeof(buf), 0));
                // n == 0 is a clean close; n < 0 with a REAL error (reset, pipe,
                // invalid socket) is a dirty one. Both mean the connection is over.
                // Only the clean case used to be handled; a dirty end would have
                // stayed in `clients` with its socket open and a heartbeat still
                // being written into it every 0.25 s for the rest of the session.
                if (n == 0 || (n < 0 && !scWouldBlock())) {
                    dropClient(i);
                    continue;
                }
                if (n < 0) { ++i; continue; }   // spurious wake, nothing to read yet
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
                    sendTo(c, seq);
                    auto sub = subscribeInitial();
                    sendTo(c, sub);
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
                    auto& acc = acc_[c];
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
                                    if (g_objDump.load(std::memory_order_relaxed)) {
                                        char line[640];
                                        std::snprintf(line, sizeof(line),
                                            "decl %02x%02x%02x%02x%02x%02x%02x%02x  %s",
                                            pay[0], pay[1], pay[2], pay[3],
                                            pay[4], pay[5], pay[6], pay[7], nm.c_str());
                                        static std::mutex dmx;
                                        static std::set<std::string> dseen;
                                        std::lock_guard<std::mutex> dlk(dmx);
                                        if (dseen.insert(line).second) {
                                            objLog("%s", line);
                                            // The whole declaration too: if an
                                            // object is an enum, its members are
                                            // in here, and guessing an index is
                                            // then unnecessary.
                                            std::string h; char bb[4];
                                            for (size_t q = 0; q < avail && q < 160; ++q) {
                                                std::snprintf(bb, sizeof(bb), "%02x", pay[q]);
                                                h += bb;
                                            }
                                            objLog("decl+ %s", h.c_str());
                                        }
                                    }
                                    const std::string suf = "EQCurveData";
                                    if (nm.size() > suf.size() &&
                                        nm.compare(nm.size() - suf.size(), suf.size(), suf) == 0) {
                                        if (g_clientModel.find(c) == g_clientModel.end()) {
                                            g_clientModel[c] = nm.substr(0, nm.size() - suf.size());
                                            slog("[corr] client model = %s",
                                                 g_clientModel[c].c_str());
                                        }
                                        // Same declaration names the curve object.
                                        std::memcpy(&g_clientEqObj[c], pay, 8);
                                    } else if (const int fi = fpIndexOf_(nm); fi >= 0) {
                                        // Remember which object id carries this
                                        // parameter ON THIS CONNECTION, so its
                                        // value frames can be recognised below.
                                        uint64_t oid = 0;
                                        std::memcpy(&oid, pay, 8);
                                        g_clientFpObj[c][oid] = fi;
                                    } else if (const int mi = mfIndexOf_(nm); mi >= 0) {
                                        // Same, for the Meter's own settings.
                                        uint64_t oid = 0;
                                        std::memcpy(&oid, pay, 8);
                                        g_clientMfObj[c][oid] = mi;
                                    }
                                    break;
                                }
                            }

                            // Every DISTINCT property write, deduped on the exact
                            // bytes: the first 8 are the object id from the
                            // declarations above, the rest is the value. A control
                            // pressed in the plug-in's GUI shows up here as a pair
                            // the connect burst never sent.
                            if (ftype == 18 && avail >= 8 &&
                                g_objDump.load(std::memory_order_relaxed)) {
                                std::string h;
                                h.reserve(80);
                                char bb[4];
                                for (size_t k = 0; k < avail && k < 40; ++k) {
                                    std::snprintf(bb, sizeof(bb), "%02x", pay[k]);
                                    h += bb;
                                }
                                static std::mutex smx;
                                static std::set<std::string> sseen;
                                std::lock_guard<std::mutex> slk(smx);
                                if (sseen.size() < 4000 && sseen.insert(h).second)
                                    objLog("set  %s", h.c_str());
                            }

                            // EQ CURVE frames on the object named above.
                            //   type=17 Prepare  → fields 2/3 = Min/MaxFrequencyHz
                            //                      (`15`/`1d` + float32 LE)
                            //   type=18 Value    → repeated field 1 float32
                            //                      (`0d` + float32 LE) = dB per point
                            // Walked defensively: read whatever `0d` run is there
                            // rather than trusting a fixed length, because the
                            // point count is the plug-in's business and has never
                            // been pinned down for every model.
                            if ((ftype == 17 || ftype == 18) && avail > 8) {
                                if (auto itE = g_clientEqObj.find(c);
                                    itE != g_clientEqObj.end()) {
                                    uint64_t oid = 0;
                                    std::memcpy(&oid, pay, 8);
                                    if (oid == itE->second) {
                                        std::lock_guard<std::mutex> lk(g_meterMx);
                                        EqCurve& ec = g_clientEq[c];
                                        if (ftype == 18) {
                                            std::vector<float> db;
                                            db.reserve((avail - 8) / 5);
                                            for (size_t k = 8; k + 4 < avail; ) {
                                                if (pay[k] != 0x0d) { ++k; continue; }
                                                float f = 0;
                                                std::memcpy(&f, pay + k + 1, 4);
                                                db.push_back(f);
                                                k += 5;
                                            }
                                            if (!db.empty()) ec.db.swap(db);
                                        } else {                    // Prepare: the axis
                                            for (size_t k = 8; k + 4 < avail; ++k) {
                                                if (pay[k] == 0x15)
                                                    std::memcpy(&ec.fMin, pay + k + 1, 4);
                                                else if (pay[k] == 0x1d)
                                                    std::memcpy(&ec.fMax, pay + k + 1, 4);
                                            }
                                        }
                                        if (auto itP = g_connPort.find(c);
                                            itP != g_connPort.end())
                                            g_portEq[itP->second] = ec;
                                    }
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
                                        // Always against the connection…
                                        FpSet& cs = g_clientFp[c];
                                        cs.val[itF->second]  = v;
                                        cs.have[itF->second] = true;
                                        // …and mirrored to the port once it has one.
                                        if (auto itP = g_connPort.find(c);
                                            itP != g_connPort.end()) {
                                            FpSet& fs = g_portFp[itP->second];
                                            fs.val[itF->second]  = v;
                                            fs.have[itF->second] = true;
                                        }
                                    }
                                }
                                // The Meter's settings arrive in the same form —
                                // a double, whether the parameter is a real unit
                                // or an enum index (the caller's table says which).
                                if (auto itO = g_clientMfObj.find(c);
                                    itO != g_clientMfObj.end()) {
                                    uint64_t oid = 0;
                                    std::memcpy(&oid, pay, 8);
                                    if (auto itF = itO->second.find(oid);
                                        itF != itO->second.end()) {
                                        double v = 0;
                                        std::memcpy(&v, pay + 9, 8);
                                        std::lock_guard<std::mutex> lk(g_meterMx);
                                        MeterFpSet& cs = g_clientMf[c];
                                        cs.val[itF->second]  = v;
                                        cs.have[itF->second] = true;
                                        if (auto itP = g_connPort.find(c);
                                            itP != g_connPort.end()) {
                                            MeterFpSet& fs = g_portMf[itP->second];
                                            fs.val[itF->second]  = v;
                                            fs.have[itF->second] = true;
                                        }
                                    }
                                }
                            }

                            // PresetList / PresetSelection: `0a <varint len> <bytes>`.
                            // Ids are name hashes, so they are the same constant on
                            // every SSL plug-in — the channel strips announce these
                            // two exactly like the Meter does.
                            if (ftype == 18 && avail >= 10 && pay[8] == 0x0a) {
                                const bool isList = std::memcmp(pay, kObjPresetList, 8) == 0;
                                const bool isSel  = std::memcmp(pay, kObjPresetSel,  8) == 0;
                                if (isList || isSel) {
                                    size_t k = 9, sl = 0, shift = 0;
                                    while (k < avail && shift <= 28) {      // varint length
                                        sl |= size_t(pay[k] & 0x7f) << shift;
                                        if (!(pay[k] & 0x80)) { ++k; break; }
                                        ++k; shift += 7;
                                    }
                                    // Only a COMPLETE string is stored: a truncated
                                    // preset list would parse into a short list that
                                    // looks perfectly plausible.
                                    if (sl > 0 && k + sl <= avail) {
                                        std::string v(reinterpret_cast<const char*>(pay + k), sl);
                                        std::lock_guard<std::mutex> lk(g_meterMx);
                                        auto& dst = isList ? g_clientPresetList[c]
                                                           : g_clientPresetSel[c];
                                        const bool changed = (dst != v);
                                        dst = v;
                                        if (auto itP = g_connPort.find(c); itP != g_connPort.end())
                                            (isList ? g_portPresetList : g_portPresetSel)[itP->second] = v;
                                        if (changed && isList &&
                                            g_objDump.load(std::memory_order_relaxed)) {
                                            // The whole document, once, so the parser
                                            // can be written against the real thing.
                                            char fn[64];
                                            std::snprintf(fn, sizeof(fn),
                                                          "reasixty_presetlist_%d.xml", int(c));
                                            if (FILE* f = std::fopen(uf8::logPath(fn).c_str(), "w")) {
                                                std::fwrite(v.data(), 1, v.size(), f);
                                                std::fclose(f);
                                            }
                                        }
                                    } else if (sl > 0) {
                                        objLog("preset %s TRUNCATED: len=%zu avail=%zu",
                                               isList ? "list" : "sel", sl, avail);
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
                                    // ⚠ A REAL VARINT, not one byte. The old
                                    // `pay[9] & 0x7f` silently truncated every
                                    // index above 127 to its low 7 bits, so on a
                                    // big session track 130 announced itself as
                                    // track 2 and the meter followed the wrong
                                    // one. Sessions that large are ordinary here.
                                    int v = 0, shift = 0; size_t k = 9;
                                    while (k < avail && shift <= 28) {
                                        v |= int(pay[k] & 0x7f) << shift;
                                        if (!(pay[k] & 0x80)) { ++k; break; }
                                        ++k; shift += 7;
                                    }
                                    g_clientIndex[c] = v;             // 1-based track idx
                                }
                                // Queue once this client has announced BOTH; the
                                // port claim below ties them to the next new port.
                                auto itN = g_clientName.find(c);
                                auto itI = g_clientIndex.find(c);
                                if (itN != g_clientName.end() && itI != g_clientIndex.end()) {
                                    g_pending.push_back({ itN->second, itI->second });
                                    g_namedClients.insert(c);
                                    // ALWAYS logged, not behind g_trace: it fires
                                    // once per plug-in connect, and it is the only
                                    // record of what the host calls an instance.
                                    // Needed to tell a master / monitoring-chain
                                    // Meter from a plain track one without guessing.
                                    // (It said that and still used slog(), which is
                                    // trace-gated — so on a machine with tracing off
                                    // this record did not exist. slogAlways now.)
                                    slogAlways("instance announced: track=%d name=\"%s\"",
                                               itI->second, itN->second.c_str());
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
    g_trace    = std::getenv("REASIXTY_SSLCORE_TRACE") != nullptr;
    g_t10Dump  = std::getenv("REASIXTY_T10_DUMP") != nullptr;
    g_ndlProbe = std::getenv("REASIXTY_NDL_PROBE") != nullptr;
    if (g_ndlProbe) g_trace = true;   // the probe writes through the trace log
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

void setObjectDump(bool on) { g_objDump.store(on, std::memory_order_relaxed); }

namespace {
std::string presetProp_(const std::map<uint16_t, std::string>& byPort, int port)
{
    std::lock_guard<std::mutex> lk(g_meterMx);
    const uint16_t p = port ? uint16_t(port) : currentMeterPortLocked_();
    auto it = byPort.find(p);
    return (it != byPort.end()) ? it->second : std::string();
}
}  // namespace

std::string presetListXml(int port)  { return presetProp_(g_portPresetList, port); }
std::string presetSelection(int port) { return presetProp_(g_portPresetSel, port); }

bool setPresetSelection(const std::string& path, int port)
{
    if (path.empty() || !g_running.load()) return false;
    // `0a <varint len> <bytes>` — the same encoding the plug-in announces it in.
    std::vector<uint8_t> val;
    val.push_back(0x0a);
    for (size_t n = path.size(); ; ) {
        uint8_t b = uint8_t(n & 0x7f);
        n >>= 7;
        if (n) b |= 0x80;
        val.push_back(b);
        if (!n) break;
    }
    val.insert(val.end(), path.begin(), path.end());
    std::lock_guard<std::mutex> lk(g_cmdMx);
    g_cmdQueue.emplace_back(uint16_t(port), propFrame(kObjPresetSel, val));
    return true;
}

void resetMeter(bool peakHolds, bool loudness)
{
    const int m = (peakHolds ? 1 : 0) | (loudness ? 2 : 0);
    if (m) g_resetReq.fetch_or(m, std::memory_order_relaxed);
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
    // wrong"). Ports with no known index sort last.
    //
    // WITHIN one track, order by CONNECT SEQUENCE — the port number is as
    // arbitrary here as it was across tracks. It has to be the same order
    // liveMeterPorts_ uses, or the pin and the FX resolution walk two different
    // lists: Frank 2026-08-11 cycled V-Pot1 and got "Track 4 2" first and
    // "Track 4 1" second, because the label counts in connect order and the
    // cycle counted in port order. One ordering, or the numbering is a lie.
    std::sort(out.begin(), out.end(), [](uint16_t a, uint16_t b) {
        auto ia = g_portIndex.find(a), ib = g_portIndex.find(b);
        const int va = (ia != g_portIndex.end()) ? ia->second : INT_MAX;
        const int vb = (ib != g_portIndex.end()) ? ib->second : INT_MAX;
        if (va != vb) return va < vb;
        auto sa = g_portSeq.find(a), sb = g_portSeq.find(b);
        const uint64_t qa = (sa != g_portSeq.end()) ? sa->second : UINT64_MAX;
        const uint64_t qb = (sb != g_portSeq.end()) ? sb->second : UINT64_MAX;
        return (qa != qb) ? (qa < qb) : (a < b);
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
static std::vector<uint16_t> liveMeterPorts_(int trackIndex);
static void steerAutoPort_() {
    if (g_meterSel >= 0) return;                     // manual V-Pot1 pin wins
    const int want = g_autoTrackIdx.load();
    if (want == 0) return;                           // 0 = nothing selected; -1 IS the master
    // The track's Meters in CONNECT order, and take the first. Iterating g_inst
    // instead handed out whichever UDP port number happened to sort first, so on
    // a track with two Meters the view could land on either one — and the FX the
    // V-Pots then edited was resolved separately, by FX-chain order. Two
    // different answers to "which instance", which is the whole bug.
    const std::vector<uint16_t> live = liveMeterPorts_(want);
    if (!live.empty()) g_srcPort = live.front();
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
// The port the view is reading — the pin, else the auto-picked instance, else
// the first in track order. Caller MUST hold g_meterMx. currentMeterTrackIndex()
// and currentMeterPort() are the same question asked at two levels of detail, so
// they resolve through this one body and cannot answer differently.
static uint16_t currentMeterPortLocked_() {
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
    return port;
}

int currentMeterTrackIndex() {
    std::lock_guard<std::mutex> lk(g_meterMx);
    const uint16_t port = currentMeterPortLocked_();
    auto it = g_portIndex.find(port);
    return (it != g_portIndex.end()) ? it->second : 0;
}

int currentMeterPort() {
    std::lock_guard<std::mutex> lk(g_meterMx);
    return int(currentMeterPortLocked_());
}

int currentMeterInstanceOnTrack(int* countOut) {
    if (countOut) *countOut = 0;
    std::lock_guard<std::mutex> lk(g_meterMx);
    const uint16_t port = currentMeterPortLocked_();
    auto ti = g_portIndex.find(port);
    if (ti == g_portIndex.end()) return -1;
    // ⇨ COUNT ONLY THE ONES THAT WOULD READ THE SAME.
    // The number exists for one reason: two Meters that announce the SAME name
    // label identically, so cycling V-Pot1 looks stuck. The master's two chains
    // announce DIFFERENT names ("MASTER" vs "HARDWARE OUTPUT") and the label
    // shows that difference, so numbering them adds a digit that means nothing
    // (Frank 2026-08-28: "das muss MASTER und MON FX heissen, keine NR hinten").
    // Same track index, same announced name → same label → number them.
    auto nameOf = [](uint16_t p) -> std::string {
        auto it = g_portName.find(p);
        return (it != g_portName.end()) ? it->second : std::string();
    };
    const std::string mine = nameOf(port);
    std::vector<uint16_t> live;
    for (uint16_t p : liveMeterPorts_(ti->second))
        if (nameOf(p) == mine) live.push_back(p);
    if (countOut) *countOut = int(live.size());
    for (size_t i = 0; i < live.size(); ++i)
        if (live[i] == port) return int(i);
    return -1;
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

// ── Which STREAM belongs to which plug-in ────────────────────────────────────
// One resolver, three rungs, most specific first. Every rung DEFERS when it
// cannot decide instead of picking, because a wrong pick is invisible — it just
// shows a plausible number from the wrong strip, which is how the 2026-08-10 bug
// survived for weeks.
//
//   1. SETTINGS  — the values the caller read off the FX in REAPER, matched
//                  against what each instance streams. The only rung that can
//                  separate two strips of the SAME model.
//   2. MODEL     — "4K E" vs "4K B", from the EQ-curve object name at connect.
//   3. ORDINAL   — FX-chain position, valid only while the track's UDP ports
//                  are still in connect order. One reconnect breaks that.
//
// Returns the winning UDP source port, or 0 for "cannot say" (→ caller goes
// dark). Callers hold nothing; g_meterMx is taken here.
static std::string squashModel_(const char* s)
{
    std::string o;
    for (; s && *s; ++s)
        if (!std::isspace(static_cast<unsigned char>(*s)))
            o += char(std::toupper(static_cast<unsigned char>(*s)));
    return o;
}

// Every live channel strip on the track, in connect order. Caller holds g_meterMx.
static std::vector<uint16_t> liveStripPorts_(int trackIndex)
{
    const long long now = nowMs();
    std::vector<std::pair<uint64_t, uint16_t>> v;
    for (const auto& kv : g_inst) {
        if (kv.second.kind != Kind::ChannelStrip) continue;
        auto pi = g_portIndex.find(kv.first);
        if (pi == g_portIndex.end() || pi->second != trackIndex) continue;
        // Dead reconnect-orphans linger in g_inst frozen at their last value,
        // so each candidate is tested for freshness on its own.
        if (now - kv.second.lastMs > 1000) continue;
        auto sq = g_portSeq.find(kv.first);
        v.emplace_back(sq == g_portSeq.end() ? UINT64_MAX : sq->second, kv.first);
    }
    std::sort(v.begin(), v.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<uint16_t> out;
    out.reserve(v.size());
    for (const auto& e : v) out.push_back(e.second);
    return out;
}

static uint16_t resolveStripPort_(int trackIndex, const char* model,
                                  const StripParam* fp, int nfp,
                                  int instanceOrdinal)
{
    if (trackIndex <= 0) return 0;
    const std::vector<uint16_t> live = liveStripPorts_(trackIndex);
    if (live.empty()) return 0;

    // 1 — settings.
    if (fp && nfp > 0) {
        uint16_t best = 0; int bestScore = 0, bestCount = 0;
        for (uint16_t port : live) {
            auto pf = g_portFp.find(port);
            if (pf == g_portFp.end()) continue;
            int score = 0;
            for (int i = 0; i < nfp; ++i) {
                const int fi = fpIndexOf_(fp[i].id ? fp[i].id : "");
                if (fi < 0 || !pf->second.have[fi]) continue;
                // Relative tolerance: dB, Hz and ratios share one set, and
                // REAPER's formatted value is rounded for display.
                const double a = pf->second.val[fi], b = fp[i].value;
                const double tol = 0.02 * std::max(1.0, std::max(std::fabs(a),
                                                                 std::fabs(b)));
                if (std::fabs(a - b) <= tol) ++score;
            }
            if (score > bestScore)      { bestScore = score; best = port; bestCount = 1; }
            else if (score == bestScore && score > 0) ++bestCount;
        }
        if (bestScore > 0 && bestCount == 1) return best;
    }

    // 2 — model.
    if (const std::string want = squashModel_(model); !want.empty()) {
        uint16_t hit = 0; int matches = 0;
        for (uint16_t port : live) {
            auto pm = g_portModel.find(port);
            if (pm == g_portModel.end() ||
                squashModel_(pm->second.c_str()) != want) continue;
            ++matches;
            if (!hit) hit = port;
        }
        if (matches == 1) return hit;      // two of the same model → ambiguous
    }

    // 3 — ordinal.
    if (instanceOrdinal >= 0 && size_t(instanceOrdinal) < live.size())
        return live[size_t(instanceOrdinal)];
    return 0;
}

// ── Which METER STREAM belongs to which Meter plug-in ────────────────────────
// The strip resolver above, one plug-in over. Same three rungs, same rule that
// an ambiguous rung defers instead of picking: routing a V-Pot edit to the wrong
// instance is invisible — the display keeps showing the other one's numbers,
// which is exactly how this survived unnoticed next to the strip fix.
//
// Every live Meter on the track, in connect order. Caller holds g_meterMx.
static std::vector<uint16_t> liveMeterPorts_(int trackIndex)
{
    const long long cut = nowMs() - 3000;   // a loaded Meter streams ~25 Hz
    std::vector<std::pair<uint64_t, uint16_t>> v;
    for (const auto& kv : g_inst) {
        if (kv.second.kind != Kind::Meter || kv.second.lastMs < cut) continue;
        auto pi = g_portIndex.find(kv.first);
        if (pi == g_portIndex.end() || pi->second != trackIndex) continue;
        auto sq = g_portSeq.find(kv.first);
        v.emplace_back(sq == g_portSeq.end() ? UINT64_MAX : sq->second, kv.first);
    }
    std::sort(v.begin(), v.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<uint16_t> out;
    out.reserve(v.size());
    for (const auto& e : v) out.push_back(e.second);
    return out;
}

int meterPortForFx(int trackIndex, bool isPro, bool monitorChain,
                   const StripParam* fp, int nfp, int instanceOrdinal)
{
    // ⇨ -1 IS A TRACK, 0 IS "THE HOST DID NOT SAY".
    // The announcement uses REAPER's IP_TRACKNUMBER convention, and the master
    // reports -1 there. Measured in Frank's own log, 2026-08-28:
    //   instance announced: track=-1 name="MASTER"
    //   instance announced: track=-1 name="HARDWARE OUTPUT"
    // Rejecting everything <= 0 therefore switched this whole resolver off for
    // the master, so a Meter on the master and one in the monitoring chain were
    // never told apart: the caller kept its first-match fallback and V-Pot1's pin
    // moved the DATA while the labels, the V-Pot2/3/4 edits and the preset
    // browser stayed on the first one ("kann am Gerät nicht mehr umschalten").
    if (trackIndex == 0) return 0;
    std::lock_guard<std::mutex> lk(g_meterMx);
    std::vector<uint16_t> live = liveMeterPorts_(trackIndex);
    if (live.empty()) return 0;
    // Which rung answered, logged once per changed answer — this is the whole
    // diagnosis when the surface edits the wrong Meter, and it is not derivable
    // from anything else in the log.
    auto say = [&](int rung, uint16_t port) {
        static int sRung = -1; static uint16_t sPort = 0; static int sTrack = -1;
        if (rung == sRung && port == sPort && trackIndex == sTrack) return int(port);
        sRung = rung; sPort = port; sTrack = trackIndex;
        slog("[meter] track %d fx#%d -> port %u by %s", trackIndex, instanceOrdinal,
             unsigned(port), rung == 0 ? "sole instance" : rung == 1 ? "settings"
                          : rung == 2 ? "pro-ness"
                          : rung == 4 ? "chain"         : "ordinal");
        return int(port);
    };
    if (live.size() == 1) return say(0, live.front());   // nothing to tell apart

    // 0 — CHAIN, and it beats every rung below because the host itself says it.
    // The master's two FX chains announce different names: the track's own chain
    // says "MASTER" (the track name), the monitoring chain says "HARDWARE
    // OUTPUT". The FX index carries the same distinction in its 0x1000000 flag,
    // so the two sides can simply be matched. Nothing here is inferred from
    // order, which is what rung 3 has to do.
    bool narrowed = false;
    {
        int named = 0;
        for (uint16_t port : live) {
            auto it = g_portName.find(port);
            if (it != g_portName.end() && it->second == "HARDWARE OUTPUT") ++named;
        }
        // Only meaningful once some live stream actually carries that name — on
        // an ordinary track none does and this rung must stay silent.
        if (named > 0 && named < int(live.size())) {
            std::vector<uint16_t> f;
            for (uint16_t port : live) {
                auto it = g_portName.find(port);
                const bool isMon = (it != g_portName.end() &&
                                    it->second == "HARDWARE OUTPUT");
                if (isMon == monitorChain) f.push_back(port);
            }
            if (f.size() == 1) return say(4, f.front());
            if (!f.empty()) { live.swap(f); narrowed = true; }
        }
    }

    // 1 — settings.
    if (fp && nfp > 0) {
        uint16_t best = 0; int bestScore = 0, bestCount = 0;
        for (uint16_t port : live) {
            auto pm = g_portMf.find(port);
            if (pm == g_portMf.end()) continue;
            int score = 0;
            for (int i = 0; i < nfp; ++i) {
                const int mi = mfIndexOf_(fp[i].id ? fp[i].id : "");
                if (mi < 0 || !pm->second.have[mi]) continue;
                const double a = pm->second.val[mi], b = fp[i].value;
                // Same relative tolerance as the strips: REAPER's formatted value
                // is rounded for display, and an enum index compares exactly.
                const double tol = 0.02 * std::max(1.0, std::max(std::fabs(a),
                                                                 std::fabs(b)));
                if (std::fabs(a - b) <= tol) ++score;
            }
            if (score > bestScore)      { bestScore = score; best = port; bestCount = 1; }
            else if (score == bestScore && score > 0) ++bestCount;
        }
        if (bestScore > 0 && bestCount == 1) return say(1, best);
    }

    // 2 — Pro-ness. POSITIVE evidence only: isPro is set the first time an
    // instance streams a Loudness data type, so a false only means "has not shown
    // Loudness yet", never "is not a Pro". Both directions therefore require that
    // some instance on this track HAS shown it, or the rung says nothing.
    {
        int pros = 0; uint16_t proPort = 0, plainPort = 0; int plains = 0;
        for (uint16_t port : live) {
            auto in = g_inst.find(port);
            if (in == g_inst.end()) continue;
            if (in->second.isPro) { ++pros;   if (!proPort)   proPort   = port; }
            else                  { ++plains; if (!plainPort) plainPort = port; }
        }
        if (pros > 0) {
            if (isPro  && pros   == 1) return say(2, proPort);
            if (!isPro && plains == 1) return say(2, plainPort);
        }
    }

    // 3 — ordinal. NOT after rung 0 narrowed the list: the ordinal the caller
    // passes counts the track's Meters across BOTH chains, so indexing a
    // single-chain list with it would point at a neighbour. Deferring is the
    // house rule here — a tie never quietly routes to the wrong instance.
    if (!narrowed && instanceOrdinal >= 0 && size_t(instanceOrdinal) < live.size())
        return say(3, live[size_t(instanceOrdinal)]);
    return 0;
}

bool getChannelStripMeterForTrackInstance(int csType, int trackIndex,
                                          int instanceOrdinal,
                                          std::vector<float>& current) {
    return getChannelStripMeterForTrackStrip(csType, trackIndex, nullptr,
                                             nullptr, 0, instanceOrdinal, current);
}

bool getChannelStripMeterForTrackModel(int csType, int trackIndex,
                                       const char* model, int instanceOrdinal,
                                       std::vector<float>& current) {
    return getChannelStripMeterForTrackStrip(csType, trackIndex, model,
                                             nullptr, 0, instanceOrdinal, current);
}

bool getChannelStripMeterForTrackStrip(int csType, int trackIndex,
                                       const char* model,
                                       const StripParam* fp, int nfp,
                                       int instanceOrdinal,
                                       std::vector<float>& current) {
    if (csType < 0 || csType > int(ChannelStripMeter::MicPreSaturation)) return false;
    std::lock_guard<std::mutex> lk(g_meterMx);
    const uint16_t port = resolveStripPort_(trackIndex, model, fp, nfp,
                                            instanceOrdinal);
    if (!port) return false;
    auto in = g_inst.find(port);
    if (in == g_inst.end()) return false;
    const Slot& s = in->second.meter[csType];
    // The right strip may simply not carry this meter — a gate that has never
    // moved has no GateGain slot. Dark is the honest answer; borrowing the
    // neighbour's value is the thing this whole file exists to prevent.
    if (!s.have || s.current.empty()) return false;
    current = s.current;
    return true;
}

bool getChannelStripEqCurve(int trackIndex, const char* model,
                            const StripParam* fp, int nfp, int instanceOrdinal,
                            std::vector<float>& dbValues,
                            float& minHz, float& maxHz) {
    std::lock_guard<std::mutex> lk(g_meterMx);
    const uint16_t port = resolveStripPort_(trackIndex, model, fp, nfp,
                                            instanceOrdinal);
    if (!port) return false;
    auto it = g_portEq.find(port);
    if (it == g_portEq.end() || it->second.db.size() < 8) return false;
    dbValues = it->second.db;
    minHz = it->second.fMin;
    maxHz = it->second.fMax;
    return true;
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
