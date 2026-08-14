#pragma once
//
// MixerLayout — pure layout/widget logic for the Plugin Mixer view tab.
//
// ⚠ UNREACHABLE. draw() has no call site anywhere in the tree: the Plug-in
// Mixer never got a rail entry in MixerWindow.cpp's kRail, so nothing can
// reach it. The body below is a one-line placeholder plus the plan for what
// would go in it. Phase 2.6 is real backlog and NOT near-term (Frank,
// 2026-08-14), so this is parked rather than in progress. It still compiles
// into the target; see COMPENDIUM.md §6.
//
// 75% left: horizontally scrollable channel-strip columns (one per track
//           hosting a CS-family plugin from PluginMap).
// 25% right: vertical Bus Compressor rack (one strip per BC instance).
//
// Decoupled from MixerWindow so unit tests can exercise layout math without
// an ImGui context. Widget callbacks write back via TrackFX_SetParamNormalized
// directly — same path UF8/UC1 already use, no extra plumbing.
//
// Phase 2.6 scaffold; bodies arrive in 2.6b/2.6c.
//

class ImGui_Context; // ReaImGui opaque context pointer

namespace uf8 {

class MixerLayout {
public:
    static void draw(ImGui_Context* ctx);
};

} // namespace uf8
