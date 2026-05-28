#include "MixerWindow.h"
#include "SettingsScreen.h"
#include "ThemeBridge.h"

#include <cstdio>
#include <cstdlib>

#include "reaper_plugin_functions.h"

// One translation unit must define REAIMGUIAPI_IMPLEMENT before including the
// header — this materialises the storage for the lazy-resolved ReaImGuiFunc
// instances. Every other TU just `#include "reaper_imgui_functions.h"` and
// uses them as if they were free functions.
#define REAIMGUIAPI_IMPLEMENT
#include "reaper_imgui_functions.h"

// Settings → Appearance state, defined in main.cpp at file scope.
int reasixty_theme();
int reasixty_fontScale();

namespace uf8 {

namespace {

// Left-rail navigation. Settings only — Mixer entry was removed
// 2026-05-14 after the worktree-mixer-standalone experiment was
// reverted; the Plugin Mixer view will land later as its own
// dedicated window, not as a tab in here.
enum Section : int {
    kSecDevice = 0,
    kSecAppearance,
    kSecBindings,
    kSecModes,
    kSecFxLearn,
    kSecSelectionSets,
    kSecParameterGroups,
    kSecAbout,
    kSecCount,
};

struct RailEntry {
    const char* label;
    Section     section;
    bool        separatorBefore;
    void (*draw)(ImGui_Context*);
};

constexpr RailEntry kRail[] = {
    { "Device",         kSecDevice,        false, &SettingsScreen::drawDevice        },
    { "Appearance",     kSecAppearance,    false, &SettingsScreen::drawAppearance    },
    { "Bindings",       kSecBindings,      false, &SettingsScreen::drawBindings      },
    { "Modes",          kSecModes,         false, &SettingsScreen::drawModes         },
    { "FX Learn",       kSecFxLearn,       false, &SettingsScreen::drawFxLearn       },
    { "Selection Sets",  kSecSelectionSets,  false, &SettingsScreen::drawSelectionSets },
    { "Parameter Groups",kSecParameterGroups,false, &SettingsScreen::drawParameterGroups },
    { "About",           kSecAbout,          true,  &SettingsScreen::drawAbout         },
};

constexpr double kRailWidthPx = 160.0;

} // namespace

// Persisted window pose. Stored in REAPER's ExtState so the user
// sees the Settings window at the size + position they last left it
// across REAPER restarts. NoSavedSettings is intentionally kept on
// the ImGui window itself (it persists collapsed / closed pose too,
// which broke the window in the past — see comments inside Begin
// below); we mirror only pos + size by hand.
namespace pose {
constexpr const char* kSection = "rea_sixty";
constexpr const char* kKeyX    = "settings_pos_x";
constexpr const char* kKeyY    = "settings_pos_y";
constexpr const char* kKeyW    = "settings_size_w";
constexpr const char* kKeyH    = "settings_size_h";

bool loadDouble(const char* key, double& out) {
    const char* v = GetExtState(kSection, key);
    if (!v || !*v) return false;
    char* endp = nullptr;
    const double d = std::strtod(v, &endp);
    if (endp == v) return false;
    out = d;
    return true;
}
void saveDouble(const char* key, double v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%g", v);
    SetExtState(kSection, key, buf, /*persist*/ true);
}
} // namespace pose

struct MixerWindow::Impl {
    // v0.10+ owns context lifetime itself: ImGui_CreateContext returns a
    // context that auto-destroys when the calling extension unloads. There
    // is no ImGui_DestroyContext export in v0.10 — calling the vendored
    // binding for it crashes with PC=0 (plugin_getapi returns null for the
    // missing symbol). So we never destroy. Toggle instead flips a
    // visibility flag; when invisible, we skip Begin/End entirely and
    // ReaImGui closes the OS window. Re-toggling resumes drawing.
    ImGui_Context* ctx = nullptr;
    // Sans-serif font (sizeless in v0.10 — size is selected at PushFont
    // time via the third arg). Re-created per context because every
    // ensureCtx allocates a fresh ImGui_Context, and Attached resources
    // bind to a single context.
    ImGui_Font*    font = nullptr;
    bool           visible = false;
    int            selected = kSecDevice;
    // Session counter — bumped on every closed→open transition. Used to
    // suffix the Begin window-id so each session is a fresh ImGui
    // window object. Required because ReaImGui v0.10 retains stale
    // window state (collapsed/closed/off-screen pose) under the old
    // id and refuses to re-show after a single open/close cycle. New
    // id = no carried-over state.
    int            sessionGen = 0;
    // Persisted pose. Loaded lazily from ExtState on first ensureCtx,
    // then mirrored back whenever the user resizes or moves the
    // window. Mirroring NoSavedSettings (kept on) — ImGui's own
    // persistence saves collapsed/closed pose too, which previously
    // bricked the window.
    double         saveX = 60.0, saveY = 60.0;
    double         saveW = 1500.0, saveH = 1080.0;
    bool           poseLoaded = false;

    void loadPose()
    {
        if (poseLoaded) return;
        pose::loadDouble(pose::kKeyX, saveX);
        pose::loadDouble(pose::kKeyY, saveY);
        pose::loadDouble(pose::kKeyW, saveW);
        pose::loadDouble(pose::kKeyH, saveH);
        // Sanity clamp — a stored 0×0 or off-screen pose from a
        // previous bug would make the window unreachable.
        if (saveW < 200) saveW = 1500;
        if (saveH < 200) saveH = 1080;
        if (saveX < -8000 || saveX > 16000) saveX = 60;
        if (saveY < -8000 || saveY > 16000) saveY = 60;
        poseLoaded = true;
    }

    void ensureCtx()
    {
        if (ctx) return;
        // v0.10+ ImGui_CreateContext takes optional int* for all four
        // dimension args — passing raw ints (1280, 720) crashed because
        // the dylib's trampoline dereferenced them as pointers (= addr
        // 0x500). Must pass &int or nullptr. See learnings.md rule 17.
        //
        // Context name carries a version suffix so a fresh ReaImGui
        // state file is allocated. Stale persisted state under the bare
        // "Rea-Sixty" key prevented the window from reopening across
        // recent debugging sessions — bumping the suffix forces v0.10
        // to treat us as a brand-new context with no carried-over
        // collapsed / off-screen / closed pose.
        int sizeW = 1280;
        int sizeH = 720;
        ctx = ImGui_CreateContext(
            "Rea-Sixty v2",
            &sizeW, &sizeH,
            /*pos_x*/ nullptr, /*pos_y*/ nullptr);

        // Load a generic sans-serif font and attach to the freshly
        // created context. CreateFont in v0.10 returns a sizeless
        // resource — size is chosen at PushFont time. The previous
        // font (if any) belonged to the now-orphaned context and is
        // GC'd by ReaImGui on the next defer cycle.
        font = ImGui_CreateFont("sans-serif", /*flagsInOptional*/ nullptr);
        if (ctx && font) ImGui_Attach(ctx, font);
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
        // Drop the old context pointer so ensureCtx() creates a brand
        // new ImGui_Context on the next onRunTick. ReaImGui v0.10 GCs
        // contexts that go unused for a defer cycle (per its embedded
        // docs), so the orphaned previous context cleans up on its
        // own — we don't have DestroyContext available in v0.10. A
        // fresh ctx per open guarantees zero state carry-over from
        // any prior session: no remembered id-stack, no stale window
        // pose, no half-popped style stack.
        impl_->ctx = nullptr;
        // Font is bound to the dropped context — ensureCtx allocates
        // a new one paired with the new ctx.
        impl_->font = nullptr;
    }
}

void MixerWindow::openToFxLearn()
{
    // Select the FX Learn rail entry first, then open if needed. toggle()
    // only resets the ImGui ctx/font — it leaves impl_->selected intact —
    // so setting the section before opening survives the context refresh.
    impl_->selected = kSecFxLearn;
    if (!impl_->visible) toggle();
}

bool MixerWindow::isOpen() const { return impl_->visible; }

void MixerWindow::onRunTick()
{
    impl_->ensureCtx();
    if (!impl_->ctx) return;  // CreateContext failed (ReaImGui not installed?)

    // ReaImGui v0.10 GCs objects that don't get touched each defer
    // cycle (the dylib's embedded docs spell it out: "valid as long as
    // it is used in each defer cycle unless attached to a context").
    // We must call into the context every tick or it dies; we must
    // also call End() exactly once for each Begin() that returned true
    // (modern ImGui rule, RAPID uses the same pattern). When the user
    // wants the window hidden, we still tick the context but skip
    // Begin entirely — that's fine because we ALSO touch the context
    // through the SetNextWindowSize / SetNextWindowPos calls above
    // and through ThemeBridge::pushAll, all of which count as "use".
    // Initial size sized to fit the FX-Learn pane (860 px schematic +
    // 280 px param list + 12 px gap + chrome) without horizontal scroll
    // on first open. 2026-05-14: bumped width 1280→1500 so both the
    // UC1 (content fills to chassis edge) and UF8 (strips + bezel)
    // mockups display fully without the schematic-pane scrollbar.
    // CreateContext host stays at 1280×720 / "Rea-Sixty v2" — see
    // memory/reaimgui-host-size-bisect.md for why host size is sacred.
    impl_->loadPose();
    // Each session uses a fresh window-id (see sessionGen) so
    // FirstUseEver applies our saved pose every open instead of
    // ImGui's stale internal default.
    int condFirst = ImGui_Cond_FirstUseEver;
    ImGui_SetNextWindowSize(impl_->ctx, impl_->saveW, impl_->saveH,
                            &condFirst);
    ImGui_SetNextWindowPos(impl_->ctx, impl_->saveX, impl_->saveY,
                           &condFirst, /*pivot_x*/ nullptr,
                           /*pivot_y*/ nullptr);

    // Resolve active palette + font size from the Settings → Appearance
    // pickers (definitions live in main.cpp at file scope, hence the
    // ::-qualified call). Frank 2026-05-22.
    const Theme        theme   = static_cast<Theme>(::reasixty_theme());
    const ThemePalette& palette = paletteFor(theme);
    const int   scaleIdx = ::reasixty_fontScale();
    constexpr double kFontSizes[3] = { 12.0, 14.0, 18.0 };
    const double fontPx = kFontSizes[
        (scaleIdx < 0 || scaleIdx > 2) ? 1 : scaleIdx];
    if (impl_->font) {
        ImGui_PushFont(impl_->ctx, impl_->font, fontPx);
    }
    const int pushed = ThemeBridge::pushAll(impl_->ctx, palette);

    // NoSavedSettings tells ImGui not to persist closed/collapsed/
    // off-screen pose for this window across toggles — without it,
    // a single X-click leaves the window's "open=false" state stuck
    // in ImGui's internal storage on the same id, so the very next
    // *p_open=true couldn't override and the window silently failed
    // to reopen. Combined with the per-session id suffix this gives
    // a guaranteed-fresh window every open.
    // NoCollapse: prevent the window from going into the title-bar-only
    // state that breaks rendering. Repro: clicking the FX Learn rail entry
    // some way triggered a one-frame collapse, after which Begin returned
    // false forever and the Settings window appeared "dead" until REAPER
    // restart. There's no UX reason to allow collapsing — the user toggles
    // the whole window via a REAPER action, not via the title-bar arrow.
    int winFlags = ImGui_WindowFlags_NoSavedSettings
                 | ImGui_WindowFlags_NoCollapse;
    char winId[64];
    snprintf(winId, sizeof(winId),
                  "Rea-Sixty##session_%d", impl_->sessionGen);
    bool open = impl_->visible;
    if (impl_->visible) {
        // Dear ImGui >=1.89 rule: End() MUST always be called for every
        // Begin(), regardless of return value. Begin returns false when
        // the window is collapsed or fully clipped — body is skipped, but
        // End still required so the window stack stays balanced. Skipping
        // End on a false-return frame imbalances the stack, and every
        // subsequent Begin returns false → window bricked until REAPER
        // restart. Repro: clicking the FX Learn rail entry caused Begin
        // to return false on the next frame; without an unconditional
        // End() the Settings window died permanently.
        // Force-uncollapse every frame. Combined with NoCollapse this is
        // bulletproof against any state weirdness that flips the window
        // into a title-bar-only state on a frame transition.
        int condAlways = ImGui_Cond_Always;
        ImGui_SetNextWindowCollapsed(impl_->ctx, false, &condAlways);
        const bool bodyVisible =
            ImGui_Begin(impl_->ctx, winId, &open, &winFlags);
        if (bodyVisible) {
            // -- Left rail: section list ---------------------------------
            double railW = kRailWidthPx;
            const bool railVisible =
                ImGui_BeginChild(impl_->ctx, "rail", &railW,
                                 /*size_h*/ nullptr, /*border*/ nullptr,
                                 /*flags*/ nullptr);
            if (railVisible) {
                for (const RailEntry& e : kRail) {
                    if (e.separatorBefore) ImGui_Separator(impl_->ctx);
                    bool isSelected = (impl_->selected == e.section);
                    if (ImGui_Selectable(impl_->ctx, e.label, &isSelected,
                                         /*flags*/ nullptr,
                                         /*size_w*/ nullptr,
                                         /*size_h*/ nullptr)) {
                        impl_->selected = e.section;
                    }
                }
            }
            ImGui_EndChild(impl_->ctx);

            ImGui_SameLine(impl_->ctx, /*offset_from_start_x*/ nullptr,
                           /*spacing*/ nullptr);

            // -- Right content pane --------------------------------------
            const bool contentVisible =
                ImGui_BeginChild(impl_->ctx, "content", /*size_w*/ nullptr,
                                 /*size_h*/ nullptr, /*border*/ nullptr,
                                 /*flags*/ nullptr);
            if (contentVisible) {
                // 10 px left padding across all Settings tabs so labels +
                // separators don't crowd the rail divider. Frank 2026-05-22.
                double padX = 10.0;
                ImGui_Indent(impl_->ctx, &padX);
                for (const RailEntry& e : kRail) {
                    if (e.section == impl_->selected) {
                        e.draw(impl_->ctx);
                        break;
                    }
                }
                ImGui_Unindent(impl_->ctx, &padX);
            }
            ImGui_EndChild(impl_->ctx);

            // Mirror the user's current pose back to ExtState. Only
            // write when the value actually changed so we don't churn
            // the config file on every render frame.
            double curX = 0, curY = 0, curW = 0, curH = 0;
            ImGui_GetWindowPos (impl_->ctx, &curX, &curY);
            ImGui_GetWindowSize(impl_->ctx, &curW, &curH);
            if (curW > 50 && curH > 50) {
                if (curX != impl_->saveX) {
                    impl_->saveX = curX;
                    pose::saveDouble(pose::kKeyX, curX);
                }
                if (curY != impl_->saveY) {
                    impl_->saveY = curY;
                    pose::saveDouble(pose::kKeyY, curY);
                }
                if (curW != impl_->saveW) {
                    impl_->saveW = curW;
                    pose::saveDouble(pose::kKeyW, curW);
                }
                if (curH != impl_->saveH) {
                    impl_->saveH = curH;
                    pose::saveDouble(pose::kKeyH, curH);
                }
            }
        }
        ImGui_End(impl_->ctx);
        // Mirror ImGui's title-bar X click back to our flag so the
        // next 360 toggle correctly moves false→true.
        impl_->visible = open;
    }

    ThemeBridge::popAll(impl_->ctx, pushed);
    if (impl_->font) {
        ImGui_PopFont(impl_->ctx);
    }
}

} // namespace uf8
