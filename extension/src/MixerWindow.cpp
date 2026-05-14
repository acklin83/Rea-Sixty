#include "MixerWindow.h"
#include "MixerLayout.h"
#include "ThemeBridge.h"

#include <cstdio>

// REAIMGUIAPI_IMPLEMENT lives in SettingsWindow.cpp (only one TU per dylib
// may define it — multiple definitions would multiply-define the lazy-
// resolved ReaImGuiFunc storage at link time). This TU just consumes the
// already-materialised bindings.
#include "reaper_imgui_functions.h"

namespace uf8 {

struct MixerWindow::Impl {
    // ReaImGui v0.10 GCs contexts that don't get touched each defer cycle.
    // We never call DestroyContext (the binding is missing in v0.10 — calling
    // it crashes with PC=0). Toggle flips a visibility flag; when invisible
    // we still keep the context alive across one defer cycle, but Begin/End
    // is skipped so ReaImGui closes the OS window.
    ImGui_Context* ctx = nullptr;
    bool           visible = false;
    // Session counter — bumped on every closed→open transition. Suffixed
    // onto the Begin window-id so each session is a fresh ImGui window
    // object. Defeats stale collapsed/closed/off-screen state under the
    // old id (same trap that bit the Settings window).
    int            sessionGen = 0;

    void ensureCtx()
    {
        if (ctx) return;
        // 1280×720 host context is sacred — see memory/reaimgui-host-size-
        // bisect.md. Inner-window size set via SetNextWindowSize below is
        // free to change.
        int sizeW = 1280;
        int sizeH = 720;
        ctx = ImGui_CreateContext(
            "Rea-Sixty Mixer v1",
            &sizeW, &sizeH,
            /*pos_x*/ nullptr, /*pos_y*/ nullptr);
    }
};

MixerWindow::MixerWindow()  : impl_(new Impl) {}
MixerWindow::~MixerWindow() { delete impl_; }

void MixerWindow::toggle()
{
    const bool wasOpen = impl_->visible;
    impl_->visible = !wasOpen;
    if (impl_->visible) {
        ++impl_->sessionGen;
        // Drop the old context so ensureCtx() builds a fresh one on the
        // next tick. ReaImGui auto-GCs the orphan. Fresh ctx = zero state
        // carry-over from any prior session.
        impl_->ctx = nullptr;
    }
}

bool MixerWindow::isOpen() const { return impl_->visible; }

void MixerWindow::onRunTick()
{
    impl_->ensureCtx();
    if (!impl_->ctx) return;

    // 1600×1000 first-use size — wide enough to show ~12-14 strips before
    // horizontal scrolling kicks in; user can resize freely afterwards.
    // Native OS fullscreen (macOS green button, Windows maximise) works
    // on this window because ReaImGui floats it as a real OS window when
    // undocked.
    int condFirst = ImGui_Cond_FirstUseEver;
    ImGui_SetNextWindowSize(impl_->ctx, /*w*/ 1600, /*h*/ 1000,
                            &condFirst);
    ImGui_SetNextWindowPos(impl_->ctx, /*x*/ 80, /*y*/ 80,
                           &condFirst, /*pivot_x*/ nullptr,
                           /*pivot_y*/ nullptr);

    const int pushed = ThemeBridge::pushAll(impl_->ctx);

    // NoSavedSettings: don't persist collapsed/closed pose across toggles
    // (see SettingsWindow comments for the trap).
    // NoCollapse: title-bar-only state breaks rendering — no UX reason to
    // allow it (user toggles via REAPER action, not title-bar arrow).
    int winFlags = ImGui_WindowFlags_NoSavedSettings
                 | ImGui_WindowFlags_NoCollapse;
    char winId[64];
    std::snprintf(winId, sizeof(winId),
                  "Rea-Sixty Mixer##session_%d", impl_->sessionGen);
    bool open = impl_->visible;
    if (impl_->visible) {
        // Force-uncollapse every frame. With NoCollapse this is belt-and-
        // braces against any state weirdness that flips us into title-bar-
        // only mode.
        int condAlways = ImGui_Cond_Always;
        ImGui_SetNextWindowCollapsed(impl_->ctx, false, &condAlways);
        const bool bodyVisible =
            ImGui_Begin(impl_->ctx, winId, &open, &winFlags);
        if (bodyVisible) {
            MixerLayout::draw(impl_->ctx);
        }
        // End MUST be called for every Begin, regardless of return value
        // (Dear ImGui ≥1.89). Skipping it on a false-return frame imbalances
        // the stack permanently.
        ImGui_End(impl_->ctx);
        // Mirror ImGui's title-bar X click back to our flag so the next
        // toggle action correctly moves false→true.
        impl_->visible = open;
    }

    ThemeBridge::popAll(impl_->ctx, pushed);
}

} // namespace uf8
