//
// ParameterGroups — implementation.
//
// JSON style mirrors UserPluginCatalog.cpp: WDL/jsonparse for reading,
// hand-written serializer for writing. Path:
// <REAPER_RESOURCE>/rea_sixty/parameter_groups.json.
//

#include "ParameterGroups.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <sstream>
#include <sys/stat.h>

#ifdef _WIN32
  #include <direct.h>
#else
  #include <unistd.h>
#endif

#include "reaper_plugin_functions.h"

#include "PluginMap.h"
#include "UserPluginCatalog.h"
#include "WDL/jsonparse.h"

namespace uf8::param_groups {

namespace {

std::mutex g_mutex;
State      g_state;

constexpr const char* kPExtKey = "P_EXT:reasixty:pg_mask";

// Bumped while the broadcast helpers fan out member writes. The REAPER
// CSURF_EXT_SETFXPARAM hook reads this via inBroadcast() and skips
// re-entry — our own member writes round-trip through Extended and
// would otherwise trigger another broadcast layer per write.
std::atomic<int>     g_broadcastDepth{0};
// Wall-clock ms of the most recent broadcast end. chaseLastTouchedFx
// uses this to suppress UC1 focus jumps while member writes are still
// updating REAPER's GetLastTouchedFX state. INT64_MIN = "never ran".
std::atomic<int64_t> g_lastBroadcastEndMs{(std::numeric_limits<int64_t>::min)()};

int64_t nowMs_()
{
    using clock = std::chrono::steady_clock;
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        clock::now().time_since_epoch()).count();
}

struct ScopedSuppress {
    ScopedSuppress()  { g_broadcastDepth.fetch_add(1, std::memory_order_acq_rel); }
    ~ScopedSuppress() {
        g_broadcastDepth.fetch_sub(1, std::memory_order_acq_rel);
        g_lastBroadcastEndMs.store(nowMs_(), std::memory_order_release);
    }
    ScopedSuppress(const ScopedSuppress&)            = delete;
    ScopedSuppress& operator=(const ScopedSuppress&) = delete;
};

// ---- P_EXT helpers --------------------------------------------------------

uint8_t readMask_(MediaTrack* tr)
{
    if (!tr) return 0;
    char buf[16] = {0};
    GetSetMediaTrackInfo_String(tr, const_cast<char*>(kPExtKey), buf, false);
    if (buf[0] == 0) return 0;
    const long v = std::strtol(buf, nullptr, 10);
    if (v <= 0 || v > 255) return 0;
    return static_cast<uint8_t>(v);
}

void writeMask_(MediaTrack* tr, uint8_t mask)
{
    if (!tr) return;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(mask));
    GetSetMediaTrackInfo_String(tr, const_cast<char*>(kPExtKey), buf, true);
}

uint8_t activeGroupBits_()
{
    uint8_t m = 0;
    for (int i = 0; i < kSlotCount; ++i) {
        if (g_state.slots[i].active) m |= static_cast<uint8_t>(1u << i);
    }
    return m;
}

// ---- Path / file helpers --------------------------------------------------

std::string configDir_()
{
    const char* base = GetResourcePath ? GetResourcePath() : nullptr;
    if (!base || !*base) base = ".";
    std::string d = base;
    d += "/rea_sixty";
    return d;
}

std::string configPath_()
{
    return configDir_() + "/parameter_groups.json";
}

void ensureConfigDir_()
{
    const std::string d = configDir_();
    struct stat st{};
    if (stat(d.c_str(), &st) == 0) return;
#ifdef _WIN32
    _mkdir(d.c_str());
#else
    mkdir(d.c_str(), 0755);
#endif
}

bool readFile_(const std::string& path, std::string& out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n < 0) { std::fclose(f); return false; }
    out.resize(static_cast<size_t>(n));
    if (n > 0) std::fread(out.data(), 1, static_cast<size_t>(n), f);
    std::fclose(f);
    return true;
}

bool writeFileAtomic_(const std::string& path, const std::string& contents)
{
    const std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    if (!contents.empty()
        && std::fwrite(contents.data(), 1, contents.size(), f) != contents.size())
    {
        std::fclose(f);
        std::remove(tmp.c_str());
        return false;
    }
    std::fclose(f);
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

void appendEscaped_(std::ostringstream& os, const std::string& s)
{
    os << '"';
    for (char c : s) {
        switch (c) {
            case '"':  os << "\\\""; break;
            case '\\': os << "\\\\"; break;
            case '\n': os << "\\n";  break;
            case '\r': os << "\\r";  break;
            case '\t': os << "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned>(c));
                    os << buf;
                } else {
                    os << c;
                }
                break;
        }
    }
    os << '"';
}

bool getStrI_(wdl_json_element* obj, const char* key, std::string& out)
{
    if (!obj) return false;
    auto* v = obj->get_item_by_name(key);
    if (!v) return false;
    if (auto* s = v->get_string_value()) { out = s; return true; }
    return false;
}

bool getBoolI_(wdl_json_element* obj, const char* key, bool& out)
{
    if (!obj) return false;
    auto* v = obj->get_item_by_name(key);
    if (!v) return false;
    if (auto* s = v->get_string_value(true)) {
        out = (std::strcmp(s, "true") == 0 || std::strcmp(s, "1") == 0);
        return true;
    }
    return false;
}

} // anonymous namespace

// ---- Public API -----------------------------------------------------------

State& state() { return g_state; }

uint8_t getMaskForTrack(MediaTrack* tr) { return readMask_(tr); }
void    setMaskForTrack(MediaTrack* tr, uint8_t mask) { writeMask_(tr, mask); }

void addSelectedToGroup(int slotIdx)
{
    if (slotIdx < 0 || slotIdx >= kSlotCount) return;
    const uint8_t bit = static_cast<uint8_t>(1u << slotIdx);
    const int n = CountSelectedTracks(nullptr);
    for (int i = 0; i < n; ++i) {
        MediaTrack* t = GetSelectedTrack(nullptr, i);
        if (!t) continue;
        const uint8_t cur = readMask_(t);
        if ((cur & bit) == 0) writeMask_(t, static_cast<uint8_t>(cur | bit));
    }
}

void removeSelectedFromAllGroups()
{
    const int n = CountSelectedTracks(nullptr);
    for (int i = 0; i < n; ++i) {
        MediaTrack* t = GetSelectedTrack(nullptr, i);
        if (!t) continue;
        if (readMask_(t) != 0) writeMask_(t, 0);
    }
}

void clearGroupMembership(int slotIdx)
{
    if (slotIdx < 0 || slotIdx >= kSlotCount) return;
    const uint8_t bit    = static_cast<uint8_t>(1u << slotIdx);
    const uint8_t invBit = static_cast<uint8_t>(~bit);
    const int n = CountTracks(nullptr);
    for (int i = 0; i < n; ++i) {
        MediaTrack* t = GetTrack(nullptr, i);
        if (!t) continue;
        const uint8_t cur = readMask_(t);
        if (cur & bit) writeMask_(t, static_cast<uint8_t>(cur & invBit));
    }
}

void toggleGroupActive(int slotIdx)
{
    if (slotIdx < 0 || slotIdx >= kSlotCount) return;
    g_state.slots[slotIdx].active = !g_state.slots[slotIdx].active;
    save();
}

bool isGroupActive(int slotIdx)
{
    if (slotIdx < 0 || slotIdx >= kSlotCount) return false;
    return g_state.slots[slotIdx].active;
}

bool multiSelectAsTempGroup() { return g_state.multiSelectAsTempGroup; }

void setMultiSelectAsTempGroup(bool on)
{
    if (g_state.multiSelectAsTempGroup == on) return;
    g_state.multiSelectAsTempGroup = on;
    save();
}

std::vector<MediaTrack*> resolveBroadcastTargets(MediaTrack* leader)
{
    std::vector<MediaTrack*> out;
    if (!leader) return out;

    const uint8_t activeMask = activeGroupBits_();
    if (activeMask != 0) {
        const int n = CountTracks(nullptr);
        out.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            MediaTrack* t = GetTrack(nullptr, i);
            if (!t || t == leader) continue;
            if (readMask_(t) & activeMask) out.push_back(t);
        }
        return out;
    }

    if (!g_state.multiSelectAsTempGroup) return out;

    const int nSel = CountSelectedTracks(nullptr);
    if (nSel < 2) return out;

    bool leaderSelected = false;
    out.reserve(static_cast<size_t>(nSel - 1));
    for (int i = 0; i < nSel; ++i) {
        MediaTrack* t = GetSelectedTrack(nullptr, i);
        if (!t) continue;
        if (t == leader) { leaderSelected = true; continue; }
        out.push_back(t);
    }
    if (!leaderSelected) out.clear();
    return out;
}

void broadcastBuiltinSlot(MediaTrack* leader,
                          Domain domain,
                          int slotLinkIdx,
                          double normValue)
{
    if (domain == Domain::None) return;
    auto targets = resolveBroadcastTargets(leader);
    if (targets.empty()) return;
    ScopedSuppress guard;
    for (auto* t : targets) {
        auto match = lookupPluginOnTrack(t, domain);
        if (!match.map) continue;
        const LinkSlot* sl = findSlotByLinkIdx(*match.map, slotLinkIdx);
        if (!sl || sl->vst3Param < 0) continue;
        TrackFX_SetParamNormalized(t, match.fxIndex, sl->vst3Param, normValue);
    }
    // Cement leader as last-touched-FX so chaseLastTouchedFx doesn't
    // chase the last member we just wrote and jump focus around. The
    // re-write is idempotent (same value) and Extended skips it because
    // we're still inside ScopedSuppress.
    if (auto leaderMatch = lookupPluginOnTrack(leader, domain);
        leaderMatch.map)
    {
        const LinkSlot* sl =
            findSlotByLinkIdx(*leaderMatch.map, slotLinkIdx);
        if (sl && sl->vst3Param >= 0) {
            TrackFX_SetParamNormalized(
                leader, leaderMatch.fxIndex, sl->vst3Param, normValue);
        }
    }
}

void broadcastUserParam(MediaTrack* leader,
                        const UserPluginMap* leaderMap,
                        int vst3Param,
                        double normValue)
{
    if (!leaderMap || vst3Param < 0 || leaderMap->match.empty()) return;
    auto targets = resolveBroadcastTargets(leader);
    if (targets.empty()) return;
    ScopedSuppress guard;
    for (auto* t : targets) {
        const int nfx = TrackFX_GetCount(t);
        char fxName[256];
        for (int i = 0; i < nfx; ++i) {
            if (!uf8::fxIdentityName(t, i, fxName, sizeof(fxName))) continue;
            if (std::strstr(fxName, leaderMap->match.c_str()) == nullptr) continue;
            TrackFX_SetParamNormalized(t, i, vst3Param, normValue);
            break;
        }
    }
    // Cement leader as last-touched (see broadcastBuiltinSlot above).
    const int nfx = TrackFX_GetCount(leader);
    char fxName[256];
    for (int i = 0; i < nfx; ++i) {
        if (!uf8::fxIdentityName(leader, i, fxName, sizeof(fxName))) continue;
        if (std::strstr(fxName, leaderMap->match.c_str()) == nullptr) continue;
        TrackFX_SetParamNormalized(leader, i, vst3Param, normValue);
        break;
    }
}

void broadcastTrackBool(MediaTrack* leader,
                        const char* attrName, double value)
{
    if (!attrName) return;
    auto targets = resolveBroadcastTargets(leader);
    if (targets.empty()) return;
    ScopedSuppress guard;
    for (auto* t : targets) {
        SetMediaTrackInfo_Value(t, const_cast<char*>(attrName), value);
    }
}

void broadcastSoloMute(MediaTrack* leader, bool isSolo, int absoluteValue)
{
    auto targets = resolveBroadcastTargets(leader);
    if (targets.empty()) return;
    ScopedSuppress guard;
    for (auto* t : targets) {
        if (isSolo) CSurf_OnSoloChange(t, absoluteValue);
        else        CSurf_OnMuteChange(t, absoluteValue);
    }
}

void broadcastTrackVolumeLinear(MediaTrack* leader, double linValue)
{
    auto targets = resolveBroadcastTargets(leader);
    if (targets.empty()) return;
    ScopedSuppress guard;
    for (auto* t : targets) {
        CSurf_OnVolumeChange(t, linValue, false);
    }
}

bool inBroadcast()
{
    return g_broadcastDepth.load(std::memory_order_acquire) > 0;
}

int64_t millisSinceLastBroadcast()
{
    const int64_t end = g_lastBroadcastEndMs.load(std::memory_order_acquire);
    if (end == (std::numeric_limits<int64_t>::min)()) {
        return (std::numeric_limits<int64_t>::max)();
    }
    const int64_t dt = nowMs_() - end;
    return dt < 0 ? 0 : dt;
}

// ---- Persistence ----------------------------------------------------------

void load()
{
    std::lock_guard<std::mutex> lk(g_mutex);

    // Reset to defaults first so a missing/empty file gives a clean slate.
    g_state = State{};

    std::string contents;
    if (!readFile_(configPath_(), contents) || contents.empty()) return;

    wdl_json_parser p;
    wdl_json_element* root = p.parse(contents.c_str(),
                                     static_cast<int>(contents.size()));
    if (!root || !root->is_object()) return;

    bool b = false;
    if (getBoolI_(root, "multi_select_as_temp_group", b))
        g_state.multiSelectAsTempGroup = b;

    if (auto* arr = root->get_item_by_name("slots");
        arr && arr->is_array() && arr->m_array)
    {
        const int n = std::min<int>(arr->m_array->GetSize(), kSlotCount);
        for (int i = 0; i < n; ++i) {
            wdl_json_element* so = arr->enum_item(i);
            if (!so || !so->is_object()) continue;
            getStrI_(so, "name", g_state.slots[i].name);
            bool a = false;
            if (getBoolI_(so, "active", a)) g_state.slots[i].active = a;
        }
    }
}

void save()
{
    // No mutex re-entry: this is the only writer, called from main-thread
    // or from input-thread dispatchers; either way single-threaded per call.
    std::ostringstream os;
    os << "{\n";
    os << "  \"format_version\": 1,\n";
    os << "  \"multi_select_as_temp_group\": "
       << (g_state.multiSelectAsTempGroup ? "true" : "false") << ",\n";
    os << "  \"slots\": [\n";
    for (int i = 0; i < kSlotCount; ++i) {
        os << "    { \"name\": ";
        appendEscaped_(os, g_state.slots[i].name);
        os << ", \"active\": "
           << (g_state.slots[i].active ? "true" : "false") << " }";
        if (i + 1 < kSlotCount) os << ",";
        os << "\n";
    }
    os << "  ]\n";
    os << "}\n";

    ensureConfigDir_();
    writeFileAtomic_(configPath_(), os.str());
}

} // namespace uf8::param_groups
