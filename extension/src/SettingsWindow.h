#pragma once
//
// SettingsWindow — dedicated ReaImGui window hosting the left-rail config
// surface (Device / Bindings / FX Learn / Modes / Selection Sets / About).
//
// Split out of the previous unified MixerWindow on 2026-05-14: the Plugin
// Mixer now lives in its own window (MixerWindow.{cpp,h}) toggled by a
// separate REAPER action, so each surface can be sized / docked / full-
// screened independently.
//

namespace uf8 {

class SettingsWindow {
public:
    SettingsWindow();
    ~SettingsWindow();

    SettingsWindow(const SettingsWindow&)            = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    // Toggle visibility. Wired to the REAPER action
    // "Rea-Sixty: Toggle Settings Window".
    void toggle();

    bool isOpen() const;

    // Called from IReaperControlSurface::Run() each tick. No-op when closed.
    void onRunTick();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace uf8
