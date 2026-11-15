#pragma once
//
// ⇨ THE COMPUTER'S MEDIA KEYS ON A KEY IN ORC (Frank 15.11.2026, docs/orc-media-keys-plan.md):
// play/pause, next, previous and the system volume, as the keyboard sends them. The
// system hands them to whatever is playing (Spotify, Music, a browser), so ORC asks
// no player anything and shows nothing. Only ORC registers these: with REAPER
// running, Rea-Sixty has the surface and the transport keys are REAPER's again
// (bnd::orcKeySlot stays at rme_, see the plan).
//
// The registration is here and header-only, like OrcLight.h, so ORC, the manual's
// dump, the settings demo and the test all register the same six. What a press
// does is the platform's (OrcMediaMac.mm, OrcMediaWin.cpp); the tools pass none.

#include "Bindings.h"

#include <functional>
#include <string>

namespace orc::media {

enum class Key { PlayPause, Next, Previous, VolumeUp, VolumeDown, Mute };

// Press and release one media key. Mac: the first press without the right to
// post events asks macOS for it (Frank 15.11.: "auslösen"), once per run.
void press(Key k);

// The default output's volume moved by `delta` (0..1 scale, + = louder), and
// unmuted when it was muted. Nothing where the device has no volume of its own.
void stepSystemVolume(double delta);

// ⇨ THE JOG WHEEL ON THE SYSTEM VOLUME (Frank 15.11.): a target the UF1 page offers
// besides TotalMix' channels. rme.json keeps it in jog.target like any other;
// rmeu::resolveTarget knows nothing of it, so everything that only knows TotalMix
// (the time display, Rea-Sixty's side-car) sees no target and does nothing.
inline constexpr const char* kJogSystemSpec = "system";
inline bool jogIsSystem(const std::string& jogTarget) { return jogTarget == kJogSystemSpec; }
// One detent, as a share of the full scale; Shift's fine factor on top.
inline constexpr double kJogSystemStep = 0.01;

namespace detail {
inline void regMediaKey(const char* name, Key k, const char* title,
                        const std::function<void(Key)>& press)
{
    uf8::bindings::registerBuiltin(name, uf8::bindings::BuiltinDescriptor{
        [press, k](bool firing, bool, int) { if (firing && press) press(k); },
        nullptr, title, false });
}
} // namespace detail

// ⛔ One literal call per name: tools/check_builtin_docs.py reads them (regMediaKey).
inline void registerBuiltins(const std::function<void(Key)>& press)
{
    using detail::regMediaKey;
    regMediaKey("media_play_pause",  Key::PlayPause,  "Media play/pause",     press);
    regMediaKey("media_next",        Key::Next,       "Media next track",     press);
    regMediaKey("media_previous",    Key::Previous,   "Media previous track", press);
    regMediaKey("media_volume_up",   Key::VolumeUp,   "System volume up",     press);
    regMediaKey("media_volume_down", Key::VolumeDown, "System volume down",   press);
    regMediaKey("media_mute",        Key::Mute,       "System mute",          press);
}

} // namespace orc::media
