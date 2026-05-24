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
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
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
    Mapping,     // sequential wiggle-detect + editable table + Save
                 // (Review was merged into Mapping — Frank 2026-05-24:
                 // with the editable table, a separate read-only review
                 // step was duplicate ceremony. Save lives on Mapping now.)
};

// What we're mapping right now — UC1 slots or UF8 V-Pot banks.
enum class QLTarget : int {
    Uc1,
    Uf8,
};

// One slot in the sequential mapping queue.
struct QLSlot {
    std::string label;       // display: "Comp Thr", "Bank 1 - V-Pot 1", etc.
    int         linkIdx = -1;   // UC1 linkIdx (QLTarget::Uc1) or -1
    int         faderBank = 0;  // UF8 coords (QLTarget::Uf8)
    int         vpotBank  = 0;
    int         strip     = 0;
    QLTarget    target;
    int         boundParam = -1;  // VST3 param bound by wiggle, -1 = unbound
    std::string boundName;        // raw VST3 param name (read-only, from plug-in)
    // Scribble-strip label written to UserLinkSlot::customLabel /
    // UF8BankSlot::label on Save. 7-char hardware limit; we keep a few
    // extra chars in the buffer for the trailing NUL + safe input.
    char        labelBuf[12] = {};
};

// scaleW_ — local copy of SettingsScreen.cpp's font-relative width helper
// so QuickLearn's table widths track the user's font-size choice in the
// same way the AutoLearn table does. Keeping the implementation in sync.
inline double scaleW_(ImGui_Context* ctx, double designWidth)
{
    constexpr double kRefSize = 14.0;
    const double fs = ImGui_GetFontSize(ctx);
    if (fs <= 0) return designWidth;
    return designWidth * (fs / kRefSize);
}

} // anonymous namespace

// ---- Impl ------------------------------------------------------------------

struct QuickLearnWindow::Impl {
    ImGui_Context* ctx     = nullptr;
    ImGui_Font*    font    = nullptr;
    bool           visible = false;
    int            sessionGen = 0;
    // Counter — non-zero for the first N frames after toggle()-to-open
    // (or after a re-open press while already visible). Each of those
    // frames calls ImGui_SetNextWindowFocus AND the macOS bring-to-front
    // helper so the host OS window definitely surfaces above any floating
    // plug-in GUI. Bumped from 3 to 15 (~0.5 s at 30 Hz, Frank 2026-05-24):
    // some plug-in GUIs aggressively re-claim front status for several
    // frames after losing focus, especially out-of-process VST3 hosts —
    // a one-shot raise wasn't sticky enough.
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
    // Editable match-substring for the UserPluginMap. Seeded from the
    // auto-derived fxMatch (FX name without "VST3: " etc.) and writable by
    // the user — useful when a plug-in's full name carries a version
    // suffix you don't want to bake into the match key. Frank 2026-05-24.
    char matchBuf[64] = {};

    // Param snapshot for the Param dropdown in the Mapping table.
    // Built once on entering Mapping; pair = (vst3Param, paramName).
    // We rebuild it lazily so re-entering Mapping after a Setup tweak
    // gets fresh names.
    std::vector<std::pair<int, std::string>> paramSnapshot;

    void buildParamSnapshot()
    {
        paramSnapshot.clear();
        if (!fxTrack || fxIdx < 0) return;
        const int n = TrackFX_GetNumParams(fxTrack, fxIdx);
        paramSnapshot.reserve(static_cast<size_t>(n));
        for (int p = 0; p < n; ++p) {
            char name[256] = {};
            TrackFX_GetParamName(fxTrack, fxIdx, p, name, sizeof(name));
            paramSnapshot.emplace_back(p, std::string(name));
        }
    }

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
        // matchBuf: seed from the stored map.match so re-learn re-uses
        // whatever override the user already committed (don't snap
        // back to the auto-derived FX name).
        if (!m->match.empty()) {
            std::strncpy(matchBuf, m->match.c_str(),
                         sizeof(matchBuf) - 1);
            matchBuf[sizeof(matchBuf) - 1] = '\0';
        }
    }

    void resolveActiveFx()
    {
        fxTrack = nullptr;
        fxIdx = -1;
        fxName.clear();
        fxMatch.clear();

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

        // Priority:
        //   1) Focused FX (any track, incl. master) — the plug-in whose
        //      window currently has OS focus. Strongest signal for the
        //      QuickLearn use case "I have a plug-in open, hit the
        //      action, map it now". Frank 2026-05-24: previously this
        //      required `cand == selected_track` which blocked the
        //      common "plug-in floating, no/other track selected" case.
        //   2) Selected-track + last-touched FX (only if on selected
        //      track — global last-touched is too stale to trust).
        //   3) Selected-track + FX[0].
        {
            int trNum = -1, itemNum = -1, fxNum = -1;
            const int ret = GetFocusedFX2(&trNum, &itemNum, &fxNum);
            // ret&1: track FX focused. ret&4 set means "no longer focused"
            // (mask off — we still want a recently-focused FX as the
            // target since the user may have just clicked QuickLearn).
            if ((ret & 1) && trNum >= 0) {
                MediaTrack* cand = (trNum == 0)
                    ? GetMasterTrack(nullptr)
                    : GetTrack(nullptr, trNum - 1);
                const int candFx = fxNum & 0x00FFFFFF;
                if (cand && candFx >= 0
                    && candFx < TrackFX_GetCount(cand))
                {
                    if (adopt(cand, candFx)) return;
                }
            }
        }

        MediaTrack* sel = GetSelectedTrack(nullptr, 0);
        if (!sel) {
            // Master track fallback when nothing's selected.
            sel = GetMasterTrack(nullptr);
            if (!sel || TrackFX_GetCount(sel) == 0) return;
        }
        fxTrack = sel;

        // Selected-track + last-touched FX (only when on selected).
        {
            int trWord = -1, fxWord = -1, paramWord = -1;
            if (GetLastTouchedFX(&trWord, &fxWord, &paramWord)) {
                const int trLow = trWord & 0xFFFF;
                if (trLow > 0 && !((trWord & 0xFFFF0000) != 0)) {
                    MediaTrack* tr = GetTrack(nullptr, trLow - 1);
                    const int fi = fxWord & 0xFFFFFF;
                    if (tr == sel && !((fxWord >> 24) & 0x01)) {
                        if (adopt(sel, fi)) return;
                    }
                }
            }
        }

        // FX[0] of the selected track (last resort).
        if (TrackFX_GetCount(sel) > 0) {
            adopt(sel, 0);
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

        // UF8 V-Pot banks. Slot-label nomenclature matches the AutoLearn
        // Preview table (Frank 2026-05-24): "Bank N - V-Pot M" for the
        // single-fader-bank case, prefixed with "FB X · " when a second
        // fader bank is in play. Keeps the two FX-Learn surfaces visually
        // and semantically consistent.
        if (wantUf8 || dom == Domain::None) {
            for (int fb = 0; fb < faderBankCount; ++fb) {
                for (int vb = 0; vb < kUserUf8VpotBankCount; ++vb) {
                    for (int st = 0; st < 8; ++st) {
                        QLSlot qs;
                        char lbl[40];
                        if (faderBankCount > 1) {
                            snprintf(lbl, sizeof(lbl),
                                "FB %c · Bank %d - V-Pot %d",
                                static_cast<char>('A' + fb),
                                vb + 1, st + 1);
                        } else {
                            snprintf(lbl, sizeof(lbl),
                                "Bank %d - V-Pot %d", vb + 1, st + 1);
                        }
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
        auto seedLabelBuf = [](char* buf, size_t cap, const std::string& s) {
            if (s.empty()) { buf[0] = '\0'; return; }
            std::strncpy(buf, s.c_str(), cap - 1);
            buf[cap - 1] = '\0';
        };
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
                                // labelBuf = customLabel if set, else
                                // the raw param name as a starting point.
                                seedLabelBuf(qs.labelBuf,
                                    sizeof(qs.labelBuf),
                                    !s.customLabel.empty()
                                        ? s.customLabel : qs.boundName);
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
                            // boundName is the *raw* plug-in param name —
                            // displayed in the read-only Param column.
                            // Scribble label travels through labelBuf.
                            qs.boundName  = paramNameForBound_(bs.vst3Param);
                            seedLabelBuf(qs.labelBuf,
                                sizeof(qs.labelBuf),
                                !bs.label.empty()
                                    ? bs.label : qs.boundName);
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

        // Run AutoLearn engine. When the engine fills a slot, seed the
        // editable labelBuf from the param name so the Mapping table
        // shows something useful in the Label column right away (user
        // can tighten the wording before hitting Save).
        auto seedLabelBuf = [](char* buf, size_t cap, const std::string& s) {
            if (s.empty()) { buf[0] = '\0'; return; }
            std::strncpy(buf, s.c_str(), cap - 1);
            buf[cap - 1] = '\0';
        };
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
                        seedLabelBuf(qs.labelBuf,
                            sizeof(qs.labelBuf), s.paramName);
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
                        seedLabelBuf(qs.labelBuf,
                            sizeof(qs.labelBuf), u.paramName);
                        break;
                    }
                }
            }
        }

        // Advance currentSlot to first unbound (or leave on last if
        // all are already bound — user can still re-pick any row).
        for (int i = 0; i < static_cast<int>(queue.size()); ++i) {
            if (queue[static_cast<size_t>(i)].boundParam < 0) {
                currentSlot = i;
                return;
            }
        }
    }

    void saveMap()
    {
        // Effective match key — user override wins over auto-derived
        // fxMatch (Frank 2026-05-24). Empty buffer falls back so we
        // never write a blank match.
        const std::string effMatch = (matchBuf[0] != '\0')
            ? std::string(matchBuf) : fxMatch;
        if (effMatch.empty()) return;

        UserPluginMap map;
        // Check if a map already exists. Lookup uses the *effective*
        // key so renaming the match doesn't lose existing slots when
        // the same plug-in is re-learned with a tweaked substring.
        if (const auto* existing = user_plugins::lookupOwnedByName(effMatch))
            map = *existing;
        else
            map.match = effMatch;
        // Always write the current effective match (covers the rename
        // case where existing != nullptr but the user changed matchBuf).
        map.match = effMatch;

        map.domain  = domainFromChoice();
        map.uf8Mode = uf8FromChoice() || map.domain == Domain::None;
        if (map.domain == Domain::None) map.uf8Mode = true;
        if (displayShort[0])
            map.displayShort = displayShort;
        else if (map.displayShort.empty())
            map.displayShort = !effMatch.empty() ? effMatch : "USR";

        // Apply UC1 slot bindings. labelBuf -> customLabel (scribble
        // strip override); when the user didn't touch it, falls back to
        // the param name so the strip isn't blank.
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
                const std::string lbl = qs.labelBuf[0]
                    ? std::string(qs.labelBuf) : qs.boundName;
                map.slots.push_back({qs.linkIdx, qs.boundParam, false, lbl});
            }
        }

        // Apply UF8 V-Pot bank bindings. labelBuf -> UF8BankSlot.label
        // (drives the V-Pot scribble) with the same fallback rule.
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
                bs.label = qs.labelBuf[0]
                    ? std::string(qs.labelBuf) : qs.boundName;
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
    // Toggle semantic (Frank 2026-05-24): when the window is already
    // open, do NOT close — just raise it above any focused plug-in GUI.
    // Rationale: the most common QuickLearn workflow is "plug-in window
    // floating in front → hit REAPER action → wiggle and map". If the
    // QuickLearn host happens to be hidden behind the plug-in GUI when
    // the action fires, a plain toggle would close it without the user
    // ever seeing it — looks broken. The user closes via the title-bar
    // X or the in-window Cancel/Save buttons instead.
    if (impl_->visible) {
        impl_->focusPendingFrames = 3;    // 3 frames of raise attempts
        // Re-resolve the active FX in case the user has since focused
        // a different plug-in window. We only re-seed when we don't
        // have one yet (avoid blowing away in-progress mapping work).
        if (impl_->fxName.empty() || impl_->phase == QLPhase::Setup) {
            impl_->resolveActiveFx();
            impl_->seedFromExistingMap_();
            if (impl_->phase == QLPhase::Setup
                && !impl_->fxMatch.empty()
                && user_plugins::lookupOwnedByName(impl_->fxMatch))
            {
                impl_->buildQueue();
                impl_->buildParamSnapshot();
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
        return;
    }

    impl_->visible = true;
    ++impl_->sessionGen;
    impl_->focusPendingFrames = 3;    // 3 frames of raise attempts (open path)
    // Fresh ImGui context per open — same trick MixerWindow uses.
    // We previously kept ctx alive across toggles to dodge a ReaImGui
    // per-name context cache that returned closed-state on re-open;
    // that workaround caused a worse bug (Frank 2026-05-24): after
    // the first body click the host window went inactive and stopped
    // accepting any further OS or ImGui input. Resetting the ctx
    // pointer forces ensureCtx() to call ImGui_CreateContext again,
    // which (combined with NoSavedSettings + the session-bumped
    // winId in ImGui_Begin) gives a brand-new host window every
    // open — input routing stays healthy. ReaImGui v0.10 GCs the
    // orphaned previous context on its next defer cycle.
    impl_->ctx  = nullptr;
    impl_->font = nullptr;
    // Reset state machine.
    impl_->phase = QLPhase::Setup;
    impl_->domainChoice   = 0;
    impl_->faderBankCount = 1;
    impl_->autoLearnFirst = false;
    impl_->queue.clear();
    impl_->paramSnapshot.clear();
    impl_->currentSlot = 0;
    impl_->displayShort[0] = '\0';
    impl_->matchBuf[0] = '\0';
    // Resolve the active FX, then seed Setup-phase inputs from any
    // existing UserPluginMap so re-opening on a known plug-in lands
    // the user on familiar settings (domain/UF8/displayShort).
    impl_->resolveActiveFx();
    impl_->seedFromExistingMap_();
    // Seed matchBuf from the auto-derived fxMatch — user can override
    // before saving (Frank 2026-05-24).
    if (!impl_->fxMatch.empty()) {
        std::strncpy(impl_->matchBuf, impl_->fxMatch.c_str(),
                     sizeof(impl_->matchBuf) - 1);
        impl_->matchBuf[sizeof(impl_->matchBuf) - 1] = '\0';
    }
    // If this plug-in is already learned, skip the Setup step and
    // drop the user straight into the editable Mapping list. Setup
    // is only needed for first-time learn (domain pick + UF8 toggle).
    if (!impl_->fxMatch.empty()
        && user_plugins::lookupOwnedByName(impl_->fxMatch))
    {
        impl_->buildQueue();
        impl_->buildParamSnapshot();
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

    // NoSavedSettings: don't let ReaImGui persist this window's inner
    // state across sessions. Earlier ReaImGui's persistence pinned a
    // bad collapsed/tiny-size state (seen as quickLearnSizeH=79 in
    // reaper-extstate.ini) — once that state was stuck, the inner
    // window opened as a sliver below the title bar and looked like
    // "the window never appears". Frank 2026-05-24.
    // Combined with a session-bumped winId (sessionGen++ on each open)
    // this guarantees a fresh, unconstrained inner window per open.
    // NoCollapse: prevent title-bar-only state entirely.
    int winFlags = ImGui_WindowFlags_NoCollapse
                 | ImGui_WindowFlags_NoSavedSettings
                 | ImGui_WindowFlags_NoResize
                 | ImGui_WindowFlags_NoMove
                 | ImGui_WindowFlags_NoScrollbar
                 | ImGui_WindowFlags_NoScrollWithMouse;
    // NoResize/NoMove: the inner ImGui window is pinned to the host
    // content area (0,0 + oversized). User resizes/moves the OS host
    // frame, not the inner. Without these, ImGui-driven drag/resize
    // would fight our Cond_Always pos/size.
    // NoScrollbar/NoScrollWithMouse: the inner is intentionally
    // oversized for hit-test coverage; we don't want scrollbars or
    // mouse-wheel scrolling the (mostly-empty) inner.
    char winId[64];
    snprintf(winId, sizeof(winId),
             "QuickLearn##session_%d", impl_->sessionGen);
    bool open = impl_->visible;

    if (impl_->visible) {
        int condAlways = ImGui_Cond_Always;
        ImGui_SetNextWindowCollapsed(impl_->ctx, false, &condAlways);
        // Explicit pos/size on the FirstUseEver ID so the inner window
        // adopts our remembered shape on the first frame of each session.
        // We use the same persisted values that CreateContext read.
        int sizeW = 520, sizeH = 720;
        const char* sw = GetExtState("ReaSixty", "quickLearnSizeW");
        const char* sh = GetExtState("ReaSixty", "quickLearnSizeH");
        if (sw && *sw) { int v = std::atoi(sw); if (v >= 320 && v <= 4096) sizeW = v; }
        if (sh && *sh) { int v = std::atoi(sh); if (v >= 320 && v <= 4096) sizeH = v; }
        // ROOT CAUSE of the "first-click greys out the window and
        // breaks all input" symptom (Frank 2026-05-24, after digging
        // through the ReaImGui v0.9.3.3 changelog):
        //
        //   ReaImGui reports hit-test transparency to SWELL's
        //   WindowFromPoint on macOS. Any pixel of the OS host that
        //   ImGui has NOT covered with opaque content is treated as
        //   click-through — the click skips QuickLearn entirely and
        //   lands on whatever's behind, the host loses key status,
        //   and every subsequent event goes to the wrong window.
        //
        // Without an explicit SetNextWindowPos, ImGui placed the inner
        // window at its default new-window offset (~60 px from the
        // host origin), leaving an L-shaped transparent strip across
        // the top and left of the OS host. MixerWindow doesn't hit
        // this because its inner-window size (1500×1080) is larger
        // than its CreateContext host (1280×720), so the inner
        // overflows the host and there are no transparent margins.
        //
        // Fix: pin the inner to (0, 0) every frame. Oversize it a
        // bit (host width × 4, host height × 4) so the inner reliably
        // overflows whatever shape the user has dragged the host
        // into — no transparent margins regardless of host size.
        // ImGui clips to the host content area, so the oversize is
        // free; ImGui_WindowFlags_NoScrollbar / NoScrollWithMouse
        // (added below) keep the user from seeing scrollbars from
        // the inner overflow.
        double posX = 0, posY = 0;
        int condAlwaysPos = ImGui_Cond_Always;
        ImGui_SetNextWindowPos(impl_->ctx, posX, posY,
                               &condAlwaysPos,
                               /*pivot_x*/ nullptr,
                               /*pivot_y*/ nullptr);
        double sW = sizeW * 4.0, sH = sizeH * 4.0;
        int condAlwaysSize = ImGui_Cond_Always;
        ImGui_SetNextWindowSize(impl_->ctx, sW, sH, &condAlwaysSize);
        (void)impl_->focusPendingFrames;
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
            // Write-side sanity gate: only persist sizes within the
            // same range the read-side accepts. Without this, any
            // transient tiny GetWindowSize result (collapse animation,
            // ReaImGui startup race) could pin a bad sizeH like 79 into
            // ExtState forever (Frank 2026-05-24 — fixed alongside the
            // NoSavedSettings switch on the inner window).
            if (iw >= 320 && iw <= 4096 && iw != impl_->lastSavedW) {
                persist("quickLearnSizeW", iw); impl_->lastSavedW = iw;
            }
            if (ih >= 320 && ih <= 4096 && ih != impl_->lastSavedH) {
                persist("quickLearnSizeH", ih); impl_->lastSavedH = ih;
            }
            // Position: clamp to a generous on-screen range so a stray
            // off-screen result doesn't pin a useless pose.
            if (ix >= -100 && ix <= 8192 && ix != impl_->lastSavedX) {
                persist("quickLearnPosX",  ix); impl_->lastSavedX = ix;
            }
            if (iy >= -100 && iy <= 8192 && iy != impl_->lastSavedY) {
                persist("quickLearnPosY",  iy); impl_->lastSavedY = iy;
            }
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
                    impl_->buildParamSnapshot();
                    if (impl_->autoLearnFirst) impl_->applyAutoLearn();
                    // Always land in Mapping so the user sees the full
                    // slot list with existing bindings, click-to-re-wiggle
                    // any row OR pick params via the column dropdown,
                    // then hit Save. Review phase was merged into
                    // Mapping (Frank 2026-05-24).
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
            // Table-based layout (Frank 2026-05-24) — mirrors the
            // AutoLearn Preview table's columns and styling so the two
            // FX-Learn surfaces feel like the same product. The old
            // "[OK]" / ">>>" / "[ ]" prefixes were redundant with the
            // row-text colour; dropped. Status now lives in the row
            // background tint (current row = highlight, conflict = red).
            // Per-row scribble labels are editable inline.
            case QLPhase::Mapping: {
                if (impl_->queue.empty()) {
                    ImGui_TextDisabled(impl_->ctx,
                        "No slots — adjust setup and reopen.");
                    if (ImGui_Button(impl_->ctx,
                            "Cancel##ql_cancel_empty",
                            nullptr, nullptr))
                    {
                        open = false;
                    }
                    break;
                }

                // ---- Display label + Match header block ----
                // Two inputs side-by-side. Display label drives the
                // FX-name header in mixer/Toggle UI; Match overrides
                // the substring used by UserPluginCatalog::lookupOwnedByName
                // (auto-seeded from the raw FX name minus the format
                // prefix — user can shorten or rewrite to control which
                // plug-ins this map binds to). Frank 2026-05-24.
                const double colInputW = scaleW_(impl_->ctx, 160.0);
                ImGui_Text(impl_->ctx, "Display label:");
                ImGui_SameLine(impl_->ctx, nullptr, nullptr);
                ImGui_SetNextItemWidth(impl_->ctx, colInputW);
                int dsFlags = 0;
                ImGui_InputText(impl_->ctx, "##ql_dshort_map",
                                impl_->displayShort,
                                sizeof(impl_->displayShort), &dsFlags,
                                nullptr);
                ImGui_SameLine(impl_->ctx, nullptr, nullptr);
                ImGui_Text(impl_->ctx, "   Match:");
                ImGui_SameLine(impl_->ctx, nullptr, nullptr);
                ImGui_SetNextItemWidth(impl_->ctx, colInputW);
                int mtFlags = 0;
                ImGui_InputTextWithHint(impl_->ctx, "##ql_match_map",
                                impl_->fxMatch.c_str(),
                                impl_->matchBuf,
                                sizeof(impl_->matchBuf),
                                &mtFlags, nullptr);

                ImGui_Spacing(impl_->ctx);

                const int total = static_cast<int>(impl_->queue.size());
                const int cur   = std::clamp(impl_->currentSlot, 0, total - 1);
                auto& slot = impl_->queue[static_cast<size_t>(cur)];

                // Progress + active-slot prompt on one line.
                char progress[64];
                snprintf(progress, sizeof(progress), "Slot %d / %d",
                         cur + 1, total);
                ImGui_Text(impl_->ctx, progress);
                ImGui_SameLine(impl_->ctx, nullptr, nullptr);
                ImGui_TextDisabled(impl_->ctx, "  —  ");
                ImGui_SameLine(impl_->ctx, nullptr, nullptr);
                char prompt[256];
                snprintf(prompt, sizeof(prompt),
                    "%s  (wiggle the parameter now…)",
                    slot.label.c_str());
                ImGui_TextColored(impl_->ctx, 0xFFFF80FF, prompt);

                // Poll for wiggle (only on the live current slot, which
                // is what snapshotBaseline tracks). Seed labelBuf when
                // freshly bound so the Label column shows immediately.
                {
                    int detectedParam = -1;
                    std::string detectedName;
                    if (impl_->pollWiggle(detectedParam, detectedName)) {
                        slot.boundParam = detectedParam;
                        slot.boundName  = detectedName;
                        if (slot.labelBuf[0] == '\0') {
                            std::strncpy(slot.labelBuf, detectedName.c_str(),
                                sizeof(slot.labelBuf) - 1);
                            slot.labelBuf[sizeof(slot.labelBuf) - 1] = '\0';
                        }
                        // Auto-advance to next unbound slot. If all
                        // bound, leave the cursor where it is — user
                        // hits Save when satisfied (no auto-jump to a
                        // separate review screen anymore).
                        for (int i = cur + 1; i < total; ++i) {
                            if (impl_->queue[static_cast<size_t>(i)].boundParam < 0) {
                                impl_->currentSlot = i;
                                impl_->snapshotBaseline();
                                break;
                            }
                        }
                    }
                }

                // ---- Conflict scan (mirrors AutoLearn Preview) ----
                // Count vst3Param hits across all bound slots so the
                // table can tint conflict rows red and a warning shows
                // above the table. Two slots → same plug-in param is
                // almost always unintended.
                std::unordered_map<int, int> paramHits;
                for (const auto& qs : impl_->queue) {
                    if (qs.boundParam >= 0) ++paramHits[qs.boundParam];
                }
                int paramConflicts = 0;
                for (const auto& kv : paramHits)
                    if (kv.second > 1) ++paramConflicts;
                if (paramConflicts > 0) {
                    char warn[200];
                    snprintf(warn, sizeof(warn),
                        "%d param conflict(s) — two slots target the "
                        "same plug-in param. Repick or undo to resolve.",
                        paramConflicts);
                    ImGui_TextColored(impl_->ctx, 0xFF6060FF, warn);
                }

                ImGui_Spacing(impl_->ctx);
                ImGui_Separator(impl_->ctx);
                ImGui_Spacing(impl_->ctx);

                // ---- Slot table ----
                // Columns: Slot (location) | Param (dropdown — pick by
                // wiggle OR by combo with filter) | Label (editable
                // scribble-strip override). Width budget tuned at the
                // AutoLearn-default 14 px font (scaleW_ stretches at
                // larger fonts).
                const double colSlotW  = scaleW_(impl_->ctx, 150.0);
                const double colLabelW = scaleW_(impl_->ctx, 100.0);
                const int    wFixed    = ImGui_TableColumnFlags_WidthFixed;
                const int    wStretch  = ImGui_TableColumnFlags_WidthStretch;
                int tblFlags = ImGui_TableFlags_RowBg
                             | ImGui_TableFlags_ScrollY
                             | ImGui_TableFlags_BordersInnerH;
                double tblH = scaleW_(impl_->ctx, 340.0);
                int clickIdx = -1;
                if (ImGui_BeginTable(impl_->ctx, "##ql_tbl", 3,
                                     &tblFlags, nullptr, &tblH, nullptr))
                {
                    int    wF1 = wFixed, wF2 = wStretch, wF3 = wFixed;
                    double w1 = colSlotW, w2 = 0,        w3 = colLabelW;
                    ImGui_TableSetupColumn(impl_->ctx, "Slot",  &wF1, &w1, nullptr);
                    ImGui_TableSetupColumn(impl_->ctx, "Param", &wF2, &w2, nullptr);
                    ImGui_TableSetupColumn(impl_->ctx, "Label", &wF3, &w3, nullptr);
                    ImGui_TableHeadersRow(impl_->ctx);

                    for (int i = 0; i < total; ++i) {
                        auto& qs = impl_->queue[static_cast<size_t>(i)];
                        ImGui_TableNextRow(impl_->ctx, nullptr, nullptr);

                        // Row tint priority: conflict > current.
                        const bool rowConflict = qs.boundParam >= 0
                            && paramHits[qs.boundParam] > 1;
                        if (rowConflict) {
                            // Translucent red (AutoLearn parity).
                            int rowBgTarget = ImGui_TableBgTarget_RowBg0;
                            ImGui_TableSetBgColor(impl_->ctx, rowBgTarget,
                                                  0xCC444480, nullptr);
                        } else if (i == cur) {
                            int rowBgTarget = ImGui_TableBgTarget_RowBg0;
                            // Dim blue-grey current-row strip.
                            ImGui_TableSetBgColor(impl_->ctx, rowBgTarget,
                                                  0x4060A050, nullptr);
                        }

                        // Per-row text colour: bound = white-ish,
                        // current-unbound = yellow, empty = dim grey.
                        uint32_t textCol;
                        if (qs.boundParam >= 0)
                            textCol = (i == cur) ? 0x80FF80FF : 0xE0E0E0FF;
                        else if (i == cur)
                            textCol = 0xFFFF80FF;
                        else
                            textCol = 0x808080FF;

                        // ---- Slot column ----
                        // Selectable confined to this column only (no
                        // SpanAllColumns). With Param being a Combo and
                        // Label an InputText, those cells need to own
                        // their click hit-areas — a row-spanning
                        // Selectable was eating their clicks and made
                        // the table feel un-clickable. Frank 2026-05-24.
                        ImGui_TableNextColumn(impl_->ctx);
                        char selId[64];
                        snprintf(selId, sizeof(selId),
                                 "%s##ql_slot_%d",
                                 qs.label.c_str(), i);
                        bool selBool = (i == cur);
                        int  selFlags = 0;
                        int popCount = 1;
                        ImGui_PushStyleColor(impl_->ctx,
                            ImGui_Col_Text, textCol);
                        if (ImGui_Selectable(impl_->ctx, selId, &selBool,
                                             &selFlags, nullptr, nullptr))
                            clickIdx = i;
                        ImGui_PopStyleColor(impl_->ctx, &popCount);

                        // ---- Param column ----
                        // Plain text — the previous incarnation tried a
                        // BeginCombo per row but with 64 rows in a
                        // ScrollY table that broke window interaction
                        // entirely (Frank 2026-05-24). Inline picker
                        // moved to a dedicated "Pick…" row below the
                        // table so only ONE Combo lives in the widget
                        // tree at a time.
                        ImGui_TableNextColumn(impl_->ctx);
                        char paramStr[128];
                        if (qs.boundParam >= 0) {
                            snprintf(paramStr, sizeof(paramStr),
                                "%s   p%d",
                                qs.boundName.c_str(), qs.boundParam);
                        } else if (i == cur) {
                            snprintf(paramStr, sizeof(paramStr),
                                "(waiting…)");
                        } else {
                            paramStr[0] = '\0';
                        }
                        if (paramStr[0]) {
                            ImGui_PushStyleColor(impl_->ctx,
                                ImGui_Col_Text, textCol);
                            ImGui_Text(impl_->ctx, paramStr);
                            int popTxt = 1;
                            ImGui_PopStyleColor(impl_->ctx, &popTxt);
                        }

                        // ---- Label column ----
                        ImGui_TableNextColumn(impl_->ctx);
                        if (qs.boundParam >= 0) {
                            char lblId[40];
                            snprintf(lblId, sizeof(lblId),
                                     "##ql_lbl_%d", i);
                            ImGui_SetNextItemWidth(impl_->ctx, -1.0);
                            int inputFlags = 0;
                            ImGui_InputTextWithHint(impl_->ctx, lblId,
                                qs.boundName.c_str(),
                                qs.labelBuf, 8,
                                &inputFlags, nullptr);
                        }
                    }
                    ImGui_EndTable(impl_->ctx);
                }
                if (clickIdx >= 0) {
                    impl_->currentSlot = clickIdx;
                    impl_->snapshotBaseline();
                }

                ImGui_Spacing(impl_->ctx);

                // ---- Controls ----
                // Save replaces the old Done/Review-then-Save flow
                // (Frank 2026-05-24). With the table editable in-place,
                // a separate Review step was duplicate ceremony.
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
                    slot.boundParam = -1;
                    slot.boundName.clear();
                    slot.labelBuf[0] = '\0';
                    for (int i = cur - 1; i >= 0; --i) {
                        impl_->currentSlot = i;
                        impl_->snapshotBaseline();
                        break;
                    }
                }
                ImGui_SameLine(impl_->ctx, nullptr, nullptr);
                {
                    int boundCount = 0;
                    for (const auto& qs : impl_->queue)
                        if (qs.boundParam >= 0) ++boundCount;
                    if (boundCount > 0) {
                        if (ImGui_Button(impl_->ctx, "Save##ql_save",
                                         nullptr, nullptr)) {
                            impl_->saveMap();
                            open = false;
                        }
                    } else {
                        // Render Save in disabled style (ReaImGui v0.10
                        // doesn't expose BeginDisabled, so we fake it
                        // with a dim button colour pair).
                        ImGui_PushStyleColor(impl_->ctx,
                            ImGui_Col_Button,        0x44444460);
                        ImGui_PushStyleColor(impl_->ctx,
                            ImGui_Col_ButtonHovered, 0x44444460);
                        ImGui_PushStyleColor(impl_->ctx,
                            ImGui_Col_ButtonActive,  0x44444460);
                        ImGui_PushStyleColor(impl_->ctx,
                            ImGui_Col_Text,          0x80808080);
                        ImGui_Button(impl_->ctx, "Save##ql_save_dis",
                                     nullptr, nullptr);
                        int popped = 4;
                        ImGui_PopStyleColor(impl_->ctx, &popped);
                    }
                }
                ImGui_SameLine(impl_->ctx, nullptr, nullptr);
                if (ImGui_Button(impl_->ctx, "Cancel##ql_cancel_map",
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
