#pragma once
//
// MixerWindow — dedicated, full-screen-capable Plugin Mixer window.
//
// Hosts a ReaImGui context independently of the SettingsWindow so it can be
// docked / floated / native-fullscreened on its own. Renders the SSL 360°-
// style channel-strip view via uf8::MixerLayout::draw, which reads track
// state from the REAPER API each tick (GetTrackUIVolPan, Track_GetPeakInfo,
// TrackFX_GetNamedConfigParm "GainReduction_dB", GetTrackColor) and uses
// active-theme PNG assets loaded by uf8::theme_assets.
//
// Wired to the REAPER action "Rea-Sixty: Toggle Plugin Mixer Window".
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

    bool isOpen() const;

    // Called from IReaperControlSurface::Run() each tick. No-op when closed.
    void onRunTick();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace uf8
