//
// QuickLearnWindow — standalone popup for FX parameter mapping.
//
// Follows the MixerWindow pattern: lazy ImGui context, session counter,
// visibility flag, main-thread render via onRunTick().
//

#include "QuickLearnWindow.h"
#include "AutoLearnEngine.h"
#include "ThemeBridge.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "reaper_plugin_functions.h"
#include "reaper_imgui_functions.h"   // no IMPLEMENT — MixerWindow owns it

#include "FocusedParam.h"
#include "PluginMap.h"
#include "UserPluginCatalog.h"

// Defined in macos_pin_fx_gui.mm. Raises the OS-level window owning the
// passed HWND-equivalent to the front and steals focus. Platform helper
// — Windows / Linux fallback below uses SWELL via main.cpp.
#ifdef __APPLE__
namespace uf8 {
    void macosBringWindowToFront(void* hwnd, const char* titleHint);
}
#endif

// Settings → Appearance state, defined in main.cpp at file scope.
int reasixty_theme();
int reasixty_fontScale();

namespace uf8 {
namespace {

// ---- QuickLearn state machine ----------------------------------------------

enum class QLPhase : int {
    Setup,       // choose domain + fader banks
    Mapping,     // sequential wiggle-detect
    Review,      // confirm + save
};

// What we're mapping right now — UC1 slots or UF8 V-Pot banks.
enum class QLTarget : int {
    Uc1,
    Uf8,
};

// One slot in the sequential mapping queue.
struct QLSlot {
    std::string label;       // display: "Comp Thr", "FB1 B2 S3", etc.
    int         linkIdx = -1;   // UC1 linkIdx (QLTarget::Uc1) or -1
    int         faderBank = 0;  // UF8 coords (QLTarget::Uf8)
    int         vpotBank  = 0;
    int         strip     = 0;
    QLTarget    target;
    int         boundParam = -1;  // VST3 param bound by wiggle, -1 = unbound
    std::string boundName;        // VST3 param name (for review)
};

} // anonymous namespace

// ---- Impl ------------------------------------------------------------------

struct QuickLearnWindow::Impl {
    ImGui_Context* ctx     = nullptr;
    ImGui_Font*    font    = nullptr;
    bool           visible = false;
    int            sessionGen = 0;
    // Counter — non-zero for the first N frames after toggle()-to-open.
    // Each of those frames calls ImGui_SetNextWindowFocus AND the
    // macOS bring-to-front helper so the host OS window definitely
    // surfaces above any floating plug-in GUI. One frame proved
    // unreliable (Frank 2026-05-24): the ReaImGui host window can
    // still be in the middle of materialising on the first frame
    // after CreateContext, so we run the raise for ~3 frames.
    int            focusPendingFrames = 0;
    // Track the last-persisted host shape so we only call SetExtState
    // when something actually changed (avoid churn during render).
    int            lastSavedW = -1;
    int            lastSavedH = -1;
    int            lastSavedX = INT32_MIN;
    int            lastSavedY = INT32_MIN;

    // ---- State machine ----
    QLPhase phase = QLPhase::Setup;

    // Setup inputs
    int  domainChoice  = 0;   // 0=CS, 1=BC, 2=CS+UF8, 3=BC+UF8, 4=UF8-only
    int  faderBankCount = 1;  // 1 or 2
    bool autoLearnFirst = false;

    // Resolved FX
    MediaTrack* fxTrack = nullptr;
    int         fxIdx   = -1;
    std::string fxName;
    std::string fxMatch;   // match substring for UserPluginMap

    // Mapping queue
    std::vector<QLSlot> queue;
    int  currentSlot = 0;

    // Wiggle detection baseline
    int  baseTr    = -2;
    int  baseFx    = -1;
    int  baseParam = -1;

    // DisplayShort for the new map. 32-byte buffer matches the working
    // pattern in SettingsScreen.cpp's FX-Learn label input — earlier 8-byte
    // size paired with SetNextItemWidth produced an invisible InputText in
    // ReaImGui v0.10.
    char displayShort[32] = {};

    void ensureCtx()
    {
        if (ctx) return;
        // Size + position are persisted to ExtState per-frame after
        // ImGui_Begin (see onRunTick). We restore those values here as
        // the CreateContext hints so the new host-window opens at the
        // user's last shape. Defaults 520×720 (centered) on first run.
        // (Previous code relied on ReaImGui's per-context persistence,
        // but ImGui_WindowFlags_NoSavedSettings + a session-bumped
        // winId both blocked it — Frank 2026-05-24.)
        int sizeW = 520, sizeH = 720;
        const char* sw = GetExtState("ReaSixty", "quickLearnSizeW");
        const char* sh = GetExtState("ReaSixty", "quickLearnSizeH");
        if (sw && *sw) { int v = std::atoi(sw); if (v >= 320 && v <= 4096) sizeW = v; }
        if (sh && *sh) { int v = std::atoi(sh); if (v >= 320 && v <= 4096) sizeH = v; }
        int posX = -1, posY = -1;
        const char* px = GetExtState("ReaSixty", "quickLearnPosX");
        const char* py = GetExtState("ReaSixty", "quickLearnPosY");
        if (px && *px) posX = std::atoi(px);
        if (py && *py) posY = std::atoi(py);
        int* posXp = (posX >= 0) ? &posX : nullptr;
        int* posYp = (posY >= 0) ? &posY : nullptr;
        ctx = ImGui_CreateContext("Rea-Sixty QuickLearn",
                                 &sizeW, &sizeH, posXp, posYp);
        font = ImGui_CreateFont("sans-serif", nullptr);
        if (ctx && font) ImGui_Attach(ctx, font);
    }

    // Strip plug-in format prefix (VST3: / AU: / JS: / CLAP: etc.) from a
    // raw FX name. Used both as the UserPluginMap.match key and as the
    // default displayShort when the user doesn't supply one.
    static std::string stripFxPrefix_(const std::string& raw)
    {
        for (const char* pfx : {"VST3: ", "VST: ", "VSTi: ", "VST3i: ",
                                 "JS: ", "AU: ", "AUi: ",
                                 "CLAP: ", "CLAPi: "})
        {
            const size_t L = std::strlen(pfx);
            if (raw.compare(0, L, pfx) == 0) return raw.substr(L);
        }
        return raw;
    }

    // Returns the highest faderBank index with any non-empty UF8 slot in
    // the given map, plus 1 — so we can re-create the right number of
    // fader-bank rows when seeding the queue from an existing learn.
    static int countUsedFaderBanks_(const UserPluginMap& m)
    {
        int used = 0;
        for (int fb = 0; fb < kUserUf8FaderBankCount; ++fb)
            for (int vb = 0; vb < kUserUf8VpotBankCount; ++vb)
                for (int st = 0; st < 8; ++st)
                    if (m.uf8.banks.banks[fb][vb][st].vst3Param >= 0)
                        used = std::max(used, fb + 1);
        return used > 0 ? used : 1;
    }

    // Seed Setup-phase inputs (domainChoice / faderBankCount /
    // displayShort) from an existing UserPluginMap for the resolved FX,
    // if any. Called from toggle() so re-opening QuickLearn on a known
    // plug-in lands the user on familiar settings.
    void seedFromExistingMap_()
    {
        if (fxMatch.empty()) return;
        const auto* m = user_plugins::lookupOwnedByName(fxMatch);
        if (!m) return;
        // Domain + uf8 mode → domainChoice
        if (m->domain == Domain::ChannelStrip)
            domainChoice = m->uf8Mode ? 2 : 0;
        else if (m->domain == Domain::BusComp)
            domainChoice = m->uf8Mode ? 3 : 1;
        else
            domainChoice = 4; // UF8-only
        faderBankCount = countUsedFaderBanks_(*m);
        // displayShort: empty user buffer + map has one → seed.
        if (!m->displayShort.empty()) {
            std::strncpy(displayShort, m->displayShort.c_str(),
                         sizeof(displayShort) - 1);
            displayShort[sizeof(displayShort) - 1] = '\0';
        }
    }

    void resolveActiveFx()
    {
        fxTrack = GetSelectedTrack(nullptr, 0);
        fxIdx = -1;
        fxName.clear();
        fxMatch.clear();
        if (!fxTrack) return;

        // Anchor on the selected track. GetLastTouchedFX is global —
        // using it as the primary source meant a knob touched on a
        // different track earlier still won over the currently-selected
        // track's plug-in, even after a fresh track-select. Frank
        // 2026-05-23. Priority within the selected track:
        //   1) REAPER's focused FX (matches what's shown in the FX
        //      chain header) — if it's on the selected track.
        //   2) Last-touched FX — if on the selected track.
        //   3) FX[0] — first plug-in on the selected track.
        auto adopt = [&](MediaTrack* tr, int fi) -> bool {
            if (!tr || fi < 0) return false;
            char buf[512] = {};
            TrackFX_GetFXName(tr, fi, buf, sizeof(buf));
            if (!buf[0]) return false;
            fxTrack = tr;
            fxIdx   = fi;
            fxName  = buf;
            fxMatch = stripFxPrefix_(fxName);
            return true;
        };

        // 1) Focused FX (FX chain or floating window with input focus).
        {
            int trNum = -1, itemNum = -1, fxNum = -1;
            const int ret = GetFocusedFX2(&trNum, &itemNum, &fxNum);
            if ((ret & 1) && trNum > 0) {
                MediaTrack* cand = GetTrack(nullptr, trNum - 1);
                const int candFx = fxNum & 0x00FFFFFF;
                if (cand == fxTrack && candFx >= 0
                    && candFx < TrackFX_GetCount(fxTrack))
                {
                    if (adopt(fxTrack, candFx)) return;
                }
            }
        }

        // 2) Last-touched FX — only if it's on the selected track.
        {
            int trWord = -1, fxWord = -1, paramWord = -1;
            if (GetLastTouchedFX(&trWord, &fxWord, &paramWord)) {
                const int trLow = trWord & 0xFFFF;
                if (trLow > 0 && !((trWord & 0xFFFF0000) != 0)) {
                    MediaTrack* tr = GetTrack(nullptr, trLow - 1);
                    const int fi = fxWord & 0xFFFFFF;
                    if (tr == fxTrack && !((fxWord >> 24) & 0x01)) {
                        if (adopt(fxTrack, fi)) return;
                    }
                }
            }
        }

        // 3) FX[0] of the selected track.
        if (TrackFX_GetCount(fxTrack) > 0) {
            adopt(fxTrack, 0);
        }
    }

    Domain domainFromChoice() const
    {
        switch (domainChoice) {
            case 0: case 2: return Domain::ChannelStrip;
            case 1: case 3: return Domain::BusComp;
            default:        return Domain::None;
        }
    }

    bool uf8FromChoice() const
    {
        return domainChoice >= 2;
    }

    void buildQueue()
    {
        queue.clear();
        currentSlot = 0;

        const Domain dom = domainFromChoice();
        const bool wantUf8 = uf8FromChoice();

        // UC1 slots (when CS or BC selected).
        if (dom != Domain::None) {
            const PluginMap* topo = nullptr;
            for (const auto& m : allPluginMaps()) {
                if (m.domain == dom) { topo = &m; break; }
            }
            if (topo) {
                for (const auto& s : topo->slots) {
                    QLSlot qs;
                    qs.label   = s.name ? s.name : "(slot)";
                    qs.linkIdx = s.linkIdx;
                    qs.target  = QLTarget::Uc1;
                    queue.push_back(qs);
                }
            }
        }

        // UF8 V-Pot banks.
        if (wantUf8 || dom == Domain::None) {
            for (int fb = 0; fb < faderBankCount; ++fb) {
                for (int vb = 0; vb < kUserUf8VpotBankCount; ++vb) {
                    for (int st = 0; st < 8; ++st) {
                        QLSlot qs;
                        char lbl[32];
                        snprintf(lbl, sizeof(lbl), "FB%d B%d S%d",
                                 fb + 1, vb + 1, st + 1);
                        qs.label     = lbl;
                        qs.faderBank = fb;
                        qs.vpotBank  = vb;
                        qs.strip     = st;
                        qs.target    = QLTarget::Uf8;
                        queue.push_back(qs);
                    }
                }
            }
        }

        // Pre-fill from existing UserPluginMap so re-learn shows the
        // current bindings, editable in Mapping phase.
        if (!fxMatch.empty()) {
            if (const auto* m = user_plugins::lookupOwnedByName(fxMatch)) {
                for (auto& qs : queue) {
                    if (qs.target == QLTarget::Uc1) {
                        for (const auto& s : m->slots) {
                            if (s.linkIdx == qs.linkIdx
                             && s.vst3Param >= 0)
                            {
                                qs.boundParam = s.vst3Param;
                                qs.boundName  = paramNameForBound_(s.vst3Param);
                                break;
                            }
                        }
                    } else {
                        const auto& bs = m->uf8.banks.banks
                            [std::clamp(qs.faderBank, 0,
                                        kUserUf8FaderBankCount - 1)]
                            [std::clamp(qs.vpotBank,  0,
                                        kUserUf8VpotBankCount - 1)]
                            [std::clamp(qs.strip,     0, 7)];
                        if (bs.vst3Param >= 0) {
                            qs.boundParam = bs.vst3Param;
                            qs.boundName  = !bs.label.empty()
                                ? bs.label
                                : paramNameForBound_(bs.vst3Param);
                        }
                    }
                }
            }
        }
    }

    // Resolve a VST3 param index to its current display name on the live
    // FX. Used when re-hydrating bindings from an existing UserPluginMap
    // — UF8BankSlot stores a saved label but UC1 slots don't, so we read
    // back from the plug-in to keep the Mapping list readable.
    std::string paramNameForBound_(int p)
    {
        if (!fxTrack || fxIdx < 0 || p < 0) return {};
        char buf[256] = {};
        TrackFX_GetParamName(fxTrack, fxIdx, p, buf, sizeof(buf));
        return buf;
    }

    void snapshotBaseline()
    {
        int t = -1, f = -1, p = -1;
        if (GetLastTouchedFX(&t, &f, &p)) {
            baseTr = t; baseFx = f; baseParam = p;
        } else {
            baseTr = -1; baseFx = -1; baseParam = -1;
        }
    }

    // Returns true if a new param was detected.
    bool pollWiggle(int& outParam, std::string& outName)
    {
        int t = -1, f = -1, p = -1;
        if (!GetLastTouchedFX(&t, &f, &p)) return false;
        if (t == baseTr && f == baseFx && p == baseParam) return false;

        // Update baseline.
        baseTr = t; baseFx = f; baseParam = p;

        // Resolve param name.
        if (fxTrack && fxIdx >= 0) {
            char pn[128] = {};
            TrackFX_GetParamName(fxTrack, fxIdx, p, pn, sizeof(pn));
            outName = pn;
        }
        outParam = p;
        return true;
    }

    void applyAutoLearn()
    {
        if (!fxTrack || fxIdx < 0) return;
        // Build param snapshot from live FX.
        const int n = TrackFX_GetNumParams(fxTrack, fxIdx);
        std::vector<UserParamInfo> params;
        params.reserve(static_cast<size_t>(n));
        for (int p = 0; p < n; ++p) {
            UserParamInfo pi;
            pi.vst3Param = p;
            char name[256] = {};
            TrackFX_GetParamName(fxTrack, fxIdx, p, name, sizeof(name));
            pi.name = name;
            double mn = 0, mx = 1, def = 0;
            TrackFX_GetParamEx(fxTrack, fxIdx, p, &mn, &mx, &def);
            const double range = mx - mn;
            pi.defaultNorm = (range > 1e-9) ? (def - mn) / range : 0.5;
            double step = 0, small = 0, large = 0;
            bool isToggle = false;
            TrackFX_GetParameterStepSizes(fxTrack, fxIdx, p,
                &step, &small, &large, &isToggle);
            pi.wasEnum = isToggle || step >= 0.5;
            params.push_back(std::move(pi));
        }

        // Run AutoLearn engine.
        const Domain dom = domainFromChoice();
        if (dom != Domain::None) {
            auto suggestions = autolearn::suggestSlots(params, dom);
            for (const auto& s : suggestions) {
                for (auto& qs : queue) {
                    if (qs.target == QLTarget::Uc1 &&
                        qs.linkIdx == s.linkIdx &&
                        qs.boundParam < 0)
                    {
                        qs.boundParam = s.vst3Param;
                        qs.boundName  = s.paramName;
                        break;
                    }
                }
            }
        }

        if (uf8FromChoice() || dom == Domain::None) {
            auto uf8Sugg = autolearn::suggestUf8Banks(params, faderBankCount);
            for (const auto& u : uf8Sugg) {
                for (auto& qs : queue) {
                    if (qs.target == QLTarget::Uf8 &&
                        qs.faderBank == u.faderBank &&
                        qs.vpotBank  == u.vpotBank  &&
                        qs.strip     == u.strip     &&
                        qs.boundParam < 0)
                    {
                        qs.boundParam = u.vst3Param;
                        qs.boundName  = u.paramName;
                        break;
                    }
                }
            }
        }

        // Advance currentSlot to first unbound.
        for (int i = 0; i < static_cast<int>(queue.size()); ++i) {
            if (queue[static_cast<size_t>(i)].boundParam < 0) {
                currentSlot = i;
                return;
            }
        }
        // All bound — go to review.
        phase = QLPhase::Review;
    }

    void saveMap()
    {
        if (fxMatch.empty()) return;

        UserPluginMap map;
        // Check if a map already exists for this match.
        if (const auto* existing = user_plugins::lookupOwnedByName(fxMatch))
            map = *existing;
        else
            map.match = fxMatch;

        map.domain  = domainFromChoice();
        map.uf8Mode = uf8FromChoice() || map.domain == Domain::None;
        if (map.domain == Domain::None) map.uf8Mode = true;
        if (displayShort[0])
            map.displayShort = displayShort;
        else if (map.displayShort.empty())
            map.displayShort = !fxMatch.empty() ? fxMatch : "USR";

        // Apply UC1 slot bindings.
        for (const auto& qs : queue) {
            if (qs.boundParam < 0) continue;
            if (qs.target == QLTarget::Uc1) {
                // Remove existing binding for this linkIdx.
                map.slots.erase(
                    std::remove_if(map.slots.begin(), map.slots.end(),
                        [&](const UserLinkSlot& s) {
                            return s.linkIdx == qs.linkIdx;
                        }),
                    map.slots.end());
                map.slots.push_back({qs.linkIdx, qs.boundParam, false, {}});
            }
        }

        // Apply UF8 V-Pot bank bindings.
        for (const auto& qs : queue) {
            if (qs.boundParam < 0) continue;
            if (qs.target == QLTarget::Uf8) {
                const int fb = std::clamp(qs.faderBank, 0,
                    kUserUf8FaderBankCount - 1);
                const int vb = std::clamp(qs.vpotBank, 0,
                    kUserUf8VpotBankCount - 1);
                const int st = std::clamp(qs.strip, 0, 7);
                auto& bs = map.uf8.banks.banks[fb][vb][st];
                bs.vst3Param = qs.boundParam;
                if (bs.label.empty()) bs.label = qs.boundName;
            }
        }

        // Take a param snapshot from the live FX.
        if (fxTrack && fxIdx >= 0) {
            const int n = TrackFX_GetNumParams(fxTrack, fxIdx);
            map.paramSnapshot.clear();
            map.paramSnapshot.reserve(static_cast<size_t>(n));
            for (int p = 0; p < n; ++p) {
                UserParamInfo pi;
                pi.vst3Param = p;
                char name[256] = {};
                TrackFX_GetParamName(fxTrack, fxIdx, p, name, sizeof(name));
                pi.name = name;
                double mn = 0, mx = 1, def = 0;
                TrackFX_GetParamEx(fxTrack, fxIdx, p, &mn, &mx, &def);
                const double range = mx - mn;
                pi.defaultNorm = (range > 1e-9) ? (def - mn) / range : 0.5;
                double step = 0, small = 0, large = 0;
                bool isToggle = false;
                TrackFX_GetParameterStepSizes(fxTrack, fxIdx, p,
                    &step, &small, &large, &isToggle);
                pi.wasEnum = isToggle || step >= 0.5;
                map.paramSnapshot.push_back(std::move(pi));
            }
            map.snapshotTakenAt = static_cast<int64_t>(std::time(nullptr));
        }

        user_plugins::upsert(std::move(map));
        user_plugins::save();
    }
};

// ---- Public interface ------------------------------------------------------

QuickLearnWindow::QuickLearnWindow()  : impl_(new Impl) {}
QuickLearnWindow::~QuickLearnWindow() { delete impl_; }

void QuickLearnWindow::toggle()
{
    const bool wasOpen = impl_->visible;
    impl_->visible = !wasOpen;
    if (impl_->visible) {
        ++impl_->sessionGen;
        impl_->focusPendingFrames = 3;   // raise above plug-in GUIs
        // Keep impl_->ctx / impl_->font alive across toggles. Resetting
        // them to nullptr on every open used to force ensureCtx to call
        // ImGui_CreateContext again — and ReaImGui's per-name context
        // cache returned the SAME (closed-state) context from the
        // previous session, so Begin's *p_open=true override was never
        // honoured and the window stayed hidden on the second action
        // trigger (Frank 2026-05-24). ensureCtx() is a no-op when
        // ctx is already valid, so this just skips the recreation.
        // Reset state machine.
        impl_->phase = QLPhase::Setup;
        impl_->domainChoice   = 0;
        impl_->faderBankCount = 1;
        impl_->autoLearnFirst = false;
        impl_->queue.clear();
        impl_->currentSlot = 0;
        impl_->displayShort[0] = '\0';
        // Resolve the active FX, then seed Setup-phase inputs from any
        // existing UserPluginMap so re-opening on a known plug-in lands
        // the user on familiar settings (domain/UF8/displayShort).
        impl_->resolveActiveFx();
        impl_->seedFromExistingMap_();
        // If this plug-in is already learned, skip the Setup step and
        // drop the user straight into the editable Mapping list. Setup
        // is only needed for first-time learn (domain pick + UF8 toggle).
        if (!impl_->fxMatch.empty()
            && user_plugins::lookupOwnedByName(impl_->fxMatch))
        {
            impl_->buildQueue();
            impl_->phase = QLPhase::Mapping;
            int firstUnbound = -1;
            for (size_t i = 0; i < impl_->queue.size(); ++i) {
                if (impl_->queue[i].boundParam < 0) {
                    firstUnbound = static_cast<int>(i);
                    break;
                }
            }
            impl_->currentSlot = firstUnbound >= 0 ? firstUnbound : 0;
            impl_->snapshotBaseline();
        }
    }
}

bool QuickLearnWindow::isOpen() const { return impl_->visible; }

void QuickLearnWindow::onRunTick()
{
    impl_->ensureCtx();
    if (!impl_->ctx) return;

    // Theme + font (matches Settings window).
    const Theme         theme   = static_cast<Theme>(::reasixty_theme());
    const ThemePalette& palette = paletteFor(theme);
    const int   scaleIdx = ::reasixty_fontScale();
    constexpr double kFontSizes[3] = { 12.0, 14.0, 18.0 };
    const double fontPx = kFontSizes[
        (scaleIdx < 0 || scaleIdx > 2) ? 1 : scaleIdx];
    if (impl_->font) ImGui_PushFont(impl_->ctx, impl_->font, fontPx);
    const int pushed = ThemeBridge::pushAll(impl_->ctx, palette);

    // Stable winId + no NoSavedSettings so ImGui itself can persist
    // the inner-window state across the lifetime of the context. (The
    // host-window size + pos are also explicitly persisted to ExtState
    // below — that survives even context destruction / app restart.)
    int winFlags = ImGui_WindowFlags_NoCollapse;
    const char* winId = "QuickLearn";
    bool open = impl_->visible;

    if (impl_->visible) {
        int condAlways = ImGui_Cond_Always;
        ImGui_SetNextWindowCollapsed(impl_->ctx, false, &condAlways);
        if (impl_->focusPendingFrames > 0) {
            // Raise the QuickLearn host window above any focused
            // plug-in GUI. ImGui_SetNextWindowFocus only reorders
            // ImGui's internal stack — to bring the OS-level host
            // window over a REAPER plug-in's floating window we also
            // need a platform call. Repeated for a few frames because
            // ReaImGui may still be materialising the host on the
            // first tick after CreateContext.
            ImGui_SetNextWindowFocus(impl_->ctx);
#ifdef __APPLE__
            void* hwnd = ImGui_GetNativeHwnd(impl_->ctx);
            uf8::macosBringWindowToFront(hwnd, "QuickLearn");
#endif
            --impl_->focusPendingFrames;
        }
        const bool bodyVisible =
            ImGui_Begin(impl_->ctx, winId, &open, &winFlags);
        if (bodyVisible) {
            // Capture the current host-context shape and persist when
            // it diverges meaningfully from the last-saved value. Polled
            // every frame; only writes ExtState on real changes so we
            // don't churn the registry mid-drag. ImGui_GetWindowSize /
            // _Pos report the inner ImGui window; for a non-docked
            // ReaImGui host that's the host content area (drifts ~20-
            // 30 px from the host frame size, but converges quickly).
            double curW = 0, curH = 0;
            ImGui_GetWindowSize(impl_->ctx, &curW, &curH);
            double curX = 0, curY = 0;
            ImGui_GetWindowPos(impl_->ctx, &curX, &curY);
            auto persist = [](const char* key, int v) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%d", v);
                SetExtState("ReaSixty", key, buf, true);
            };
            const int iw = static_cast<int>(curW);
            const int ih = static_cast<int>(curH);
            const int ix = static_cast<int>(curX);
            const int iy = static_cast<int>(curY);
            if (iw != impl_->lastSavedW) { persist("quickLearnSizeW", iw); impl_->lastSavedW = iw; }
            if (ih != impl_->lastSavedH) { persist("quickLearnSizeH", ih); impl_->lastSavedH = ih; }
            if (ix != impl_->lastSavedX) { persist("quickLearnPosX",  ix); impl_->lastSavedX = ix; }
            if (iy != impl_->lastSavedY) { persist("quickLearnPosY",  iy); impl_->lastSavedY = iy; }
            // ---- Render content based on phase ----
            switch (impl_->phase) {

            // ================================================================
            // SETUP PHASE
            // ================================================================
            case QLPhase::Setup: {
                if (impl_->fxName.empty()) {
                    ImGui_TextColored(impl_->ctx, 0xFF8080FF,
                        "No active FX found. Select a track and touch "
                        "a plugin parameter first.");
                    if (ImGui_Button(impl_->ctx, "Close##ql_close",
                                     nullptr, nullptr)) {
                        open = false;
                    }
                    break;
                }

                char fxHdr[256];
                snprintf(fxHdr, sizeof(fxHdr), "Plugin: %s",
                         impl_->fxName.c_str());
                ImGui_Text(impl_->ctx, fxHdr);
                ImGui_Spacing(impl_->ctx);
                ImGui_Separator(impl_->ctx);
                ImGui_Spacing(impl_->ctx);

                ImGui_Text(impl_->ctx, "Mode:");
                if (ImGui_RadioButton(impl_->ctx, "CS##ql_cs",
                                      impl_->domainChoice == 0))
                    impl_->domainChoice = 0;
                ImGui_SameLine(impl_->ctx, nullptr, nullptr);
                if (ImGui_RadioButton(impl_->ctx, "BC##ql_bc",
                                      impl_->domainChoice == 1))
                    impl_->domainChoice = 1;
                ImGui_SameLine(impl_->ctx, nullptr, nullptr);
                if (ImGui_RadioButton(impl_->ctx, "CS+UF8##ql_csuf8",
                                      impl_->domainChoice == 2))
                    impl_->domainChoice = 2;
                ImGui_SameLine(impl_->ctx, nullptr, nullptr);
                if (ImGui_RadioButton(impl_->ctx, "BC+UF8##ql_bcuf8",
                                      impl_->domainChoice == 3))
                    impl_->domainChoice = 3;
                ImGui_SameLine(impl_->ctx, nullptr, nullptr);
                if (ImGui_RadioButton(impl_->ctx, "UF8##ql_uf8only",
                                      impl_->domainChoice == 4))
                    impl_->domainChoice = 4;

                // Fader bank count (only when UF8 is involved).
                if (impl_->uf8FromChoice()) {
                    ImGui_Spacing(impl_->ctx);
                    ImGui_Text(impl_->ctx, "Fader Banks:");
                    if (ImGui_RadioButton(impl_->ctx, "1##ql_fb1",
                                          impl_->faderBankCount == 1))
                        impl_->faderBankCount = 1;
                    ImGui_SameLine(impl_->ctx, nullptr, nullptr);
                    if (ImGui_RadioButton(impl_->ctx, "2##ql_fb2",
                                          impl_->faderBankCount == 2))
                        impl_->faderBankCount = 2;
                }

                ImGui_Spacing(impl_->ctx);
                ImGui_Checkbox(impl_->ctx, "AutoLearn first##ql_al",
                               &impl_->autoLearnFirst);
                ImGui_SameLine(impl_->ctx, nullptr, nullptr);
                ImGui_TextDisabled(impl_->ctx,
                    "(pre-fill from known patterns)");

                ImGui_Spacing(impl_->ctx);
                ImGui_Text(impl_->ctx, "Display label:");
                int inputFlags = 0;
                ImGui_InputText(impl_->ctx, "##ql_dshort",
                                impl_->displayShort,
                                sizeof(impl_->displayShort), &inputFlags,
                                nullptr);

                ImGui_Spacing(impl_->ctx);
                ImGui_Separator(impl_->ctx);
                ImGui_Spacing(impl_->ctx);

                if (ImGui_Button(impl_->ctx, "Start##ql_start",
                                 nullptr, nullptr)) {
                    impl_->buildQueue();
                    if (impl_->autoLearnFirst) impl_->applyAutoLearn();
                    // Always land in Mapping so the user sees the full
                    // slot list with existing bindings, click-to-re-wiggle
                    // any row, then explicitly hit "Done" → Review → Save.
                    // (Was: hop to Review when all slots were already
                    // bound, which made bindings non-editable.)
                    impl_->phase = QLPhase::Mapping;
                    int firstUnbound = -1;
                    for (size_t i = 0; i < impl_->queue.size(); ++i) {
                        if (impl_->queue[i].boundParam < 0) {
                            firstUnbound = static_cast<int>(i);
                            break;
                        }
                    }
                    impl_->currentSlot =
                        firstUnbound >= 0 ? firstUnbound : 0;
                    impl_->snapshotBaseline();
                }
                ImGui_SameLine(impl_->ctx, nullptr, nullptr);
                if (ImGui_Button(impl_->ctx, "Cancel##ql_cancel_setup",
                                 nullptr, nullptr)) {
                    open = false;
                }
                break;
            }

            // ================================================================
            // MAPPING PHASE
            // ================================================================
            case QLPhase::Mapping: {
                if (impl_->queue.empty()) {
                    impl_->phase = QLPhase::Review;
                    break;
                }

                // Display label editor — always available while in
                // Mapping so users get to it whether they came through
                // Setup or skipped it (existing-map auto-jump).
                ImGui_Text(impl_->ctx, "Display label:");
                int dsFlags = 0;
                ImGui_InputText(impl_->ctx, "##ql_dshort_map",
                                impl_->displayShort,
                                sizeof(impl_->displayShort), &dsFlags,
                                nullptr);
                ImGui_Spacing(impl_->ctx);

                const int total = static_cast<int>(impl_->queue.size());
                const int cur   = std::clamp(impl_->currentSlot, 0, total - 1);
                auto& slot = impl_->queue[static_cast<size_t>(cur)];

                char progress[64];
                snprintf(progress, sizeof(progress), "Slot %d / %d",
                         cur + 1, total);
                ImGui_Text(impl_->ctx, progress);
                ImGui_Spacing(impl_->ctx);

                // Current slot highlight.
                char prompt[256];
                snprintf(prompt, sizeof(prompt),
                    "%s  —  wiggle the parameter now...",
                    slot.label.c_str());
                ImGui_TextColored(impl_->ctx, 0x80FF80FF, prompt);

                // Poll for wiggle.
                {
                    int detectedParam = -1;
                    std::string detectedName;
                    if (impl_->pollWiggle(detectedParam, detectedName)) {
                        slot.boundParam = detectedParam;
                        slot.boundName  = detectedName;
                        // Auto-advance to next unbound slot.
                        bool advanced = false;
                        for (int i = cur + 1; i < total; ++i) {
                            if (impl_->queue[static_cast<size_t>(i)].boundParam < 0) {
                                impl_->currentSlot = i;
                                impl_->snapshotBaseline();
                                advanced = true;
                                break;
                            }
                        }
                        if (!advanced) {
                            // All remaining slots bound — go to review.
                            impl_->phase = QLPhase::Review;
                        }
                    }
                }

                ImGui_Spacing(impl_->ctx);
                ImGui_Separator(impl_->ctx);
                ImGui_Spacing(impl_->ctx);

                // Show slot list with status. Rows are Selectable so the
                // user can click any [OK] / [ ] entry to move focus there
                // and re-wiggle that slot — needed when QuickLearn opens
                // on an already-learned plug-in and bindings need editing.
                int childFlags = 0;
                double childH = 320;
                if (ImGui_BeginChild(impl_->ctx, "##ql_list",
                                     nullptr, &childH, nullptr, &childFlags))
                {
                    int clickIdx = -1;
                    for (int i = 0; i < total; ++i) {
                        auto& qs = impl_->queue[static_cast<size_t>(i)];
                        char row[256];
                        char selId[40];
                        snprintf(selId, sizeof(selId), "##ql_row_%d", i);
                        if (qs.boundParam >= 0) {
                            snprintf(row, sizeof(row),
                                "[OK] %s -> %s (p%d)%s",
                                qs.label.c_str(), qs.boundName.c_str(),
                                qs.boundParam, selId);
                        } else if (i == cur) {
                            snprintf(row, sizeof(row),
                                ">>> %s  (waiting...)%s",
                                qs.label.c_str(), selId);
                        } else {
                            snprintf(row, sizeof(row), "[ ] %s%s",
                                qs.label.c_str(), selId);
                        }
                        // Colour by state. Selectable carries the click
                        // hit-region; the colour push only affects the
                        // text inside it.
                        uint32_t textCol = 0;
                        if (qs.boundParam >= 0)
                            textCol = (i == cur) ? 0x80FF80FF : 0xFFFFFFFF;
                        else if (i == cur)
                            textCol = 0xFFFF80FF;
                        else
                            textCol = 0x808080FF;
                        int colCount = 1;
                        ImGui_PushStyleColor(impl_->ctx,
                            ImGui_Col_Text, textCol);
                        bool sel = (i == cur);
                        int selFlags = 0;
                        if (ImGui_Selectable(impl_->ctx, row, &sel,
                                             &selFlags, nullptr, nullptr))
                            clickIdx = i;
                        ImGui_PopStyleColor(impl_->ctx, &colCount);
                    }
                    if (clickIdx >= 0) {
                        impl_->currentSlot = clickIdx;
                        impl_->snapshotBaseline();
                    }
                }
                ImGui_EndChild(impl_->ctx);

                ImGui_Spacing(impl_->ctx);

                // Controls.
                if (ImGui_Button(impl_->ctx, "Skip##ql_skip",
                                 nullptr, nullptr)) {
                    for (int i = cur + 1; i < total; ++i) {
                        if (impl_->queue[static_cast<size_t>(i)].boundParam < 0) {
                            impl_->currentSlot = i;
                            impl_->snapshotBaseline();
                            break;
                        }
                    }
                }
                ImGui_SameLine(impl_->ctx, nullptr, nullptr);
                if (ImGui_Button(impl_->ctx, "Undo##ql_undo",
                                 nullptr, nullptr)) {
                    // Clear current binding and go back.
                    slot.boundParam = -1;
                    slot.boundName.clear();
                    for (int i = cur - 1; i >= 0; --i) {
                        impl_->currentSlot = i;
                        impl_->snapshotBaseline();
                        break;
                    }
                }
                ImGui_SameLine(impl_->ctx, nullptr, nullptr);
                if (ImGui_Button(impl_->ctx, "Done##ql_done",
                                 nullptr, nullptr)) {
                    impl_->phase = QLPhase::Review;
                }
                ImGui_SameLine(impl_->ctx, nullptr, nullptr);
                if (ImGui_Button(impl_->ctx, "Cancel##ql_cancel_map",
                                 nullptr, nullptr)) {
                    open = false;
                }
                break;
            }

            // ================================================================
            // REVIEW PHASE
            // ================================================================
            case QLPhase::Review: {
                ImGui_Text(impl_->ctx, "Review mappings:");
                ImGui_Spacing(impl_->ctx);

                int boundCount = 0;
                int childFlags = 0;
                double childH = 350;
                if (ImGui_BeginChild(impl_->ctx, "##ql_review",
                                     nullptr, &childH, nullptr, &childFlags))
                {
                    for (const auto& qs : impl_->queue) {
                        if (qs.boundParam < 0) continue;
                        ++boundCount;
                        char row[256];
                        snprintf(row, sizeof(row), "%s  ->  %s (p%d)",
                            qs.label.c_str(), qs.boundName.c_str(),
                            qs.boundParam);
                        ImGui_Text(impl_->ctx, row);
                    }
                    if (boundCount == 0) {
                        ImGui_TextDisabled(impl_->ctx,
                            "No parameters mapped.");
                    }
                }
                ImGui_EndChild(impl_->ctx);

                ImGui_Spacing(impl_->ctx);
                ImGui_Separator(impl_->ctx);
                ImGui_Spacing(impl_->ctx);

                if (boundCount > 0) {
                    if (ImGui_Button(impl_->ctx, "Save##ql_save",
                                     nullptr, nullptr)) {
                        impl_->saveMap();
                        open = false;
                    }
                    ImGui_SameLine(impl_->ctx, nullptr, nullptr);
                }
                if (ImGui_Button(impl_->ctx, "Back##ql_back",
                                 nullptr, nullptr)) {
                    impl_->phase = QLPhase::Mapping;
                    impl_->snapshotBaseline();
                }
                ImGui_SameLine(impl_->ctx, nullptr, nullptr);
                if (ImGui_Button(impl_->ctx, "Cancel##ql_cancel_rev",
                                 nullptr, nullptr)) {
                    open = false;
                }
                break;
            }
            } // switch phase
        }
        ImGui_End(impl_->ctx);
        impl_->visible = open;
    }

    ThemeBridge::popAll(impl_->ctx, pushed);
    if (impl_->font) ImGui_PopFont(impl_->ctx);
}

} // namespace uf8
