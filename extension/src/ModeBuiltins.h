#pragma once
//
// The actions of the Hue, DynaMount and OBS modes, registered with the bindings
// engine. Out of main.cpp (29.10.2026, docs/orc-uf8-plan.md stage 6) so that a
// key does the same in ORC and in Rea-Sixty: one registration, two callers. No
// REAPER: every action is a call into a manager.

#include <functional>

namespace reasixty::modes {

struct BuiltinHost {
    // Something a soft-key lamp shows changed (main.cpp: g_bankDirty).
    std::function<void()> changed;
    // Register hue_rec_light: the recording light follows REAPER's record
    // state, so only Rea-Sixty has it (Frank 29.10.2026: ORC needs none).
    bool recLight = false;
};

void registerBuiltins(const BuiltinHost& host);

} // namespace reasixty::modes
