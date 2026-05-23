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
                std::string dispName;
                for (const auto& bm : allPluginMaps()) {
                    const auto* ls = findSlotByLinkIdx(bm, slot.linkIdx);
                    if (ls && ls->name) { dispName = ls->name; break; }
                }
                if (dispName.empty()) dispName = pi.name;

                // Determine category from built-in seeds or fall back.
                std::string cat2 = "Misc";
                auto it = dict.find(key);
                if (it != dict.end() && it->second.linkIdx == slot.linkIdx) {
                    cat2 = it->second.category;
                    it->second.sourceCount++;
                } else {
                    // Insert new entry from user map.
                    dict[key] = {slot.linkIdx, m.domain, cat2, dispName, 1};
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

// Try exact match (normalised) → confidence 1.0.
bool tryExact(const Dict& dict, const std::string& norm, Match& out)
{
    auto it = dict.find(norm);
    if (it == dict.end()) return false;
    out = {it->second.linkIdx, 1.0f, it->second.displayName, it->second.category};
    if (it->second.sourceCount >= 3) out.confidence += 0.05f;
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
        // Skip likely non-mappable params (bypass, enable, etc. handled separately).
        std::string norm = normalise(pi.name);
        if (norm == "bypass" || norm == "enable" || norm == "on/off") continue;

        Match m{};
        if (tryExact(dict, norm, m) ||
            trySubstring(dict, norm, m) ||
            tryToken(dict, tokenise(norm), m))
        {
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

std::vector<Uf8Suggestion> suggestUf8Banks(
    const std::vector<UserParamInfo>& params,
    int faderBankCount)
{
    if (faderBankCount < 1) faderBankCount = 1;
    if (faderBankCount > 2) faderBankCount = 2;

    // Classify all non-trivial params by category.
    struct CatParam {
        int         vst3Param;
        std::string name;
        std::string category;
    };
    std::vector<CatParam> classified;
    for (const auto& pi : params) {
        if (pi.name.empty()) continue;
        std::string norm = normalise(pi.name);
        if (norm == "bypass" || norm == "enable" || norm == "on/off") continue;
        // Skip likely enum/toggle params for V-Pot mapping.
        if (pi.wasEnum) continue;
        classified.push_back({pi.vst3Param, pi.name, classifyParamName(norm)});
    }

    // Group by category, preserving order within each group.
    const char* categoryOrder[] = {"EQ", "Comp", "Gate", "Filter", "I/O", "Misc"};
    std::vector<Uf8Suggestion> results;

    int vpotBank = 0;
    int strip    = 0;
    int faderBank = 0;
    const int maxStrips = 8 * kUserUf8VpotBankCount * faderBankCount;

    for (const char* cat : categoryOrder) {
        for (const auto& cp : classified) {
            if (cp.category != cat) continue;
            if (static_cast<int>(results.size()) >= maxStrips) break;

            results.push_back({
                faderBank, vpotBank, strip,
                cp.vst3Param, cp.name, cp.category,
                0.5f,  // UF8 suggestions are category-based, not name-matched
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
        if (faderBank >= faderBankCount && vpotBank >= kUserUf8VpotBankCount)
            break;
    }

    return results;
}

} // namespace autolearn
} // namespace uf8
