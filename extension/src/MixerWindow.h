#pragma once
//
// MixerWindow — the dockable on-screen window that hosts Rea-Sixty's UI.
//
// ⚠ The name is historical. It was created for the Phase 2.6 Plug-in Mixer
// view, which does NOT exist: its scaffold (MixerLayout) was never reachable
// and was removed 2026-08-14. What this window actually hosts is the Settings
// rail:
// a left-hand list of thirteen panes (Devices, Appearance, Behaviour, Bindings,
// Modes, FX Learn, Favourites, Selection Sets, Sticky Pot, Parameter Groups,
// Exchange, Manual, About) with the selected pane drawn in the content area. The rail
// table is kRail in MixerWindow.cpp; add panes there, not here.
//
// Rendering goes through ReaImGui, REAPER's own ImGui binding, reached via
// GetFunc through vendor/reaimgui/reaper_imgui_functions.h. There is no
// vendored Dear ImGui in this tree and no SWELL window of our own: the user
// installs reaper_imgui via ReaPack and we call into it. An earlier plan to
// vendor ImGui directly was abandoned; if you find a reference to
// extension/vendor/imgui/, it is stale.
//
// The render loop is driven from IReaperControlSurface::Run(), the same tick
// that pushes UF8/UC1/UF1 frames, so every REAPER-API read happens in one
// synchronous main-thread pass per frame.
//
// ⚠ An exception escaping an ImGui frame corrupts the ImGui stack, so
// main.cpp's onTimer rethrows rather than swallowing when the throw came from
// inside one (g_throwCameFromImGuiFrame).
//

namespace uf8 {

class MixerWindow {
public:
    MixerWindow();
    ~MixerWindow();

    MixerWindow(const MixerWindow&)            = delete;
    MixerWindow& operator=(const MixerWindow&) = delete;

    // Toggle visibility. Wired to the REAPER action
    // "Rea-Sixty: Toggle Plugin Mixer Window".
    void toggle();

    // Open the window (if closed) and switch to the FX Learn section.
    // Used by the Quick-Learn action to drop the user straight into the
    // FX-Learn tab with the +New dialog primed. Idempotent if already
    // open — just selects the section, no close.
    void openToFxLearn();

    bool isOpen() const;

    // Called from IReaperControlSurface::Run() each tick. No-op when closed.
    // Drives ImGui frame begin/render/end + theme-change probe.
    void onRunTick();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace uf8
