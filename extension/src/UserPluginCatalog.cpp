//
// UserPluginCatalog — runtime catalogue of user-learned plugin maps.
//
// JSON style mirrors Bindings.cpp: WDL/jsonparse for reading, hand-written
// serializer for writing. Path: <REAPER_RESOURCE>/rea_sixty/user_plugins.json.
// Sibling to bindings.json on purpose — same lifecycle, same backup story.
//
// Atomic write: serialise to <path>.tmp, rename onto <path>. Pre-write
// backup to <path>.bak when the destination already exists.
//

#include "UserPluginCatalog.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <sys/stat.h>

#ifdef _WIN32
  #include <direct.h>
  #include <io.h>
#else
  #include <unistd.h>
#endif

#include "reaper_plugin_functions.h"

#include "WDL/jsonparse.h"

namespace uf8::user_plugins {

namespace {

std::mutex          g_mutex;
UserPluginCatalog   g_catalog;
std::atomic<int>    g_generation{0};   // bumped on every mutation

// View cache: synthesised PluginMap structs returned from lookupByName.
// Rebuilt on every mutation so spans stay valid until the next change.
struct ViewCacheEntry {
    std::string             match;        // owns string memory
    std::string             displayShort;
    PluginMap               map;          // points into the strings + slotsBuf
    std::vector<LinkSlot>   slotsBuf;
};
std::vector<ViewCacheEntry> g_viewCache;

// ---- path helpers ----------------------------------------------------------

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
    return configDir_() + "/user_plugins.json";
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

bool fileExists_(const std::string& path)
{
    struct stat st{};
    return stat(path.c_str(), &st) == 0;
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
    if (!contents.empty()) {
        if (std::fwrite(contents.data(), 1, contents.size(), f) != contents.size()) {
            std::fclose(f);
            std::remove(tmp.c_str());
            return false;
        }
    }
    std::fclose(f);

    if (fileExists_(path)) {
        const std::string bak = path + ".bak";
        // Best-effort backup; ignore failure (rename across the same
        // directory is cheap and rarely fails — but if it does, we'd
        // rather still write the new file than block the save).
        std::remove(bak.c_str());
        std::rename(path.c_str(), bak.c_str());
    }

    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

void logErr_(const char* fmt, ...)
{
#ifdef _WIN32
    const char* logPath = "rea_sixty.log";
#else
    const char* logPath = "/tmp/rea_sixty.log";
#endif
    if (FILE* lf = std::fopen(logPath, "a")) {
        va_list ap;
        va_start(ap, fmt);
        std::fprintf(lf, "[user_plugins] ");
        std::vfprintf(lf, fmt, ap);
        std::fprintf(lf, "\n");
        va_end(ap);
        std::fclose(lf);
    }
}

// ---- JSON helpers ----------------------------------------------------------

const char* domainName_(Domain d)
{
    switch (d) {
        case Domain::ChannelStrip: return "ChannelStrip";
        case Domain::BusComp:      return "BusComp";
        default:                   return "None";
    }
}

Domain domainFromName_(const char* s)
{
    if (!s) return Domain::None;
    if (std::strcmp(s, "ChannelStrip") == 0) return Domain::ChannelStrip;
    if (std::strcmp(s, "BusComp")      == 0) return Domain::BusComp;
    return Domain::None;
}

const char* vpotModeName_(VPotMode m)
{
    return m == VPotMode::Toggle ? "Toggle" : "Value";
}

VPotMode vpotModeFromName_(const char* s)
{
    if (s && std::strcmp(s, "Toggle") == 0) return VPotMode::Toggle;
    return VPotMode::Value;
}

bool uf8MapHasContent_(const UserUf8Map& u)
{
    for (int s = 0; s < 8; ++s) {
        const auto& sb = u.strips[s];
        if (sb.faderVst3Param >= 0 || sb.soloVst3Param >= 0
         || sb.cutVst3Param   >= 0 || sb.selVst3Param  >= 0) return true;
    }
    for (int b = 0; b < uf8::kUserUf8BankCount; ++b) {
        for (int s = 0; s < 8; ++s) {
            if (u.banks.banks[b][s].vst3Param >= 0) return true;
            if (!u.banks.banks[b][s].label.empty()) return true;
        }
    }
    return false;
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
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
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

bool getIntI_(wdl_json_element* obj, const char* key, int& out)
{
    if (!obj) return false;
    auto* v = obj->get_item_by_name(key);
    if (!v) return false;
    if (auto* s = v->get_string_value(true)) { out = std::atoi(s); return true; }
    return false;
}

bool getDoubleI_(wdl_json_element* obj, const char* key, double& out)
{
    if (!obj) return false;
    auto* v = obj->get_item_by_name(key);
    if (!v) return false;
    if (auto* s = v->get_string_value(true)) { out = std::atof(s); return true; }
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

// ---- Serialise --------------------------------------------------------------

std::string serialize_(const UserPluginCatalog& c)
{
    std::ostringstream os;
    os << "{\n";
    os << "  \"format_version\": " << c.formatVersion << ",\n";
    os << "  \"plugins\": [";
    bool firstPlugin = true;
    for (const auto& m : c.maps) {
        if (!firstPlugin) os << ",";
        firstPlugin = false;
        os << "\n    {\n";
        os << "      \"match\": ";        appendEscaped_(os, m.match);        os << ",\n";
        os << "      \"domain\": ";       appendEscaped_(os, domainName_(m.domain)); os << ",\n";
        os << "      \"displayShort\": "; appendEscaped_(os, m.displayShort); os << ",\n";
        os << "      \"isDefault\": "     << (m.isDefault ? "true" : "false") << ",\n";
        os << "      \"uf8Mode\": "       << (m.uf8Mode   ? "true" : "false") << ",\n";
        if (m.snapshotTakenAt > 0)
            os << "      \"snapshotTakenAt\": " << m.snapshotTakenAt << ",\n";
        os << "      \"slots\": [";
        bool firstSlot = true;
        for (const auto& s : m.slots) {
            if (!firstSlot) os << ",";
            firstSlot = false;
            os << "\n        { \"linkIdx\": " << s.linkIdx
               << ", \"vst3Param\": "         << s.vst3Param
               << ", \"inverted\": "          << (s.inverted ? "true" : "false")
               << " }";
        }
        os << "\n      ]";
        if (m.metering.grVst3Param >= 0) {
            os << ",\n      \"metering\": { \"gainReduction\": { "
               << "\"vst3Param\": " << m.metering.grVst3Param
               << ", \"offsetDb\": " << m.metering.grOffsetDb
               << " } }";
        }
        if (uf8MapHasContent_(m.uf8)) {
            os << ",\n      \"uf8\": {\n";
            os << "        \"banks\": [";
            for (int b = 0; b < uf8::kUserUf8BankCount; ++b) {
                if (b) os << ",";
                os << "\n          [";
                for (int s = 0; s < 8; ++s) {
                    if (s) os << ",";
                    const auto& bs = m.uf8.banks.banks[b][s];
                    os << "\n            { \"vst3Param\": " << bs.vst3Param
                       << ", \"label\": ";
                    appendEscaped_(os, bs.label);
                    os << ", \"vpotMode\": ";
                    appendEscaped_(os, vpotModeName_(bs.vpotMode));
                    os << ", \"inverted\": " << (bs.inverted ? "true" : "false")
                       << ", \"defaultNorm\": " << bs.defaultNorm
                       << ", \"colour\": " << static_cast<unsigned>(bs.colour)
                       << " }";
                }
                os << "\n          ]";
            }
            os << "\n        ],\n";
            os << "        \"strips\": [";
            for (int s = 0; s < 8; ++s) {
                if (s) os << ",";
                const auto& sb = m.uf8.strips[s];
                os << "\n          { "
                   << "\"fader\": { \"vst3Param\": " << sb.faderVst3Param
                   << ", \"inverted\": " << (sb.faderInverted ? "true" : "false")
                   << " }, "
                   << "\"solo\": { \"vst3Param\": " << sb.soloVst3Param
                   << ", \"colour\": " << static_cast<unsigned>(sb.soloColour)
                   << " }, "
                   << "\"cut\": { \"vst3Param\": "  << sb.cutVst3Param
                   << ", \"colour\": " << static_cast<unsigned>(sb.cutColour)
                   << " }, "
                   << "\"sel\": { \"vst3Param\": "  << sb.selVst3Param
                   << ", \"colour\": " << static_cast<unsigned>(sb.selColour)
                   << " }"
                   << " }";
            }
            os << "\n        ]\n      }";
        }
        if (!m.paramSnapshot.empty()) {
            os << ",\n      \"paramSnapshot\": [";
            bool firstParam = true;
            for (const auto& p : m.paramSnapshot) {
                if (!firstParam) os << ",";
                firstParam = false;
                os << "\n        { \"vst3Param\": " << p.vst3Param
                   << ", \"name\": ";
                appendEscaped_(os, p.name);
                os << ", \"defaultNorm\": " << p.defaultNorm
                   << ", \"wasEnum\": " << (p.wasEnum ? "true" : "false")
                   << " }";
            }
            os << "\n      ]";
        }
        os << "\n    }";
    }
    if (!firstPlugin) os << "\n  ";
    os << "]\n}\n";
    return os.str();
}

// ---- Parse ------------------------------------------------------------------

bool parse_(const std::string& json, UserPluginCatalog& out)
{
    wdl_json_parser p;
    wdl_json_element* root = p.parse(json.c_str(), static_cast<int>(json.size()));
    if (!root || !root->is_object()) return false;

    int fv = 1;
    getIntI_(root, "format_version", fv);
    if (fv > kCurrentFormatVersion) {
        logErr_("refusing to load format_version=%d (max known=%d)",
                fv, kCurrentFormatVersion);
        return false;
    }
    out.formatVersion = fv;
    out.maps.clear();

    auto* arr = root->get_item_by_name("plugins");
    if (!arr || !arr->is_array() || !arr->m_array) return true;

    const int n = arr->m_array->GetSize();
    for (int i = 0; i < n; ++i) {
        wdl_json_element* po = arr->enum_item(i);
        if (!po || !po->is_object()) continue;

        UserPluginMap m;
        getStrI_(po, "match", m.match);
        std::string dom;
        if (getStrI_(po, "domain", dom)) m.domain = domainFromName_(dom.c_str());
        getStrI_(po, "displayShort", m.displayShort);
        // Widened from 4→7 chars 2026-05-09 — UF8 / UC1 colour-bar zone
        // accepts longer strings via length-byte protocol; older 4-char
        // values still load identically (just no padding).
        if (m.displayShort.size() > 7) m.displayShort.resize(7);
        getBoolI_(po, "isDefault", m.isDefault);
        // v4: explicit uf8Mode flag. For v3 files the key is missing — we
        // derive uf8Mode below (after the uf8 block parses) from
        // uf8MapHasContent_ so legacy maps with bank/strip bindings stay
        // UF8-enabled.
        bool uf8ModeRead = false;
        const bool hadUf8Mode = getBoolI_(po, "uf8Mode", uf8ModeRead);
        if (hadUf8Mode) m.uf8Mode = uf8ModeRead;
        int snapTs = 0;
        if (getIntI_(po, "snapshotTakenAt", snapTs))
            m.snapshotTakenAt = snapTs;

        if (auto* slotsArr = po->get_item_by_name("slots");
            slotsArr && slotsArr->is_array() && slotsArr->m_array)
        {
            const int sn = slotsArr->m_array->GetSize();
            for (int s = 0; s < sn; ++s) {
                wdl_json_element* so = slotsArr->enum_item(s);
                if (!so || !so->is_object()) continue;
                UserLinkSlot us{};
                getIntI_(so, "linkIdx", us.linkIdx);
                getIntI_(so, "vst3Param", us.vst3Param);
                getBoolI_(so, "inverted", us.inverted);
                if (us.linkIdx < 0 || us.vst3Param < 0) continue;
                m.slots.push_back(us);
            }
        }

        if (auto* met = po->get_item_by_name("metering");
            met && met->is_object())
        {
            if (auto* gr = met->get_item_by_name("gainReduction");
                gr && gr->is_object())
            {
                int gp = -1;
                getIntI_(gr, "vst3Param", gp);
                m.metering.grVst3Param = gp;
                getDoubleI_(gr, "offsetDb", m.metering.grOffsetDb);
            }
        }

        if (auto* uo = po->get_item_by_name("uf8");
            uo && uo->is_object())
        {
            if (auto* banksArr = uo->get_item_by_name("banks");
                banksArr && banksArr->is_array() && banksArr->m_array)
            {
                const int bn = (std::min)(banksArr->m_array->GetSize(),
                                          uf8::kUserUf8BankCount);
                for (int b = 0; b < bn; ++b) {
                    wdl_json_element* row = banksArr->enum_item(b);
                    if (!row || !row->is_array() || !row->m_array) continue;
                    const int sn = (std::min)(row->m_array->GetSize(), 8);
                    for (int s = 0; s < sn; ++s) {
                        wdl_json_element* so = row->enum_item(s);
                        if (!so || !so->is_object()) continue;
                        auto& bs = m.uf8.banks.banks[b][s];
                        getIntI_(so, "vst3Param", bs.vst3Param);
                        getStrI_(so, "label", bs.label);
                        std::string mode;
                        if (getStrI_(so, "vpotMode", mode))
                            bs.vpotMode = vpotModeFromName_(mode.c_str());
                        getBoolI_(so, "inverted", bs.inverted);
                        getDoubleI_(so, "defaultNorm", bs.defaultNorm);
                        int colTmp = 0;
                        if (getIntI_(so, "colour", colTmp))
                            bs.colour = static_cast<uint32_t>(colTmp) & 0x00FFFFFFu;
                    }
                }
            }
            if (auto* stripsArr = uo->get_item_by_name("strips");
                stripsArr && stripsArr->is_array() && stripsArr->m_array)
            {
                const int sn = (std::min)(stripsArr->m_array->GetSize(), 8);
                for (int s = 0; s < sn; ++s) {
                    wdl_json_element* so = stripsArr->enum_item(s);
                    if (!so || !so->is_object()) continue;
                    auto& sb = m.uf8.strips[s];
                    if (auto* fo = so->get_item_by_name("fader");
                        fo && fo->is_object())
                    {
                        getIntI_(fo, "vst3Param", sb.faderVst3Param);
                        getBoolI_(fo, "inverted", sb.faderInverted);
                    }
                    int colTmp = 0;
                    if (auto* so2 = so->get_item_by_name("solo");
                        so2 && so2->is_object())
                    {
                        getIntI_(so2, "vst3Param", sb.soloVst3Param);
                        colTmp = 0;
                        if (getIntI_(so2, "colour", colTmp))
                            sb.soloColour = static_cast<uint32_t>(colTmp) & 0x00FFFFFFu;
                    }
                    if (auto* co = so->get_item_by_name("cut");
                        co && co->is_object())
                    {
                        getIntI_(co, "vst3Param", sb.cutVst3Param);
                        colTmp = 0;
                        if (getIntI_(co, "colour", colTmp))
                            sb.cutColour = static_cast<uint32_t>(colTmp) & 0x00FFFFFFu;
                    }
                    if (auto* selo = so->get_item_by_name("sel");
                        selo && selo->is_object())
                    {
                        getIntI_(selo, "vst3Param", sb.selVst3Param);
                        colTmp = 0;
                        if (getIntI_(selo, "colour", colTmp))
                            sb.selColour = static_cast<uint32_t>(colTmp) & 0x00FFFFFFu;
                    }
                }
            }
        }

        // Parse paramSnapshot array (v4+; absent in v3 files).
        if (auto* psArr = po->get_item_by_name("paramSnapshot");
            psArr && psArr->is_array() && psArr->m_array)
        {
            const int pn = psArr->m_array->GetSize();
            m.paramSnapshot.reserve(static_cast<size_t>(pn));
            for (int p = 0; p < pn; ++p) {
                wdl_json_element* po2 = psArr->enum_item(p);
                if (!po2 || !po2->is_object()) continue;
                UserParamInfo pi{};
                getIntI_(po2, "vst3Param", pi.vst3Param);
                getStrI_(po2, "name", pi.name);
                getDoubleI_(po2, "defaultNorm", pi.defaultNorm);
                getBoolI_(po2, "wasEnum", pi.wasEnum);
                if (pi.vst3Param < 0) continue;
                m.paramSnapshot.push_back(std::move(pi));
            }
        }

        if (m.match.empty()) continue;

        // v3 → v4 migration: if uf8Mode wasn't in the file, derive it from
        // the uf8 block. Maps with non-empty bank/strip bindings keep the
        // UF8 layer active; everything else opts out.
        if (!hadUf8Mode) m.uf8Mode = uf8MapHasContent_(m.uf8);

        // A map with no domain and no UF8 layer is meaningless — drop it
        // rather than carrying around dead entries.
        if (m.domain == Domain::None && !m.uf8Mode) continue;

        out.maps.push_back(std::move(m));
    }

    // Enforce isDefault one-of per "primary mode" (highest-index wins on
    // conflict). Primary modes are CS, BC, and UF8-only — a CS+UF8 map
    // shares the CS default slot with a CS-only map.
    bool seenCs = false, seenBc = false, seenUf8Only = false;
    for (auto it = out.maps.rbegin(); it != out.maps.rend(); ++it) {
        if (!it->isDefault) continue;
        bool& seen = (it->domain == Domain::BusComp)      ? seenBc
                  : (it->domain == Domain::ChannelStrip)  ? seenCs
                  :                                          seenUf8Only;
        if (seen) it->isDefault = false;
        else      seen = true;
    }
    return true;
}

// ---- View-cache rebuild ----------------------------------------------------

// Find canonical id/name/legend strings (static-storage const char*) for a
// linkIdx, preferring built-in maps whose `domain` matches `preferred`.
// Without the domain hint, BC linkIdxes 2..7 (Makeup/Attack/Release/
// Ratio/SidechainHPF/DryWetMix) collide with CS linkIdxes 2..7
// (Width/Pan/InputTrim/Phase/LowPassFreq/HighPassFreq) — and CS maps
// come first in kMaps order, so a user-mapped BC plug-in would inherit
// "Width"/"Pan"/etc. as its slot names. Walks domain-matched maps
// first; falls back to any-domain match (covers ext::* shared linkIdx
// ranges + any user pinning a slot to an unusual idx). Returns nullptr
// when no built-in slot uses that linkIdx.
const LinkSlot* canonicalSlot_(int linkIdx, Domain preferred = Domain::None)
{
    if (preferred != Domain::None) {
        for (const auto& m : allPluginMaps()) {
            if (m.domain != preferred) continue;
            if (const auto* s = findSlotByLinkIdx(m, linkIdx)) return s;
        }
    }
    for (const auto& m : allPluginMaps()) {
        if (const auto* s = findSlotByLinkIdx(m, linkIdx)) return s;
    }
    return nullptr;
}

void rebuildViewCache_()
{
    g_viewCache.clear();
    g_viewCache.reserve(g_catalog.maps.size());
    for (const auto& m : g_catalog.maps) {
        ViewCacheEntry e;
        e.match        = m.match;
        e.displayShort = m.displayShort.empty() ? std::string("USR") : m.displayShort;
        e.slotsBuf.reserve(m.slots.size());
        for (const auto& us : m.slots) {
            const LinkSlot* canon = canonicalSlot_(us.linkIdx, m.domain);
            LinkSlot ls{
                us.linkIdx,
                canon ? canon->id     : "",
                canon ? canon->name   : "",
                canon ? canon->legend : "",
                us.vst3Param,
                us.inverted,
                canon ? canon->deflt  : std::nullopt,
            };
            e.slotsBuf.push_back(ls);
        }
        g_viewCache.push_back(std::move(e));
    }
    // Now populate map fields whose char* / span members must point into
    // the cached entry. Done in a second pass so vector reallocation
    // during reserve()/push_back doesn't dangle the pointers.
    for (auto& e : g_viewCache) {
        // Domain is captured by value from the source UserPluginMap.
        const UserPluginMap* src = nullptr;
        for (const auto& m : g_catalog.maps) {
            if (m.match == e.match) { src = &m; break; }
        }
        e.map = PluginMap{
            e.match.c_str(),
            e.displayShort.c_str(),
            src ? src->domain : Domain::None,
            std::span<const LinkSlot>{ e.slotsBuf.data(), e.slotsBuf.size() },
        };
    }
}

} // namespace

void load()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    g_catalog = {};

    std::string contents;
    if (readFile_(configPath_(), contents) && !contents.empty()) {
        UserPluginCatalog tmp;
        tmp.formatVersion = kCurrentFormatVersion;
        if (parse_(contents, tmp)) {
            g_catalog = std::move(tmp);
        } else {
            logErr_("parse failed for %s — leaving catalog empty",
                    configPath_().c_str());
        }
    }
    rebuildViewCache_();
    g_generation.fetch_add(1, std::memory_order_relaxed);
}

// Built-in collision check. Walks the static built-in registry (no
// catalog access), so it's safe to call with or without g_mutex held.
bool collidesWithBuiltin_(std::string_view match)
{
    if (match.empty()) return false;
    for (const auto& bm : allPluginMaps()) {
        std::string_view bmm{ bm.match };
        if (match.find(bmm) != std::string_view::npos) return true;
        if (bmm.find(match) != std::string_view::npos) return true;
    }
    return false;
}

SaveResult save()
{
    std::lock_guard<std::mutex> lk(g_mutex);

    for (const auto& m : g_catalog.maps) {
        if (collidesWithBuiltin_(m.match)) {
            // Built-in already claims this match string. Refuse the save
            // — built-ins must stay unshadowable, and this also prevents
            // the user from accidentally shadowing CS 2 with a half-
            // mapped catalog entry.
            logErr_("save refused: '%s' collides with a built-in PluginMap",
                    m.match.c_str());
            return SaveResult::Collision;
        }
    }

    ensureConfigDir_();
    g_catalog.formatVersion = kCurrentFormatVersion;
    if (!writeFileAtomic_(configPath_(), serialize_(g_catalog))) {
        logErr_("atomic write failed for %s", configPath_().c_str());
        return SaveResult::IoError;
    }
    return SaveResult::Ok;
}

const UserPluginCatalog& get()
{
    return g_catalog;
}

void setAll(UserPluginCatalog c)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    g_catalog = std::move(c);
    rebuildViewCache_();
    g_generation.fetch_add(1, std::memory_order_relaxed);
}

void upsert(UserPluginMap m)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    auto it = std::find_if(g_catalog.maps.begin(), g_catalog.maps.end(),
        [&](const UserPluginMap& x) { return x.match == m.match; });

    if (m.isDefault) {
        // Buckets by primary mode (CS / BC / UF8-only). CS+UF8 shares the
        // CS bucket with CS-only; UF8-only is its own bucket (domain=None
        // + uf8Mode=true).
        auto primaryOf = [](const UserPluginMap& x) -> int {
            if (x.domain == Domain::ChannelStrip) return 1;
            if (x.domain == Domain::BusComp)      return 2;
            return 3;  // UF8-only
        };
        const int myPrimary = primaryOf(m);
        for (auto& other : g_catalog.maps) {
            if (other.match == m.match) continue;
            if (primaryOf(other) == myPrimary) other.isDefault = false;
        }
    }
    if (it != g_catalog.maps.end()) *it = std::move(m);
    else                            g_catalog.maps.push_back(std::move(m));
    rebuildViewCache_();
    g_generation.fetch_add(1, std::memory_order_relaxed);
}

bool removeByMatch(std::string_view match)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    auto it = std::find_if(g_catalog.maps.begin(), g_catalog.maps.end(),
        [&](const UserPluginMap& x) { return x.match == match; });
    if (it == g_catalog.maps.end()) return false;
    g_catalog.maps.erase(it);
    rebuildViewCache_();
    g_generation.fetch_add(1, std::memory_order_relaxed);
    return true;
}

int generation()
{
    return g_generation.load(std::memory_order_relaxed);
}

const PluginMap* lookupByName(std::string_view fxName)
{
    // No lock needed for read path: rebuildViewCache_ runs under the
    // mutex, and the cache is stable between mutations. Lookup happens
    // on the REAPER tick thread; mutations happen from the editor UI on
    // the same thread, so there's no race in practice. If we ever push
    // mutations off-thread this needs revisiting.
    for (const auto& e : g_viewCache) {
        if (fxName.find(e.match) != std::string_view::npos) return &e.map;
    }
    return nullptr;
}

const UserPluginMap* lookupOwnedByName(std::string_view fxName)
{
    // Same matching rule as lookupByName, but returns a pointer into the
    // owned catalog so callers can read the uf8.* fields directly.
    // Lifetime: until next mutation (same contract as g_viewCache).
    for (const auto& m : g_catalog.maps) {
        if (fxName.find(m.match) != std::string_view::npos) return &m;
    }
    return nullptr;
}

bool collidesWithBuiltin(std::string_view match)
{
    return collidesWithBuiltin_(match);
}

} // namespace uf8::user_plugins
