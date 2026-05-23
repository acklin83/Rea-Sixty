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
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "reaper_plugin_functions.h"
#include "reaper_imgui_functions.h"   // no IMPLEMENT — MixerWindow owns it

#include "FocusedParam.h"
#include "PluginMap.h"
#include "UserPluginCatalog.h"

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

    // DisplayShort for the new map
    char displayShort[8] = {};

    void ensureCtx()
    {
        if (ctx) return;
        int sizeW = 480, sizeH = 520;
        ctx = ImGui_CreateContext("Rea-Sixty QuickLearn",
                                 &sizeW, &sizeH, nullptr, nullptr);
        font = ImGui_CreateFont("sans-serif", nullptr);
        if (ctx && font) ImGui_Attach(ctx, font);
    }

    void resolveActiveFx()
    {
        fxTrack = GetSelectedTrack(nullptr, 0);
        fxIdx = -1;
        fxName.clear();
        fxMatch.clear();
        if (!fxTrack) return;

        // Use GetLastTouchedFX as primary source.
        int trWord = -1, fxWord = -1, paramWord = -1;
        if (GetLastTouchedFX(&trWord, &fxWord, &paramWord)) {
            const int trLow = trWord & 0xFFFF;
            if (trLow > 0 && !((trWord & 0xFFFF0000) != 0)) {
                MediaTrack* tr = GetTrack(nullptr, trLow - 1);
                if (tr) {
                    const int fi = fxWord & 0xFFFFFF;
                    if (!((fxWord >> 24) & 0x01)) {
                        char buf[512] = {};
                        TrackFX_GetFXName(tr, fi, buf, sizeof(buf));
                        if (buf[0]) {
                            fxTrack = tr;
                            fxIdx   = fi;
                            fxName  = buf;
                            // Derive match from the name (strip prefix like
                            // "VST3: " or "JS: ").
                            fxMatch = fxName;
                            // Strip common prefixes.
                            for (const char* pfx : {"VST3: ", "VST: ", "VSTi: ",
                                                     "VST3i: ", "JS: ", "AU: ",
                                                     "AUi: ", "CLAP: ", "CLAPi: "}) {
                                if (fxMatch.substr(0, strlen(pfx)) == pfx) {
                                    fxMatch = fxMatch.substr(strlen(pfx));
                                    break;
                                }
                            }
                            return;
                        }
                    }
                }
            }
        }

        // Fallback: first FX on selected track.
        if (fxTrack && TrackFX_GetCount(fxTrack) > 0) {
            char buf[512] = {};
            TrackFX_GetFXName(fxTrack, 0, buf, sizeof(buf));
            fxIdx   = 0;
            fxName  = buf;
            fxMatch = fxName;
            for (const char* pfx : {"VST3: ", "VST: ", "VSTi: ",
                                     "VST3i: ", "JS: ", "AU: ",
                                     "AUi: ", "CLAP: ", "CLAPi: "}) {
                if (fxMatch.substr(0, strlen(pfx)) == pfx) {
                    fxMatch = fxMatch.substr(strlen(pfx));
                    break;
                }
            }
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
            map.displayShort = "USR";

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
        impl_->ctx  = nullptr;
        impl_->font = nullptr;
        // Reset state machine.
        impl_->phase = QLPhase::Setup;
        impl_->domainChoice   = 0;
        impl_->faderBankCount = 1;
        impl_->autoLearnFirst = false;
        impl_->queue.clear();
        impl_->currentSlot = 0;
        impl_->displayShort[0] = '\0';
        // Resolve the active FX.
        impl_->resolveActiveFx();
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

    int winFlags = ImGui_WindowFlags_NoSavedSettings
                 | ImGui_WindowFlags_NoCollapse;
    char winId[64];
    snprintf(winId, sizeof(winId),
             "QuickLearn##ql_session_%d", impl_->sessionGen);
    bool open = impl_->visible;

    if (impl_->visible) {
        int condAlways = ImGui_Cond_Always;
        ImGui_SetNextWindowCollapsed(impl_->ctx, false, &condAlways);
        const bool bodyVisible =
            ImGui_Begin(impl_->ctx, winId, &open, &winFlags);
        if (bodyVisible) {
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
                ImGui_SameLine(impl_->ctx, nullptr, nullptr);
                int inputFlags = 0;
                ImGui_InputText(impl_->ctx, "##ql_dshort",
                                impl_->displayShort,
                                sizeof(impl_->displayShort), &inputFlags);

                ImGui_Spacing(impl_->ctx);
                ImGui_Separator(impl_->ctx);
                ImGui_Spacing(impl_->ctx);

                if (ImGui_Button(impl_->ctx, "Start##ql_start",
                                 nullptr, nullptr)) {
                    impl_->buildQueue();
                    if (impl_->autoLearnFirst) {
                        impl_->applyAutoLearn();
                    }
                    if (impl_->phase != QLPhase::Review) {
                        impl_->phase = QLPhase::Mapping;
                        impl_->snapshotBaseline();
                    }
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

                // Show slot list with status.
                int childFlags = 0;
                double childH = 280;
                if (ImGui_BeginChild(impl_->ctx, "##ql_list",
                                     nullptr, &childH, nullptr, &childFlags))
                {
                    for (int i = 0; i < total; ++i) {
                        const auto& qs = impl_->queue[static_cast<size_t>(i)];
                        char row[256];
                        if (qs.boundParam >= 0) {
                            snprintf(row, sizeof(row),
                                "[OK] %s -> %s (p%d)",
                                qs.label.c_str(), qs.boundName.c_str(),
                                qs.boundParam);
                            if (i == cur)
                                ImGui_TextColored(impl_->ctx, 0x80FF80FF, row);
                            else
                                ImGui_Text(impl_->ctx, row);
                        } else if (i == cur) {
                            snprintf(row, sizeof(row),
                                ">>> %s  (waiting...)", qs.label.c_str());
                            ImGui_TextColored(impl_->ctx, 0xFFFF80FF, row);
                        } else {
                            snprintf(row, sizeof(row),
                                "[ ] %s", qs.label.c_str());
                            ImGui_TextDisabled(impl_->ctx, row);
                        }
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
