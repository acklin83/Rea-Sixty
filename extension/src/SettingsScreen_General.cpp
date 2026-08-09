//
// SettingsScreen_General.cpp — the three general Settings panes, split out
// of SettingsScreen.cpp so that file stops being the place every pane lives.
// Pure translation-unit move: the pane bodies came across unchanged; the
// only code that is new here is the rail-search scroll plumbing below.
// Frank 2026-08-09.
//
#include "SettingsScreen.h"

#include <cstdio>           // snprintf — device lines, calibration readouts
#include <cstdlib>          // std::atoi — ExtState round-trips
#include <string>           // g_scrollPending / g_scrollActive

#include "Bindings.h"       // uf8::bindings::getActiveLayer (startup soft-key bank)
#include "reaper_imgui_functions.h"
#include "reaper_plugin_functions.h"   // Get/SetExtState

// Forward declarations of accessors defined in main.cpp — the subset these
// three panes use, copied verbatim from SettingsScreen.cpp (which keeps the
// full list for the panes that stayed there). Same pattern as
// reasixty_followSelectedInMixer / reasixty_toggleMixerWindow — keeps the
// anonymous-namespace globals owned by main.cpp while letting the UI read
// runtime state. Called only from the main thread (via onTimer → ImGui).
bool reasixty_uf8Connected();
bool reasixty_uc1Connected();
bool reasixty_uf1Connected();
const char* reasixty_uf8Serial();
const char* reasixty_uc1Serial();
const char* reasixty_uf1Serial();
int  reasixty_brightnessLevel();
int  reasixty_scribbleBrightnessLevel();
void reasixty_setBrightnessLevel(int level);
void reasixty_setScribbleBrightnessLevel(int level);
void reasixty_identifyUf8();
void reasixty_identifyUc1();
bool reasixty_selFollowsColor();
void reasixty_setSelFollowsColor(bool follow);
bool reasixty_grAnyFx();
void reasixty_setGrAnyFx(bool enabled);
bool reasixty_grCombineUf8();
void reasixty_setGrCombineUf8(bool on);
bool reasixty_grCombineUc1();
void reasixty_setGrCombineUc1(bool on);
bool reasixty_insertMarkers();
void reasixty_setInsertMarkers(bool on);
bool reasixty_focusedPanel();
void reasixty_setFocusedPanel(bool on);
bool reasixty_focusedPanelRunning();
bool reasixty_modeBanner();
void reasixty_setModeBanner(bool on);
bool reasixty_modeBannerRunning();
int    reasixty_overlayCsColor();
void   reasixty_setOverlayCsColor(int rgb);
int    reasixty_overlayBcColor();
void   reasixty_setOverlayBcColor(int rgb);
int    reasixty_overlaySelColor();
void   reasixty_setOverlaySelColor(int rgb);
double reasixty_overlayFillAlpha();
void   reasixty_setOverlayFillAlpha(double a);
double reasixty_overlayLineAlpha();
void   reasixty_setOverlayLineAlpha(double a);
int    reasixty_overlayRowHeight();
void   reasixty_setOverlayRowHeight(int px);
int    reasixty_overlayTopPad();
void   reasixty_setOverlayTopPad(int px);
bool   reasixty_insertsOverlayRunning();
int    reasixty_uc1CalCount(int section);
double reasixty_uc1CalTickDb(int section, int idx);
double reasixty_uc1CalGet(int section, int idx);
void   reasixty_uc1CalSet(int section, int idx, double newVal);
void   reasixty_uc1CalResetSection(int section);
int    reasixty_uc1CalActiveTest();
void   reasixty_uc1SetCalActiveTest(int enc);
bool reasixty_trackSelFollowsParam();
void reasixty_setTrackSelFollowsParam(bool follow);
bool reasixty_touchSelectsChannel();
void reasixty_setTouchSelectsChannel(bool on);
bool reasixty_uc1ShowMasterAsTrack0();
void reasixty_setUc1ShowMasterAsTrack0(bool on);
bool reasixty_masterPinShift();
void reasixty_setMasterPinShift(bool shift);
bool reasixty_cycleEngagesUf8();
void reasixty_setCycleEngagesUf8(bool on);
bool reasixty_altDragSnapBack();
void reasixty_setAltDragSnapBack(bool on);
bool reasixty_hideOfflineFx();
void reasixty_setHideOfflineFx(bool on);
bool reasixty_wrapPluginCycle();
void reasixty_setWrapPluginCycle(bool on);
bool reasixty_jsfxGridFine();
void reasixty_setJsfxGridFine(bool on);
bool reasixty_paramSwitchesSoftKeyBank(); void reasixty_setParamSwitchesSoftKeyBank(bool on);
bool reasixty_keyboardShiftModifier();
void reasixty_setKeyboardShiftModifier(bool on);
bool reasixty_keyboardCmdModifier();
void reasixty_setKeyboardCmdModifier(bool on);
bool reasixty_keyboardCtrlModifier();
void reasixty_setKeyboardCtrlModifier(bool on);
bool reasixty_shiftFineMode();
void reasixty_setShiftFineMode(bool on);
double reasixty_knobSpeedUf8();
void   reasixty_setKnobSpeedUf8(double v);
double reasixty_knobSpeedUc1();
void   reasixty_setKnobSpeedUc1(double v);
double reasixty_fineFactorUf8();
void   reasixty_setFineFactorUf8(double v);
double reasixty_fineFactorUc1();
void   reasixty_setFineFactorUc1(double v);
double reasixty_notchZone();
void   reasixty_setNotchZone(double v);
double reasixty_notchFineStep();
void   reasixty_setNotchFineStep(double v);
double reasixty_notchHold();
void   reasixty_setNotchHold(double v);
int  reasixty_theme();
void reasixty_setTheme(int t);
int  reasixty_fontScale();
void reasixty_setFontScale(int s);
bool reasixty_tcpFollowsSelection();
void reasixty_setTcpFollowsSelection(bool on);
int  reasixty_visibilityFollow();
void reasixty_setVisibilityFollow(int v);
bool reasixty_pinnedSurvivesBanking();
void reasixty_setPinnedSurvivesBanking(bool v);
int  reasixty_uiSpelling();
void reasixty_setUiSpelling(int v);
bool reasixty_stripFollowsFocusedFx();
void reasixty_setStripFollowsFocusedFx(bool follow);
bool reasixty_pluginGuiFollowsInstance();
bool reasixty_uf1StripKeyWithGui();
void reasixty_setUf1StripKeyWithGui(bool withGui);
void reasixty_setPluginGuiFollowsInstance(bool follow);
bool reasixty_pluginGuiPinPos();
void reasixty_setPluginGuiPinPos(bool on);
void reasixty_getPluginGuiPin(int* x, int* y);
bool reasixty_capturePluginGuiPin();
bool reasixty_pluginGuiPinCenter();
void reasixty_setPluginGuiPinCenter(bool on);
bool reasixty_fxChainPinPos();
void reasixty_setFxChainPinPos(bool on);
void reasixty_getFxChainPin(int* x, int* y);
bool reasixty_captureFxChainPin();
bool reasixty_fxChainPinCenter();
void reasixty_setFxChainPinCenter(bool on);
// Per-meter peak fall rate (meterId: 0 = UC1 In, 1 = UC1 Out, 2 = UF8 strips).
double reasixty_meterFall(int meterId);             // dB/s
void   reasixty_setMeterFall(int meterId, double v);
void   reasixty_copyMeter(int fromId, int toId);
int  reasixty_trackNameMode();
void reasixty_setTrackNameMode(int mode);
int                reasixty_activeQuickFor(int layer);
int                reasixty_activeSubBankFor(int layer);
// Pinned startup soft-key bank (fixed user-Quick engaged at boot).
bool               reasixty_startupBank(int* layer, int* quick, int* sub);
void               reasixty_setStartupBank(bool on, int layer, int quick, int sub);

namespace uf8 {

// Pending "scroll to this heading" request from the rail search box. Set on a
// result click, latched by the very next pane draw, consumed by the matching
// heading in that pane — so it fires exactly once and never leaks into a
// later pane. The request has a one-pane-draw lifetime on purpose: the
// content pane is conditional (BeginChild can return false, and this window
// has a history of Begin returning false on a rail click), so a request no
// heading claims must be DROPPED, not left armed for an unrelated visit ten
// minutes later. Frank 2026-08-09.
static std::string g_scrollPending;   // set by the rail, survives to one draw
static std::string g_scrollActive;    // owned by the pane currently drawing
void settingsRequestSectionScroll(const char* title)
{
    g_scrollPending = (title && *title) ? title : "";
}
// Called by MixerWindow's rail dispatch immediately before EVERY pane's draw
// (MixerWindow.cpp), not from inside the panes: only three of the twelve panes
// own headings, so latching in those three would leave a request armed
// indefinitely whenever any of the other nine drew instead. Taking it here
// gives the request a true one-pane-draw lifetime — whatever the headings
// below don't consume dies on the next pane draw, whichever pane that is.
void settingsLatchSectionScroll()
{
    g_scrollActive = g_scrollPending;
    g_scrollPending.clear();
}
// Called by every section heading in the Devices / Appearance / Behaviour
// panes, right after the heading text is drawn (SetScrollHereY anchors on the
// previous line). No-op unless this heading is the latched target.
static void consumeSectionScroll_(ImGui_Context* ctx, const char* title)
{
    if (g_scrollActive.empty() || !title) return;
    if (g_scrollActive != title) return;
    double ratio = 0.0;
    ImGui_SetScrollHereY(ctx, &ratio);
    g_scrollActive.clear();
}

// Duplicate of the static helper in SettingsScreen.cpp (which still has 79
// call sites of its own) — small, pure, and a shared copy would mean a new
// header for four lines of arithmetic.
static inline double scaleW_(ImGui_Context* ctx, double designWidth)
{
    constexpr double kRefSize = 14.0;
    const double fs = ImGui_GetFontSize(ctx);
    if (fs <= 0) return designWidth;
    return designWidth * (fs / kRefSize);
}

// ---- Appearance -----------------------------------------------------------
// Everything the user SEES, wherever it is rendered: the on-screen companions
// (MCP Inserts overlay, focused-track panel, mode banner) and their colours,
// the surface-side display choices, and the Settings window's own theme /
// font / spelling. Behaviour lives in drawBehaviour, hardware in drawDevices.
// Frank 2026-08-09 — split out of the old catch-all Device pane.
//
// (The 15-line spec comment that used to sit here described the original
// "Tab: Device" sketch in docs/plan-settings-ui.md — a document that no longer
// exists — and listed a meter-ballistic selector and an Export Diagnostic
// Report button that were never built. Dropped rather than carried forward.)
void SettingsScreen::drawAppearance(ImGui_Context* ctx)
{
    // Consistent, roomy section header: a gap above, the title, a rule,
    // and a little air below. Keeps the panes from feeling cramped — use
    // for every section after the first. Frank 2026-06-12.
    auto sectionHeader = [&](const char* title) {
        ImGui_Dummy(ctx, 0.0, 8.0);
        ImGui_Text(ctx, title);
        consumeSectionScroll_(ctx, title);
        ImGui_Separator(ctx);
        ImGui_Spacing(ctx);
    };

    ImGui_Text(ctx, "On-screen");
    consumeSectionScroll_(ctx, "On-screen");
    ImGui_Separator(ctx);
    ImGui_Spacing(ctx);

    // Two independent, persistent cues for the active CS / BC instance, both
    // drawn by the bundled companion Lua (non-destructive — no FX rename, no
    // dirty project). Each toggle auto-starts / stops the companion as needed
    // and is restored on the next REAPER launch.
    //   • MCP overlay — JS_Composite highlight on the native Mixer Inserts row.
    // (The old dockable focused-track docker was retired — the frameless
    // focused-track panel below replaces it.)
    bool insMark = reasixty_insertMarkers();
    if (ImGui_Checkbox(ctx, "Show MCP Inserts overlay", &insMark)) {
        reasixty_setInsertMarkers(insMark);
    }
    // Learn-HUD has no checkbox — it is toggled via the "learn_hud_toggle"
    // built-in (bindable to hardware) and the REASIXTY_LEARN_HUD_TOGGLE REAPER
    // action. Frank 2026-06-15.
    // Frameless focused-track panel (Gridbox-style, floats on the Arrange,
    // drag-to-move / drag-edges-to-resize / right-click menu).
    bool fpanel = reasixty_focusedPanel();
    if (ImGui_Checkbox(ctx, "Show focused-track panel", &fpanel)) {
        reasixty_setFocusedPanel(fpanel);
    }
    if (fpanel) {
        ImGui_TextDisabled(ctx, reasixty_focusedPanelRunning()
                                ? "  Panel companion running"
                                : "  Panel companion starting\xE2\x80\xA6");
    }

    // Transient mode-change banner — flashes the new Selection-/Encoder-Mode
    // on screen for ~2 s then auto-hides. Also bindable via the
    // "mode_banner_toggle" built-in / REAPER action.
    bool mbanner = reasixty_modeBanner();
    if (ImGui_Checkbox(ctx, "Show mode-change banner", &mbanner)) {
        reasixty_setModeBanner(mbanner);
    }
    if (mbanner) {
        ImGui_TextDisabled(ctx, reasixty_modeBannerRunning()
                                ? "  Banner companion running"
                                : "  Banner companion starting\xE2\x80\xA6");
    }

    if (insMark) {
        ImGui_TextDisabled(ctx, reasixty_insertsOverlayRunning()
                                ? "Companion running"
                                : "Companion starting\xE2\x80\xA6");
    }

    // CS / BC colours — shared by the MCP overlay and the frameless panel.
    // Live: each companion re-reads these from ExtState on every repaint.
    // ColorEdit3 uses 0xRRGGBB order — matches the Lua's colour packing.
    if (insMark || fpanel) {
        int ceFlags = ImGui_ColorEditFlags_NoInputs;
        int csCol = reasixty_overlayCsColor();
        ImGui_SetNextItemWidth(ctx, 220.0);
        if (ImGui_ColorEdit3(ctx, "CS colour", &csCol, &ceFlags)) {
            reasixty_setOverlayCsColor(csCol);
        }
        int bcCol = reasixty_overlayBcColor();
        ImGui_SetNextItemWidth(ctx, 220.0);
        if (ImGui_ColorEdit3(ctx, "BC colour", &bcCol, &ceFlags)) {
            reasixty_setOverlayBcColor(bcCol);
        }
        // Selected-FX box (the surface-focused non-CS/BC plug-in) — MCP overlay
        // only, so only offered when the overlay is enabled.
        if (insMark) {
            int selCol = reasixty_overlaySelColor();
            ImGui_SetNextItemWidth(ctx, 220.0);
            if (ImGui_ColorEdit3(ctx, "Selected FX colour", &selCol, &ceFlags)) {
                reasixty_setOverlaySelColor(selCol);
            }
        }
    }

    // Fill / border opacity + row geometry apply to the MCP overlay only.
    if (insMark) {
        double fillA = reasixty_overlayFillAlpha();
        ImGui_SetNextItemWidth(ctx, 220.0);
        if (ImGui_SliderDouble(ctx, "Fill opacity", &fillA, 0.0, 1.0,
                               "%.2f", nullptr)) {
            reasixty_setOverlayFillAlpha(fillA);
        }
        double lineA = reasixty_overlayLineAlpha();
        ImGui_SetNextItemWidth(ctx, 220.0);
        if (ImGui_SliderDouble(ctx, "Border opacity", &lineA, 0.0, 1.0,
                               "%.2f", nullptr)) {
            reasixty_setOverlayLineAlpha(lineA);
        }
        // Row height + first-row offset. The insert-row height is a theme/
        // UI-scale font constant (16/24/32px @ scale 1/1.5/2); dial it until
        // the highlight sits on the right rows.
        int rh = reasixty_overlayRowHeight();
        ImGui_SetNextItemWidth(ctx, 220.0);
        if (ImGui_SliderInt(ctx, "Inserts row height", &rh, 8, 48,
                            nullptr, nullptr)) {
            reasixty_setOverlayRowHeight(rh);
        }
        int tp = reasixty_overlayTopPad();
        ImGui_SetNextItemWidth(ctx, 220.0);
        if (ImGui_SliderInt(ctx, "Inserts top offset", &tp, -20, 40,
                            nullptr, nullptr)) {
            reasixty_setOverlayTopPad(tp);
        }
    }

    ImGui_Spacing(ctx);
    ImGui_Spacing(ctx);
    sectionHeader("Surface display");

    bool selFollow = reasixty_selFollowsColor();
    if (ImGui_Checkbox(ctx, "SEL LED follows REAPER track colour",
                       &selFollow)) {
        reasixty_setSelFollowsColor(selFollow);
    }

    // Track names longer than the 7-char scribble-strip slot need shortening.
    // Truncate keeps the legacy first-7-chars behaviour. Smart Abbreviate
    // strips spaces / vowels-after-first-letter / repeated consonants so
    // "Background Vocals" lands as "BckgrV" instead of "Backgro".
    // BeginCombo + Selectable instead of ImGui_Combo with \0 items —
    // the Combo overload silently renders empty in ReaImGui v0.10.
    {
        const char* kNameModeLabels[2] = { "Truncate", "Smart abbreviate" };
        int curMode = reasixty_trackNameMode();
        if (curMode < 0 || curMode > 1) curMode = 0;
        ImGui_Text(ctx, "Long track-name handling");
        ImGui_SameLine(ctx, nullptr, nullptr);
        ImGui_SetNextItemWidth(ctx, 220.0);
        if (ImGui_BeginCombo(ctx, "##long_track_name_handling",
                             kNameModeLabels[curMode],
                             /*flags*/ nullptr)) {
            for (int i = 0; i < 2; ++i) {
                bool sel = (curMode == i);
                if (ImGui_Selectable(ctx, kNameModeLabels[i], &sel,
                                     /*flags*/ nullptr,
                                     /*size_w*/ nullptr,
                                     /*size_h*/ nullptr)) {
                    reasixty_setTrackNameMode(i);
                }
            }
            ImGui_EndCombo(ctx);
        }
    }

    ImGui_Spacing(ctx);
    ImGui_Spacing(ctx);
    sectionHeader("Theme");

    int theme = reasixty_theme();
    if (ImGui_RadioButtonEx(ctx, "Vanilla", &theme, 0)) {
        reasixty_setTheme(theme);
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (ImGui_RadioButtonEx(ctx, "Dark",    &theme, 1)) {
        reasixty_setTheme(theme);
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (ImGui_RadioButtonEx(ctx, "Light",   &theme, 2)) {
        reasixty_setTheme(theme);
    }

    ImGui_Spacing(ctx);
    ImGui_Spacing(ctx);
    sectionHeader("Font Size");

    int scale = reasixty_fontScale();
    if (ImGui_RadioButtonEx(ctx, "Small",  &scale, 0)) {
        reasixty_setFontScale(scale);
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (ImGui_RadioButtonEx(ctx, "Normal", &scale, 1)) {
        reasixty_setFontScale(scale);
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (ImGui_RadioButtonEx(ctx, "Large",  &scale, 2)) {
        reasixty_setFontScale(scale);
    }

    ImGui_Spacing(ctx);
    ImGui_Spacing(ctx);
    sectionHeader("Spelling");

    int sp = reasixty_uiSpelling();
    if (ImGui_RadioButtonEx(ctx, "British (Colour, Grey)",  &sp, 0)) {
        reasixty_setUiSpelling(sp);
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (ImGui_RadioButtonEx(ctx, "American (Color, Gray)", &sp, 1)) {
        reasixty_setUiSpelling(sp);
    }

    ImGui_Spacing(ctx);
    ImGui_Spacing(ctx);
    sectionHeader("Settings window");

    bool reopenLast = false;
    if (const char* v = GetExtState("rea_sixty", "settings_reopen_last_tab");
        v && *v) {
        reopenLast = std::atoi(v) != 0;
    }
    if (ImGui_Checkbox(ctx, "Reopen on last tab viewed", &reopenLast)) {
        SetExtState("rea_sixty", "settings_reopen_last_tab",
                    reopenLast ? "1" : "0", /*persist*/ true);
    }
}

void SettingsScreen::drawDevices(ImGui_Context* ctx)
{
    // Consistent, roomy section header: a gap above, the title, a rule,
    // and a little air below. Keeps the panes from feeling cramped — use
    // for every section after the first. Frank 2026-06-12.
    auto sectionHeader = [&](const char* title) {
        ImGui_Dummy(ctx, 0.0, 8.0);
        ImGui_Text(ctx, title);
        consumeSectionScroll_(ctx, title);
        ImGui_Separator(ctx);
        ImGui_Spacing(ctx);
    };

    ImGui_Text(ctx, "Connected devices");
    consumeSectionScroll_(ctx, "Connected devices");
    ImGui_Separator(ctx);
    ImGui_Spacing(ctx);

    char line[128];
    const bool uf8On = reasixty_uf8Connected();
    const bool uc1On = reasixty_uc1Connected();
    const bool uf1On = reasixty_uf1Connected();

    auto deviceLine = [&](const char* name, bool on, const char* serial) {
        if (on && serial && *serial) {
            snprintf(line, sizeof(line), "  %s   [connected]   SN %s",
                          name, serial);
        } else {
            snprintf(line, sizeof(line), "  %s   %s", name,
                          on ? "[connected]" : "[not connected]");
        }
        ImGui_Text(ctx, line);
    };

    deviceLine("UF8", uf8On, reasixty_uf8Serial());
    if (uf8On) {
        ImGui_SameLine(ctx, /*offset_from_start_x*/ nullptr, /*spacing*/ nullptr);
        if (ImGui_Button(ctx, "Identify##uf8",
                         /*size_w*/ nullptr, /*size_h*/ nullptr)) {
            reasixty_identifyUf8();
        }
    }

    deviceLine("UC1", uc1On, reasixty_uc1Serial());
    if (uc1On) {
        ImGui_SameLine(ctx, /*offset_from_start_x*/ nullptr, /*spacing*/ nullptr);
        if (ImGui_Button(ctx, "Identify##uc1",
                         /*size_w*/ nullptr, /*size_h*/ nullptr)) {
            reasixty_identifyUc1();
        }
    }

    deviceLine("UF1", uf1On, reasixty_uf1Serial());

    ImGui_Spacing(ctx);
    ImGui_Spacing(ctx);

    sectionHeader("Brightness");

    // 5 SSL-equivalent steps: dark / dim / half / bright / full. LED step
    // drives buttons + V-Pot rings + UC1 LEDs. Scribble step drives the
    // UF8 LCD strips, UC1 LCD, and UC1 status displays. Independent so a
    // user can crank the displays while keeping LEDs dim, or vice versa.
    static const char* kLevelNames[5] = {
        "Dark", "Dim", "Half", "Bright", "Full"
    };
    int led = reasixty_brightnessLevel();
    ImGui_Text(ctx, "  LEDs");
    ImGui_SetNextItemWidth(ctx, 200.0);
    if (ImGui_SliderInt(ctx, "##led_brightness", &led,
                        /*v_min*/ 0, /*v_max*/ 4,
                        /*format*/ nullptr, /*flags*/ nullptr)) {
        reasixty_setBrightnessLevel(led);
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    ImGui_Text(ctx, kLevelNames[led]);

    int scr = reasixty_scribbleBrightnessLevel();
    ImGui_Text(ctx, "  LCDs");
    ImGui_SetNextItemWidth(ctx, 200.0);
    if (ImGui_SliderInt(ctx, "##scribble_brightness", &scr,
                        /*v_min*/ 0, /*v_max*/ 4,
                        /*format*/ nullptr, /*flags*/ nullptr)) {
        reasixty_setScribbleBrightnessLevel(scr);
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    ImGui_Text(ctx, kLevelNames[scr]);

    ImGui_Spacing(ctx);
    ImGui_Spacing(ctx);
    sectionHeader("Metering");

    // GR meter source — when "Show any GR data" is on, the CS GR strip
    // (UF8) and the UC1 Comp meter fall back to ANY track FX exposing
    // the PreSonus GainReduction_dB convention if no SSL CS / mapped
    // CS plug-in is on the focused track. ReaComp / FabFilter etc. work
    // out of the box. Off limits the meter to SSL CS / mapped plug-ins.
    // BeginCombo + Selectable, never ImGui_Combo with \0-items — the
    // latter renders an invisible/empty control in ReaImGui v0.10, which
    // is exactly why this GR-source toggle was unreachable (and the Any-FX
    // poll stuck on its default, crashing on Acustica). Frank 2026-05-29.
    static const char* kGrLabels[2] = {
        "Only Show Channel Strip GR", "Show any GR Data"
    };
    int grIdx = reasixty_grAnyFx() ? 1 : 0;
    ImGui_Text(ctx, "GR meter source");
    ImGui_SameLine(ctx, nullptr, nullptr);
    ImGui_SetNextItemWidth(ctx, 220.0);
    if (ImGui_BeginCombo(ctx, "##gr_meter_source", kGrLabels[grIdx],
                         /*flags*/ nullptr)) {
        for (int i = 0; i < 2; ++i) {
            bool sel = (grIdx == i);
            if (ImGui_Selectable(ctx, kGrLabels[i], &sel,
                                 /*flags*/ nullptr,
                                 /*size_w*/ nullptr,
                                 /*size_h*/ nullptr)) {
                reasixty_setGrAnyFx(i == 1);
            }
        }
        ImGui_EndCombo(ctx);
    }

    // Combine GR across the channel — with "Show any GR Data", sum the
    // gain reduction of every compressor on the track (CS + ReaComp + …)
    // instead of showing a single source. In-series GR adds in dB, so the
    // meter reads the channel's total reduction. Separate per surface so
    // e.g. the UF8 strip can show combined while the UC1 shows the CS only.
    // No effect unless "Show any GR Data" is selected. Frank 2026-06-12.
    bool cUf8 = reasixty_grCombineUf8();
    if (ImGui_Checkbox(ctx, "Combine GR across plug-ins (UF8 strips)",
                       &cUf8)) {
        reasixty_setGrCombineUf8(cUf8);
    }
    bool cUc1 = reasixty_grCombineUc1();
    if (ImGui_Checkbox(ctx, "Combine GR across plug-ins (UC1 Comp)",
                       &cUc1)) {
        reasixty_setGrCombineUc1(cUc1);
    }

    // ── Metering ─────────────────────────────────────────────────────
    // Peak-meter fall rate, per meter. All meters are peak-hold; this sets how
    // fast each falls (dB/sec). 26.5 dB/s == REAPER's own meter decay, so the
    // UC1 input falls in lock-step with the output (Frank 2026-06-03).
    {
        int tblFlags = 0;
        if (ImGui_BeginTable(ctx, "##metering_tbl", 2, &tblFlags,
                             nullptr, nullptr, nullptr)) {
            int    wFlag = ImGui_TableColumnFlags_WidthFixed;
            double wName = scaleW_(ctx, 130.0);
            double wFall = scaleW_(ctx, 150.0);
            ImGui_TableSetupColumn(ctx, "n", &wFlag, &wName, nullptr);
            ImGui_TableSetupColumn(ctx, "f", &wFlag, &wFall, nullptr);

            auto fallRow = [&](const char* name, int mid) {
                ImGui_TableNextColumn(ctx);
                ImGui_Text(ctx, name);
                ImGui_TableNextColumn(ctx);
                ImGui_SetNextItemWidth(ctx, scaleW_(ctx, 140.0));
                double v = reasixty_meterFall(mid), st = 1.0, fa = 5.0; int fl = 0;
                char id[24]; snprintf(id, sizeof(id), "##mf%d", mid);
                if (ImGui_InputDouble(ctx, id, &v, &st, &fa, "%.1f dB/s", &fl))
                    reasixty_setMeterFall(mid, v);
            };

            fallRow("UC1 Input",  0);
            fallRow("UC1 Output", 1);
            fallRow("UF8 Strips", 2);
            ImGui_EndTable(ctx);
        }
    }

    if (ImGui_Button(ctx, "Copy Input to Output##meter",
                     /*size_w*/ nullptr, /*size_h*/ nullptr)) {
        reasixty_copyMeter(0, 1);
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    ImGui_Text(ctx, "match both UC1 meters");

    ImGui_Spacing(ctx);
    ImGui_Spacing(ctx);
    sectionHeader("V-Pot / encoder feel");

    bool shiftFine = reasixty_shiftFineMode();
    if (ImGui_Checkbox(ctx,
        "Shift activates Fine mode (V-Pots / encoders, not faders)",
        &shiftFine))
    {
        reasixty_setShiftFineMode(shiftFine);
    }

    // JSFX (esp. REAPER's own) can have sliders so long that even Fine mode
    // can't resolve a single value cleanly. With this on, Fine on a
    // continuous JSFX slider steps by the slider's OWN native increment
    // (1 detent = 1 step, fast flick accelerates) — the finest the plug-in
    // supports. Continuous JSFX + Fine only; VST3/AU and normal turns are
    // unaffected. Frank 2026-07-09.
    bool jsfxGrid = reasixty_jsfxGridFine();
    if (ImGui_Checkbox(ctx,
        "Fine mode steps JSFX sliders by their native increment",
        &jsfxGrid))
    {
        reasixty_setJsfxGridFine(jsfxGrid);
    }

    // V-Pot / encoder resolution. Speed scales every rotation delta (1.00 =
    // default feel, higher = faster / coarser). Fine factor is how much the
    // delta shrinks while Fine is engaged (lower = finer). Per-surface so
    // UF8 and UC1 can be tuned independently. Faders are unaffected.
    {
        // Plain typeable value field per setting — no slider, no +/- steps.
        // `dispScale` lets a field show different units than it stores (the
        // notch zone stores 0..0.05 normalised but shows 0..5 %). Values are
        // clamped to [lo,hi] (in stored units) on entry.
        auto field = [&](const char* label, double cur,
                         double lo, double hi, double dispScale,
                         const char* fmt, void (*setFn)(double)) {
            double disp = cur * dispScale;
            char iid[64];
            snprintf(iid, sizeof(iid), "##in_%s", label);
            ImGui_SetNextItemWidth(ctx, 90.0);
            if (ImGui_InputDouble(ctx, iid, &disp,
                                  nullptr, nullptr, fmt, nullptr)) {
                double v = disp / dispScale;
                if (v < lo) v = lo;
                if (v > hi) v = hi;
                setFn(v);
            }
            ImGui_SameLine(ctx, nullptr, nullptr);
            ImGui_Text(ctx, label);
        };
        field("UF8 V-Pot speed", reasixty_knobSpeedUf8(),
              0.01, 2.0, 1.0, "%.2fx", reasixty_setKnobSpeedUf8);
        field("UF8 Fine factor", reasixty_fineFactorUf8(),
              0.05, 0.50, 1.0, "%.2fx", reasixty_setFineFactorUf8);
        ImGui_Spacing(ctx);
        field("UC1 encoder speed", reasixty_knobSpeedUc1(),
              0.01, 2.0, 1.0, "%.2fx", reasixty_setKnobSpeedUc1);
        field("UC1 Fine factor", reasixty_fineFactorUc1(),
              0.05, 0.50, 1.0, "%.2fx", reasixty_setFineFactorUc1);
        ImGui_Spacing(ctx);
        // Virtual-notch zone: how close to the neutral point (0 dB / centre
        // pan) a V-Pot must land for the magnet to snap. 0 % disables the
        // slow-approach snap (crossing the centre still snaps). Stored as a
        // normalised 0..0.05 fraction, shown as 0..5 %.
        field("Virtual notch zone", reasixty_notchZone(),
              0.0, 0.05, 100.0, "%.1f%%", reasixty_setNotchZone);
        // Fine step near the notch: V-Pot moves this much slower while the
        // value sits within 2x the zone of centre, for precise 0 dB nudges.
        // 1.00x = off (normal speed everywhere).
        field("Notch fine step", reasixty_notchFineStep(),
              0.1, 1.0, 1.0, "%.2fx", reasixty_setNotchFineStep);
        // Soft-detent hold: the value parks on 0 dB and absorbs this much
        // rotation before releasing — stops an endless encoder sailing past
        // 0. 0% = off (pure magnet, can overshoot). Higher = stickier 0.
        field("Notch hold", reasixty_notchHold(),
              0.0, 0.10, 100.0, "%.1f%%", reasixty_setNotchHold);
    }

    // UC1 GR calibration sits at the very bottom of the Devices pane per
    // Frank 2026-05-20 — it's a niche hardware-trim workflow that doesn't
    // need to be above the common settings.
    ImGui_Spacing(ctx);
    ImGui_Spacing(ctx);
    sectionHeader("UC1 GR calibration");
    ImGui_TextDisabled(ctx,
        "Hardware-trim — per-tick offsets that nudge the UC1 to match its");
    ImGui_TextDisabled(ctx,
        "printed scale. Same workflow as SSL 360°'s BC VU calibration tool.");
    ImGui_TextDisabled(ctx,
        "Click \"Test\" on a row, then ± until the UC1 lines up with the");
    ImGui_TextDisabled(ctx,
        "marking. Auto-saved. Stop test to resume normal GR.");
    ImGui_Spacing(ctx);

    auto drawCalSection = [&](const char* title, int section) {
        ImGui_Text(ctx, title);
        const int n = reasixty_uc1CalCount(section);
        const int testActive = reasixty_uc1CalActiveTest();
        // Encoding: 0..5 = BC tick, 100..104 = CS tick. Per-section
        // active range so the radio reads cleanly per section.
        const int testBase = (section == 0) ? 0 : 100;

        // Inner table — fixed-width columns so every row aligns:
        // dB label | Test button | dB input | dB suffix. Frank 2026-05-22.
        char tblId[32];
        snprintf(tblId, sizeof(tblId), "##gr_cal_tbl_%d", section);
        int tblFlags = 0;
        if (ImGui_BeginTable(ctx, tblId, 4, &tblFlags,
                             nullptr, nullptr, nullptr)) {
            int   wFlag      = ImGui_TableColumnFlags_WidthFixed;
            double wTick     = scaleW_(ctx, 50.0);
            double wTest     = scaleW_(ctx, 60.0);
            double wInput    = scaleW_(ctx, 130.0);
            double wSuffix   = scaleW_(ctx, 30.0);
            ImGui_TableSetupColumn(ctx, "tick",  &wFlag, &wTick,   nullptr);
            ImGui_TableSetupColumn(ctx, "test",  &wFlag, &wTest,   nullptr);
            ImGui_TableSetupColumn(ctx, "input", &wFlag, &wInput,  nullptr);
            ImGui_TableSetupColumn(ctx, "suffix",&wFlag, &wSuffix, nullptr);

            for (int i = 0; i < n; ++i) {
                const double tickDb = reasixty_uc1CalTickDb(section, i);
                const double cur    = reasixty_uc1CalGet(section, i);
                const bool   active = (testActive == testBase + i);

                ImGui_TableNextColumn(ctx);
                char rowLbl[32];
                snprintf(rowLbl, sizeof(rowLbl), "%g dB", tickDb);
                ImGui_Text(ctx, rowLbl);

                ImGui_TableNextColumn(ctx);
                char testId[64];
                snprintf(testId, sizeof(testId),
                    "%s##cal_test_%d_%d",
                    active ? "Stop" : "Test", section, i);
                if (ImGui_Button(ctx, testId, nullptr, nullptr)) {
                    reasixty_uc1SetCalActiveTest(active ? -1 : (testBase + i));
                }

                ImGui_TableNextColumn(ctx);
                char inputId[64];
                snprintf(inputId, sizeof(inputId),
                    "##cal_in_%d_%d", section, i);
                double v = cur;
                double step = 0.1, fast = 1.0;
                int    flags = 0;
                ImGui_SetNextItemWidth(ctx, scaleW_(ctx, 110.0));
                if (ImGui_InputDouble(ctx, inputId, &v, &step, &fast,
                                      "%+.2f", &flags)) {
                    reasixty_uc1CalSet(section, i, v);
                    if (!active) reasixty_uc1SetCalActiveTest(testBase + i);
                }

                ImGui_TableNextColumn(ctx);
                ImGui_Text(ctx, "dB");
            }
            ImGui_EndTable(ctx);
        }
        ImGui_Spacing(ctx);
        char resetId[48];
        snprintf(resetId, sizeof(resetId),
            "Reset all##cal_reset_%d", section);
        if (ImGui_Button(ctx, resetId, nullptr, nullptr)) {
            reasixty_uc1CalResetSection(section);
        }
        const bool sectionTestActive =
            (section == 0  && testActive >= 0   && testActive < 6) ||
            (section == 1  && testActive >= 100 && testActive < 105);
        if (sectionTestActive) {
            ImGui_SameLine(ctx, nullptr, nullptr);
            char stopId[48];
            snprintf(stopId, sizeof(stopId),
                "Stop test##cal_stop_%d", section);
            if (ImGui_Button(ctx, stopId, nullptr, nullptr)) {
                reasixty_uc1SetCalActiveTest(-1);
            }
        }
        ImGui_Spacing(ctx);
    };

    // Both sections stacked vertically + left-aligned. Frank 2026-05-22:
    // the prior 2-column outer table pushed CS DYN GR way to the right.
    drawCalSection("BC VU meter (0/4/8/12/16/20 dB)", 0);
    ImGui_Spacing(ctx);
    drawCalSection("CS DYN GR LEDs (3/6/10/14/20 dB)", 1);
}

void SettingsScreen::drawBehaviour(ImGui_Context* ctx)
{
    // Consistent, roomy section header: a gap above, the title, a rule,
    // and a little air below. Keeps the panes from feeling cramped — use
    // for every section after the first. Frank 2026-06-12.
    auto sectionHeader = [&](const char* title) {
        ImGui_Dummy(ctx, 0.0, 8.0);
        ImGui_Text(ctx, title);
        consumeSectionScroll_(ctx, title);
        ImGui_Separator(ctx);
        ImGui_Spacing(ctx);
    };

    ImGui_Text(ctx, "Tracks");
    consumeSectionScroll_(ctx, "Tracks");
    ImGui_Separator(ctx);
    ImGui_Spacing(ctx);

    // TCP scrolls to keep the UF8-selected track visible. Independent of
    // the always-on MCP follow (those are separate REAPER scroll surfaces).
    // Frank 2026-05-20.
    bool tcpFollow = reasixty_tcpFollowsSelection();
    if (ImGui_Checkbox(ctx, "TCP follows UF8 selection", &tcpFollow)) {
        reasixty_setTcpFollowsSelection(tcpFollow);
    }

    // Visibility follow: the surface mirrors what's visible in either
    // REAPER's TCP (arrange-view) or MCP (mixer). TCP-mode also hides
    // children of fully-collapsed folders because REAPER's
    // "Hide children of collapsed folders" pref clears B_SHOWINTCP on
    // those children — no separate toggle needed. Frank 2026-05-22.
    int visFollow = reasixty_visibilityFollow();
    ImGui_Text(ctx, "Surface mirrors:");
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (ImGui_RadioButtonEx(ctx, "TCP", &visFollow, 0)) {
        reasixty_setVisibilityFollow(visFollow);
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (ImGui_RadioButtonEx(ctx, "MCP", &visFollow, 1)) {
        reasixty_setVisibilityFollow(visFollow);
    }
    ImGui_TextDisabled(ctx,
        "TCP hides children of collapsed folders when REAPER's "
        "'Hide children of collapsed folders' preference is on.");

    // Pinned-tracks behaviour. Only effective in TCP-mode (MCP has
    // no pin concept) — rebuildVisibleTrackList silently skips the
    // reorder when Surface mirrors MCP, so the checkbox is harmless
    // in that mode. Hint via TextDisabled instead of greying out.
    {
        bool pinSurvives = reasixty_pinnedSurvivesBanking();
        if (ImGui_Checkbox(ctx, "Pinned tracks survive banking", &pinSurvives)) {
            reasixty_setPinnedSurvivesBanking(pinSurvives);
        }
    }

    // Touch a UF8 fader → that strip's track becomes the only selected
    // track. Cheap tactile bank navigation: grab the fader and UC1
    // follows. Plain select; not subject to UF8 Plugin Mode's SEL-button
    // selVst3Param hijack (Frank 2026-05-19).
    bool tsc = reasixty_touchSelectsChannel();
    if (ImGui_Checkbox(ctx, "Touch selects channel", &tsc)) {
        reasixty_setTouchSelectsChannel(tsc);
    }

    // V-Pot / SC / BC parameter edits on a non-selected track auto-select
    // the manipulated track when on. Off → UC1 stays on the currently
    // selected track regardless of which strip was just edited.
    bool tsfp = reasixty_trackSelFollowsParam();
    if (ImGui_Checkbox(ctx, "Track selection follows parameter change",
                       &tsfp)) {
        reasixty_setTrackSelFollowsParam(tsfp);
    }

    // Master track — surface-side handling of the REAPER Master bus.
    ImGui_Spacing(ctx);
    ImGui_Spacing(ctx);
    sectionHeader("Master track");

    // CHANNEL encoder scrolls onto the Master track (IP_TRACKNUMBER 0)
    // before track 1, as a virtual "track 0". UC1-only — never appears on
    // the UF8 strips. A Bus Compressor on the Master shows in the BC
    // context independently of this toggle.
    bool showMaster = reasixty_uc1ShowMasterAsTrack0();
    if (ImGui_Checkbox(ctx, "Show Master as Track 0 on UC1", &showMaster)) {
        reasixty_setUc1ShowMasterAsTrack0(showMaster);
    }

    // "Pin Master to UF8 Strip 1/8" (bindable built-ins / REAPER actions)
    // behaviour. Replace: the pinned strip hides its banked track. Shift:
    // the regular tracks bank over the remaining 7 strips so none is hidden
    // (bank step + clamp drop to 7 while a pin is live). BeginCombo, never
    // ImGui_Combo with \0-items (renders empty in ReaImGui v0.10).
    static const char* kPinLabels[2] = { "Replace strip", "Shift banking" };
    int pinIdx = reasixty_masterPinShift() ? 1 : 0;
    ImGui_Text(ctx, "Pinned Master");
    ImGui_SameLine(ctx, nullptr, nullptr);
    ImGui_SetNextItemWidth(ctx, 220.0);
    if (ImGui_BeginCombo(ctx, "##pinned_master", kPinLabels[pinIdx],
                         /*flags*/ nullptr)) {
        for (int i = 0; i < 2; ++i) {
            bool sel = (pinIdx == i);
            if (ImGui_Selectable(ctx, kPinLabels[i], &sel,
                                 /*flags*/ nullptr,
                                 /*size_w*/ nullptr,
                                 /*size_h*/ nullptr)) {
                reasixty_setMasterPinShift(i == 1);
            }
        }
        ImGui_EndCombo(ctx);
    }

    ImGui_Spacing(ctx);
    ImGui_Spacing(ctx);
    sectionHeader("Plug-ins");

    // Offline FX are skipped by all four cycle paths (Channel-Encoder
    // FX/Instance Cycle, per-strip V-Pot FX/Instance Cycle). Frank 2026-05-20.
    bool hideOffline = reasixty_hideOfflineFx();
    if (ImGui_Checkbox(ctx, "Don't show offline FX", &hideOffline)) {
        reasixty_setHideOfflineFx(hideOffline);
    }

    // Wrap-around at the end of the FX chain. Default on (legacy
    // behaviour). When off, both ends hard-stop on all four cycle
    // paths and the UC1 carousel shows no neighbour name past the
    // first/last FX. Frank 2026-05-22.
    bool wrapCycle = reasixty_wrapPluginCycle();
    if (ImGui_Checkbox(ctx, "Wrap Plug-in Cycle", &wrapCycle)) {
        reasixty_setWrapPluginCycle(wrapCycle);
    }

    bool sff = reasixty_stripFollowsFocusedFx();
    if (ImGui_Checkbox(ctx, "SSL Strip Mode follows focused plug-in window",
                       &sff)) {
        reasixty_setStripFollowsFocusedFx(sff);
    }

    // When SSL Strip Mode (with GUI) or UF8 Plugin Mode (with GUI) has
    // a plug-in window open, Instance Cycle re-points the window at
    // the cycle's new target. Off → cycle moves the surface but
    // leaves the floating window pinned to its current FX.
    bool pgfi = reasixty_pluginGuiFollowsInstance();
    if (ImGui_Checkbox(ctx, "Plug-in GUI follows active Instance", &pgfi)) {
        reasixty_setPluginGuiFollowsInstance(pgfi);
    }

    // The UF1's PLUG-IN soft-key on a NATIVE SSL strip. An explicit UF1 map
    // chooses per key (FX-Learn → UF1 cell → Action); the built-in p188 pages
    // have no per-key storage, so they follow this.
    bool uskg = reasixty_uf1StripKeyWithGui();
    if (ImGui_Checkbox(ctx, "UF1 PLUG-IN key opens the plug-in GUI", &uskg)) {
        reasixty_setUf1StripKeyWithGui(uskg);
    }

    // (Favourite copy-mode, section mask, remember + per-project bank moved to
    // the dedicated Favourites tab. Frank 2026-06-26.)

    // Moved out of the former Modes → "Device" sub-tab on 2026-05-20.
    bool engageUf8 = reasixty_cycleEngagesUf8();
    if (ImGui_Checkbox(ctx,
        "Auto-engage UF8 Plug-in Mode for UF8-mapped plug-ins",
        &engageUf8))
    {
        reasixty_setCycleEngagesUf8(engageUf8);
    }

    // Pin plug-in GUI position: drag a plug-in window where you want it,
    // then click "Capture current". From then on, every managed
    // TrackFX_Show snaps the floating window to that x/y (size left
    // alone). -1 / -1 = no pin captured yet → checkbox does nothing
    // until a position is captured.
    bool pgpp = reasixty_pluginGuiPinPos();
    if (ImGui_Checkbox(ctx, "Pin plug-in GUI position", &pgpp)) {
        reasixty_setPluginGuiPinPos(pgpp);
    }
    {
        int px = -1, py = -1;
        reasixty_getPluginGuiPin(&px, &py);
        const bool centerMode = reasixty_pluginGuiPinCenter();
        char hint[96];
        if (centerMode) {
            snprintf(hint, sizeof(hint), "  Pin: center");
        } else if (px < 0 || py < 0) {
            snprintf(hint, sizeof(hint),
                "  Pin: (none captured yet)");
        } else {
            snprintf(hint, sizeof(hint),
                "  Pin: %d, %d", px, py);
        }
        ImGui_Text(ctx, hint);
        ImGui_SameLine(ctx, nullptr, nullptr);
        if (ImGui_Button(ctx, "Capture current",
                         /*size_w*/ nullptr, /*size_h*/ nullptr)) {
            reasixty_capturePluginGuiPin();
        }
        ImGui_SameLine(ctx, nullptr, nullptr);
        if (ImGui_Button(ctx, "Center on Screen",
                         /*size_w*/ nullptr, /*size_h*/ nullptr)) {
            reasixty_setPluginGuiPinCenter(true);
        }
    }

    // Pin FX-chain window position — parallel to the floating-window
    // pin above. Separate atomics because chain windows are typically
    // larger and live at a different captured position. Implementation
    // finds the chain HWND via NSApp.windows title-match (REAPER's chain
    // titles start with "FX:" on macOS).
    bool fxcp = reasixty_fxChainPinPos();
    if (ImGui_Checkbox(ctx, "Pin FX-chain GUI position", &fxcp)) {
        reasixty_setFxChainPinPos(fxcp);
    }
    {
        int cx = -1, cy = -1;
        reasixty_getFxChainPin(&cx, &cy);
        const bool chainCenter = reasixty_fxChainPinCenter();
        char hint[96];
        if (chainCenter) {
            snprintf(hint, sizeof(hint), "  Pin: center");
        } else if (cx < 0 || cy < 0) {
            snprintf(hint, sizeof(hint),
                "  Pin: (none captured yet)");
        } else {
            snprintf(hint, sizeof(hint),
                "  Pin: %d, %d", cx, cy);
        }
        ImGui_Text(ctx, hint);
        ImGui_SameLine(ctx, nullptr, nullptr);
        if (ImGui_Button(ctx, "Capture current##fx_chain_pin",
                         /*size_w*/ nullptr, /*size_h*/ nullptr)) {
            reasixty_captureFxChainPin();
        }
        ImGui_SameLine(ctx, nullptr, nullptr);
        if (ImGui_Button(ctx, "Center on Screen##fx_chain_pin",
                         /*size_w*/ nullptr, /*size_h*/ nullptr)) {
            reasixty_setFxChainPinCenter(true);
        }
    }

    ImGui_Spacing(ctx);
    ImGui_Spacing(ctx);
    sectionHeader("Soft-keys");

    // When on (default), a focused-parameter change switches the SSL soft-key
    // bank to the bank holding that param. Off = the soft-key bank stays put.
    // The UF8 parameter display follows the param either way.
    bool pssb = reasixty_paramSwitchesSoftKeyBank();
    if (ImGui_Checkbox(ctx, "Parameter change switches soft-key bank",
                       &pssb)) {
        reasixty_setParamSwitchesSoftKeyBank(pssb);
    }

    // Startup soft-key bank — engage a fixed user-Quick (Layer + Quick +
    // Sub-bank) on every fresh REAPER session instead of the plug-in-driven
    // default. "Use current hardware bank" captures whatever is engaged on
    // the UF8 right now. Layer-1 Q1/Q2 are excluded (hardcoded SSL CS/BC
    // focus, no user-Quick slots).
    {
        static const char* kSbLayer[3] = { "Layer 1", "Layer 2", "Layer 3" };
        static const char* kSbQuick[3] = { "Q1", "Q2", "Q3" };
        static const char* kSbSub[6]   = {
            "V-POT", "Soft 1", "Soft 2", "Soft 3", "Soft 4", "Soft 5" };

        int sbL = 1, sbQ = 0, sbS = 0;
        const bool sbOn = reasixty_startupBank(&sbL, &sbQ, &sbS);
        bool on = sbOn;
        if (ImGui_Checkbox(ctx, "Engage a fixed soft-key bank at startup",
                           &on)) {
            if (on) {
                // Seed from the live engaged bank; fall back to a valid
                // user-Quick when nothing usable is engaged.
                int lv = uf8::bindings::getActiveLayer();
                if (lv < 0 || lv > 2) lv = 1;
                int lq = reasixty_activeQuickFor(lv);
                int ls = reasixty_activeSubBankFor(lv);
                if (lq < 0) lq = (lv == 0) ? 2 : 0;
                if (lv == 0 && lq <= 1) lq = 2;
                if (ls < 0 || ls > 5) ls = 0;
                reasixty_setStartupBank(true, lv, lq, ls);
                sbL = lv; sbQ = lq; sbS = ls;
            } else {
                reasixty_setStartupBank(false, 0, 0, 0);
            }
        }
        if (on) {
            auto commit = [&]() {
                if (sbL == 0 && sbQ <= 1) sbQ = 2;   // L1 → only Q3 is user
                reasixty_setStartupBank(true, sbL, sbQ, sbS);
            };
            ImGui_Indent(ctx, /*indent_w*/ nullptr);
            ImGui_SetNextItemWidth(ctx, 110.0);
            if (ImGui_BeginCombo(ctx, "##sb_layer", kSbLayer[sbL], nullptr)) {
                for (int i = 0; i < 3; ++i) {
                    bool s = (sbL == i);
                    if (ImGui_Selectable(ctx, kSbLayer[i], &s,
                                         nullptr, nullptr, nullptr)) {
                        sbL = i; commit();
                    }
                }
                ImGui_EndCombo(ctx);
            }
            ImGui_SameLine(ctx, nullptr, nullptr);
            ImGui_SetNextItemWidth(ctx, 70.0);
            if (ImGui_BeginCombo(ctx, "##sb_quick", kSbQuick[sbQ], nullptr)) {
                for (int i = 0; i < 3; ++i) {
                    if (sbL == 0 && i <= 1) {
                        ImGui_TextDisabled(ctx, kSbQuick[i]);  // L1 CS/BC
                        continue;
                    }
                    bool s = (sbQ == i);
                    if (ImGui_Selectable(ctx, kSbQuick[i], &s,
                                         nullptr, nullptr, nullptr)) {
                        sbQ = i; commit();
                    }
                }
                ImGui_EndCombo(ctx);
            }
            ImGui_SameLine(ctx, nullptr, nullptr);
            ImGui_SetNextItemWidth(ctx, 110.0);
            if (ImGui_BeginCombo(ctx, "##sb_sub", kSbSub[sbS], nullptr)) {
                for (int i = 0; i < 6; ++i) {
                    bool s = (sbS == i);
                    if (ImGui_Selectable(ctx, kSbSub[i], &s,
                                         nullptr, nullptr, nullptr)) {
                        sbS = i; commit();
                    }
                }
                ImGui_EndCombo(ctx);
            }
            ImGui_SameLine(ctx, nullptr, nullptr);
            if (ImGui_Button(ctx, "Use current hardware bank",
                             /*size_w*/ nullptr, /*size_h*/ nullptr)) {
                int lv = uf8::bindings::getActiveLayer();
                if (lv < 0 || lv > 2) lv = 1;
                int lq = reasixty_activeQuickFor(lv);
                int ls = reasixty_activeSubBankFor(lv);
                if (lq < 0) lq = (lv == 0) ? 2 : 0;
                if (lv == 0 && lq <= 1) lq = 2;
                if (ls < 0 || ls > 5) ls = 0;
                sbL = lv; sbQ = lq; sbS = ls;
                reasixty_setStartupBank(true, sbL, sbQ, sbS);
            }
            ImGui_TextDisabled(ctx,
                "Ignored while UF8 Plugin Mode is on (it owns the soft-keys).");
            ImGui_Unindent(ctx, /*indent_w*/ nullptr);
        }
    }

    ImGui_Spacing(ctx);
    ImGui_Spacing(ctx);
    sectionHeader("Keyboard");

    // Moved out of the former Modes → "Device" sub-tab on 2026-05-20.
    bool altSnap = reasixty_altDragSnapBack();
    if (ImGui_Checkbox(ctx,
        "Alt/Option + fader drag → snap back to original on release",
        &altSnap))
    {
        reasixty_setAltDragSnapBack(altSnap);
    }

    // Host-OS keyboard modifier keys engage the matching slot (in addition
    // to any HW `mod_*` binding). Frank 2026-05-22.
    bool kbShift = reasixty_keyboardShiftModifier();
    if (ImGui_Checkbox(ctx,
        "Keyboard Shift acts as Shift modifier",
        &kbShift))
    {
        reasixty_setKeyboardShiftModifier(kbShift);
    }
    bool kbCmd = reasixty_keyboardCmdModifier();
    if (ImGui_Checkbox(ctx,
        "Keyboard Cmd (⌘) acts as Cmd modifier",
        &kbCmd))
    {
        reasixty_setKeyboardCmdModifier(kbCmd);
    }
    bool kbCtrl = reasixty_keyboardCtrlModifier();
    if (ImGui_Checkbox(ctx,
        "Keyboard Ctrl acts as Ctrl modifier",
        &kbCtrl))
    {
        reasixty_setKeyboardCtrlModifier(kbCtrl);
    }
}

} // namespace uf8
