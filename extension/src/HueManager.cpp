#include "HueManager.h"

#include "HttpClient.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace uf8::hue {
namespace {

using clock_t_ = std::chrono::steady_clock;

int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               clock_t_::now().time_since_epoch()).count();
}

double clampd(double v, double lo, double hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

double wrapDeg(double d)
{
    d = std::fmod(d, 360.0);
    if (d < 0.0) d += 360.0;
    return d;
}

// Tabs and newlines are the field and record separators of the persisted form,
// so a light called "Spot\tlinks" would otherwise shift every later column.
std::string sanitize(const std::string& in)
{
    std::string out = in;
    for (char& c : out)
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    return out;
}

std::vector<std::string> splitTabs(const std::string& line)
{
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        const size_t tab = line.find('\t', start);
        if (tab == std::string::npos) { out.push_back(line.substr(start)); break; }
        out.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
    return out;
}

int toInt(const std::string& s, int dflt = 0)
{
    return s.empty() ? dflt : std::atoi(s.c_str());
}

// The fader's very bottom is a dead zone that means "off" rather than "1 %".
// One percent of travel: wide enough to reach with a motor fader, narrow enough
// that nobody hits it aiming for a low level.
constexpr double kOffZone = 0.01;

} // namespace

// ---- lifecycle --------------------------------------------------------------

HueManager::~HueManager()
{
    stop();
}

void HueManager::start()
{
    if (run_.exchange(true)) return;
    worker_ = std::thread([this] { workerLoop(); });
}

void HueManager::stop()
{
    if (!run_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();
}

HueManager& manager()
{
    static HueManager m;
    static std::once_flag once;
    std::call_once(once, [] { m.start(); });
    return m;
}

// ---- link -------------------------------------------------------------------

void HueManager::setStatus(LinkState s, std::string text)
{
    link_.store(s);
    std::lock_guard<std::mutex> lk(cfgMx_);
    status_ = std::move(text);
}

std::string HueManager::statusLine()
{
    std::lock_guard<std::mutex> lk(cfgMx_);
    return status_;
}

std::string HueManager::bridgeIp()
{
    std::lock_guard<std::mutex> lk(cfgMx_);
    return ip_;
}

std::string HueManager::bridgeId()
{
    std::lock_guard<std::mutex> lk(cfgMx_);
    return bridgeId_;
}

bool HueManager::haveAppKey()
{
    std::lock_guard<std::mutex> lk(cfgMx_);
    return !appKey_.empty();
}

void HueManager::setBridgeIp(const std::string& ip)
{
    {
        std::lock_guard<std::mutex> lk(cfgMx_);
        if (ip == ip_) return;
        ip_ = ip;
        // A different address is a different bridge until proven otherwise —
        // the stored id is the only identity check we have left once the
        // certificate goes unverified, so it must not survive an IP change.
        bridgeId_.clear();
        lights_.clear();
        groups_.clear();
        scenes_.clear();
    }
    link_.store(LinkState::Idle);
    if (haveAppKey()) verifyReq_.store(true);
}

void HueManager::requestDiscovery() { discoverReq_.store(true); }

void HueManager::requestPairing()
{
    // Thirty seconds is what the bridge's own apps give you to walk over and
    // press the button.
    pairDeadlineMs_.store(nowMs() + 30000);
    pairReq_.store(true);
}

void HueManager::cancelPairing()
{
    pairReq_.store(false);
    pairDeadlineMs_.store(0);
    if (link_.load() == LinkState::Pairing)
        setStatus(LinkState::Idle, "Pairing cancelled");
}

void HueManager::requestRefresh() { refreshReq_.store(true); }

void HueManager::forget()
{
    pairReq_.store(false);
    {
        std::lock_guard<std::mutex> lk(cfgMx_);
        ip_.clear();
        appKey_.clear();
        clientKey_.clear();
        bridgeId_.clear();
        lights_.clear();
        groups_.clear();
        scenes_.clear();
        found_.clear();
    }
    setStatus(LinkState::Idle, "No bridge");
}

std::vector<DiscoveredBridge> HueManager::discovered()
{
    std::lock_guard<std::mutex> lk(cfgMx_);
    return found_;
}

// ---- catalogue --------------------------------------------------------------

std::vector<Light> HueManager::lights()
{
    std::lock_guard<std::mutex> lk(cfgMx_);
    return lights_;
}

std::vector<Group> HueManager::groups()
{
    std::lock_guard<std::mutex> lk(cfgMx_);
    return groups_;
}

std::vector<Scene> HueManager::scenes()
{
    std::lock_guard<std::mutex> lk(cfgMx_);
    return scenes_;
}

std::string HueManager::targetNameFor(const SlotConfig& c)
{
    if (c.rid.empty()) return std::string();
    std::lock_guard<std::mutex> lk(cfgMx_);
    if (c.kind == TargetKind::Light) {
        for (const Light& L : lights_)
            if (L.id == c.rid) return L.name;
    } else {
        for (const Group& g : groups_)
            if (g.groupedLightId == c.rid || g.id == c.groupId) return g.name;
    }
    // Falls back to whatever the bridge called it when the slot was made, so a
    // bridge that is offline still shows readable rows in Settings.
    return c.bridgeName;
}

// ---- config -----------------------------------------------------------------

SlotConfig HueManager::slot(int i)
{
    if (i < 0 || i >= kMaxSlots) return SlotConfig{};
    std::lock_guard<std::mutex> lk(cfgMx_);
    return slots_[static_cast<size_t>(i)];
}

void HueManager::setSlot(int i, const SlotConfig& c)
{
    if (i < 0 || i >= kMaxSlots) return;
    std::lock_guard<std::mutex> lk(cfgMx_);
    SlotConfig cfg = c;
    if (cfg.label.size() > static_cast<size_t>(kLabelChars))
        cfg.label.resize(static_cast<size_t>(kLabelChars));
    slots_[static_cast<size_t>(i)] = std::move(cfg);
}

SceneSlot HueManager::sceneSlot(int i)
{
    if (i < 0 || i >= kMaxScenes) return SceneSlot{};
    std::lock_guard<std::mutex> lk(cfgMx_);
    return sceneSlots_[static_cast<size_t>(i)];
}

void HueManager::setSceneSlot(int i, const SceneSlot& s)
{
    if (i < 0 || i >= kMaxScenes) return;
    std::lock_guard<std::mutex> lk(cfgMx_);
    sceneSlots_[static_cast<size_t>(i)] = s;
}

Controls HueManager::controls()
{
    std::lock_guard<std::mutex> lk(cfgMx_);
    return controls_;
}

void HueManager::setControls(const Controls& c)
{
    std::lock_guard<std::mutex> lk(cfgMx_);
    controls_ = c;
    if (controls_.transitionMs < 0)    controls_.transitionMs = 0;
    if (controls_.transitionMs > 5000) controls_.transitionMs = 5000;
}

RecLightConfig HueManager::recLight()
{
    std::lock_guard<std::mutex> lk(cfgMx_);
    return recCfg_;
}

void HueManager::setRecLight(const RecLightConfig& r)
{
    std::lock_guard<std::mutex> lk(cfgMx_);
    recCfg_ = r;
}

MarkerConfig HueManager::markers()
{
    std::lock_guard<std::mutex> lk(cfgMx_);
    return markerCfg_;
}

void HueManager::setMarkers(const MarkerConfig& m)
{
    std::lock_guard<std::mutex> lk(cfgMx_);
    markerCfg_ = m;
    if (markerCfg_.prefix.empty()) markerCfg_.prefix = "hue:";
}

int HueManager::definedCount()
{
    std::lock_guard<std::mutex> lk(cfgMx_);
    int n = 0;
    for (const SlotConfig& c : slots_)
        if (c.enabled && !c.rid.empty()) ++n;
    return n;
}

bool HueManager::slotEnabled(int i)
{
    if (i < 0 || i >= kMaxSlots) return false;
    std::lock_guard<std::mutex> lk(cfgMx_);
    const SlotConfig& c = slots_[static_cast<size_t>(i)];
    return c.enabled && !c.rid.empty();
}

// ---- strip mapping ----------------------------------------------------------

int HueManager::slotForStrip(int strip)
{
    if (strip < 0 || strip >= kMaxSlots) return -1;

    std::vector<int> order;
    {
        std::lock_guard<std::mutex> lk(cfgMx_);
        for (int i = 0; i < kMaxSlots; ++i)
            if (slots_[static_cast<size_t>(i)].enabled
                && !slots_[static_cast<size_t>(i)].rid.empty())
                order.push_back(i);
    }
    const int n = static_cast<int>(order.size());
    if (n <= 0) return -1;

    const int pos = (fill_.load() == FillDir::Right) ? (strip - (kMaxSlots - n))
                                                     : strip;
    if (pos < 0 || pos >= n) return -1;
    return order[static_cast<size_t>(pos)];
}

// ---- control ----------------------------------------------------------------

void HueManager::setFader(int i, double f01, bool released)
{
    if (i < 0 || i >= kMaxSlots) return;
    SlotLive& L = live_[static_cast<size_t>(i)];

    bool bottomOff;
    { std::lock_guard<std::mutex> lk(cfgMx_); bottomOff = controls_.bottomOff; }

    f01 = clampd(f01, 0.0, 1.0);
    if (bottomOff && f01 <= kOffZone) {
        L.on.store(false);
    } else {
        L.on.store(true);
        // 1..100: the bridge reads 0 as "lowest possible", never as off.
        L.bri.store(1.0 + f01 * 99.0);
    }
    L.dirty.store(true);
    if (released) L.flush.store(true);
}

void HueManager::setOn(int i, bool on)
{
    if (i < 0 || i >= kMaxSlots) return;
    SlotLive& L = live_[static_cast<size_t>(i)];
    L.on.store(on);
    L.dirty.store(true);
    L.flush.store(true);
}

void HueManager::toggleOn(int i)
{
    if (i < 0 || i >= kMaxSlots) return;
    setOn(i, !live_[static_cast<size_t>(i)].on.load());
}

void HueManager::nudgePot(int i, int clicks, bool flip)
{
    if (i < 0 || i >= kMaxSlots || clicks == 0) return;
    PotRole role;
    {
        std::lock_guard<std::mutex> lk(cfgMx_);
        role = flip ? controls_.potFlip : controls_.pot;
    }
    nudgeAxis(i, clicks, role);
}

void HueManager::nudgeAxis(int i, int clicks, PotRole role)
{
    if (i < 0 || i >= kMaxSlots || clicks == 0) return;
    SlotLive& L = live_[static_cast<size_t>(i)];

    switch (role) {
        case PotRole::Hue:
            // Three degrees a detent: a full turn of the wheel walks most of the
            // way round the circle without making a single click invisible.
            L.hueDeg.store(wrapDeg(L.hueDeg.load() + clicks * 3.0));
            L.white.store(false);
            break;
        case PotRole::Saturation:
            L.sat.store(clampd(L.sat.load() + clicks * 0.02, 0.0, 1.0));
            L.white.store(false);
            break;
        case PotRole::Warmth:
            L.warm.store(clampd(L.warm.load() + clicks * 0.02, 0.0, 1.0));
            L.white.store(true);
            break;
        case PotRole::Off:
            return;
    }
    // No flush: a fast spin would then bypass the gap and flood the bridge.
    // The gap plus dynamics.duration turns the detents into a smooth sweep.
    L.dirty.store(true);
}

void HueManager::pushPot(int i)
{
    if (i < 0 || i >= kMaxSlots) return;
    SlotLive& L = live_[static_cast<size_t>(i)];

    PushRole role;
    { std::lock_guard<std::mutex> lk(cfgMx_); role = controls_.push; }

    switch (role) {
        case PushRole::WhiteToggle:
            L.white.store(!L.white.load());
            L.dirty.store(true);
            L.flush.store(true);
            break;
        case PushRole::OnOff:
            toggleOn(i);
            break;
        case PushRole::Off:
            break;
    }
}

void HueManager::soloLight(int i)
{
    for (int j = 0; j < kMaxSlots; ++j) {
        if (!slotEnabled(j)) continue;
        SlotLive& L = live_[static_cast<size_t>(j)];
        L.on.store(j == i);
        L.dirty.store(true);
        L.flush.store(true);
    }
}

void HueManager::allOff()
{
    for (int j = 0; j < kMaxSlots; ++j) {
        if (!slotEnabled(j)) continue;
        SlotLive& L = live_[static_cast<size_t>(j)];
        L.on.store(false);
        L.dirty.store(true);
        L.flush.store(true);
    }
}

// ---- feedback ---------------------------------------------------------------

bool HueManager::liveOn(int i)
{
    if (i < 0 || i >= kMaxSlots) return false;
    return live_[static_cast<size_t>(i)].on.load();
}

double HueManager::liveBri01(int i)
{
    if (i < 0 || i >= kMaxSlots) return 0.0;
    const SlotLive& L = live_[static_cast<size_t>(i)];
    if (!L.on.load()) return 0.0;
    return clampd((L.bri.load() - 1.0) / 99.0, 0.0, 1.0);
}

double HueManager::liveHueDeg(int i)
{
    if (i < 0 || i >= kMaxSlots) return 0.0;
    return live_[static_cast<size_t>(i)].hueDeg.load();
}

double HueManager::liveSat(int i)
{
    if (i < 0 || i >= kMaxSlots) return 0.0;
    return live_[static_cast<size_t>(i)].sat.load();
}

double HueManager::liveWarm(int i)
{
    if (i < 0 || i >= kMaxSlots) return 0.0;
    return live_[static_cast<size_t>(i)].warm.load();
}

bool HueManager::liveWhite(int i)
{
    if (i < 0 || i >= kMaxSlots) return false;
    return live_[static_cast<size_t>(i)].white.load();
}

uint32_t HueManager::liveRgb24(int i)
{
    if (i < 0 || i >= kMaxSlots) return 0u;
    const SlotLive& L = live_[static_cast<size_t>(i)];
    // A dark bar for a dark lamp. The colour bar carries no brightness of its
    // own, so "off" has to be a colour or it reads as "on and red".
    if (!L.on.load()) return 0x000000u;
    if (L.white.load()) return mirekToRgb24(mirekFromWarmth(L.warm.load()));
    return xyToRgb24(hueSatToXy(L.hueDeg.load(), L.sat.load()));
}

bool HueManager::liveReachable(int i)
{
    if (i < 0 || i >= kMaxSlots) return false;
    return live_[static_cast<size_t>(i)].reachable.load();
}

std::string HueManager::liveValueLine(int i)
{
    if (i < 0 || i >= kMaxSlots) return std::string();
    const SlotLive& L = live_[static_cast<size_t>(i)];
    char b[32];
    if (!L.reachable.load()) {
        std::snprintf(b, sizeof(b), "OFFLINE");
    } else if (!L.on.load()) {
        std::snprintf(b, sizeof(b), "OFF");
    } else if (L.white.load()) {
        std::snprintf(b, sizeof(b), "B%-3d CT%d",
                      static_cast<int>(L.bri.load() + 0.5),
                      mirekFromWarmth(L.warm.load()));
    } else {
        std::snprintf(b, sizeof(b), "B%-3d H%-3d S%d",
                      static_cast<int>(L.bri.load() + 0.5),
                      static_cast<int>(L.hueDeg.load() + 0.5),
                      static_cast<int>(L.sat.load() * 100.0 + 0.5));
    }
    std::string s = b;
    if (s.size() > 19) s.resize(19);
    return s;
}

// ---- scenes -----------------------------------------------------------------

void HueManager::recallSceneSlot(int slot, bool dynamic)
{
    if (slot < 0 || slot >= kMaxScenes) return;
    std::string id;
    int duration;
    {
        std::lock_guard<std::mutex> lk(cfgMx_);
        id = sceneSlots_[static_cast<size_t>(slot)].id;
        duration = controls_.transitionMs;
        if (id.empty()) return;
        sceneQueue_.push_back(SceneReq{ id, dynamic, duration });
    }
    sceneReq_.store(true);
}

bool HueManager::recallSceneByName(const std::string& name, bool dynamic,
                                   int durationMs)
{
    if (name.empty()) return false;
    std::string id;
    {
        std::lock_guard<std::mutex> lk(cfgMx_);
        for (const Scene& s : scenes_) {
            if (s.name == name) { id = s.id; break; }
        }
        if (id.empty()) return false;
        sceneQueue_.push_back(SceneReq{ id, dynamic, durationMs });
    }
    sceneReq_.store(true);
    return true;
}

std::string HueManager::sceneSlotName(int slot)
{
    if (slot < 0 || slot >= kMaxScenes) return std::string();
    std::lock_guard<std::mutex> lk(cfgMx_);
    const SceneSlot& s = sceneSlots_[static_cast<size_t>(slot)];
    if (!s.label.empty()) return s.label;
    for (const Scene& sc : scenes_)
        if (sc.id == s.id) return sc.name;
    return s.bridgeName;
}

// ---- recording light --------------------------------------------------------

void HueManager::recordingStarted()
{
    if (recHeld_.exchange(true)) return;   // already held, do not re-snapshot
    recReq_.store(1);
}

void HueManager::recordingStopped()
{
    if (!recHeld_.exchange(false)) return;
    recReq_.store(-1);
}

// ---- persistence ------------------------------------------------------------

std::string HueManager::serialize()
{
    std::lock_guard<std::mutex> lk(cfgMx_);
    std::string out = "V1\n";

    out += "ip\t" + sanitize(ip_) + "\t" + sanitize(bridgeId_) + "\n";
    out += std::string("fill\t")
         + (fill_.load() == FillDir::Right ? "1" : "0") + "\n";

    char b[256];
    std::snprintf(b, sizeof(b), "ctl\t%d\t%d\t%d\t%d\t%d\n",
                  static_cast<int>(controls_.pot),
                  static_cast<int>(controls_.potFlip),
                  static_cast<int>(controls_.push),
                  controls_.bottomOff ? 1 : 0,
                  controls_.transitionMs);
    out += b;

    std::snprintf(b, sizeof(b), "rec\t%d\t%d\t%u\t%d\t%d\t",
                  recCfg_.enabled ? 1 : 0,
                  static_cast<int>(recCfg_.target),
                  recCfg_.rgb,
                  static_cast<int>(recCfg_.brightness + 0.5),
                  static_cast<int>(recCfg_.restore));
    out += b;
    out += sanitize(recCfg_.groupId) + "\t"
         + sanitize(recCfg_.restoreSceneId) + "\n";

    std::snprintf(b, sizeof(b), "mrk\t%d\t%d\t",
                  markerCfg_.enabled ? 1 : 0, markerCfg_.durationMs);
    out += b;
    out += sanitize(markerCfg_.prefix) + "\n";

    for (int i = 0; i < kMaxSlots; ++i) {
        const SlotConfig& c = slots_[static_cast<size_t>(i)];
        std::snprintf(b, sizeof(b), "slot\t%d\t%d\t%d\t%d\t%d\t",
                      i, c.enabled ? 1 : 0, static_cast<int>(c.kind),
                      c.colour, c.recLight ? 1 : 0);
        out += b;
        out += sanitize(c.rid) + "\t" + sanitize(c.groupId) + "\t"
             + sanitize(c.label) + "\t" + sanitize(c.bridgeName) + "\n";
    }

    for (int i = 0; i < kMaxScenes; ++i) {
        const SceneSlot& s = sceneSlots_[static_cast<size_t>(i)];
        std::snprintf(b, sizeof(b), "scene\t%d\t%u\t", i, s.rgb);
        out += b;
        out += sanitize(s.id) + "\t" + sanitize(s.label) + "\t"
             + sanitize(s.bridgeName) + "\n";
    }
    return out;
}

void HueManager::deserialize(const std::string& s)
{
    if (s.empty()) return;
    {
        std::lock_guard<std::mutex> lk(cfgMx_);
        slots_.fill(SlotConfig{});
        sceneSlots_.fill(SceneSlot{});
    }

    size_t pos = 0;
    while (pos <= s.size()) {
        const size_t nl = s.find('\n', pos);
        const std::string line =
            s.substr(pos, (nl == std::string::npos) ? std::string::npos : nl - pos);
        pos = (nl == std::string::npos) ? s.size() + 1 : nl + 1;
        if (line.empty()) continue;

        const std::vector<std::string> f = splitTabs(line);
        std::lock_guard<std::mutex> lk(cfgMx_);

        if (f[0] == "ip" && f.size() >= 3) {
            ip_       = f[1];
            bridgeId_ = f[2];
        } else if (f[0] == "fill" && f.size() >= 2) {
            fill_.store(toInt(f[1]) ? FillDir::Right : FillDir::Left);
        } else if (f[0] == "ctl" && f.size() >= 6) {
            controls_.pot          = static_cast<PotRole>(toInt(f[1]));
            controls_.potFlip      = static_cast<PotRole>(toInt(f[2]));
            controls_.push         = static_cast<PushRole>(toInt(f[3]));
            controls_.bottomOff    = toInt(f[4]) != 0;
            controls_.transitionMs = toInt(f[5], 100);
        } else if (f[0] == "rec" && f.size() >= 8) {
            recCfg_.enabled        = toInt(f[1]) != 0;
            recCfg_.target         = static_cast<RecTarget>(toInt(f[2]));
            recCfg_.rgb            = static_cast<uint32_t>(
                                        std::strtoul(f[3].c_str(), nullptr, 10));
            recCfg_.brightness     = toInt(f[4], 100);
            recCfg_.restore        = static_cast<RecRestore>(toInt(f[5]));
            recCfg_.groupId        = f[6];
            recCfg_.restoreSceneId = f[7];
        } else if (f[0] == "mrk" && f.size() >= 4) {
            markerCfg_.enabled    = toInt(f[1]) != 0;
            markerCfg_.durationMs = toInt(f[2], 400);
            markerCfg_.prefix     = f[3].empty() ? "hue:" : f[3];
        } else if (f[0] == "slot" && f.size() >= 10) {
            const int i = toInt(f[1]);
            if (i < 0 || i >= kMaxSlots) continue;
            SlotConfig& c = slots_[static_cast<size_t>(i)];
            c.enabled    = toInt(f[2]) != 0;
            c.kind       = static_cast<TargetKind>(toInt(f[3]));
            c.colour     = toInt(f[4]);
            c.recLight   = toInt(f[5]) != 0;
            c.rid        = f[6];
            c.groupId    = f[7];
            c.label      = f[8];
            c.bridgeName = f[9];
        } else if (f[0] == "scene" && f.size() >= 6) {
            const int i = toInt(f[1]);
            if (i < 0 || i >= kMaxScenes) continue;
            SceneSlot& sl = sceneSlots_[static_cast<size_t>(i)];
            sl.rgb        = static_cast<uint32_t>(
                                std::strtoul(f[2].c_str(), nullptr, 10));
            sl.id         = f[3];
            sl.label      = f[4];
            sl.bridgeName = f[5];
        }
    }

    if (haveAppKey() && !bridgeIp().empty()) verifyReq_.store(true);
}

std::string HueManager::serializeCredentials()
{
    std::lock_guard<std::mutex> lk(cfgMx_);
    return sanitize(appKey_) + "\t" + sanitize(clientKey_);
}

void HueManager::deserializeCredentials(const std::string& s)
{
    if (s.empty()) return;
    {
        std::lock_guard<std::mutex> lk(cfgMx_);
        const std::vector<std::string> f = splitTabs(s);
        appKey_ = f.empty() ? std::string() : f[0];
        clientKey_ = (f.size() > 1) ? f[1] : std::string();
    }
    if (!bridgeIp().empty()) verifyReq_.store(true);
}

// ---- worker: one request ----------------------------------------------------

bool HueManager::request(const char* method, const std::string& url,
                         const std::string& body, std::string* out)
{
    if (out) out->clear();

    std::vector<std::string> headers;
    headers.push_back("Content-Type: application/json");

    // Discovery is the one call that leaves the LAN, and the one that must keep
    // its certificate check — meethue.com has an ordinary public certificate.
    const bool lan = url.rfind("https://discovery.", 0) != 0;
    if (lan) {
        std::lock_guard<std::mutex> lk(cfgMx_);
        if (!appKey_.empty()) headers.push_back(appKeyHeader(appKey_));
    }

    const uint64_t id = reasixty::http::begin(method, url, headers, body,
                                              /*timeoutSeconds=*/8,
                                              /*allowUntrustedCert=*/lan);
    if (!id) return false;

    // Poll rather than block: run_ can go false while a dead bridge is still
    // timing out, and the worker has to be able to leave.
    reasixty::http::Response r;
    for (;;) {
        if (reasixty::http::poll(id, r)) break;
        if (!run_.load()) { reasixty::http::cancel(id); return false; }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (!r.error.empty() || r.status < 200 || r.status >= 300) return false;
    if (out) *out = std::move(r.body);
    return true;
}

// ---- worker: link steps -----------------------------------------------------

void HueManager::runDiscovery()
{
    setStatus(LinkState::Discovering, "Looking for a bridge");
    std::string body;
    if (!request("GET", discoveryUrl(), std::string(), &body)) {
        setStatus(LinkState::Failed,
                  "Could not reach discovery.meethue.com. Enter the IP by hand.");
        return;
    }
    std::vector<DiscoveredBridge> found = parseDiscovery(body);
    const size_t n = found.size();
    {
        std::lock_guard<std::mutex> lk(cfgMx_);
        found_ = std::move(found);
        // One bridge on the network is the normal case: take it rather than
        // making the user copy an address out of a list of one.
        if (found_.size() == 1 && ip_.empty()) {
            ip_       = found_[0].ip;
            bridgeId_ = found_[0].id;
        }
    }
    if (n == 0) {
        setStatus(LinkState::Idle,
                  "No bridge found on this network. Enter the IP by hand.");
    } else {
        setStatus(haveAppKey() ? LinkState::Verifying : LinkState::Idle,
                  std::to_string(n) + (n == 1 ? " bridge found" : " bridges found"));
        if (haveAppKey()) verifyReq_.store(true);
    }
}

void HueManager::runPairing()
{
    const std::string ip = bridgeIp();
    if (ip.empty()) {
        pairReq_.store(false);
        setStatus(LinkState::Failed, "No bridge address yet");
        return;
    }

    const int64_t left = pairDeadlineMs_.load() - nowMs();
    if (left <= 0) {
        pairReq_.store(false);
        setStatus(LinkState::Failed,
                  "The link button was not pressed. Start pairing again.");
        return;
    }

    setStatus(LinkState::Pairing,
              "Press the round button on the bridge. "
              + std::to_string((left + 999) / 1000) + "s left.");

    std::string body;
    if (!request("POST", pairUrl(ip), pairBody("rea-sixty", "surface"), &body)) {
        // Not fatal: the bridge is reachable often enough that a single failed
        // POST inside the 30 s window is worth another try a second later.
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        return;
    }

    const PairResult pr = parsePairResponse(body);
    if (pr.waiting) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        return;
    }
    if (!pr.ok) {
        pairReq_.store(false);
        setStatus(LinkState::Failed,
                  pr.error.empty() ? "The bridge refused the pairing" : pr.error);
        return;
    }

    {
        std::lock_guard<std::mutex> lk(cfgMx_);
        appKey_    = pr.appKey;
        clientKey_ = pr.clientKey;
    }
    pairReq_.store(false);
    verifyReq_.store(true);
    setStatus(LinkState::Verifying, "Paired. Reading the bridge.");
}

void HueManager::runVerify()
{
    const std::string ip = bridgeIp();
    if (ip.empty() || !haveAppKey()) {
        setStatus(LinkState::Idle, "No bridge");
        return;
    }

    std::string body;
    if (!request("GET", resourceUrl(ip, kTypeBridge), std::string(), &body)) {
        setStatus(LinkState::Failed, "The bridge did not answer at " + ip);
        return;
    }

    const std::string id = parseBridgeId(body);
    if (id.empty()) {
        setStatus(LinkState::Failed, "That address did not answer like a bridge");
        return;
    }

    // ⛔ THE ONLY IDENTITY CHECK LEFT. The transport is certificate-blind for
    // this host (HttpClient.h), so a different bridge id at the same address is
    // the one signal that something else picked up the lease.
    std::string known;
    { std::lock_guard<std::mutex> lk(cfgMx_); known = bridgeId_; }
    if (!known.empty() && known != id) {
        setStatus(LinkState::Failed,
                  "A different bridge answered at " + ip
                  + ". Pair again if this is the one you want.");
        return;
    }
    { std::lock_guard<std::mutex> lk(cfgMx_); bridgeId_ = id; }

    runRefresh();
}

void HueManager::runRefresh()
{
    const std::string ip = bridgeIp();
    if (ip.empty() || !haveAppKey()) return;

    std::string body;
    if (!request("GET", resourceUrl(ip, kTypeLight), std::string(), &body)) {
        setStatus(LinkState::Failed, "Lost the bridge at " + ip);
        for (SlotLive& L : live_) L.reachable.store(false);
        return;
    }
    std::vector<Light> lights = parseLights(body);

    std::vector<Group> groups;
    if (request("GET", resourceUrl(ip, kTypeRoom), std::string(), &body))
        groups = parseGroups(body, /*zones=*/false);
    if (request("GET", resourceUrl(ip, kTypeZone), std::string(), &body)) {
        std::vector<Group> zones = parseGroups(body, /*zones=*/true);
        groups.insert(groups.end(), zones.begin(), zones.end());
    }

    std::vector<Scene> scenes;
    if (request("GET", resourceUrl(ip, kTypeScene), std::string(), &body))
        scenes = parseScenes(body);

    const size_t nl = lights.size(), ng = groups.size();
    {
        std::lock_guard<std::mutex> lk(cfgMx_);
        lights_ = std::move(lights);
        groups_ = std::move(groups);
        scenes_ = std::move(scenes);
    }

    // ⛔ EVERY enabled slot counts as reachable once the bridge answered, not
    // only the ones the adopt loop below walks. That loop only visits single
    // LIGHT slots (a group has no per-lamp state to adopt), so a zone slot never
    // got its flag set and its value line read OFFLINE for ever, on a bridge that
    // was answering perfectly well.
    for (int i = 0; i < kMaxSlots; ++i) {
        bool en;
        {
            std::lock_guard<std::mutex> lk(cfgMx_);
            const SlotConfig& c = slots_[static_cast<size_t>(i)];
            en = c.enabled && !c.rid.empty();
        }
        if (en) live_[static_cast<size_t>(i)].reachable.store(true);
    }

    setStatus(LinkState::Online,
              std::to_string(nl) + " lights, " + std::to_string(ng) + " rooms");
}

// ---- worker: pushing a slot -------------------------------------------------

void HueManager::pushSlot(int i, bool /*flushing*/)
{
    SlotConfig cfg;
    int transition;
    std::string ip;
    Gamut gamut;
    {
        std::lock_guard<std::mutex> lk(cfgMx_);
        cfg        = slots_[static_cast<size_t>(i)];
        transition = controls_.transitionMs;
        ip         = ip_;
        if (cfg.kind == TargetKind::Light) {
            for (const Light& L : lights_)
                if (L.id == cfg.rid) { gamut = L.gamut; break; }
        }
    }
    if (ip.empty() || cfg.rid.empty()) return;

    SlotLive& S = live_[static_cast<size_t>(i)];

    LightWrite w;
    w.setOn      = true;
    w.on         = S.on.load();
    w.durationMs = transition;
    if (w.on) {
        w.setBri     = true;
        w.briPercent = S.bri.load();
        if (S.white.load()) {
            w.setMirek = true;
            w.mirek    = mirekFromWarmth(S.warm.load());
        } else {
            w.setXy = true;
            // Clamped here rather than left to the bridge so the colour bar can
            // show what the lamp will actually produce. A group has no single
            // gamut, so its members are left to clamp themselves.
            w.xy = clampToGamut(hueSatToXy(S.hueDeg.load(), S.sat.load()), gamut);
        }
    }

    const std::string url =
        resourceUrl(ip, typeForKind(cfg.kind), cfg.rid);
    const bool ok = request("PUT", url, lightBody(w), nullptr);
    S.reachable.store(ok);
}

// ---- worker: recording light ------------------------------------------------

std::vector<std::string> HueManager::recLightLightIds()
{
    std::lock_guard<std::mutex> lk(cfgMx_);
    std::vector<std::string> out;

    auto addGroupMembers = [&](const std::string& groupId) {
        for (const Group& g : groups_) {
            if (g.id != groupId) continue;
            for (const Light& L : lights_) {
                // A room lists device rids, a zone lists the light services
                // themselves. Matching both means one loop serves both.
                const bool member =
                    std::find(g.childRids.begin(), g.childRids.end(), L.ownerRid)
                        != g.childRids.end()
                    || std::find(g.childRids.begin(), g.childRids.end(), L.id)
                        != g.childRids.end();
                if (member) out.push_back(L.id);
            }
            break;
        }
    };

    if (recCfg_.target == RecTarget::Group) {
        addGroupMembers(recCfg_.groupId);
    } else {
        for (const SlotConfig& c : slots_) {
            if (!c.enabled || !c.recLight || c.rid.empty()) continue;
            if (c.kind == TargetKind::Light) out.push_back(c.rid);
            else                             addGroupMembers(c.groupId);
        }
    }

    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

void HueManager::applyRecLight(bool on)
{
    RecLightConfig cfg;
    int transition;
    std::string ip;
    {
        std::lock_guard<std::mutex> lk(cfgMx_);
        cfg        = recCfg_;
        transition = controls_.transitionMs;
        ip         = ip_;
    }
    if (!cfg.enabled || ip.empty()) return;

    if (on) {
        // Fresh read first: the cached list is up to two seconds old, and this
        // snapshot is the only thing the restore has to work from.
        runRefresh();

        const std::vector<std::string> ids = recLightLightIds();
        std::vector<Saved> saved;
        {
            std::lock_guard<std::mutex> lk(cfgMx_);
            for (const std::string& id : ids) {
                for (const Light& L : lights_) {
                    if (L.id != id) continue;
                    saved.push_back(Saved{ L.id, L.on, L.briPercent,
                                           L.hasXy, L.xy, L.hasMirek, L.mirek });
                    break;
                }
            }
            recSaved_ = saved;
        }

        // Apply: one broadcast for a group target (instant, which is what a red
        // light wants to be), one write per marked slot otherwise.
        const double r = static_cast<double>((cfg.rgb >> 16) & 0xFF) / 255.0;
        const double g = static_cast<double>((cfg.rgb >> 8)  & 0xFF) / 255.0;
        const double b = static_cast<double>( cfg.rgb        & 0xFF) / 255.0;

        LightWrite w;
        w.setOn = true; w.on = true;
        w.setBri = true; w.briPercent = cfg.brightness;
        w.setXy = true;  w.xy = rgbToXy(r, g, b);
        w.durationMs = transition;
        const std::string body = lightBody(w);

        if (cfg.target == RecTarget::Group) {
            std::string glid;
            {
                std::lock_guard<std::mutex> lk(cfgMx_);
                for (const Group& gr : groups_)
                    if (gr.id == cfg.groupId) { glid = gr.groupedLightId; break; }
            }
            if (!glid.empty())
                request("PUT", resourceUrl(ip, kTypeGroupedLight, glid), body,
                        nullptr);
        } else {
            std::vector<SlotConfig> marked;
            {
                std::lock_guard<std::mutex> lk(cfgMx_);
                for (const SlotConfig& c : slots_)
                    if (c.enabled && c.recLight && !c.rid.empty())
                        marked.push_back(c);
            }
            for (const SlotConfig& c : marked) {
                request("PUT", resourceUrl(ip, typeForKind(c.kind), c.rid), body,
                        nullptr);
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(minGapMsForKind(c.kind)));
            }
        }
        return;
    }

    // ---- off: put it back the way it was -----------------------------------
    if (cfg.restore == RecRestore::Scene && !cfg.restoreSceneId.empty()) {
        request("PUT", resourceUrl(ip, kTypeScene, cfg.restoreSceneId),
                sceneRecallBody(false, transition), nullptr);
        std::lock_guard<std::mutex> lk(cfgMx_);
        recSaved_.clear();
        return;
    }

    std::vector<Saved> saved;
    { std::lock_guard<std::mutex> lk(cfgMx_); saved.swap(recSaved_); }

    // Per LIGHT, not per group: a grouped_light can be written in one broadcast
    // but never read back per lamp, so restoring through it would flatten a room
    // whose lamps were on different colours.
    for (const Saved& s : saved) {
        LightWrite w;
        w.setOn = true; w.on = s.on;
        if (s.on) {
            w.setBri = true; w.briPercent = s.bri;
            if (s.hasXy)         { w.setXy = true;    w.xy = s.xy; }
            else if (s.hasMirek) { w.setMirek = true; w.mirek = s.mirek; }
        }
        w.durationMs = transition;
        request("PUT", resourceUrl(ip, kTypeLight, s.id), lightBody(w), nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds(kLightMinGapMs));
    }
}

// ---- worker loop ------------------------------------------------------------

void HueManager::workerLoop()
{
    std::array<int64_t, kMaxSlots> lastSend{};
    lastSend.fill(0);

    // What we last put on the wire per slot, so the periodic refresh can tell an
    // external change (Hue app, wall switch) from our own echo. Adopting our own
    // echo is how a V-Pot starts creeping on its own.
    std::array<Xy, kMaxSlots>     sentXy{};
    std::array<double, kMaxSlots> sentBri{};
    sentBri.fill(-1.0);

    int64_t nextRefresh = 0;

    while (run_.load()) {
        const int64_t t = nowMs();

        if (discoverReq_.exchange(false)) runDiscovery();
        if (pairReq_.load())              runPairing();
        if (verifyReq_.exchange(false))   { runVerify(); nextRefresh = nowMs() + 2000; }

        if (const int rec = recReq_.exchange(0); rec != 0) {
            applyRecLight(rec > 0);
            nextRefresh = nowMs() + 2000;
        }

        if (sceneReq_.exchange(false)) {
            std::vector<SceneReq> reqs;
            {
                std::lock_guard<std::mutex> lk(cfgMx_);
                reqs.swap(sceneQueue_);
            }
            const std::string ip = bridgeIp();
            for (const SceneReq& q : reqs) {
                if (ip.empty()) break;
                request("PUT", resourceUrl(ip, kTypeScene, q.id),
                        sceneRecallBody(q.dynamic, q.durationMs), nullptr);
            }
            // A scene moves lamps we are showing, so pull the truth back in.
            if (!reqs.empty()) nextRefresh = nowMs() + 300;
        }

        if (link_.load() == LinkState::Online) {
            for (int i = 0; i < kMaxSlots; ++i) {
                SlotLive& S = live_[static_cast<size_t>(i)];
                if (!S.dirty.load()) continue;

                SlotConfig cfg;
                {
                    std::lock_guard<std::mutex> lk(cfgMx_);
                    cfg = slots_[static_cast<size_t>(i)];
                }
                if (!cfg.enabled || cfg.rid.empty()) { S.dirty.store(false); continue; }

                const int  gap      = minGapMsForKind(cfg.kind);
                const bool flushing = S.flush.load();
                if (!flushing && (t - lastSend[static_cast<size_t>(i)]) < gap)
                    continue;

                S.dirty.store(false);
                S.flush.store(false);
                lastSend[static_cast<size_t>(i)] = t;
                sentBri[static_cast<size_t>(i)]  = S.on.load() ? S.bri.load() : -1.0;
                sentXy[static_cast<size_t>(i)]   =
                    hueSatToXy(S.hueDeg.load(), S.sat.load());
                pushSlot(i, flushing);
            }

            if (t >= nextRefresh) {
                nextRefresh = t + 2000;
                runRefresh();

                // Adopt what the bridge reports, but only where it disagrees
                // with what we sent — see the note on sentXy above.
                std::vector<Light> snap = lights();
                for (int i = 0; i < kMaxSlots; ++i) {
                    SlotConfig cfg;
                    {
                        std::lock_guard<std::mutex> lk(cfgMx_);
                        cfg = slots_[static_cast<size_t>(i)];
                    }
                    if (!cfg.enabled || cfg.kind != TargetKind::Light) continue;

                    SlotLive& S = live_[static_cast<size_t>(i)];
                    if (S.dirty.load()) continue;   // a pending edit outranks the bridge

                    for (const Light& L : snap) {
                        if (L.id != cfg.rid) continue;
                        S.reachable.store(true);

                        const double want = sentBri[static_cast<size_t>(i)];
                        if (want < 0.0 || std::fabs(L.briPercent - want) > 1.0) {
                            S.on.store(L.on);
                            if (L.dimmable && L.briPercent > 0.0)
                                S.bri.store(L.briPercent);
                        }

                        if (L.hasXy) {
                            const Xy& s = sentXy[static_cast<size_t>(i)];
                            if (std::fabs(L.xy.x - s.x) > 0.01
                             || std::fabs(L.xy.y - s.y) > 0.01) {
                                double h = 0.0, sat = 0.0;
                                xyToHueSat(L.xy, &h, &sat);
                                S.hueDeg.store(h);
                                S.sat.store(sat);
                                S.white.store(false);
                                sentXy[static_cast<size_t>(i)] = L.xy;
                            }
                        } else if (L.hasMirek) {
                            S.warm.store(warmthFromMirek(L.mirek));
                            S.white.store(true);
                        }
                        break;
                    }
                }
            }
        } else if (link_.load() == LinkState::Failed && t >= nextRefresh) {
            // A bridge that dropped off comes back on its own; retrying every
            // ten seconds is cheap and saves a trip to the settings pane.
            nextRefresh = t + 10000;
            if (haveAppKey() && !bridgeIp().empty()) verifyReq_.store(true);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

} // namespace uf8::hue
