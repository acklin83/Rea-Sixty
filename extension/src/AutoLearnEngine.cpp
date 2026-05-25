//
// AutoLearnEngine — pattern-based auto-mapping for FX Learn.
//
// Two-layer dictionary:
//   1. Hardcoded base — curated from SSL Native built-in maps + common
//      third-party naming conventions (case-insensitive).
//   2. User maps — merged at runtime from paramSnapshot + slot bindings
//      in the user_plugins catalog. Overrides base on conflict.
//
// Matching runs three passes with decreasing confidence:
//   Exact (1.0) → Substring (0.8) → Token (0.6)
// Frequency bonus: +0.1 when the same pattern→linkIdx appears in 3+ sources.
//

#include "AutoLearnEngine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>

#include "PluginMap.h"           // ext::*, allPluginMaps, LinkSlot

namespace uf8 {
namespace autolearn {
namespace {

// ---- Normalisation ---------------------------------------------------------

std::string toLower(const std::string& s)
{
    std::string r = s;
    for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

// Detect a "CH<N>" / "Channel <N>" / "Chan <N>" / "Ch<N>" prefix and return
// the 1-based channel index. Returns 0 when no prefix is found. Tolerates a
// space, hyphen, dot or underscore between prefix and digits ("CH 1",
// "Ch-1", "ch.1", "Ch_1"). Pass a lowercased name in.
int extractChannelIndex(const std::string& lower)
{
    const char* prefixes[] = { "channel", "chan", "ch" };
    for (const char* pfx : prefixes) {
        const size_t L = std::strlen(pfx);
        if (lower.size() <= L) continue;
        if (lower.compare(0, L, pfx) != 0) continue;
        size_t i = L;
        while (i < lower.size() &&
               (lower[i] == ' ' || lower[i] == '-' ||
                lower[i] == '.' || lower[i] == '_')) ++i;
        if (i >= lower.size() ||
            !std::isdigit(static_cast<unsigned char>(lower[i]))) continue;
        int n = 0;
        while (i < lower.size() &&
               std::isdigit(static_cast<unsigned char>(lower[i]))) {
            n = n * 10 + (lower[i] - '0');
            ++i;
        }
        if (n >= 1) return n;
    }
    return 0;
}

// Identify params claimed by suggestUf8Strips (CH<N> Vol/Level/Mute/etc.).
// suggestSlots and suggestUf8Banks both skip these so the channel-mixer
// params get a single, correctly-positioned home in the strip suggestion
// list rather than getting double-mapped to V-Pots or singular SSL slots.
bool isChannelStripCandidate(const std::string& lower)
{
    if (extractChannelIndex(lower) < 1) return false;
    return lower.find("volume") != std::string::npos
        || lower.find("level")  != std::string::npos
        || lower.find("fader")  != std::string::npos
        || lower.find(" vol")   != std::string::npos
        || (lower.size() >= 3 &&
            lower.compare(lower.size() - 3, 3, "vol") == 0)
        || lower.find("output") != std::string::npos
        || lower.find("mute")   != std::string::npos
        || lower.find(" cut")   != std::string::npos
        || (lower.size() >= 3 &&
            lower.compare(lower.size() - 3, 3, "cut") == 0)
        || lower.find("solo")   != std::string::npos
        || lower.find("select") != std::string::npos
        || lower.find(" sel")   != std::string::npos
        || (lower.size() >= 3 &&
            lower.compare(lower.size() - 3, 3, "sel") == 0);
}

// Strip leading/trailing whitespace + collapse internal runs to single space.
std::string normalise(const std::string& s)
{
    std::string r;
    bool lastSpace = true;
    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!lastSpace) { r += ' '; lastSpace = true; }
        } else {
            r += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            lastSpace = false;
        }
    }
    if (!r.empty() && r.back() == ' ') r.pop_back();
    return r;
}

// Split on whitespace into tokens.
std::vector<std::string> tokenise(const std::string& norm)
{
    std::vector<std::string> out;
    std::string tok;
    for (char c : norm) {
        if (c == ' ') {
            if (!tok.empty()) { out.push_back(tok); tok.clear(); }
        } else {
            tok += c;
        }
    }
    if (!tok.empty()) out.push_back(tok);
    return out;
}

// ---- Dictionary entry ------------------------------------------------------

struct DictEntry {
    int         linkIdx;
    Domain      domain;       // ChannelStrip, BusComp, or None (= both)
    std::string category;     // "EQ", "Comp", "Gate", "Filter", "I/O", "Misc"
    std::string displayName;  // canonical name for the suggestion
    int         sourceCount = 1;  // how many sources contributed this pattern
    bool        fromUser = false; // true = added by mergeUserMaps (lower
                                  // confidence cap; base seeds win 1.0)
};

// Key = normalised param-name pattern. Value = best DictEntry for that pattern.
using Dict = std::unordered_map<std::string, DictEntry>;

// ---- Hardcoded base dictionary ---------------------------------------------
// Derived from SSL Native built-in maps (PluginMap.cpp) + common third-party
// naming conventions. Patterns are lowercase. Multiple patterns can point to
// the same linkIdx.

struct SeedRow {
    const char* pattern;
    int         linkIdx;
    Domain      domain;
    const char* category;
    const char* displayName;
};

// CS = ChannelStrip, BC = BusComp, N = None (applies to either when domain matches)
static const SeedRow kCsSeeds[] = {
    // -- Filters --
    {"lpf",               6, Domain::ChannelStrip, "Filter", "LPF"},
    {"low pass",          6, Domain::ChannelStrip, "Filter", "LPF"},
    {"lowpass",           6, Domain::ChannelStrip, "Filter", "LPF"},
    {"low cut",           6, Domain::ChannelStrip, "Filter", "LPF"},
    {"hpf",               7, Domain::ChannelStrip, "Filter", "HPF"},
    {"high pass",         7, Domain::ChannelStrip, "Filter", "HPF"},
    {"highpass",          7, Domain::ChannelStrip, "Filter", "HPF"},
    {"high cut",          7, Domain::ChannelStrip, "Filter", "HPF"},
    // -- EQ HF --
    {"hf type",           8, Domain::ChannelStrip, "EQ", "HF Type"},
    {"hf bell",           8, Domain::ChannelStrip, "EQ", "HF Type"},
    {"hf gain",           9, Domain::ChannelStrip, "EQ", "HF Gain"},
    {"high gain",         9, Domain::ChannelStrip, "EQ", "HF Gain"},
    {"hf freq",          10, Domain::ChannelStrip, "EQ", "HF Freq"},
    {"hf frequency",     10, Domain::ChannelStrip, "EQ", "HF Freq"},
    {"high frequency",   10, Domain::ChannelStrip, "EQ", "HF Freq"},
    {"high freq",        10, Domain::ChannelStrip, "EQ", "HF Freq"},
    // -- EQ HMF --
    {"hmf gain",         11, Domain::ChannelStrip, "EQ", "HMF Gain"},
    {"high mid gain",    11, Domain::ChannelStrip, "EQ", "HMF Gain"},
    {"hi mid gain",      11, Domain::ChannelStrip, "EQ", "HMF Gain"},
    {"hmf freq",         12, Domain::ChannelStrip, "EQ", "HMF Freq"},
    {"hmf frequency",    12, Domain::ChannelStrip, "EQ", "HMF Freq"},
    {"high mid freq",    12, Domain::ChannelStrip, "EQ", "HMF Freq"},
    {"hi mid freq",      12, Domain::ChannelStrip, "EQ", "HMF Freq"},
    {"hmf q",            13, Domain::ChannelStrip, "EQ", "HMF Q"},
    {"high mid q",       13, Domain::ChannelStrip, "EQ", "HMF Q"},
    {"hi mid q",         13, Domain::ChannelStrip, "EQ", "HMF Q"},
    // -- EQ global --
    {"eq type",          14, Domain::ChannelStrip, "EQ", "EQ Type"},
    {"eq colour",        14, Domain::ChannelStrip, "EQ", "EQ Type"},
    {"eq color",         14, Domain::ChannelStrip, "EQ", "EQ Type"},
    {"eq in",            15, Domain::ChannelStrip, "EQ", "EQ In"},
    {"eq on",            15, Domain::ChannelStrip, "EQ", "EQ In"},
    {"eq enable",        15, Domain::ChannelStrip, "EQ", "EQ In"},
    // -- EQ LMF --
    {"lmf gain",         16, Domain::ChannelStrip, "EQ", "LMF Gain"},
    {"low mid gain",     16, Domain::ChannelStrip, "EQ", "LMF Gain"},
    {"lo mid gain",      16, Domain::ChannelStrip, "EQ", "LMF Gain"},
    {"lmf freq",         17, Domain::ChannelStrip, "EQ", "LMF Freq"},
    {"lmf frequency",    17, Domain::ChannelStrip, "EQ", "LMF Freq"},
    {"low mid freq",     17, Domain::ChannelStrip, "EQ", "LMF Freq"},
    {"lo mid freq",      17, Domain::ChannelStrip, "EQ", "LMF Freq"},
    {"lmf q",            18, Domain::ChannelStrip, "EQ", "LMF Q"},
    {"low mid q",        18, Domain::ChannelStrip, "EQ", "LMF Q"},
    {"lo mid q",         18, Domain::ChannelStrip, "EQ", "LMF Q"},
    // -- EQ LF --
    {"lf freq",          19, Domain::ChannelStrip, "EQ", "LF Freq"},
    {"lf frequency",     19, Domain::ChannelStrip, "EQ", "LF Freq"},
    {"low frequency",    19, Domain::ChannelStrip, "EQ", "LF Freq"},
    {"low freq",         19, Domain::ChannelStrip, "EQ", "LF Freq"},
    {"lf gain",          20, Domain::ChannelStrip, "EQ", "LF Gain"},
    {"low gain",         20, Domain::ChannelStrip, "EQ", "LF Gain"},
    {"lf type",          21, Domain::ChannelStrip, "EQ", "LF Type"},
    {"lf bell",          21, Domain::ChannelStrip, "EQ", "LF Type"},
    // -- CS Dynamics / Compressor --
    {"dynamics in",      22, Domain::ChannelStrip, "Comp", "Dynamics In"},
    {"dyn in",           22, Domain::ChannelStrip, "Comp", "Dynamics In"},
    {"comp mix",         23, Domain::ChannelStrip, "Comp", "Comp Mix"},
    {"comp fast attack", 24, Domain::ChannelStrip, "Comp", "Comp F.Atk"},
    {"comp f.atk",       24, Domain::ChannelStrip, "Comp", "Comp F.Atk"},
    {"comp peak",        25, Domain::ChannelStrip, "Comp", "Comp Peak"},
    {"comp ratio",       26, Domain::ChannelStrip, "Comp", "Comp Ratio"},
    {"comp threshold",   27, Domain::ChannelStrip, "Comp", "Comp Thr"},
    {"comp thr",         27, Domain::ChannelStrip, "Comp", "Comp Thr"},
    {"comp release",     28, Domain::ChannelStrip, "Comp", "Comp Rel"},
    {"comp rel",         28, Domain::ChannelStrip, "Comp", "Comp Rel"},
    // -- CS Gate --
    {"gate range",       29, Domain::ChannelStrip, "Gate", "Gate Range"},
    {"gate threshold",   30, Domain::ChannelStrip, "Gate", "Gate Thr"},
    {"gate thr",         30, Domain::ChannelStrip, "Gate", "Gate Thr"},
    {"gate release",     31, Domain::ChannelStrip, "Gate", "Gate Rel"},
    {"gate rel",         31, Domain::ChannelStrip, "Gate", "Gate Rel"},
    {"gate hold",        32, Domain::ChannelStrip, "Gate", "Gate Hold"},
    {"gate expander",    33, Domain::ChannelStrip, "Gate", "Gate/Exp"},
    {"gate/exp",         33, Domain::ChannelStrip, "Gate", "Gate/Exp"},
    {"gate fast attack", 34, Domain::ChannelStrip, "Gate", "Gate F.Atk"},
    {"gate f.atk",       34, Domain::ChannelStrip, "Gate", "Gate F.Atk"},
    {"gate attack",      34, Domain::ChannelStrip, "Gate", "Gate F.Atk"},
    // -- CS I/O --
    {"input trim",        4, Domain::ChannelStrip, "I/O", "Input Trim"},
    {"input",             4, Domain::ChannelStrip, "I/O", "Input Trim"},
    {"polarity",          5, Domain::ChannelStrip, "I/O", "Polarity"},
    {"phase",             5, Domain::ChannelStrip, "I/O", "Polarity"},
    {"output trim",      37, Domain::ChannelStrip, "I/O", "Out Trim"},
    {"output",           37, Domain::ChannelStrip, "I/O", "Out Trim"},
    {"pan",               3, Domain::ChannelStrip, "I/O", "Pan"},
    {"width",             2, Domain::ChannelStrip, "I/O", "Width"},
    {"s/c listen",       36, Domain::ChannelStrip, "Comp", "S/C Listen"},
};

static const SeedRow kBcSeeds[] = {
    {"threshold",         1, Domain::BusComp, "Comp", "Threshold"},
    {"thresh",            1, Domain::BusComp, "Comp", "Threshold"},
    {"thr",               1, Domain::BusComp, "Comp", "Threshold"},
    {"makeup",            2, Domain::BusComp, "Comp", "Makeup"},
    {"make-up",           2, Domain::BusComp, "Comp", "Makeup"},
    {"make up",           2, Domain::BusComp, "Comp", "Makeup"},
    {"output gain",       2, Domain::BusComp, "Comp", "Makeup"},
    {"attack",            3, Domain::BusComp, "Comp", "Attack"},
    {"atk",               3, Domain::BusComp, "Comp", "Attack"},
    {"release",           4, Domain::BusComp, "Comp", "Release"},
    {"rel",               4, Domain::BusComp, "Comp", "Release"},
    {"ratio",             5, Domain::BusComp, "Comp", "Ratio"},
    {"sidechain hpf",     6, Domain::BusComp, "Comp", "S/C HPF"},
    {"sc hpf",            6, Domain::BusComp, "Comp", "S/C HPF"},
    {"s/c hpf",           6, Domain::BusComp, "Comp", "S/C HPF"},
    {"sidechain filter",  6, Domain::BusComp, "Comp", "S/C HPF"},
    {"mix",               7, Domain::BusComp, "Comp", "Mix"},
    {"dry/wet",           7, Domain::BusComp, "Comp", "Mix"},
    {"dry wet",           7, Domain::BusComp, "Comp", "Mix"},
    {"wet",               7, Domain::BusComp, "Comp", "Mix"},
    {"knee",              7, Domain::BusComp, "Comp", "Mix"},  // fallback if no mix
};

// Generic patterns that apply when no domain-specific match is found.
// These use higher linkIdx values (CS namespace) as fallback.
static const SeedRow kGenericSeeds[] = {
    // Generic compressor (mapped to CS comp slots)
    {"threshold",        27, Domain::None, "Comp", "Comp Thr"},
    {"thresh",           27, Domain::None, "Comp", "Comp Thr"},
    {"thr",              27, Domain::None, "Comp", "Comp Thr"},
    {"ratio",            26, Domain::None, "Comp", "Comp Ratio"},
    {"attack",           24, Domain::None, "Comp", "Comp F.Atk"},
    {"atk",              24, Domain::None, "Comp", "Comp F.Atk"},
    {"release",          28, Domain::None, "Comp", "Comp Rel"},
    {"rel",              28, Domain::None, "Comp", "Comp Rel"},
    {"makeup",           37, Domain::None, "I/O",  "Out Trim"},
    {"make-up",          37, Domain::None, "I/O",  "Out Trim"},
    {"mix",              23, Domain::None, "Comp", "Comp Mix"},
    {"dry/wet",          23, Domain::None, "Comp", "Comp Mix"},
    {"dry wet",          23, Domain::None, "Comp", "Comp Mix"},
    {"input",             4, Domain::None, "I/O",  "Input Trim"},
    {"output",           37, Domain::None, "I/O",  "Out Trim"},
    // Generic EQ
    {"frequency",        12, Domain::None, "EQ", "HMF Freq"},
    {"gain",              9, Domain::None, "EQ", "HF Gain"},
    {"q",                13, Domain::None, "EQ", "HMF Q"},
    {"bandwidth",        13, Domain::None, "EQ", "HMF Q"},
    // Bypass — linkIdx 0 in both CS and BC topologies. Frank
    // 2026-05-24: the engine used to hard-skip "bypass" params at
    // the top of suggestSlots, so the slot stayed unmapped even on
    // plug-ins that literally have a "Bypass" param.
    {"bypass",            0, Domain::None, "Misc", "Bypass"},
};

// ---- Build dictionary ------------------------------------------------------

Dict buildBaseDict(Domain domain)
{
    Dict dict;

    auto addSeeds = [&](const SeedRow* rows, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            const auto& r = rows[i];
            // Skip entries that don't match the requested domain.
            if (r.domain != Domain::None && r.domain != domain) continue;
            std::string key = r.pattern;  // already lowercase
            DictEntry entry{r.linkIdx, r.domain, r.category, r.displayName, 1};
            auto it = dict.find(key);
            if (it == dict.end()) {
                dict[key] = entry;
            } else {
                // Domain-specific entry wins over generic.
                if (it->second.domain == Domain::None && entry.domain != Domain::None)
                    it->second = entry;
                it->second.sourceCount++;
            }
        }
    };

    // Add domain-specific seeds first (higher priority), then generic.
    if (domain == Domain::BusComp) {
        addSeeds(kBcSeeds, sizeof(kBcSeeds) / sizeof(kBcSeeds[0]));
    } else if (domain == Domain::ChannelStrip) {
        addSeeds(kCsSeeds, sizeof(kCsSeeds) / sizeof(kCsSeeds[0]));
    }
    addSeeds(kGenericSeeds, sizeof(kGenericSeeds) / sizeof(kGenericSeeds[0]));

    return dict;
}

void mergeUserMaps(Dict& dict, Domain domain)
{
    const auto& cat = user_plugins::get();
    for (const auto& m : cat.maps) {
        // Only merge maps from the same domain (or any domain for UF8-only).
        if (m.domain != domain && m.domain != Domain::None) continue;
        // Walk slots and correlate with paramSnapshot names.
        for (const auto& slot : m.slots) {
            // Find the param name from the snapshot.
            for (const auto& pi : m.paramSnapshot) {
                if (pi.vst3Param != slot.vst3Param) continue;
                if (pi.name.empty()) break;
                std::string key = normalise(pi.name);
                if (key.empty()) break;

                // Look up the canonical display name from built-in maps.
                // CRITICAL: filter by the user-map's domain. linkIdx is
                // NOT unique across domains — e.g. linkIdx 1 is
                // FaderLevel in CS but Threshold in BC. Without this
                // filter the first matching PluginMap wins regardless
                // of domain, so user-mapped CS slots get tagged with
                // BC slot names and AutoLearn happily suggests
                // "Threshold ← Out Gain @ 100%" on every new map
                // (Frank 2026-05-24).
                std::string dispName;
                for (const auto& bm : allPluginMaps()) {
                    if (bm.domain != m.domain) continue;
                    const auto* ls = findSlotByLinkIdx(bm, slot.linkIdx);
                    if (ls && ls->name) { dispName = ls->name; break; }
                }
                if (dispName.empty()) dispName = pi.name;

                // Determine category from built-in seeds or fall back.
                std::string cat2 = "Misc";
                auto it = dict.find(key);
                if (it != dict.end()) {
                    // Existing entry — only count this user-map as a
                    // corroborating source when the slot matches.
                    // Conflict (different linkIdx) is silently dropped:
                    // do NOT overwrite a base seed or an earlier user
                    // entry with potentially-bad data (Frank 2026-05-24,
                    // accidental Width binding on bx_console caused
                    // every "LC Threshold" param to suggest "Width @
                    // 100%" across all later plug-ins).
                    if (it->second.linkIdx == slot.linkIdx) {
                        it->second.sourceCount++;
                    }
                } else {
                    // Insert new entry from user map.
                    dict[key] = {slot.linkIdx, m.domain, cat2, dispName,
                                 1, /*fromUser*/ true};
                }
                break;
            }
        }
    }
}

// ---- Matching --------------------------------------------------------------

struct Match {
    int   linkIdx;
    float confidence;
    std::string slotName;
    std::string category;
};

// Try exact match (normalised) → confidence 1.0 for base seeds, capped
// lower for user-learned entries. Single-source user maps top out at
// 0.75 (yellow zone) so Frank gets a visual cue to verify — accidental
// or domain-specific user mappings shouldn't claim canonical authority.
// Sourcecount ≥ 3 means the same pattern appeared in multiple user
// maps and treats the entry as reliable (0.95).
bool tryExact(const Dict& dict, const std::string& norm, Match& out)
{
    auto it = dict.find(norm);
    if (it == dict.end()) return false;
    float conf = 1.0f;
    if (it->second.fromUser) {
        conf = (it->second.sourceCount >= 3) ? 0.95f : 0.75f;
    }
    // Hard cap at 1.0 — earlier code added +0.05 for sourceCount≥3 and
    // produced 105% renders (Frank 2026-05-24). Base-seed exact matches
    // already top out at 1.0; the bump was redundant.
    if (conf > 1.0f) conf = 1.0f;
    out = {it->second.linkIdx, conf, it->second.displayName,
           it->second.category};
    return true;
}

// Try substring: dictionary pattern is contained in param name → 0.8.
bool trySubstring(const Dict& dict, const std::string& norm, Match& out)
{
    Match best{-1, 0.0f, {}, {}};
    for (const auto& [pat, entry] : dict) {
        if (pat.size() < 3) continue;  // skip very short patterns for substring
        if (norm.find(pat) != std::string::npos) {
            // Prefer longer pattern matches (more specific).
            float conf = 0.8f + static_cast<float>(pat.size()) * 0.005f;
            if (entry.sourceCount >= 3) conf += 0.05f;
            if (conf > best.confidence) {
                best = {entry.linkIdx, conf, entry.displayName, entry.category};
            }
        }
    }
    if (best.linkIdx < 0) return false;
    out = best;
    // Cap at 0.95 to stay below exact match.
    if (out.confidence > 0.95f) out.confidence = 0.95f;
    return true;
}

// Try token overlap: at least one meaningful token in common → 0.6.
bool tryToken(const Dict& dict, const std::vector<std::string>& paramTokens,
              Match& out)
{
    Match best{-1, 0.0f, {}, {}};
    for (const auto& [pat, entry] : dict) {
        auto patTokens = tokenise(pat);
        if (patTokens.empty()) continue;
        int overlap = 0;
        for (const auto& pt : patTokens) {
            if (pt.size() < 3) continue;  // skip short tokens
            for (const auto& qt : paramTokens) {
                if (qt == pt) { ++overlap; break; }
            }
        }
        if (overlap == 0) continue;
        float conf = 0.6f + static_cast<float>(overlap) * 0.05f;
        if (entry.sourceCount >= 3) conf += 0.05f;
        if (conf > best.confidence) {
            best = {entry.linkIdx, conf, entry.displayName, entry.category};
        }
    }
    if (best.linkIdx < 0) return false;
    out = best;
    if (out.confidence > 0.75f) out.confidence = 0.75f;
    return true;
}

// ---- UF8 category classification -------------------------------------------

std::string classifyParamName(const std::string& normName)
{
    // Simple keyword-based classification.
    if (normName.find("eq") != std::string::npos ||
        normName.find("frequency") != std::string::npos ||
        normName.find("freq") != std::string::npos ||
        normName.find("gain") != std::string::npos ||
        normName.find("bell") != std::string::npos ||
        normName.find("shelf") != std::string::npos ||
        normName.find("band") != std::string::npos ||
        normName.find(" q") != std::string::npos ||
        (normName.size() == 1 && normName[0] == 'q'))
        return "EQ";

    if (normName.find("gate") != std::string::npos ||
        normName.find("expander") != std::string::npos)
        return "Gate";

    if (normName.find("comp") != std::string::npos ||
        normName.find("threshold") != std::string::npos ||
        normName.find("thresh") != std::string::npos ||
        normName.find("ratio") != std::string::npos ||
        normName.find("attack") != std::string::npos ||
        normName.find("release") != std::string::npos ||
        normName.find("makeup") != std::string::npos ||
        normName.find("make-up") != std::string::npos ||
        normName.find("knee") != std::string::npos)
        return "Comp";

    if (normName.find("filter") != std::string::npos ||
        normName.find("low pass") != std::string::npos ||
        normName.find("high pass") != std::string::npos ||
        normName.find("lowpass") != std::string::npos ||
        normName.find("highpass") != std::string::npos ||
        normName.find("lpf") != std::string::npos ||
        normName.find("hpf") != std::string::npos ||
        normName.find("low cut") != std::string::npos ||
        normName.find("high cut") != std::string::npos)
        return "Filter";

    if (normName.find("input") != std::string::npos ||
        normName.find("output") != std::string::npos ||
        normName.find("trim") != std::string::npos ||
        normName.find("pan") != std::string::npos ||
        normName.find("width") != std::string::npos ||
        normName.find("phase") != std::string::npos ||
        normName.find("polarity") != std::string::npos)
        return "I/O";

    return "Misc";
}

} // anonymous namespace

// ---- Public API ------------------------------------------------------------

std::vector<Suggestion> suggestSlots(
    const std::vector<UserParamInfo>& params,
    Domain domain)
{
    Dict dict = buildBaseDict(domain);
    mergeUserMaps(dict, domain);

    std::vector<Suggestion> results;
    std::set<int> usedLinkIdx;   // prevent duplicate slot assignments

    // First pass: collect all matches with confidence.
    struct Candidate {
        int   vst3Param;
        int   linkIdx;
        float confidence;
        std::string paramName;
        std::string slotName;
    };
    std::vector<Candidate> candidates;

    for (const auto& pi : params) {
        if (pi.name.empty()) continue;
        std::string norm = normalise(pi.name);
        // (Previously hard-skipped "bypass" / "enable" / "on/off"
        // claiming they were "handled separately" — they weren't, the
        // Bypass SSL slot never got a candidate and stayed unmapped
        // even on plug-ins with a clearly-named "Bypass" param. Skip
        // removed 2026-05-24; the dictionary now matches them
        // properly. The strict-keyword filter below still prevents
        // "Bypass" param ↔ non-Bypass slot cross-matches.)

        // Skip channel-numbered params (CH<N>, Channel <N>, …). They
        // belong to UF8 strip / V-Pot positions — see suggestUf8Strips
        // and the channel-positioned branch of suggestUf8Banks. SSL
        // slots are single-instance and grabbing one of "CH7 Phase" /
        // "CH7 Enable" / "CH8 Pan" for the singular "Polarity" / "EQ
        // In" / "Pan" slots gives Frank's "wrong-channel" mismatch
        // (2026-05-24).
        if (extractChannelIndex(norm) >= 1) continue;

        Match m{};
        if (tryExact(dict, norm, m) ||
            trySubstring(dict, norm, m) ||
            tryToken(dict, tokenise(norm), m))
        {
            // Semantic-keyword guard. Some words are too distinctive
            // to ignore: when one side carries the keyword and the
            // other doesn't, the match is structurally wrong and we
            // reject it outright. Stops "Bypass ← Meter Scale @ 82%"
            // (Bypass is a control toggle, not a meter param) and
            // "Out Trim ← Output Pan @ 83%" (pan ≠ output trim).
            // (Frank 2026-05-24.)
            auto lc = [](std::string s) {
                for (auto& c : s)
                    c = static_cast<char>(std::tolower(
                        static_cast<unsigned char>(c)));
                return s;
            };
            const std::string paramLc = lc(pi.name);
            const std::string slotLc  = lc(m.slotName);
            static const char* kStrictKeywords[] = {
                "bypass", "pan"
            };
            bool semOk = true;
            for (const char* kw : kStrictKeywords) {
                const bool paramHas = paramLc.find(kw) != std::string::npos;
                const bool slotHas  = slotLc.find(kw)  != std::string::npos;
                if (paramHas != slotHas) { semOk = false; break; }
            }
            if (!semOk) continue;

            candidates.push_back({pi.vst3Param, m.linkIdx, m.confidence,
                                  pi.name, m.slotName});
        }
    }

    // Sort by confidence descending so highest-confidence match claims each slot.
    std::sort(candidates.begin(), candidates.end(),
        [](const Candidate& a, const Candidate& b) {
            return a.confidence > b.confidence;
        });

    for (const auto& c : candidates) {
        if (usedLinkIdx.count(c.linkIdx)) continue;
        usedLinkIdx.insert(c.linkIdx);
        results.push_back({c.linkIdx, c.vst3Param, c.paramName,
                           c.slotName, c.confidence, true});
    }

    // Sort results by linkIdx for consistent display order.
    std::sort(results.begin(), results.end(),
        [](const Suggestion& a, const Suggestion& b) {
            return a.linkIdx < b.linkIdx;
        });

    return results;
}

// Extract the lowercased "control-kind" suffix from a CH<N>-prefixed
// param name. "CH1 Pan" → "pan", "Channel 7 Send 2 Level" → "send 2
// level". Returns empty when no CH<N> prefix or no suffix after the
// digits. Used by suggestUf8Banks to group channels of the same
// control type into one V-Pot bank.
std::string extractChannelControlKind(const std::string& lower)
{
    const char* prefixes[] = { "channel", "chan", "ch" };
    for (const char* pfx : prefixes) {
        const size_t L = std::strlen(pfx);
        if (lower.size() <= L || lower.compare(0, L, pfx) != 0) continue;
        size_t i = L;
        while (i < lower.size() &&
               (lower[i] == ' ' || lower[i] == '-' ||
                lower[i] == '.' || lower[i] == '_')) ++i;
        if (i >= lower.size() ||
            !std::isdigit(static_cast<unsigned char>(lower[i]))) continue;
        while (i < lower.size() &&
               std::isdigit(static_cast<unsigned char>(lower[i]))) ++i;
        while (i < lower.size() &&
               (lower[i] == ' ' || lower[i] == '-' ||
                lower[i] == '.' || lower[i] == '_')) ++i;
        if (i >= lower.size()) return {};
        return lower.substr(i);
    }
    return {};
}

std::vector<Uf8Suggestion> suggestUf8Banks(
    const std::vector<UserParamInfo>& params,
    int faderBankCount)
{
    if (faderBankCount < 1) faderBankCount = 1;
    if (faderBankCount > 2) faderBankCount = 2;

    auto lowerOf = [](const std::string& s) {
        std::string out = s;
        for (auto& c : out)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    };

    // Pass A: split params into channel-positioned (CH<N> + non-strip
    // keyword) and category-classified (un-numbered). Strip-keyword
    // channelled params (CH<N> Volume / Mute / Solo / Sel) are handed
    // off to suggestUf8Strips and skipped here so we don't double-map.
    struct ChnParam {
        int         chN;
        std::string kind;       // suffix after "CH<N> "
        int         vst3Param;
        std::string name;
    };
    struct CatParam {
        int         vst3Param;
        std::string name;
        std::string category;
    };
    std::vector<ChnParam> channelled;
    std::vector<CatParam> unNumbered;

    for (const auto& pi : params) {
        if (pi.name.empty()) continue;
        const std::string norm = normalise(pi.name);
        if (norm == "bypass" || norm == "enable" || norm == "on/off")
            continue;
        if (pi.wasEnum) continue;

        const std::string lower = lowerOf(pi.name);
        if (isChannelStripCandidate(lower)) continue;     // strips claim it

        const int chN = extractChannelIndex(lower);
        if (chN >= 1) {
            channelled.push_back({chN, extractChannelControlKind(lower),
                                  pi.vst3Param, pi.name});
        } else {
            unNumbered.push_back({pi.vst3Param, pi.name,
                                  classifyParamName(norm)});
        }
    }

    // Pass B: group channelled params by control-kind in first-seen
    // order. Each unique kind claims one V-Pot bank; CH<N> goes to
    // strip (N-1)%8, fader-bank (N-1)/8.
    std::unordered_map<std::string, std::vector<ChnParam>> byKind;
    std::vector<std::string> kindOrder;
    for (const auto& c : channelled) {
        if (byKind.find(c.kind) == byKind.end())
            kindOrder.push_back(c.kind);
        byKind[c.kind].push_back(c);
    }

    std::vector<Uf8Suggestion> results;
    int  nextVpotBank = 0;

    for (const auto& kind : kindOrder) {
        if (nextVpotBank >= kUserUf8VpotBankCount) break;
        for (const auto& c : byKind[kind]) {
            const int fb = (c.chN - 1) / 8;
            const int st = (c.chN - 1) % 8;
            if (fb >= faderBankCount) continue;
            // Title-case the kind for the Category column display.
            std::string cat = kind;
            if (!cat.empty())
                cat[0] = static_cast<char>(std::toupper(
                    static_cast<unsigned char>(cat[0])));
            results.push_back({
                fb, nextVpotBank, st,
                c.vst3Param, c.name, cat,
                0.95f,      // channel-positioned = high confidence
                true
            });
        }
        ++nextVpotBank;
    }

    // Pass C: pack the remaining V-Pot banks with category-classified
    // un-numbered params. Uses the existing EQ / Comp / Gate / Filter /
    // I-O / Misc grouping. Confidence stays at 0.5 since the position
    // is heuristic — no semantic match between the strip and the param.
    const char* categoryOrder[] = {"EQ", "Comp", "Gate", "Filter", "I/O", "Misc"};
    int vpotBank = nextVpotBank;
    int strip    = 0;
    int faderBank = 0;
    const int maxStrips = 8 * kUserUf8VpotBankCount * faderBankCount;

    for (const char* cat : categoryOrder) {
        for (const auto& cp : unNumbered) {
            if (cp.category != cat) continue;
            if (vpotBank >= kUserUf8VpotBankCount) break;
            if (static_cast<int>(results.size()) >= maxStrips) break;

            results.push_back({
                faderBank, vpotBank, strip,
                cp.vst3Param, cp.name, cp.category,
                0.5f,
                true
            });

            strip++;
            if (strip >= 8) {
                strip = 0;
                vpotBank++;
                if (vpotBank >= kUserUf8VpotBankCount) {
                    vpotBank = 0;
                    faderBank++;
                    if (faderBank >= faderBankCount) break;
                }
            }
        }
        if (faderBank >= faderBankCount &&
            vpotBank >= kUserUf8VpotBankCount) break;
    }

    return results;
}

std::vector<Uf8StripSuggestion> suggestUf8Strips(
    const std::vector<UserParamInfo>& params,
    int faderBankCount)
{
    if (faderBankCount < 1) faderBankCount = 1;
    if (faderBankCount > 2) faderBankCount = 2;

    auto lowerOf = [](const std::string& s) {
        std::string out = s;
        for (auto& c : out)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    };

    // Extract a 1-based channel index from a "CH<N>" or "Channel <N>"
    // prefix. Returns 0 if no match. Tolerates a space, hyphen or
    // dot between prefix and digits ("CH 1", "Ch-1", "ch.1").
    auto chIndex = [](const std::string& lower) -> int {
        const char* prefixes[] = { "channel", "chan", "ch" };
        for (const char* pfx : prefixes) {
            const size_t L = std::strlen(pfx);
            if (lower.size() <= L) continue;
            if (lower.compare(0, L, pfx) != 0) continue;
            size_t i = L;
            while (i < lower.size() &&
                   (lower[i] == ' ' || lower[i] == '-' ||
                    lower[i] == '.' || lower[i] == '_')) ++i;
            if (i >= lower.size() || !std::isdigit(
                    static_cast<unsigned char>(lower[i]))) continue;
            int n = 0;
            while (i < lower.size() && std::isdigit(
                       static_cast<unsigned char>(lower[i]))) {
                n = n * 10 + (lower[i] - '0');
                ++i;
            }
            if (n >= 1) return n;
        }
        return 0;
    };

    std::vector<Uf8StripSuggestion> out;
    const int maxStrip = 8 * faderBankCount;   // 8 or 16

    for (const auto& pi : params) {
        if (pi.name.empty()) continue;
        const std::string lower = lowerOf(pi.name);
        const int n = chIndex(lower);
        if (n < 1 || n > maxStrip) continue;

        using Kind = Uf8StripSuggestion::Kind;
        Kind kind;
        // Order matters: "Mute"/"Solo"/"Sel" win over "Volume" because
        // some plug-ins name buttons "Channel 1 Mute Level" etc. Check
        // the button-style keywords first.
        if (lower.find("mute") != std::string::npos ||
            lower.find(" cut") != std::string::npos ||
            (lower.size() >= 3 && lower.compare(lower.size() - 3, 3, "cut") == 0))
            kind = Kind::Cut;
        else if (lower.find("solo") != std::string::npos)
            kind = Kind::Solo;
        else if (lower.find("select") != std::string::npos ||
                 lower.find(" sel")   != std::string::npos ||
                 (lower.size() >= 3 &&
                  lower.compare(lower.size() - 3, 3, "sel") == 0))
            kind = Kind::Sel;
        else if (lower.find("volume") != std::string::npos ||
                 lower.find("level")  != std::string::npos ||
                 lower.find("fader")  != std::string::npos ||
                 lower.find(" vol")   != std::string::npos ||
                 (lower.size() >= 3 &&
                  lower.compare(lower.size() - 3, 3, "vol") == 0) ||
                 lower.find("output") != std::string::npos)
            kind = Kind::Fader;
        else
            continue;

        const int fb = (n - 1) / 8;
        const int st = (n - 1) % 8;
        if (fb >= faderBankCount) continue;

        out.push_back({ kind, fb, st, pi.vst3Param, pi.name, 0.85f, true });
    }

    // Stable sort: by kind (Fader, Cut, Solo, Sel), then by faderBank,
    // then by strip. Keeps the preview table well grouped.
    std::sort(out.begin(), out.end(),
        [](const Uf8StripSuggestion& a, const Uf8StripSuggestion& b) {
            if (a.kind != b.kind) return static_cast<int>(a.kind) < static_cast<int>(b.kind);
            if (a.faderBank != b.faderBank) return a.faderBank < b.faderBank;
            return a.strip < b.strip;
        });

    return out;
}

} // namespace autolearn
} // namespace uf8
