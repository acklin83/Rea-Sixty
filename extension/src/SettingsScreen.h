#pragma once
//
// SettingsScreen — Rea-Sixty configuration UI. Each section renders
// inside the right-hand content pane of MixerWindow's left-rail nav;
// MixerWindow drives selection, this header just exposes the draws.
//
// Section sources:
//   docs/plan-settings-ui.md      — original tab structure
//   docs/bindings.md              — binding format / Learn-mode flow
//   docs/ssl-360-settings-inventory.md — gap analysis vs SSL 360°
//   memory uf8-softkey-banks.md   — CS 6 banks + BC 2 banks
//
// Persistence still goes through ExtState `rea_sixty` (per-project) +
// JSON config (global) per bindings.md §"Config File".
//

#include <string>
#include <vector>

class ImGui_Context;

namespace uf8 {

// ---- Shared with MixerWindow's rail search box ----------------------------
// The additive matcher every Settings search field already uses (implemented
// once in SettingsScreen.cpp as searchTokensLower_ / searchAllTokensCI_).
// Exposed rather than duplicated so the rail search behaves identically to
// the map / action / parameter pickers. Frank 2026-08-09.
std::vector<std::string> settingsSearchTokens(const char* query);
bool settingsSearchMatches(const std::vector<std::string>& tokens,
                           const std::string& hay);

// Best-effort scroll-to: the rail search names the section heading a clicked
// result lives under; the owning pane scrolls to that heading once, then the
// request clears itself.
void settingsRequestSectionScroll(const char* title);
// Called by MixerWindow once per pane draw, immediately before the pane's draw
// function. Takes ownership of a pending request for exactly that one draw —
// whatever no heading claims dies there. Must run for EVERY pane, not just the
// three that own headings, or a request the target pane never sees stays armed
// and fires on an unrelated visit later.
void settingsLatchSectionScroll();

class SettingsScreen {
public:
    static void drawDevices(ImGui_Context* ctx);
    static void drawAppearance(ImGui_Context* ctx);
    static void drawBehaviour(ImGui_Context* ctx);
    static void drawBindings(ImGui_Context* ctx);
    static void drawModes(ImGui_Context* ctx);
    static void drawFxLearn(ImGui_Context* ctx);
    static void drawFavourites(ImGui_Context* ctx);
    static void drawSelectionSets(ImGui_Context* ctx);
    static void drawParameterGroups(ImGui_Context* ctx);
    static void drawAbout(ImGui_Context* ctx);

    // Hardware mockups — same vector schematics used by the Bindings
    // editor as a button picker, but in passive mode (no click-to-bind,
    // no selection highlight). MixerLayout uses them as the live
    // visual mirror of UF8 + UC1 face state.
    static void drawUf8Mockup(ImGui_Context* ctx);
    static void drawUc1Mockup(ImGui_Context* ctx);
};

} // namespace uf8
