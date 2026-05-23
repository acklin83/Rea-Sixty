#pragma once
//
// AutoLearnEngine — pattern-based auto-mapping for FX Learn.
//
// Builds a dictionary from hardcoded SSL param names + user-learned maps,
// then matches a new plugin's VST3 parameter list against it. Returns
// ranked suggestions with confidence scores for UC1 linkIdx slots and
// UF8 V-Pot bank groupings.
//

#include <string>
#include <vector>

#include "FocusedParam.h"        // Domain
#include "UserPluginCatalog.h"   // UserParamInfo, UserPluginCatalog

namespace uf8 {
namespace autolearn {

// ---- UC1 slot suggestion ---------------------------------------------------

struct Suggestion {
    int         linkIdx;        // target UC1 slot
    int         vst3Param;      // source VST3 param index
    std::string paramName;      // VST3 param name
    std::string slotName;       // canonical SSL slot name (e.g. "Comp Thr")
    float       confidence;     // 0.0 .. 1.0
    bool        accepted = true;
};

// ---- UF8 V-Pot bank suggestion ---------------------------------------------

struct Uf8Suggestion {
    int         faderBank;      // 0 or 1
    int         vpotBank;       // 0..7
    int         strip;          // 0..7 (position within the bank)
    int         vst3Param;
    std::string paramName;
    std::string category;       // "EQ", "Comp", "Gate", "Filter", "I/O", "Misc"
    float       confidence;
    bool        accepted = true;
};

// ---- Engine API ------------------------------------------------------------

// Build suggestions for UC1 linkIdx-based slots. `domain` determines which
// slot namespace applies (CS uses linkIdx 0..46+ext, BC uses linkIdx 0..7+ext).
// Merges hardcoded base dictionary with patterns learned from `userMaps`.
std::vector<Suggestion> suggestSlots(
    const std::vector<UserParamInfo>& params,
    Domain domain);

// Build suggestions for UF8 V-Pot banks. Groups matched params by category
// (EQ, Comp, Gate, Filter, I/O, Misc) into banks of 8. When faderBankCount
// is 2 and params > 8, overflow goes to faderBank 1.
std::vector<Uf8Suggestion> suggestUf8Banks(
    const std::vector<UserParamInfo>& params,
    int faderBankCount = 1);

} // namespace autolearn
} // namespace uf8
