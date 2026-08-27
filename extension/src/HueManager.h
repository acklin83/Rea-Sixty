#pragma once
//
// HueManager — owns the bridge connection, the eight strip slots, the eight
// scene slots and one background worker that does every byte of network I/O.
//
// Threading contract, the same one DynaMountManager works to:
//   * The REAPER main/timer thread only ever calls the cheap non-blocking
//     setters (setBrightness / nudgeHue / …) and reads the live values back for
//     feedback. Nothing on the main thread ever waits for the bridge.
//   * The worker thread does ALL of it: discovery, pairing, the periodic state
//     refresh, and every write. A bridge that has been unplugged therefore costs
//     a timeout on a thread nobody is watching, not a frozen REAPER.
//   * Config (slots, scenes, controls, recording light, marker cues) is edited
//     from Settings on the main thread under cfgMx_; the worker takes brief
//     copies rather than holding the lock across a request.
//
// ⛔ WHY THE WORKER COALESCES INSTEAD OF QUEUEING. A fader drag produces a new
// value every tick. The bridge takes about ten writes per second for one light
// and about one per second for a group, and past that it drops commands with no
// error at all — which reads as a fader that sticks, not as a fault. So targets
// are ATOMICS the worker samples, never a queue it drains: a drag collapses into
// however many writes the gap allows, always carrying the newest value, and the
// release always gets one final write of its own.
//
// Each write carries dynamics.duration = the gap, so the bridge interpolates
// between our packets. Ten packets a second look like a ramp instead of ten
// steps, which is the only reason REST is good enough here at all.

#include "HueClient.h"
#include "HueColor.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace uf8::hue {

// Eight strips on a UF8, eight favourites everywhere else in this project.
inline constexpr int kMaxSlots  = 8;
inline constexpr int kMaxScenes = 8;

// The label that fits the UF8 scribble row.
inline constexpr int kLabelChars = 7;

// How the link to the bridge is doing. Anything but Online means the strips
// still respond but say so instead of pretending.
enum class LinkState : uint8_t {
    Idle = 0,      // no bridge configured
    Discovering,   // asking discovery.meethue.com
    Pairing,       // POSTing /api, waiting for the link button
    Verifying,     // have a key, checking the bridge id and pulling the lists
    Online,
    Failed,        // see statusLine()
};

// What a V-Pot does in Hue Mode. Configurable because a room of white-only
// lamps has no use for a hue axis.
enum class PotRole : uint8_t { Hue = 0, Saturation, Warmth, Off };

// What pushing the V-Pot does.
enum class PushRole : uint8_t { WhiteToggle = 0, OnOff, Off };

// Who the recording light applies to.
enum class RecTarget : uint8_t { MarkedSlots = 0, Group };

// What happens when the recording stops.
enum class RecRestore : uint8_t { Previous = 0, Scene };

// ---- config -----------------------------------------------------------------

// One strip slot. A slot points at EITHER a single light or the grouped_light
// service of a room/zone; `kind` decides the rate limit and nothing else.
struct SlotConfig {
    bool        enabled = false;
    std::string label;               // <= kLabelChars, shown on the scribble
    TargetKind  kind    = TargetKind::Light;
    std::string rid;                 // light id, or the room's grouped_light id
    std::string bridgeName;          // what the bridge calls it, for Settings
    std::string groupId;             // room/zone id when kind == Group
    int         colour  = 0;         // palette index for the colour bar
    bool        recLight = false;    // part of the recording light
};

// One scene slot, reachable as the action hue_scene_recall with param 1..8.
struct SceneSlot {
    std::string id;
    std::string label;               // key label; empty falls back to the name
    std::string bridgeName;
    uint32_t    rgb = 0xFFFFFFu;     // key LED colour
};

// The Controls block of the settings pane.
struct Controls {
    PotRole  pot        = PotRole::Hue;
    PotRole  potFlip    = PotRole::Saturation;
    PushRole push       = PushRole::WhiteToggle;
    bool     bottomOff  = true;      // fader at the bottom switches the lamp off
    int      transitionMs = 100;     // dynamics.duration on every write
};

struct RecLightConfig {
    bool        enabled   = false;
    RecTarget   target    = RecTarget::MarkedSlots;
    std::string groupId;             // room/zone id when target == Group
    uint32_t    rgb       = 0xE02020u;
    double      brightness = 100.0;  // percent
    RecRestore  restore   = RecRestore::Previous;
    std::string restoreSceneId;
};

struct MarkerConfig {
    bool        enabled = false;
    std::string prefix  = "hue:";
    int         durationMs = 400;
};

// ---- live per-slot state ----------------------------------------------------

// Main thread writes the targets, worker reads the newest and sends it. Not
// copyable on purpose: there is exactly one of these per slot, forever.
struct SlotLive {
    std::atomic<bool>   on{false};
    std::atomic<double> bri{0.0};      // 1..100 percent
    std::atomic<double> hueDeg{0.0};
    std::atomic<double> sat{1.0};
    std::atomic<double> warm{0.5};     // 0 cold .. 1 warm, drives mirek
    std::atomic<bool>   white{false};  // white (mirek) instead of colour (xy)
    std::atomic<bool>   dirty{false};
    std::atomic<bool>   flush{false};  // fader released: send now, ignore the gap
    std::atomic<bool>   reachable{false};
};

class HueManager {
public:
    HueManager() = default;
    ~HueManager();

    HueManager(const HueManager&)            = delete;
    HueManager& operator=(const HueManager&) = delete;

    void start();
    void stop();

    // ---- link (main thread) -------------------------------------------------
    LinkState   linkState() const { return link_.load(); }
    bool        online() const    { return link_.load() == LinkState::Online; }
    std::string statusLine();               // one line for the settings pane
    std::string bridgeIp();
    std::string bridgeId();
    bool        haveAppKey();

    void setBridgeIp(const std::string& ip);   // manual entry; drops the key check
    void requestDiscovery();                   // ask discovery.meethue.com
    void requestPairing();                     // press-the-button loop, ~30 s
    void cancelPairing();
    void requestRefresh();                     // re-pull lights, groups, scenes
    void forget();                             // drop key + ip, back to Idle

    // Bridges found by the last discovery run.
    std::vector<DiscoveredBridge> discovered();

    // ---- catalogue snapshots (main thread, for Settings) --------------------
    std::vector<Light> lights();
    std::vector<Group> groups();     // rooms and zones in one list
    std::vector<Scene> scenes();

    // Display name for a slot's target, resolved against the catalogue.
    std::string targetNameFor(const SlotConfig& c);

    // ---- config (main thread) ----------------------------------------------
    SlotConfig  slot(int i);
    void        setSlot(int i, const SlotConfig& c);
    SceneSlot   sceneSlot(int i);
    void        setSceneSlot(int i, const SceneSlot& s);
    Controls    controls();
    void        setControls(const Controls& c);
    RecLightConfig recLight();
    void        setRecLight(const RecLightConfig& r);
    MarkerConfig markers();
    void        setMarkers(const MarkerConfig& m);

    // Number of enabled slots = number of pinned Hue strips.
    int  definedCount();
    bool slotEnabled(int i);

    // ---- strip mapping ------------------------------------------------------
    // Mirrors DynaMount: the k-th enabled slot takes strip k from the left, or
    // 8-N+k from the right. -1 when that strip is a normal track strip.
    enum class FillDir : uint8_t { Left = 0, Right };
    void    setFillDir(FillDir d) { fill_.store(d); cfgDirty_.store(true); }
    FillDir fillDir() const       { return fill_.load(); }
    int     slotForStrip(int strip);
    bool    isHueStrip(int strip) { return slotForStrip(strip) >= 0; }

    // ---- control (main thread, cheap) --------------------------------------
    // f01 is the fader, 0..1. With Controls::bottomOff the very bottom becomes
    // on:false instead of a brightness.
    void setFader(int i, double f01, bool released);
    void setOn(int i, bool on);
    void toggleOn(int i);
    void nudgePot(int i, int clicks, bool flip);
    // The same walk with the axis named outright, for a surface whose pots are
    // FIXED (the UF1 has one knob per axis). Exists so such a caller never has
    // to write the configured role, borrow it and write it back: the UF8 painter
    // reads that role every tick to draw its ring, and the borrow would make the
    // ring flicker on a surface nobody was touching.
    void nudgeAxis(int i, int clicks, PotRole axis);
    void pushPot(int i);
    void soloLight(int i);          // this one stays on, every other slot goes dark
    void allOff();

    // ---- feedback (main thread) --------------------------------------------
    bool     liveOn(int i);
    double   liveBri01(int i);      // 0..1 for the motor fader
    double   liveHueDeg(int i);
    double   liveSat(int i);
    double   liveWarm(int i);       // 0 cold .. 1 warm
    bool     liveWhite(int i);
    uint32_t liveRgb24(int i);      // colour bar / LED
    bool     liveReachable(int i);
    // "B75 H210" style, <= 19 chars, for the UF8 value line.
    std::string liveValueLine(int i);

    // ---- scenes -------------------------------------------------------------
    // slot is 0..kMaxScenes-1. Long press asks for the dynamic recall.
    void recallSceneSlot(int slot, bool dynamic);
    // Used by the marker cues, which resolve by NAME rather than by slot.
    bool recallSceneByName(const std::string& name, bool dynamic, int durationMs);
    // Name of the scene in a slot, for the display flash. Empty when unset.
    std::string sceneSlotName(int slot);

    // ---- recording light ----------------------------------------------------
    // Called from the transport edge in onTimerBody_. Idempotent: calling
    // recordingStarted twice does not take a second snapshot.
    void recordingStarted();
    void recordingStopped();
    bool recordingLightHeld() const { return recHeld_.load(); }

    // ---- persistence --------------------------------------------------------
    std::string serialize();
    // Returns false and CHANGES NOTHING when `s` does not parse as a
    // configuration. See the definition for why that matters more than it
    // sounds: a reader that fails silently followed by a writer that saves the
    // failure is how a configuration disappears for good.
    bool        deserialize(const std::string& s);
    // True once since the last call: something the user (or pairing) changed
    // needs writing out. Lets the settings pane save worker-side changes without
    // saving a configuration nobody has touched over one that is already there.
    bool        takeConfigDirty() { return cfgDirty_.exchange(false); }
    // The key material lives in its own ExtState value so a shared setup bundle
    // can carry the slots without carrying the credentials.
    std::string serializeCredentials();
    void        deserializeCredentials(const std::string& s);

private:
    // ---- worker -------------------------------------------------------------
    void workerLoop();
    bool request(const char* method, const std::string& url,
                 const std::string& body, std::string* out);
    void runDiscovery();
    void runPairing();
    void runVerify();
    // `full` also re-reads rooms, zones and scenes. Those change when somebody
    // rearranges their house, not while you are mixing, so the fast path leaves
    // them alone and polls only what actually moves: the lights and the grouped
    // lights. The catalogue rides along once every ten passes and on demand.
    void runRefresh(bool full);
    void adoptFromBridge(double* sentBri, Xy* sentXy);
    void pushSlot(int i, bool flushing);
    void applyRecLight(bool on);
    std::vector<std::string> recLightLightIds();

    void setStatus(LinkState s, std::string text);

    // ---- state --------------------------------------------------------------
    std::array<SlotConfig, kMaxSlots>  slots_;
    std::array<SceneSlot,  kMaxScenes> sceneSlots_;
    std::array<SlotLive,   kMaxSlots>  live_;

    Controls        controls_;
    RecLightConfig  recCfg_;
    MarkerConfig    markerCfg_;

    std::string ip_;
    std::string appKey_;
    std::string clientKey_;
    std::string bridgeId_;
    std::string status_;

    std::vector<DiscoveredBridge> found_;
    std::vector<Light>        lights_;
    std::vector<Group>        groups_;
    std::vector<GroupedLight> groupedLights_;
    std::vector<Scene>        scenes_;

    // A recall the worker still has to make: scene id, dynamic, duration.
    struct SceneReq { std::string id; bool dynamic; int durationMs; };
    std::vector<SceneReq> sceneQueue_;

    // The state of every light the recording light overrode, taken just before
    // the override so the restore is exact.
    struct Saved { std::string id; bool on; double bri; bool hasXy; Xy xy;
                   bool hasMirek; int mirek; };
    std::vector<Saved> recSaved_;

    mutable std::mutex cfgMx_;

    std::atomic<LinkState> link_{LinkState::Idle};
    std::atomic<FillDir>   fill_{FillDir::Left};
    std::atomic<bool>      cfgDirty_{false};
    std::atomic<bool>      run_{false};
    std::atomic<bool>      discoverReq_{false};
    std::atomic<bool>      pairReq_{false};
    std::atomic<bool>      refreshReq_{false};
    std::atomic<bool>      verifyReq_{false};
    std::atomic<bool>      sceneReq_{false};
    std::atomic<int>       recReq_{0};     // +1 = turn on, -1 = turn off
    std::atomic<bool>      recHeld_{false};
    std::atomic<int64_t>   pairDeadlineMs_{0};

    std::thread worker_;
};

// Process-wide singleton; first access starts the worker.
HueManager& manager();

} // namespace uf8::hue
