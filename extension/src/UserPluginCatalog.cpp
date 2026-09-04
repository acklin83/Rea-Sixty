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
#include "LogPath.h"
#include "UC1PluginMap.h"   // uc1::linkIdxIsButton — the UF1 seed splits the UC1
                            // slots into the same two streams the runtime does

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <mutex>
#include <set>
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
#include "JsonTree.h"

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
    // Owned storage for per-slot custom labels. LinkSlot::name points into
    // these when a customLabel override is present on the UserLinkSlot.
    // Using std::list so pointers stay stable when the container grows.
    std::list<std::string>  customLabelPool;
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
    // Hold the path in a named string. uf8::logPath returns std::string BY
    // VALUE, so `const char* p = uf8::logPath(...).c_str()` leaves p dangling
    // the instant the full-expression ends — before fopen on the next line
    // ever reads it. It appeared to work because the freed short-string buffer
    // usually still held the bytes, which is what kept it alive for so long;
    // clang flags it as -Wdangling-gsl.
    //
    // Every other log site in the tree writes
    // `std::fopen(uf8::logPath(...).c_str(), "a")` in ONE expression, where
    // the temporary legitimately outlives the call. Only this one stored the
    // pointer first. Keep it that way: either pass it straight to fopen, or
    // bind the string to a local like this.
    //
    // No #ifdef here. uf8::logPath already resolves per platform (%TEMP% on
    // Windows via GetTempPath, /tmp elsewhere), which is the entire reason
    // LogPath.cpp exists. This site used to special-case Windows to a bare
    // relative "rea_sixty.log", so on Windows this one log landed in REAPER's
    // current working directory while every other log went to %TEMP% — an
    // inconsistency, and a file the user would not find.
    const std::string logFile = uf8::logPath("rea_sixty.log");
    if (FILE* lf = std::fopen(logFile.c_str(), "a")) {
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
    switch (m) {
        case VPotMode::Toggle:    return "Toggle";
        case VPotMode::StepCycle: return "StepCycle";
        default:                  return "Value";
    }
}

VPotMode vpotModeFromName_(const char* s)
{
    if (s && std::strcmp(s, "Toggle")    == 0) return VPotMode::Toggle;
    if (s && std::strcmp(s, "StepCycle") == 0) return VPotMode::StepCycle;
    return VPotMode::Value;
}

const char* vpotPolarityName_(VPotPolarity p)
{
    return p == VPotPolarity::Bipolar ? "bipolar" : "unipolar";
}

VPotPolarity vpotPolarityFromName_(const char* s)
{
    if (s && std::strcmp(s, "bipolar") == 0) return VPotPolarity::Bipolar;
    return VPotPolarity::Unipolar;
}

bool uf8MapHasContent_(const UserUf8Map& u)
{
    for (int fb = 0; fb < uf8::kUserUf8FaderBankCount; ++fb) {
        for (int s = 0; s < 8; ++s) {
            const auto& sb = u.strips[fb][s];
            if (sb.faderVst3Param >= 0 || sb.soloVst3Param >= 0
             || sb.cutVst3Param   >= 0 || sb.selVst3Param  >= 0) return true;
        }
        for (int vb = 0; vb < uf8::kUserUf8VpotBankCount; ++vb) {
            for (int s = 0; s < 8; ++s) {
                if (u.banks.banks[fb][vb][s].vst3Param >= 0) return true;
                if (!u.banks.banks[fb][vb][s].label.empty()) return true;
            }
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
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
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
        // v11, additive — only written when ON, so v10 catalogs round-trip
        // byte-identically and an older reader ignores it (absent = false).
        if (m.uf1Mode)
            os << "      \"uf1Mode\": true,\n";
        if (m.uf1EqGraph != UserPluginMap::Uf1EqGraph::Follow)   // v16, sparse
            os << "      \"uf1EqGraph\": " << int(m.uf1EqGraph) << ",\n";
        // Additive (Frank 2026-06-02) — only written when the user has turned
        // the default off, so pre-feature catalogs stay byte-identical.
        if (!m.useReaperTrackPolarity)
            os << "      \"useReaperTrackPolarity\": false,\n";
        if (m.snapshotTakenAt > 0)
            os << "      \"snapshotTakenAt\": " << m.snapshotTakenAt << ",\n";
        // Additive (Frank 2026-07-19) — the live plug-in factory name, written
        // only once captured so pre-feature catalogs stay byte-identical.
        if (!m.originalName.empty()) {
            os << "      \"original_name\": ";
            appendEscaped_(os, m.originalName);
            os << ",\n";
        }
        // Additive (Frank 2026-07-20) — functional param count for the
        // exchange's parameter coverage. Only written once captured (>= 0).
        if (m.functionalParamCount >= 0)
            os << "      \"functional_params\": " << m.functionalParamCount << ",\n";
        // Emit the optional knob-travel fields (range / sensitivity / curve
        // points). All are additive — only written when non-default so
        // existing files stay byte-identical until the user actually
        // customises a slot. Used for both `slots` and the per-domain
        // caches below.
        auto emitSlotKnobTravel = [&](const SlotLayer& s) {
            if (s.rangeMin != 0.0f) os << ", \"rangeMin\": " << s.rangeMin;
            if (s.rangeMax != 1.0f) os << ", \"rangeMax\": " << s.rangeMax;
            if (s.sensitivity != 1.0f)
                os << ", \"sensitivity\": " << s.sensitivity;
            if (!s.curvePoints.empty()) {
                os << ", \"curvePoints\": [";
                bool firstPt = true;
                for (const auto& pt : s.curvePoints) {
                    if (!firstPt) os << ",";
                    firstPt = false;
                    os << " [" << pt.first << ", " << pt.second << "]";
                }
                os << " ]";
            }
            // Polarity / defaultNorm — additive (Frank 2026-05-26).
            // Default = Unipolar / 0.5; only emit when customised so
            // pre-feature catalogs stay byte-identical.
            if (s.polarity != VPotPolarity::Unipolar) {
                os << ", \"polarity\": ";
                appendEscaped_(os, vpotPolarityName_(s.polarity));
            }
            if (s.defaultNorm != 0.5) {
                os << ", \"defaultNorm\": " << s.defaultNorm;
            }
            // Per-button push-cycle steps — additive; only written when the
            // user has curated a subset / built a macro. Empty list keeps the
            // shipped auto-cycle-all-options behaviour and writes nothing.
            if (!s.pushSteps.empty()) {
                os << ", \"pushSteps\": [";
                bool firstStep = true;
                for (const auto& st : s.pushSteps) {
                    if (!firstStep) os << ",";
                    firstStep = false;
                    os << " { \"vst3Param\": " << st.vst3Param
                       << ", \"norm\": " << st.norm;
                    if (!st.enabled) os << ", \"enabled\": false";
                    os << " }";
                }
                os << " ]";
            }
        };
        // KnobTravel sub-struct writer for UF8 V-Pot + fader bindings.
        // Wraps the four fields in a nested "travel" object so older
        // readers (no travel key) ignore them; current readers parse
        // them as additive metadata.
        auto emitKnobTravelObj = [&](const char* keyPrefix,
                                     const KnobTravel& t) {
            if (!t.isCustom()) return;
            os << ", \"" << keyPrefix << "\": {";
            bool any = false;
            auto sep = [&]() { if (any) os << ","; any = true; };
            if (t.rangeMin != 0.0f) {
                sep(); os << " \"rangeMin\": " << t.rangeMin;
            }
            if (t.rangeMax != 1.0f) {
                sep(); os << " \"rangeMax\": " << t.rangeMax;
            }
            if (t.sensitivity != 1.0f) {
                sep(); os << " \"sensitivity\": " << t.sensitivity;
            }
            if (!t.curvePoints.empty()) {
                sep(); os << " \"curvePoints\": [";
                bool firstPt = true;
                for (const auto& pt : t.curvePoints) {
                    if (!firstPt) os << ",";
                    firstPt = false;
                    os << " [" << pt.first << ", " << pt.second << "]";
                }
                os << " ]";
            }
            os << " }";
        };
        // Emit the mutable mapping fields of one SlotLayer (vst3Param +
        // inverted + optional customLabel + knob-travel + push-cycle),
        // starting with vst3Param (no leading comma). Shared by the slot
        // body and the per-modifier overlay objects below.
        auto emitLayerBody = [&](const SlotLayer& s) {
            os << "\"vst3Param\": "    << s.vst3Param
               << ", \"inverted\": "   << (s.inverted ? "true" : "false");
            if (!s.customLabel.empty()) {
                os << ", \"customLabel\": ";
                appendEscaped_(os, s.customLabel);
            }
            emitSlotKnobTravel(s);
        };
        // Emit one full slot object: linkIdx + Normal-layer body + the optional
        // FX-Learn modifier overlays (v9). Additive: "modLayers" only appears
        // when an Option/Control overlay is populated, so v8-era catalogs round-
        // trip byte-identically and older readers ignore the key. Shared by the
        // active `slots` array AND the per-domain cs/bcSlotCache below — the
        // cache MUST carry modLayers too, else switching the edit domain (which
        // round-trips slots through the cache) silently drops every overlay's
        // param + custom label. Frank 2026-06-15.
        auto emitSlotObject = [&](const UserLinkSlot& s) {
            os << "{ \"linkIdx\": " << s.linkIdx << ", ";
            emitLayerBody(s);
            if (hasAnyModLayer(s)) {
                os << ", \"modLayers\": {";
                bool firstML = true;
                auto emitML = [&](const char* key, const SlotLayer& ml) {
                    if (!fxLayerNonDefault(ml)) return;
                    if (!firstML) os << ",";
                    firstML = false;
                    os << " \"" << key << "\": { ";
                    emitLayerBody(ml);
                    os << " }";
                };
                emitML("option",     s.modLayers[0]);
                emitML("control",    s.modLayers[1]);
                emitML("ctrlOption", s.modLayers[2]);
                os << " }";
            }
            os << " }";
        };
        os << "      \"slots\": [";
        bool firstSlot = true;
        for (const auto& s : m.slots) {
            if (!firstSlot) os << ",";
            firstSlot = false;
            os << "\n        ";
            emitSlotObject(s);
        }
        os << "\n      ]";
        // Metering block — emit when any field is non-default. Older
        // versions only emitted when grVst3Param ≥ 0, but offset + cal
        // tables are independent of which read path is in use, so a
        // user who calibrates against the GainReduction_dB fallback
        // (grVst3Param = -1) still needs their numbers persisted.
        auto anyCalNonZero = [](const double* a, int n) {
            for (int i = 0; i < n; ++i) if (a[i] != 0.0) return true;
            return false;
        };
        const bool bcCalDirty   = anyCalNonZero(m.metering.grBcVuCalDb, 6);
        const bool ledsCalDirty = anyCalNonZero(m.metering.grLedsCalDb, 5);
        const bool meteringDirty =
            m.metering.grVst3Param >= 0 ||
            m.metering.grOffsetDb  != 0.0 ||
            bcCalDirty || ledsCalDirty;
        if (meteringDirty) {
            os << ",\n      \"metering\": { \"gainReduction\": { "
               << "\"vst3Param\": " << m.metering.grVst3Param
               << ", \"offsetDb\": " << m.metering.grOffsetDb;
            if (bcCalDirty) {
                os << ", \"bcVuCalDb\": [";
                for (int i = 0; i < 6; ++i) {
                    if (i) os << ", ";
                    os << m.metering.grBcVuCalDb[i];
                }
                os << "]";
            }
            if (ledsCalDirty) {
                os << ", \"ledsCalDb\": [";
                for (int i = 0; i < 5; ++i) {
                    if (i) os << ", ";
                    os << m.metering.grLedsCalDb[i];
                }
                os << "]";
            }
            os << " } }";
        }
        // Per-domain slot caches — emit only when non-empty. Both
        // arrays use the same row layout as the active `slots` field.
        auto emitSlotCache = [&](const char* key,
                                 const std::vector<UserLinkSlot>& vs) {
            if (vs.empty()) return;
            os << ",\n      \"" << key << "\": [";
            bool first = true;
            for (const auto& s : vs) {
                if (!first) os << ",";
                first = false;
                os << "\n        ";
                emitSlotObject(s);   // same body as `slots` — INCLUDING modLayers
            }
            os << "\n      ]";
        };
        emitSlotCache("csSlotCache", m.csSlotCache);
        emitSlotCache("bcSlotCache", m.bcSlotCache);
        if (uf8MapHasContent_(m.uf8)) {
            os << ",\n      \"uf8\": {\n";
            // banksByFaderBank: 2 fader-banks × 8 V-Pot-banks × 8 strips
            // (v7, Frank 2026-05-17). Pre-v7 readers see the older
            // `banks` 2D array; their parser path runs through the v6
            // migration below.
            os << "        \"banksByFaderBank\": [";
            for (int fb = 0; fb < uf8::kUserUf8FaderBankCount; ++fb) {
                if (fb) os << ",";
                os << "\n          [";
                for (int vb = 0; vb < uf8::kUserUf8VpotBankCount; ++vb) {
                    if (vb) os << ",";
                    os << "\n            [";
                    for (int s = 0; s < 8; ++s) {
                        if (s) os << ",";
                        const auto& bs = m.uf8.banks.banks[fb][vb][s];
                        os << "\n              { \"vst3Param\": " << bs.vst3Param
                           << ", \"label\": ";
                        appendEscaped_(os, bs.label);
                        os << ", \"vpotMode\": ";
                        appendEscaped_(os, vpotModeName_(bs.vpotMode));
                        os << ", \"inverted\": " << (bs.inverted ? "true" : "false")
                           << ", \"defaultNorm\": " << bs.defaultNorm
                           << ", \"stripColour\": "
                           << static_cast<unsigned>(bs.stripColour);
                        // Polarity emitted only when non-default
                        // (bipolar) so existing files stay
                        // byte-identical until a user opts in.
                        if (bs.polarity != VPotPolarity::Unipolar) {
                            os << ", \"polarity\": ";
                            appendEscaped_(os, vpotPolarityName_(bs.polarity));
                        }
                        emitKnobTravelObj("travel", bs.travel);
                        // Rotary step-cycle steps — additive; only written when
                        // the slot is in StepCycle mode with a curated list.
                        if (!bs.vpotSteps.empty()) {
                            os << ", \"vpotSteps\": [";
                            bool firstStep = true;
                            for (const auto& st : bs.vpotSteps) {
                                if (!firstStep) os << ",";
                                firstStep = false;
                                os << " { \"vst3Param\": " << st.vst3Param
                                   << ", \"norm\": " << st.norm;
                                if (!st.enabled) os << ", \"enabled\": false";
                                os << " }";
                            }
                            os << " ]";
                        }
                        // (UF8 FX-Learn modifier overlays removed — UC1-only.)
                        os << " }";
                    }
                    os << "\n            ]";
                }
                os << "\n          ]";
            }
            os << "\n        ],\n";
            // stripsByFaderBank: 2 fader-banks × 8 strips (v7).
            // v6 had stripsByBank as topSoftKey×slot; that dimension was
            // dropped — fader/solo/cut/sel don't vary with the Top-Soft-
            // Key V-Pot layer. Migration in parse_ takes strips[0][slot]
            // from v6 into faderBank=0; fader-bank 1 starts empty.
            os << "        \"stripsByFaderBank\": [";
            for (int fb = 0; fb < uf8::kUserUf8FaderBankCount; ++fb) {
                if (fb) os << ",";
                os << "\n          [";
                for (int s = 0; s < 8; ++s) {
                    if (s) os << ",";
                    const auto& sb = m.uf8.strips[fb][s];
                    os << "\n            { "
                       << "\"fader\": { \"vst3Param\": " << sb.faderVst3Param
                       << ", \"inverted\": " << (sb.faderInverted ? "true" : "false")
                       << ", \"label\": ";
                    appendEscaped_(os, sb.faderLabel);
                    os << " }, "
                       << "\"solo\": { \"vst3Param\": " << sb.soloVst3Param
                       << ", \"colour\": " << static_cast<unsigned>(sb.soloColour)
                       << ", \"invert\": " << (sb.soloInvert ? "true" : "false")
                       << " }, "
                       << "\"cut\": { \"vst3Param\": "  << sb.cutVst3Param
                       << ", \"colour\": " << static_cast<unsigned>(sb.cutColour)
                       << ", \"invert\": " << (sb.cutInvert ? "true" : "false")
                       << " }, "
                       << "\"sel\": { \"vst3Param\": "  << sb.selVst3Param
                       << ", \"colour\": " << static_cast<unsigned>(sb.selColour)
                       << ", \"invert\": " << (sb.selInvert ? "true" : "false")
                       << " }";
                    // (UF8 FX-Learn modifier overlays removed — UC1-only.)
                    os << " }";
                }
                os << "\n          ]";
            }
            os << "\n        ],\n";
            // Per-bank TopSoftKey LED appearance.
            os << "        \"topSoftKeyLeds\": [";
            for (int vb = 0; vb < uf8::kUserUf8VpotBankCount; ++vb) {
                if (vb) os << ",";
                const auto& l = m.uf8.topSoftKeyLeds[vb];
                os << "\n          { "
                   << "\"colour\": "
                   << static_cast<unsigned>(l.colour)
                   << ", \"label\": ";
                appendEscaped_(os, l.label);
                os << " }";
            }
            os << "\n        ]\n";
            os << "      }";
        }
        // UF1 plugin-mode map (v11). Additive + SPARSE: only mapped positions
        // are written, and the whole block is omitted when the UF1 layer is
        // empty — so a v10 catalog round-trips byte-identically and an older
        // reader just ignores the key. Reuses emitLayerBody, so a UF1 slot
        // carries the same tuning fields as a UC1 slot. No modLayers by design
        // (an explicit UF1 map is layer-free — see UserUf1Map in the header).
        if (uf8::uf1MapHasContent(m.uf1)) {
            os << ",\n      \"uf1\": {\n";
            auto emitUf1Stream = [&](const char* key,
                                     const std::vector<uf8::UserUf1Slot>& v,
                                     bool last) {
                os << "        \"" << key << "\": [";
                bool first = true;
                for (const auto& s : v) {
                    if (s.pos < 0) continue;
                    if (s.vst3Param < 0 && s.pushSteps.empty() && !s.special) continue;
                    if (!first) os << ",";
                    first = false;
                    os << "\n          { \"pos\": " << s.pos << ", ";
                    // v12: per-key LED colour, only when set (0 = no override),
                    // so a v11-shaped map still serialises byte-identically.
                    if (s.ledRgb) os << "\"ledRgb\": " << s.ledRgb << ", ";
                    // v14: fixed non-parameter soft-key action, likewise sparse.
                    if (s.special) os << "\"special\": " << int(s.special) << ", ";
                    emitLayerBody(s);
                    os << " }";
                }
                os << "\n        ]" << (last ? "\n" : ",\n");
            };
            emitUf1Stream("vpots",    m.uf1.vpots,    false);
            emitUf1Stream("softKeys", m.uf1.softKeys, true);
            os << "      }";
        }
        // v13: the shared per-parameter display names. Sparse, emitted only
        // when the user has named something, so a map without them serialises
        // exactly as it did in v12.
        if (!m.paramLabels.empty()) {
            os << ",\n      \"paramLabels\": [";
            bool firstLabel = true;
            for (const auto& pl : m.paramLabels) {
                if (pl.vst3Param < 0 || pl.label.empty()) continue;
                if (!firstLabel) os << ",";
                firstLabel = false;
                os << "\n        { \"vst3Param\": " << pl.vst3Param
                   << ", \"label\": ";
                appendEscaped_(os, pl.label);
                os << " }";
            }
            os << "\n      ]";
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
        // EXT FUNCS list (v8). Emit only populated slots, each with its grid
        // index, so positions (and named-but-unassigned slots) survive.
        {
            bool anyExt = false;
            for (const auto& e : m.extFuncs)
                if (!e.name.empty() || e.vst3Param >= 0) { anyExt = true; break; }
            if (anyExt) {
                os << ",\n      \"extFuncs\": [";
                bool firstExt = true;
                for (int i = 0; i < kUserExtFuncsCount; ++i) {
                    const auto& e = m.extFuncs[i];
                    if (e.name.empty() && e.vst3Param < 0) continue;
                    if (!firstExt) os << ",";
                    firstExt = false;
                    os << "\n        { \"slot\": " << i
                       << ", \"name\": ";
                    appendEscaped_(os, e.name);
                    os << ", \"vst3Param\": " << e.vst3Param
                       << " }";
                }
                os << "\n      ]";
            }
        }
        os << "\n    }";
    }
    if (!firstPlugin) os << "\n  ";
    // ⛔ ONE closer for "plugins", one for the object. The removal of the
    // Quick-Learn skip list (2026-09-03) took out the block BETWEEN these two
    // lines, and the second line had been closing the SKIP array — so what was
    // left wrote "]]" and every save produced a catalog the loader refuses.
    // Frank's file was unreadable within the hour. Deleting a block whose
    // neighbours share a bracket means re-reading the brackets, not the block.
    // ⚠ And the first cut of THIS fix removed both, leaving "plugins" unclosed —
    // caught immediately by test_user_catalog_uf1's round trip, which is what
    // that test is for.
    os << "]\n}\n";
    return os.str();
}

// ---- Parse ------------------------------------------------------------------

bool parse_(const std::string& json, UserPluginCatalog& out)
{
    wdl_json_parser p;
    wdl_json_element* root = p.parse(json.c_str(), static_cast<int>(json.size()));
    JsonTreeGuard rootGuard{p, root};
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
        // Widened 4→7 (2026-05-09) → 12 (2026-08-12, the colour-bar zone
        // measured on HW at last). Older 4- and 7-char values still load
        // identically — the cap only ever truncates, never pads.
        if (m.displayShort.size() > 12) m.displayShort.resize(12);
        getBoolI_(po, "isDefault", m.isDefault);
        // v4: explicit uf8Mode flag. For v3 files the key is missing — we
        // derive uf8Mode below (after the uf8 block parses) from
        // uf8MapHasContent_ so legacy maps with bank/strip bindings stay
        // UF8-enabled.
        bool uf8ModeRead = false;
        const bool hadUf8Mode = getBoolI_(po, "uf8Mode", uf8ModeRead);
        if (hadUf8Mode) m.uf8Mode = uf8ModeRead;
        // v11 uf1Mode. Absent on v10 files → derived from the uf1 block below
        // (which is also absent there, so it stays false = sequential fallback).
        bool uf1ModeRead = false;
        const bool hadUf1Mode = getBoolI_(po, "uf1Mode", uf1ModeRead);
        if (hadUf1Mode) m.uf1Mode = uf1ModeRead;
        bool uf1EqRead = false;                // v15, absent before
        // v16 reads a NUMBER (0/1/2); v15 wrote `true`. Accept both — a v15
        // `true` was an explicit choice made before the global existed, so it
        // becomes On, not Follow.
        if (int uf1EqNum = 0; getIntI_(po, "uf1EqGraph", uf1EqNum))
            m.uf1EqGraph = (uf1EqNum >= 0 && uf1EqNum <= 2)
                ? UserPluginMap::Uf1EqGraph(uf1EqNum)
                : UserPluginMap::Uf1EqGraph::Follow;
        else if (getBoolI_(po, "uf1EqGraph", uf1EqRead) && uf1EqRead)
            m.uf1EqGraph = UserPluginMap::Uf1EqGraph::On;
        // Additive; missing key keeps the struct default (true).
        getBoolI_(po, "useReaperTrackPolarity", m.useReaperTrackPolarity);
        int snapTs = 0;
        if (getIntI_(po, "snapshotTakenAt", snapTs))
            m.snapshotTakenAt = snapTs;
        // Additive; missing key leaves it empty (the exchange falls back to
        // `match`). Round-trips through a shared .rea60map so an imported map
        // keeps the identity of the plug-in it was learned from.
        getStrI_(po, "original_name", m.originalName);
        int fpc = -1;
        if (getIntI_(po, "functional_params", fpc)) m.functionalParamCount = fpc;

        // Parse the mutable mapping fields of one SlotLayer from object `so`.
        // Shared by the base slot (Normal layer) and the per-modifier overlay
        // objects. All fields additive — missing keys keep struct defaults.
        auto parseLayerBody = [&](wdl_json_element* so, SlotLayer& sl) {
            getIntI_(so, "vst3Param", sl.vst3Param);
            getBoolI_(so, "inverted", sl.inverted);
            getStrI_(so, "customLabel", sl.customLabel);
            double tmp = 0.0;
            if (getDoubleI_(so, "rangeMin",    tmp)) sl.rangeMin    = (float)tmp;
            if (getDoubleI_(so, "rangeMax",    tmp)) sl.rangeMax    = (float)tmp;
            if (getDoubleI_(so, "sensitivity", tmp)) sl.sensitivity = (float)tmp;
            if (auto* cpa = so->get_item_by_name("curvePoints");
                cpa && cpa->is_array() && cpa->m_array)
            {
                const int pn = cpa->m_array->GetSize();
                sl.curvePoints.reserve(pn);
                for (int pi = 0; pi < pn; ++pi) {
                    wdl_json_element* pe = cpa->enum_item(pi);
                    if (!pe || !pe->is_array() || !pe->m_array) continue;
                    if (pe->m_array->GetSize() < 2) continue;
                    wdl_json_element* xe = pe->enum_item(0);
                    wdl_json_element* ye = pe->enum_item(1);
                    if (!xe || !ye) continue;
                    const char* xs = xe->get_string_value(true);
                    const char* ys = ye->get_string_value(true);
                    if (!xs || !ys) continue;
                    sl.curvePoints.emplace_back((float)std::atof(xs),
                                                (float)std::atof(ys));
                }
            }
            // Polarity / defaultNorm (Frank 2026-05-26). Both additive.
            std::string polStr;
            if (getStrI_(so, "polarity", polStr)) {
                sl.polarity = vpotPolarityFromName_(polStr.c_str());
            }
            getDoubleI_(so, "defaultNorm", sl.defaultNorm);
            // Per-button push-cycle steps (additive). Each entry is an
            // object { "vst3Param", "norm" }. Absent => empty => legacy
            // auto-cycle.
            if (auto* psa = so->get_item_by_name("pushSteps");
                psa && psa->is_array() && psa->m_array)
            {
                const int pn = psa->m_array->GetSize();
                sl.pushSteps.reserve(pn);
                for (int pi = 0; pi < pn; ++pi) {
                    wdl_json_element* pe = psa->enum_item(pi);
                    if (!pe || !pe->is_object()) continue;
                    PushStep st{};
                    getIntI_(pe, "vst3Param", st.vst3Param);
                    double nv = 0.0;
                    if (getDoubleI_(pe, "norm", nv)) st.norm = (float)nv;
                    // Additive; absent => enabled (default true).
                    getBoolI_(pe, "enabled", st.enabled);
                    if (st.vst3Param < 0) continue;
                    sl.pushSteps.push_back(st);
                }
            }
        };

        auto readSlotArr = [&](const char* key,
                               std::vector<UserLinkSlot>& dest) {
            auto* slotsArr = po->get_item_by_name(key);
            if (!slotsArr || !slotsArr->is_array() || !slotsArr->m_array) return;
            const int sn = slotsArr->m_array->GetSize();
            for (int s = 0; s < sn; ++s) {
                wdl_json_element* so = slotsArr->enum_item(s);
                if (!so || !so->is_object()) continue;
                UserLinkSlot us{};
                getIntI_(so, "linkIdx", us.linkIdx);
                parseLayerBody(so, us);   // base object == Normal layer
                // FX-Learn modifier layers (v9). Additive — absent on v8 files,
                // leaving both overlays at struct defaults (= Normal-only).
                if (auto* ml = so->get_item_by_name("modLayers");
                    ml && ml->is_object())
                {
                    if (auto* opt = ml->get_item_by_name("option");
                        opt && opt->is_object())
                        parseLayerBody(opt, us.modLayers[0]);
                    if (auto* ctl = ml->get_item_by_name("control");
                        ctl && ctl->is_object())
                        parseLayerBody(ctl, us.modLayers[1]);
                    if (auto* co = ml->get_item_by_name("ctrlOption");
                        co && co->is_object())
                        parseLayerBody(co, us.modLayers[2]);
                }
                if (us.linkIdx < 0) continue;
                // A slot is valid when the Normal layer carries a mapping
                // (param or push-cycle) OR any modifier overlay does — the
                // latter supports a control that's inert normally but active
                // under a held modifier.
                if (!fxLayerMapped(us) && !hasAnyModLayer(us)) continue;
                dest.push_back(us);
            }
        };
        readSlotArr("slots",        m.slots);
        readSlotArr("csSlotCache",  m.csSlotCache);
        readSlotArr("bcSlotCache",  m.bcSlotCache);

        // UF1 plugin-mode map (v11). Additive + sparse; absent on v10 files,
        // which then keep the sequential fallback. Mirrors readSlotArr but
        // keys on `pos` instead of `linkIdx` and has NO modLayers by design.
        if (auto* u1 = po->get_item_by_name("uf1"); u1 && u1->is_object()) {
            auto readUf1Stream = [&](const char* key,
                                     std::vector<uf8::UserUf1Slot>& dest) {
                auto* arr = u1->get_item_by_name(key);
                if (!arr || !arr->is_array() || !arr->m_array) return;
                const int n = arr->m_array->GetSize();
                for (int i = 0; i < n; ++i) {
                    wdl_json_element* so = arr->enum_item(i);
                    if (!so || !so->is_object()) continue;
                    uf8::UserUf1Slot us{};
                    getIntI_(so, "pos", us.pos);
                    int ledRgb = 0;                       // v12, absent on v11
                    getIntI_(so, "ledRgb", ledRgb);
                    us.ledRgb = static_cast<uint32_t>(ledRgb < 0 ? 0 : ledRgb);
                    int special = 0;                      // v14, absent before
                    getIntI_(so, "special", special);
                    us.special = static_cast<uint8_t>(
                        (special >= 0 && special <= int(uf8::Uf1SkSpecial::StripModeGui))
                            ? special : 0);
                    parseLayerBody(so, us);
                    if (us.pos < 0) continue;
                    if (us.vst3Param < 0 && us.pushSteps.empty() && !us.special) continue;
                    dest.push_back(std::move(us));
                }
            };
            readUf1Stream("vpots",    m.uf1.vpots);
            readUf1Stream("softKeys", m.uf1.softKeys);
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
                // v5 per-breakpoint correction tables. Missing or
                // wrong-length arrays silently default to zeros, so
                // v4 files load identity-calibrated.
                auto readCalArr = [](wdl_json_element* obj, const char* key,
                                     double* out, int n) {
                    auto* arr = obj->get_item_by_name(key);
                    if (!arr || !arr->is_array() || !arr->m_array) return;
                    const int len = (std::min)(arr->m_array->GetSize(), n);
                    for (int i = 0; i < len; ++i) {
                        wdl_json_element* item = arr->enum_item(i);
                        if (!item) continue;
                        if (auto* s = item->get_string_value(true)) {
                            out[i] = std::atof(s);
                        }
                    }
                };
                readCalArr(gr, "bcVuCalDb", m.metering.grBcVuCalDb, 6);
                readCalArr(gr, "ledsCalDb", m.metering.grLedsCalDb, 5);
            }
        }

        if (auto* uo = po->get_item_by_name("uf8");
            uo && uo->is_object())
        {
            // KnobTravel sub-object reader — invoked for each fader and
            // each V-Pot binding. Missing keys leave the struct defaults
            // (= linear / no curve / sensitivity 1.0), so pre-feature
            // files remain byte-identical no-ops.
            auto parseKnobTravel = [&](wdl_json_element* parent,
                                       KnobTravel& t) {
                auto* to = parent->get_item_by_name("travel");
                if (!to || !to->is_object()) return;
                double tmp = 0.0;
                if (getDoubleI_(to, "rangeMin",    tmp)) t.rangeMin    = (float)tmp;
                if (getDoubleI_(to, "rangeMax",    tmp)) t.rangeMax    = (float)tmp;
                if (getDoubleI_(to, "sensitivity", tmp)) t.sensitivity = (float)tmp;
                if (auto* cpa = to->get_item_by_name("curvePoints");
                    cpa && cpa->is_array() && cpa->m_array)
                {
                    const int pn = cpa->m_array->GetSize();
                    t.curvePoints.clear();
                    t.curvePoints.reserve(pn);
                    for (int pi = 0; pi < pn; ++pi) {
                        wdl_json_element* pe = cpa->enum_item(pi);
                        if (!pe || !pe->is_array() || !pe->m_array) continue;
                        if (pe->m_array->GetSize() < 2) continue;
                        wdl_json_element* xe = pe->enum_item(0);
                        wdl_json_element* ye = pe->enum_item(1);
                        if (!xe || !ye) continue;
                        const char* xs = xe->get_string_value(true);
                        const char* ys = ye->get_string_value(true);
                        if (!xs || !ys) continue;
                        t.curvePoints.emplace_back((float)std::atof(xs),
                                                   (float)std::atof(ys));
                    }
                }
            };
            // Helper for parsing one bank slot object (V-Pot binding).
            auto parseBankSlot = [&](wdl_json_element* so,
                                     UserUf8BankSlot& bs) {
                getIntI_(so, "vst3Param", bs.vst3Param);
                getStrI_(so, "label", bs.label);
                std::string mode;
                if (getStrI_(so, "vpotMode", mode))
                    bs.vpotMode = vpotModeFromName_(mode.c_str());
                getBoolI_(so, "inverted", bs.inverted);
                getDoubleI_(so, "defaultNorm", bs.defaultNorm);
                int colTmp = 0;
                if (getIntI_(so, "stripColour", colTmp)) {
                    bs.stripColour =
                        static_cast<uint32_t>(colTmp) & 0x00FFFFFFu;
                } else if (getIntI_(so, "colour", colTmp)) {
                    bs.stripColour =
                        static_cast<uint32_t>(colTmp) & 0x00FFFFFFu;
                }
                std::string pol;
                if (getStrI_(so, "polarity", pol)) {
                    bs.polarity = vpotPolarityFromName_(pol.c_str());
                }
                parseKnobTravel(so, bs.travel);
                // Rotary step-cycle steps (additive). Same shape as the UC1
                // button pushSteps. Absent => empty => not a step-cycle.
                if (auto* psa = so->get_item_by_name("vpotSteps");
                    psa && psa->is_array() && psa->m_array)
                {
                    const int pn = psa->m_array->GetSize();
                    bs.vpotSteps.reserve(pn);
                    for (int pi = 0; pi < pn; ++pi) {
                        wdl_json_element* pe = psa->enum_item(pi);
                        if (!pe || !pe->is_object()) continue;
                        PushStep st{};
                        getIntI_(pe, "vst3Param", st.vst3Param);
                        double nv = 0.0;
                        if (getDoubleI_(pe, "norm", nv)) st.norm = (float)nv;
                        getBoolI_(pe, "enabled", st.enabled);
                        if (st.vst3Param < 0) continue;
                        bs.vpotSteps.push_back(st);
                    }
                }
                // (UF8 FX-Learn V-Pot modifier overlays removed — UC1-only.
                // Any "modLayers" key in an old v9 file is silently ignored.)
            };
            // v7: banksByFaderBank [fb][vpotBank][slot]. v6 had a flat
            // `banks` [vpotBank][slot] — migrate into faderBank=0.
            if (auto* bfbArr = uo->get_item_by_name("banksByFaderBank");
                bfbArr && bfbArr->is_array() && bfbArr->m_array)
            {
                const int fbn = (std::min)(bfbArr->m_array->GetSize(),
                                           uf8::kUserUf8FaderBankCount);
                for (int fb = 0; fb < fbn; ++fb) {
                    wdl_json_element* fbRow = bfbArr->enum_item(fb);
                    if (!fbRow || !fbRow->is_array() || !fbRow->m_array) continue;
                    const int bn = (std::min)(fbRow->m_array->GetSize(),
                                              uf8::kUserUf8VpotBankCount);
                    for (int b = 0; b < bn; ++b) {
                        wdl_json_element* row = fbRow->enum_item(b);
                        if (!row || !row->is_array() || !row->m_array) continue;
                        const int sn = (std::min)(row->m_array->GetSize(), 8);
                        for (int s = 0; s < sn; ++s) {
                            wdl_json_element* so = row->enum_item(s);
                            if (!so || !so->is_object()) continue;
                            parseBankSlot(so, m.uf8.banks.banks[fb][b][s]);
                        }
                    }
                }
            }
            else if (auto* banksArr = uo->get_item_by_name("banks");
                     banksArr && banksArr->is_array() && banksArr->m_array)
            {
                const int bn = (std::min)(banksArr->m_array->GetSize(),
                                          uf8::kUserUf8VpotBankCount);
                for (int b = 0; b < bn; ++b) {
                    wdl_json_element* row = banksArr->enum_item(b);
                    if (!row || !row->is_array() || !row->m_array) continue;
                    const int sn = (std::min)(row->m_array->GetSize(), 8);
                    for (int s = 0; s < sn; ++s) {
                        wdl_json_element* so = row->enum_item(s);
                        if (!so || !so->is_object()) continue;
                        parseBankSlot(so, m.uf8.banks.banks[0][b][s]);
                    }
                }
            }
            // Per-strip bindings. v7 emits `stripsByFaderBank` (2D:
            // faderBank × slot). v6 had `stripsByBank` (2D: topSoftKey ×
            // slot) — the top-soft-key dimension is dropped; we take
            // strips[0][slot] from v6 into faderBank=0. v5 had a flat
            // `strips` (1D: slot) — replicate into faderBank=0.
            auto parseStripObj = [&](wdl_json_element* so,
                                     UserUf8StripBinding& sb) {
                if (auto* fo = so->get_item_by_name("fader");
                    fo && fo->is_object())
                {
                    getIntI_(fo, "vst3Param", sb.faderVst3Param);
                    getBoolI_(fo, "inverted", sb.faderInverted);
                    getStrI_(fo, "label", sb.faderLabel);
                }
                int colTmp = 0;
                if (auto* so2 = so->get_item_by_name("solo");
                    so2 && so2->is_object())
                {
                    getIntI_(so2, "vst3Param", sb.soloVst3Param);
                    colTmp = 0;
                    if (getIntI_(so2, "colour", colTmp))
                        sb.soloColour = static_cast<uint32_t>(colTmp) & 0x00FFFFFFu;
                    getBoolI_(so2, "invert", sb.soloInvert);
                }
                if (auto* co = so->get_item_by_name("cut");
                    co && co->is_object())
                {
                    getIntI_(co, "vst3Param", sb.cutVst3Param);
                    colTmp = 0;
                    if (getIntI_(co, "colour", colTmp))
                        sb.cutColour = static_cast<uint32_t>(colTmp) & 0x00FFFFFFu;
                    getBoolI_(co, "invert", sb.cutInvert);
                }
                if (auto* selo = so->get_item_by_name("sel");
                    selo && selo->is_object())
                {
                    getIntI_(selo, "vst3Param", sb.selVst3Param);
                    colTmp = 0;
                    if (getIntI_(selo, "colour", colTmp))
                        sb.selColour = static_cast<uint32_t>(colTmp) & 0x00FFFFFFu;
                    getBoolI_(selo, "invert", sb.selInvert);
                }
                // (UF8 FX-Learn strip modifier overlays removed — UC1-only.
                // Any "modLayers" key in an old v9 file is silently ignored.)
            };
            // v7 path: stripsByFaderBank [faderBank][slot].
            if (auto* sfbArr = uo->get_item_by_name("stripsByFaderBank");
                sfbArr && sfbArr->is_array() && sfbArr->m_array)
            {
                const int fbn = (std::min)(sfbArr->m_array->GetSize(),
                                           uf8::kUserUf8FaderBankCount);
                for (int fb = 0; fb < fbn; ++fb) {
                    wdl_json_element* row = sfbArr->enum_item(fb);
                    if (!row || !row->is_array() || !row->m_array) continue;
                    const int sn = (std::min)(row->m_array->GetSize(), 8);
                    for (int s = 0; s < sn; ++s) {
                        wdl_json_element* so = row->enum_item(s);
                        if (!so || !so->is_object()) continue;
                        parseStripObj(so, m.uf8.strips[fb][s]);
                    }
                }
            }
            // v6 path: stripsByBank [topSoftKey][slot] — take row 0
            // (topSoftKey 0) into faderBank=0. v6's per-top-soft-key
            // fader specialisation is dropped (see header).
            else if (auto* sbbArr = uo->get_item_by_name("stripsByBank");
                     sbbArr && sbbArr->is_array() && sbbArr->m_array)
            {
                if (sbbArr->m_array->GetSize() > 0) {
                    wdl_json_element* row = sbbArr->enum_item(0);
                    if (row && row->is_array() && row->m_array) {
                        const int sn = (std::min)(row->m_array->GetSize(), 8);
                        for (int s = 0; s < sn; ++s) {
                            wdl_json_element* so = row->enum_item(s);
                            if (!so || !so->is_object()) continue;
                            parseStripObj(so, m.uf8.strips[0][s]);
                        }
                    }
                }
            }
            // v5 path: flat strips[8] → faderBank=0.
            else if (auto* stripsArr = uo->get_item_by_name("strips");
                     stripsArr && stripsArr->is_array() && stripsArr->m_array)
            {
                const int sn = (std::min)(stripsArr->m_array->GetSize(), 8);
                for (int s = 0; s < sn; ++s) {
                    wdl_json_element* so = stripsArr->enum_item(s);
                    if (!so || !so->is_object()) continue;
                    parseStripObj(so, m.uf8.strips[0][s]);
                }
            }
            // Legacy v6 bankLeft / bankRight blocks are skipped. The
            // Bank ←/→ buttons no longer carry per-plug-in VST3-param
            // overrides (Frank 2026-05-17 — they're reserved for
            // fader-bank toggling inside UF8 Plugin Mode).

            // Per-bank TopSoftKey LED state. New shape (single colour
            // + label); legacy entries with activeColour/inactiveColour
            // get their activeColour migrated into colour, brightness
            // fields are dropped (Frank 2026-05-13 simplified to one
            // colour, active=bright, inactive=dim fixed).
            if (auto* tslArr = uo->get_item_by_name("topSoftKeyLeds");
                tslArr && tslArr->is_array() && tslArr->m_array)
            {
                const int ln = (std::min)(tslArr->m_array->GetSize(),
                                          uf8::kUserUf8VpotBankCount);
                for (int b = 0; b < ln; ++b) {
                    wdl_json_element* lo = tslArr->enum_item(b);
                    if (!lo || !lo->is_object()) continue;
                    auto& l = m.uf8.topSoftKeyLeds[b];
                    int colTmp = 0;
                    if (getIntI_(lo, "colour", colTmp)) {
                        l.colour =
                            static_cast<uint32_t>(colTmp) & 0x00FFFFFFu;
                    } else if (getIntI_(lo, "activeColour", colTmp)) {
                        l.colour =
                            static_cast<uint32_t>(colTmp) & 0x00FFFFFFu;
                    }
                    getStrI_(lo, "label", l.label);
                }
            }
        }

        // Parse paramSnapshot array (v4+; absent in v3 files).
        // v13 shared per-parameter display names; absent on v12 and older.
        if (auto* plArr = po->get_item_by_name("paramLabels");
            plArr && plArr->is_array() && plArr->m_array)
        {
            const int ln = plArr->m_array->GetSize();
            m.paramLabels.reserve(static_cast<size_t>(ln));
            for (int i = 0; i < ln; ++i) {
                wdl_json_element* lo = plArr->enum_item(i);
                if (!lo || !lo->is_object()) continue;
                UserParamLabel pl{};
                getIntI_(lo, "vst3Param", pl.vst3Param);
                getStrI_(lo, "label", pl.label);
                if (pl.vst3Param < 0 || pl.label.empty()) continue;
                m.paramLabels.push_back(std::move(pl));
            }
        }

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

        // Parse extFuncs array (v8+; absent in v7 files). Each entry carries
        // its grid `slot` index so positions/gaps are preserved.
        if (auto* efArr = po->get_item_by_name("extFuncs");
            efArr && efArr->is_array() && efArr->m_array)
        {
            const int en = efArr->m_array->GetSize();
            for (int e = 0; e < en; ++e) {
                wdl_json_element* eo = efArr->enum_item(e);
                if (!eo || !eo->is_object()) continue;
                int slot = -1;
                getIntI_(eo, "slot", slot);
                if (slot < 0 || slot >= kUserExtFuncsCount) continue;
                getStrI_(eo, "name", m.extFuncs[slot].name);
                getIntI_(eo, "vst3Param", m.extFuncs[slot].vst3Param);
            }
        }

        if (m.match.empty()) continue;

        // v3 → v4 migration: if uf8Mode wasn't in the file, derive it from
        // the uf8 block. Maps with non-empty bank/strip bindings keep the
        // UF8 layer active; everything else opts out.
        if (!hadUf8Mode) m.uf8Mode = uf8MapHasContent_(m.uf8);
        // v10 → v11 the same way: no uf1Mode key → derive it from the uf1
        // block. A v10 file has neither, so it stays false (= the UF1 keeps
        // filling sequentially from `slots`, exactly as before).
        if (!hadUf1Mode) m.uf1Mode = uf8::uf1MapHasContent(m.uf1);

        // A map with no domain and no surface layer is meaningless — drop it
        // rather than carrying around dead entries. ⚠ UF1-ONLY maps are VALID
        // (Frank 2026-08-08), so uf1Mode counts here exactly like uf8Mode —
        // miss it and every UF1-only map is silently deleted on the next save.
        // The exchange server mirrors this rule (lib/rea60map.js) and must be
        // kept in step.
        if (m.domain == Domain::None && !m.uf8Mode && !m.uf1Mode) continue;

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
            const char* slotName = canon ? canon->name : "";
            if (!us.customLabel.empty()) {
                e.customLabelPool.push_back(us.customLabel);
                slotName = e.customLabelPool.back().c_str();
            }
            LinkSlot ls{
                us.linkIdx,
                canon ? canon->id     : "",
                slotName,
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

std::string configPath() { return configPath_(); }

bool exportToFile(const std::string& path, std::string* errOut)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    const std::string json = serialize_(g_catalog);
    if (!writeFileAtomic_(path, json)) {
        if (errOut) *errOut = "could not write " + path;
        return false;
    }
    return true;
}

bool importFromFile(const std::string& path, std::string* errOut)
{
    std::string contents;
    if (!readFile_(path, contents)) {
        if (errOut) *errOut = "could not read " + path;
        return false;
    }
    UserPluginCatalog tmp;
    tmp.formatVersion = kCurrentFormatVersion;
    if (!parse_(contents, tmp)) {
        if (errOut) *errOut = "parse error (file not a valid user_plugins.json?)";
        return false;
    }
    std::lock_guard<std::mutex> lk(g_mutex);
    g_catalog = std::move(tmp);
    ensureConfigDir_();
    if (!writeFileAtomic_(configPath_(), serialize_(g_catalog))) {
        if (errOut) *errOut = "imported, but could not persist to user_plugins.json";
        // Don't return false — in-memory state is good. UI will see
        // it; persistence error is informational.
    }
    rebuildViewCache_();
    g_generation.fetch_add(1, std::memory_order_relaxed);
    return true;
}

// ----- Single-map sharing (.rea60map) --------------------------------------

namespace {

// Every VST3 param index the map actually references — across the base layer,
// the modifier overlays, both domain slot caches, the metering pick, the UC1
// EXT FUNCS list, the UF8 V-Pot banks and the UF8 strip bindings, including
// macro/push-cycle steps. Anything not in here is noise in a shared file.
void collectUsedParams_(const UserPluginMap& m, std::set<int>& used)
{
    auto addLayer = [&](const SlotLayer& L) {
        if (L.vst3Param >= 0) used.insert(L.vst3Param);
        for (const auto& ps : L.pushSteps)
            if (ps.vst3Param >= 0) used.insert(ps.vst3Param);
    };
    auto addSlots = [&](const std::vector<UserLinkSlot>& v) {
        for (const auto& s : v) {
            addLayer(s);
            for (int i = 0; i < kNumFxLayers - 1; ++i) addLayer(s.modLayers[i]);
        }
    };
    addSlots(m.slots);
    addSlots(m.csSlotCache);
    addSlots(m.bcSlotCache);

    if (m.metering.grVst3Param >= 0) used.insert(m.metering.grVst3Param);
    for (const auto& e : m.extFuncs)
        if (e.vst3Param >= 0) used.insert(e.vst3Param);

    for (int fb = 0; fb < kUserUf8FaderBankCount; ++fb) {
        for (int vb = 0; vb < kUserUf8VpotBankCount; ++vb) {
            for (int s = 0; s < 8; ++s) {
                const auto& bs = m.uf8.banks.banks[fb][vb][s];
                if (bs.vst3Param >= 0) used.insert(bs.vst3Param);
                for (const auto& ps : bs.vpotSteps)
                    if (ps.vst3Param >= 0) used.insert(ps.vst3Param);
            }
        }
        for (int s = 0; s < 8; ++s) {
            const auto& sb = m.uf8.strips[fb][s];
            if (sb.faderVst3Param >= 0) used.insert(sb.faderVst3Param);
            if (sb.soloVst3Param  >= 0) used.insert(sb.soloVst3Param);
            if (sb.cutVst3Param   >= 0) used.insert(sb.cutVst3Param);
            if (sb.selVst3Param   >= 0) used.insert(sb.selVst3Param);
        }
    }
}

} // namespace

// Surface taxonomy — also the exchange envelope's `surfaces` value, so the
// server's VALID_SURFACES set must carry every string produced here (v11 adds
// the uf1 combinations; ship the server BEFORE a build that can emit them).
const char* surfaceScope(const UserPluginMap& m)
{
    if (m.domain == Domain::None) {
        if (m.uf8Mode && m.uf1Mode) return "uf8+uf1";
        if (m.uf8Mode)              return "uf8";
        return m.uf1Mode ? "uf1" : "";
    }
    if (m.uf8Mode && m.uf1Mode) return "uc1+uf8+uf1";
    if (m.uf8Mode)              return "uc1+uf8";
    return m.uf1Mode ? "uc1+uf1" : "uc1";
}

bool serializeMapShare(const MapShare& share, std::string& out,
                       std::string* errOut)
{
    if (share.map.match.empty()) {
        if (errOut) *errOut = "map has no match string";
        return false;
    }
    const char* scope = surfaceScope(share.map);
    if (!*scope) {
        if (errOut) *errOut = "map has no surface (domain None with uf8Mode off)";
        return false;
    }

    UserPluginMap m = share.map;
    {
        std::set<int> used;
        collectUsedParams_(m, used);
        std::vector<UserParamInfo> keep;
        keep.reserve(used.size());
        for (const auto& pi : m.paramSnapshot)
            if (used.count(pi.vst3Param)) keep.push_back(pi);
        m.paramSnapshot = std::move(keep);
    }
    // A shared map must never arrive as someone's Default: upsert() clears
    // isDefault on the recipient's own map in that domain bucket.
    m.isDefault = false;

    // serialize_ takes the catalog by const ref, so a one-entry catalog gives
    // us a single-map payload with zero serializer duplication.
    UserPluginCatalog one;
    one.formatVersion = kCurrentFormatVersion;
    one.maps.push_back(std::move(m));
    const std::string mapJson = serialize_(one);

    std::ostringstream os;
    os << "{\n";
    os << "  \"format\": \"rea-sixty-map\",\n";
    // v2 (2026-07-19) adds `original_name`; v3 (2026-07-20) adds
    // `functional_params`. Additive: the importer keys on `format`, never
    // `version`, so any version loads in any build; the server accepts them all.
    os << "  \"version\": 3,\n";
    // Envelope-level copies so the exchange can index without unescaping the
    // payload. `plugin` mirrors the map's own `match`; `original_name` is the
    // full factory name (carries the vendor), captured live — empty on maps
    // learned before v2, in which case the server falls back to `plugin`.
    os << "  \"plugin\": ";        appendEscaped_(os, share.map.match);        os << ",\n";
    os << "  \"original_name\": "; appendEscaped_(os, share.map.originalName); os << ",\n";
    // Functional param count (v3) — the denominator for parameter coverage.
    // -1 when not captured; the server then shows no parameter coverage.
    os << "  \"functional_params\": " << share.map.functionalParamCount << ",\n";
    os << "  \"vendor\": ";        appendEscaped_(os, share.vendor);           os << ",\n";
    os << "  \"surfaces\": ";    appendEscaped_(os, scope);             os << ",\n";
    os << "  \"author\": ";      appendEscaped_(os, share.author);      os << ",\n";
    os << "  \"description\": "; appendEscaped_(os, share.description); os << ",\n";
    os << "  \"licence\": ";     appendEscaped_(os, share.licence);     os << ",\n";
    os << "  \"created_at\": " << static_cast<long long>(share.createdAt) << ",\n";
    os << "  \"map\": ";         appendEscaped_(os, mapJson);           os << "\n";
    os << "}\n";

    out = os.str();
    return true;
}

bool exportMapToFile(const std::string& path, const MapShare& share,
                     std::string* errOut)
{
    std::string envelope;
    if (!serializeMapShare(share, envelope, errOut)) return false;
    if (!writeFileAtomic_(path, envelope)) {
        if (errOut) *errOut = "could not write " + path;
        return false;
    }
    return true;
}

bool importMapFromFile(const std::string& path, MapShare& out,
                       std::string* errOut)
{
    std::string contents;
    if (!readFile_(path, contents)) {
        if (errOut) *errOut = "could not read " + path;
        return false;
    }
    return importMapFromString(contents, out, errOut);
}

bool importMapFromString(const std::string& contents, MapShare& out,
                         std::string* errOut)
{
    wdl_json_parser p;
    wdl_json_element* root = p.parse(contents.c_str(),
                                     static_cast<int>(contents.size()));
    JsonTreeGuard rootGuard{p, root};
    if (!root || !root->is_object()) {
        if (errOut) *errOut = "not a JSON object";
        return false;
    }
    std::string fmt;
    if (!getStrI_(root, "format", fmt) || fmt != "rea-sixty-map") {
        if (errOut) *errOut = "not a Rea-Sixty mapping file";
        return false;
    }
    std::string mapJson;
    if (!getStrI_(root, "map", mapJson) || mapJson.empty()) {
        if (errOut) *errOut = "file carries no map payload";
        return false;
    }
    UserPluginCatalog tmp;
    tmp.formatVersion = kCurrentFormatVersion;
    if (!parse_(mapJson, tmp)) {
        if (errOut) *errOut = "map payload is not a valid catalog";
        return false;
    }
    // parse_ silently drops entries with an empty match or the invalid
    // (domain=None, uf8Mode=false) pair, so a syntactically fine file can
    // still yield nothing. Check the count, don't assume it.
    if (tmp.maps.size() != 1) {
        if (errOut) *errOut = "expected exactly one map, got "
                              + std::to_string(tmp.maps.size());
        return false;
    }
    out.map = std::move(tmp.maps[0]);
    out.map.isDefault = false;
    getStrI_(root, "vendor",      out.vendor);
    getStrI_(root, "author",      out.author);
    getStrI_(root, "description", out.description);
    getStrI_(root, "licence",     out.licence);
    out.createdAt = 0;
    if (auto* el = root->get_item_by_name("created_at")) {
        if (const char* s = el->get_string_value(true)) out.createdAt = atoll(s);
    }
    return true;
}

namespace {
std::atomic<bool>    g_saveDirty{false};
std::atomic<int64_t> g_saveMarkedAt{0};
int64_t nowMsCat_()
{
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}
}  // namespace

void saveSoon()
{
    g_saveMarkedAt.store(nowMsCat_(), std::memory_order_relaxed);
    g_saveDirty.store(true, std::memory_order_release);
}

bool savePending() { return g_saveDirty.load(std::memory_order_acquire); }

SaveResult saveFlush(bool force, int quietMs)
{
    if (!g_saveDirty.load(std::memory_order_acquire)) return SaveResult::Ok;
    if (!force) {
        const int64_t since =
            nowMsCat_() - g_saveMarkedAt.load(std::memory_order_relaxed);
        if (since < quietMs) return SaveResult::Ok;
    }
    // Cleared BEFORE the write: an edit landing during the write marks the flag
    // again and the next flush picks it up. Clearing after would swallow it.
    g_saveDirty.store(false, std::memory_order_release);
    return save();
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

bool captureOriginalName(std::string_view match, std::string_view originalName)
{
    if (match.empty() || originalName.empty()) return false;
    bool changed = false;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        for (auto& m : g_catalog.maps) {
            if (m.match == match) {
                if (m.originalName.compare(0, std::string::npos,
                                          originalName.data(),
                                          originalName.size()) != 0) {
                    m.originalName.assign(originalName.data(), originalName.size());
                    changed = true;
                }
                break;
            }
        }
    }
    // save() takes g_mutex itself, so it is called AFTER the guard above is
    // released. Only on a real change — the focus-follow path fires often and
    // must not write the catalog every frame. The view cache does not depend
    // on originalName, so no rebuild / generation bump is needed.
    if (changed) save();
    return changed;
}

// Sibling of captureOriginalName for the v3 functional-param count. Same reason
// to exist: the count is only set when a paramSnapshot is (re)taken (learn /
// focus / create-bind), so a map learned before v3 — or restored from ExtState
// and never re-snapshotted — ships functionalParamCount == -1 and the exchange
// records NO parameter coverage. Called from the Share-time FX scan (which has
// the live plug-in in hand) so a re-Share fills it, exactly as original_name is.
// Seed an EMPTY UF1 map with exactly what the sequential fallback currently
// shows, so switching a plug-in's UF1 layer on changes NOTHING visible — the
// layout the user already sees simply becomes editable. Without this, enabling
// the layer (or auto-enabling it on the first learn gesture) would replace a
// working automatic layout with a blank grid, which is a downgrade dressed as a
// feature (Frank 2026-08-08). No-op when the UF1 map already carries anything.
//
// Mirrors uf1LearnedStreamSlots_ in main.cpp: mapped slots only, split by
// uc1::linkIdxIsButton (buttons → soft-keys, rest → V-Pots), each stream sorted
// by linkIdx and packed from position 0. Keep the two in step.
void seedUf1FromSlots(UserPluginMap& m)
{
    if (uf1MapHasContent(m.uf1)) return;
    const bool busComp = (m.domain == Domain::BusComp);
    std::vector<const UserLinkSlot*> vp, sk;
    for (const auto& s : m.slots) {
        if (s.vst3Param < 0) continue;
        (uc1::linkIdxIsButton(s.linkIdx, busComp) ? sk : vp).push_back(&s);
    }
    auto byLink = [](const UserLinkSlot* a, const UserLinkSlot* b) {
        return a->linkIdx < b->linkIdx;
    };
    std::sort(vp.begin(), vp.end(), byLink);
    std::sort(sk.begin(), sk.end(), byLink);
    auto fill = [](const std::vector<const UserLinkSlot*>& src,
                   std::vector<UserUf1Slot>& dst) {
        dst.clear();
        for (size_t i = 0; i < src.size(); ++i) {
            UserUf1Slot s{};
            s.pos         = static_cast<int>(i);
            s.vst3Param   = src[i]->vst3Param;
            s.inverted    = src[i]->inverted;
            s.customLabel = src[i]->customLabel;
            dst.push_back(std::move(s));
        }
    };
    fill(vp, m.uf1.vpots);
    fill(sk, m.uf1.softKeys);
}
// Turn the UF1 layer on for `match`, seeding it on the way. Returns true when
// the catalog changed. Safe to call repeatedly — a populated map is left alone.
bool enableUf1Layer(std::string_view match)
{
    auto cat = get();                       // copy
    for (auto& m : cat.maps) {
        if (m.match != match) continue;
        // Seed ONLY when the layer was off. An empty layer that is already on
        // is a deliberate clear ("show nothing on the UF1"), and re-seeding it
        // from the UC1 slots would resurrect every parameter the user just
        // unbound as soon as they learned one control (Frank 2026-08-09).
        if (m.uf1Mode) return false;
        seedUf1FromSlots(m);
        m.uf1Mode = true;
        // upsert, not setAll: this is reachable from an editor frame (the UF1
        // fills call it), and setAll replaces the whole catalog mid-draw.
        upsert(m);
        save();
        return true;
    }
    return false;
}

bool captureFunctionalParamCount(std::string_view match, int count)
{
    if (match.empty() || count < 0) return false;
    bool changed = false;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        for (auto& m : g_catalog.maps) {
            if (m.match == match) {
                if (m.functionalParamCount != count) {
                    m.functionalParamCount = count;
                    changed = true;
                }
                break;
            }
        }
    }
    if (changed) save();
    return changed;
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

const UserLinkSlot* lookupOwnedSlot(std::string_view fxName, int linkIdx)
{
    const UserPluginMap* m = lookupOwnedByName(fxName);
    if (!m) return nullptr;
    for (const auto& sl : m->slots) {
        if (sl.linkIdx == linkIdx) return &sl;
    }
    return nullptr;
}

// The user's shared display name for a parameter (v13), or "" when unset.
// Read by ALL THREE surfaces so one rename shows up everywhere.
std::string paramLabelFor(std::string_view fxName, int vst3Param)
{
    if (vst3Param < 0) return {};
    const UserPluginMap* m = lookupOwnedByName(fxName);
    if (!m) return {};
    for (const auto& pl : m->paramLabels)
        if (pl.vst3Param == vst3Param) return pl.label;
    return {};
}

// Set / clear (empty label) the shared name. Returns true when the catalog
// changed; the caller persists.
bool setParamLabel(UserPluginMap& m, int vst3Param, std::string_view label)
{
    if (vst3Param < 0) return false;
    for (size_t i = 0; i < m.paramLabels.size(); ++i) {
        if (m.paramLabels[i].vst3Param != vst3Param) continue;
        if (label.empty()) { m.paramLabels.erase(m.paramLabels.begin() + i); return true; }
        if (m.paramLabels[i].label == label) return false;      // no-op
        m.paramLabels[i].label = std::string(label);
        return true;
    }
    if (label.empty()) return false;
    m.paramLabels.push_back(UserParamLabel{vst3Param, std::string(label)});
    return true;
}

bool collidesWithBuiltin(std::string_view match)
{
    return collidesWithBuiltin_(match);
}

} // namespace uf8::user_plugins

namespace uf8 {

// Piecewise-linear forward evaluator. See header for contract.
//
// t∈[0..1] walks segments (0,rangeMin) → curvePoints[i] → (1,rangeMax).
// Curve point x values are clamped to [0..1]; the segment-walk assumes
// they are sorted by x (editor enforces this on edit). An unsorted list
// won't crash — it just produces a fold-back curve, which the editor's
// drag handling actively prevents.
float applyCurve(float t, float rangeMin, float rangeMax,
                 const std::vector<std::pair<float, float>>& curvePoints)
{
    if (t < 0.0f) t = 0.0f;
    else if (t > 1.0f) t = 1.0f;
    if (curvePoints.empty()) {
        return rangeMin + t * (rangeMax - rangeMin);
    }
    float prevX = 0.0f, prevY = rangeMin;
    for (const auto& pt : curvePoints) {
        float xi = pt.first;
        float yi = pt.second;
        if (xi < 0.0f) xi = 0.0f; else if (xi > 1.0f) xi = 1.0f;
        if (yi < 0.0f) yi = 0.0f; else if (yi > 1.0f) yi = 1.0f;
        if (t <= xi) {
            const float span = xi - prevX;
            const float u    = (span > 1e-6f) ? (t - prevX) / span : 0.0f;
            return prevY + u * (yi - prevY);
        }
        prevX = xi;
        prevY = yi;
    }
    const float span = 1.0f - prevX;
    const float u    = (span > 1e-6f) ? (t - prevX) / span : 0.0f;
    return prevY + u * (rangeMax - prevY);
}

// Inverse evaluator — finds the first segment whose y-range contains v
// and linear-interpolates back to t. With a non-monotonic curve the
// "first match" determines which encoder-position the FX value gets
// mapped to; users get what they draw.
//
// If v lies outside the curve's reachable y range (e.g. external
// automation moved the param past rangeMax), the helper clamps to the
// nearest endpoint t — subsequent encoder turns then move within the
// curve's reachable region rather than jumping.
float inverseCurve(float v, float rangeMin, float rangeMax,
                   const std::vector<std::pair<float, float>>& curvePoints)
{
    if (v < 0.0f) v = 0.0f;
    else if (v > 1.0f) v = 1.0f;
    if (curvePoints.empty()) {
        const float span = rangeMax - rangeMin;
        if (span > 1e-6f || span < -1e-6f) {
            const float t = (v - rangeMin) / span;
            if (t < 0.0f) return 0.0f;
            if (t > 1.0f) return 1.0f;
            return t;
        }
        return 0.0f;
    }
    float prevX = 0.0f, prevY = rangeMin;
    const size_t nPts = curvePoints.size();
    for (size_t i = 0; i <= nPts; ++i) {
        float nextX, nextY;
        if (i < nPts) {
            nextX = curvePoints[i].first;
            nextY = curvePoints[i].second;
            if (nextX < 0.0f) nextX = 0.0f; else if (nextX > 1.0f) nextX = 1.0f;
            if (nextY < 0.0f) nextY = 0.0f; else if (nextY > 1.0f) nextY = 1.0f;
        } else {
            nextX = 1.0f;
            nextY = rangeMax;
        }
        const float yLo = (prevY < nextY) ? prevY : nextY;
        const float yHi = (prevY < nextY) ? nextY : prevY;
        if (v >= yLo && v <= yHi) {
            const float yspan = nextY - prevY;
            const float u = (yspan > 1e-6f || yspan < -1e-6f)
                                ? (v - prevY) / yspan
                                : 0.0f;
            return prevX + u * (nextX - prevX);
        }
        prevX = nextX;
        prevY = nextY;
    }
    // v out of every segment's y-range — snap to the endpoint t closest
    // to v in param space.
    const float dLo = (v > rangeMin) ? v - rangeMin : rangeMin - v;
    const float dHi = (v > rangeMax) ? v - rangeMax : rangeMax - v;
    return (dLo <= dHi) ? 0.0f : 1.0f;
}

// Stepped-parameter helpers — see UserPluginCatalog.h for the rationale.
// pStep is REAPER's per-step size in normalised [0..1] space, returned by
// TrackFX_GetParameterStepSizes.
float snapToStep(float v, float pStep)
{
    if (pStep <= 0.0f) return v;
    if (v < 0.0f) v = 0.0f;
    else if (v > 1.0f) v = 1.0f;
    const float n = std::floor(v / pStep + 0.5f);
    float snapped = n * pStep;
    if (snapped < 0.0f) snapped = 0.0f;
    if (snapped > 1.0f) snapped = 1.0f;
    return snapped;
}

int numStepsFor(float pStep)
{
    if (pStep <= 0.0f) return 2;
    const int n = static_cast<int>(std::floor(1.0f / pStep + 0.5f)) + 1;
    return (n < 2) ? 2 : n;
}

// Map signed raw-detent events into logical step ticks via a fractional
// accumulator. sensitivity is the user-facing knob from the editor;
// effRate (logical steps emitted per detent) = clamp(sens, 0.1, 8) * 0.5.
// That keeps the default sens=1.0 at "2 detents per step", matching the
// pre-feature UC1 baseline (UC1Surface.cpp:1253).
SteppedTickResult tickStepped(float accum, int rawDetents, float sensitivity)
{
    // Floor low enough that Shift-fine (×0.25) on the editor's slider
    // minimum (0.1) still slows further — clamping to 0.1 here would
    // swallow Shift at the user's lowest setting (Frank 2026-05-26).
    // Upper clamp keeps an absurd typed-in value (>8 → 4 steps per
    // detent) from making the encoder feel uncontrollable.
    if (sensitivity < 0.001f) sensitivity = 0.001f;
    else if (sensitivity > 8.0f) sensitivity = 8.0f;
    const float effRate = sensitivity * 0.5f;
    if ((rawDetents > 0 && accum < 0.0f)
        || (rawDetents < 0 && accum > 0.0f)) {
        accum = 0.0f;
    }
    accum += static_cast<float>(rawDetents) * effRate;
    const int logical = static_cast<int>(accum);
    accum -= static_cast<float>(logical);
    return SteppedTickResult{ logical, accum };
}

double jsfxContinuousStep(int rawDetents)
{
    const int d = std::abs(rawDetents);
    if (d <= 0) return 0.0;
    // kBase = slow-detent step (d=1) ≈ 1/33 of range — ~2× the old
    // sluggish 1/64. kGain shapes the sqrt accel so a fast flick (d≈14)
    // lands near 1/16 (≈ the speed Frank confirmed "passt"), without the
    // raw-linear ~1/5 overshoot. Tunable; raise kBase for faster slow
    // turns, lower kGain to flatten the fast end.
    constexpr double kBase = 1.0 / 33.0;
    constexpr double kGain = 0.30;
    const double mag = kBase
        * (1.0 + kGain * std::sqrt(static_cast<double>(d - 1)));
    return rawDetents < 0 ? -mag : mag;
}

double jsfxGridFineStep(int rawDetents, double normStep)
{
    const int d = std::abs(rawDetents);
    if (d <= 0 || normStep <= 0.0) return 0.0;
    // 1 detent = exactly 1 native slider increment (max precision). A fast
    // flick piles on a few more via a mild sqrt so a hard spin still travels
    // without leaving Fine; slow turns stay strictly 1:1. kAccel tunes how
    // quickly the extra increments accumulate — keep it modest, Fine is for
    // nudging (release Fine for the analog fast-travel curve).
    constexpr double kAccel = 0.6;
    const double units = 1.0 + kAccel * std::sqrt(static_cast<double>(d - 1));
    // NOT std::max(1, …): on MSVC the windows.h `max` macro expands and breaks
    // `std::max(` (error C2589). Manual clamp instead. See msvc-minmax trap.
    int n = static_cast<int>(units + 0.5);
    if (n < 1) n = 1;
    const double mag = static_cast<double>(n) * normStep;
    return rawDetents < 0 ? -mag : mag;
}

double jsfxAccumulate(double& wantN, double curN, double intendedNext)
{
    // Re-sync when the virtual position has drifted past a few slider
    // quanta from the live value — means the param was changed elsewhere
    // (mouse) or this is the first use, so don't fight a stale target.
    // Within that band, keep accumulating: writing the un-rounded `want`
    // lets the JSFX slider step once the residual crosses its grid.
    constexpr double kResync = 0.05;
    if (std::abs(wantN - curN) > kResync) wantN = curN;
    double want = wantN + (intendedNext - curN);
    if (want < 0.0) want = 0.0;
    else if (want > 1.0) want = 1.0;
    wantN = want;
    return want;
}

} // namespace uf8
