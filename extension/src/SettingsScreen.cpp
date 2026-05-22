#include "SettingsScreen.h"

#include "commit_count.h"   // generated; defines REASIXTY_COMMIT_COUNT

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>

#include "Bindings.h"
#include "GrCalibration.h"
#include "ParameterGroups.h"
#include "PluginMap.h"
#include "Protocol.h"
#include "UserPluginCatalog.h"
#include "reaper_imgui_functions.h"
#include "reaper_plugin_functions.h"

// Forward declarations of accessors defined in main.cpp. Same pattern as
// reasixty_followSelectedInMixer / reasixty_toggleMixerWindow — keeps the
// anonymous-namespace globals owned by main.cpp while letting the UI read
// runtime state. Called only from the main thread (via onTimer → ImGui).
bool reasixty_uf8Connected();
bool reasixty_uc1Connected();
const char* reasixty_uf8Serial();
const char* reasixty_uc1Serial();
// REAPER Action picker — Settings → Bindings editor uses these to drive
// REAPER's PromptForAction window and to resolve stored action strings
// to their human-readable names. Implemented in main.cpp; the poll runs
// off onTimer so the picker keeps working even if the user navigates
// away from the editor while the picker is open.
void reasixty_actionPickerStart(int layer, uf8::bindings::ButtonId id,
                                bool longPress, int modIdx = 0,
                                int stepIdx = 0);
bool reasixty_actionPickerActiveFor(int layer, uf8::bindings::ButtonId id,
                                    bool longPress, int modIdx = 0,
                                    int stepIdx = 0);
// User-Quick destination variant — picked action is stored into
// userQuicks[L].quicks[Q].subBanks[SB].slots[s], not into a layer
// binding. Layer-mode and User-Quick-mode share the picker session;
// only one can be open at a time.
void reasixty_actionPickerStartUserQuick(int uqLayer, int uqQuick,
                                         int uqSubBank, int uqSlot,
                                         int modIdx = 0, int stepIdx = 0);
bool reasixty_actionPickerActiveForUserQuick(int uqLayer, int uqQuick,
                                             int uqSubBank, int uqSlot,
                                             int modIdx = 0, int stepIdx = 0);
void reasixty_actionPickerCancel();
std::string reasixty_resolveActionName(const std::string& action);
bool        reasixty_actionIsToggle(const std::string& action);
std::string reasixty_loadReaScript();
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
bool reasixty_autoHideReadTrim();
void reasixty_setAutoHideReadTrim(bool hide);
bool reasixty_autoFillFromRight();
void reasixty_setAutoFillFromRight(bool fromRight);
int  reasixty_cycleOpenMode();
void reasixty_setCycleOpenMode(int mode);
bool reasixty_cycleEngagesUf8();
void reasixty_setCycleEngagesUf8(bool on);
int  reasixty_cycleControlMask();
void reasixty_setCycleControlMask(int mask);
// Selection-Set exports — Phase 2.5b. All slot args are 1..8.
int         reasixty_selsetActive();
bool        reasixty_selsetGlobal(int slot);
void        reasixty_setSelsetGlobal(int slot, bool global);
int         reasixty_selsetType(int slot);                 // 0=Snapshot, 1=Group
void        reasixty_setSelsetType(int slot, int type);
const char* reasixty_selsetName(int slot);
void        reasixty_setSelsetName(int slot, const char* name);
int         reasixty_selsetGroupIdx(int slot);
void        reasixty_setSelsetGroupIdx(int slot, int groupIdx);
int         reasixty_selsetTrackCount(int slot);
void        reasixty_selsetSaveCurrent(int slot);
void        reasixty_selsetRecallToggle(int slot);
void        reasixty_selsetClear(int slot);
int         reasixty_selsetAutoMode();                          // global: -1 = off, 0..5 = REAPER mode
void        reasixty_setSelsetAutoMode(int mode);
bool reasixty_recRmeEnabled();
bool reasixty_recVpotRotateGain();
bool reasixty_recVpotShiftInputCh();
int  reasixty_recVpotPush();
int  reasixty_recCut();
int  reasixty_recSolo();
void reasixty_setRecRmeEnabled(bool on);
void reasixty_setRecVpotRotateGain(bool on);
void reasixty_setRecVpotShiftInputCh(bool on);
bool reasixty_altDragSnapBack();
void reasixty_setAltDragSnapBack(bool on);
bool reasixty_hideOfflineFx();
void reasixty_setHideOfflineFx(bool on);
bool reasixty_wrapPluginCycle();
void reasixty_setWrapPluginCycle(bool on);
bool reasixty_keyboardShiftModifier();
void reasixty_setKeyboardShiftModifier(bool on);
bool reasixty_keyboardCmdModifier();
void reasixty_setKeyboardCmdModifier(bool on);
bool reasixty_keyboardCtrlModifier();
void reasixty_setKeyboardCtrlModifier(bool on);
int  reasixty_theme();
void reasixty_setTheme(int t);
int  reasixty_fontScale();
void reasixty_setFontScale(int s);
bool reasixty_tcpFollowsSelection();
void reasixty_setTcpFollowsSelection(bool on);
bool reasixty_showTracksHiddenInTcp();
void reasixty_setShowTracksHiddenInTcp(bool on);
bool reasixty_showTracksHiddenInMcp();
void reasixty_setShowTracksHiddenInMcp(bool on);
bool reasixty_navAutoFollow();
void reasixty_setNavAutoFollow(bool follow);
int  reasixty_navDefaultView();
void reasixty_setNavDefaultView(int v);
int  reasixty_navRegionPress();
void reasixty_setNavRegionPress(int v);
extern "C" int  reasixty_navUc1Takeover();
extern "C" int  reasixty_navUc1Push();
extern "C" int  reasixty_navUc1PushShift();
extern "C" int  reasixty_navUc1LongPress();
void reasixty_setNavUc1Takeover(bool on);
void reasixty_setNavUc1Push(int v);
void reasixty_setNavUc1PushShift(int v);
void reasixty_setNavUc1LongPress(int v);
int  reasixty_navLowerRow();
bool reasixty_navPaginate();
int  reasixty_navColorBar();
void reasixty_setNavLowerRow(int v);
void reasixty_setNavPaginate(bool on);
void reasixty_setNavColorBar(int v);
void reasixty_setRecVpotPush(int v);
void reasixty_setRecCut(int v);
void reasixty_setRecSolo(int v);
bool reasixty_stripFollowsFocusedFx();
void reasixty_setStripFollowsFocusedFx(bool follow);
bool reasixty_pluginGuiFollowsInstance();
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
bool reasixty_folderMode();
void reasixty_setFolderMode(bool on);
bool reasixty_showOnlySelected();
void reasixty_setShowOnlySelected(bool on);
int  reasixty_ballisticMode();
void reasixty_setBallisticMode(int mode);
int  reasixty_trackNameMode();
void reasixty_setTrackNameMode(int mode);
void reasixty_exportDiagnostic();  // shows confirmation dialog itself
// Stock SSL plug-in soft-key labels for the read-only tabs.
// domain: 0 = ChannelStrip, 1 = BusComp.
const char* const* reasixty_softkeyStockLabels(int domain, int bank);
int                reasixty_softkeyStockBankCount();
int                reasixty_focusedDomain();
int                reasixty_activeUserBank();
int                reasixty_activeQuickFor(int layer);
int                reasixty_activeSubBankFor(int layer);
int                reasixty_engagedQuickFor(int layer);
const char*        reasixty_userBankSlotLabel(int bank, int slot);
// Currently active SSL soft-key PAGE bank — used by the schematic
// to highlight the matching V-POT/Bank tile and by the per-binding
// editor header to surface the live bank context.
int                reasixty_softkeyCurrentBank();
const char*        reasixty_softkeyCurrentBankName();
int                reasixty_softKeyBankRaw();
void               reasixty_setSoftKeyBank(int bank);
// Bindings save/load to user-chosen path. Both spawn a native file
// dialog and persist via uf8::bindings::exportLayerTo / importLayerFrom
// for the layer index passed in. Returns true on success, false on
// cancel or I/O error.
bool reasixty_exportLayerViaDialog(int layer);
std::string reasixty_fxLearnExportViaDialog(std::string* errOut);
bool reasixty_fxLearnImportViaDialog(std::string* errOut);
std::string reasixty_setupExportViaDialog(std::string* errOut);
bool reasixty_setupImportViaDialog(std::string* errOut);
bool reasixty_setupRestoreFactoryDefaults(std::string* errOut);
#ifdef _WIN32
bool reasixty_installWinUsbDriver(std::string* errOut);
#endif
#ifdef __linux__
bool reasixty_installLinuxUdevRule(std::string* errOut);
#endif
bool reasixty_importLayerViaDialog(int layer);
void reasixty_onActiveLayerChanged();
const char* reasixty_reaperVersion();
void reasixty_openUrl(const char* url);
void reasixty_revealInFinder(const char* path);

// UF8 Plugin Mode fader-bank bridge (defined in main.cpp).
// reasixty_uf8FaderBank() returns the clamped live hardware value;
// reasixty_setUf8FaderBank(fb) writes it (clamped). FX-Learn editor's
// Fader Bank 1/2 tab uses both for bidirectional sync.
int  reasixty_uf8FaderBank();
void reasixty_setUf8FaderBank(int fb);

namespace uf8 {

// Scale a design-time pixel width by the active font-size ratio so
// fixed-width input fields, combos, and table columns still fit their
// text at the user's chosen Font Size (Appearance tab). Reference size
// is the "Normal" preset (14 px) that all hardcoded widths were tuned
// for. Frank 2026-05-22.
static inline double scaleW_(ImGui_Context* ctx, double designWidth)
{
    constexpr double kRefSize = 14.0;
    const double fs = ImGui_GetFontSize(ctx);
    if (fs <= 0) return designWidth;
    return designWidth * (fs / kRefSize);
}

// ---- Device ---------------------------------------------------------------
// Per docs/plan-settings-ui.md §"Tab: Device" + SSL HOME equivalent (see
// docs/ssl-360-settings-inventory.md):
//   - Connected devices list with USB status dots (UF8 #N, UC1 #N, …)
//     + serial number, drag-to-reorder when ≥2 UF8s
//   - Identify Unit button — overrides target's LCD with a "THIS UNIT"
//     marker for ~2 s. Reuses the existing UF8/UC1 frame protocol.
//   - LED brightness slider (writes the global-brightness frame)
//   - Scribble brightness slider
//   - Meter ballistic selector (PPM / VU / RMS) — UI-only until Phase 1
//     meter forwarding is implemented
//   - SEL-follows-track-color toggle (needs SEL-color frame capture)
//   - Export Diagnostic Report button — produces
//     ~/Desktop/rea_sixty_diag_<date>.zip with build hash, REAPER version,
//     recent extension log, USB device tree.
// Appearance — theme + font-size pickers. Frank 2026-05-22.
void SettingsScreen::drawAppearance(ImGui_Context* ctx)
{
    ImGui_Text(ctx, "Theme");
    ImGui_Separator(ctx);

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
    ImGui_Text(ctx,
        "  Vanilla = the original Rea-Sixty dark blue. Dark = Indigo "
        "accent on");
    ImGui_Text(ctx,
        "  neutral dark (formerly MixnoteStyle). Light = high-contrast "
        "light mode.");

    ImGui_Spacing(ctx);
    ImGui_Spacing(ctx);
    ImGui_Text(ctx, "Font Size");
    ImGui_Separator(ctx);

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
    ImGui_Text(ctx,
        "  Changes apply on the next frame. If a long form looks cramped at "
        "Large,");
    ImGui_Text(ctx,
        "  resize the window — ReaImGui doesn't reflow widget rects until the "
        "next layout pass.");
}

void SettingsScreen::drawDevice(ImGui_Context* ctx)
{
    ImGui_Text(ctx, "Connected devices");
    ImGui_Separator(ctx);

    char line[128];
    const bool uf8On = reasixty_uf8Connected();
    const bool uc1On = reasixty_uc1Connected();

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

    ImGui_Spacing(ctx);
    ImGui_Spacing(ctx);

    ImGui_Text(ctx, "Brightness");
    ImGui_Separator(ctx);

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
    ImGui_Text(ctx, "Display behaviour");
    ImGui_Separator(ctx);

    bool selFollow = reasixty_selFollowsColor();
    if (ImGui_Checkbox(ctx, "SEL LED follows REAPER track colour",
                       &selFollow)) {
        reasixty_setSelFollowsColor(selFollow);
    }

    // GR meter source — when "Show any GR data" is on, the CS GR strip
    // (UF8) and the UC1 Comp meter fall back to ANY track FX exposing
    // the PreSonus GainReduction_dB convention if no SSL CS / mapped
    // CS plug-in is on the focused track. ReaComp / FabFilter etc. work
    // out of the box. Off limits the meter to SSL CS / mapped plug-ins.
    static char kGrItems[] =
        "Only Show Channel Strip GR\0Show any GR Data\0";
    int grIdx = reasixty_grAnyFx() ? 1 : 0;
    if (ImGui_Combo(ctx, "GR meter source", &grIdx,
                    kGrItems,
                    /*popup_max_height_in_items*/ nullptr)) {
        reasixty_setGrAnyFx(grIdx == 1);
    }

    // V-Pot / SC / BC parameter edits on a non-selected track auto-select
    // the manipulated track when on. Off → UC1 stays on the currently
    // selected track regardless of which strip was just edited.
    bool tsfp = reasixty_trackSelFollowsParam();
    if (ImGui_Checkbox(ctx, "Track selection follows parameter change",
                       &tsfp)) {
        reasixty_setTrackSelFollowsParam(tsfp);
    }

    // Touch a UF8 fader → that strip's track becomes the only selected
    // track. Cheap tactile bank navigation: grab the fader and UC1
    // follows. Plain select; not subject to UF8 Plugin Mode's SEL-button
    // selVst3Param hijack (Frank 2026-05-19).
    bool tsc = reasixty_touchSelectsChannel();
    if (ImGui_Checkbox(ctx, "Touch selects channel", &tsc)) {
        reasixty_setTouchSelectsChannel(tsc);
    }

    bool sff = reasixty_stripFollowsFocusedFx();
    if (ImGui_Checkbox(ctx, "SSL Strip Mode follows focused plugin window",
                       &sff)) {
        reasixty_setStripFollowsFocusedFx(sff);
    }

    // When SSL Strip Mode (with GUI) or UF8 Plugin Mode (with GUI) has
    // a plug-in window open, Instance Cycle re-points the window at
    // the cycle's new target. Off → cycle moves the surface but
    // leaves the floating window pinned to its current FX.
    bool pgfi = reasixty_pluginGuiFollowsInstance();
    if (ImGui_Checkbox(ctx, "Plugin GUI follows active Instance", &pgfi)) {
        reasixty_setPluginGuiFollowsInstance(pgfi);
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

    // Ballistic dropdown. Combo's `items` arg is a NUL-separated list
    // followed by a final NUL terminator — the string literal already
    // ends with one implicit \0, so "Peak\0VU\0RMS\0" is the proper
    // double-terminated form.
    static char kBallisticItems[] = "Peak\0VU\0RMS\0";
    int ballistic = reasixty_ballisticMode();
    if (ImGui_Combo(ctx, "Meter ballistic", &ballistic,
                    kBallisticItems,
                    /*popup_max_height_in_items*/ nullptr)) {
        reasixty_setBallisticMode(ballistic);
    }

    ImGui_Spacing(ctx);
    ImGui_Spacing(ctx);
    ImGui_Text(ctx, "Tracks");
    ImGui_Separator(ctx);

    // TCP scrolls to keep the UF8-selected track visible. Independent of
    // the always-on MCP follow (those are separate REAPER scroll surfaces).
    // Frank 2026-05-20.
    bool tcpFollow = reasixty_tcpFollowsSelection();
    if (ImGui_Checkbox(ctx, "TCP follows UF8 selection", &tcpFollow)) {
        reasixty_setTcpFollowsSelection(tcpFollow);
    }

    // Show tracks REAPER has hidden in TCP / MCP. Default both OFF — the
    // UF8 mirrors REAPER's track-panel visibility. Either toggle ON keeps
    // tracks on the surface even when that view hides them. Frank 2026-05-20.
    bool showTcpHidden = reasixty_showTracksHiddenInTcp();
    if (ImGui_Checkbox(ctx, "Show tracks hidden in TCP", &showTcpHidden)) {
        reasixty_setShowTracksHiddenInTcp(showTcpHidden);
    }
    bool showMcpHidden = reasixty_showTracksHiddenInMcp();
    if (ImGui_Checkbox(ctx, "Show tracks hidden in MCP", &showMcpHidden)) {
        reasixty_setShowTracksHiddenInMcp(showMcpHidden);
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
        ImGui_SetNextItemWidth(ctx, 220.0);
        if (ImGui_BeginCombo(ctx, "Long track-name handling",
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
    ImGui_Text(ctx, "Plug-ins");
    ImGui_Separator(ctx);

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
    if (ImGui_Checkbox(ctx, "Wrap Plugin Cycle", &wrapCycle)) {
        reasixty_setWrapPluginCycle(wrapCycle);
    }

    // Moved out of the former Modes → "Device" sub-tab on 2026-05-20.
    bool engageUf8 = reasixty_cycleEngagesUf8();
    if (ImGui_Checkbox(ctx,
        "Auto-engage UF8 Plugin Mode for UF8-mapped plug-ins",
        &engageUf8))
    {
        reasixty_setCycleEngagesUf8(engageUf8);
    }
    ImGui_Text(ctx,
        "  When the SEL-Mode cycle V-Pot push OR any \"Toggle focused "
        "plug-in GUI\"");
    ImGui_Text(ctx,
        "  binding (UC1 Encoder 2 push, etc.) lands on a UF8-mapped "
        "plug-in, also");
    ImGui_Text(ctx,
        "  engage UF8 Plugin Mode (with GUI). Press the same button "
        "again to exit.");

    ImGui_Spacing(ctx);
    ImGui_Spacing(ctx);
    ImGui_Text(ctx, "Keyboard Options");
    ImGui_Separator(ctx);

    // Moved out of the former Modes → "Device" sub-tab on 2026-05-20.
    bool altSnap = reasixty_altDragSnapBack();
    if (ImGui_Checkbox(ctx,
        "Alt/Option + fader drag → snap back to original on release",
        &altSnap))
    {
        reasixty_setAltDragSnapBack(altSnap);
    }
    ImGui_Text(ctx,
        "  Release while Alt/Option is still held → value snaps back to "
        "touch-on position.");

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
    ImGui_Text(ctx,
        "  Holding a keyboard modifier engages the matching slot the same "
        "as a HW");
    ImGui_Text(ctx,
        "  mod_shift / mod_cmd / mod_ctrl binding would. Both sources work "
        "in parallel.");
    ImGui_Text(ctx,
        "  Note: Cmd has no Windows keyboard source (Windows key is "
        "OS-reserved).");

    ImGui_Spacing(ctx);
    ImGui_Spacing(ctx);
    ImGui_Text(ctx, "Pending");
    ImGui_Separator(ctx);
    ImGui_Text(ctx, "  Drag-to-reorder for multi-UF8 setups: deferred");
    ImGui_Text(ctx, "  (codebase has no multi-UF8 support yet — single-device assumption");
    ImGui_Text(ctx, "  in the bank-shift / colour-sync / VU-meter paths).");

    // UC1 GR calibration sits at the very bottom of the Device pane per
    // Frank 2026-05-20 — it's a niche hardware-trim workflow that doesn't
    // need to be above the common settings.
    ImGui_Spacing(ctx);
    ImGui_Spacing(ctx);
    ImGui_Text(ctx, "UC1 GR calibration");
    ImGui_Separator(ctx);
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
        for (int i = 0; i < n; ++i) {
            const double tickDb = reasixty_uc1CalTickDb(section, i);
            const double cur    = reasixty_uc1CalGet(section, i);
            const bool   active = (testActive == testBase + i);

            char rowLbl[32];
            snprintf(rowLbl, sizeof(rowLbl), "  %4.0f dB", tickDb);
            ImGui_Text(ctx, rowLbl);
            ImGui_SameLine(ctx, nullptr, nullptr);

            char testId[64];
            snprintf(testId, sizeof(testId),
                "%s##cal_test_%d_%d",
                active ? "Stop" : "Test", section, i);
            if (ImGui_Button(ctx, testId, nullptr, nullptr)) {
                reasixty_uc1SetCalActiveTest(active ? -1 : (testBase + i));
            }
            ImGui_SameLine(ctx, nullptr, nullptr);

            char inputId[64];
            snprintf(inputId, sizeof(inputId),
                "dB##cal_in_%d_%d", section, i);
            double v = cur;
            double step = 0.1, fast = 1.0;
            int    flags = 0;
            // 110 base — wide enough for "+0.00" plus modest padding at
            // the Appearance Normal preset (14 px). The +/- spinner
            // buttons sit outside the SetNextItemWidth budget, so this
            // only sizes the text-input portion. Frank 2026-05-22.
            double w = scaleW_(ctx, 110.0);
            ImGui_SetNextItemWidth(ctx, w);
            if (ImGui_InputDouble(ctx, inputId, &v, &step, &fast,
                                  "%+.2f", &flags)) {
                reasixty_uc1CalSet(section, i, v);
                // Auto-activate the row's test mode on the first edit
                // so the user sees the change immediately.
                if (!active) reasixty_uc1SetCalActiveTest(testBase + i);
            }
        }
        ImGui_Spacing(ctx);
        char resetId[48];
        snprintf(resetId, sizeof(resetId),
            "Reset all##cal_reset_%d", section);
        if (ImGui_Button(ctx, resetId, nullptr, nullptr)) {
            reasixty_uc1CalResetSection(section);
        }
        ImGui_SameLine(ctx, nullptr, nullptr);
        char stopId[48];
        snprintf(stopId, sizeof(stopId),
            "Stop test##cal_stop_%d", section);
        const bool sectionTestActive =
            (section == 0  && testActive >= 0   && testActive < 6) ||
            (section == 1  && testActive >= 100 && testActive < 105);
        if (sectionTestActive) {
            if (ImGui_Button(ctx, stopId, nullptr, nullptr)) {
                reasixty_uc1SetCalActiveTest(-1);
            }
        } else {
            ImGui_TextDisabled(ctx, "  (no test active)");
        }
        ImGui_Spacing(ctx);
    };

    if (ImGui_BeginTable(ctx, "##gr_cal_cols", 2,
                         /*flags*/ nullptr, /*outer_size_w*/ nullptr,
                         /*outer_size_h*/ nullptr, /*inner_width*/ nullptr)) {
        ImGui_TableNextColumn(ctx);
        drawCalSection("BC VU meter (0/4/8/12/16/20 dB)", 0);
        ImGui_TableNextColumn(ctx);
        drawCalSection("CS DYN GR LEDs (3/6/10/14/20 dB)", 1);
        ImGui_EndTable(ctx);
    }
}

// ---- Bindings -------------------------------------------------------------
// Consolidates docs/bindings.md §"Config UI Sketch (ReaImGui)" + §"Binding
// Types" + §"Builtin Action Catalogue (v1)", augmented with SSL-equivalent
// rows from docs/ssl-360-settings-inventory.md.
//
// Persistence: ~/Library/Application Support/REAPER/rea_sixty/bindings.json
// Sections (single scroll):
//   - Per-strip buttons       (Select / Mute / Solo / Rec / V-Pot press)
//   - Transport               (Play / Stop / Rec / RW / FF — REAPER actions)
//   - Global buttons          (Bank L/R, Channel L/R, Flip, Layer cycle)
//   - 3 Quick Keys            (UF8 left-side QUICK row)
//   - 2 Foot-switches         (UF8 back-panel TRS jacks, ids 0x00 / 0x01)
//   - Layer-scoped soft-keys  (per active layer)
//   - Learn button (top right)
namespace {

using uf8::bindings::ButtonId;

// Short button-face label shown over the schematic (≈ what's printed on
// the physical UF8 silk-screen). Used by the editor header too.
const char* hwFaceLabel(ButtonId id)
{
    switch (id) {
        case ButtonId::BankLeft:    return "BANK \xE2\x97\x82";
        case ButtonId::BankRight:   return "BANK \xE2\x96\xB8";
        case ButtonId::PageLeft:    return "PAGE \xE2\x97\x82";
        case ButtonId::PageRight:   return "PAGE \xE2\x96\xB8";
        case ButtonId::Layer1:      return "1";
        case ButtonId::Layer2:      return "2";
        case ButtonId::Layer3:      return "3";
        case ButtonId::Quick1:      return "1";
        case ButtonId::Quick2:      return "2";
        case ButtonId::Quick3:      return "3";
        case ButtonId::PluginBtn:   return "PLUGIN";
        case ButtonId::Flip:        return "FLIP";
        case ButtonId::Pan:         return "PAN";
        case ButtonId::Fine:        return "FINE";
        case ButtonId::Btn360:      return "360\xC2\xB0";
        case ButtonId::AutoOff:     return "OFF";
        case ButtonId::AutoRead:    return "READ";
        case ButtonId::AutoWrite:   return "WRITE";
        case ButtonId::AutoTrim:    return "TRIM";
        case ButtonId::AutoLatch:   return "LATCH";
        case ButtonId::AutoTouch:   return "TOUCH";
        case ButtonId::ZoomUp:      return "\xE2\x96\xB2";
        case ButtonId::ZoomDown:    return "\xE2\x96\xBC";
        case ButtonId::ZoomLeft:    return "\xE2\x97\x82";
        case ButtonId::ZoomRight:   return "\xE2\x96\xB8";
        case ButtonId::ZoomCenter:  return "\xE2\x97\x8F";
        case ButtonId::Nav:         return "NAV";
        case ButtonId::Nudge:       return "NUDGE";
        case ButtonId::EncFocus:    return "FOCUS";
        case ButtonId::ChannelPush: return "ENC PUSH";
        case ButtonId::Channel:     return "CHANNEL";
        case ButtonId::SendPlugin1: return "S/P 1";
        case ButtonId::SendPlugin2: return "S/P 2";
        case ButtonId::SendPlugin3: return "S/P 3";
        case ButtonId::SendPlugin4: return "S/P 4";
        case ButtonId::SendPlugin5: return "S/P 5";
        case ButtonId::SendPlugin6: return "S/P 6";
        case ButtonId::SendPlugin7: return "S/P 7";
        case ButtonId::SendPlugin8: return "S/P 8";
        case ButtonId::TopSoftKey1: return "Soft-Key 1";
        case ButtonId::TopSoftKey2: return "Soft-Key 2";
        case ButtonId::TopSoftKey3: return "Soft-Key 3";
        case ButtonId::TopSoftKey4: return "Soft-Key 4";
        case ButtonId::TopSoftKey5: return "Soft-Key 5";
        case ButtonId::TopSoftKey6: return "Soft-Key 6";
        case ButtonId::TopSoftKey7: return "Soft-Key 7";
        case ButtonId::TopSoftKey8: return "Soft-Key 8";
        case ButtonId::VPotBank:     return "V-POT";
        case ButtonId::SoftKey1Bank: return "BANK 1";
        case ButtonId::SoftKey2Bank: return "BANK 2";
        case ButtonId::SoftKey3Bank: return "BANK 3";
        case ButtonId::SoftKey4Bank: return "BANK 4";
        case ButtonId::SoftKey5Bank: return "BANK 5";
        case ButtonId::Foot1:       return "FOOT 1";
        case ButtonId::Foot2:       return "FOOT 2";
        case ButtonId::SelectionNorm: return "NORM";
        case ButtonId::SelectionRec:  return "REC";
        case ButtonId::SelectionAuto: return "AUTO";
        case ButtonId::ChannelEncoder: return "Channel Encoder";
        case ButtonId::Uc1Encoder2:    return "UC1 Encoder 2";
        case ButtonId::Uc1Encoder2Push: return "UC1 Encoder 2 Push";
        default:                     return uf8::bindings::toName(id);
    }
}

// Convenience for ImGui_SameLine with default args.
void sameLine(ImGui_Context* ctx)
{
    ImGui_SameLine(ctx, /*offset_from_start_x*/ nullptr, /*spacing*/ nullptr);
}

// Vector-graphics canvas for the UF8 schematic — drawn into the
// surrounding ImGui window's draw list. Coordinates are in the
// schematic's local 0..W × 0..H space; ox/oy translate to screen.
struct VCanvas {
    ImGui_Context*  ctx;
    ImGui_DrawList* dl;
    float           ox, oy;
};

void drawText_(VCanvas& c, float x, float y, uint32_t col, const char* text)
{
    ImGui_DrawList_AddText(c.dl, c.ox + x, c.oy + y, col, text);
}

void drawTextCentered_(VCanvas& c, float cx, float cy, uint32_t col,
                       const char* text)
{
    double tw = 0, th = 0;
    ImGui_CalcTextSize(c.ctx, text, &tw, &th, /*hide_after_##*/ nullptr,
                       /*wrap_width*/ nullptr);
    drawText_(c, cx - float(tw) / 2.0f, cy - float(th) / 2.0f, col, text);
}

void rect_(VCanvas& c, float x, float y, float w, float h,
           uint32_t fill, uint32_t border, double rounding = 3.0)
{
    const float x1 = c.ox + x, y1 = c.oy + y;
    const float x2 = x1 + w,   y2 = y1 + h;
    if (fill) {
        ImGui_DrawList_AddRectFilled(c.dl, x1, y1, x2, y2, fill,
                                     &rounding, /*flags*/ nullptr);
    }
    if (border) {
        ImGui_DrawList_AddRect(c.dl, x1, y1, x2, y2, border,
                               &rounding, /*flags*/ nullptr,
                               /*thickness*/ nullptr);
    }
}

void circle_(VCanvas& c, float cx, float cy, float r,
             uint32_t fill, uint32_t border)
{
    if (fill) {
        ImGui_DrawList_AddCircleFilled(c.dl, c.ox + cx, c.oy + cy, r,
                                       fill, /*num_segments*/ nullptr);
    }
    if (border) {
        ImGui_DrawList_AddCircle(c.dl, c.ox + cx, c.oy + cy, r,
                                 border, /*num_segments*/ nullptr,
                                 /*thickness*/ nullptr);
    }
}

void line_(VCanvas& c, float x1, float y1, float x2, float y2,
           uint32_t col, double thickness = 1.0)
{
    ImGui_DrawList_AddLine(c.dl, c.ox + x1, c.oy + y1,
                           c.ox + x2, c.oy + y2, col, &thickness);
}

// Right-click "Copy / Paste binding" clipboard, shared between the UF8
// and UC1 schematic tabs. Right-clicking a binding tile captures the
// Binding into s_bindingClipboard; pasting writes it to whichever
// tile was right-clicked next. Frank 2026-05-15. The Binding struct
// holds behavior, label, both LED state pairs, the 4×2 modifier
// action matrix (with extraSteps + per-slot LedOverride) and the
// ledShowWhenEmpty toggle — a full deep copy.
uf8::bindings::Binding  s_bindingClipboard;
bool                    s_bindingClipboardFull = false;
uf8::bindings::ButtonId s_bindingCtxBtn        = uf8::bindings::ButtonId::None;
bool                    s_bindingCtxOpenRequested = false;

// Helper invoked at the bottom of each Bindings schematic (drawUf8Vector
// and drawUc1BindingsVector). Opens the context menu when a right-click
// landed on a hardware button this frame and renders the Copy / Paste
// items. Layer is the editor's active layer (= uf8::bindings::
// getActiveLayer()); keeping the popup tied to the canvas's ID stack
// avoids the cross-tab ID mismatch that would happen if OpenPopup was
// inside a tab but BeginPopup outside.
void renderBindingContextMenu_(ImGui_Context* ctx, int layer)
{
    if (s_bindingCtxOpenRequested) {
        ImGui_OpenPopup(ctx, "##binding_ctx_menu", nullptr);
        s_bindingCtxOpenRequested = false;
    }
    if (!ImGui_BeginPopup(ctx, "##binding_ctx_menu", nullptr)) return;

    if (ImGui_MenuItem(ctx, "Copy binding",  nullptr, nullptr, nullptr)) {
        s_bindingClipboard = uf8::bindings::getBinding(
            layer, s_bindingCtxBtn);
        s_bindingClipboardFull = true;
    }
    bool pasteEnabled = s_bindingClipboardFull
                     && s_bindingCtxBtn != uf8::bindings::ButtonId::None;
    if (ImGui_MenuItem(ctx, "Paste binding", nullptr, nullptr,
                       &pasteEnabled))
    {
        uf8::bindings::setBinding(
            layer, s_bindingCtxBtn, s_bindingClipboard);
    }
    ImGui_EndPopup(ctx);
}

// Render the full UF8 schematic. Click hit-test goes against the
// canvas-wide InvisibleButton; per-rect hits are computed by comparing
// the cached mouse-coords against each button's local rectangle.
// `sel` is updated on click. Layout follows SSL UF8 User Guide page 14
// (numbered controls).
void drawUf8Vector(ImGui_Context* ctx, ButtonId& sel)
{
    constexpr float W = 1000, H = 490;

    // Lock the schematic font to Small (12 px) so the labels stay
    // readable regardless of the global font-size picker. ReaImGui
    // v0.10 accepts font=nil meaning "keep current font, override
    // size only". Popped at function exit. Frank 2026-05-22.
    ImGui_PushFont(ctx, /*font*/ nullptr, 12.0);

    double oxd = 0, oyd = 0;
    ImGui_GetCursorScreenPos(ctx, &oxd, &oyd);

    // Reserve canvas layout space + provide a single hit target for
    // click capture. Per-button hit-testing happens manually against
    // the cached mouse position below.
    ImGui_InvisibleButton(ctx, "uf8_canvas", W, H, /*flags*/ nullptr);
    const bool canvasHovered = ImGui_IsItemHovered(ctx, /*flags*/ nullptr);
    int leftBtn = 0;
    const bool canvasClicked = ImGui_IsItemClicked(ctx, &leftBtn);
    // Right-click → "Copy / Paste binding" context menu (Frank 2026-05-15).
    // ImGui_IsItemClicked takes the mouse-button index as input; passing
    // 1 (= ImGui_MouseButton_Right) reads a fresh right-click on the
    // same canvas item.
    int rightBtn = 1;
    const bool canvasRightClicked = ImGui_IsItemClicked(ctx, &rightBtn);

    double mxd = 0, myd = 0;
    ImGui_GetMousePos(ctx, &mxd, &myd);

    VCanvas c {
        ctx, ImGui_GetWindowDrawList(ctx),
        static_cast<float>(oxd), static_cast<float>(oyd)
    };
    const float mx = static_cast<float>(mxd) - c.ox;
    const float my = static_cast<float>(myd) - c.oy;

    auto inside = [&](float x, float y, float w, float h) {
        return canvasHovered
            && mx >= x && mx <= x + w
            && my >= y && my <= y + h;
    };

    // Bindable button: hit-tests against the canvas mouse, draws a
    // hardware-face rectangle, highlights on hover/select. Returns
    // true when this tile was clicked this frame so the caller can
    // optionally fire the binding's action (used by the V-POT/Bank
    // tiles so clicking them in the schematic actually switches the
    // SSL PAGE bank — a hardware proxy for that one row).
    auto drawHwBtn = [&](float x, float y, float w, float h,
                         ButtonId id, const char* label) -> bool
    {
        const bool hot      = inside(x, y, w, h);
        const bool selected = (id == sel);
        const bool clicked  = hot && canvasClicked && leftBtn == 0;
        if (clicked) sel = id;
        // Right-click on a tile → arm the Copy / Paste context menu
        // for this ButtonId. Renderer (renderBindingContextMenu_) runs
        // at the bottom of this canvas function and consumes the flag.
        if (hot && canvasRightClicked && id != ButtonId::None) {
            s_bindingCtxBtn           = id;
            s_bindingCtxOpenRequested = true;
        }

        const uint32_t fill   = selected ? 0x4477CCFF
                                : hot     ? 0x3A4253FF
                                          : 0x252A33FF;
        const uint32_t border = selected ? 0xAACCFFFF : 0x4A5060FF;
        const uint32_t txt    = selected ? 0xFFFFFFFF : 0xD0D4DAFF;
        rect_(c, x, y, w, h, fill, border, /*rounding*/ 3.5);
        drawTextCentered_(c, x + w / 2.0f, y + h / 2.0f, txt, label);
        return clicked;
    };

    // Locked (non-bindable in v1) button — flatter colour, no hover.
    auto drawLocked = [&](float x, float y, float w, float h,
                          const char* label)
    {
        rect_(c, x, y, w, h, 0x1A1E24FF, 0x383C44FF, 3.0);
        drawTextCentered_(c, x + w / 2.0f, y + h / 2.0f, 0x70747CFF, label);
    };

    // Group label — small all-caps text painted on the chassis above
    // a related cluster of controls (mirrors the SSL silk-screen).
    auto drawGroupLabel = [&](float x, float y, const char* text) {
        drawText_(c, x, y, 0x9CA0AAFF, text);
    };

    // ---- Chassis ----
    rect_(c, 4, 4, W - 8, H - 8, 0x14181EFF, 0x2A3038FF, /*rounding*/ 8.0);

    // ---- Centre: 8 strips ----
    constexpr float kStripX0 = 138, kStripW = 80, kStripGap = 7;
    constexpr ButtonId kStripTsk[8] = {
        ButtonId::TopSoftKey1, ButtonId::TopSoftKey2,
        ButtonId::TopSoftKey3, ButtonId::TopSoftKey4,
        ButtonId::TopSoftKey5, ButtonId::TopSoftKey6,
        ButtonId::TopSoftKey7, ButtonId::TopSoftKey8,
    };
    // Live SSL labels for the active PAGE bank — the top-soft-key
    // scribble area shows whatever the hardware would show right now.
    // Domain follows the focused-param domain (Q1 → CS, Q2 → BC, plus
    // anything else that calls uf8::setFocus). Earlier this hardcoded
    // domain=0, so the preview never switched to BC labels when Q2
    // was active (Frank 2026-05-12 "Labels für Q2 sollten auf die von
    // der Soft-Key Bank zugewiesenen Params wechseln").
    const int sslBank   = reasixty_softkeyCurrentBank();
    const int sslDomain = reasixty_focusedDomain();
    const char* const* sslLabels =
        reasixty_softkeyStockLabels(sslDomain, sslBank);
    // User soft-key bank override — when the user engaged a custom
    // bank via show_user_bank, the device replaces the plug-in labels
    // with the bank's slot labels. Mirror that here so the preview is
    // truly WYSIWYG.
    const int activeUserBank = reasixty_activeUserBank();
    const int activeLayer = uf8::bindings::getActiveLayer();
    for (int i = 0; i < 8; ++i) {
        const float sx = kStripX0 + i * (kStripW + kStripGap);
        // Top soft-key — clickable so the user can edit the per-strip
        // binding directly from the schematic.
        char tlbl[4];
        snprintf(tlbl, sizeof(tlbl), "%d", i + 1);
        drawHwBtn(sx + 6, 12, kStripW - 12, 22, kStripTsk[i], tlbl);
        // Scribble LCD — show the live top-soft-key label. Resolution
        // mirrors the runtime render path:
        //   1. binding.shortPress[Plain].label   (user override)
        //   2. SSL plug-in's softkey label for the current bank slot —
        //      only when the binding is ssl_softkey (otherwise an empty
        //      slot on a fresh layer would still display SSL labels)
        //   3. blank
        rect_(c, sx + 4, 40, kStripW - 8, 58, 0x080C12FF, 0x444A55FF, 2.0);
        std::string scribble;
        {
            const auto bd =
                uf8::bindings::getBinding(activeLayer, kStripTsk[i]);
            const auto& sp = bd.shortPress[
                static_cast<int>(uf8::bindings::Modifier::Plain)];
            const bool isSslSoftkey =
                sp.type == uf8::bindings::ActionType::Builtin &&
                sp.action == "ssl_softkey";
            // Resolution order mirrors pushZonesForVisibleSlots:
            //   1. Per-binding user label (Plain slot)
            //   2. Active user bank slot label (overrides plug-in)
            //   3. SSL plug-in's softkey label for live (domain, bank)
            //      — only when the binding is ssl_softkey, so an
            //      unrelated action doesn't borrow an SSL label.
            if (!sp.label.empty()) {
                scribble = sp.label;
            } else if (activeUserBank >= 0) {
                if (const char* ub =
                        reasixty_userBankSlotLabel(activeUserBank, i)) {
                    scribble = ub;
                }
            } else if (isSslSoftkey
                       && sslLabels && sslLabels[i] && *sslLabels[i]) {
                scribble = sslLabels[i];
            }
        }
        if (scribble.size() > 10) scribble.resize(10);
        if (!scribble.empty()) {
            drawTextCentered_(c, sx + kStripW / 2.0f, 68,
                              0x4488DDFF, scribble.c_str());
        }
        // V-Pot (large dial with notch)
        const float vx = sx + kStripW / 2.0f, vy = 124;
        circle_(c, vx, vy, 18, 0x14181EFF, 0x4A5060FF);
        circle_(c, vx, vy, 14, 0x2A3038FF, 0x555A66FF);
        line_(c, vx, vy - 16, vx, vy - 8, 0xCCCCCCFF, 2.0);
        // Solo / Cut / Sel (locked, per-strip)
        drawLocked(sx + 8, 152, kStripW - 16, 16, "SOLO");
        drawLocked(sx + 8, 172, kStripW - 16, 16, "CUT");
        drawLocked(sx + 8, 192, kStripW - 16, 16, "SEL");
        // Fader: scale ticks + track + cap
        const float fx = sx + kStripW / 2.0f;
        const float fyTop = 220, fyBot = 440;
        // Scale tick marks (left side)
        for (int t = 0; t <= 10; ++t) {
            const float ty = fyTop + (fyBot - fyTop) * (t / 10.0f);
            const float len = (t % 5 == 0) ? 6.0f : 3.0f;
            line_(c, fx - 12, ty, fx - 12 + len, ty, 0x6A6E78FF, 1.0);
        }
        // Track
        rect_(c, fx - 1.5f, fyTop, 3, fyBot - fyTop, 0x444B55FF, 0x000000FF, 1.0);
        // Cap (fixed at unity-ish)
        const float capY = fyTop + (fyBot - fyTop) * 0.40f;
        rect_(c, fx - 12, capY - 7, 24, 14, 0x6A7080FF, 0x9CA0AAFF, 2.5);
        line_(c, fx - 9, capY, fx + 9, capY, 0xE0E0E0FF, 1.5);
    }

    // ---- Left panel ----
    // Group labels need a >= 14 px vertical gap to the button row below
    // (default font height ≈ 13 — anything tighter clips the descenders
    // into the button border).
    drawGroupLabel(20, 6, "LAYER");
    drawGroupLabel(64, 6, "QUICK");
    // Layer + Quick buttons follow the same hardware-proxy + live-ring
    // pattern as the sub-bank selectors below: click dispatches the
    // binding (so the schematic click switches the actual layer / engages
    // the actual Quick), and a green outline marks whichever entry is
    // currently live on the hardware. `activeLayer` is the function-
    // scope live layer captured earlier (line 500); we just need the
    // matching Quick here.
    const int activeQuickRing = (activeLayer >= 0 && activeLayer <= 2)
                                ? reasixty_engagedQuickFor(activeLayer) : -1;

    struct LqBtn { float x, y; ButtonId id; const char* lbl; int idx; };
    const LqBtn layerBtns[3] = {
        {15, 22, ButtonId::Layer1, "1", 0},
        {15, 48, ButtonId::Layer2, "2", 1},
        {15, 74, ButtonId::Layer3, "3", 2},
    };
    for (auto& b : layerBtns) {
        // Layer 3 is conceptually removed (Frank 2026-05-13:
        // "Layer 3 komplett ausgrauen, haben wir rausgeworfen").
        // Render as locked / non-interactive — no hover, no click,
        // no active-ring overlay. Data model still carries 3 layers
        // so hardware presses can still flip activeLayer, but the
        // editor doesn't let you navigate there.
        if (b.idx == 2) {
            drawLocked(b.x, b.y, 36, 22, b.lbl);
            continue;
        }
        const bool justClicked = drawHwBtn(b.x, b.y, 36, 22, b.id, b.lbl);
        if (justClicked) {
            uf8::bindings::dispatch(b.id, /*pressed*/ true);
            uf8::bindings::dispatch(b.id, /*pressed*/ false);
        }
        if (b.idx == activeLayer) {
            ImGui_DrawList_AddRect(c.dl,
                c.ox + b.x - 2, c.oy + b.y - 2,
                c.ox + b.x + 36 + 2, c.oy + b.y + 22 + 2,
                0x40C040FF, /*rounding*/ nullptr,
                /*flags*/ nullptr, /*thickness*/ nullptr);
        }
    }
    const LqBtn quickBtns[3] = {
        {57, 22, ButtonId::Quick1, "1", 0},
        {57, 48, ButtonId::Quick2, "2", 1},
        {57, 74, ButtonId::Quick3, "3", 2},
    };
    for (auto& b : quickBtns) {
        const bool justClicked = drawHwBtn(b.x, b.y, 36, 22, b.id, b.lbl);
        if (justClicked) {
            uf8::bindings::dispatch(b.id, /*pressed*/ true);
            uf8::bindings::dispatch(b.id, /*pressed*/ false);
        }
        if (b.idx == activeQuickRing) {
            ImGui_DrawList_AddRect(c.dl,
                c.ox + b.x - 2, c.oy + b.y - 2,
                c.ox + b.x + 36 + 2, c.oy + b.y + 22 + 2,
                0x40C040FF, /*rounding*/ nullptr,
                /*flags*/ nullptr, /*thickness*/ nullptr);
        }
    }

    drawHwBtn(15, 108, 78, 24, ButtonId::Btn360, "360\xC2\xB0");

    drawGroupLabel(15, 136, "SEND / PLUGIN");
    {
        constexpr ButtonId kSp[8] = {
            ButtonId::SendPlugin1, ButtonId::SendPlugin2,
            ButtonId::SendPlugin3, ButtonId::SendPlugin4,
            ButtonId::SendPlugin5, ButtonId::SendPlugin6,
            ButtonId::SendPlugin7, ButtonId::SendPlugin8,
        };
        char buf[8];
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 2; ++col) {
                const int idx = row * 2 + col;
                snprintf(buf, sizeof(buf), "%d", idx + 1);
                drawHwBtn(13 + col * 42, 152 + row * 26, 38, 22,
                          kSp[idx], buf);
            }
        }
    }

    // PLUGIN / CHANNEL — wider buttons so their silk-screen labels fit.
    drawHwBtn(13, 260, 50, 22, ButtonId::PluginBtn, "PLUGIN");
    drawHwBtn(67, 260, 50, 22, ButtonId::Channel,   "CHANNEL");

    // PAGE — label sits 18 px above the arrow row so the descenders
    // don't touch the button frame.
    drawGroupLabel(48, 282, "PAGE");
    drawHwBtn(13, 300, 50, 22, ButtonId::PageLeft,  "\xE2\x97\x82");
    drawHwBtn(67, 300, 50, 22, ButtonId::PageRight, "\xE2\x96\xB8");

    drawHwBtn(13, 326, 104, 22, ButtonId::Flip, "FLIP");

    drawGroupLabel(36, 354, "AUTOMATION");
    // 3 columns × 2 rows. 33-wide buttons fit "WRITE" / "LATCH" / "TOUCH"
    // without overflow at the default ImGui font.
    drawHwBtn(13,  372, 33, 22, ButtonId::AutoOff,   "OFF");
    drawHwBtn(48,  372, 33, 22, ButtonId::AutoRead,  "READ");
    drawHwBtn(83,  372, 33, 22, ButtonId::AutoWrite, "WRITE");
    drawHwBtn(13,  396, 33, 22, ButtonId::AutoTrim,  "TRIM");
    drawHwBtn(48,  396, 33, 22, ButtonId::AutoLatch, "LATCH");
    drawHwBtn(83,  396, 33, 22, ButtonId::AutoTouch, "TOUCH");

    // Foot-switch jacks (back-panel TRS) decoded 2026-05-19. Rendered
    // below AUTOMATION since the schematic has space there and the
    // jacks have no fixed face position to anchor to anyway.
    drawGroupLabel(34, 424, "FOOT SWITCHES");
    drawHwBtn(13, 442, 50, 22, ButtonId::Foot1, "FOOT 1");
    drawHwBtn(67, 442, 50, 22, ButtonId::Foot2, "FOOT 2");

    // ---- Right panel ----
    // SSL soft-key bank selectors — 2×3 grid. Each switches the active
    // PAGE bank (V-POT / Bank 1..5) via the softkey_bank_select
    // builtin. User can rebind to anything via the editor. The
    // currently-active bank gets an extra green ring around its tile
    // so the user always sees which bank the top-soft-key labels are
    // sourced from right now.
    {
        const int activeBank = reasixty_softkeyCurrentBank();
        struct BankBtn { float x, y, w; ButtonId id; const char* lbl; int idx; };
        const BankBtn banks[6] = {
            {852, 22, 42, ButtonId::VPotBank,     "V-POT", 0},
            {898, 22, 42, ButtonId::SoftKey1Bank, "1",     1},
            {944, 22, 41, ButtonId::SoftKey2Bank, "2",     2},
            {852, 46, 42, ButtonId::SoftKey3Bank, "3",     3},
            {898, 46, 42, ButtonId::SoftKey4Bank, "4",     4},
            {944, 46, 41, ButtonId::SoftKey5Bank, "5",     5},
        };
        for (auto& b : banks) {
            const bool justClicked = drawHwBtn(b.x, b.y, b.w, 20, b.id, b.lbl);
            if (justClicked) {
                // Hardware proxy: fire the binding's action so the
                // schematic click actually switches the SSL PAGE bank
                // (g_softKeyBank), exactly like pressing the hardware
                // button would.
                uf8::bindings::dispatch(b.id, /*pressed*/ true);
                uf8::bindings::dispatch(b.id, /*pressed*/ false);
            }
            if (b.idx == activeBank) {
                // Inset green outline to mark the live bank without
                // fighting the existing select/hover highlight.
                ImGui_DrawList_AddRect(c.dl,
                    c.ox + b.x - 2, c.oy + b.y - 2,
                    c.ox + b.x + b.w + 2, c.oy + b.y + 22,
                    0x40C040FF, /*rounding*/ nullptr,
                    /*flags*/ nullptr, /*thickness*/ nullptr);
            }
        }
    }

    // PAN + FINE — own row below SOFT KEYS, with breathing room.
    drawHwBtn(852, 80, 64, 22, ButtonId::Pan,  "PAN");
    drawHwBtn(921, 80, 64, 22, ButtonId::Fine, "FINE");

    drawGroupLabel(852, 112, "SELECTION MODE");
    drawHwBtn(852, 128, 43, 20, ButtonId::SelectionNorm, "NORM");
    drawHwBtn(899, 128, 43, 20, ButtonId::SelectionRec,  "REC");
    drawHwBtn(946, 128, 39, 20, ButtonId::SelectionAuto, "AUTO");

    // CHANNEL encoder — clickable hit-area covers the dial body so users
    // can edit the encoder push binding directly from the schematic.
    // Label centered over the dial.
    {
        constexpr float cx = 918, cy = 200, r = 32;
        // Hit-area spans the dial bounds. Selecting drives the
        // ChannelPush binding.
        const float hbx = cx - r, hby = cy - r;
        const float hbw = 2 * r, hbh = 2 * r;
        const bool hot      = inside(hbx, hby, hbw, hbh);
        const bool selected = (ButtonId::ChannelPush == sel);
        if (hot && canvasClicked && leftBtn == 0) sel = ButtonId::ChannelPush;

        const uint32_t edge = selected ? 0xAACCFFFF
                              : hot     ? 0x6688AAFF
                                        : 0x4A5060FF;
        circle_(c, cx, cy, r,        0x14181EFF, edge);
        circle_(c, cx, cy, r - 3,    0x252A33FF, 0x555A66FF);
        circle_(c, cx, cy, r * 0.78f, 0x383C44FF, 0x6A6E78FF);
        line_(c, cx, cy - r * 0.95f, cx, cy - r * 0.62f, 0xE0E0E0FF, 2.5);
        for (int k = 0; k < 24; ++k) {
            const float ang = (k / 24.0f) * 6.2831853f - 1.5707963f;
            const float r0 = r - 4, r1 = r - 1;
            const float x1 = cx + std::cos(ang) * r0;
            const float y1 = cy + std::sin(ang) * r0;
            const float x2 = cx + std::cos(ang) * r1;
            const float y2 = cy + std::sin(ang) * r1;
            line_(c, x1, y1, x2, y2, 0x555A66FF, 1.0);
        }
        // Centered CHANNEL label above the dial.
        drawTextCentered_(c, cx, 158, 0x9CA0AAFF, "CHANNEL");
    }

    // NAV / NUDGE / FOCUS — sit below the encoder. Encoder Push has its
    // own click target on the dial above (kept the labelled bar too so
    // users still see the binding name explicitly).
    drawHwBtn(852, 244, 44, 22, ButtonId::Nav,      "NAV");
    drawHwBtn(898, 244, 44, 22, ButtonId::Nudge,    "NUDGE");
    drawHwBtn(944, 244, 41, 22, ButtonId::EncFocus, "FOCUS");

    // Channel-encoder bindings split into push (left) + rotate (right).
    // Rotate is the new bindable surface for the rotation gesture —
    // Plain / Shift / Cmd / Ctrl modifier slots each map to a builtin.
    drawHwBtn(852, 270, 65, 18, ButtonId::ChannelPush,    "PUSH");
    drawHwBtn(919, 270, 66, 18, ButtonId::ChannelEncoder, "ROTATE");

    drawGroupLabel(902, 298, "BANK");
    drawHwBtn(870, 314, 42, 22, ButtonId::BankLeft,  "\xE2\x97\x82");
    drawHwBtn(922, 314, 42, 22, ButtonId::BankRight, "\xE2\x96\xB8");

    // Zoom pad — cross. Shifted down to follow the encoder column.
    {
        constexpr float cx = 918;
        constexpr float baseY = 372;
        drawHwBtn(cx - 17, baseY - 32, 34, 26, ButtonId::ZoomUp,
                  "\xE2\x96\xB2");
        drawHwBtn(cx - 54, baseY,      34, 26, ButtonId::ZoomLeft,
                  "\xE2\x97\x82");
        drawHwBtn(cx - 17, baseY,      34, 26, ButtonId::ZoomCenter,
                  "\xE2\x97\x8F");
        drawHwBtn(cx + 20, baseY,      34, 26, ButtonId::ZoomRight,
                  "\xE2\x96\xB8");
        drawHwBtn(cx - 17, baseY + 32, 34, 26, ButtonId::ZoomDown,
                  "\xE2\x96\xBC");
    }

    // Brand line — replaces the SSL silk-screen with our product name.
    drawTextCentered_(c, 500, 470, 0x9CA0AAFF, "Rea-Sixty");

    // Right-click context menu — must live inside the same canvas ID
    // scope as the OpenPopup call so the popup ID matches.
    renderBindingContextMenu_(ctx, uf8::bindings::getActiveLayer());

    ImGui_PopFont(ctx);
}

// Render the full UC1 schematic. Layout follows the SSL UC1 hardware
// (User Guide page 14 + reference photo): three vertical zones —
// Filters/EQ left, Bus-Compressor + Central Control Panel centre,
// Dynamics + Channel right.
//
// EQ + BC + Dynamics each use a 2-column knob layout:
//   EQ:   Gain & Q in column 1, Freq in column 2.
//         EQ Type / EQ In are two small toggles stacked in column 2
//         between bands. HF Bell + LF Bell sit diagonal next to their
//         band's Gain knob.
//   BC:   Threshold / Attack / Ratio / S/C HPF in column 1;
//         Make-Up / Release / IN-toggle / Mix in column 2.
//         Analog VU meter spans both columns at the top.
//   Comp: Ratio + Release column 1, Threshold column 2 (mid-row).
//         Fast-Attack + Peak toggles tucked in the top-right corner.
//         Dyn-IN toggle + GR meter row below the knob block.
//   Gate: Range + Release column 1, Threshold + Hold column 2.
//         Expand + Fast-Attack-Gate stacked beneath.
//   Channel section bottom: Polarity / S/C Listen / Solo Clear in a
//         small column on the left, then SOLO / CUT / FINE as the
//         large bottom row, with Channel-IN large above FINE.
//
// Colour codes follow the silk-screen: red Gain caps on HF, green on
// HMF, blue on LMF, grey on LF + filters + dynamics. Decorative-only;
// no hit-targets in this iteration.
// Pure visual rendering of the UC1 face. Caller must already have set up
// the canvas (cursor pos + DrawList) and is responsible for reserving
// layout space (via Dummy or InvisibleButton) — this function only paints
// into the DrawList.
//
// `dimDomain` controls the section-level dim overlay used by the FX-Learn
// schematic:
//   Domain::None         → full saturation everywhere (passive face).
//   Domain::ChannelStrip → BC section rendered at ~30% alpha (we're
//                          editing a CS user map; BC controls aren't
//                          relevant).
//   Domain::BusComp      → EQ + DYNAMICS sections dimmed (we're editing
//                          a BC user map).
void drawUc1Face_(VCanvas& c, uf8::Domain dimDomain, bool ccpOnly = false)
{
    constexpr float W = 860, H = 660;

    // Helpers --------------------------------------------------------------
    auto knob = [&](float cx, float cy, float r, uint32_t cap,
                    const char* label = nullptr)
    {
        // Outer ring + inner cap, indicator at 12 o'clock.
        circle_(c, cx, cy, r,        0x14181EFF, 0x4A5060FF);
        circle_(c, cx, cy, r * 0.78f, cap,        0x80808055);
        line_(c,  cx, cy - r * 0.85f, cx, cy - r * 0.45f, 0xE8E8E8FF, 1.6);
        if (label) {
            drawTextCentered_(c, cx, cy + r + 13, 0xB8BCC4FF, label);
        }
    };
    auto btn = [&](float x, float y, float w, float h, const char* label,
                   uint32_t accent = 0x252A33FF)
    {
        rect_(c, x, y, w, h, accent, 0x4A5060FF, 3.0);
        drawTextCentered_(c, x + w / 2.0f, y + h / 2.0f, 0xD0D4DAFF, label);
    };
    auto sectionLabel = [&](float x, float y, const char* text) {
        drawText_(c, x, y, 0x9CA0AAFF, text);
    };

    constexpr uint32_t kRedCap    = 0xC03038FF;  // HF Gain
    constexpr uint32_t kGreenCap  = 0x408840FF;  // HMF Gain + Q
    constexpr uint32_t kBlueCap   = 0x4070C0FF;  // LMF Gain + Q
    constexpr uint32_t kGreyCap   = 0x6A707CFF;  // filters + dynamics
    constexpr uint32_t kBlackCap  = 0x101418FF;  // LF Gain + Freq
    constexpr uint32_t kFreqCap   = 0x3A4150FF;  // Freq knobs (darker grey)
    constexpr uint32_t kAccentBC  = 0x2A4870FF;  // Bus Comp section accent
    constexpr uint32_t kAccentCC  = 0x903030FF;  // Central Control accent

    // Chassis -------------------------------------------------------------
    // ccpOnly mode (Bindings tab): no outer chassis — only the UC1
    // Central Control Panel is the actual UC1 hardware. EQ/BC/DYN
    // belong to CS / BC physical units, drawn only in FX-Learn's
    // unified cross-domain schematic (default ccpOnly=false).
    if (!ccpOnly) {
        rect_(c, 4, 4, W - 8, H - 8, 0x14181EFF, 0x2A3038FF, /*rounding*/ 8.0);
    }

    constexpr float kSmallToggleW = 34, kSmallToggleH = 18;
    auto smallToggle = [&](float x, float y, const char* label) {
        rect_(c, x, y, kSmallToggleW, kSmallToggleH,
              0x252A33FF, 0x4A5060FF, 2.5);
        drawTextCentered_(c, x + kSmallToggleW / 2.0f,
                              y + kSmallToggleH / 2.0f,
                              0xC0C4CCFF, label);
    };

    // Mid-sized toggle — same footprint as the Channel section's
    // POLARITY/S-C LISTEN/SOLO CLR buttons (66×22). Used for the four
    // dynamics-section toggles (FAST ATK COMP / PEAK / EXPAND /
    // FAST ATK GATE).
    constexpr float kDynBtnW = 66, kDynBtnH = 22;
    auto dynBtn = [&](float x, float y, const char* label) {
        rect_(c, x, y, kDynBtnW, kDynBtnH, 0x252A33FF, 0x4A5060FF, 3.0);
        drawTextCentered_(c, x + kDynBtnW / 2.0f, y + kDynBtnH / 2.0f,
                              0xC0C4CCFF, label);
    };

    // ---- Left column: Filters + EQ -------------------------------------
    // CS-domain (Channel Strip hardware). Drawn only in FX-Learn's
    // unified cross-domain schematic, not in the Bindings UC1 mockup.
    constexpr float kColLx = 12, kColLw = 230;
    if (!ccpOnly) {
        // 2-column layout: Gain & Q live in column 1 (cx=60), Freq in
        // column 2 (cx=170). Lo-Pass and Hi-Pass (no IN buttons!) sit at
        // the top, diagonal to each other. EQ Type + EQ In are two small
        // toggles stacked in column 2 between HMF and LMF; HF/LF Bell
        // toggles sit diagonally next to their band's Gain knob.
        rect_(c, kColLx, 12, kColLw, H - 24, 0x1A1E24FF, 0x2A3038FF, 6.0);

        // Filters: Lo-Pass column 1 (top-left), Hi-Pass column 2 diagonal
        // below it. NO IN buttons — they don't exist on UC1.
        // 2026-05-22: EQ knobs shifted inward — GAIN/Q col 60→70, FREQ
        // col 170→162 — so the side labels (HF / HMF / LMF / LF) breathe.
        knob(kColLx + 70,  44, 20, kGreyCap, "LO-PASS");
        knob(kColLx + 162, 70, 20, kGreyCap, "HI-PASS");
        sectionLabel(kColLx + 14, 110, "FILTERS");
        line_(c, kColLx + 70, 122, kColLx + kColLw - 8, 122, 0x383C44FF, 1.0);

        // HF band — Gain in col 1, Freq in col 2 diagonal, HF Bell toggle
        // beside the Freq knob.
        drawText_(c, kColLx + 14, 152, 0xB8BCC4FF, "HF");
        knob(kColLx + 70,  154, 20, kRedCap, "GAIN");
        knob(kColLx + 162, 182, 20, kRedCap, "FREQ");
        smallToggle(kColLx + 192, 146, "BELL");

        // HMF band — Gain + Q in col 1, Freq in col 2.
        drawText_(c, kColLx + 14, 230, 0xB8BCC4FF, "HMF");
        knob(kColLx + 70,  232, 20, kGreenCap, "GAIN");
        knob(kColLx + 162, 260, 20, kGreenCap, "FREQ");
        knob(kColLx + 70,  300, 20, kGreenCap, "Q");

        // EQ Type + EQ In: stacked in column 2 deep in the gap between
        // HMF and LMF. Spacing bumped to 22 px for the larger 34×18
        // toggle footprint.
        smallToggle(kColLx + 192, 356, "TYPE");
        smallToggle(kColLx + 192, 380, "IN");

        // LMF band — Gain + Q in col 1, Freq in col 2.
        drawText_(c, kColLx + 14, 428, 0xB8BCC4FF, "LMF");
        knob(kColLx + 70,  430, 20, kBlueCap, "GAIN");
        knob(kColLx + 162, 458, 20, kBlueCap, "FREQ");
        knob(kColLx + 70,  498, 20, kBlueCap, "Q");

        // LF band — both controls in black per silk-screen. LF Bell
        // dropped to y=600 so the FREQ label (cy+r+13 = 591) clears.
        drawText_(c, kColLx + 14, 556, 0xB8BCC4FF, "LF");
        knob(kColLx + 162, 558, 20, kBlackCap, "FREQ");
        knob(kColLx + 70,  598, 20, kBlackCap, "GAIN");
        smallToggle(kColLx + 192, 600, "BELL");
    }

    // ---- Centre column: Bus Comp (top) + Central Control (bottom) ------
    // Wider than the side columns so the BC knob block can hold an
    // Input-Gain knob to the left of S/C HPF and an Output-Gain knob
    // to the right of MIX, both at the same y as the bottom BC row.
    constexpr float kColCx = kColLx + kColLw + 8, kColCw = 360;
    if (!ccpOnly) {
    rect_(c, kColCx, 12, kColCw, 420, 0x1A1E24FF, kAccentBC, 6.0);
    drawTextCentered_(c, kColCx + kColCw / 2.0f, 22,
                      0x9CA0AAFF, "BUS COMPRESSOR");

    // SSL Bus Comp GR meter — LCD-black face, blue scale, 0 left →
    // 20 right (gain-reduction in dB, NOT input level). No red zone:
    // GR meters don't carry the analog-VU "hot" convention. Pivot
    // sits 3 px above the face bottom so the pivot dot stays inside
    // the face and the needle never protrudes.
    {
        const float mw = 196.0f, mh = 80.0f;
        const float mx = kColCx + (kColCw - mw) / 2.0f, my = 44.0f;
        // Outer bezel + LCD-black face inset (palette matches the
        // 7-seg / LCD blocks in the Central Control panel).
        rect_(c, mx, my, mw, mh, 0x141416FF, 0x282A2EFF, 4.0);
        rect_(c, mx + 4, my + 4, mw - 8, mh - 8,
              0x080C12FF, 0x444A55FF, 2.0);
        const float mcx = mx + mw / 2.0f;
        const float mcy = my + mh - 3;             // pivot inside face
        const float ra  = 70;                      // outer scale radius
        // Sweep -130° to -50° (90° arc)
        const float a0 = -3.14159265f * 130.0f / 180.0f;
        const float a1 = -3.14159265f *  50.0f / 180.0f;
        auto dBtoA = [&](float dB) {
            const float t = dB / 20.0f;             // 0..20
            return a0 + t * (a1 - a0);
        };
        constexpr uint32_t kVuBlue = 0x4499DDFF;    // matches LCD text
        // Tick marks: majors at 0/5/10/15/20, minors at 1/2/3/7
        struct Tick { float dB; const char* label; };
        const Tick ticks[] = {
            {0, "0"}, {1, ""}, {2, ""}, {3, ""}, {5, "5"},
            {7, ""}, {10, "10"}, {15, "15"}, {20, "20"}
        };
        for (const Tick& t : ticks) {
            const float a = dBtoA(t.dB);
            const float tlen = t.label[0] ? 8.0f : 5.0f;
            const float x1 = mcx + std::cos(a) * ra,
                        y1 = mcy + std::sin(a) * ra;
            const float x2 = mcx + std::cos(a) * (ra - tlen),
                        y2 = mcy + std::sin(a) * (ra - tlen);
            line_(c, x1, y1, x2, y2, kVuBlue, 1.6);
            if (t.label[0]) {
                const float lx = mcx + std::cos(a) * (ra - 16);
                const float ly = mcy + std::sin(a) * (ra - 16);
                drawTextCentered_(c, lx, ly, kVuBlue, t.label);
            }
        }
        // Needle — mock reading at 7 dB of gain reduction
        const float aN = dBtoA(7.0f);
        line_(c, mcx, mcy,
                 mcx + std::cos(aN) * (ra - 4),
                 mcy + std::sin(aN) * (ra - 4),
                 kVuBlue, 2.0);
        circle_(c, mcx, mcy, 3, kVuBlue, 0);
        // "GR" badge inside the face
        drawTextCentered_(c, mcx, my + mh - 12, kVuBlue, "GR");
    }

    // BC knob 4×2 grid centred in the wider column. Input-Gain and
    // Output-Gain flank the bottom row but belong to the CHANNEL STRIP
    // domain — they're physically inside the BC chassis on UC1 (knobs
    // 0x0C / 0x16, above the I/O VU strips), but they drive CS Input
    // Trim + Fader Level even with a BC plug-in on the track (see
    // UC1PluginMap.cpp::classifyKnob). Drawing them AFTER the dim
    // overlay below so the CS/BC dim masks treat them correctly:
    // bright when editing a CS map, dimmed when editing a BC map.
    //   col 1 (THR / ATTACK / RATIO / S/C HPF)
    //   col 2 (MAKE-UP / RELEASE / IN-toggle / MIX)
    {
        const float c1x = kColCx + 120, c2x = kColCx + 240;
        const float ry[4] = { 172, 234, 296, 358 };
        knob(c1x, ry[0], 20, kAccentBC, "THR");
        knob(c2x, ry[0], 20, kAccentBC, "MAKE-UP");
        knob(c1x, ry[1], 20, kAccentBC, "ATTACK");
        knob(c2x, ry[1], 20, kAccentBC, "RELEASE");
        knob(c1x, ry[2], 20, kAccentBC, "RATIO");
        // BC IN — toggle button instead of a knob (per UC1 hardware).
        // Label sits 8 px below the button bottom so the text top
        // doesn't bump the box edge.
        rect_(c, c2x - 14, ry[2] - 14, 28, 28, 0xE0E0E0FF, 0x808088FF, 3.0);
        drawTextCentered_(c, c2x, ry[2] + 26, 0x9CA0AAFF, "IN");
        knob(c1x,  ry[3], 20, kAccentBC, "S/C HPF");
        knob(c2x,  ry[3], 20, kAccentBC, "MIX");
    }
    } // end if (!ccpOnly) — BC top section
    // CS-domain Input / Output Gain — drawn last so they sit on top of
    // any dim overlay applied below (see "Section dim overlay" footer).
    // Coordinates mirror the BC bottom-row y so they visually align with
    // S/C HPF and MIX, but the kGreyCap accent + dim-after rendering
    // make their CS domain explicit.
    constexpr float kInOutY  = 358;
    constexpr float kInOutCxL = 60, kInOutCxR = 300;
    auto drawInOutGain = [&]() {
        const float cInL = kColCx + kInOutCxL, cInR = kColCx + kInOutCxR;
        knob(cInL, kInOutY, 20, kGreyCap, "INPUT");
        knob(cInR, kInOutY, 20, kGreyCap, "OUTPUT");
    };

    // Central Control Panel
    // CCP top edge: default 440 (sits under the BC top section), or 12
    // in ccpOnly mode so the CCP isn't floating in 400 px of empty
    // space when the side columns are gone (Bindings UC1 tab).
    const float kCcpY = ccpOnly ? 12.0f : 440.0f;
    constexpr float kCcpH = 208.0f;
    rect_(c, kColCx, kCcpY, kColCw, kCcpH, 0x1A1E24FF, kAccentCC, 6.0);
    // 7-segment display (top-left)
    rect_(c, kColCx + 14, kCcpY + 14, 56, 30, 0x1A0408FF, 0x401818FF, 3.0);
    drawTextCentered_(c, kColCx + 42, kCcpY + 28, 0xFF3030FF, "001");
    // LCD (centre, with mock content). Width grows with the wider
    // BC column; recompute the label-x at the LCD's new horizontal
    // centre instead of the old +138 hardcode.
    {
        const float lcdX = kColCx + 78, lcdW = kColCw - 138;
        const float lcdCx = lcdX + lcdW / 2.0f;
        rect_(c, lcdX, kCcpY + 12, lcdW, 76, 0x05080CFF, 0x444A55FF, 3.0);
        drawTextCentered_(c, lcdCx, kCcpY + 26, 0x808088FF, "MAIN");
        drawTextCentered_(c, lcdCx, kCcpY + 46, 0xE0E0E0FF, "Track Name");
        drawTextCentered_(c, lcdCx, kCcpY + 66, 0x4488DDFF, "Stereo Bus");
    }

    // CCP button row — two bindable buttons centred between LCD bottom
    // and the CS/BC encoders. 360 + MAGNIFY are the only CCP controls
    // exposed for binding; the SSL face's other CCP buttons
    // (BACK/CONFIRM/ROUTING/PRESETS) keep their hardcoded mode-switch
    // behaviour inside UC1Surface and are not represented here.
    // drawUc1BindingsVector paints click/hover/selected overlay on top.
    {
        constexpr float bw = 80, bh = 22, gap = 20;
        const float total = 2 * bw + gap;
        const float x0    = kColCx + (kColCw - total) / 2.0f;
        const float y0    = kCcpY + 100;
        const char* labels[2] = { "360", "MAGNIFY" };
        for (int i = 0; i < 2; ++i) {
            const float bx = x0 + i * (bw + gap);
            rect_(c, bx, y0, bw, bh, 0x252A33FF, 0x4A5060FF, 3.0);
            drawTextCentered_(c, bx + bw / 2.0f, y0 + bh / 2.0f,
                              0xC0C4CCFF, labels[i]);
        }
    }

    // CS encoder + BC encoder — symmetrically centred under the LCD.
    // y = kCcpY + 145 puts the encoder labels (cy+r+9) at kCcpY+172,
    // clearing the brand line at kCcpY + kCcpH - 14 = kCcpY+194.
    {
        const float midX = kColCx + kColCw / 2.0f;
        knob(midX - 40, kCcpY + 145, 18, kGreyCap, "CS Encoder");
        knob(midX + 40, kCcpY + 145, 18, kGreyCap, "BC Encoder");
    }

    // ---- Right column: Dynamics + Channel ------------------------------
    // CS-domain (Channel Strip hardware). Skipped in ccpOnly mode.
    constexpr float kColRx = kColCx + kColCw + 8, kColRw = 230;
    if (!ccpOnly) {
    rect_(c, kColRx, 12, kColRw, H - 24, 0x1A1E24FF, 0x2A3038FF, 6.0);

    // Compressor section. Fast Attack + Peak in the top-right corner;
    // 2-column knob layout below: Ratio + Release in col 1, Threshold
    // in col 2 (mid-row). Y-positions chosen so Compressor + Gate are
    // VERTICALLY CENTRED in the column, with Channel section pinned to
    // the bottom (per Frank's note).
    sectionLabel(kColRx + 14, 32, "COMPRESSOR");
    dynBtn(kColRx + kColRw - 76, 24, "FAST ATK");
    dynBtn(kColRx + kColRw - 76, 50, "PEAK");
    {
        const float c1x = kColRx + 60, c2x = kColRx + 156;
        knob(c1x, 96,  20, kGreyCap, "RATIO");
        knob(c2x, 124, 20, kGreyCap, "THRESHOLD");
        knob(c1x, 158, 20, kGreyCap, "RELEASE");
    }
    // Dyn IN toggle (left) + GR meter (right) — vertically centred
    // in the gap between the Compressor knob block (ends ~y=194 incl.
    // RELEASE label) and the Gate section label (y=270). Mid-point
    // y=232. GR row spacing 12 px (was 8) so values + LEDs breathe.
    rect_(c, kColRx + 14, 221, 22, 22, 0xE0E0E0FF, 0x808088FF, 3.0);
    drawTextCentered_(c, kColRx + 25, 251, 0x9CA0AAFF, "IN");
    {
        const float gx = kColRx + kColRw - 26, gy = 208;
        const char* steps[] = { "20", "14", "9", "6", "3" };
        for (int i = 0; i < 5; ++i) {
            const float ly = gy + i * 12;
            circle_(c, gx, ly, 2.5, 0x404448FF, 0);
            drawText_(c, gx - 18, ly - 5, 0x808088FF, steps[i]);
        }
    }

    // Gate / Expander section. Staggered layout matching the Comp
    // section above — Threshold sits below Range diagonally, Hold
    // sits below Release diagonally. Same visual rhythm as the EQ
    // bands' Gain/Freq pairs in the left column.
    // 2026-05-22 (Frank): section label moved from the section top
    // to the FAST ATK row, right-aligned to the column edge.
    {
        const float c1x = kColRx + 60, c2x = kColRx + 156;
        knob(c1x, 308, 20, kGreyCap, "RANGE");
        knob(c2x, 332, 20, kGreyCap, "THRESHOLD");
        knob(c1x, 374, 20, kGreyCap, "RELEASE");
        knob(c2x, 398, 20, kGreyCap, "HOLD");
    }
    dynBtn(kColRx + 14, 430, "EXPAND");
    dynBtn(kColRx + 14, 456, "FAST ATK");
    {
        // Right-aligned label at the FAST ATK row. Use ImGui_CalcTextSize
        // to measure the actual rendered width (12 px font lock applies
        // here because callers wrap the schematic in PushFont(nil,12)).
        double tw = 0, th = 0;
        ImGui_CalcTextSize(c.ctx, "GATE / EXPANDER", &tw, &th,
                           nullptr, nullptr);
        const float labelX = kColRx + kColRw - 14 - static_cast<float>(tw);
        const float labelY = 456 + (22 - static_cast<float>(th)) / 2.0f;
        drawText_(c, labelX, labelY, 0x9CA0AAFF, "GATE / EXPANDER");
    }

    // Channel section — pinned to the bottom of the column. Three
    // columns; small toggles keep their full width so "S/C LISTEN"
    // fits, while IN / SOLO / CUT / FINE are half-size per Frank's
    // note (those four buttons are physically smaller on the UC1).
    sectionLabel(kColRx + 14, 488, "CHANNEL");
    {
        constexpr float bw = 66;          // small-toggle width — fits "S/C LISTEN"
        constexpr float bh = 22;          // small toggle height
        // 2026-05-22 (Frank): IN / SOLO / CUT / FINE all square + larger
        // (40×40, up from 33×28 rect for SOLO/CUT/FINE and 33×37 for IN).
        constexpr float bigSq = 40.0f;
        // Column 1 left edge (kColRx+14) flush with the dynBtn left
        // edge above (EXPAND / FAST ATK), so Polarity / S/C LISTEN /
        // SOLO CLR sit in the same vertical line as the dynamics
        // toggles. Columns 2/3 shift the same +6 to keep equal gaps.
        const float c1x = kColRx + 14;
        const float c2x = kColRx + 88;
        const float c3x = kColRx + 162;
        // Column 1 — three small toggles + SOLO large at the bottom.
        rect_(c, c1x, 510, bw, bh, 0x252A33FF, 0x4A5060FF, 3.0);
        drawTextCentered_(c, c1x + bw / 2.0f, 521, 0xC0C4CCFF, "Ø");
        rect_(c, c1x, 536, bw, bh, 0x252A33FF, 0x4A5060FF, 3.0);
        drawTextCentered_(c, c1x + bw / 2.0f, 547, 0xC0C4CCFF, "S/C LISTEN");
        rect_(c, c1x, 562, bw, bh, 0x252A33FF, 0x4A5060FF, 3.0);
        drawTextCentered_(c, c1x + bw / 2.0f, 573, 0xC0C4CCFF, "SOLO CLR");
        // Large bottom row — SOLO / CUT / FINE share one Y-line, all
        // square 40×40 and centred in their column slot.
        const float largeY = 595;
        const float soloX = c1x + (bw - bigSq) / 2.0f;
        const float cutX  = c2x + (bw - bigSq) / 2.0f;
        const float fineX = c3x + (bw - bigSq) / 2.0f;
        rect_(c, soloX, largeY, bigSq, bigSq, 0xE0E0E0FF, 0x808088FF, 3.0);
        drawTextCentered_(c, soloX + bigSq / 2.0f, largeY + bigSq / 2.0f, 0x303338FF, "SOLO");
        rect_(c, cutX,  largeY, bigSq, bigSq, 0xE0E0E0FF, 0x808088FF, 3.0);
        drawTextCentered_(c, cutX  + bigSq / 2.0f, largeY + bigSq / 2.0f, 0x303338FF, "CUT");
        // Column 3 — Channel-IN spans the small-toggle rows (centred
        // vertically over the three of them); FINE in the large bottom
        // row, same 40×40 footprint.
        const float inX = c3x + (bw - bigSq) / 2.0f;
        const float inY = 510 + (3 * bh - bigSq) / 2.0f;
        rect_(c, inX, inY, bigSq, bigSq, 0xE0E0E0FF, 0x808088FF, 3.0);
        drawTextCentered_(c, inX + bigSq / 2.0f, inY + bigSq / 2.0f, 0x303338FF, "IN");
        rect_(c, fineX, largeY, bigSq, bigSq, 0xE0E0E0FF, 0x808088FF, 3.0);
        drawTextCentered_(c, fineX + bigSq / 2.0f, largeY + bigSq / 2.0f, 0x303338FF, "FINE");
    }
    } // end if (!ccpOnly) — Right column

    // Brand line — pinned to the bottom of the CCP chassis so it stays
    // inside the panel whether ccpOnly is shifting the CCP up or not.
    drawTextCentered_(c, W / 2.0f, kCcpY + kCcpH - 14,
                      0x9CA0AAFF, "Rea-Sixty / UC1");

    // ---- Section dim overlay (FX-Learn off-domain mask) ------------------
    // 50% alpha black rect over the inactive region. Drawn LAST so it
    // dims everything below it. Coordinates mirror the column backplates
    // above (kColLx/kColLw, kColCx/kColCw, kColRx/kColRw). Skipped in
    // ccpOnly mode (no off-domain sections to mask).
    if (!ccpOnly) {
    constexpr uint32_t kDim = 0x000000A0;     // ~63% alpha black
    constexpr float kColLxDim = 12,  kColLwDim = 230;
    constexpr float kColCxDim = 250, kColCwDim = 360;  // = kColLx + kColLw + 8
    constexpr float kColRxDim = 618, kColRwDim = 230;  // = kColCx + kColCw + 8
    if (dimDomain == uf8::Domain::ChannelStrip) {
        // Editing a CS map → dim BC (centre column, top section only —
        // leave the Central Control Panel below it lit).
        rect_(c, kColCxDim, 12, kColCwDim, 420, kDim, 0, 6.0);
    } else if (dimDomain == uf8::Domain::BusComp) {
        // Editing a BC map → dim left (EQ/Filters) + right (DYN/Channel).
        rect_(c, kColLxDim, 12, kColLwDim, H - 24, kDim, 0, 6.0);
        rect_(c, kColRxDim, 12, kColRwDim, H - 24, kDim, 0, 6.0);
    }

    // Input / Output Gain belong to the Channel Strip domain (knobs 0x0C
    // / 0x16 on UC1 — see UC1PluginMap.cpp::classifyKnob). Draw them on
    // top of the dim overlay so:
    //   - dimDomain=None         → bright (passive face)
    //   - dimDomain=ChannelStrip → bright (peek through the BC dim, they
    //                              ARE CS)
    //   - dimDomain=BusComp      → drawn bright, then a small dim rect
    //                              put OVER them so they read as off-
    //                              domain (they're not BC)
    drawInOutGain();
    // (Frank 2026-05-22: the per-knob dim rect was visually noisy — black
    // squares behind round caps read worse than just leaving the CS knobs
    // at full brightness on the BC chassis. The labels INPUT / OUTPUT
    // already disambiguate them from the BC parameter knobs.)
    } // end if (!ccpOnly) — dim overlay + InOutGain
}

// Thin wrapper around drawUc1Face_ for the passive use-case (Bindings
// editor or future Mixer-tab live mockup) — reserves layout space via a
// canvas-wide InvisibleButton that can also receive clicks.
void drawUc1Vector(ImGui_Context* ctx)
{
    constexpr float W = 860, H = 660;
    ImGui_PushFont(ctx, /*font*/ nullptr, 12.0);
    double oxd = 0, oyd = 0;
    ImGui_GetCursorScreenPos(ctx, &oxd, &oyd);
    ImGui_InvisibleButton(ctx, "uc1_canvas", W, H, /*flags*/ nullptr);
    VCanvas c {
        ctx, ImGui_GetWindowDrawList(ctx),
        static_cast<float>(oxd), static_cast<float>(oyd)
    };
    drawUc1Face_(c, uf8::Domain::None);
    ImGui_PopFont(ctx);
}

// Click-to-bind variant: paints the UC1 face + a small "bindings strip"
// below the chassis with tiles for every bindable UC1 control. Mirrors
// drawUf8Vector's selection/hover/click model so the editor below the
// schematic picks up s_selected the same way regardless of which tab
// you're on. Today only Encoder 2 (rotation + push) is bindable —
// future bindable UC1 controls slot into the same strip.
void drawUc1BindingsVector(ImGui_Context* ctx, ButtonId& sel)
{
    // Same font-lock rationale as drawUf8Vector — schematic labels
    // stay readable at 12 px regardless of the global Font Size
    // picker. Frank 2026-05-22.
    ImGui_PushFont(ctx, /*font*/ nullptr, 12.0);
    // ccpOnly UC1 face: chassis is 208 px tall starting at y=12, brand
    // line at y=206. Pad to 232 so the strip below has 16 px breathing
    // room below the CCP chassis edge.
    constexpr float W       = 860;
    constexpr float faceH   = 232;
    constexpr float stripH  = 56;
    constexpr float H       = faceH + stripH;

    double oxd = 0, oyd = 0;
    ImGui_GetCursorScreenPos(ctx, &oxd, &oyd);
    ImGui_InvisibleButton(ctx, "uc1_bindings_canvas", W, H, /*flags*/ nullptr);
    const bool canvasHovered = ImGui_IsItemHovered(ctx, /*flags*/ nullptr);
    int leftBtn = 0;
    const bool canvasClicked = ImGui_IsItemClicked(ctx, &leftBtn);
    // Right-click → "Copy / Paste binding" context menu (mirrors UF8).
    int rightBtn = 1;
    const bool canvasRightClicked = ImGui_IsItemClicked(ctx, &rightBtn);

    double mxd = 0, myd = 0;
    ImGui_GetMousePos(ctx, &mxd, &myd);

    VCanvas c {
        ctx, ImGui_GetWindowDrawList(ctx),
        static_cast<float>(oxd), static_cast<float>(oyd)
    };
    const float mx = static_cast<float>(mxd) - c.ox;
    const float my = static_cast<float>(myd) - c.oy;

    drawUc1Face_(c, uf8::Domain::None, /*ccpOnly*/ true);

    auto inside = [&](float x, float y, float w, float h) {
        return canvasHovered
            && mx >= x && mx <= x + w
            && my >= y && my <= y + h;
    };

    auto drawHwBtn = [&](float x, float y, float w, float h,
                         ButtonId id, const char* label)
    {
        const bool hot      = inside(x, y, w, h);
        const bool selected = (id == sel);
        const bool clicked  = hot && canvasClicked && leftBtn == 0;
        if (clicked) sel = id;
        // Right-click → "Copy / Paste binding" context menu (Frank
        // 2026-05-15). Mirrors the UF8 schematic's drawHwBtn.
        if (hot && canvasRightClicked && id != ButtonId::None) {
            s_bindingCtxBtn           = id;
            s_bindingCtxOpenRequested = true;
        }
        const uint32_t fill   = selected ? 0x4477CCFF
                                : hot     ? 0x3A4253FF
                                          : 0x252A33FF;
        const uint32_t border = selected ? 0xAACCFFFF : 0x4A5060FF;
        const uint32_t txt    = selected ? 0xFFFFFFFF : 0xD0D4DAFF;
        rect_(c, x, y, w, h, fill, border, /*rounding*/ 3.5);
        drawTextCentered_(c, x + w / 2.0f, y + h / 2.0f, txt, label);
    };

    // 360 + Magnifier overlay — same geometry as drawUc1Face_'s CCP row,
    // hover/select/click painted on top so the user drives the binding
    // from the schematic. Keep numbers in sync with drawUc1Face_'s CCP
    // block: bw=80, bh=22, gap=20, kColCx=250, kColCw=360, and y0 =
    // kCcpY + 100. In ccpOnly mode kCcpY=12, so y0 = 112.
    {
        constexpr float bw = 80, bh = 22, gap = 20;
        constexpr float kColCxV = 250, kColCwV = 360;
        const float total = 2 * bw + gap;
        const float x0    = kColCxV + (kColCwV - total) / 2.0f;
        constexpr float y0 = 12 + 100;          // kCcpY + 100 (ccpOnly)
        drawHwBtn(x0,            y0, bw, bh, ButtonId::Uc1Btn360,    "360");
        drawHwBtn(x0 + bw + gap, y0, bw, bh, ButtonId::Uc1Magnifier, "MAGNIFY");
    }

    // Bindings strip below the chassis. Reads as continuous with the
    // UC1 face above (same chassis fill + outline colours).
    const float stripY = faceH + 4;
    rect_(c, 4, stripY, W - 8, stripH - 8,
          0x14181EFF, 0x2A3038FF, /*rounding*/ 6.0);
    drawText_(c, 18, stripY + 8, 0x9CA0AAFF, "ENCODER 2");
    const float tileY = stripY + 24;
    drawHwBtn(W / 2.0f - 100, tileY, 90, 22,
              ButtonId::Uc1Encoder2,     "ROTATE");
    drawHwBtn(W / 2.0f + 10,  tileY, 90, 22,
              ButtonId::Uc1Encoder2Push, "PUSH");

    // Right-click context menu — same canvas ID scope as drawUf8Vector.
    renderBindingContextMenu_(ctx, uf8::bindings::getActiveLayer());

    ImGui_PopFont(ctx);
}

// Push a Rea-Sixty-themed colour set so the editor's combos / buttons /
// inputs match the schematic palette (dark blue-grey, soft accents)
// instead of the default ImGui orange/red. Returns count to pop.
int pushBindingsTheme(ImGui_Context* ctx)
{
    auto pc = [&](int idx, int rgba) { ImGui_PushStyleColor(ctx, idx, rgba); };
    pc(ImGui_Col_FrameBg,         0x252A33FF);
    pc(ImGui_Col_FrameBgHovered,  0x3A4253FF);
    pc(ImGui_Col_FrameBgActive,   0x4477CCFF);
    pc(ImGui_Col_Button,          0x252A33FF);
    pc(ImGui_Col_ButtonHovered,   0x3A4253FF);
    pc(ImGui_Col_ButtonActive,    0x4477CCFF);
    pc(ImGui_Col_Header,          0x252A33FF);
    pc(ImGui_Col_HeaderHovered,   0x3A4253FF);
    pc(ImGui_Col_HeaderActive,    0x4477CCFF);
    pc(ImGui_Col_Border,          0x4A5060FF);
    pc(ImGui_Col_Text,            0xD0D4DAFF);
    pc(ImGui_Col_TextDisabled,    0x6A6E78FF);
    pc(ImGui_Col_PopupBg,         0x14181EFF);
    pc(ImGui_Col_CheckMark,       0xAACCFFFF);
    pc(ImGui_Col_SliderGrab,      0x4477CCFF);
    pc(ImGui_Col_SliderGrabActive,0x6699EEFF);
    pc(ImGui_Col_Separator,       0x3A4253FF);
    pc(ImGui_Col_ChildBg,         0x1A1E24FF);
    return 18;
}

void popBindingsTheme(ImGui_Context* ctx, int n)
{
    ImGui_PopStyleColor(ctx, &n);
}

// Mutable refs into a Binding so the same picker code drives the
// primary action AND the long-press secondary action — the underlying
// fields differ but the widget tree is identical.
struct ActionFieldsRef {
    uf8::bindings::ActionType*  type;
    std::string*                action;
    int*                        param;
    std::string*                label;
    std::string*                midiDevice;
    int*                        midiChannel;
    int*                        midiMsgType;
    int*                        midiData1;
    int*                        midiData2;
    bool*                       fireOnInactive = nullptr;  // optional — REAPER actions only
};

bool drawActionPicker(ImGui_Context* ctx, const char* prefix,
                      ActionFieldsRef f, int layer,
                      uf8::bindings::ButtonId id, bool isLongPress,
                      int modIdx = 0, int stepIdx = 0,
                      // User-Quick destination coords. When uqLayer >= 0,
                      // the "Browse Action..." button routes the picked
                      // REAPER action into the User-Quick slot rather than
                      // a layer binding. Bug fix 2026-05-13: Frank's
                      // Transport:Play binding on an L1.Q3 soft-key slot
                      // never fired because the picker poll wrote to
                      // layers[-1].bindings[None] (a no-op).
                      int uqLayer = -1, int uqQuick = -1,
                      int uqSubBank = -1, int uqSlot = -1)
{
    using namespace uf8::bindings;
    bool dirty = false;
    char idbuf[80];
    const bool isUserQuick = (uqLayer >= 0);

    auto sectionRadio = [&](const char* tag, const char* label, ActionType t) {
        const bool on = (*f.type == t);
        snprintf(idbuf, sizeof(idbuf), "%s##%s_%s", label, prefix, tag);
        if (ImGui_RadioButton(ctx, idbuf, on)) {
            // Switching action type — clear payload that doesn't transfer
            // across types. A REAPER action ID is meaningless as a Native
            // builtin name (and vice versa), and the integer param's
            // semantics differ per type. Frank 2026-05-14: dropping the
            // old value avoids the "preview blank but stale ID under it"
            // state that left a hidden action sitting in the binding
            // after a type swap. MIDI fields stay (they're MIDI-only
            // anyway and won't fire unless type == Midi).
            if (*f.type != t) {
                f.action->clear();
                *f.param = 0;
            }
            *f.type = t;
            dirty = true;
        }
    };

    // ---- Per-action display label ----
    // What the UF8 LCD shows when this slot is the one that will fire
    // (e.g. user soft-key bank slot, or visible while the matching
    // modifier is held). Empty falls back to the binding's top-level
    // label / hardware-face name. Some callers (e.g. drawSlotPicker
    // for short/long binding-editor slots) opt out by passing nullptr.
    if (f.label) {
        char lblBuf[64] = {0};
        std::strncpy(lblBuf, f.label->c_str(), sizeof(lblBuf) - 1);
        snprintf(idbuf, sizeof(idbuf),
                      "Display label##%s_lbl", prefix);
        double lw = 220;
        ImGui_PushItemWidth(ctx, lw);
        if (ImGui_InputTextWithHint(ctx, idbuf,
                                    "shown on the UF8 LCD",
                                    lblBuf, sizeof(lblBuf),
                                    nullptr, nullptr)) {
            *f.label = lblBuf;
            dirty = true;
        }
        ImGui_PopItemWidth(ctx);
        ImGui_Spacing(ctx);
    }

    // ---- REAPER Action ----
    sectionRadio("rd_reaper", "REAPER Action", ActionType::Reaper);
    if (*f.type == ActionType::Reaper) {
        ImGui_Indent(ctx, /*indent_w*/ nullptr);
        char buf[128] = {0};
        std::strncpy(buf, f.action->c_str(), sizeof(buf) - 1);
        snprintf(idbuf, sizeof(idbuf), "Action ID##%s_actid", prefix);
        double w = 260;
        ImGui_PushItemWidth(ctx, w);
        if (ImGui_InputTextWithHint(ctx, idbuf,
                                    "40044  /  _RS123abc",
                                    buf, sizeof(buf),
                                    /*flags*/ nullptr,
                                    /*callback*/ nullptr)) {
            *f.action = buf;
            dirty = true;
        }
        ImGui_PopItemWidth(ctx);

        // Browse Action / Load ReaScript — open REAPER's pickers.
        // Browse uses PromptForAction; if a picker is already open for
        // THIS binding, swap the button to "Cancel" so the user can
        // close it without picking. Polling is owned by main.cpp's
        // onTimer hook so the result lands even if the user navigates
        // away from this editor before the picker closes.
        const bool pickerOpen = isUserQuick
            ? reasixty_actionPickerActiveForUserQuick(
                uqLayer, uqQuick, uqSubBank, uqSlot, modIdx, stepIdx)
            : reasixty_actionPickerActiveFor(
                layer, id, isLongPress, modIdx, stepIdx);
        snprintf(idbuf, sizeof(idbuf), "%s##%s_browse",
                      pickerOpen ? "Cancel Action Pick" : "Browse Action...",
                      prefix);
        if (ImGui_Button(ctx, idbuf,
                         /*size_w*/ nullptr, /*size_h*/ nullptr)) {
            if (pickerOpen) {
                reasixty_actionPickerCancel();
            } else if (isUserQuick) {
                reasixty_actionPickerStartUserQuick(
                    uqLayer, uqQuick, uqSubBank, uqSlot, modIdx, stepIdx);
            } else {
                reasixty_actionPickerStart(layer, id,
                                           isLongPress,
                                           modIdx, stepIdx);
            }
        }
        sameLine(ctx);
        snprintf(idbuf, sizeof(idbuf), "Load ReaScript...##%s_load",
                      prefix);
        if (ImGui_Button(ctx, idbuf, nullptr, nullptr)) {
            std::string picked = reasixty_loadReaScript();
            if (!picked.empty()) {
                *f.action = picked;
                dirty = true;
            }
        }

        // Resolved-name line — dim, just for confirmation. Empty when
        // the field is blank; "(unresolved)" if the stored named cmd no
        // longer exists in this REAPER instance.
        std::string nameStr = reasixty_resolveActionName(*f.action);
        if (!nameStr.empty()) {
            char line[256];
            snprintf(line, sizeof(line), "  %s", nameStr.c_str());
            ImGui_TextDisabled(ctx, line);
        }

        // Inactive-edge re-fire policy — Frank 2026-05-08.
        // Always opt-in via "fire again on inactive". Behavior::Hold
        // already double-fires via the engine's firing=true on both
        // edges; this checkbox is for non-Hold bindings (Momentary /
        // Toggle) that should still fire on release. Toggle-detection
        // is informational only — the user binds to Hold for "active
        // while held" semantics.
        if (f.fireOnInactive && !f.action->empty()) {
            const bool isToggle = reasixty_actionIsToggle(*f.action);
            if (isToggle) {
                ImGui_TextDisabled(ctx,
                    "  Toggle action — set Behavior to Hold for "
                    "ON-while-held");
            }
            snprintf(idbuf, sizeof(idbuf),
                          "Fire again on inactive##%s_foi", prefix);
            if (ImGui_Checkbox(ctx, idbuf, f.fireOnInactive)) {
                dirty = true;
            }
        }
        ImGui_Unindent(ctx, /*indent_w*/ nullptr);
    }

    // ---- Native (Built-in) Action ----
    sectionRadio("rd_native", "Native Action (Built-in)", ActionType::Builtin);
    if (*f.type == ActionType::Builtin) {
        ImGui_Indent(ctx, nullptr);
        const std::string preview = f.action->empty()
            ? std::string("<pick one>")
            : builtinDisplayName(*f.action);
        snprintf(idbuf, sizeof(idbuf), "Built-in##%s_native", prefix);
        double w = 280;
        ImGui_PushItemWidth(ctx, w);

        // Constrain the popup width up front. Without this ImGui auto-
        // fits to the longest content row on the first frame (very wide
        // because TreeNodes contribute their full label + indent),
        // then re-evaluates on subsequent frames as the layout
        // stabilises — Frank 2026-05-19 sees this as the popup being
        // "too wide on first open and slowly narrowing". Pinning both
        // min and max to the same value makes the popup snap to width
        // immediately. 360 px = wide enough for the longest builtin
        // display name ("Toggle focused plug-in GUI") plus tree
        // indent + scrollbar.
        constexpr double kPopupW = 360.0;
        ImGui_SetNextWindowSizeConstraints(
            ctx, kPopupW, 0.0, kPopupW, 999999.0);

        // HeightLargest = ~20 rows visible — short popup forced two
        // page-scrolls to find anything past the first category. We
        // also auto-scroll the popup so the currently-selected entry
        // is visible the moment it opens (see SetScrollHereY below).
        int comboFlags = ImGui_ComboFlags_HeightLargest;
        if (ImGui_BeginCombo(ctx, idbuf, preview.c_str(), &comboFlags)) {
            // Capture popup-just-opened on the first frame so we can
            // request a scroll-to-selected exactly once per open and
            // reset the search buffer between visits. Reading later in
            // the loop returns false (popup no longer "appearing").
            const bool popupJustOpened = ImGui_IsWindowAppearing(ctx);

            // ---- Search bar -------------------------------------------
            // Single shared buffer across all picker popups — only one
            // combo can be open at a time, so reusing the buffer is safe
            // and avoids per-binding leak of search state. Resets on
            // every fresh open so the user starts on a clean slate.
            static char searchBuf[128] = {0};
            if (popupJustOpened) {
                searchBuf[0] = 0;
                ImGui_SetKeyboardFocusHere(ctx, /*offset*/ nullptr);
            }
            char searchId[64];
            snprintf(searchId, sizeof(searchId), "##%s_search", prefix);
            ImGui_PushItemWidth(ctx, -1.0);
            ImGui_InputTextWithHint(ctx, searchId, "Search actions…",
                                    searchBuf, sizeof(searchBuf),
                                    /*flags*/ nullptr,
                                    /*callback*/ nullptr);
            ImGui_PopItemWidth(ctx);

            std::string filter(searchBuf);
            std::transform(filter.begin(), filter.end(), filter.begin(),
                           [](unsigned char c) {
                               return std::tolower(c);
                           });
            const bool filtering = !filter.empty();

            auto matches = [&](const std::string& key) -> bool {
                if (!filtering) return true;
                auto toLower = [](std::string s) {
                    for (auto& c : s)
                        c = std::tolower(static_cast<unsigned char>(c));
                    return s;
                };
                const std::string display = toLower(builtinDisplayName(key));
                const std::string keyLower = toLower(key);
                return display.find(filter) != std::string::npos
                    || keyLower.find(filter) != std::string::npos;
            };

            // ---- Category buckets -------------------------------------
            // Buckets are tuned 2026-05-15 to put the most-used cycle and
            // mode actions at the top; the long tail (Bank / Page,
            // Automation, Zoom, …) lives further down. Internals starting
            // with "__" are hidden. New builtins must be added here
            // explicitly or they will not show in the picker.
            auto categoryFor = [](const std::string& n) -> const char* {
                if (n.rfind("__", 0) == 0) return "";

                // Cycle Actions — free-bindable to any button / encoder
                // for one-shot or continuous cycling. Sits at the top of
                // the picker because these are the most-asked-for
                // bindings (Frank 2026-05-15).
                if (n == "instance_cycle" || n == "fx_cycle"
                 || n == "instance_next"  || n == "instance_prev"
                 || n == "bc_track_scroll"
                 || n == "select_relative"
                 || n == "playhead_nudge"
                 || n == "mouse_scroll")
                    return "Cycle Actions";

                // Selection Modes — what the V-Pot rotation / push
                // and SEL button do per strip. Each action's display
                // string carries a "(V-Pot)" / "(SEL Button)" suffix so
                // the user sees the control surface at a glance.
                if (n.rfind("selection_mode_", 0) == 0)
                    return "Selection Modes";

                // Encoder Modes — what the UF8 Channel Encoder rotation
                // does (Nav / Nudge / Focus / Instance Cycle / FX Cycle).
                if (n.rfind("encoder_", 0) == 0)
                    return "Encoder Modes";

                // Hardware Modes — surface-wide toggles.
                if (n == "flip" || n == "pan_force"
                 || n == "mixer_toggle" || n == "home"
                 || n == "folder_mode" || n == "show_only_selected"
                 || n.rfind("ssl_strip_mode_", 0) == 0
                 || n.rfind("uf8_plugin_mode_", 0) == 0
                 || n.rfind("marker_overlay_", 0) == 0)
                    return "Hardware Modes";

                // Plug-in family — operates on the currently active
                // FX (cursor with focused-Instance fallback): GUI
                // toggles, bypass / offline, preset navigation, chain
                // reorder. Toggle-all-windows belongs here too.
                if (n == "show_focused_plugin_gui"
                 || n == "show_fx_chain"
                 || n == "close_all_fx_guis"
                 || n.rfind("plugin_", 0) == 0)
                    return "Plug-in";

                if (n.rfind("layer_select", 0) == 0)
                    return "Layer";
                if (n.rfind("softkey_bank_", 0) == 0)
                    return "Soft-Key Bank";

                // SSL stock factory mappings (domain focus + bank-aware
                // soft-key / V-Pot pickers).
                if (n == "domain_cs" || n == "domain_bc"
                 || n == "ssl_softkey"
                 || n.rfind("ssl_bank_", 0) == 0)
                    return "SSL";

                if (n == "bank_left"  || n == "bank_right"
                 || n == "page_left"  || n == "page_right"
                 || n == "bank_by_1_left" || n == "bank_by_1_right")
                    return "Bank / Page";

                if (n.rfind("auto_", 0) == 0) return "Automation";
                if (n.rfind("zoom_", 0) == 0) return "Zoom";

                if (n == "send_this" || n == "recv_this"
                 || n.rfind("send_all_", 0) == 0
                 || n.rfind("recv_all_", 0) == 0)
                    return "Sends / Receives";

                if (n.rfind("selset_", 0) == 0) return "Selection Sets";

                if (n.rfind("param_group_", 0) == 0
                 || n == "multi_select_as_temp_group_toggle")
                    return "Parameter Groups";

                if (n == "selection_clear_all"
                 || n == "tracks_arm_all"
                 || n == "automation_zero_all")
                    return "Tracks";

                if (n.rfind("brightness_", 0) == 0) return "Brightness";

                if (n == "mod_shift" || n == "mod_cmd" || n == "mod_ctrl")
                    return "Modifiers";

                return "";
            };
            static const char* kCats[] = {
                "Cycle Actions",
                "Selection Modes",
                "Encoder Modes",
                "Hardware Modes",
                "Plug-in",
                "Layer",
                "Soft-Key Bank",
                "SSL",
                "Bank / Page",
                "Automation",
                "Zoom",
                "Sends / Receives",
                "Selection Sets",
                "Parameter Groups",
                "Tracks",
                "Brightness",
                "Modifiers",
            };

            std::unordered_map<std::string, std::vector<std::string>> bucket;
            for (auto& n : builtinNames()) {
                const char* cat = categoryFor(n);
                if (!cat || !*cat) continue;
                if (!matches(n)) continue;
                bucket[cat].emplace_back(n);
            }
            for (auto& v : bucket) std::sort(v.second.begin(), v.second.end());

            // Default-open: when not filtering, the top three categories
            // (Cycle Actions / V-Pot Sel-Modes / Encoder Modes) open so
            // the user lands on the common stuff. Filtering force-opens
            // every category with matches so hits are visible without
            // an extra click.
            for (auto* cat : kCats) {
                auto it = bucket.find(cat);
                if (it == bucket.end() || it->second.empty()) continue;

                if (filtering) {
                    int cond = ImGui_Cond_Always;
                    ImGui_SetNextItemOpen(ctx, true, &cond);
                }

                int treeFlags = ImGui_TreeNodeFlags_SpanAvailWidth;
                const bool topThree = (cat == kCats[0]
                                    || cat == kCats[1]
                                    || cat == kCats[2]);
                if (topThree && !filtering) {
                    treeFlags |= ImGui_TreeNodeFlags_DefaultOpen;
                }
                if (ImGui_TreeNode(ctx, cat, &treeFlags)) {
                    for (auto& n : it->second) {
                        std::string lbl = builtinDisplayName(n);
                        bool sel = (n == *f.action);
                        if (ImGui_Selectable(ctx, lbl.c_str(), &sel,
                                             nullptr, nullptr, nullptr)) {
                            *f.action = n;
                            dirty = true;
                        }
                        // Centre the selected row on first open so the
                        // user sees what's currently bound. Skip when
                        // filtering because the result list reshuffles
                        // and the scroll anchor would land on the wrong
                        // row.
                        if (sel && popupJustOpened && !filtering) {
                            double centre = 0.5;
                            ImGui_SetScrollHereY(ctx, &centre);
                        }
                    }
                    ImGui_TreePop(ctx);
                }
            }
            ImGui_EndCombo(ctx);
        }
        ImGui_PopItemWidth(ctx);
        if (builtinUsesParam(*f.action)) {
            // Modifier builtins use param as a Momentary/Toggle mode
            // selector — show a friendlier combo instead of the raw int.
            const bool isMod = (*f.action == "mod_shift"
                             || *f.action == "mod_cmd"
                             || *f.action == "mod_ctrl");
            // Send/receive routing builtins use param as a Flip flag
            // (0 = Faders default, 1 = V-Pots).
            const bool isRouting =
                f.action->rfind("send_all_", 0) == 0
             || f.action->rfind("recv_all_", 0) == 0
             || *f.action == "send_this"
             || *f.action == "recv_this";
            // SSL Soft-Key builtins use param as the slot index 0..7.
            // Show a combo listing the actual function names from the
            // SSL plug-in's bank (e.g. "BYPASS / IN TRIM / PHASE …").
            // ssl_softkey follows the current PAGE bank (we use V-POT
            // labels as the picker hint since they're the most useful
            // generic set); ssl_bank_* address a specific bank.
            // ssl_softkey is bank-aware — its slot's meaning changes
            // with the live PAGE bank, so auto-filling a single label
            // from one bank would be wrong on every other bank
            // (incident 2026-05-02: sp.label "BYPASS" persisting onto
            // Bank 1's slot 0 = WIDTH). Limit the slot picker + label
            // auto-fill to the explicit-bank ssl_bank_* actions.
            int sslBankIdx = -1;          // -1 = no bank-tied combo / auto-fill
            int sslBankComboOnly = -1;    // show combo only, no label fill
            if (*f.action == "ssl_softkey") {
                sslBankComboOnly = 0;     // V-POT labels as a hint for the combo
            } else if (*f.action == "ssl_bank_vpot") {
                sslBankIdx = 0;
            } else if (f.action->rfind("ssl_bank_", 0) == 0
                    && f.action->size() == 10) {
                const char d = (*f.action)[9];
                if (d >= '1' && d <= '5') sslBankIdx = d - '0';
            }
            if (isMod) {
                static char kModeItems[] =
                    "Momentary (held = active)\0"
                    "Toggle (press flips state)\0";
                snprintf(idbuf, sizeof(idbuf), "Mode##%s_modemod", prefix);
                int m = (*f.param == 1) ? 1 : 0;
                double pw = 200;
                ImGui_PushItemWidth(ctx, pw);
                if (ImGui_Combo(ctx, idbuf, &m, kModeItems, nullptr)) {
                    *f.param = m;
                    dirty = true;
                }
                ImGui_PopItemWidth(ctx);
            } else if (isRouting) {
                snprintf(idbuf, sizeof(idbuf),
                              "Flip onto V-Pots (default: Faders)##%s_routeflip",
                              prefix);
                bool flipped = (*f.param == 1);
                if (ImGui_Checkbox(ctx, idbuf, &flipped)) {
                    *f.param = flipped ? 1 : 0;
                    dirty = true;
                }
            } else if (sslBankIdx >= 0 || sslBankComboOnly >= 0) {
                const int comboBank = (sslBankIdx >= 0)
                                         ? sslBankIdx : sslBankComboOnly;
                // Build a combo from the bank's labels. Empty slots
                // (some banks have gaps in the SSL plug-in spec) show
                // as "(empty)" so the user sees the slot exists but
                // does nothing if pressed.
                const char* const* labels =
                    reasixty_softkeyStockLabels(
                        reasixty_focusedDomain(), comboBank);
                // Auto-fill the display label only for explicit-bank
                // actions where the slot's meaning is stable. ssl_softkey
                // intentionally skipped — its label has to track the live
                // PAGE bank at render time, not be frozen here.
                if (sslBankIdx >= 0 && f.label && f.label->empty()
                    && labels) {
                    const int curSlot = std::clamp(*f.param, 0, 7);
                    if (labels[curSlot] && *labels[curSlot]) {
                        *f.label = labels[curSlot];
                        dirty = true;
                    }
                }
                char comboItems[8 * 32 + 1] = {0};
                size_t pos = 0;
                for (int i = 0; i < 8; ++i) {
                    const char* l = (labels && labels[i] && *labels[i])
                        ? labels[i] : "(empty)";
                    const size_t n = std::strlen(l);
                    if (pos + n + 2 < sizeof(comboItems)) {
                        std::memcpy(comboItems + pos, l, n);
                        pos += n;
                        comboItems[pos++] = '\0';
                    }
                }
                comboItems[pos] = '\0';
                snprintf(idbuf, sizeof(idbuf),
                              "SSL function##%s_sslslot", prefix);
                int slot = std::clamp(*f.param, 0, 7);
                double pw = 240;
                ImGui_PushItemWidth(ctx, pw);
                if (ImGui_Combo(ctx, idbuf, &slot, comboItems, nullptr)) {
                    *f.param = slot;
                    dirty = true;
                    // Auto-fill the slot's display label only for
                    // explicit-bank SSL actions. ssl_softkey is
                    // bank-aware so freezing one slot's name into
                    // sp.label would mis-display on every other bank.
                    if (sslBankIdx >= 0 && f.label && labels) {
                        const char* prevName =
                            labels[std::clamp(slot, 0, 7)];
                        // Detect "previously auto-filled with an SSL
                        // name" by comparing against any of this
                        // bank's labels. User-typed labels survive.
                        bool wasAutoFilled = f.label->empty();
                        for (int j = 0; !wasAutoFilled && j < 8; ++j) {
                            if (labels[j] && *labels[j]
                                && *f.label == labels[j]) {
                                wasAutoFilled = true;
                            }
                        }
                        if (wasAutoFilled && prevName && *prevName) {
                            *f.label = prevName;
                        }
                    }
                }
                ImGui_PopItemWidth(ctx);
            } else {
                snprintf(idbuf, sizeof(idbuf), "Param##%s_bparam", prefix);
                double pw = 90;
                ImGui_PushItemWidth(ctx, pw);
                int p = *f.param;
                if (ImGui_InputInt(ctx, idbuf, &p, nullptr, nullptr, nullptr)) {
                    *f.param = p;
                    dirty = true;
                }
                ImGui_PopItemWidth(ctx);
            }
        }
        ImGui_Unindent(ctx, nullptr);
    }

    // ---- MIDI Command ----
    sectionRadio("rd_midi", "MIDI Command", ActionType::Midi);
    if (*f.type == ActionType::Midi) {
        ImGui_Indent(ctx, nullptr);
        // Device — combo over REAPER's enumerated MIDI outputs.
        // Sentinel "(all enabled outputs)" stores empty string,
        // routing to every enabled output at dispatch time. Stored
        // device names that aren't currently enumerated (e.g. usb
        // device unplugged) surface as an "(offline)" entry so the
        // binding isn't silently lost when the user re-opens the
        // editor without that hardware connected.
        const std::string preview = f.midiDevice->empty()
            ? std::string("(all enabled outputs)")
            : *f.midiDevice;
        snprintf(idbuf, sizeof(idbuf), "Device##%s_dev", prefix);
        double w = 280;
        ImGui_PushItemWidth(ctx, w);
        if (ImGui_BeginCombo(ctx, idbuf, preview.c_str(), nullptr)) {
            bool selAny = f.midiDevice->empty();
            char anyId[64];
            snprintf(anyId, sizeof(anyId),
                          "(all enabled outputs)##%s_devany", prefix);
            if (ImGui_Selectable(ctx, anyId, &selAny, nullptr,
                                 nullptr, nullptr)) {
                f.midiDevice->clear();
                dirty = true;
            }
            const int nOut = GetNumMIDIOutputs();
            bool sawCurrent = f.midiDevice->empty();
            for (int i = 0; i < nOut; ++i) {
                char nm[256] = {0};
                if (!GetMIDIOutputName(i, nm, sizeof(nm))) continue;
                if (!*nm) continue;
                bool sel = (*f.midiDevice == nm);
                if (sel) sawCurrent = true;
                char rowId[320];
                snprintf(rowId, sizeof(rowId),
                              "%s##%s_devo%d", nm, prefix, i);
                if (ImGui_Selectable(ctx, rowId, &sel, nullptr,
                                     nullptr, nullptr)) {
                    *f.midiDevice = nm;
                    dirty = true;
                }
            }
            if (!sawCurrent) {
                bool sel = true;
                char rowId[320];
                snprintf(rowId, sizeof(rowId),
                              "%s (offline)##%s_devstale",
                              f.midiDevice->c_str(), prefix);
                // Selecting an offline entry is a no-op — it just
                // confirms the stored name. Kept selectable so the
                // user can see it's still set.
                ImGui_Selectable(ctx, rowId, &sel, nullptr,
                                 nullptr, nullptr);
            }
            ImGui_EndCombo(ctx);
        }
        ImGui_PopItemWidth(ctx);

        // Channel 1..16
        snprintf(idbuf, sizeof(idbuf), "Channel##%s_ch", prefix);
        double cw = 90;
        ImGui_PushItemWidth(ctx, cw);
        int ch = *f.midiChannel;
        if (ImGui_InputInt(ctx, idbuf, &ch, nullptr, nullptr, nullptr)) {
            if (ch < 1)  ch = 1;
            if (ch > 16) ch = 16;
            *f.midiChannel = ch;
            dirty = true;
        }
        ImGui_PopItemWidth(ctx);

        // Message type. BeginCombo + Selectable instead of ImGui_Combo
        // because ReaImGui's ImGui_Combo silently rendered nothing for
        // the \0-separated items format here (Frank 2026-05-14: "da ist
        // kein dropdown!"), so the user could never change msgType from
        // its NoteOn default — every "send CC" binding actually sent
        // Note On. The explicit popup form is the same one we use for
        // the Native-action picker and definitely works.
        static const char* kMidiMsgNames[] = {
            "Note On", "Note Off", "Control Change", "Program Change"
        };
        constexpr int kMidiMsgCount =
            sizeof(kMidiMsgNames) / sizeof(kMidiMsgNames[0]);
        snprintf(idbuf, sizeof(idbuf), "Message##%s_msg", prefix);
        const int curMsg = std::clamp(*f.midiMsgType, 0, kMidiMsgCount - 1);
        double mw = 200;
        ImGui_PushItemWidth(ctx, mw);
        if (ImGui_BeginCombo(ctx, idbuf, kMidiMsgNames[curMsg],
                             /*flags*/ nullptr)) {
            for (int i = 0; i < kMidiMsgCount; ++i) {
                bool sel = (i == curMsg);
                if (ImGui_Selectable(ctx, kMidiMsgNames[i], &sel,
                                     nullptr, nullptr, nullptr)) {
                    *f.midiMsgType = i;
                    dirty = true;
                }
            }
            ImGui_EndCombo(ctx);
        }
        ImGui_PopItemWidth(ctx);

        // Data bytes
        snprintf(idbuf, sizeof(idbuf), "Note / CC ###%s_d1", prefix);
        double dw = 90;
        ImGui_PushItemWidth(ctx, dw);
        int d1 = *f.midiData1;
        if (ImGui_InputInt(ctx, idbuf, &d1, nullptr, nullptr, nullptr)) {
            if (d1 < 0)   d1 = 0;
            if (d1 > 127) d1 = 127;
            *f.midiData1 = d1;
            dirty = true;
        }
        ImGui_PopItemWidth(ctx);

        snprintf(idbuf, sizeof(idbuf), "Velocity / Value##%s_d2", prefix);
        ImGui_PushItemWidth(ctx, dw);
        int d2 = *f.midiData2;
        if (ImGui_InputInt(ctx, idbuf, &d2, nullptr, nullptr, nullptr)) {
            if (d2 < 0)   d2 = 0;
            if (d2 > 127) d2 = 127;
            *f.midiData2 = d2;
            dirty = true;
        }
        ImGui_PopItemWidth(ctx);

        ImGui_Unindent(ctx, nullptr);
    }

    return dirty;
}

bool drawStepPicker_(ImGui_Context* ctx, const char* prefix,
                     int layer, ButtonId id,
                     uf8::bindings::ActionStep& st,
                     bool isLongPress, int modIdx = 0, int stepIdx = 0)
{
    ActionFieldsRef ref{
        &st.type, &st.action, &st.param, &st.label,
        &st.midiDevice, &st.midiChannel, &st.midiMsgType,
        &st.midiData1, &st.midiData2,
        &st.fireOnInactive,
    };
    return drawActionPicker(ctx, prefix, ref, layer, id, isLongPress,
                            modIdx, stepIdx);
}

// Helper: render an ActionSlot as an ordered chain of steps. Each step
// row gets its own action picker plus a "wait ms after" int input;
// + / – buttons add or remove steps. Single-step chains read identically
// to the legacy single-action editor (the +/wait controls are below the
// inline picker).
bool drawSlotPicker(ImGui_Context* ctx, const char* prefix,
                    int layer, ButtonId id, uf8::bindings::ActionSlot& s,
                    bool isLongPress, int modIdx = 0)
{
    using namespace uf8::bindings;
    bool dirty = false;
    char idbuf[80];
    const int n = stepCount(s);
    int removeIdx = -1;
    for (int i = 0; i < n; ++i) {
        ActionStep& st = stepAt(s, i);
        if (n > 1) {
            char header[80];
            snprintf(header, sizeof(header), "Step %d", i + 1);
            ImGui_Text(ctx, header);
            ImGui_SameLine(ctx, nullptr, nullptr);
            snprintf(idbuf, sizeof(idbuf), "Remove##%s_rm_%d", prefix, i);
            if (ImGui_Button(ctx, idbuf, nullptr, nullptr)) {
                removeIdx = i;
            }
        }
        char stepPrefix[80];
        snprintf(stepPrefix, sizeof(stepPrefix), "%s_st%d", prefix, i);
        if (drawStepPicker_(ctx, stepPrefix, layer, id, st, isLongPress,
                            modIdx, /*stepIdx*/ i)) {
            dirty = true;
        }
        if (i < n - 1) {
            snprintf(idbuf, sizeof(idbuf),
                          "Wait ms after##%s_wait_%d", prefix, i);
            double ww = 110;
            ImGui_PushItemWidth(ctx, ww);
            int w = st.wait_ms;
            if (ImGui_InputInt(ctx, idbuf, &w, nullptr, nullptr, nullptr)) {
                if (w < 0) w = 0;
                if (w > 600000) w = 600000;
                st.wait_ms = w;
                dirty = true;
            }
            ImGui_PopItemWidth(ctx);
        }
        ImGui_Spacing(ctx);
    }
    if (removeIdx >= 0) {
        if (removeIdx == 0) {
            // Promote step 1 into the slot's inline step; if there was
            // only step 0, fall back to a default Noop step.
            if (!s.extraSteps.empty()) {
                static_cast<ActionStep&>(s) = std::move(s.extraSteps.front());
                s.extraSteps.erase(s.extraSteps.begin());
            } else {
                static_cast<ActionStep&>(s) = ActionStep{};
            }
        } else {
            s.extraSteps.erase(s.extraSteps.begin() + (removeIdx - 1));
        }
        dirty = true;
    }
    snprintf(idbuf, sizeof(idbuf), "+ Add step##%s_add", prefix);
    if (ImGui_Button(ctx, idbuf, nullptr, nullptr)) {
        s.extraSteps.emplace_back();
        dirty = true;
    }

    // Per-slot LED override. Collapsed by default. Active and inactive
    // are independently opt-in; checkbox toggles the override and the
    // colour swatch / brightness radios mutate the slot's LedOverride.
    char ledHdr[80];
    snprintf(ledHdr, sizeof(ledHdr), "LED override##%s_ledhdr", prefix);
    if (ImGui_CollapsingHeader(ctx, ledHdr, nullptr, nullptr)) {
        ImGui_Indent(ctx, nullptr);
        auto drawOverrideRow = [&](const char* rowLabel,
                                   bool& has,
                                   uint8_t (&rgb)[3],
                                   Brightness& bri,
                                   const char* idTag) {
            char cbId[64];
            snprintf(cbId, sizeof(cbId),
                          "Override %s##%s_ov_%s", rowLabel, prefix, idTag);
            bool en = has;
            if (ImGui_Checkbox(ctx, cbId, &en)) {
                has = en;
                dirty = true;
            }
            if (!has) return;
            ImGui_Indent(ctx, nullptr);
            const int curRgba =
                (int(rgb[0]) << 24) |
                (int(rgb[1]) << 16) |
                (int(rgb[2]) <<  8) | 0xFF;
            char btnId[64];
            snprintf(btnId, sizeof(btnId),
                          "##cur_%s_%s", prefix, idTag);
            int btnFlags = 0;
            double bw = 56.0, bh = 22.0;
            if (ImGui_ColorButton(ctx, btnId, curRgba,
                                  &btnFlags, &bw, &bh)) {
                char popId[64];
                snprintf(popId, sizeof(popId),
                              "palette_%s_%s", prefix, idTag);
                ImGui_OpenPopup(ctx, popId, nullptr);
            }
            char popId[64];
            snprintf(popId, sizeof(popId),
                          "palette_%s_%s", prefix, idTag);
            if (ImGui_BeginPopup(ctx, popId, nullptr)) {
                int paletteCount = 0;
                const uf8::PaletteRgb* palette =
                    uf8::selPaletteRgb(&paletteCount);
                const double sw = 26.0;
                const int perRow = 5;
                for (int j = 0; j < paletteCount; ++j) {
                    const auto& p = palette[j];
                    const int packed =
                        (int(p.r) << 24) |
                        (int(p.g) << 16) |
                        (int(p.b) <<  8) | 0xFF;
                    char swId[64];
                    snprintf(swId, sizeof(swId),
                                  "##pp_%s_%s_%d", prefix, idTag, j);
                    int swFlags = 0;
                    double w = sw, h = sw;
                    if (ImGui_ColorButton(ctx, swId, packed,
                                          &swFlags, &w, &h)) {
                        rgb[0] = p.r;
                        rgb[1] = p.g;
                        rgb[2] = p.b;
                        dirty = true;
                        ImGui_CloseCurrentPopup(ctx);
                    }
                    if ((j % perRow) != (perRow - 1)
                        && j != paletteCount - 1) {
                        ImGui_SameLine(ctx, nullptr, nullptr);
                    }
                }
                ImGui_EndPopup(ctx);
            }
            ImGui_SameLine(ctx, nullptr, nullptr);
            ImGui_Text(ctx, "  ");
            ImGui_SameLine(ctx, nullptr, nullptr);
            const char* names[] = {"Off", "Dim", "Bright"};
            for (int j = 0; j < 3; ++j) {
                char rId[64];
                snprintf(rId, sizeof(rId),
                              "%s##b_%s_%s_%d",
                              names[j], prefix, idTag, j);
                if (ImGui_RadioButton(ctx, rId,
                                      static_cast<int>(bri) == j)) {
                    bri = static_cast<Brightness>(j);
                    dirty = true;
                }
                if (j < 2) ImGui_SameLine(ctx, nullptr, nullptr);
            }
            ImGui_Unindent(ctx, nullptr);
        };
        drawOverrideRow("Active",   s.led.hasActive,
                        s.led.color,         s.led.brightness,         "act");
        drawOverrideRow("Inactive", s.led.hasInactive,
                        s.led.inactiveColor, s.led.inactiveBrightness, "ina");
        char resetId[64];
        snprintf(resetId, sizeof(resetId),
                      "Reset to binding default##%s_ledreset", prefix);
        if (ImGui_Button(ctx, resetId, nullptr, nullptr)) {
            s.led = LedOverride{};
            dirty = true;
        }
        ImGui_Unindent(ctx, nullptr);
    }
    return dirty;
}

// Editor panel — two-column matrix. Left: SHORT PRESS (Plain row + 3
// modifier collapsibles). Right: LONG PRESS (same shape, only for
// Momentary). Auto-saves on every change.
void drawBindingEditor(ImGui_Context* ctx, int layer, ButtonId id)
{
    using namespace uf8::bindings;

    Binding bd    = getBinding(layer, id);
    bool    dirty = false;

    char header[200];
    // Header reads left-to-right with broadest context first
    // ("Editing: Layer 2, Quick 1, FLIP") — Frank 2026-05-13.
    // Layer / Quick buttons are self-identifying, so their header
    // skips the redundant "1" face label entirely.
    const bool idIsLayer =
        id == ButtonId::Layer1 || id == ButtonId::Layer2
     || id == ButtonId::Layer3;
    const bool idIsQuick =
        id == ButtonId::Quick1 || id == ButtonId::Quick2
     || id == ButtonId::Quick3;
    const int  quickNum =
        (id == ButtonId::Quick1) ? 1
      : (id == ButtonId::Quick2) ? 2
      : (id == ButtonId::Quick3) ? 3 : 0;

    if (idIsLayer) {
        snprintf(header, sizeof(header),
                      "Editing: Layer %d", layer + 1);
    } else if (idIsQuick) {
        snprintf(header, sizeof(header),
                      "Editing: Layer %d, Quick %d", layer + 1, quickNum);
    } else {
        // Generic button: layer + (engaged Quick) breadcrumb, then face.
        const int liveQ = reasixty_activeQuickFor(layer);
        if (liveQ >= 0) {
            snprintf(header, sizeof(header),
                          "Editing: Layer %d, Quick %d, %s",
                          layer + 1, liveQ + 1, hwFaceLabel(id));
        } else {
            snprintf(header, sizeof(header),
                          "Editing: Layer %d, %s",
                          layer + 1, hwFaceLabel(id));
        }
    }
    ImGui_Text(ctx, header);
    ImGui_Separator(ctx);

    ImGui_PushID(ctx, uf8::bindings::toName(id));
    const int themePushed = pushBindingsTheme(ctx);

    // True when the binding's plain short slot maps a button to a
    // Modifier role. Combining a modifier with itself is undefined, so
    // we hide the modifier rows + long press for these bindings.
    auto& shortPlain = bd.shortPress[static_cast<int>(Modifier::Plain)];
    const bool plainIsModifier =
        shortPlain.type == ActionType::Builtin
        && (shortPlain.action == "mod_shift"
         || shortPlain.action == "mod_cmd"
         || shortPlain.action == "mod_ctrl");

    const bool momentary = (bd.behavior == Behavior::Momentary);
    // Long-press: hidden only when this button IS itself a modifier (the
    // modifier slots make no sense for a modifier binding) OR when it's the
    // encoder rotate gesture (no press/hold concept on a rotation). All
    // three behaviours (Momentary / Toggle / Hold) support long-press —
    // Momentary picks short OR long on release; Toggle / Hold fire primary
    // on press then long additively on hold-and-release past threshold.
    const bool isEncoder = (id == ButtonId::ChannelEncoder
                          || id == ButtonId::Uc1Encoder2);
    const bool longPressAvailable = !plainIsModifier && !isEncoder;

    // Two side-by-side columns. Each child sized half the available
    // width with matching height so the bordered panels read as a pair.
    double availX = 0, availY = 0;
    ImGui_GetContentRegionAvail(ctx, &availX, &availY);
    double colW = (availX - 16) / 2.0;
    if (colW < 320) colW = 320;
    const double colH = 480;

    static const char* kModNames[]   = { "(no modifier)", "+ Shift / Fine",
                                         "+ Cmd",  "+ Ctrl" };
    static const char* kModSlugs[]   = { "pl", "sh", "cm", "ct" };

    auto drawColumn = [&](const char* title, const char* tag,
                          ActionSlot* slots, bool isLongCol)
    {
        double w = colW, h = colH;
        int childFlags = ImGui_ChildFlags_Borders;
        char childId[32];
        snprintf(childId, sizeof(childId), "%s_col", tag);
        if (ImGui_BeginChild(ctx, childId, &w, &h, &childFlags, nullptr)) {
            ImGui_Text(ctx, title);
            ImGui_Separator(ctx);

            // Whether to render the slot rows after the header. Replaces
            // the previous early-`return`s — those skipped EndChild and
            // tore the parent window down (same ReaImGui v0.10 trap as
            // commit c6bb9d0). Now the only exit point is the bottom of
            // the lambda.
            bool renderSlots = true;

            // Behavior combo lives only in the SHORT column — it
            // applies to both columns.
            if (!isLongCol) {
                static char kBehaviorItems[] =
                    "Momentary (fire on press)\0"
                    "Toggle (flip on each press)\0"
                    "Hold (state mirrors button)\0";
                int b = static_cast<int>(bd.behavior);
                double bw = 240;
                ImGui_PushItemWidth(ctx, bw);
                if (ImGui_Combo(ctx, "Behavior##pri_beh", &b, kBehaviorItems,
                                nullptr)) {
                    bd.behavior = static_cast<Behavior>(b);
                    dirty = true;
                }
                ImGui_PopItemWidth(ctx);
                ImGui_Spacing(ctx);
                ImGui_Separator(ctx);
            } else {
                // Long column gets an explicit enable toggle. Visible for
                // Momentary / Toggle / Hold — semantics differ at dispatch
                // (Momentary chooses short OR long on release; Toggle and
                // Hold fire primary on press then additionally fire long
                // on release after the hold threshold).
                bool en = bd.hasLongPress;
                if (ImGui_Checkbox(ctx, "Enable long-press (held > 0.5 s)",
                                   &en)) {
                    bd.hasLongPress = en;
                    if (en && slots[0].type == ActionType::Noop) {
                        slots[0].type = ActionType::Builtin;
                    }
                    // Auto-coerce to Momentary so short and long are
                    // mutually exclusive (defer-and-choose). Otherwise
                    // a Toggle / Hold binding fires the short action on
                    // press-edge AND the long action on release-after-
                    // threshold — almost never what the user wants
                    // when they attach a long-press (Frank 2026-05-14:
                    // folder_mode on PAN long-press also flipped pan
                    // mode). Override by re-picking Toggle / Hold in
                    // the Behavior combo above.
                    if (en && bd.behavior != Behavior::Momentary) {
                        bd.behavior = Behavior::Momentary;
                    }
                    dirty = true;
                }
                ImGui_TextDisabled(ctx,
                    "Long-press auto-sets Behavior to Momentary so the");
                ImGui_TextDisabled(ctx,
                    "short action doesn't also fire on release.");
                if (!bd.hasLongPress) {
                    renderSlots = false;
                } else {
                    ImGui_Spacing(ctx);
                    ImGui_Separator(ctx);
                }
            }

            if (renderSlots) {
            // Plain row — always drawn first; never collapsed.
            ImGui_Text(ctx, kModNames[0]);
            char slotPrefix[32];
            snprintf(slotPrefix, sizeof(slotPrefix), "%s_pl", tag);
            if (drawSlotPicker(ctx, slotPrefix, layer, id, slots[0], isLongCol,
                               /*modIdx*/ 0))
                dirty = true;

            // Modifier rows — each in its own collapsing header. Hidden
            // entirely when this binding is itself a modifier.
            if (plainIsModifier) {
                ImGui_Spacing(ctx);
                ImGui_TextDisabled(ctx,
                    "(Modifier rows hidden — this button IS a modifier.)");
            } else {
                for (int m = 1; m < kModifierCount; ++m) {
                    ImGui_Spacing(ctx);
                    ImGui_Separator(ctx);
                    char hdr[96];
                    if (slots[m].type != ActionType::Noop && !slots[m].action.empty()) {
                        // Resolve to the user-facing label so builtins
                        // appear under their friendly name (e.g.
                        // "Nav Mode: Regions only (no drill)" instead
                        // of "marker_overlay_regions_only_toggle"
                        // Frank 2026-05-19). Non-builtins fall through
                        // to the raw action string (REAPER actions
                        // already use their `_` id, MIDI commands
                        // ditto).
                        const std::string disp =
                            (slots[m].type == ActionType::Builtin)
                            ? builtinDisplayName(slots[m].action)
                            : slots[m].action;
                        snprintf(hdr, sizeof(hdr), "%s   →  %s",
                                      kModNames[m], disp.c_str());
                    } else {
                        snprintf(hdr, sizeof(hdr), "%s   (empty)",
                                      kModNames[m]);
                    }
                    // `###` (not `##`) so only the slug after the ###
                    // determines the ImGui ID — without this, picking a
                    // native action changes the visible label, which
                    // changes the hashed ID, which collapses the header
                    // on the next frame.
                    char hdrId[80];
                    snprintf(hdrId, sizeof(hdrId), "%s###%s_h_%s",
                                  hdr, tag, kModSlugs[m]);
                    if (ImGui_CollapsingHeader(ctx, hdrId, nullptr, nullptr)) {
                        char modPrefix[32];
                        snprintf(modPrefix, sizeof(modPrefix), "%s_%s",
                                      tag, kModSlugs[m]);
                        if (drawSlotPicker(ctx, modPrefix, layer, id,
                                           slots[m], isLongCol,
                                           /*modIdx*/ m)) dirty = true;
                    }
                }
            }
            }   // if (renderSlots)
        }
        ImGui_EndChild(ctx);
    };

    drawColumn("SHORT PRESS", "sp", bd.shortPress, /*isLongCol*/ false);
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (longPressAvailable) {
        drawColumn("LONG PRESS", "lp", bd.longPress, /*isLongCol*/ true);
    } else {
        // Placeholder column so the editor's two panels stay balanced
        // for the two cases where long-press doesn't apply: this button
        // is itself a modifier, or it's the encoder rotate gesture.
        double w = colW, h = colH;
        int childFlags = ImGui_ChildFlags_Borders;
        if (ImGui_BeginChild(ctx, "lp_col_disabled", &w, &h, &childFlags, nullptr)) {
            ImGui_Text(ctx, "LONG PRESS");
            ImGui_Separator(ctx);
            if (isEncoder) {
                ImGui_TextDisabled(ctx,
                    "Long-press doesn't apply to encoder rotation.");
            } else {
                ImGui_TextDisabled(ctx,
                    "Long-press disabled — this button IS a modifier.");
            }
            if (bd.hasLongPress) { bd.hasLongPress = false; dirty = true; }
        }
        ImGui_EndChild(ctx);
    }

    ImGui_Spacing(ctx);
    ImGui_Separator(ctx);

    // ---- LED appearance ----
    // Two independent rows (Active / Inactive). Each row shows a single
    // colour swatch displaying the current value; clicking it opens a
    // popup with the 10 hardware-renderable colours from cap33 (SEL
    // DAW-Colour palette: red, orange, yellow, green, cyan, blue,
    // purple, magenta, pink, white). Brightness sits next to the
    // swatch as Off/Dim/Bright radios.
    //   Active   = lit-state (Toggle on / Hold held / Momentary press)
    //   Inactive = idle state
    ImGui_Text(ctx, "LED");
    ImGui_Spacing(ctx);

    auto drawLedRow = [&](const char* label,
                          uint8_t (&rgb)[3],
                          Brightness& bri,
                          const char* idTag) {
        ImGui_Text(ctx, label);
        ImGui_SameLine(ctx, nullptr, nullptr);
        const double labelColEnd = 80.0;
        ImGui_SetCursorPosX(ctx, labelColEnd);

        // Single swatch reflecting the currently-selected colour. Click
        // opens the palette popup. Slightly wider than tall so it reads
        // as a "field" rather than a single-cell pick.
        const int curRgba =
            (int(rgb[0]) << 24) |
            (int(rgb[1]) << 16) |
            (int(rgb[2]) <<  8) | 0xFF;
        char btnId[32];
        snprintf(btnId, sizeof(btnId), "##cur_%s", idTag);
        int btnFlags = 0;
        double bw = 56.0, bh = 22.0;
        if (ImGui_ColorButton(ctx, btnId, curRgba, &btnFlags, &bw, &bh)) {
            char popId[32];
            snprintf(popId, sizeof(popId), "palette_%s", idTag);
            ImGui_OpenPopup(ctx, popId, nullptr);
        }

        // Palette popup — 10 swatches in a 5x2 grid. Clicking a swatch
        // commits the colour and closes the popup.
        char popId[32];
        snprintf(popId, sizeof(popId), "palette_%s", idTag);
        if (ImGui_BeginPopup(ctx, popId, nullptr)) {
            int paletteCount = 0;
            const uf8::PaletteRgb* palette = uf8::selPaletteRgb(&paletteCount);
            const double sw = 26.0;
            const int perRow = 5;
            for (int i = 0; i < paletteCount; ++i) {
                const auto& p = palette[i];
                const int packed =
                    (int(p.r) << 24) |
                    (int(p.g) << 16) |
                    (int(p.b) <<  8) | 0xFF;
                char swId[32];
                snprintf(swId, sizeof(swId), "##pp_%s_%d", idTag, i);
                int swFlags = 0;
                double w = sw, h = sw;
                if (ImGui_ColorButton(ctx, swId, packed, &swFlags, &w, &h)) {
                    rgb[0] = p.r;
                    rgb[1] = p.g;
                    rgb[2] = p.b;
                    dirty = true;
                    ImGui_CloseCurrentPopup(ctx);
                }
                if ((i % perRow) != (perRow - 1) && i != paletteCount - 1)
                    ImGui_SameLine(ctx, nullptr, nullptr);
            }
            ImGui_EndPopup(ctx);
        }

        // Brightness radios — same row, after the swatch.
        ImGui_SameLine(ctx, nullptr, nullptr);
        ImGui_Text(ctx, "  ");
        ImGui_SameLine(ctx, nullptr, nullptr);
        const char* names[] = {"Off", "Dim", "Bright"};
        for (int i = 0; i < 3; ++i) {
            char idbuf[32];
            snprintf(idbuf, sizeof(idbuf), "%s##b_%s_%d",
                          names[i], idTag, i);
            if (ImGui_RadioButton(ctx, idbuf,
                                  static_cast<int>(bri) == i)) {
                bri = static_cast<Brightness>(i);
                dirty = true;
            }
            if (i < 2) ImGui_SameLine(ctx, nullptr, nullptr);
        }
    };

    drawLedRow("Active",   bd.color,         bd.brightness,         "act");
    drawLedRow("Inactive", bd.inactiveColor, bd.inactiveBrightness, "ina");

    ImGui_Spacing(ctx);
    // LED-when-empty override. Default is OFF — an unbound button stays
    // dark so the surface visibly shows what's available vs assigned.
    // Tick the checkbox to keep the configured Inactive colour glowing
    // even when the binding has no action (useful for hardware labels
    // / colour-coding rows that the user wants to keep visible).
    {
        bool en = bd.ledShowWhenEmpty;
        if (ImGui_Checkbox(ctx,
                "Show LED even when no action is assigned##ledforce",
                &en)) {
            bd.ledShowWhenEmpty = en;
            dirty = true;
        }
    }

    ImGui_Spacing(ctx);
    ImGui_Separator(ctx);
    bool cleared = false;
    if (ImGui_Button(ctx, "Clear binding (Do nothing)",
                     /*size_w*/ nullptr, /*size_h*/ nullptr)) {
        // Remove the entry entirely so the LED pusher's hasBinding
        // check returns false → cell falls back to its table-default
        // colour. Just resetting to Binding{} would leave a default-
        // valued entry that resolveLed_ now treats as "user touched
        // it" and renders white-Bright/white-Dim, hiding the firmware
        // default the user expects after a Clear.
        cleared = true;
        bd = Binding{};   // also reflect the cleared state in this frame's UI snapshot
    }

    popBindingsTheme(ctx, themePushed);

    if (cleared) clearBinding(layer, id);
    else if (dirty) setBinding(layer, id, bd);

    ImGui_PopID(ctx);
}

} // namespace

namespace {

// ---- User-Quick slot editor (per-slot, driven by TopSoftKey click) ------
// Edits ONE user-Quick slot at coordinates (editLayer, engaged Quick on
// editLayer, active Sub-Bank on editLayer, slotIdx). The Quick + Sub-
// Bank halves of the coordinate come from the live hardware state —
// the user already switched them by clicking Q1/Q2/Q3 + V-POT/Soft 1-5
// in the mockup above (those clicks engage on the hardware via the
// schematic-proxy dispatch). All the user does here is fill the slot.
//
// Layer 1 Q1/Q2 = SSL CS/BC are plug-in-driven; their slots are filled
// by the SSL plug-in's stock soft-key labels and not user-editable.
// Pop a clear notice instead of pretending an editor.
void drawUserQuickSlotEditor_(ImGui_Context* ctx, int editLayer, int slotIdx)
{
    using namespace uf8::bindings;
    if (editLayer < 0 || editLayer > 2)               return;
    if (slotIdx < 0 || slotIdx >= kSlotsPerSubBank)   return;

    const int liveQ  = reasixty_activeQuickFor(editLayer);
    const int liveSB = reasixty_activeSubBankFor(editLayer);

    const char* qLabels[3]  = { "Q1", "Q2", "Q3" };
    const char* sbLabels[6] = {
        "V-POT", "Soft 1", "Soft 2", "Soft 3", "Soft 4", "Soft 5"
    };

    // Layer 1 Q1/Q2 = SSL CS/BC. Engaged via domain_cs / domain_bc;
    // g_activeQuick stays at -1 on press, so we detect this state by
    // looking at the focused-domain. Slots are filled by the plug-in.
    const int engagedForDisplay = reasixty_engagedQuickFor(editLayer);
    const bool isSslCsBc = (editLayer == 0
                            && engagedForDisplay >= 0
                            && engagedForDisplay <= 1);
    if (isSslCsBc) {
        char hdr[160];
        snprintf(hdr, sizeof(hdr),
                      "Top Soft-Key %d   (Layer 1, %s)",
                      slotIdx + 1,
                      engagedForDisplay == 0 ? "Q1 = SSL CS"
                                             : "Q2 = SSL BC");
        ImGui_Text(ctx, hdr);
        ImGui_TextDisabled(ctx,
            "Layer 1 Q1 (SSL CS) and Q2 (SSL BC) auto-fill the top-soft-"
            "key row from the SSL plug-in's stock soft-key labels. "
            "Nothing to edit here — pick Q3 (or switch to Layer 2/3) "
            "for free user-Quick slots.");
        return;
    }
    if (liveQ < 0) {
        ImGui_Text(ctx, "(no Quick engaged on this layer)");
        ImGui_TextDisabled(ctx,
            "Click Q1, Q2, or Q3 in the mockup above to engage a Quick "
            "context, then click a top-soft-key tile to edit its slot. "
            "On Layer 1 you can engage Q1 (SSL CS), Q2 (SSL BC), or Q3 "
            "(user-fillable).");
        return;
    }

    const int  qIdx  = liveQ;
    const int  sbIdx = (liveSB < 0 || liveSB > 5) ? 0 : liveSB;
    const char* qName  = qLabels[qIdx];
    const char* sbName = sbLabels[sbIdx];

    Binding bd = getUserQuickSlot(editLayer, qIdx, sbIdx, slotIdx);
    auto& sp = bd.shortPress[static_cast<int>(Modifier::Plain)];

    // Full breadcrumb. Frank 2026-05-13: "selbstverständlich" that the
    // header carries every coordinate that defines what this slot is.
    char hdr[200];
    snprintf(hdr, sizeof(hdr),
                  "Editing: Top Soft-Key %d — %s   (Layer %d, %s)",
                  slotIdx + 1, sbName, editLayer + 1, qName);
    ImGui_Text(ctx, hdr);
    ImGui_Separator(ctx);
    ImGui_Spacing(ctx);

    ImGui_PushID(ctx, "uqslot_inline");
    char idtag[48];
    snprintf(idtag, sizeof(idtag), "uq%d_%d_%d_s%d",
                  editLayer, qIdx, sbIdx, slotIdx);
    ImGui_PushID(ctx, idtag);

    bool dirty = false;
    char labelBuf[64] = {0};
    std::strncpy(labelBuf, bd.label.c_str(), sizeof(labelBuf) - 1);
    double w = 240;
    ImGui_PushItemWidth(ctx, w);
    if (ImGui_InputTextWithHint(ctx, "Label##uqslotlabel_inl",
                                "shown on the top-soft-key LCD",
                                labelBuf, sizeof(labelBuf),
                                nullptr, nullptr)) {
        bd.label = labelBuf;
        dirty = true;
    }
    ImGui_PopItemWidth(ctx);

    // Slot's action picker. f.label = nullptr — the slot has only ONE
    // label (the binding's bd.label above), Frank's request 2026-05-13:
    // "für was Label UND display label? Label reicht."
    ActionFieldsRef ref{
        &sp.type, &sp.action, &sp.param,
        /*label*/ nullptr,
        &sp.midiDevice, &sp.midiChannel, &sp.midiMsgType,
        &sp.midiData1, &sp.midiData2,
        &sp.fireOnInactive,
    };
    if (drawActionPicker(ctx, idtag, ref,
                         /*layer*/ -1, ButtonId::None,
                         /*isLongPress*/ false,
                         /*modIdx*/ 0, /*stepIdx*/ 0,
                         /*uqLayer*/ editLayer, /*uqQuick*/ qIdx,
                         /*uqSubBank*/ sbIdx, /*uqSlot*/ slotIdx)) {
        dirty = true;
    }
    if (ImGui_Button(ctx, "Clear slot##uqclr_inl",
                     /*size_w*/ nullptr, /*size_h*/ nullptr)) {
        bd = Binding{};
        dirty = true;
    }

    // LED appearance for this slot. Same Active / Inactive split the
    // regular binding editor exposes, scoped to this user-Quick slot.
    ImGui_Spacing(ctx);
    ImGui_Separator(ctx);
    ImGui_Spacing(ctx);
    ImGui_Text(ctx, "LED");
    ImGui_TextDisabled(ctx,
        "Active = slot's bound action is engaged. Inactive = idle.");
    ImGui_Spacing(ctx);

    auto drawSlotLedRow = [&](const char* label, uint8_t (&rgb)[3],
                              Brightness& bri, const char* idTag) {
        ImGui_Text(ctx, label);
        ImGui_SameLine(ctx, nullptr, nullptr);
        ImGui_SetCursorPosX(ctx, 80.0);
        const int curRgba =
            (int(rgb[0]) << 24) | (int(rgb[1]) << 16)
          | (int(rgb[2]) <<  8) | 0xFF;
        char btnId[48];
        snprintf(btnId, sizeof(btnId), "##uqled_cur_%s", idTag);
        int btnFlags = 0;
        double bw = 56.0, bh = 22.0;
        if (ImGui_ColorButton(ctx, btnId, curRgba, &btnFlags, &bw, &bh)) {
            char popId[48];
            snprintf(popId, sizeof(popId), "uqled_palette_%s", idTag);
            ImGui_OpenPopup(ctx, popId, nullptr);
        }
        char popId[48];
        snprintf(popId, sizeof(popId), "uqled_palette_%s", idTag);
        if (ImGui_BeginPopup(ctx, popId, nullptr)) {
            int paletteCount = 0;
            const uf8::PaletteRgb* palette = uf8::selPaletteRgb(&paletteCount);
            const double sw = 26.0;
            const int perRow = 5;
            for (int i = 0; i < paletteCount; ++i) {
                const auto& p = palette[i];
                const int packed =
                    (int(p.r) << 24) | (int(p.g) << 16)
                  | (int(p.b) <<  8) | 0xFF;
                char swId[48];
                snprintf(swId, sizeof(swId), "##uqled_pp_%s_%d",
                              idTag, i);
                int swFlags = 0;
                double w = sw, h = sw;
                if (ImGui_ColorButton(ctx, swId, packed, &swFlags, &w, &h)) {
                    rgb[0] = p.r; rgb[1] = p.g; rgb[2] = p.b;
                    dirty = true;
                    ImGui_CloseCurrentPopup(ctx);
                }
                if ((i % perRow) != (perRow - 1) && i != paletteCount - 1)
                    ImGui_SameLine(ctx, nullptr, nullptr);
            }
            ImGui_EndPopup(ctx);
        }
        ImGui_SameLine(ctx, nullptr, nullptr);
        ImGui_Text(ctx, "  ");
        ImGui_SameLine(ctx, nullptr, nullptr);
        const char* names[] = {"Off", "Dim", "Bright"};
        for (int i = 0; i < 3; ++i) {
            char idbuf[48];
            snprintf(idbuf, sizeof(idbuf), "%s##uqled_b_%s_%d",
                          names[i], idTag, i);
            if (ImGui_RadioButton(ctx, idbuf,
                                  static_cast<int>(bri) == i)) {
                bri = static_cast<Brightness>(i);
                dirty = true;
            }
            if (i < 2) ImGui_SameLine(ctx, nullptr, nullptr);
        }
    };
    char tagA[24], tagI[24];
    snprintf(tagA, sizeof(tagA), "act_%s",  idtag);
    snprintf(tagI, sizeof(tagI), "inact_%s", idtag);
    drawSlotLedRow("Active",   bd.color,         bd.brightness,         tagA);
    drawSlotLedRow("Inactive", bd.inactiveColor, bd.inactiveBrightness, tagI);

    if (dirty) {
        setUserQuickSlot(editLayer, qIdx, sbIdx, slotIdx, bd);
    }
    ImGui_PopID(ctx);   // idtag
    ImGui_PopID(ctx);   // "uqslot_inline"
}

// ---- User-Quick slot editor (OLD section-style) — UNUSED ---------------
// Kept under #if 0 for one revision until Frank confirms the new TopSoft-
// Key-click flow lands cleanly. Remove on the next commit.
#if 0
void drawUserQuickSection_(ImGui_Context* ctx, int editLayer,
                           uf8::bindings::ButtonId selected)
{
    using namespace uf8::bindings;

    static int s_editQuick   = -1;  // -1 = uninitialised; 0..2 = Q1/Q2/Q3
    static int s_editSubBank = 0;   // 0..5 (V-POT, Soft 1..5)

    if (editLayer < 0 || editLayer > 2) return;

    // WYSIWYG: schematic-click on a Quick button pivots the editor to
    // that Quick. Keeps the section in lock-step with what the user
    // visually selected up there.
    if      (selected == ButtonId::Quick1) s_editQuick = 0;
    else if (selected == ButtonId::Quick2) s_editQuick = 1;
    else if (selected == ButtonId::Quick3) s_editQuick = 2;
    if (s_editQuick < 0) s_editQuick = (editLayer == 0) ? 2 : 0;

    // Live state on this layer's hardware right now.
    const int liveQuick   = reasixty_activeQuickFor(editLayer);
    const int liveSubBank = reasixty_activeSubBankFor(editLayer);

    const char* qLabels[3]  = { "Q1", "Q2", "Q3" };
    const char* sbLabels[6] = {
        "V-POT", "Soft 1", "Soft 2", "Soft 3", "Soft 4", "Soft 5"
    };

    // Header + live badge -----------------------------------------------
    char hdr[160];
    snprintf(hdr, sizeof(hdr),
                  "User-Quick slots — Layer %d", editLayer + 1);
    ImGui_Text(ctx, hdr);

    {
        char liveBadge[200];
        const char* qLive = liveQuick < 0
            ? "none (plug-in driven)"
            : (liveQuick == 0 ? "Q1" : liveQuick == 1 ? "Q2" : "Q3");
        const char* sbLive = sbLabels[
            (liveSubBank < 0 || liveSubBank > 5) ? 0 : liveSubBank];
        snprintf(liveBadge, sizeof(liveBadge),
                      "Live on Layer %d hardware:   Quick = %s   |   "
                      "Sub-bank = %s",
                      editLayer + 1, qLive, sbLive);
        ImGui_TextColored(ctx, 0x55DD77FF, liveBadge);
    }
    if (editLayer == 0) {
        ImGui_TextDisabled(ctx,
            "Layer 1: Q1/Q2 are hardcoded SSL CS/BC focus and have no "
            "user-Quick slots — Q3 is free for user actions.");
    } else {
        ImGui_TextDisabled(ctx,
            "Each Quick (Q1/Q2/Q3) carries 6 sub-banks (V-POT + Soft 1-5). "
            "Each sub-bank holds 8 top-soft-key slots.");
    }
    ImGui_Spacing(ctx);

    // Quick selector — radio across Q1/Q2/Q3. Layer 1 Q1/Q2 disabled.
    // Dot marker (●) on the radio of the Quick that's live-engaged on
    // this layer's hardware right now.
    {
        ImGui_Text(ctx, "Edit Quick:");
        ImGui_SameLine(ctx, nullptr, nullptr);
        for (int qi = 0; qi < 3; ++qi) {
            ImGui_SameLine(ctx, nullptr, nullptr);
            const bool locked = (editLayer == 0 && qi <= 1);
            if (locked) {
                char tag[16];
                snprintf(tag, sizeof(tag), "[%s]", qLabels[qi]);
                ImGui_TextDisabled(ctx, tag);
                continue;
            }
            const bool isLive = (liveQuick == qi);
            char tag[32];
            snprintf(tag, sizeof(tag), "%s%s##quick_pick_%d",
                          qLabels[qi], isLive ? "  \xE2\x97\x8F" : "", qi);
            if (ImGui_RadioButton(ctx, tag, s_editQuick == qi)) {
                s_editQuick = qi;
            }
        }
    }
    // If the user lands on Layer 1 with s_editQuick still pointing at
    // Q1/Q2 (carried over from another layer), nudge to Q3.
    if (editLayer == 0 && s_editQuick <= 1) s_editQuick = 2;

    // The Layer-1 Q1/Q2 user-Quick slots have no path to fire (the
    // buttons trigger domain_cs/bc, not quick_select_*), so editing
    // them would be pure cosmetics. Bail out with an explanation.
    const bool unreachable = (editLayer == 0 && s_editQuick <= 1);
    if (unreachable) {
        ImGui_Spacing(ctx);
        ImGui_TextDisabled(ctx,
            "Layer 1 Q1 = SSL CS focus, Q2 = SSL BC focus. Pressing "
            "them doesn't engage a user-Quick context. Pick Q3 above "
            "to edit Layer 1's user-Quick slots.");
        return;
    }

    // Sub-bank selector — same dot rule.
    {
        ImGui_Spacing(ctx);
        ImGui_Text(ctx, "Edit sub-bank:");
        ImGui_SameLine(ctx, nullptr, nullptr);
        for (int bi = 0; bi < 6; ++bi) {
            ImGui_SameLine(ctx, nullptr, nullptr);
            const bool isLive = (liveSubBank == bi
                                  && liveQuick == s_editQuick);
            char tag[40];
            snprintf(tag, sizeof(tag), "%s%s##subbank_pick_%d",
                          sbLabels[bi],
                          isLive ? "  \xE2\x97\x8F" : "", bi);
            if (ImGui_RadioButton(ctx, tag, s_editSubBank == bi)) {
                s_editSubBank = bi;
            }
        }
    }
    ImGui_Spacing(ctx);
    ImGui_Separator(ctx);
    ImGui_Spacing(ctx);

    // 8 slot editors with full breadcrumb in the header + a clear
    // "Editing:" line inside each expanded slot.
    const char* qName  = qLabels[s_editQuick];
    const char* sbName = sbLabels[s_editSubBank];
    for (int si = 0; si < kSlotsPerSubBank; ++si) {
        Binding bd = getUserQuickSlot(editLayer, s_editQuick,
                                      s_editSubBank, si);
        char idtag[40];
        snprintf(idtag, sizeof(idtag), "uq%d_%d_%d_slot%d",
                      editLayer, s_editQuick, s_editSubBank, si);
        ImGui_PushID(ctx, idtag);

        auto& sp = bd.shortPress[static_cast<int>(Modifier::Plain)];

        // `###` suffix: header ID stays stable while visible text
        // changes mid-edit. Same trap that bit the old bank editor.
        char header[200];
        const std::string& act = sp.action;
        const char* visText = act.empty()
            ? "(empty)"
            : (bd.label.empty() ? act.c_str() : bd.label.c_str());
        snprintf(header, sizeof(header),
                      "Slot %d  —  %s (Layer %d, %s):   %s###uqslot%d",
                      si + 1, sbName, editLayer + 1, qName,
                      visText, si);
        if (ImGui_CollapsingHeader(ctx, header, nullptr, nullptr)) {
            ImGui_Indent(ctx, nullptr);

            // Echo the full path so the user can never mistake what
            // context they're editing — the per-slot breadcrumb is
            // load-bearing (Frank: "selbstverständlich", 2026-05-13).
            char editingLine[200];
            snprintf(editingLine, sizeof(editingLine),
                          "Editing: %s slot %d   (Layer %d, %s)",
                          sbName, si + 1, editLayer + 1, qName);
            ImGui_Text(ctx, editingLine);
            ImGui_Spacing(ctx);

            bool dirty = false;
            char labelBuf[64] = {0};
            std::strncpy(labelBuf, bd.label.c_str(), sizeof(labelBuf) - 1);
            double w = 200;
            ImGui_PushItemWidth(ctx, w);
            if (ImGui_InputTextWithHint(ctx, "Label##uqslotlabel",
                                        "shown on the top-soft-key LCD",
                                        labelBuf, sizeof(labelBuf),
                                        nullptr, nullptr)) {
                bd.label = labelBuf;
                dirty = true;
            }
            ImGui_PopItemWidth(ctx);

            ActionFieldsRef ref{
                &sp.type, &sp.action, &sp.param, &sp.label,
                &sp.midiDevice, &sp.midiChannel, &sp.midiMsgType,
                &sp.midiData1, &sp.midiData2,
                &sp.fireOnInactive,
            };
            char prefix[48];
            snprintf(prefix, sizeof(prefix), "uq%d_%d_%d_s%d",
                          editLayer, s_editQuick, s_editSubBank, si);
            if (drawActionPicker(ctx, prefix, ref,
                                 /*layer*/ -1, ButtonId::None,
                                 /*isLongPress*/ false)) {
                dirty = true;
            }
            if (ImGui_Button(ctx, "Clear slot##uqclr",
                             /*size_w*/ nullptr, /*size_h*/ nullptr)) {
                bd = Binding{};
                dirty = true;
            }
            ImGui_Unindent(ctx, nullptr);

            if (dirty) {
                setUserQuickSlot(editLayer, s_editQuick,
                                 s_editSubBank, si, bd);
            }
        }
        ImGui_PopID(ctx);
    }
}
#endif

// ---- Sub-Bank cell editor (V-POT / Soft 1-5 in the mockup) --------------
// Replaces the regular drawBindingEditor for these cells — they don't
// carry a user-editable action (the binding is always
// softkey_bank_select with a fixed param). Shows what's selected +
// where to go next (click a Top-Soft-Key to configure a slot) + the
// per-(Layer, Quick) LED colour override. Frank 2026-05-13:
// "Dort soll nichts eingestellt werden können, sondern einfach
// stehen: Click Soft-Keys 1-8 to configure Soft-Key Bank N.
// LED-optionen einblenden für V-POT (oder whatever selected)".
void drawSubBankCellEditor_(ImGui_Context* ctx, int editLayer,
                            uf8::bindings::ButtonId id)
{
    using namespace uf8::bindings;
    int sbIdx = -1;
    switch (id) {
        case ButtonId::VPotBank:     sbIdx = 0; break;
        case ButtonId::SoftKey1Bank: sbIdx = 1; break;
        case ButtonId::SoftKey2Bank: sbIdx = 2; break;
        case ButtonId::SoftKey3Bank: sbIdx = 3; break;
        case ButtonId::SoftKey4Bank: sbIdx = 4; break;
        case ButtonId::SoftKey5Bank: sbIdx = 5; break;
        default: return;
    }
    if (editLayer < 0 || editLayer > 2) return;

    const char* sbLabels[6] = {
        "V-POT", "Soft 1", "Soft 2", "Soft 3", "Soft 4", "Soft 5"
    };
    const int engagedQ = reasixty_activeQuickFor(editLayer);

    // ---- Header + navigation hint -------------------------------------
    char hdr[160];
    if (engagedQ >= 0) {
        snprintf(hdr, sizeof(hdr),
                      "Editing: %s   (Layer %d, Quick %d)",
                      sbLabels[sbIdx], editLayer + 1, engagedQ + 1);
    } else {
        snprintf(hdr, sizeof(hdr),
                      "Editing: %s   (Layer %d — no Quick engaged)",
                      sbLabels[sbIdx], editLayer + 1);
    }
    ImGui_Text(ctx, hdr);
    ImGui_Separator(ctx);
    ImGui_Spacing(ctx);

    if (engagedQ < 0) {
        ImGui_TextDisabled(ctx,
            "Click Q1, Q2 or Q3 in the mockup above to engage a Quick "
            "on this Layer first. Then click any of the 8 Top-Soft-Keys "
            "to configure this Sub-Bank's slots, or stay here to set "
            "this button's per-Quick LED colours.");
        return;
    }

    char nav[160];
    snprintf(nav, sizeof(nav),
                  "Click any of the 8 Top-Soft-Keys above to configure "
                  "this Sub-Bank's slots (Layer %d, Quick %d, %s).",
                  editLayer + 1, engagedQ + 1, sbLabels[sbIdx]);
    ImGui_Text(ctx, nav);
    ImGui_Spacing(ctx);
    ImGui_Separator(ctx);
    ImGui_Spacing(ctx);

    // ---- Soft-Key Bank preset save/recall -----------------------------
    // Snapshot all 8 top-soft-key slots in the current (L, Q, SB)
    // tuple as a named preset; recall a preset into any (L, Q, SB).
    // LED-override is NOT part of the preset — it belongs to the
    // engaged-Quick context, not the bank's identity.
    {
        char presetHdr[160];
        snprintf(presetHdr, sizeof(presetHdr),
                      "Preset for %s   (Layer %d, Quick %d)",
                      sbLabels[sbIdx], editLayer + 1, engagedQ + 1);
        ImGui_Text(ctx, presetHdr);
        ImGui_TextDisabled(ctx,
            "Snapshot the 8 Top-Soft-Key slots in this Sub-Bank as a "
            "named preset, or recall one into this Sub-Bank.");
        ImGui_Spacing(ctx);

        // Per (L, Q, SB) selection — different sub-bank cells keep
        // their own combo state so navigating between them doesn't
        // reset the user's choice.
        static int s_selPreset[3][kQuicksPerLayer][kSubBanksPerQuick]
            = {};
        int& selRef = s_selPreset[editLayer][engagedQ][sbIdx];

        const int nPresets = bankPresetCount();
        if (selRef >= nPresets) selRef = -1;
        if (selRef < -1)        selRef = -1;

        std::string preview = "(none)";
        if (selRef >= 0 && selRef < nPresets) {
            preview = bankPresetAt(selRef).name;
        }

        double comboW = 240;
        ImGui_PushItemWidth(ctx, comboW);
        if (ImGui_BeginCombo(ctx, "##bp_combo", preview.c_str(),
                             nullptr)) {
            bool selNone = (selRef < 0);
            if (ImGui_Selectable(ctx, "(none)##bp_none", &selNone,
                                 nullptr, nullptr, nullptr)) {
                selRef = -1;
            }
            for (int i = 0; i < nPresets; ++i) {
                SoftKeyBankPreset p = bankPresetAt(i);
                char rowId[160];
                snprintf(rowId, sizeof(rowId), "%s##bp_row_%d",
                              p.name.c_str(), i);
                bool sel = (i == selRef);
                if (ImGui_Selectable(ctx, rowId, &sel, nullptr,
                                     nullptr, nullptr)) {
                    selRef = i;
                }
            }
            ImGui_EndCombo(ctx);
        }
        ImGui_PopItemWidth(ctx);

        const bool hasSel = (selRef >= 0 && selRef < nPresets);

        // Deferred-open: button click only sets s_pendingOp; the
        // OpenPopup call lives at the bottom of this block so the
        // ID-stack matches the BeginPopupModal site exactly. Same
        // pattern the FX-Learn editor uses for its mode/del popups.
        static char s_nameBuf[64] = {};
        enum Op { OpNone, OpRecall, OpSaveAs, OpRename, OpDelete };
        static int s_pendingOp  = OpNone;
        static int s_pendingIdx = -1;

        ImGui_SameLine(ctx, nullptr, nullptr);
        if (ImGui_Button(ctx, "Save as…##bp_saveas",
                         nullptr, nullptr)) {
            s_nameBuf[0] = '\0';
            s_pendingOp  = OpSaveAs;
        }
        if (hasSel) {
            ImGui_SameLine(ctx, nullptr, nullptr);
            if (ImGui_Button(ctx, "Recall##bp_recall",
                             nullptr, nullptr)) {
                s_pendingOp  = OpRecall;
                s_pendingIdx = selRef;
            }
            ImGui_SameLine(ctx, nullptr, nullptr);
            if (ImGui_Button(ctx, "Rename…##bp_rename",
                             nullptr, nullptr)) {
                SoftKeyBankPreset p = bankPresetAt(selRef);
                std::strncpy(s_nameBuf, p.name.c_str(),
                             sizeof(s_nameBuf) - 1);
                s_nameBuf[sizeof(s_nameBuf) - 1] = '\0';
                s_pendingOp  = OpRename;
                s_pendingIdx = selRef;
            }
            ImGui_SameLine(ctx, nullptr, nullptr);
            if (ImGui_Button(ctx, "Delete##bp_delete",
                             nullptr, nullptr)) {
                s_pendingOp  = OpDelete;
                s_pendingIdx = selRef;
            }
        } else {
            ImGui_SameLine(ctx, nullptr, nullptr);
            ImGui_TextDisabled(ctx,
                "  (pick a preset to Recall / Rename / Delete)");
        }

        if (s_pendingOp == OpRecall)
            ImGui_OpenPopup(ctx, "Recall preset?###bp_recall_confirm",
                            nullptr);
        else if (s_pendingOp == OpSaveAs)
            ImGui_OpenPopup(ctx, "Save preset###bp_save_as",   nullptr);
        else if (s_pendingOp == OpRename)
            ImGui_OpenPopup(ctx, "Rename preset###bp_rename",  nullptr);
        else if (s_pendingOp == OpDelete)
            ImGui_OpenPopup(ctx, "Delete preset?###bp_delete_confirm",
                            nullptr);
        s_pendingOp = OpNone;

        // Fixed popup width so wrapped text doesn't collapse the modal
        // to a 2-pixel tower (same trick as fxl_mode_popup).
        const double kBpPopupW = 420.0;
        int condAlways = ImGui_Cond_Always;

        // Helper: trim leading/trailing spaces from s_nameBuf.
        auto trimmedName = []() {
            std::string t = s_nameBuf;
            while (!t.empty() && t.back()  == ' ') t.pop_back();
            while (!t.empty() && t.front() == ' ') t.erase(t.begin());
            return t;
        };

        // ---- Recall confirm -------------------------------------
        ImGui_SetNextWindowSize(ctx, kBpPopupW, 0.0, &condAlways);
        if (ImGui_BeginPopupModal(ctx,
                                  "Recall preset?###bp_recall_confirm",
                                  nullptr, nullptr)) {
            SoftKeyBankPreset p = bankPresetAt(s_pendingIdx);
            char line[256];
            snprintf(line, sizeof(line),
                "Overwrite the 8 slots of %s on (Layer %d, Quick %d) "
                "with preset '%s'?",
                sbLabels[sbIdx], editLayer + 1, engagedQ + 1,
                p.name.c_str());
            ImGui_TextWrapped(ctx, line);
            ImGui_Spacing(ctx);
            if (ImGui_Button(ctx, "Recall##bp_recall_ok",
                             nullptr, nullptr)) {
                recallBankPreset(s_pendingIdx, editLayer, engagedQ,
                                 sbIdx);
                ImGui_CloseCurrentPopup(ctx);
            }
            ImGui_SameLine(ctx, nullptr, nullptr);
            if (ImGui_Button(ctx, "Cancel##bp_recall_cancel",
                             nullptr, nullptr)) {
                ImGui_CloseCurrentPopup(ctx);
            }
            ImGui_EndPopup(ctx);
        }

        // ---- Save as … ------------------------------------------
        ImGui_SetNextWindowSize(ctx, kBpPopupW, 0.0, &condAlways);
        if (ImGui_BeginPopupModal(ctx, "Save preset###bp_save_as",
                                  nullptr, nullptr)) {
            ImGui_Text(ctx, "Save current 8 slots as preset");
            ImGui_Spacing(ctx);
            ImGui_PushItemWidth(ctx, 280);
            ImGui_InputTextWithHint(ctx, "Name##bp_saveas_name",
                "e.g. 'Drum compression'",
                s_nameBuf, static_cast<int>(sizeof(s_nameBuf)),
                nullptr, nullptr);
            ImGui_PopItemWidth(ctx);
            ImGui_Spacing(ctx);
            std::string nm = trimmedName();
            const bool nameOk = !nm.empty();
            const int  existing = nameOk ? findBankPreset(nm) : -1;
            if (!nameOk) {
                ImGui_TextDisabled(ctx, "Enter a name.");
            } else if (existing >= 0) {
                ImGui_TextDisabled(ctx,
                    "A preset with that name exists — Save will "
                    "overwrite it.");
            } else {
                ImGui_TextDisabled(ctx, "New preset.");
            }
            ImGui_Spacing(ctx);
            if (nameOk) {
                const char* label = (existing >= 0)
                    ? "Save (overwrite)##bp_saveas_ok"
                    : "Save##bp_saveas_ok";
                if (ImGui_Button(ctx, label, nullptr, nullptr)) {
                    saveBankPreset(nm, editLayer, engagedQ, sbIdx);
                    selRef = findBankPreset(nm);
                    ImGui_CloseCurrentPopup(ctx);
                }
            } else {
                ImGui_TextDisabled(ctx, "Save");
            }
            ImGui_SameLine(ctx, nullptr, nullptr);
            if (ImGui_Button(ctx, "Cancel##bp_saveas_cancel",
                             nullptr, nullptr)) {
                ImGui_CloseCurrentPopup(ctx);
            }
            ImGui_EndPopup(ctx);
        }

        // ---- Rename ---------------------------------------------
        ImGui_SetNextWindowSize(ctx, kBpPopupW, 0.0, &condAlways);
        if (ImGui_BeginPopupModal(ctx, "Rename preset###bp_rename",
                                  nullptr, nullptr)) {
            ImGui_Text(ctx, "Rename preset");
            ImGui_Spacing(ctx);
            ImGui_PushItemWidth(ctx, 280);
            ImGui_InputTextWithHint(ctx, "Name##bp_rename_name",
                "preset name",
                s_nameBuf, static_cast<int>(sizeof(s_nameBuf)),
                nullptr, nullptr);
            ImGui_PopItemWidth(ctx);
            ImGui_Spacing(ctx);
            std::string nm = trimmedName();
            const bool nameOk = !nm.empty();
            const int  dup    = nameOk ? findBankPreset(nm) : -1;
            const bool valid  =
                nameOk && (dup < 0 || dup == s_pendingIdx);
            if (!nameOk) {
                ImGui_TextDisabled(ctx, "Enter a name.");
            } else if (!valid) {
                ImGui_TextDisabled(ctx,
                    "Another preset already has that name.");
            }
            ImGui_Spacing(ctx);
            if (valid) {
                if (ImGui_Button(ctx, "Rename##bp_rename_ok",
                                 nullptr, nullptr)) {
                    renameBankPreset(s_pendingIdx, nm);
                    ImGui_CloseCurrentPopup(ctx);
                }
            } else {
                ImGui_TextDisabled(ctx, "Rename");
            }
            ImGui_SameLine(ctx, nullptr, nullptr);
            if (ImGui_Button(ctx, "Cancel##bp_rename_cancel",
                             nullptr, nullptr)) {
                ImGui_CloseCurrentPopup(ctx);
            }
            ImGui_EndPopup(ctx);
        }

        // ---- Delete confirm -------------------------------------
        ImGui_SetNextWindowSize(ctx, kBpPopupW, 0.0, &condAlways);
        if (ImGui_BeginPopupModal(ctx,
                                  "Delete preset?###bp_delete_confirm",
                                  nullptr, nullptr)) {
            SoftKeyBankPreset p = bankPresetAt(s_pendingIdx);
            char line[256];
            snprintf(line, sizeof(line),
                "Delete preset '%s'? This cannot be undone.",
                p.name.c_str());
            ImGui_TextWrapped(ctx, line);
            ImGui_Spacing(ctx);
            if (ImGui_Button(ctx, "Delete##bp_delete_ok",
                             nullptr, nullptr)) {
                deleteBankPreset(s_pendingIdx);
                if (selRef >= bankPresetCount()) selRef = -1;
                ImGui_CloseCurrentPopup(ctx);
            }
            ImGui_SameLine(ctx, nullptr, nullptr);
            if (ImGui_Button(ctx, "Cancel##bp_delete_cancel",
                             nullptr, nullptr)) {
                ImGui_CloseCurrentPopup(ctx);
            }
            ImGui_EndPopup(ctx);
        }
    }
    ImGui_Spacing(ctx);
    ImGui_Separator(ctx);
    ImGui_Spacing(ctx);

    // ---- Per-(Layer, Quick) LED override -----------------------------
    char ledHdr[160];
    snprintf(ledHdr, sizeof(ledHdr),
                  "LED colours for %s on (Layer %d, Quick %d)",
                  sbLabels[sbIdx], editLayer + 1, engagedQ + 1);
    ImGui_Text(ctx, ledHdr);
    ImGui_TextDisabled(ctx,
        "Active = this Sub-Bank is the selected one. Inactive = "
        "another Sub-Bank is selected in this Quick.");
    ImGui_Spacing(ctx);

    SubBankLed app = getSubBankLed(editLayer, engagedQ, sbIdx);
    bool dirty = false;

    auto drawRow = [&](const char* label, uint8_t (&rgb)[3],
                       Brightness& bri, const char* idTag) {
        ImGui_Text(ctx, label);
        ImGui_SameLine(ctx, nullptr, nullptr);
        ImGui_SetCursorPosX(ctx, 80.0);
        const int curRgba =
            (int(rgb[0]) << 24) | (int(rgb[1]) << 16)
          | (int(rgb[2]) <<  8) | 0xFF;
        char btnId[40];
        snprintf(btnId, sizeof(btnId), "##sbov_cur_%s", idTag);
        int btnFlags = 0;
        double bw = 56.0, bh = 22.0;
        if (ImGui_ColorButton(ctx, btnId, curRgba, &btnFlags, &bw, &bh)) {
            char popId[40];
            snprintf(popId, sizeof(popId), "sbov_palette_%s", idTag);
            ImGui_OpenPopup(ctx, popId, nullptr);
        }
        char popId[40];
        snprintf(popId, sizeof(popId), "sbov_palette_%s", idTag);
        if (ImGui_BeginPopup(ctx, popId, nullptr)) {
            int paletteCount = 0;
            const uf8::PaletteRgb* palette = uf8::selPaletteRgb(&paletteCount);
            const double sw = 26.0;
            const int perRow = 5;
            for (int i = 0; i < paletteCount; ++i) {
                const auto& p = palette[i];
                const int packed =
                    (int(p.r) << 24) | (int(p.g) << 16)
                  | (int(p.b) <<  8) | 0xFF;
                char swId[40];
                snprintf(swId, sizeof(swId), "##sbov_pp_%s_%d",
                              idTag, i);
                int swFlags = 0;
                double w = sw, h = sw;
                if (ImGui_ColorButton(ctx, swId, packed, &swFlags, &w, &h)) {
                    rgb[0] = p.r; rgb[1] = p.g; rgb[2] = p.b;
                    dirty = true;
                    ImGui_CloseCurrentPopup(ctx);
                }
                if ((i % perRow) != (perRow - 1) && i != paletteCount - 1)
                    ImGui_SameLine(ctx, nullptr, nullptr);
            }
            ImGui_EndPopup(ctx);
        }
        ImGui_SameLine(ctx, nullptr, nullptr);
        ImGui_Text(ctx, "  ");
        ImGui_SameLine(ctx, nullptr, nullptr);
        const char* names[] = {"Off", "Dim", "Bright"};
        for (int i = 0; i < 3; ++i) {
            char idbuf[40];
            snprintf(idbuf, sizeof(idbuf), "%s##sbov_b_%s_%d",
                          names[i], idTag, i);
            if (ImGui_RadioButton(ctx, idbuf,
                                  static_cast<int>(bri) == i)) {
                bri = static_cast<Brightness>(i);
                dirty = true;
            }
            if (i < 2) ImGui_SameLine(ctx, nullptr, nullptr);
        }
    };
    char tagA[16], tagI[16];
    snprintf(tagA, sizeof(tagA), "act_L%d_Q%d_S%d",
                  editLayer, engagedQ, sbIdx);
    snprintf(tagI, sizeof(tagI), "inact_L%d_Q%d_S%d",
                  editLayer, engagedQ, sbIdx);
    drawRow("Active",   app.color,         app.brightness,         tagA);
    drawRow("Inactive", app.inactiveColor, app.inactiveBrightness, tagI);

    if (dirty) setSubBankLed(editLayer, engagedQ, sbIdx, app);
}

} // namespace

// Phase C UI — hardware-schematic view. Click a button on the schematic
// to select it; the editor below reveals the binding details. Auto-saves
// on every change (USB worker picks up the new binding on next press
// through dispatch's lock-protected lookup).
//
// Per-strip Sel/Cut/Solo/Rec, V-Pot push, top soft-keys, and soft-key
// bank selectors are shown greyed/locked — they stay hardcoded in v1
// (resolved Q2). Phase D widens the catalogue.
void SettingsScreen::drawBindings(ImGui_Context* ctx)
{
    using namespace uf8::bindings;

    // The editor's "current layer" follows the live hardware layer at
    // all times — the user changes layer exclusively by clicking the
    // Layer 1/2/3 buttons in the mockup below (which fire the
    // layer_select_* bindings via the schematic-proxy click). No
    // separate tab strip; the mockup's green outline ring around the
    // active Layer button is the single source of truth.
    const int       s_editLayer = getActiveLayer();
    static ButtonId s_selected  = ButtonId::None;

    // ---- Hardware schematic (vector, mirrors SSL UF8 page-14 layout) ----
    // Click → selects the button for editing AND, for Layer / Quick /
    // Sub-Bank tiles, dispatches the binding so the hardware engages
    // the same as a real press would. WYSIWYG end-to-end.
    //
    // UF8 / UC1 split tabs: each device has its own bindable surface.
    // The editor + admin row below are SHARED — they accept any
    // ButtonId from either device, so switching tabs just changes
    // which schematic you click on. s_selected is preserved across
    // tab switches so the editor stays open on the last-clicked
    // button.
    int tabBarFlags = 0;
    if (ImGui_BeginTabBar(ctx, "bindings_surface_tabs", &tabBarFlags)) {
        if (ImGui_BeginTabItem(ctx, "UF8", nullptr, nullptr)) {
            drawUf8Vector(ctx, s_selected);
            ImGui_EndTabItem(ctx);
        }
        if (ImGui_BeginTabItem(ctx, "UC1", nullptr, nullptr)) {
            drawUc1BindingsVector(ctx, s_selected);
            ImGui_EndTabItem(ctx);
        }
        ImGui_EndTabBar(ctx);
    }

    ImGui_Spacing(ctx);
    ImGui_Separator(ctx);
    ImGui_Spacing(ctx);

    // ---- Editor — branches on what the user clicked ---------------------
    // A top-soft-key tile pivots to the user-Quick slot editor for the
    // live (Layer, Quick, Sub-Bank, slot) combination. Everything else
    // goes through the regular per-button binding editor.
    const bool isTopSoftKey =
        s_selected >= ButtonId::TopSoftKey1
        && s_selected <= ButtonId::TopSoftKey8;
    if (s_selected == ButtonId::None) {
        ImGui_Text(ctx,
            "Click a button in the mockup above to edit its binding. "
            "Click Layer 1/2/3 + Q1/Q2/Q3 + V-POT/Soft 1-5 to switch "
            "what's live on the hardware; click a top-soft-key tile to "
            "edit the slot at that (Layer, Quick, Sub-Bank) coordinate.");
    } else if (isTopSoftKey) {
        const int slotIdx =
            static_cast<int>(s_selected)
            - static_cast<int>(ButtonId::TopSoftKey1);
        drawUserQuickSlotEditor_(ctx, s_editLayer, slotIdx);
    } else {
        const bool isSubBankCell =
            s_selected == ButtonId::VPotBank
         || s_selected == ButtonId::SoftKey1Bank
         || s_selected == ButtonId::SoftKey2Bank
         || s_selected == ButtonId::SoftKey3Bank
         || s_selected == ButtonId::SoftKey4Bank
         || s_selected == ButtonId::SoftKey5Bank;
        if (isSubBankCell) {
            // Sub-bank selectors don't carry a user-editable action —
            // they pick which of the 6 sub-banks renders into the 8
            // top-soft-key slots. The editor below shows what's
            // selected + a Per-Quick LED override so the user can
            // distinguish (L, Q) contexts visually. Frank 2026-05-13.
            drawSubBankCellEditor_(ctx, s_editLayer, s_selected);
        } else {
            drawBindingEditor(ctx, s_editLayer, s_selected);
        }
    }

    ImGui_Spacing(ctx);
    ImGui_Separator(ctx);
    ImGui_Spacing(ctx);

    // ---- Per-layer admin row (reset / save / load) -----------------
    if (ImGui_Button(ctx, "Reset this layer to factory defaults",
                     /*size_w*/ nullptr, /*size_h*/ nullptr)) {
        resetLayerToDefaults(s_editLayer);
    }

    static std::string s_portMsg;
    char btnSave[40], btnLoad[40];
    snprintf(btnSave, sizeof(btnSave), "Save layer %d to file…",
                  s_editLayer + 1);
    snprintf(btnLoad, sizeof(btnLoad), "Load layer %d from file…",
                  s_editLayer + 1);
    sameLine(ctx);
    if (ImGui_Button(ctx, btnSave, /*size_w*/ nullptr, /*size_h*/ nullptr)) {
        s_portMsg = reasixty_exportLayerViaDialog(s_editLayer)
            ? "Layer exported."
            : "Export cancelled or failed.";
    }
    sameLine(ctx);
    if (ImGui_Button(ctx, btnLoad, /*size_w*/ nullptr, /*size_h*/ nullptr)) {
        s_portMsg = reasixty_importLayerViaDialog(s_editLayer)
            ? "Layer imported."
            : "Import cancelled or failed.";
    }
    if (!s_portMsg.empty()) {
        ImGui_TextDisabled(ctx, s_portMsg.c_str());
    }
}

// ---- FX Learn -------------------------------------------------------------
// Phase 2.5d-A Step 3: Master-View + Editor-View.
//   3a Master-View — table of UserPluginMaps with CRUD.
//   3b Editor-View — slot list (canonical SSL 360 Link topology) on the
//      left, FX-param list on the right, click-to-listen + click-param
//      to bind. Drag-and-drop and vector schematic come in 3c.
// See: docs/plan-fx-learn-and-multi-instance.md §"Editor-View"
namespace {

// Inline form state for "+ New" / per-row error reporting. File-scope
// statics — same pattern as the bindings editor's transient buffers.
char        g_newMatch[128]      = {};
char        g_newDisplay[16]     = {};   // up to 7 chars + NUL + slack
// Mode picker for the "+ New" popup. 1=CS-primary, 2=BC-primary,
// 3=UF8-only. Default 1 matches the old CS default. The UF8 checkbox is
// stored separately so toggling mode doesn't lose it.
int         g_newPrimaryMode     = 1;
bool        g_newUf8Mode         = false;
std::string g_newError;                  // transient inline error text
std::string g_pendingDeleteMatch;        // populated when the confirm popup is open
bool        g_pendingDeleteOpen  = false;// set when row's Del was clicked,
                                         // consumed by the OpenPopup at the
                                         // outer scope so popup id-stack
                                         // matches the BeginPopupModal site.
// Mode-change confirm state: the per-map editor stages a primary-mode
// switch here when the change would clear slot bindings (CS↔BC or →UF8-
// only). The popup applies/cancels in the outer scope to keep the
// OpenPopup ID-stack matched with BeginPopupModal.
std::string g_pendingModeMatch;
int         g_pendingModePrimary = 0;    // 1=CS, 2=BC, 3=UF8-only
bool        g_pendingModeOpen    = false;
std::string g_lastSaveError;             // last persistence error, sticky until next save

// Master-view filter + sort state. Filter is a case-insensitive
// substring matched against either the map's `match` (FX name) or
// the auto-derived developer column.
char g_fxlMasterFilter[64] = {};

// Cached list of installed FX populated lazily on first "+ New" open.
// REAPER's EnumInstalledFX walks the entire plugin catalog (can be
// 5000+ entries with FabFilter / Waves bundles), so we cache it for
// the session and only refresh on explicit "Reload" click.
struct InstalledFx {
    std::string name;   // human-readable, e.g. "VST3: SSL Native Channel Strip 2 (SSL)"
    std::string ident;  // identifier, e.g. "ssl_nativechannelstrip2.vst3"
};
std::vector<InstalledFx> g_installedFx;
char g_pickerFilter[64] = {};
int  g_pickerSelectedIdx = -1;   // index into g_installedFx (filtered or full)

// Best-effort developer/vendor lookup for the FX-Learn master list.
// Returns the vendor parsed from the first installed FX whose name
// contains the map's match substring. Empty when the catalog hasn't
// been loaded yet or no installed FX matches. The vendor portion
// follows REAPER's "Name (Vendor)" convention at the tail of the FX
// name string.
std::string fxlMasterDeveloperFor_(const std::string& match)
{
    if (match.empty() || g_installedFx.empty()) return {};
    for (const auto& fx : g_installedFx) {
        if (fx.name.find(match) == std::string::npos) continue;
        const auto paren = fx.name.rfind(" (");
        if (paren == std::string::npos) return {};
        const auto end = fx.name.rfind(')');
        if (end == std::string::npos || end <= paren + 2) return {};
        return fx.name.substr(paren + 2, end - paren - 2);
    }
    return {};
}

void loadInstalledFx_()
{
    g_installedFx.clear();
    int idx = 0;
    const char* name = nullptr;
    const char* ident = nullptr;
    while (EnumInstalledFX(idx, &name, &ident)) {
        InstalledFx e;
        if (name)  e.name  = name;
        if (ident) e.ident = ident;
        if (!e.name.empty()) g_installedFx.push_back(std::move(e));
        ++idx;
        // Defensive cap so a bug in EnumInstalledFX can't lock us up.
        if (idx > 20000) break;
    }
}

// Heuristic: build a displayShort from a full FX name. Strips the
// "VSTn: " prefix, removes trailing vendor "(...)" parens, then takes
// the first 7 chars (UF8 / UC1 colour-bar zone width).
std::string deriveShortLabel_(const std::string& fxName)
{
    std::string s = fxName;
    auto colon = s.find(": ");
    if (colon != std::string::npos && colon < 8) s = s.substr(colon + 2);
    auto paren = s.rfind(" (");
    if (paren != std::string::npos) s = s.substr(0, paren);
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && s.back()  == ' ') s.pop_back();
    if (s.size() > 7) s.resize(7);
    return s;
}

// Editor-View state. `g_editingMatch` is the current map's `match`
// substring; empty = master-view. `g_listeningLinkIdx == -1` means no
// slot is awaiting a param bind. Param-list filter is sticky between
// renders so the search query survives a tab switch.
std::string g_editingMatch;
int         g_listeningLinkIdx = -1;
char        g_paramFilter[64]  = {};

// (GR-meter picker uses an inline combo dropdown; no listening flag.)

// Click-and-turn state: when a slot is in listening mode we poll
// REAPER's GetLastTouchedFX every frame so wiggling the actual
// plugin-GUI control binds the touched param to the listening slot.
// We snapshot the current GetLastTouchedFX value at the moment the
// listen begins (or jumps to the next slot) so a stale prior touch
// doesn't auto-bind on entry.
int g_listeningPrevIdx     = -1;
int g_lastTouchedTr        = -2;   // -2 = uninitialised; -1 = no last touch
int g_lastTouchedFx        = -1;
int g_lastTouchedParam     = -1;
// Plugin-selector key — "trackIdx:fxIdx" of the FX instance whose param
// list the editor reads from. -1 trackIdx = master. Empty string = pick
// first match (auto). Cleared whenever the editing map changes so the
// selector doesn't outlive its scope.
std::string g_fxSelectorKey;
std::string g_fxSelectorScope;   // editing match for which the key is valid

const char* domainLabel_(uf8::Domain d)
{
    switch (d) {
        case uf8::Domain::ChannelStrip: return "CS";
        case uf8::Domain::BusComp:      return "BC";
        default:                        return "—";
    }
}

// Combined mode label for the new domain structure: shows CS / BC / UF8 /
// CS+UF8 / BC+UF8 depending on which surfaces the map drives.
const char* modeLabel_(uf8::Domain d, bool uf8Mode)
{
    if (d == uf8::Domain::ChannelStrip) return uf8Mode ? "CS+UF8" : "CS";
    if (d == uf8::Domain::BusComp)      return uf8Mode ? "BC+UF8" : "BC";
    return uf8Mode ? "UF8" : "—";
}

void persistAndReport_()
{
    using uf8::user_plugins::SaveResult;
    g_lastSaveError.clear();
    switch (uf8::user_plugins::save()) {
        case SaveResult::Ok:        break;
        case SaveResult::Collision: g_lastSaveError = "Save refused: a match collides with a built-in plugin map."; break;
        case SaveResult::IoError:   g_lastSaveError = "Save failed: could not write user_plugins.json (see /tmp/rea_sixty.log)."; break;
    }
}

// ---- Editor helpers --------------------------------------------------------

// Pick the canonical slot topology for a domain. The SSL 360 Link CS map
// has the broadest CS slot coverage (input/EQ/dyn/output incl. ext::*
// extension slots); the SSL 360 Link Bus Compressor map covers BC.
// Falling back to the first map of that domain keeps the editor
// renderable even if the Link maps are renamed in future.
const uf8::PluginMap* canonicalTopology_(uf8::Domain d)
{
    const uf8::PluginMap* fallback = nullptr;
    for (const auto& m : uf8::allPluginMaps()) {
        if (m.domain != d) continue;
        if (!fallback) fallback = &m;
        if (d == uf8::Domain::ChannelStrip &&
            std::strcmp(m.displayShort, "Link") == 0) return &m;
        if (d == uf8::Domain::BusComp &&
            std::strcmp(m.displayShort, "L-BC") == 0) return &m;
    }
    return fallback;
}

// All FX instances (across all tracks + master) whose name contains the
// user map's match substring. The plugin-selector dropdown lets the user
// pick which one the editor reads its param list from; the schematic +
// param list bind to whichever instance the user picks. Returns the
// instances in the order they're discovered (master first, then tracks
// 1..N), so the auto-pick (g_fxSelectorKey empty) maps to the same
// instance findEditingFx_ used to return pre-selector.
struct EditingFx { MediaTrack* tr = nullptr; int fxIdx = -1; bool ok = false; };

struct EditingFxInstance {
    MediaTrack* tr;
    int         trIdx;     // -1 = master
    int         fxIdx;
    std::string label;     // human-readable for the combo
    std::string key;       // "trIdx:fxIdx" for the selector
};

std::vector<EditingFxInstance> findEditingFxAll_(const std::string& match)
{
    std::vector<EditingFxInstance> out;
    if (match.empty()) return out;
    char buf[512];

    auto scan = [&](MediaTrack* tr, int trIdx) {
        if (!tr) return;
        const int n = TrackFX_GetCount(tr);
        for (int i = 0; i < n; ++i) {
            if (!uf8::fxIdentityName(tr, i, buf, sizeof(buf))) continue;
            if (std::strstr(buf, match.c_str()) == nullptr) continue;
            // Track name (best-effort; empty for unnamed tracks).
            char trName[128] = {};
            if (trIdx >= 0) {
                GetTrackName(tr, trName, sizeof(trName));
            }
            char lbl[700];
            if (trIdx < 0) {
                snprintf(lbl, sizeof(lbl),
                    "Master / FX %d  '%s'", i + 1, buf);
            } else if (trName[0]) {
                snprintf(lbl, sizeof(lbl),
                    "Track %d '%s' / FX %d  '%s'",
                    trIdx + 1, trName, i + 1, buf);
            } else {
                snprintf(lbl, sizeof(lbl),
                    "Track %d / FX %d  '%s'",
                    trIdx + 1, i + 1, buf);
            }
            char keybuf[32];
            snprintf(keybuf, sizeof(keybuf), "%d:%d", trIdx, i);
            out.push_back({ tr, trIdx, i, std::string(lbl), std::string(keybuf) });
        }
    };

    scan(GetMasterTrack(nullptr), -1);
    const int trackCount = CountTracks(nullptr);
    for (int t = 0; t < trackCount; ++t) scan(GetTrack(nullptr, t), t);
    return out;
}

// Resolve the user's selector choice (or auto-pick) to an EditingFx.
EditingFx pickEditingFx_(const std::vector<EditingFxInstance>& list)
{
    if (list.empty()) return {};
    if (!g_fxSelectorKey.empty()) {
        for (const auto& e : list) {
            if (e.key == g_fxSelectorKey) return { e.tr, e.fxIdx, true };
        }
    }
    // Auto-pick = first hit (master before tracks; preserves pre-selector
    // behaviour when nothing's been selected explicitly).
    return { list[0].tr, list[0].fxIdx, true };
}

// Find the existing vst3Param (if any) bound to `linkIdx` for the map
// currently being edited. Returns -1 when not yet mapped.
int mappedVst3For_(int linkIdx)
{
    for (const auto& m : uf8::user_plugins::get().maps) {
        if (m.match != g_editingMatch) continue;
        for (const auto& s : m.slots) {
            if (s.linkIdx == linkIdx) return s.vst3Param;
        }
        break;
    }
    return -1;
}

// Bind / overwrite a slot's vst3Param on the editing map, then save.
void bindSlot_(int linkIdx, int vst3Param)
{
    if (g_editingMatch.empty() || linkIdx < 0 || vst3Param < 0) return;

    auto cat = uf8::user_plugins::get();   // copy
    bool changed = false;
    for (auto& m : cat.maps) {
        if (m.match != g_editingMatch) continue;
        bool replaced = false;
        for (auto& s : m.slots) {
            if (s.linkIdx == linkIdx) {
                s.vst3Param = vst3Param;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            uf8::UserLinkSlot s{};
            s.linkIdx   = linkIdx;
            s.vst3Param = vst3Param;
            s.inverted  = false;
            m.slots.push_back(s);
        }
        uf8::user_plugins::upsert(m);
        changed = true;
        break;
    }
    if (changed) persistAndReport_();
}

// Bind / clear / re-offset the GR-meter VST3 param on the editing map.
// Independent of regular slot bindings — the metering struct is its own
// field on UserPluginMap. All three persist via upsert + persistAndReport_.
void bindGrMeter_(int vst3Param)
{
    if (g_editingMatch.empty() || vst3Param < 0) return;
    auto cat = uf8::user_plugins::get();
    for (auto& m : cat.maps) {
        if (m.match != g_editingMatch) continue;
        m.metering.grVst3Param = vst3Param;
        uf8::user_plugins::upsert(m);
        persistAndReport_();
        break;
    }
}

void clearGrMeter_()
{
    if (g_editingMatch.empty()) return;
    auto cat = uf8::user_plugins::get();
    for (auto& m : cat.maps) {
        if (m.match != g_editingMatch) continue;
        if (m.metering.grVst3Param < 0) return;   // nothing to clear
        m.metering.grVst3Param = -1;
        uf8::user_plugins::upsert(m);
        persistAndReport_();
        break;
    }
}

void setGrOffset_(double offsetDb)
{
    if (g_editingMatch.empty()) return;
    auto cat = uf8::user_plugins::get();
    for (auto& m : cat.maps) {
        if (m.match != g_editingMatch) continue;
        if (m.metering.grOffsetDb == offsetDb) return;  // no-op
        m.metering.grOffsetDb = offsetDb;
        uf8::user_plugins::upsert(m);
        persistAndReport_();
        break;
    }
}

// Per-breakpoint GR calibration mutators (v5 schema). `which` selects
// the table: 0 = BC VU motor (6 points at 0/4/8/12/16/20 dB),
// 1 = DYN GR LEDs + UF8 GR row (5 points at 3/6/10/14/20 dB).
// `idx` is the index inside that table. Absolute set (not delta) —
// ImGui_InputDouble feeds the final value after its own +/-/typing.
// Clamped to ±20 dB hard cap so a misclick can't dial the meter off
// scale (full hardware range is 20 dB; ±20 is enough headroom for
// any practical correction).
void setGrCal_(int which, int idx, double newValue)
{
    if (g_editingMatch.empty()) return;
    const int n = (which == 0) ? 6 : 5;
    if (idx < 0 || idx >= n) return;
    if (newValue >  20.0) newValue =  20.0;
    if (newValue < -20.0) newValue = -20.0;
    auto cat = uf8::user_plugins::get();
    for (auto& m : cat.maps) {
        if (m.match != g_editingMatch) continue;
        double* arr = (which == 0) ? m.metering.grBcVuCalDb
                                    : m.metering.grLedsCalDb;
        if (arr[idx] == newValue) return;
        arr[idx] = newValue;
        uf8::user_plugins::upsert(m);
        persistAndReport_();
        break;
    }
}

void resetGrCal_(int which)
{
    if (g_editingMatch.empty()) return;
    auto cat = uf8::user_plugins::get();
    for (auto& m : cat.maps) {
        if (m.match != g_editingMatch) continue;
        const int n = (which == 0) ? 6 : 5;
        double* arr = (which == 0) ? m.metering.grBcVuCalDb
                                    : m.metering.grLedsCalDb;
        bool dirty = false;
        for (int i = 0; i < n; ++i) {
            if (arr[i] != 0.0) { arr[i] = 0.0; dirty = true; }
        }
        if (!dirty) return;
        uf8::user_plugins::upsert(m);
        persistAndReport_();
        break;
    }
}

// Snapshot every VST3 param on `fx` into the user map identified by
// `match` so subsequent edits can render the param list / V-Pot picker /
// GR-meter picker even when no live instance is loaded. Overwrites any
// previous snapshot. Persists immediately.
void snapshotParamsFor_(const std::string& match, const EditingFx& fx)
{
    if (match.empty() || !fx.ok) return;
    auto cat = uf8::user_plugins::get();
    for (auto& m : cat.maps) {
        if (m.match != match) continue;
        const int n = TrackFX_GetNumParams(fx.tr, fx.fxIdx);
        m.paramSnapshot.clear();
        m.paramSnapshot.reserve(static_cast<size_t>(n));
        char name[256];
        for (int p = 0; p < n; ++p) {
            uf8::UserParamInfo pi{};
            pi.vst3Param = p;
            if (TrackFX_GetParamName(fx.tr, fx.fxIdx, p, name, sizeof(name)))
                pi.name = name;
            double mn = 0, mx = 1, def = 0;
            TrackFX_GetParamEx(fx.tr, fx.fxIdx, p, &mn, &mx, &def);
            const double range = mx - mn;
            pi.defaultNorm = (range > 1e-9) ? (def - mn) / range : 0.5;
            double step = 0, smallStep = 0, largeStep = 0;
            bool isToggle = false;
            TrackFX_GetParameterStepSizes(fx.tr, fx.fxIdx, p,
                &step, &smallStep, &largeStep, &isToggle);
            pi.wasEnum = isToggle || step >= 0.5;
            m.paramSnapshot.push_back(std::move(pi));
        }
        m.snapshotTakenAt = static_cast<int64_t>(std::time(nullptr));
        uf8::user_plugins::upsert(m);
        persistAndReport_();
        break;
    }
}

// Resolve a param name. Prefer live FX (always current), fall back to the
// persisted snapshot when no instance is loaded. Returns false if neither
// source has a name for this index.
bool paramNameFor_(const UserPluginMap& map, const EditingFx& fx,
                   int p, char* out, int outSize)
{
    if (outSize <= 0) return false;
    out[0] = '\0';
    if (fx.ok) {
        return TrackFX_GetParamName(fx.tr, fx.fxIdx, p, out, outSize);
    }
    for (const auto& pi : map.paramSnapshot) {
        if (pi.vst3Param != p) continue;
        std::strncpy(out, pi.name.c_str(), outSize - 1);
        out[outSize - 1] = '\0';
        return true;
    }
    return false;
}

int paramCountFor_(const UserPluginMap& map, const EditingFx& fx)
{
    if (fx.ok) return TrackFX_GetNumParams(fx.tr, fx.fxIdx);
    return static_cast<int>(map.paramSnapshot.size());
}

// Remove a slot binding from the editing map.
void unbindSlot_(int linkIdx)
{
    if (g_editingMatch.empty() || linkIdx < 0) return;
    auto cat = uf8::user_plugins::get();
    for (auto& m : cat.maps) {
        if (m.match != g_editingMatch) continue;
        auto before = m.slots.size();
        m.slots.erase(
            std::remove_if(m.slots.begin(), m.slots.end(),
                [&](const uf8::UserLinkSlot& s) { return s.linkIdx == linkIdx; }),
            m.slots.end());
        if (m.slots.size() != before) {
            uf8::user_plugins::upsert(m);
            persistAndReport_();
        }
        break;
    }
}

// Move listening to the next still-unmapped slot in topology order, or
// clear when nothing else needs binding. Used after a click-bind to
// support quick mass mapping without an extra click per slot.
void autoAdvanceListening_(const uf8::PluginMap& topo)
{
    if (g_listeningLinkIdx < 0) return;
    bool past = false;
    for (const auto& s : topo.slots) {
        if (!past) {
            if (s.linkIdx == g_listeningLinkIdx) past = true;
            continue;
        }
        if (mappedVst3For_(s.linkIdx) < 0) {
            g_listeningLinkIdx = s.linkIdx;
            return;
        }
    }
    g_listeningLinkIdx = -1;
}

// Toggle the inverted-flag on a mapped slot.
void toggleInverted_(int linkIdx)
{
    if (g_editingMatch.empty() || linkIdx < 0) return;
    auto cat = uf8::user_plugins::get();
    for (auto& m : cat.maps) {
        if (m.match != g_editingMatch) continue;
        for (auto& s : m.slots) {
            if (s.linkIdx == linkIdx) {
                s.inverted = !s.inverted;
                uf8::user_plugins::upsert(m);
                persistAndReport_();
                return;
            }
        }
        break;
    }
}

// ---- UF8 helpers (Phase 3) ------------------------------------------------
//
// Mirror the UC1 helpers above but address `editing->uf8.{strips,banks}`.
// Listen state is a separate (kind, strip, bank) tuple stored in
// g_listeningUf8 — mutually exclusive with g_listeningLinkIdx.

// Forward-declares for the Uf8Control struct that lives further down with
// the schematic painter. Defined in the same anonymous namespace so the
// helpers below can reference it; the actual struct definition stays
// local to the painter file region.
struct Uf8Control;

struct Uf8ListenSlot {
    int kind  = -1;     // -1 = none; otherwise Uf8Control::Kind
    int strip = 0;
    int bank  = 0;      // bank index 0..7 — all kinds now bank-aware
                        // (Frank 2026-05-16, was VPot/TopSoftKey only).
    bool active() const { return kind >= 0; }
    void clear() { kind = -1; strip = 0; bank = 0; }
    bool matches(int k, int s, int b) const {
        return kind == k && strip == s && bank == b;
    }
};
Uf8ListenSlot g_listeningUf8;

// Bank index currently shown on the UF8 mockup — drives which
// banks.banks[fb][bank][s] entries are visible/editable.
int g_uf8EditingBank = 0;

// Fader bank currently shown on the UF8 mockup (Frank 2026-05-17).
// 0 → strips 1-8 of the logical 16-strip plug-in; 1 → strips 9-16.
// Drives which (faderBank, vpotBank, slot) the editor mutates, AND
// which strips[faderBank][slot] entries the Fader/Solo/Cut/Sel row
// references. Bidirectional sync with hardware g_uf8FaderBank.
int g_uf8EditingFaderBank = 0;

// Mutator helpers. Each takes a copy of the catalog, mutates the editing
// map's uf8 field, and pushes back via upsert. Callers should already
// have validated kind/strip/bank ranges.
template <class F>
void mutateUf8_(F&& fn)
{
    if (g_editingMatch.empty()) return;
    auto cat = uf8::user_plugins::get();
    for (auto& m : cat.maps) {
        if (m.match != g_editingMatch) continue;
        fn(m.uf8);
        uf8::user_plugins::upsert(m);
        persistAndReport_();
        break;
    }
}

// Read the current vst3Param for any UF8 control. Returns -1 if unmapped
// or editing map missing.
int mappedVst3ForUf8_(int kind, int strip, int bank)
{
    using uf8::user_plugins::get;
    if (g_editingMatch.empty()) return -1;
    for (const auto& m : get().maps) {
        if (m.match != g_editingMatch) continue;
        const auto& u = m.uf8;
        switch (kind) {
            case 0 /*Fader*/:      return u.strips[g_uf8EditingFaderBank][strip].faderVst3Param;
            case 3 /*SoloBtn*/:    return u.strips[g_uf8EditingFaderBank][strip].soloVst3Param;
            case 4 /*CutBtn*/:     return u.strips[g_uf8EditingFaderBank][strip].cutVst3Param;
            case 5 /*SelBtn*/:     return u.strips[g_uf8EditingFaderBank][strip].selVst3Param;
            case 1 /*VPot*/:
            case 2 /*TopSoftKey*/: return u.banks.banks[g_uf8EditingFaderBank][bank][strip].vst3Param;
            // Cases 6/7 (BankLeft/BankRight) removed 2026-05-17 — Bank
            // ←/→ buttons no longer carry per-plug-in overrides.
            default: return -1;
        }
    }
    return -1;
}

void bindUf8_(int kind, int strip, int bank, int vst3Param)
{
    if (vst3Param < 0) return;
    mutateUf8_([&](uf8::UserUf8Map& u) {
        switch (kind) {
            case 0: u.strips[g_uf8EditingFaderBank][strip].faderVst3Param = vst3Param; break;
            case 3: u.strips[g_uf8EditingFaderBank][strip].soloVst3Param  = vst3Param; break;
            case 4: u.strips[g_uf8EditingFaderBank][strip].cutVst3Param   = vst3Param; break;
            case 5: u.strips[g_uf8EditingFaderBank][strip].selVst3Param   = vst3Param; break;
            case 1:
            case 2:
                u.banks.banks[g_uf8EditingFaderBank][bank][strip].vst3Param = vst3Param;
                break;
        }
    });
}

void unbindUf8_(int kind, int strip, int bank)
{
    mutateUf8_([&](uf8::UserUf8Map& u) {
        switch (kind) {
            case 0: u.strips[g_uf8EditingFaderBank][strip].faderVst3Param = -1;
                    u.strips[g_uf8EditingFaderBank][strip].faderInverted  = false; break;
            case 3: u.strips[g_uf8EditingFaderBank][strip].soloVst3Param  = -1; break;
            case 4: u.strips[g_uf8EditingFaderBank][strip].cutVst3Param   = -1; break;
            case 5: u.strips[g_uf8EditingFaderBank][strip].selVst3Param   = -1; break;
            case 1:
            case 2: u.banks.banks[g_uf8EditingFaderBank][bank][strip] = uf8::UserUf8BankSlot{}; break;
        }
    });
}

// "Fill sequential" — when a UF8 control is mapped to a parameter whose
// name carries a digit run (e.g. "CH1 Volume"), populate strips to the
// right with params whose names match the same pattern at successive
// numbers ("CH2 Volume", "CH3 Volume", ...). Width is preserved so
// "CH01" steps to "CH02", not "CH2". Strips with no matching param
// are left untouched. One atomic save. Returns strips bound.
int fillSequentialUf8_(int kind, int strip, int bank,
                       int curParam, const EditingFx& fx)
{
    if (curParam < 0 || strip < 0 || strip >= 7) return 0;
    if (g_editingMatch.empty()) return 0;

    auto cat = uf8::user_plugins::get();
    UserPluginMap* editing = nullptr;
    for (auto& m : cat.maps) {
        if (m.match == g_editingMatch) { editing = &m; break; }
    }
    if (!editing) return 0;

    char curName[256] = {};
    if (!paramNameFor_(*editing, fx, curParam, curName, sizeof(curName)))
        return 0;
    const std::string base(curName);

    size_t ds = std::string::npos, de = 0;
    for (size_t i = 0; i < base.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(base[i]))) {
            ds = i; de = i + 1;
            while (de < base.size() &&
                   std::isdigit(static_cast<unsigned char>(base[de]))) ++de;
            break;
        }
    }
    if (ds == std::string::npos) return 0;

    const int curNum = std::atoi(base.substr(ds, de - ds).c_str());
    const int width  = static_cast<int>(de - ds);
    const std::string prefix = base.substr(0, ds);
    const std::string suffix = base.substr(de);

    const int total = paramCountFor_(*editing, fx);
    if (total <= 0) return 0;

    // Snapshot the source strip's modifier attributes — LED colour and
    // inverted/reverse-LED flags propagate to every filled strip, so a
    // CS-channel row stays visually + behaviourally consistent without
    // the user having to touch every strip after a Fill (Frank 2026-05-21).
    bool         srcFaderInverted = false;
    bool         srcVpotInverted  = false;
    uf8::VPotMode srcVpotMode     = uf8::VPotMode::Value;
    double       srcVpotDefault   = 0.5;
    uint32_t     srcStripColour   = 0;
    uint32_t     srcSoloColour    = 0;
    uint32_t     srcCutColour     = 0;
    uint32_t     srcSelColour     = 0;
    bool         srcSoloInvert    = false;
    bool         srcCutInvert     = false;
    bool         srcSelInvert     = false;
    {
        const auto& srcU  = editing->uf8;
        const auto& srcS  = srcU.strips[g_uf8EditingFaderBank][strip];
        const auto& srcVB = srcU.banks.banks[g_uf8EditingFaderBank][bank][strip];
        srcFaderInverted = srcS.faderInverted;
        srcVpotInverted  = srcVB.inverted;
        srcVpotMode      = srcVB.vpotMode;
        srcVpotDefault   = srcVB.defaultNorm;
        srcStripColour   = srcVB.stripColour;
        srcSoloColour    = srcS.soloColour;
        srcCutColour     = srcS.cutColour;
        srcSelColour     = srcS.selColour;
        srcSoloInvert    = srcS.soloInvert;
        srcCutInvert     = srcS.cutInvert;
        srcSelInvert     = srcS.selInvert;
    }

    int filled = 0;
    for (int s = strip + 1; s < 8; ++s) {
        const int targetNum = curNum + (s - strip);
        char numBuf[16];
        snprintf(numBuf, sizeof(numBuf), "%0*d", width, targetNum);
        const std::string target = prefix + numBuf + suffix;

        int found = -1;
        char nm[256];
        for (int p = 0; p < total; ++p) {
            if (!paramNameFor_(*editing, fx, p, nm, sizeof(nm))) continue;
            if (target == nm) { found = p; break; }
        }
        if (found < 0) continue;

        auto& u = editing->uf8;
        switch (kind) {
            case 0:
                u.strips[g_uf8EditingFaderBank][s].faderVst3Param = found;
                u.strips[g_uf8EditingFaderBank][s].faderInverted  = srcFaderInverted;
                break;
            case 3:
                u.strips[g_uf8EditingFaderBank][s].soloVst3Param  = found;
                u.strips[g_uf8EditingFaderBank][s].soloColour     = srcSoloColour;
                u.strips[g_uf8EditingFaderBank][s].soloInvert     = srcSoloInvert;
                break;
            case 4:
                u.strips[g_uf8EditingFaderBank][s].cutVst3Param   = found;
                u.strips[g_uf8EditingFaderBank][s].cutColour      = srcCutColour;
                u.strips[g_uf8EditingFaderBank][s].cutInvert      = srcCutInvert;
                break;
            case 5:
                u.strips[g_uf8EditingFaderBank][s].selVst3Param   = found;
                u.strips[g_uf8EditingFaderBank][s].selColour      = srcSelColour;
                u.strips[g_uf8EditingFaderBank][s].selInvert      = srcSelInvert;
                break;
            case 1:
            case 2: {
                auto& bs = u.banks.banks[g_uf8EditingFaderBank][bank][s];
                bs.vst3Param   = found;
                bs.inverted    = srcVpotInverted;
                bs.vpotMode    = srcVpotMode;
                bs.defaultNorm = srcVpotDefault;
                bs.stripColour = srcStripColour;
                break;
            }
            default: continue;
        }
        ++filled;
    }
    if (filled > 0) {
        uf8::user_plugins::upsert(*editing);
        persistAndReport_();
    }
    return filled;
}

bool uf8Inverted_(int kind, int strip, int bank)
{
    if (g_editingMatch.empty()) return false;
    for (const auto& m : uf8::user_plugins::get().maps) {
        if (m.match != g_editingMatch) continue;
        const auto& u = m.uf8;
        if (kind == 0) return u.strips[g_uf8EditingFaderBank][strip].faderInverted;
        if (kind == 1 || kind == 2)
            return u.banks.banks[g_uf8EditingFaderBank][bank][strip].inverted;
        // Solo / Cut / Sel — "Reverse LED" toggle (bank-independent: the
        // strip-button record is faderBank-scoped only).
        if (kind == 3 /*SoloBtn*/) return u.strips[g_uf8EditingFaderBank][strip].soloInvert;
        if (kind == 4 /*CutBtn*/)  return u.strips[g_uf8EditingFaderBank][strip].cutInvert;
        if (kind == 5 /*SelBtn*/)  return u.strips[g_uf8EditingFaderBank][strip].selInvert;
        return false;
    }
    return false;
}

void toggleUf8Inverted_(int kind, int strip, int bank)
{
    mutateUf8_([&](uf8::UserUf8Map& u) {
        if (kind == 0)
            u.strips[g_uf8EditingFaderBank][strip].faderInverted = !u.strips[g_uf8EditingFaderBank][strip].faderInverted;
        else if (kind == 1 || kind == 2) {
            auto& bs = u.banks.banks[g_uf8EditingFaderBank][bank][strip];
            bs.inverted = !bs.inverted;
        }
        else if (kind == 3) {
            auto& sb = u.strips[g_uf8EditingFaderBank][strip];
            sb.soloInvert = !sb.soloInvert;
        }
        else if (kind == 4) {
            auto& sb = u.strips[g_uf8EditingFaderBank][strip];
            sb.cutInvert = !sb.cutInvert;
        }
        else if (kind == 5) {
            auto& sb = u.strips[g_uf8EditingFaderBank][strip];
            sb.selInvert = !sb.selInvert;
        }
    });
}

void setUf8VPotMode_(int strip, int bank, uf8::VPotMode mode)
{
    mutateUf8_([&](uf8::UserUf8Map& u) {
        u.banks.banks[g_uf8EditingFaderBank][bank][strip].vpotMode = mode;
    });
}

void setUf8DefaultNorm_(int strip, int bank, double norm)
{
    if (norm < 0.0) norm = 0.0; if (norm > 1.0) norm = 1.0;
    mutateUf8_([&](uf8::UserUf8Map& u) {
        u.banks.banks[g_uf8EditingFaderBank][bank][strip].defaultNorm = norm;
    });
}

void setUf8Label_(int strip, int bank, const std::string& label)
{
    std::string trimmed = label;
    if (trimmed.size() > 7) trimmed.resize(7);
    mutateUf8_([&](uf8::UserUf8Map& u) {
        u.banks.banks[g_uf8EditingFaderBank][bank][strip].label = trimmed;
    });
}

// Per-binding colour read / write. V-Pot no longer carries a colour
// (Frank 2026-05-13: "V-Pot Farbe raus, bringt nichts.") — the LCD
// strip-colour-bar uses stripColour read via getUf8StripColour_ below.
// Frank 2026-05-16: Solo/Cut/Sel become bank-aware along with the rest
// of the strip controls, so colours are now per (bank, strip) too.
// 0 = no override (LED uses class default).
uint32_t getUf8Colour_(int kind, int strip, int bank)
{
    if (g_editingMatch.empty()) return 0;
    for (const auto& m : uf8::user_plugins::get().maps) {
        if (m.match != g_editingMatch) continue;
        const auto& u = m.uf8;
        switch (kind) {
            case 3 /*SoloBtn*/: return u.strips[g_uf8EditingFaderBank][strip].soloColour;
            case 4 /*CutBtn*/:  return u.strips[g_uf8EditingFaderBank][strip].cutColour;
            case 5 /*SelBtn*/:  return u.strips[g_uf8EditingFaderBank][strip].selColour;
            // Cases 6/7 (BankLeft/BankRight) removed 2026-05-17.
            default: return 0;
        }
    }
    return 0;
}

void setUf8Colour_(int kind, int strip, int bank, uint32_t rgb)
{
    rgb &= 0x00FFFFFFu;
    mutateUf8_([&](uf8::UserUf8Map& u) {
        switch (kind) {
            case 3: u.strips[g_uf8EditingFaderBank][strip].soloColour = rgb; break;
            case 4: u.strips[g_uf8EditingFaderBank][strip].cutColour  = rgb; break;
            case 5: u.strips[g_uf8EditingFaderBank][strip].selColour  = rgb; break;
        }
    });
}

// Strip colour bar — per (bank, strip). Drives the LCD's coloured
// stripe under the V-Pot (Frank 2026-05-13: "Farbe für Farbbalken auf
// UF8 Display"). 0 = no override (falls back to bank-track colour).
uint32_t getUf8StripColour_(int strip, int bank)
{
    if (g_editingMatch.empty()) return 0;
    for (const auto& m : uf8::user_plugins::get().maps) {
        if (m.match != g_editingMatch) continue;
        return m.uf8.banks.banks[g_uf8EditingFaderBank][bank][strip].stripColour & 0x00FFFFFFu;
    }
    return 0;
}
void setUf8StripColour_(int strip, int bank, uint32_t rgb)
{
    rgb &= 0x00FFFFFFu;
    mutateUf8_([&](uf8::UserUf8Map& u) {
        u.banks.banks[g_uf8EditingFaderBank][bank][strip].stripColour = rgb;
    });
}

// Per-bank TopSoftKey LED appearance. Plugin Mode reads these to
// drive bank-N's TopSoftKey-N selector LED (active = Bright, inactive
// = Dim, both fixed; only the colour and label are user-set).
uint32_t getUf8TopSoftKeyColour_(int bank)
{
    if (g_editingMatch.empty()) return 0xFFFFFFu;
    for (const auto& m : uf8::user_plugins::get().maps) {
        if (m.match != g_editingMatch) continue;
        return m.uf8.topSoftKeyLeds[bank].colour & 0x00FFFFFFu;
    }
    return 0xFFFFFFu;
}
std::string getUf8TopSoftKeyLabel_(int bank)
{
    if (g_editingMatch.empty()) return {};
    for (const auto& m : uf8::user_plugins::get().maps) {
        if (m.match != g_editingMatch) continue;
        return m.uf8.topSoftKeyLeds[bank].label;
    }
    return {};
}
void setUf8TopSoftKeyColour_(int bank, uint32_t rgb)
{
    rgb &= 0x00FFFFFFu;
    mutateUf8_([&](uf8::UserUf8Map& u) {
        u.topSoftKeyLeds[bank].colour = rgb;
    });
}
void setUf8TopSoftKeyLabel_(int bank, std::string lbl)
{
    if (lbl.size() > 16) lbl.resize(16);
    mutateUf8_([&](uf8::UserUf8Map& u) {
        u.topSoftKeyLeds[bank].label = std::move(lbl);
    });
}

// True iff bank N has at least one V-Pot binding. Unassigned banks
// (all 8 strips' vst3Param == -1) are no-function in Plugin Mode
// (Frank 2026-05-13: "unzugewiesene Soft-Key V-Pot banks no-function
// machen") — hardware press skipped, mockup click ignored, LED dark.
bool bankHasVPotBindings_(int bank)
{
    if (g_editingMatch.empty()) return false;
    if (bank < 0 || bank >= uf8::kUserUf8BankCount) return false;
    for (const auto& m : uf8::user_plugins::get().maps) {
        if (m.match != g_editingMatch) continue;
        for (int s = 0; s < 8; ++s) {
            if (m.uf8.banks.banks[g_uf8EditingFaderBank][bank][s].vst3Param >= 0) return true;
        }
        return false;
    }
    return false;
}

// Fill all 8 strips' colour bars in the given bank with a single
// colour (Frank 2026-05-13: "rechtsklickmenu -> fill all").
void fillAllStripColours_(int bank, uint32_t rgb)
{
    rgb &= 0x00FFFFFFu;
    mutateUf8_([&](uf8::UserUf8Map& u) {
        for (int s = 0; s < 8; ++s) {
            u.banks.banks[g_uf8EditingFaderBank][bank][s].stripColour = rgb;
        }
    });
}

// ---- UC1 mockup as the FX-Learn schematic --------------------------------
//
// Each entry binds a position on the UC1 face to an SSL 360 Link slot.
// Coordinates match drawUc1Face_ exactly (cx/cy in canvas-local coords;
// for knobs use the centre + radius, for toggles/btns use top-left + w/h).
//
// Domain tags drive the dim mask: when the user is editing a CS map,
// Domain::ChannelStrip controls render in their full state-aware colour,
// Domain::BusComp controls disappear under a 30 % alpha overlay and
// register no hits (their per-control InvisibleButton is skipped).
//
// linkIdx values cross-reference kSsl360LinkSlots (CS) and
// kSsl360LinkBcSlots (BC) — same indices used by the canonical built-in
// PluginMap and by the now-obsolete kCsPads/kBcPads tables below.
//
// Slots without a UC1 control (Fader, Pan, Width, Output Trim, Bypass,
// QA1..6, SAT, SAT.I, GRP for CS; Bypass, GRP for BC) are out of scope
// for this commit. They can still be edited via the master view's text
// fields; a future "extras tray" can add them as small tiles.
struct Uc1Control {
    enum Kind : uint8_t { Knob, Toggle, DynBtn };
    Kind        kind;
    int         linkIdx;
    uf8::Domain domain;
    float       cx, cy;        // knob centre; toggle/btn top-left
    float       r;             // knob radius (0 for toggle/btn)
    float       w, h;          // toggle/btn box (0 for knob)
    uint32_t    cap;           // accent colour (RedHF / GreenHMF / …)
    const char* label;         // 4-char overlay; "" to skip
};

// Colour palette mirrors drawUc1Face_'s constants.
constexpr uint32_t kCapRed   = 0xC03038FF;  // HF Gain
constexpr uint32_t kCapGreen = 0x408840FF;  // HMF Gain + Q
constexpr uint32_t kCapBlue  = 0x4070C0FF;  // LMF Gain + Q
constexpr uint32_t kCapGrey  = 0x6A707CFF;  // filters + dynamics
constexpr uint32_t kCapBlack = 0x101418FF;  // LF Gain + Freq
constexpr uint32_t kCapBC    = 0x2A4870FF;  // Bus-Comp section

// Coords match drawUc1Face_ — see the function for the layout reasoning.
// Column anchors: kColLx=12, kColCx=250, kColRx=618. BC knobs c1x=370,
// c2x=490; cInL=310, cInR=550.
constexpr Uc1Control kUc1Controls[] = {
    // ---- LEFT COLUMN — CS Filters + EQ ----------------------------------
    // 2026-05-22: EQ knobs shifted inward (col1 60→70 / col2 170→162) and
    // BELL/TYPE/IN toggles enlarged (28×14 → 34×18); coordinates here
    // mirror drawUc1Face_'s new layout so the green overlay rings line
    // up with the painted controls.
    // Filters
    { Uc1Control::Knob,    6, uf8::Domain::ChannelStrip,
      82, 44,  20, 0, 0, kCapGrey,  "LPF" },
    { Uc1Control::Knob,    7, uf8::Domain::ChannelStrip,
      174, 70, 20, 0, 0, kCapGrey,  "HPF" },

    // HF band (red caps)
    { Uc1Control::Toggle,  8, uf8::Domain::ChannelStrip,
      204, 146, 0, 34, 18, 0, "HFBL" },
    { Uc1Control::Knob,    9, uf8::Domain::ChannelStrip,
      82,  154, 20, 0, 0, kCapRed,  "HFGN" },
    { Uc1Control::Knob,   10, uf8::Domain::ChannelStrip,
      174, 182, 20, 0, 0, kCapRed,  "HFFQ" },

    // HMF band (green caps)
    { Uc1Control::Knob,   11, uf8::Domain::ChannelStrip,
      82,  232, 20, 0, 0, kCapGreen, "HMFG" },
    { Uc1Control::Knob,   12, uf8::Domain::ChannelStrip,
      174, 260, 20, 0, 0, kCapGreen, "HMFF" },
    { Uc1Control::Knob,   13, uf8::Domain::ChannelStrip,
      82,  300, 20, 0, 0, kCapGreen, "HMFQ" },

    // EQ Type / EQ In toggles
    { Uc1Control::Toggle, 14, uf8::Domain::ChannelStrip,
      204, 356, 0, 34, 18, 0, "EQTY" },
    { Uc1Control::Toggle, 15, uf8::Domain::ChannelStrip,
      204, 380, 0, 34, 18, 0, "EQIN" },

    // LMF band (blue caps)
    { Uc1Control::Knob,   16, uf8::Domain::ChannelStrip,
      82,  430, 20, 0, 0, kCapBlue,  "LMFG" },
    { Uc1Control::Knob,   17, uf8::Domain::ChannelStrip,
      174, 458, 20, 0, 0, kCapBlue,  "LMFF" },
    { Uc1Control::Knob,   18, uf8::Domain::ChannelStrip,
      82,  498, 20, 0, 0, kCapBlue,  "LMFQ" },

    // LF band (black caps)
    { Uc1Control::Knob,   19, uf8::Domain::ChannelStrip,
      174, 558, 20, 0, 0, kCapBlack, "LFFQ" },
    { Uc1Control::Knob,   20, uf8::Domain::ChannelStrip,
      82,  598, 20, 0, 0, kCapBlack, "LFGN" },
    { Uc1Control::Toggle, 21, uf8::Domain::ChannelStrip,
      204, 600, 0, 34, 18, 0, "LFBL" },

    // ---- CENTRE COLUMN — Input/Output Gain (CS, physically on BC strip) -
    // Input-Gain knob (UC1 0x0C) → CS Input Trim slot (linkIdx 4). Same
    // slot as the Channel-IN button on the right column — clicking either
    // binds the same param; the ImGui ID is uniquified via cx/cy in
    // drawUc1Control_ so they don't collide.
    // Output-Gain knob (UC1 0x16) → CS Fader Level slot (linkIdx 1). Only
    // exists on the SSL 360 Link CS canonical topology; user-CS maps that
    // omit this slot will render it as ghost (off-domain). Coordinates
    // mirror drawUc1Face_'s kInOutCxL/kInOutCxR/kInOutY (310/550, 358).
    { Uc1Control::Knob,    4, uf8::Domain::ChannelStrip,
      310, 358, 20, 0, 0, kCapGrey, "IN G" },
    { Uc1Control::Knob,    1, uf8::Domain::ChannelStrip,
      550, 358, 20, 0, 0, kCapGrey, "OUT G" },

    // ---- CENTRE COLUMN — BC ---------------------------------------------
    // Threshold, Make-Up, Attack, Release, Ratio, S/C HPF, Mix.
    { Uc1Control::Knob,    1, uf8::Domain::BusComp,
      370, 172, 20, 0, 0, kCapBC, "THR" },
    { Uc1Control::Knob,    2, uf8::Domain::BusComp,
      490, 172, 20, 0, 0, kCapBC, "MAKE" },
    { Uc1Control::Knob,    3, uf8::Domain::BusComp,
      370, 234, 20, 0, 0, kCapBC, "ATK" },
    { Uc1Control::Knob,    4, uf8::Domain::BusComp,
      490, 234, 20, 0, 0, kCapBC, "REL" },
    { Uc1Control::Knob,    5, uf8::Domain::BusComp,
      370, 296, 20, 0, 0, kCapBC, "RAT" },
    // BC IN = compressor in/out toggle = BC linkIdx 0 (Bypass).
    { Uc1Control::Toggle,  0, uf8::Domain::BusComp,
      476, 282, 0, 28, 28, 0, "IN" },
    { Uc1Control::Knob,    6, uf8::Domain::BusComp,
      370, 358, 20, 0, 0, kCapBC, "S/C" },
    { Uc1Control::Knob,    7, uf8::Domain::BusComp,
      490, 358, 20, 0, 0, kCapBC, "MIX" },

    // ---- RIGHT COLUMN — CS Comp + Gate + Channel ------------------------
    // Comp section
    { Uc1Control::DynBtn, 24, uf8::Domain::ChannelStrip,
      772, 24,  0, 66, 22, 0, "C ATK" },     // FAST ATK
    { Uc1Control::DynBtn, 25, uf8::Domain::ChannelStrip,
      772, 50,  0, 66, 22, 0, "C PK" },      // PEAK
    { Uc1Control::Knob,   26, uf8::Domain::ChannelStrip,
      678, 96,  20, 0, 0, kCapGrey, "C RAT" },
    { Uc1Control::Knob,   27, uf8::Domain::ChannelStrip,
      774, 124, 20, 0, 0, kCapGrey, "C THR" },
    { Uc1Control::Knob,   28, uf8::Domain::ChannelStrip,
      678, 158, 20, 0, 0, kCapGrey, "C REL" },

    // DYN IN toggle (square button left of GR meter)
    { Uc1Control::Toggle, 22, uf8::Domain::ChannelStrip,
      632, 221, 0, 22, 22, 0, "DYN" },

    // Gate section
    { Uc1Control::Knob,   29, uf8::Domain::ChannelStrip,
      678, 308, 20, 0, 0, kCapGrey, "G RNG" },
    { Uc1Control::Knob,   30, uf8::Domain::ChannelStrip,
      774, 332, 20, 0, 0, kCapGrey, "G THR" },
    { Uc1Control::Knob,   31, uf8::Domain::ChannelStrip,
      678, 374, 20, 0, 0, kCapGrey, "G REL" },
    { Uc1Control::Knob,   32, uf8::Domain::ChannelStrip,
      774, 398, 20, 0, 0, kCapGrey, "G HLD" },
    { Uc1Control::DynBtn, 33, uf8::Domain::ChannelStrip,
      632, 430, 0, 66, 22, 0, "G/E" },       // EXPAND
    { Uc1Control::DynBtn, 34, uf8::Domain::ChannelStrip,
      632, 456, 0, 66, 22, 0, "G ATK" },     // GATE FAST ATK

    // Channel section — Polarity, S/C Listen, Channel-IN
    { Uc1Control::DynBtn,  5, uf8::Domain::ChannelStrip,
      632, 510, 0, 66, 22, 0, "POL" },        // Polarity (Ø)
    { Uc1Control::DynBtn, 36, uf8::Domain::ChannelStrip,
      632, 536, 0, 66, 22, 0, "S/C L" },     // S/C Listen
    // Channel-IN — square 40×40 per Frank 2026-05-22. Coords mirror
    // drawUc1Face_: inX = kColRx+162 + (66-40)/2 = 793,
    //               inY = 510 + (3*22 - 40)/2   = 523.
    { Uc1Control::Toggle,  4, uf8::Domain::ChannelStrip,
      793.0f, 523.0f, 0, 40, 40, 0, "IN" },  // Channel In / Input Trim
};

constexpr int kUc1ControlsCount =
    sizeof(kUc1Controls) / sizeof(kUc1Controls[0]);

// Render the FX-Learn interactive overlay on top of one already-painted
// UC1 control (drawUc1Face_ has already drawn the cap, ring, indicator,
// and silk-screen label). Adds:
//   - state-aware ring (mapped green / listening amber pulse / unmapped
//     thin grey / ghost dark) at +3 px outside the knob, or as a thin
//     box outline around toggles + dyn buttons
//   - small "p<param>" tag in green for mapped controls
//   - small "i" inverted-flag indicator in upper-right when set
//   - InvisibleButton hit area for click / drag-drop / context-menu
//   - hover tooltip with slot name + param info
//
// Caller must skip out-of-domain controls — drawUc1Face_'s dim overlay
// handles those visually and the absence of an InvisibleButton means
// no hover/click reaches them.
void drawUc1Control_(ImGui_Context* ctx, ImGui_DrawList* dl,
                     float ox, float oy,
                     const Uc1Control& ctrl,
                     const uf8::PluginMap& topo,
                     const EditingFx& fx)
{
    using namespace uf8;

    const LinkSlot* slot = findSlotByLinkIdx(topo, ctrl.linkIdx);
    const int  mapped    = mappedVst3For_(ctrl.linkIdx);
    const bool isMapped  = (mapped >= 0);
    const bool isListen  = (g_listeningLinkIdx == ctrl.linkIdx);
    const bool exists    = (slot != nullptr);

    // Bounding box — knob = (cx-r, cy-r, 2r, 2r); toggle/btn = (cx, cy, w, h).
    float bx, by, bw, bh;
    if (ctrl.kind == Uc1Control::Knob) {
        bx = ctrl.cx - ctrl.r;
        by = ctrl.cy - ctrl.r;
        bw = bh = ctrl.r * 2.0f;
    } else {
        bx = ctrl.cx; by = ctrl.cy;
        bw = ctrl.w;  bh = ctrl.h;
    }

    // State → ring colour.
    uint32_t ringCol;
    if (!exists)        ringCol = 0x303338FF;
    else if (isListen) {
        const double t     = ImGui_GetTime(ctx);
        const double pulse = 0.5 + 0.5 * std::sin(t * 6.0);
        const uint32_t bA  =
            static_cast<uint32_t>(140 + 115 * pulse) & 0xFFu;
        ringCol = (0xFFE040u << 8) | bA;
    }
    else if (isMapped)  ringCol = 0x60C060FF;
    else                ringCol = 0x808890FF;

    // Ring overlay on top of drawUc1Face_'s already-painted control.
    if (ctrl.kind == Uc1Control::Knob) {
        ImGui_DrawList_AddCircle(dl, ox + ctrl.cx, oy + ctrl.cy,
                                 ctrl.r + 3, ringCol,
                                 /*num_segments*/ nullptr,
                                 /*thickness*/ nullptr);
    } else {
        double rounding = (ctrl.kind == Uc1Control::DynBtn) ? 3.0 : 2.5;
        ImGui_DrawList_AddRect(dl,
            ox + bx - 1, oy + by - 1,
            ox + bx + bw + 1, oy + by + bh + 1,
            ringCol, &rounding, /*flags*/ nullptr, /*thickness*/ nullptr);
    }

    // Mapped state is conveyed purely by the ring colour above — no
    // inline text overlay (Frank 2026-05-09: text-over-silk is
    // unreadable). Param name surfaces in the hover tooltip below.

    // "i" inverted-flag indicator (upper-right corner of the bbox).
    if (isMapped) {
        bool inv = false;
        for (const auto& m : uf8::user_plugins::get().maps) {
            if (m.match != g_editingMatch) continue;
            for (const auto& s : m.slots) {
                if (s.linkIdx == ctrl.linkIdx) { inv = s.inverted; break; }
            }
            break;
        }
        if (inv) {
            ImGui_DrawList_AddText(dl,
                ox + bx + bw - 6, oy + by - 4,
                0xFFC04CFF, "i");
        }
    }

    // Hit area + interactions — same pattern drawSchematicPad_ used.
    // ID includes the screen position so two controls bound to the same
    // linkIdx (e.g. Channel-IN button + Input-Gain knob, both → CS Input
    // Trim) don't collide in ImGui's widget table.
    char btnId[64];
    snprintf(btnId, sizeof(btnId), "##fxl_pad_%d_%d_%d",
                  ctrl.linkIdx,
                  static_cast<int>(ctrl.cx),
                  static_cast<int>(ctrl.cy));
    ImGui_SetCursorScreenPos(ctx, ox + bx, oy + by);
    int ibFlags = 0;
    ImGui_InvisibleButton(ctx, btnId, bw, bh, &ibFlags);

    int lbtn = 0;
    if (exists && ImGui_IsItemClicked(ctx, &lbtn) && lbtn == 0) {
        g_listeningLinkIdx = isListen ? -1 : ctrl.linkIdx;
    }

    if (exists && ImGui_IsItemHovered(ctx, nullptr)) {
        char tip[256];
        if (isMapped) {
            char pname[128] = {};
            if (fx.ok) {
                TrackFX_GetParamName(fx.tr, fx.fxIdx, mapped,
                                     pname, sizeof(pname));
            } else {
                // Snapshot fallback so the tooltip stays meaningful when
                // no instance is loaded.
                for (const auto& um : user_plugins::get().maps) {
                    if (um.match != g_editingMatch) continue;
                    for (const auto& pi : um.paramSnapshot) {
                        if (pi.vst3Param == mapped) {
                            std::strncpy(pname, pi.name.c_str(),
                                         sizeof(pname) - 1);
                            break;
                        }
                    }
                    break;
                }
            }
            snprintf(tip, sizeof(tip),
                "%s\n  -> param %d  '%s'",
                slot->name ? slot->name : "(slot)", mapped, pname);
        } else {
            snprintf(tip, sizeof(tip),
                "%s\n  unmapped — drag a param here or click to listen",
                slot->name ? slot->name : "(slot)");
        }
        ImGui_SetTooltip(ctx, tip);
    }

    if (exists && ImGui_BeginDragDropTarget(ctx)) {
        char payload[16] = {};
        int  dropFlags   = 0;
        if (ImGui_AcceptDragDropPayload(ctx, "FXL_PARAM",
                                        payload, int(sizeof(payload)),
                                        &dropFlags)) {
            const int p = std::atoi(payload);
            if (p >= 0) {
                bindSlot_(ctrl.linkIdx, p);
                if (g_listeningLinkIdx == ctrl.linkIdx)
                    autoAdvanceListening_(topo);
            }
        }
        ImGui_EndDragDropTarget(ctx);
    }

    if (exists && isMapped) {
        char popId[40];
        snprintf(popId, sizeof(popId), "fxl_ctx_%d", ctrl.linkIdx);
        if (ImGui_BeginPopupContextItem(ctx, popId, nullptr)) {
            char title[160];
            snprintf(title, sizeof(title),
                "%s  -> param %d",
                slot->name ? slot->name : "(slot)", mapped);
            ImGui_TextDisabled(ctx, title);
            ImGui_Separator(ctx);

            bool inverted = false;
            for (const auto& m : uf8::user_plugins::get().maps) {
                if (m.match != g_editingMatch) continue;
                for (const auto& s : m.slots) {
                    if (s.linkIdx == ctrl.linkIdx) {
                        inverted = s.inverted; break;
                    }
                }
                break;
            }
            char invLbl[40];
            snprintf(invLbl, sizeof(invLbl),
                inverted ? "Inverted [on]" : "Inverted [off]");
            if (ImGui_MenuItem(ctx, invLbl, nullptr, nullptr, nullptr)) {
                toggleInverted_(ctrl.linkIdx);
            }
            if (ImGui_MenuItem(ctx, "Clear binding", nullptr,
                               nullptr, nullptr)) {
                if (g_listeningLinkIdx == ctrl.linkIdx)
                    g_listeningLinkIdx = -1;
                unbindSlot_(ctrl.linkIdx);
            }
            ImGui_EndPopup(ctx);
        }
    }
}

// ---------------------------------------------------------------------------
// UF8 face — Phase 2 read-only mockup.
//
// Geometry mirrors drawUf8Vector's strip layout (kStripW=80, kStripGap=7)
// for hardware fidelity. Width matches the UC1 mockup (860). Solo/Cut/Sel
// stacked vertically per real UF8 hardware. Bank-selector + bank-pick UI
// lives outside this canvas (Phase 3 adds it as a normal ImGui combo).
// ---------------------------------------------------------------------------

namespace {

constexpr float kUf8FaceW       = 860;
constexpr float kUf8FaceH       = 520;
constexpr float kUf8StripW      = 80;
constexpr float kUf8StripGap    = 7;
// Centre 8 strips:  8*80 + 7*7 = 689.  ox = (860 - 689) / 2 = 85.5
constexpr float kUf8StripsOx    = 86;

constexpr float kUf8TopSoftKeyY = 12;
constexpr float kUf8TopSoftKeyH = 22;
constexpr float kUf8ScribbleY   = 40;
constexpr float kUf8ScribbleH   = 58;
constexpr float kUf8VPotCy      = 124;
constexpr float kUf8VPotR       = 18;
constexpr float kUf8SoloY       = 152;
constexpr float kUf8CutY        = 172;
constexpr float kUf8SelY        = 192;
constexpr float kUf8SCSBtnH     = 16;
constexpr float kUf8FaderRailY  = 216;
constexpr float kUf8FaderRailH  = 240;
constexpr float kUf8FaderRailW  = 22;

// Bank L/R buttons (FX-Learn mockup only — no native position on the
// UF8 hardware face). Placed below the strip area, centred.
constexpr float kUf8BankBtnY    = 482;
constexpr float kUf8BankBtnH    = 18;
constexpr float kUf8BankBtnW    = 60;

inline float uf8StripCx_(int strip)
{
    return kUf8StripsOx + strip * (kUf8StripW + kUf8StripGap)
         + kUf8StripW / 2.0f;
}

// Centred between strip 4 and 5 so the pair sits visually centred.
inline float uf8BankBtnX_(bool right) {
    const float cx = (uf8StripCx_(3) + uf8StripCx_(4)) / 2.0f;
    return right ? cx + 4 : cx - 4 - kUf8BankBtnW;
}

} // namespace

void drawUf8Face_(VCanvas& c)
{
    constexpr float W = kUf8FaceW, H = kUf8FaceH;

    constexpr uint32_t kChassis    = 0x14181EFF;
    constexpr uint32_t kEdge       = 0x2A3038FF;
    constexpr uint32_t kPanel      = 0x1A1E24FF;
    constexpr uint32_t kBtnFill    = 0x252A33FF;
    constexpr uint32_t kBtnEdge    = 0x4A5060FF;
    constexpr uint32_t kScribble   = 0x080C12FF;
    constexpr uint32_t kScribEdge  = 0x444A55FF;
    constexpr uint32_t kRingOuter  = 0x4A5060FF;
    constexpr uint32_t kRingInner  = 0x555A66FF;
    constexpr uint32_t kFaderRail  = 0x303338FF;
    constexpr uint32_t kSilkText   = 0x9CA0AAFF;

    // Chassis
    rect_(c, 4, 4, W - 8, H - 8, kChassis, kEdge, /*rounding*/ 8.0);

    // Per-strip skeleton — geometry copied from drawUf8Vector for parity.
    for (int s = 0; s < 8; ++s) {
        const float cx   = uf8StripCx_(s);
        const float colX = cx - kUf8StripW / 2.0f;

        // Top soft-key
        rect_(c, colX + 6, kUf8TopSoftKeyY,
              kUf8StripW - 12, kUf8TopSoftKeyH,
              kBtnFill, kBtnEdge, 3.5);

        // Scribble (track name + value-line stacked, no internal divider —
        // matches drawUf8Vector's single-rect look)
        rect_(c, colX + 4, kUf8ScribbleY,
              kUf8StripW - 8, kUf8ScribbleH,
              kScribble, kScribEdge, 2.0);

        // V-Pot ring
        circle_(c, cx, kUf8VPotCy, kUf8VPotR,        kChassis,  kBtnEdge);
        circle_(c, cx, kUf8VPotCy, kUf8VPotR - 4,    kBtnFill,  kRingInner);
        line_(c, cx, kUf8VPotCy - kUf8VPotR + 2,
                  cx, kUf8VPotCy - (kUf8VPotR - 10),
              0xCCCCCCFF, 2.0);

        // Solo / Cut / Sel — STACKED vertically per real UF8 hardware
        rect_(c, colX + 8, kUf8SoloY,
              kUf8StripW - 16, kUf8SCSBtnH, kBtnFill, kBtnEdge, 3.0);
        drawTextCentered_(c, cx, kUf8SoloY + kUf8SCSBtnH / 2.0f,
                          kSilkText, "SOLO");
        rect_(c, colX + 8, kUf8CutY,
              kUf8StripW - 16, kUf8SCSBtnH, kBtnFill, kBtnEdge, 3.0);
        drawTextCentered_(c, cx, kUf8CutY + kUf8SCSBtnH / 2.0f,
                          kSilkText, "CUT");
        rect_(c, colX + 8, kUf8SelY,
              kUf8StripW - 16, kUf8SCSBtnH, kBtnFill, kBtnEdge, 3.0);
        drawTextCentered_(c, cx, kUf8SelY + kUf8SCSBtnH / 2.0f,
                          kSilkText, "SEL");

        // Fader rail + thumb at midpoint
        rect_(c, cx - kUf8FaderRailW / 2.0f, kUf8FaderRailY,
              kUf8FaderRailW, kUf8FaderRailH,
              kFaderRail, kEdge, 4.0);
        const float thumbY = kUf8FaderRailY + kUf8FaderRailH / 2.0f - 5;
        rect_(c, cx - kUf8FaderRailW / 2.0f - 1, thumbY,
              kUf8FaderRailW + 2, 10,
              0x60C060CC, 0xE8F0E8FF, 2.0);

        // Strip number under the fader rail — 14 px below the rail
        // bottom (was 4) so it doesn't squeeze against the Bank L/R
        // buttons that sit just below at the same y. Frank 2026-05-22.
        char snum[8];
        snprintf(snum, sizeof(snum), "%d", s + 1);
        drawTextCentered_(c, cx, kUf8FaderRailY + kUf8FaderRailH + 14,
                          0x707680FF, snum);
    }

    // Bank L / Bank R — global controls, painted below the strip area.
    for (int i = 0; i < 2; ++i) {
        const bool right = (i == 1);
        const float bx = uf8BankBtnX_(right);
        rect_(c, bx, kUf8BankBtnY, kUf8BankBtnW, kUf8BankBtnH,
              kBtnFill, kBtnEdge, 3.0);
        drawTextCentered_(c, bx + kUf8BankBtnW / 2.0f,
                          kUf8BankBtnY + kUf8BankBtnH / 2.0f,
                          kSilkText, right ? "BANK \xE2\x96\xB8"
                                           : "BANK \xE2\x97\x82");
    }
}

// ---------------------------------------------------------------------------
// kUf8Controls[] — interactive overlay positions for the UF8 face.
// Used by Phase 3+ for drag-drop / listen / right-click context. Phase 2
// only paints the face above; this table is included now so the layout
// is in one place when interactivity is wired.
// ---------------------------------------------------------------------------

struct Uf8Control {
    enum Kind : uint8_t {
        Fader,           // strip 0..7 → uf8.strips[bank][s].faderVst3Param
        VPot,            // strip 0..7 → uf8.banks.banks[bank][s]
        TopSoftKey,      // strip 0..7 → uf8.banks.banks[bank][s] (label)
        SoloBtn,         // strip 0..7 → uf8.strips[bank][s].soloVst3Param
        CutBtn,
        SelBtn,
        BankLeftBtn,     // global (strip ignored) → uf8.bankLeft
        BankRightBtn,    // global (strip ignored) → uf8.bankRight
    };
    Kind  kind;
    int   strip;     // 0..7 (ignored for BankLeftBtn / BankRightBtn)
    float cx, cy;    // top-left of bbox
    float w, h;
};

// Build the table programmatically — UF8 strips are uniform unlike the UC1
// face. Lives in a function-local static so initialisation order is sane.
// 6 strip-kinds × 8 strips + 2 global Bank L/R = 50 entries.
const Uf8Control* uf8Controls_(int* outCount)
{
    static Uf8Control tbl[50];
    static int count = 0;
    if (count == 0) {
        int n = 0;
        for (int s = 0; s < 8; ++s) {
            const float cx   = uf8StripCx_(s);
            const float colX = cx - kUf8StripW / 2.0f;

            tbl[n++] = { Uf8Control::TopSoftKey, s,
                         colX + 6, kUf8TopSoftKeyY,
                         kUf8StripW - 12, kUf8TopSoftKeyH };

            // V-Pot bbox = circumscribed square
            tbl[n++] = { Uf8Control::VPot, s,
                         cx - kUf8VPotR,
                         kUf8VPotCy - kUf8VPotR,
                         2 * kUf8VPotR, 2 * kUf8VPotR };

            tbl[n++] = { Uf8Control::SoloBtn, s,
                         colX + 8, kUf8SoloY,
                         kUf8StripW - 16, kUf8SCSBtnH };
            tbl[n++] = { Uf8Control::CutBtn, s,
                         colX + 8, kUf8CutY,
                         kUf8StripW - 16, kUf8SCSBtnH };
            tbl[n++] = { Uf8Control::SelBtn, s,
                         colX + 8, kUf8SelY,
                         kUf8StripW - 16, kUf8SCSBtnH };

            tbl[n++] = { Uf8Control::Fader, s,
                         cx - kUf8FaderRailW / 2.0f, kUf8FaderRailY,
                         kUf8FaderRailW, kUf8FaderRailH };
        }
        // Bank L / Bank R — single global controls (strip ignored).
        tbl[n++] = { Uf8Control::BankLeftBtn,  0,
                     uf8BankBtnX_(false), kUf8BankBtnY,
                     kUf8BankBtnW, kUf8BankBtnH };
        tbl[n++] = { Uf8Control::BankRightBtn, 0,
                     uf8BankBtnX_(true),  kUf8BankBtnY,
                     kUf8BankBtnW, kUf8BankBtnH };
        count = n;
    }
    if (outCount) *outCount = count;
    return tbl;
}

// Per-control interactive overlay on top of drawUf8Face_'s chassis paint.
// Mirrors drawUc1Control_'s pattern: state-aware ring/box + mapped tag +
// inverted indicator + InvisibleButton hit area + drag-drop target +
// right-click context menu. TopSoftKey paints the bound param's label
// only — clicks routed via the V-Pot below it.
void drawUf8Control_(ImGui_Context* ctx, ImGui_DrawList* dl,
                     float ox, float oy,
                     const Uf8Control& ctrl,
                     const EditingFx& fx)
{
    // Live-follow hardware: bank index is the current g_softKeyBank,
    // so a hardware TopSoftKey press immediately re-renders the mockup
    // and a mockup TopSoftKey click writes back via the same global
    // (Frank 2026-05-13: "Soll Live bei Soft-Key press umschalten").
    // Use the raw accessor — reasixty_softkeyCurrentBank() returns the
    // user-Quick activeSubBank (0..5) when a Quick is engaged, which
    // would clip the 8-bank range; FX-Learn always wants g_softKeyBank.
    const int  bank      = reasixty_softKeyBankRaw();
    g_uf8EditingBank     = bank;
    const int  mapped    = mappedVst3ForUf8_(ctrl.kind, ctrl.strip, bank);
    const bool isMapped  = (mapped >= 0);
    const bool isListen  = g_listeningUf8.matches(ctrl.kind, ctrl.strip, bank);

    const float bx = ctrl.cx, by = ctrl.cy;
    const float bw = ctrl.w,  bh = ctrl.h;

    // State → ring colour
    uint32_t ringCol;
    if (isListen) {
        const double t     = ImGui_GetTime(ctx);
        const double pulse = 0.5 + 0.5 * std::sin(t * 6.0);
        const uint32_t bA  =
            static_cast<uint32_t>(140 + 115 * pulse) & 0xFFu;
        ringCol = (0xFFE040u << 8) | bA;
    } else if (isMapped) {
        ringCol = 0x60C060FF;
    } else {
        ringCol = 0x808890FF;
    }

    // Outline overlay
    if (ctrl.kind == Uf8Control::VPot) {
        const float r = (bw / 2.0f);
        ImGui_DrawList_AddCircle(dl, ox + bx + r, oy + by + r, r + 2,
                                 ringCol,
                                 /*num_segments*/ nullptr,
                                 /*thickness*/ nullptr);
    } else {
        double rounding = 3.0;
        ImGui_DrawList_AddRect(dl,
            ox + bx - 1, oy + by - 1,
            ox + bx + bw + 1, oy + by + bh + 1,
            ringCol, &rounding, /*flags*/ nullptr, /*thickness*/ nullptr);
    }

    // TopSoftKey: paint the bank-scoped label inside the already-drawn
    // rect. The label is bank-N's fixed text (Frank 2026-05-13: soft-
    // key press doesn't rewrite the other soft-keys), so it reads from
    // topSoftKeyLeds[ctrl.strip] regardless of the currently active
    // bank. No interactive hit-test for the label — the InvisibleButton
    // below handles left-click (bank switch) + right-click (popup).
    if (ctrl.kind == Uf8Control::TopSoftKey) {
        std::string label = getUf8TopSoftKeyLabel_(ctrl.strip);
        if (label.size() > 8) label.resize(8);
        if (!label.empty()) {
            ImGui_DrawList_AddText(dl, ox + bx + 4, oy + by + 4,
                                   0x88CCEEFF, label.c_str());
        }

        // Active-bank ring — Frank 2026-05-13: "anzeigen, welche soft-
        // key v-pot bank grad aktiv ist". Green outline around the
        // TopSoftKey whose bank matches the current g_softKeyBank;
        // matches the layer/quick ring pattern in the Bindings editor
        // mockup.
        if (ctrl.strip == reasixty_softKeyBankRaw()) {
            ImGui_DrawList_AddRect(dl,
                ox + bx - 2, oy + by - 2,
                ox + bx + bw + 2, oy + by + bh + 2,
                0x40C040FF, /*rounding*/ nullptr,
                /*flags*/ nullptr, /*thickness*/ nullptr);
        }

        // Left-click → bank switch (Frank 2026-05-13: "klick auf UF8
        // mockup soft-key"). Unlike the hardware press handler, the
        // mockup-click does NOT skip unassigned banks — the user has
        // to be able to navigate to an empty bank in the editor in
        // order to populate its V-Pots (Frank 2026-05-13: "Neuer,
        // leerer soft-key v-pot bank kann nicht editiert werden an
        // den v-pots"). The hardware-side skip stays — empty banks
        // are still no-function on the device itself.
        char btnId[48];
        snprintf(btnId, sizeof(btnId),
                      "##fxl_uf8_tsk_%d", ctrl.strip);
        ImGui_SetCursorScreenPos(ctx, ox + bx, oy + by);
        int ibFlags = 0;
        ImGui_InvisibleButton(ctx, btnId, bw, bh, &ibFlags);
        int lbtn = 0;
        if (ImGui_IsItemClicked(ctx, &lbtn) && lbtn == 0) {
            // Mockup TopSoftKey N click = bank N switch in the
            // editor's view. Strips are 0-indexed → bank index.
            reasixty_setSoftKeyBank(ctrl.strip);
        }

        // Right-click menu — Frank 2026-05-13. TopSoftKey N in the
        // mockup represents the hardware bank-N selector. Menu edits
        // bank-N's TopSoftKey label (fixed across bank switches) +
        // a single colour (active = Bright, inactive = Dim — no
        // separate state, Frank: "nur eine farbe. active immer
        // bright, inactive immer dimm"). Right-click works whether
        // the V-Pot slot is bound or not.
        char popId[48];
        snprintf(popId, sizeof(popId),
                      "fxl_uf8_tsk_ctx_%d", ctrl.strip);
        if (ImGui_BeginPopupContextItem(ctx, popId, nullptr)) {
            const int softKeyBank = ctrl.strip;  // TopSoftKey N → bank N
            char title[160];
            snprintf(title, sizeof(title),
                "TopSoftKey %d — Bank %d",
                ctrl.strip + 1, softKeyBank + 1);
            ImGui_TextDisabled(ctx, title);
            ImGui_Separator(ctx);

            // Bank-scoped label — shown on the hardware TopSoftKey
            // LCD regardless of which bank is currently active, so
            // pressing TopSoftKey M doesn't rewrite TopSoftKey N's
            // label (Frank 2026-05-13: "soft-key press soll nicht
            // die anderen soft-keys verändern").
            std::string curLabel = getUf8TopSoftKeyLabel_(softKeyBank);
            static char s_tskLblBuf[16];
            snprintf(s_tskLblBuf, sizeof(s_tskLblBuf), "%s",
                          curLabel.c_str());
            if (ImGui_InputTextWithHint(ctx,
                    "Label##fxl_uf8_tsk_lbl",
                    "(bank N label, shown above the strip)",
                    s_tskLblBuf, int(sizeof(s_tskLblBuf)),
                    nullptr, nullptr))
            {
                setUf8TopSoftKeyLabel_(softKeyBank,
                                       std::string(s_tskLblBuf));
            }
            ImGui_Separator(ctx);

            // Single colour swatch — palette popup mirrors the
            // pattern used elsewhere. Active = Bright, inactive = Dim
            // is fixed on the hardware side.
            const uint32_t curRgb = getUf8TopSoftKeyColour_(softKeyBank);
            ImGui_Text(ctx, "Colour");
            ImGui_SameLine(ctx, nullptr, nullptr);
            const int curRgba = curRgb
                ? static_cast<int>(((curRgb & 0xFF0000u) << 8)
                                 | ((curRgb & 0x00FF00u) << 8)
                                 | ((curRgb & 0x0000FFu) << 8) | 0xFF)
                : 0x40404080;
            char swBtnId[48];
            snprintf(swBtnId, sizeof(swBtnId),
                          "##fxl_uf8_tskcol_%d", ctrl.strip);
            int swBtnFlags = 0;
            double swW = 56.0, swH = 18.0;
            if (ImGui_ColorButton(ctx, swBtnId, curRgba,
                                  &swBtnFlags, &swW, &swH))
            {
                char popPalId[64];
                snprintf(popPalId, sizeof(popPalId),
                              "fxl_uf8_tskpal_%d", ctrl.strip);
                ImGui_OpenPopup(ctx, popPalId, nullptr);
            }
            ImGui_SameLine(ctx, nullptr, nullptr);
            char clrBtnId[48];
            snprintf(clrBtnId, sizeof(clrBtnId),
                          "Default##fxl_uf8_tskclr_%d", ctrl.strip);
            if (ImGui_Button(ctx, clrBtnId, nullptr, nullptr)) {
                setUf8TopSoftKeyColour_(softKeyBank, 0xFFFFFFu);
            }
            char popPalId[64];
            snprintf(popPalId, sizeof(popPalId),
                          "fxl_uf8_tskpal_%d", ctrl.strip);
            if (ImGui_BeginPopup(ctx, popPalId, nullptr)) {
                int paletteCount = 0;
                const uf8::PaletteRgb* palette =
                    uf8::selPaletteRgb(&paletteCount);
                const double sw = 26.0;
                const int perRow = 5;
                for (int i = 0; i < paletteCount; ++i) {
                    const auto& p = palette[i];
                    const int packed =
                        (int(p.r) << 24) |
                        (int(p.g) << 16) |
                        (int(p.b) <<  8) | 0xFF;
                    char swId[40];
                    snprintf(swId, sizeof(swId),
                                  "##fxl_uf8_tskpp_%d_%d",
                                  ctrl.strip, i);
                    int swFlags = 0;
                    double w = sw, h = sw;
                    if (ImGui_ColorButton(ctx, swId, packed,
                                          &swFlags, &w, &h))
                    {
                        const uint32_t rgb =
                            (uint32_t(p.r) << 16) |
                            (uint32_t(p.g) <<  8) |
                             uint32_t(p.b);
                        setUf8TopSoftKeyColour_(softKeyBank, rgb);
                        ImGui_CloseCurrentPopup(ctx);
                    }
                    if ((i % perRow) != (perRow - 1) &&
                        i != paletteCount - 1)
                    {
                        ImGui_SameLine(ctx, nullptr, nullptr);
                    }
                }
                ImGui_EndPopup(ctx);
            }

            ImGui_Separator(ctx);

            if (isMapped) {
                // Fill sequential (right) — same heuristic as V-Pot:
                // the bound param's name must carry a digit run.
                if (ctrl.strip < 7) {
                    const UserPluginMap* editing = nullptr;
                    for (const auto& m : uf8::user_plugins::get().maps) {
                        if (m.match == g_editingMatch) { editing = &m; break; }
                    }
                    bool hasDigits = false;
                    if (editing) {
                        char nm[256];
                        if (paramNameFor_(*editing, fx, mapped,
                                          nm, sizeof(nm))) {
                            for (size_t i = 0; nm[i]; ++i) {
                                if (std::isdigit(
                                        static_cast<unsigned char>(nm[i])))
                                {
                                    hasDigits = true; break;
                                }
                            }
                        }
                    }
                    if (hasDigits) {
                        if (ImGui_MenuItem(ctx, "Fill sequential (right)",
                                           nullptr, nullptr, nullptr))
                        {
                            fillSequentialUf8_(Uf8Control::VPot,
                                ctrl.strip, bank, mapped, fx);
                        }
                        ImGui_Separator(ctx);
                    }
                }

                if (ImGui_MenuItem(ctx, "Clear V-Pot binding", nullptr,
                                   nullptr, nullptr)) {
                    unbindUf8_(Uf8Control::VPot, ctrl.strip, bank);
                }
            }
            ImGui_EndPopup(ctx);
        }
        return;   // no left-click hit-test for TopSoftKey — V-Pot owns it
    }

    // Mapped controls are flagged purely via the green ring drawn
    // above — no inline text overlay (Frank 2026-05-09: text-over-silk
    // is unreadable). Param name surfaces in the hover tooltip below.
    // Inverted "i" stays as a small corner glyph since it conveys a
    // distinct binary state the ring can't.
    if (isMapped &&
        (ctrl.kind == Uf8Control::Fader || ctrl.kind == Uf8Control::VPot))
    {
        if (uf8Inverted_(ctrl.kind, ctrl.strip, bank)) {
            ImGui_DrawList_AddText(dl,
                ox + bx + bw - 6, oy + by - 4,
                0xFFC04CFF, "i");
        }
    }

    // Hit area — accepts click + drag-drop + popup.
    char btnId[48];
    snprintf(btnId, sizeof(btnId), "##fxl_uf8_%d_%d",
                  int(ctrl.kind), ctrl.strip);
    ImGui_SetCursorScreenPos(ctx, ox + bx, oy + by);
    int ibFlags = 0;
    ImGui_InvisibleButton(ctx, btnId, bw, bh, &ibFlags);

    int lbtn = 0;
    if (ImGui_IsItemClicked(ctx, &lbtn) && lbtn == 0) {
        if (isListen) {
            g_listeningUf8.clear();
        } else {
            g_listeningUf8.kind  = ctrl.kind;
            g_listeningUf8.strip = ctrl.strip;
            g_listeningUf8.bank  = bank;
            g_listeningLinkIdx   = -1;   // mutually exclusive with UC1 listen
        }
    }

    if (ImGui_IsItemHovered(ctx, nullptr)) {
        const char* kindLabel = "?";
        switch (ctrl.kind) {
            case Uf8Control::Fader:        kindLabel = "Fader";    break;
            case Uf8Control::VPot:         kindLabel = "V-Pot";    break;
            case Uf8Control::SoloBtn:      kindLabel = "Solo";     break;
            case Uf8Control::CutBtn:       kindLabel = "Cut";      break;
            case Uf8Control::SelBtn:       kindLabel = "Sel";      break;
            case Uf8Control::BankLeftBtn:  kindLabel = "Bank \xE2\x97\x82"; break;
            case Uf8Control::BankRightBtn: kindLabel = "Bank \xE2\x96\xB8"; break;
            default: break;
        }
        const bool isNav = (ctrl.kind == Uf8Control::BankLeftBtn ||
                            ctrl.kind == Uf8Control::BankRightBtn);
        char tip[256];
        if (isMapped) {
            char pname[128] = {};
            if (fx.ok) {
                TrackFX_GetParamName(fx.tr, fx.fxIdx, mapped,
                                     pname, sizeof(pname));
            } else {
                for (const auto& um : uf8::user_plugins::get().maps) {
                    if (um.match != g_editingMatch) continue;
                    for (const auto& pi : um.paramSnapshot) {
                        if (pi.vst3Param == mapped) {
                            std::strncpy(pname, pi.name.c_str(),
                                         sizeof(pname) - 1);
                            break;
                        }
                    }
                    break;
                }
            }
            // Bank suffix — every strip control is bank-scoped now
            // (Frank 2026-05-16). Nav buttons are global → no suffix.
            char bankSuffix[32] = {0};
            if (!isNav) {
                snprintf(bankSuffix, sizeof(bankSuffix),
                              "  Soft-Key %d", bank + 1);
            }
            if (isNav) {
                snprintf(tip, sizeof(tip),
                    "%s%s\n  -> param %d  '%s'",
                    kindLabel, bankSuffix, mapped, pname);
            } else {
                snprintf(tip, sizeof(tip),
                    "%s strip %d%s\n  -> param %d  '%s'",
                    kindLabel, ctrl.strip + 1, bankSuffix,
                    mapped, pname);
            }
        } else if (isNav) {
            snprintf(tip, sizeof(tip),
                "%s (global)\n  default: bank %s8 — "
                "drag a param here to override",
                kindLabel,
                ctrl.kind == Uf8Control::BankLeftBtn ? "-" : "+");
        } else {
            snprintf(tip, sizeof(tip),
                "%s strip %d\n  unmapped — drag a param here or click to listen",
                kindLabel, ctrl.strip + 1);
        }
        ImGui_SetTooltip(ctx, tip);
    }

    if (ImGui_BeginDragDropTarget(ctx)) {
        char payload[16] = {};
        int  dropFlags   = 0;
        if (ImGui_AcceptDragDropPayload(ctx, "FXL_PARAM",
                                        payload, int(sizeof(payload)),
                                        &dropFlags)) {
            const int p = std::atoi(payload);
            if (p >= 0) {
                bindUf8_(ctrl.kind, ctrl.strip, bank, p);
                if (isListen) g_listeningUf8.clear();
            }
        }
        ImGui_EndDragDropTarget(ctx);
    }

    if (isMapped) {
        const bool isNav = (ctrl.kind == Uf8Control::BankLeftBtn ||
                            ctrl.kind == Uf8Control::BankRightBtn);
        char popId[48];
        snprintf(popId, sizeof(popId), "fxl_uf8_ctx_%d_%d",
                      int(ctrl.kind), ctrl.strip);
        if (ImGui_BeginPopupContextItem(ctx, popId, nullptr)) {
            char title[160];
            if (isNav) {
                snprintf(title, sizeof(title),
                    "Bank %s -> param %d",
                    ctrl.kind == Uf8Control::BankLeftBtn
                        ? "\xE2\x97\x82" : "\xE2\x96\xB8",
                    mapped);
            } else {
                snprintf(title, sizeof(title),
                    "strip %d -> param %d", ctrl.strip + 1, mapped);
            }
            ImGui_TextDisabled(ctx, title);
            ImGui_Separator(ctx);

            if (ctrl.kind == Uf8Control::Fader ||
                ctrl.kind == Uf8Control::VPot)
            {
                bool inv = uf8Inverted_(ctrl.kind, ctrl.strip, bank);
                char invLbl[40];
                snprintf(invLbl, sizeof(invLbl),
                    inv ? "Inverted [on]" : "Inverted [off]");
                if (ImGui_MenuItem(ctx, invLbl, nullptr, nullptr, nullptr)) {
                    toggleUf8Inverted_(ctrl.kind, ctrl.strip, bank);
                }
            }

            // Reverse LED — Solo / Cut / Sel only. For plug-ins whose
            // bound param reports 1 = inactive (so the LED stays bright
            // while the function is off). When on, the LED on/off bit
            // is XORed before rendering.
            if (ctrl.kind == Uf8Control::SoloBtn ||
                ctrl.kind == Uf8Control::CutBtn  ||
                ctrl.kind == Uf8Control::SelBtn)
            {
                bool rev = uf8Inverted_(ctrl.kind, ctrl.strip, bank);
                char revLbl[40];
                snprintf(revLbl, sizeof(revLbl),
                    rev ? "Reverse LED [on]" : "Reverse LED [off]");
                if (ImGui_MenuItem(ctx, revLbl, nullptr, nullptr, nullptr)) {
                    toggleUf8Inverted_(ctrl.kind, ctrl.strip, bank);
                }
            }

            if (ctrl.kind == Uf8Control::VPot) {
                // V-Pot mode (Value/Toggle) + label edit + defaultNorm
                uf8::VPotMode curMode = uf8::VPotMode::Value;
                std::string   curLabel;
                double        curDeflt = 0.5;
                for (const auto& m : uf8::user_plugins::get().maps) {
                    if (m.match != g_editingMatch) continue;
                    const auto& bs = m.uf8.banks.banks[g_uf8EditingFaderBank][bank][ctrl.strip];
                    curMode  = bs.vpotMode;
                    curLabel = bs.label;
                    curDeflt = bs.defaultNorm;
                    break;
                }
                bool isVal = (curMode == uf8::VPotMode::Value);
                if (ImGui_MenuItem(ctx, "V-Pot mode: Value",
                                   nullptr, &isVal, nullptr)) {
                    setUf8VPotMode_(ctrl.strip, bank, uf8::VPotMode::Value);
                }
                bool isTgl = (curMode == uf8::VPotMode::Toggle);
                if (ImGui_MenuItem(ctx, "V-Pot mode: Toggle",
                                   nullptr, &isTgl, nullptr)) {
                    setUf8VPotMode_(ctrl.strip, bank, uf8::VPotMode::Toggle);
                }
                ImGui_Separator(ctx);

                static char s_lblBuf[16];
                snprintf(s_lblBuf, sizeof(s_lblBuf), "%s",
                              curLabel.c_str());
                if (ImGui_InputTextWithHint(ctx,
                        "Label##fxl_uf8_lbl",
                        "(auto from param name)",
                        s_lblBuf, int(sizeof(s_lblBuf)),
                        nullptr, nullptr))
                {
                    setUf8Label_(ctrl.strip, bank, std::string(s_lblBuf));
                }

                if (curMode == uf8::VPotMode::Value) {
                    double tmp = curDeflt;
                    int sliderFlags = 0;
                    if (ImGui_SliderDouble(ctx, "Push reset##fxl_uf8_dn",
                            &tmp, 0.0, 1.0, "%.3f", &sliderFlags)) {
                        setUf8DefaultNorm_(ctrl.strip, bank, tmp);
                    }
                    if (fx.ok) {
                        if (ImGui_MenuItem(ctx, "Capture current value",
                                           nullptr, nullptr, nullptr)) {
                            const double cur = TrackFX_GetParamNormalized(
                                fx.tr, fx.fxIdx, mapped);
                            setUf8DefaultNorm_(ctrl.strip, bank, cur);
                        }
                    }
                }
                ImGui_Separator(ctx);
            }

            // Colour picker — Solo / Cut / Sel only. V-Pot colour
            // removed (Frank 2026-05-13: "V-Pot Farbe raus, bringt
            // nichts.") — the V-Pot LED ring stays at the hardware
            // default; the strip colour bar moved to its own swatch
            // on the LCD mockup. Solo / Cut / Sel colours override
            // only their LEDs and are bank-independent. 0 = no
            // override (LED falls back to class default).
            if (ctrl.kind == Uf8Control::SoloBtn ||
                ctrl.kind == Uf8Control::CutBtn  ||
                ctrl.kind == Uf8Control::SelBtn)
            {
                const uint32_t curRgb = getUf8Colour_(ctrl.kind, ctrl.strip, bank);
                ImGui_Text(ctx, "Colour");
                ImGui_SameLine(ctx, nullptr, nullptr);
                const int curRgba = curRgb
                    ? static_cast<int>(((curRgb & 0xFF0000u) << 8)
                                     | ((curRgb & 0x00FF00u) << 8)
                                     | ((curRgb & 0x0000FFu) << 8) | 0xFF)
                    : 0x40404080;   // dim grey when no override
                char swBtnId[48];
                snprintf(swBtnId, sizeof(swBtnId),
                              "##fxl_uf8_col_%d_%d", int(ctrl.kind), ctrl.strip);
                int swBtnFlags = 0;
                double swW = 56.0, swH = 18.0;
                if (ImGui_ColorButton(ctx, swBtnId, curRgba,
                                      &swBtnFlags, &swW, &swH))
                {
                    char popPalId[48];
                    snprintf(popPalId, sizeof(popPalId),
                                  "fxl_uf8_pal_%d_%d", int(ctrl.kind), ctrl.strip);
                    ImGui_OpenPopup(ctx, popPalId, nullptr);
                }
                ImGui_SameLine(ctx, nullptr, nullptr);
                char clrBtnId[48];
                snprintf(clrBtnId, sizeof(clrBtnId),
                              "Default##fxl_uf8_colclr_%d_%d",
                              int(ctrl.kind), ctrl.strip);
                if (ImGui_Button(ctx, clrBtnId, nullptr, nullptr)) {
                    setUf8Colour_(ctrl.kind, ctrl.strip, bank, 0);
                }

                char popPalId[48];
                snprintf(popPalId, sizeof(popPalId),
                              "fxl_uf8_pal_%d_%d", int(ctrl.kind), ctrl.strip);
                if (ImGui_BeginPopup(ctx, popPalId, nullptr)) {
                    int paletteCount = 0;
                    const uf8::PaletteRgb* palette =
                        uf8::selPaletteRgb(&paletteCount);
                    const double sw = 26.0;
                    const int perRow = 5;
                    for (int i = 0; i < paletteCount; ++i) {
                        const auto& p = palette[i];
                        const int packed =
                            (int(p.r) << 24) |
                            (int(p.g) << 16) |
                            (int(p.b) <<  8) | 0xFF;
                        char swId[32];
                        snprintf(swId, sizeof(swId), "##fxl_uf8_pp_%d", i);
                        int swFlags = 0;
                        double w = sw, h = sw;
                        if (ImGui_ColorButton(ctx, swId, packed,
                                              &swFlags, &w, &h))
                        {
                            const uint32_t rgb =
                                (uint32_t(p.r) << 16) |
                                (uint32_t(p.g) <<  8) |
                                 uint32_t(p.b);
                            setUf8Colour_(ctrl.kind, ctrl.strip, bank, rgb);
                            ImGui_CloseCurrentPopup(ctx);
                        }
                        if ((i % perRow) != (perRow - 1) &&
                            i != paletteCount - 1)
                        {
                            ImGui_SameLine(ctx, nullptr, nullptr);
                        }
                    }
                    ImGui_EndPopup(ctx);
                }
                ImGui_Separator(ctx);
            }

            // Fill strips to the right with sequentially-numbered params
            // derived from the current mapping's name (e.g. "CH1 Volume"
            // → "CH2 Volume"..."CH8 Volume"). Only shown when the mapped
            // param has a digit run and this isn't already the last strip.
            // Nav buttons (Bank L/R) are global — no per-strip fill.
            if (!isNav && ctrl.strip < 7) {
                const UserPluginMap* editing = nullptr;
                for (const auto& m : uf8::user_plugins::get().maps) {
                    if (m.match == g_editingMatch) { editing = &m; break; }
                }
                bool hasDigits = false;
                if (editing) {
                    char nm[256];
                    if (paramNameFor_(*editing, fx, mapped,
                                      nm, sizeof(nm))) {
                        for (size_t i = 0; nm[i]; ++i) {
                            if (std::isdigit(
                                    static_cast<unsigned char>(nm[i]))) {
                                hasDigits = true; break;
                            }
                        }
                    }
                }
                if (hasDigits) {
                    if (ImGui_MenuItem(ctx, "Fill sequential (right)",
                                       nullptr, nullptr, nullptr))
                    {
                        fillSequentialUf8_(ctrl.kind, ctrl.strip, bank,
                                           mapped, fx);
                    }
                    ImGui_Separator(ctx);
                }
            }

            if (ImGui_MenuItem(ctx, "Clear binding", nullptr,
                               nullptr, nullptr)) {
                if (isListen) g_listeningUf8.clear();
                unbindUf8_(ctrl.kind, ctrl.strip, bank);
            }
            ImGui_EndPopup(ctx);
        }
    }
}

// Render the FX-Learn schematic as the UF8 hardware face.
// Strip colour-bar overlay (Frank 2026-05-13: "Farbe für Farbbalken
// auf UF8 Display im Mockup des Displays anzeigen und per klick
// veränderbar machen"). Painted across the bottom edge of the
// scribble (LCD) rectangle on each of the 8 strips, with an
// invisible hit-area that opens a palette popup on click. Stored
// in banks[currentBank][strip].stripColour; 0 = no override (falls
// back to a dim grey marker so the user can still see the cell).
void drawFxLearnUf8StripBars_(ImGui_Context* ctx, ImGui_DrawList* dl,
                              float ox, float oy)
{
    using uf8::user_plugins::get;
    const int bank = reasixty_softKeyBankRaw();

    constexpr float kBarH = 16.0f;
    for (int s = 0; s < 8; ++s) {
        const float cx   = uf8StripCx_(s);
        const float colX = cx - kUf8StripW / 2.0f;
        const float bx   = colX + 6;
        const float by   = kUf8ScribbleY + kUf8ScribbleH - kBarH - 2;
        const float bw   = kUf8StripW - 12;
        const float bh   = kBarH;

        const uint32_t rgb = getUf8StripColour_(s, bank);
        // reaimgui's DrawList colours are RGBA-packed (0xRRGGBBAA),
        // same as every other rect_() literal in this file. Earlier
        // code swapped R↔B as if it were IM_COL32, which displayed
        // yellow as pink and zero-alpha'd anything with B=0
        // (green, cyan).
        const uint32_t fill = (rgb != 0)
            ? ((rgb << 8) | 0xFFu)
            : 0x40404080u;   // dim grey placeholder
        ImGui_DrawList_AddRectFilled(dl,
            ox + bx, oy + by, ox + bx + bw, oy + by + bh,
            fill, /*rounding*/ nullptr, /*flags*/ nullptr);

        // Hit area + palette popup. Left-click = pick this strip's
        // colour. Right-click = "Fill all" menu (Frank 2026-05-13:
        // "rechtsklickmenu -> fill all (alle farbbalken dieselbe
        // farbe)") which opens a separate palette popup that writes
        // to all 8 strips of the active bank in one shot.
        char btnId[48];
        snprintf(btnId, sizeof(btnId),
                      "##fxl_uf8_stripbar_%d", s);
        ImGui_SetCursorScreenPos(ctx, ox + bx, oy + by);
        int ibFlags = 0;
        ImGui_InvisibleButton(ctx, btnId, bw, bh, &ibFlags);
        int lbtn = 0;
        if (ImGui_IsItemClicked(ctx, &lbtn) && lbtn == 0) {
            char popId[48];
            snprintf(popId, sizeof(popId),
                          "fxl_uf8_stripbar_pal_%d", s);
            ImGui_OpenPopup(ctx, popId, nullptr);
        }
        if (ImGui_IsItemHovered(ctx, nullptr)) {
            char tip[128];
            snprintf(tip, sizeof(tip),
                "Strip %d colour bar (bank %d)\n"
                "  left-click: pick this strip\n"
                "  right-click: fill all 8 strips",
                s + 1, bank + 1);
            ImGui_SetTooltip(ctx, tip);
        }
        // Defer-open flag for the Fill-all palette popup. Calling
        // ImGui_OpenPopup directly inside the context-menu's
        // MenuItem callback gets eaten — the context popup closes
        // on click and the new popup's state never reaches the next
        // frame. Stash the request and re-issue OpenPopup after
        // EndPopup at the outer scope.
        static int s_pendingFillAllStrip = -1;
        char ctxId[48];
        snprintf(ctxId, sizeof(ctxId),
                      "fxl_uf8_stripbar_ctx_%d", s);
        if (ImGui_BeginPopupContextItem(ctx, ctxId, nullptr)) {
            if (ImGui_MenuItem(ctx, "Fill all (pick colour)...",
                               nullptr, nullptr, nullptr))
            {
                s_pendingFillAllStrip = s;
            }
            if (ImGui_MenuItem(ctx, "Clear all", nullptr,
                               nullptr, nullptr))
            {
                fillAllStripColours_(bank, 0);
            }
            ImGui_EndPopup(ctx);
        }
        if (s_pendingFillAllStrip == s) {
            s_pendingFillAllStrip = -1;
            char fillId[48];
            snprintf(fillId, sizeof(fillId),
                          "fxl_uf8_stripbar_fillpal_%d", s);
            ImGui_OpenPopup(ctx, fillId, nullptr);
        }

        // Per-strip palette popup (left-click destination).
        char popId[48];
        snprintf(popId, sizeof(popId),
                      "fxl_uf8_stripbar_pal_%d", s);
        if (ImGui_BeginPopup(ctx, popId, nullptr)) {
            char clrId[40];
            snprintf(clrId, sizeof(clrId),
                          "Default##stripbarclr_%d", s);
            if (ImGui_Button(ctx, clrId, nullptr, nullptr)) {
                setUf8StripColour_(s, bank, 0);
                ImGui_CloseCurrentPopup(ctx);
            }
            int paletteCount = 0;
            const uf8::PaletteRgb* palette =
                uf8::selPaletteRgb(&paletteCount);
            const double sw = 26.0;
            const int perRow = 5;
            for (int i = 0; i < paletteCount; ++i) {
                const auto& p = palette[i];
                const int packed =
                    (int(p.r) << 24) |
                    (int(p.g) << 16) |
                    (int(p.b) <<  8) | 0xFF;
                char swId[40];
                snprintf(swId, sizeof(swId),
                              "##stripbarpp_%d_%d", s, i);
                int swFlags = 0;
                double w = sw, h = sw;
                if (ImGui_ColorButton(ctx, swId, packed,
                                      &swFlags, &w, &h))
                {
                    const uint32_t r =
                        (uint32_t(p.r) << 16) |
                        (uint32_t(p.g) <<  8) |
                         uint32_t(p.b);
                    setUf8StripColour_(s, bank, r);
                    ImGui_CloseCurrentPopup(ctx);
                }
                if ((i % perRow) != (perRow - 1) &&
                    i != paletteCount - 1)
                {
                    ImGui_SameLine(ctx, nullptr, nullptr);
                }
            }
            ImGui_EndPopup(ctx);
        }

        // "Fill all" palette popup (right-click destination). Same
        // palette swatch grid, but selecting a colour writes to all
        // 8 strips in the active bank.
        char fillId[48];
        snprintf(fillId, sizeof(fillId),
                      "fxl_uf8_stripbar_fillpal_%d", s);
        if (ImGui_BeginPopup(ctx, fillId, nullptr)) {
            int paletteCount = 0;
            const uf8::PaletteRgb* palette =
                uf8::selPaletteRgb(&paletteCount);
            const double sw = 26.0;
            const int perRow = 5;
            for (int i = 0; i < paletteCount; ++i) {
                const auto& p = palette[i];
                const int packed =
                    (int(p.r) << 24) |
                    (int(p.g) << 16) |
                    (int(p.b) <<  8) | 0xFF;
                char swId[40];
                snprintf(swId, sizeof(swId),
                              "##stripbarfillpp_%d_%d", s, i);
                int swFlags = 0;
                double w = sw, h = sw;
                if (ImGui_ColorButton(ctx, swId, packed,
                                      &swFlags, &w, &h))
                {
                    const uint32_t r =
                        (uint32_t(p.r) << 16) |
                        (uint32_t(p.g) <<  8) |
                         uint32_t(p.b);
                    fillAllStripColours_(bank, r);
                    ImGui_CloseCurrentPopup(ctx);
                }
                if ((i % perRow) != (perRow - 1) &&
                    i != paletteCount - 1)
                {
                    ImGui_SameLine(ctx, nullptr, nullptr);
                }
            }
            ImGui_EndPopup(ctx);
        }
    }
}

void drawFxLearnUf8Schematic_(ImGui_Context* ctx, const EditingFx& fx)
{
    // Fader-bank tab row (Frank 2026-05-17). Bidirectional sync with
    // hardware Bank ←/→: a click here writes g_uf8FaderBank so the
    // hardware switches with the UI; a hardware press updates the
    // global atomic which we read fresh each frame, so the UI mirrors
    // the hardware. Skinny row — sits above the UF8 face mockup so
    // the V-Pot / fader / solo / cut / sel slots below redraw against
    // whichever fader-bank the user is editing.
    {
        const int hwBank = ::reasixty_uf8FaderBank();
        g_uf8EditingFaderBank = hwBank;
        for (int fb = 0; fb < uf8::kUserUf8FaderBankCount; ++fb) {
            if (fb) ImGui_SameLine(ctx, nullptr, nullptr);
            const bool active = (fb == hwBank);
            char lbl[32];
            snprintf(lbl, sizeof(lbl), "Fader Bank %d", fb + 1);
            // ImGui doesn't have a segmented control; emulate by tinting
            // the active button. Push the green ring colour the bank
            // selectors use everywhere else.
            if (active) {
                ImGui_PushStyleColor(ctx, ImGui_Col_Button,    0x60C060FF);
                ImGui_PushStyleColor(ctx, ImGui_Col_ButtonHovered, 0x70D070FF);
                ImGui_PushStyleColor(ctx, ImGui_Col_ButtonActive,  0x80E080FF);
            }
            double bw = 100.0, bh = 22.0;
            if (ImGui_Button(ctx, lbl, &bw, &bh)) {
                ::reasixty_setUf8FaderBank(fb);
                g_uf8EditingFaderBank = fb;
            }
            if (active) {
                int popN = 3;
                ImGui_PopStyleColor(ctx, &popN);
            }
        }
        ImGui_Spacing(ctx);
    }

    double oxd = 0, oyd = 0;
    ImGui_GetCursorScreenPos(ctx, &oxd, &oyd);
    const float ox = float(oxd), oy = float(oyd);

    ImGui_DrawList* dl = ImGui_GetWindowDrawList(ctx);
    VCanvas c { ctx, dl, ox, oy };

    // Lock the schematic font to 12 px so the chassis labels +
    // per-control overlays (TopSoftKey labels etc.) stay readable
    // when the global Font Size picker is set higher. Same rationale
    // as drawUf8Vector / drawUc1*Vector. Frank 2026-05-22.
    ImGui_PushFont(ctx, /*font*/ nullptr, 12.0);

    drawUf8Face_(c);

    // Strip colour bars first so the per-control overlays paint on
    // top (so the TopSoftKey's invisible-button still receives clicks
    // in its full area).
    drawFxLearnUf8StripBars_(ctx, dl, ox, oy);

    int n = 0;
    const Uf8Control* tbl = uf8Controls_(&n);
    for (int i = 0; i < n; ++i) {
        drawUf8Control_(ctx, dl, ox, oy, tbl[i], fx);
    }

    ImGui_PopFont(ctx);

    ImGui_SetCursorScreenPos(ctx, oxd, oyd);
    ImGui_Dummy(ctx, kUf8FaceW, kUf8FaceH);
}

// Render the FX-Learn schematic as the UC1 hardware face. drawUc1Face_
// paints the chassis + colour-coded knobs + section silk-screen + dim
// overlay over the off-domain region; we then iterate kUc1Controls and
// add the interactive overlay (state ring + hit area + drag-drop +
// popup) on top of the in-domain controls only. Off-domain controls
// are visually present but unreachable (no InvisibleButton drawn).
void drawFxLearnSchematic_(ImGui_Context* ctx,
                           const uf8::PluginMap& topo,
                           uf8::Domain domain,
                           const EditingFx& fx)
{
    constexpr float W = 860, H = 660;

    if (domain != uf8::Domain::ChannelStrip &&
        domain != uf8::Domain::BusComp)
    {
        ImGui_TextDisabled(ctx,
            "(no schematic for this domain — UC1 mockup not applicable)");
        return;
    }

    double oxd = 0, oyd = 0;
    ImGui_GetCursorScreenPos(ctx, &oxd, &oyd);
    const float ox = float(oxd), oy = float(oyd);

    ImGui_DrawList* dl = ImGui_GetWindowDrawList(ctx);

    // Lock the schematic font to 12 px (see drawFxLearnUf8Schematic_).
    ImGui_PushFont(ctx, /*font*/ nullptr, 12.0);

    // Paint the full UC1 face including the dim overlay over the
    // off-domain section.
    VCanvas c { ctx, dl, ox, oy };
    drawUc1Face_(c, domain);

    // Interactive overlay on each in-domain control.
    for (int i = 0; i < kUc1ControlsCount; ++i) {
        const Uc1Control& ctrl = kUc1Controls[i];
        if (ctrl.domain != domain) continue;
        drawUc1Control_(ctx, dl, ox, oy, ctrl, topo, fx);
    }

    ImGui_PopFont(ctx);

    // Reserve content rect so the parent BeginChild scrolls to fit.
    ImGui_SetCursorScreenPos(ctx, oxd, oyd);
    ImGui_Dummy(ctx, W, H);
}

// Render the Editor-View. Pre-condition: g_editingMatch is non-empty and
// names a map currently in the catalog. Caller is drawFxLearn.
void drawFxLearnEditor_(ImGui_Context* ctx)
{
    using namespace uf8;

    // Resolve the editing map. If the catalog no longer contains it
    // (e.g. user deleted via another path), bail back to master-view.
    const UserPluginMap* editing = nullptr;
    for (const auto& m : user_plugins::get().maps) {
        if (m.match == g_editingMatch) { editing = &m; break; }
    }
    if (!editing) {
        g_editingMatch.clear();
        g_listeningLinkIdx = -1;
        return;
    }

    // ESC clears any in-progress listening (UC1 or UF8 — mutually
    // exclusive). Only fires when nothing else is capturing keyboard.
    if (g_listeningLinkIdx >= 0 || g_listeningUf8.active()) {
        bool repeat = false;
        if (ImGui_IsKeyPressed(ctx, ImGui_Key_Escape, &repeat)) {
            g_listeningLinkIdx = -1;
            g_listeningUf8.clear();
        }
    }

    // ---- Header / breadcrumb --------------------------------------------
    if (ImGui_Button(ctx, "<- All maps##fxl_back", nullptr, nullptr)) {
        g_editingMatch.clear();
        g_listeningLinkIdx = -1;
        g_listeningUf8.clear();
        return;
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    char hdr[256];
    snprintf(hdr, sizeof(hdr), "  Editing: %s  [%s]",
                  editing->match.c_str(),
                  modeLabel_(editing->domain, editing->uf8Mode));
    ImGui_Text(ctx, hdr);

    // ---- Mode-change row -------------------------------------------------
    // Lets the user re-assign primary mode (CS / BC / UF8-only) and toggle
    // the UF8 strip layer on an existing map. Slot-destructive switches
    // (changing CS↔BC, or →UF8-only) defer to a confirm popup so we don't
    // silently nuke their bindings. Toggling UF8 just flips the flag —
    // the uf8 block is preserved either way.
    {
        const int curPrimary =
            (editing->domain == uf8::Domain::ChannelStrip) ? 1 :
            (editing->domain == uf8::Domain::BusComp)      ? 2 : 3;

        ImGui_Text(ctx, "Mode:");
        ImGui_SameLine(ctx, nullptr, nullptr);
        auto applyPrimary = [&](int newPrimary) {
            if (newPrimary == curPrimary) return;
            // Switching CS↔BC or anything→UF8-only invalidates the slot
            // bindings (the schematic topology is incompatible). Stage
            // the change and pop the confirm modal; non-destructive
            // switches (UF8-only → CS/BC, which has no slots) apply
            // immediately.
            const bool destructive =
                (newPrimary != 3) &&
                ((curPrimary == 1 && newPrimary == 2) ||
                 (curPrimary == 2 && newPrimary == 1));
            const bool toUf8Only = (newPrimary == 3);
            if (destructive || toUf8Only) {
                g_pendingModePrimary = newPrimary;
                g_pendingModeMatch   = editing->match;
                g_pendingModeOpen    = true;
                return;
            }
            UserPluginMap copy = *editing;
            copy.domain  = (newPrimary == 1) ? uf8::Domain::ChannelStrip
                         : (newPrimary == 2) ? uf8::Domain::BusComp
                         :                     uf8::Domain::None;
            copy.uf8Mode = copy.domain == uf8::Domain::None
                             ? true : copy.uf8Mode;
            uf8::user_plugins::upsert(std::move(copy));
            persistAndReport_();
        };
        if (ImGui_RadioButton(ctx, "CS##fxl_mode_cs",   curPrimary == 1)) applyPrimary(1);
        ImGui_SameLine(ctx, nullptr, nullptr);
        if (ImGui_RadioButton(ctx, "BC##fxl_mode_bc",   curPrimary == 2)) applyPrimary(2);
        ImGui_SameLine(ctx, nullptr, nullptr);
        if (ImGui_RadioButton(ctx, "UF8 only##fxl_mode_uf8", curPrimary == 3)) applyPrimary(3);

        ImGui_SameLine(ctx, nullptr, nullptr);
        ImGui_TextDisabled(ctx, "   ");
        ImGui_SameLine(ctx, nullptr, nullptr);
        if (curPrimary == 3) {
            ImGui_TextDisabled(ctx, "UF8 layer: on (required)");
        } else {
            bool uf8Now = editing->uf8Mode;
            if (ImGui_Checkbox(ctx, "UF8 layer##fxl_mode_uf8layer", &uf8Now)) {
                UserPluginMap copy = *editing;
                copy.uf8Mode = uf8Now;
                uf8::user_plugins::upsert(std::move(copy));
                persistAndReport_();
            }
        }
    }

    // UC1 / UF8 mockup picker. Lifted to ExtState so the choice survives
    // REAPER restart.
    static int s_mockup = -1;
    if (s_mockup < 0) {
        const char* v = GetExtState("ReaSixty", "fxLearnMockup");
        s_mockup = (v && *v == '1') ? 1 : 0;
    }

    // Coerce mockup to the map's available surfaces:
    //   UF8-only      → only UF8 mockup
    //   CS / BC w/o UF8 layer → only UC1 mockup
    //   CS+UF8 / BC+UF8 → both
    const bool hasUc1 = (editing->domain != uf8::Domain::None);
    const bool hasUf8 = editing->uf8Mode;
    if (!hasUc1 && hasUf8 && s_mockup != 1) {
        s_mockup = 1;
        SetExtState("ReaSixty", "fxLearnMockup", "1", true);
    } else if (hasUc1 && !hasUf8 && s_mockup != 0) {
        s_mockup = 0;
        SetExtState("ReaSixty", "fxLearnMockup", "0", true);
    }

    ImGui_SameLine(ctx, nullptr, nullptr);
    ImGui_TextDisabled(ctx, "  Mockup:");
    if (hasUc1) {
        ImGui_SameLine(ctx, nullptr, nullptr);
        if (ImGui_RadioButton(ctx, "UC1##fxl_mock_uc1", s_mockup == 0)) {
            s_mockup = 0;
            SetExtState("ReaSixty", "fxLearnMockup", "0", true);
        }
    }
    if (hasUf8) {
        ImGui_SameLine(ctx, nullptr, nullptr);
        if (ImGui_RadioButton(ctx, "UF8##fxl_mock_uf8", s_mockup == 1)) {
            s_mockup = 1;
            SetExtState("ReaSixty", "fxLearnMockup", "1", true);
        }
    }

    // Bank-combo removed (Frank 2026-05-13: "Bank dropdown oben weg").
    // The editing bank now lives in g_softKeyBank — the same global that
    // drives the hardware — so a hardware TopSoftKey press updates the
    // mockup live, and a mockup TopSoftKey click drives the hardware via
    // reasixty_setSoftKeyBank. drawUf8Control_ reads g_softKeyBank
    // directly; g_uf8EditingBank kept for legacy code paths that haven't
    // been swept yet (left intentionally as the seed for the live read).

    // Reset the FX-selector key when the editing map changes — the old
    // key (track:fx pair) means nothing for a different map.
    if (g_fxSelectorScope != editing->match) {
        g_fxSelectorKey.clear();
        g_fxSelectorScope = editing->match;
    }

    // Enumerate every FX-instance whose name contains the match — fed to
    // the plugin-selector combo and to pickEditingFx_.
    const auto fxList = findEditingFxAll_(editing->match);
    const EditingFx fx = pickEditingFx_(fxList);

    // Auto-snapshot: when a live FX is available AND we don't have a
    // persisted param list yet, capture one now so the editor can keep
    // serving param names even after the FX is removed. Subsequent
    // sessions / projects without an instance still see the param list.
    if (fx.ok && editing->paramSnapshot.empty()) {
        snapshotParamsFor_(editing->match, fx);
        // upsert above invalidated `editing` — re-resolve so subsequent
        // code reads from the new (snapshotted) copy.
        for (const auto& m : user_plugins::get().maps) {
            if (m.match == g_editingMatch) { editing = &m; break; }
        }
    }

    // "Snapshot params" button — explicit re-capture (covers plug-in
    // updates that changed the param ordering). Disabled-style hint
    // when no live FX is available; only callable in fx.ok scope.
    if (fx.ok) {
        ImGui_SameLine(ctx, nullptr, nullptr);
        if (ImGui_Button(ctx, "Snapshot params##fxl_snap", nullptr, nullptr)) {
            snapshotParamsFor_(editing->match, fx);
            for (const auto& m : user_plugins::get().maps) {
                if (m.match == g_editingMatch) { editing = &m; break; }
            }
        }
        if (editing->snapshotTakenAt > 0) {
            ImGui_SameLine(ctx, nullptr, nullptr);
            char info[64];
            snprintf(info, sizeof(info), "(%zu params stored)",
                          editing->paramSnapshot.size());
            ImGui_TextDisabled(ctx, info);
        }
    } else if (!editing->paramSnapshot.empty()) {
        ImGui_Spacing(ctx);
        char banner[160];
        snprintf(banner, sizeof(banner),
            "No live FX — editing from stored snapshot (%zu params). "
            "Listen / wiggle requires inserting the plug-in.",
            editing->paramSnapshot.size());
        ImGui_TextColored(ctx, 0xC0C0FFFF, banner);
    }

    // No-instance path — always offer the Insert button so the user can
    // bring the plug-in back into the session for live wiggle / GR meter
    // verification, even when a snapshot is already available. The
    // warning text and prefix vary depending on whether we're operating
    // from snapshot or completely cold.
    if (fxList.empty()) {
        ImGui_Spacing(ctx);
        if (editing->paramSnapshot.empty()) {
            ImGui_TextColored(ctx, 0xFFC04CFF,
                "No FX matching that name found on any track. Insert one "
                "to see its parameter list.");
        } else {
            ImGui_TextDisabled(ctx,
                "Snapshot active — insert a live instance to listen / "
                "wiggle:");
        }
        ImGui_Spacing(ctx);

        // Pick target: first selected track, else first track in project,
        // else master. Surface the choice in the button label so the user
        // doesn't have to guess where it landed.
        MediaTrack* target = GetSelectedTrack(nullptr, 0);
        const char* targetLabel = "selected track";
        if (!target) {
            target = GetTrack(nullptr, 0);
            targetLabel = "first track";
        }
        if (!target) {
            target = GetMasterTrack(nullptr);
            targetLabel = "master track";
        }

        char insLabel[160];
        snprintf(insLabel, sizeof(insLabel),
            "Insert '%s' on %s##fxl_ins",
            editing->match.c_str(), targetLabel);
        if (target && ImGui_Button(ctx, insLabel, nullptr, nullptr)) {
            // TrackFX_AddByName matches partial / fuzzy. instantiate=-1
            // means "always add a new instance".
            const int idx = TrackFX_AddByName(target, editing->match.c_str(),
                                              /*recFX*/ false,
                                              /*instantiate*/ -1);
            if (idx >= 0) {
                // 3 = show floating window so the user can wiggle controls
                // and the GetLastTouchedFX poll picks them up.
                TrackFX_Show(target, idx, 3);
            } else {
                g_lastSaveError =
                    "Insert failed — REAPER couldn't find a plug-in matching "
                    "the match string. Edit the match in the master view to "
                    "match an installed plug-in name.";
            }
        }
    } else if (!fxList.empty()) {
        // Plugin-selector combo. Preview shows the active instance's
        // label; opening the combo lists all matches across master + tracks.
        // Auto-pick row at the top falls back to first-match behaviour.
        const char* preview = nullptr;
        for (const auto& e : fxList) {
            if (e.key == g_fxSelectorKey) { preview = e.label.c_str(); break; }
        }
        if (!preview) preview = fxList[0].label.c_str();

        ImGui_Text(ctx, "  Instance:");
        ImGui_SameLine(ctx, nullptr, nullptr);
        int comboFlags = 0;
        if (ImGui_BeginCombo(ctx, "##fxl_instance", preview, &comboFlags)) {
            // "Auto" entry — clears the selector.
            const bool autoActive = g_fxSelectorKey.empty();
            bool sel0 = autoActive;
            int  sf0  = 0;
            if (ImGui_Selectable(ctx, "Auto (first match)",
                                 &sel0, &sf0, nullptr, nullptr)) {
                g_fxSelectorKey.clear();
            }
            for (const auto& e : fxList) {
                bool sel = (e.key == g_fxSelectorKey);
                int  sf  = 0;
                char rowId[700];
                snprintf(rowId, sizeof(rowId), "%s##fxl_inst_%s",
                              e.label.c_str(), e.key.c_str());
                if (ImGui_Selectable(ctx, rowId, &sel, &sf, nullptr, nullptr)) {
                    g_fxSelectorKey = e.key;
                }
            }
            ImGui_EndCombo(ctx);
        }
    }

    if (!g_lastSaveError.empty()) {
        ImGui_Spacing(ctx);
        ImGui_TextColored(ctx, 0xCC4444FF, g_lastSaveError.c_str());
    }

    ImGui_Spacing(ctx);
    ImGui_Separator(ctx);
    ImGui_Spacing(ctx);

    // ---- GR-meter picker -------------------------------------------------
    // Lets the user designate which VST3 parameter on this learned plug-in
    // represents the gain-reduction readout for the UC1 needle / Comp
    // strip / UF8 GR row. When unset (-1), the GR poll falls back to the
    // PreSonus GainReduction_dB named-config-parm. The combo lists every
    // param on the active FX so the user picks once — no listening, no
    // wiggling (a meter output isn't user-adjustable, the click-to-wiggle
    // listen flow makes no sense for it). The live raw value next to the
    // combo lets the user verify they picked the right param.
    {
        const int curParam = editing->metering.grVst3Param;

        char preview[160];
        if (curParam < 0) {
            snprintf(preview, sizeof(preview),
                "(none — fall back to GainReduction_dB)");
        } else {
            char pname[128] = {0};
            const bool gotName = paramNameFor_(*editing, fx, curParam,
                                               pname, sizeof(pname));
            if (gotName) {
                snprintf(preview, sizeof(preview),
                    "[%d] %s%s", curParam, pname,
                    fx.ok ? "" : "  (from snapshot)");
            } else {
                snprintf(preview, sizeof(preview),
                    "[%d] (no name available)", curParam);
            }
        }

        ImGui_Text(ctx, "GR Meter param:");
        ImGui_SameLine(ctx, nullptr, nullptr);

        // Fill the remaining line width so the combo's right edge lines
        // up with the mockup canvas below it. Frank 2026-05-22.
        {
            double comboAvX = 0, comboAvY = 0;
            ImGui_GetContentRegionAvail(ctx, &comboAvX, &comboAvY);
            ImGui_SetNextItemWidth(ctx, comboAvX);
        }
        if (ImGui_BeginCombo(ctx, "##fxl_gr_combo", preview, 0)) {
            // None entry first — selects the GainReduction_dB fallback.
            {
                bool sel = (curParam < 0);
                int  sf  = 0;
                if (ImGui_Selectable(ctx,
                        "(none — fall back to GainReduction_dB)##fxl_gr_none",
                        &sel, &sf, nullptr, nullptr)) {
                    clearGrMeter_();
                }
            }
            const int paramCount = paramCountFor_(*editing, fx);
            const int kMaxParams = 1024;
            const int n = (paramCount < kMaxParams) ? paramCount : kMaxParams;
            for (int p = 0; p < n; ++p) {
                char pname[128] = {0};
                paramNameFor_(*editing, fx, p, pname, sizeof(pname));
                char rowLbl[200];
                snprintf(rowLbl, sizeof(rowLbl),
                    "[%4d] %s##fxl_gr_p_%d", p, pname, p);
                bool sel = (p == curParam);
                int  sf  = 0;
                if (ImGui_Selectable(ctx, rowLbl, &sel, &sf,
                                     nullptr, nullptr)) {
                    bindGrMeter_(p);
                }
            }
            ImGui_EndCombo(ctx);
        }

        // Compute live calibrated values once so both the inline
        // readout and the popup live-preview share the same numbers.
        bool   havePreview = false;
        double previewRaw = 0.0, previewAbs = 0.0;
        double previewBc  = 0.0, previewLeds = 0.0;
        if (curParam >= 0 && fx.ok) {
            double mn = 0.0, mx = 0.0;
            previewRaw = TrackFX_GetParam(fx.tr, fx.fxIdx,
                                          curParam, &mn, &mx);
            previewAbs = (previewRaw < 0) ? -previewRaw : previewRaw;
            previewBc   = uf8::applyGrCalibration(
                previewAbs, uf8::kBcVuBpDb,  editing->metering.grBcVuCalDb,
                uf8::kBcVuBpCount);
            previewLeds = uf8::applyGrCalibration(
                previewAbs, uf8::kLedsBpDb, editing->metering.grLedsCalDb,
                uf8::kLedsBpCount);
            havePreview = true;
        }

        // Inline live readout next to the combo — domain-scoped to the
        // active renderer (BC: needle dB; CS/UF8: LEDs dB). The actual
        // calibration table renders inline at the bottom of the editor,
        // under the mockup (Frank 2026-05-15: popup mode closed on
        // every live-update tick, unusable while audio plays).
        if (havePreview) {
            const bool isBc =
                (editing->domain == uf8::Domain::BusComp);
            ImGui_SameLine(ctx, nullptr, nullptr);
            char inlineBuf[128];
            const double shown = isBc ? previewBc : previewLeds;
            snprintf(inlineBuf, sizeof(inlineBuf),
                "  raw %+.2f → %s %.2f dB", previewRaw,
                isBc ? "VU" : "GR", shown);
            ImGui_TextColored(ctx, 0xFFC080FF, inlineBuf);
        }

        ImGui_Spacing(ctx);
        ImGui_Separator(ctx);
        ImGui_Spacing(ctx);
    }

    // ---- Two-column body ------------------------------------------------
    // UF8-only maps (domain==None) have no CS/BC schematic — the UC1
    // mockup is hidden by the picker coercion above, so topo is allowed
    // to be nullptr here. Guards around UC1-specific paths below check
    // for it explicitly.
    const PluginMap* topo = canonicalTopology_(editing->domain);
    if (!topo && editing->domain != uf8::Domain::None) {
        ImGui_TextDisabled(ctx,
            "No canonical slot topology available for this domain.");
        return;
    }

    // Click-and-turn: while listening, poll REAPER's GetLastTouchedFX
    // and bind the touched param to the listening slot if the touched
    // FX's name contains our editing match. Snapshot baseline whenever
    // listening just started or auto-advanced so prior touches don't
    // auto-bind on entry.
    if (g_listeningLinkIdx >= 0 && g_listeningLinkIdx != g_listeningPrevIdx) {
        int t = -1, f = -1, p = -1;
        if (GetLastTouchedFX(&t, &f, &p)) {
            g_lastTouchedTr = t; g_lastTouchedFx = f; g_lastTouchedParam = p;
        } else {
            g_lastTouchedTr = -1; g_lastTouchedFx = -1; g_lastTouchedParam = -1;
        }
    }
    g_listeningPrevIdx = g_listeningLinkIdx;

    if (g_listeningLinkIdx >= 0) {
        int t = -1, f = -1, p = -1;
        if (GetLastTouchedFX(&t, &f, &p)) {
            const bool changed = (t != g_lastTouchedTr ||
                                  f != g_lastTouchedFx ||
                                  p != g_lastTouchedParam);
            if (changed) {
                MediaTrack* tr = nullptr;
                if (t == 0)      tr = GetMasterTrack(nullptr);
                else if (t > 0)  tr = GetTrack(nullptr, t - 1);
                if (tr) {
                    char fxName[256] = {};
                    if (uf8::fxIdentityName(tr, f, fxName, sizeof(fxName)) &&
                        std::string(fxName).find(editing->match) != std::string::npos)
                    {
                        bindSlot_(g_listeningLinkIdx, p);
                        if (topo) autoAdvanceListening_(*topo);
                    }
                }
                g_lastTouchedTr = t; g_lastTouchedFx = f; g_lastTouchedParam = p;
            }
        }
    }

    // Same click-and-turn polling for the UF8 listen state. Mutually
    // exclusive with the UC1 listen above (clicking a UF8 control clears
    // g_listeningLinkIdx, and vice versa).
    static int s_uf8PrevKind = -1, s_uf8PrevStrip = -1, s_uf8PrevBank = -1;
    const bool uf8ListenChanged =
        g_listeningUf8.kind  != s_uf8PrevKind  ||
        g_listeningUf8.strip != s_uf8PrevStrip ||
        g_listeningUf8.bank  != s_uf8PrevBank;
    if (g_listeningUf8.active() && uf8ListenChanged) {
        int t = -1, f = -1, p = -1;
        if (GetLastTouchedFX(&t, &f, &p)) {
            g_lastTouchedTr = t; g_lastTouchedFx = f; g_lastTouchedParam = p;
        } else {
            g_lastTouchedTr = -1; g_lastTouchedFx = -1; g_lastTouchedParam = -1;
        }
    }
    s_uf8PrevKind  = g_listeningUf8.kind;
    s_uf8PrevStrip = g_listeningUf8.strip;
    s_uf8PrevBank  = g_listeningUf8.bank;

    if (g_listeningUf8.active()) {
        int t = -1, f = -1, p = -1;
        if (GetLastTouchedFX(&t, &f, &p)) {
            const bool changed = (t != g_lastTouchedTr ||
                                  f != g_lastTouchedFx ||
                                  p != g_lastTouchedParam);
            if (changed) {
                MediaTrack* tr = nullptr;
                if (t == 0)      tr = GetMasterTrack(nullptr);
                else if (t > 0)  tr = GetTrack(nullptr, t - 1);
                if (tr) {
                    char fxName[256] = {};
                    if (uf8::fxIdentityName(tr, f, fxName, sizeof(fxName)) &&
                        std::string(fxName).find(editing->match) != std::string::npos)
                    {
                        bindUf8_(g_listeningUf8.kind,
                                 g_listeningUf8.strip,
                                 g_listeningUf8.bank, p);
                        g_listeningUf8.clear();
                    }
                }
                g_lastTouchedTr = t; g_lastTouchedFx = f; g_lastTouchedParam = p;
            }
        }
    }

    double avX = 0.0, avY = 0.0;
    ImGui_GetContentRegionAvail(ctx, &avX, &avY);
    if (avX < 200.0) avX = 600.0;   // safety for embedded contexts
    if (avY < 200.0) avY = 360.0;
    // Both UC1 and UF8 mockups are 860 px wide. Pin the left pane at
    // 860 whenever the window is wide enough to also satisfy the right
    // pane's 280 px floor — this avoids horizontal scrolling in the
    // schematic pane, which Frank noticed cut off the right edge of
    // the UC1 (content fills to the chassis edge, unlike the UF8 which
    // has 85 px of empty bezel and stays visible). When the window is
    // narrow, fall back to the previous 72%/28% split.
    constexpr double kMockupW   = 860.0;
    // BeginChild reserves padding + scrollbar room inside its bbox; pad
    // the left pane beyond the raw mockup width so the right edge of
    // the UC1 schematic doesn't clip and trigger a horizontal scroll.
    constexpr double kMockupSafe = kMockupW + 24.0;
    constexpr double kRightMin   = 280.0;
    // Cap the param list on wide screens so it stays compact; give the
    // freed space to the mockup pane. Frank 2026-05-22.
    constexpr double kRightPreferred = 360.0;
    constexpr double kPaneGap    = 12.0;
    double leftW, rightW;
    if (avX >= kMockupSafe + kPaneGap + kRightMin) {
        rightW = kRightPreferred;
        if (rightW > avX - kMockupSafe - kPaneGap)
            rightW = avX - kMockupSafe - kPaneGap;
        if (rightW < kRightMin) rightW = kRightMin;
        leftW = avX - rightW - kPaneGap;
    } else {
        leftW  = avX * 0.72;
        rightW = avX - leftW - kPaneGap;
        if (rightW < kRightMin) {
            rightW = kRightMin;
            leftW  = avX - rightW - kPaneGap;
        }
    }

    // Left pane — hardware face mockup. UC1 / UF8 selected via the radio
    // in the editor header. Click to listen, drag a param onto a control
    // to bind, right-click for Inverted/V-Pot mode/Clear context menu.
    int childFlags = 0;
    int winFlags   = ImGui_WindowFlags_HorizontalScrollbar;
    double hLeft = avY - 8.0;
    if (ImGui_BeginChild(ctx, "fxl_slots", &leftW, &hLeft,
                         &childFlags, &winFlags)) {
        if (s_mockup == 1) {
            drawFxLearnUf8Schematic_(ctx, fx);
        } else if (topo) {
            drawFxLearnSchematic_(ctx, *topo, editing->domain, fx);
        }

        // -------- Per-breakpoint GR calibration (Frank 2026-05-15) ----
        // Sits directly under the mockup, inside the left pane only.
        // Domain-scoped: BC plug-ins show the VU motor curve
        // (0/4/8/12/16/20 dB); CS and UF8-only plug-ins show the GR
        // LED / UF8 GR row curve (3/6/10/14/20 dB).
        {
            ImGui_Spacing(ctx);
            ImGui_Separator(ctx);
            ImGui_Spacing(ctx);

            const bool isBc =
                (editing->domain == uf8::Domain::BusComp);
            const int     which  = isBc ? 0 : 1;
            const double* bp     = isBc ? uf8::kBcVuBpDb  : uf8::kLedsBpDb;
            const int     nCal   = isBc ? uf8::kBcVuBpCount : uf8::kLedsBpCount;
            const double* curArr = isBc ? editing->metering.grBcVuCalDb
                                        : editing->metering.grLedsCalDb;
            const char*   title  = isBc
                ? "VU Cal — BC mechanical needle ticks"
                : "GR Cal — DYN LEDs + UF8 GR row segments";

            ImGui_Text(ctx, title);
            ImGui_TextDisabled(ctx,
                "Drive a sine into the plug-in; adjust its threshold so its "
                "INTERNAL meter reads exactly the breakpoint dB.");
            ImGui_TextDisabled(ctx,
                "Then type or click +/− on the matching column until UC1 "
                "aligns. Values are correction offsets in dB. "
                "Ctrl-click +/− = ±1.0.");
            ImGui_Spacing(ctx);

            // 110 base — fits "+0.00" with breathing room at Normal,
            // scales down for Small / up for Large. Spinner buttons
            // are rendered outside this budget.
            const double kInputW = scaleW_(ctx, 110.0);
            for (int i = 0; i < nCal; ++i) {
                if (i) ImGui_SameLine(ctx, nullptr, nullptr);
                ImGui_BeginGroup(ctx);
                char hdrLbl[32];
                snprintf(hdrLbl, sizeof(hdrLbl), "%g dB plugin", bp[i]);
                ImGui_Text(ctx, hdrLbl);
                char inputId[64];
                snprintf(inputId, sizeof(inputId),
                    "dB##fxl_grcal_bot_%d_in_%d", which, i);
                double v = curArr[i];
                double step = 0.1, fast = 1.0;
                int    flags = 0;
                double w = kInputW;
                ImGui_SetNextItemWidth(ctx, w);
                if (ImGui_InputDouble(ctx, inputId, &v, &step, &fast,
                                      "%+.2f", &flags)) {
                    setGrCal_(which, i, v);
                }
                ImGui_EndGroup(ctx);
            }
            ImGui_Spacing(ctx);

            char resetId[48];
            snprintf(resetId, sizeof(resetId),
                "Reset##fxl_grcal_bot_reset_%d", which);
            if (ImGui_Button(ctx, resetId, nullptr, nullptr)) {
                resetGrCal_(which);
            }
            ImGui_Spacing(ctx);

            // Live readout — what we're actually reading from the
            // plug-in and what gets pushed to the renderer after
            // |abs| + cal. Always rendered (regardless of grVst3Param
            // state) so Frank can see if the plug-in is feeding us
            // something other than dB. Uses the same read path as
            // UC1Surface::readGr (FormattedParamValue for picker,
            // GainReduction_dB fallback otherwise) so the displayed
            // numbers match the on-device meter exactly.
            if (fx.ok) {
                double rawDb = 0.0;
                bool   gotIt = false;
                if (editing->metering.grVst3Param >= 0) {
                    char fbuf[64] = {0};
                    if (TrackFX_GetFormattedParamValue(fx.tr, fx.fxIdx,
                            editing->metering.grVst3Param,
                            fbuf, sizeof(fbuf)) && fbuf[0]) {
                        rawDb = std::atof(fbuf);
                        gotIt = true;
                    } else {
                        double mn = 0.0, mx = 0.0;
                        rawDb = TrackFX_GetParam(fx.tr, fx.fxIdx,
                            editing->metering.grVst3Param, &mn, &mx);
                        gotIt = true;
                    }
                } else {
                    char buf[64] = {0};
                    if (TrackFX_GetNamedConfigParm(fx.tr, fx.fxIdx,
                            "GainReduction_dB", buf, sizeof(buf))) {
                        rawDb = std::atof(buf);
                        gotIt = true;
                    }
                }
                if (gotIt) {
                    const double absV = (rawDb < 0) ? -rawDb : rawDb;
                    const double calV = uf8::applyGrCalibration(
                        absV, bp, curArr, nCal);
                    char liveBuf[200];
                    snprintf(liveBuf, sizeof(liveBuf),
                        "Live:  plug-in %+.2f  →  |abs| %.2f  →  on-device %.2f dB",
                        rawDb, absV, calV);
                    ImGui_TextColored(ctx, 0xFFC080FF, liveBuf);
                } else {
                    ImGui_TextDisabled(ctx,
                        "Live: no GR reading (plug-in doesn't expose "
                        "GainReduction_dB and no GR param picked).");
                }
            } else {
                ImGui_TextDisabled(ctx,
                    "Live: insert a matching plug-in to see the reading.");
            }
        }

        ImGui_EndChild(ctx);
    }

    ImGui_SameLine(ctx, nullptr, nullptr);

    // Right pane — param list.
    int rightWinFlags = 0;
    if (ImGui_BeginChild(ctx, "fxl_params", &rightW, &hLeft,
                         &childFlags, &rightWinFlags)) {
        const bool haveParamSource = fx.ok || !editing->paramSnapshot.empty();
        if (!haveParamSource) {
            ImGui_TextDisabled(ctx, "Insert a matching FX to list its params.");
        } else {
            char hint[256];
            if (!fx.ok) {
                ImGui_TextColored(ctx, 0xC0C0FFFF,
                    "Param list from stored snapshot — drag to bind.");
                ImGui_TextColored(ctx, 0xC0C0FFFF,
                    "Wiggle listen needs a live FX.");
                ImGui_Spacing(ctx);
            }
            if (g_listeningLinkIdx >= 0 && topo) {
                const LinkSlot* listenSlot =
                    findSlotByLinkIdx(*topo, g_listeningLinkIdx);
                snprintf(hint, sizeof(hint),
                    "Listening for: %s\n"
                    "  - click a parameter below, OR\n"
                    "  - wiggle the control in the plug-in window",
                    listenSlot ? listenSlot->name : "(slot)");
                ImGui_TextColored(ctx, 0xFFE040FF, hint);
            } else if (g_listeningUf8.active()) {
                const char* kindLbl = "?";
                switch (g_listeningUf8.kind) {
                    case 0 /*Fader*/:      kindLbl = "Fader";      break;
                    case 1 /*VPot*/:       kindLbl = "V-Pot";      break;
                    case 3 /*SoloBtn*/:    kindLbl = "Solo";       break;
                    case 4 /*CutBtn*/:     kindLbl = "Cut";        break;
                    case 5 /*SelBtn*/:     kindLbl = "Sel";        break;
                    default: break;
                }
                snprintf(hint, sizeof(hint),
                    "Listening for UF8 %s strip %d\n"
                    "  - click a parameter below, OR\n"
                    "  - wiggle the control in the plug-in window",
                    kindLbl, g_listeningUf8.strip + 1);
                ImGui_TextColored(ctx, 0xFFE040FF, hint);
            } else {
                ImGui_TextDisabled(ctx,
                    "Drag a param onto a slot, or click a slot to start "
                    "listening.");
            }
            ImGui_Spacing(ctx);

            ImGui_Text(ctx, "Filter:");
            ImGui_SameLine(ctx, nullptr, nullptr);
            double filterW = rightW - 80.0;
            if (filterW < 80.0) filterW = 80.0;
            // No SetNextItemWidth in this ReaImGui sig set; use plain
            // InputTextWithHint at default width.
            ImGui_InputTextWithHint(ctx, "##fxl_param_filter",
                "type to filter...", g_paramFilter,
                static_cast<int>(sizeof(g_paramFilter)),
                nullptr, nullptr);
            ImGui_Spacing(ctx);
            ImGui_Separator(ctx);
            ImGui_Spacing(ctx);

            // Build a reverse-map: vst3Param -> human-readable label of
            // where it's already bound. Covers BOTH the SSL 360 Link
            // slots (UC1 mockup) AND the UF8 strip-mode bindings (banks
            // + per-strip Fader / Solo / Cut / Sel). Already-bound
            // params render as Disabled in the list (Frank 2026-05-09:
            // greys-out so the user sees what's free to grab) and the
            // suffix tells them where it's currently mapped.
            std::unordered_map<int, std::string> usedBy;
            auto noteUse_ = [&](int vp, std::string lbl) {
                if (vp < 0) return;
                auto it = usedBy.find(vp);
                if (it == usedBy.end()) {
                    usedBy.emplace(vp, std::move(lbl));
                } else {
                    // Multi-bound param: keep first label, append "+" so
                    // the user knows there's more than one binding.
                    if (it->second.find('+') == std::string::npos)
                        it->second += " (+)";
                }
            };
            // Slot list is empty for UF8-only maps (no schematic).
            if (topo) {
                for (const auto& slt : editing->slots) {
                    const LinkSlot* bs = findSlotByLinkIdx(*topo, slt.linkIdx);
                    noteUse_(slt.vst3Param,
                             bs && bs->name ? bs->name : "(slot)");
                }
            }
            for (int fb = 0; fb < uf8::kUserUf8FaderBankCount; ++fb) {
                for (int vb = 0; vb < uf8::kUserUf8VpotBankCount; ++vb) {
                    for (int s = 0; s < 8; ++s) {
                        const auto& bs =
                            editing->uf8.banks.banks[fb][vb][s];
                        if (bs.vst3Param < 0) continue;
                        char buf[64];
                        snprintf(buf, sizeof(buf),
                                      "UF8 V-Pot fb %d bk %d str %d",
                                      fb + 1, vb + 1, s + 1);
                        noteUse_(bs.vst3Param, buf);
                    }
                }
            }
            for (int fb = 0; fb < uf8::kUserUf8FaderBankCount; ++fb) {
                for (int s = 0; s < 8; ++s) {
                    const auto& sb = editing->uf8.strips[fb][s];
                    char buf[64];
                    snprintf(buf, sizeof(buf),
                                  "UF8 Fader fb %d str %d", fb + 1, s + 1);
                    noteUse_(sb.faderVst3Param, buf);
                    snprintf(buf, sizeof(buf),
                                  "UF8 Solo fb %d str %d",  fb + 1, s + 1);
                    noteUse_(sb.soloVst3Param, buf);
                    snprintf(buf, sizeof(buf),
                                  "UF8 Cut fb %d str %d",   fb + 1, s + 1);
                    noteUse_(sb.cutVst3Param,  buf);
                    snprintf(buf, sizeof(buf),
                                  "UF8 Sel fb %d str %d",   fb + 1, s + 1);
                    noteUse_(sb.selVst3Param,  buf);
                }
            }

            const int paramCount = paramCountFor_(*editing, fx);
            // Cap iteration so a 5000-param plugin doesn't tank the
            // frame; the user can always sharpen the filter to reach
            // the rest. 1024 is comfortably above any musical plugin.
            const int kMaxParams = 1024;
            const int n = (std::min)(paramCount, kMaxParams);
            const std::string filt = g_paramFilter;

            char pname[128];
            for (int p = 0; p < n; ++p) {
                pname[0] = 0;
                paramNameFor_(*editing, fx, p, pname, sizeof(pname));

                if (!filt.empty()) {
                    if (std::string(pname).find(filt) == std::string::npos)
                        continue;
                }

                auto it = usedBy.find(p);
                const bool isBound = (it != usedBy.end());

                // Already-mapped rows: render disabled so the user can't
                // accidentally re-bind (clear the existing binding via
                // the schematic's right-click menu first). The Selectable
                // carries only the index + param name; the "-> binding"
                // label is rendered via SameLine at a fixed x-offset so
                // arrows line up vertically across rows regardless of
                // the proportional font width of param names. Frank
                // 2026-05-22.
                char rowLbl[256];
                snprintf(rowLbl, sizeof(rowLbl),
                    "  [%4d] %s##fxl_param_%d",
                    p, pname, p);

                bool selected = false;
                int  selFlags = ImGui_SelectableFlags_AllowDoubleClick;
                if (isBound) selFlags |= ImGui_SelectableFlags_Disabled;
                if (ImGui_Selectable(ctx, rowLbl, &selected, &selFlags,
                                     nullptr, nullptr)) {
                    if (g_listeningLinkIdx >= 0 && topo) {
                        bindSlot_(g_listeningLinkIdx, p);
                        autoAdvanceListening_(*topo);
                    } else if (g_listeningUf8.active()) {
                        bindUf8_(g_listeningUf8.kind,
                                 g_listeningUf8.strip,
                                 g_listeningUf8.bank, p);
                        g_listeningUf8.clear();
                    }
                }

                // Aligned binding column for already-bound rows.
                if (isBound) {
                    double arrowX = scaleW_(ctx, 240.0);
                    ImGui_SameLine(ctx, &arrowX, nullptr);
                    char bindBuf[160];
                    snprintf(bindBuf, sizeof(bindBuf),
                        "-> %s", it->second.c_str());
                    ImGui_TextDisabled(ctx, bindBuf);
                }

                // Drag source — payload is the vst3 param index encoded
                // as ASCII. Schematic pads accept it via "FXL_PARAM" type.
                // Disabled rows are still draggable would surprise the
                // user (the row looks dim), so gate this on isBound too.
                if (!isBound) {
                    int dndFlags = 0;
                    if (ImGui_BeginDragDropSource(ctx, &dndFlags)) {
                        char payload[16];
                        snprintf(payload, sizeof(payload), "%d", p);
                        ImGui_SetDragDropPayload(ctx, "FXL_PARAM", payload,
                                                 nullptr);
                        char preview[160];
                        snprintf(preview, sizeof(preview),
                            "param %d  %s", p, pname);
                        ImGui_Text(ctx, preview);
                        ImGui_EndDragDropSource(ctx);
                    }
                }
            }

            if (paramCount > kMaxParams) {
                ImGui_Spacing(ctx);
                char overflow[96];
                snprintf(overflow, sizeof(overflow),
                    "(showing first %d of %d params — use filter)",
                    kMaxParams, paramCount);
                ImGui_TextDisabled(ctx, overflow);
            }
        }
        ImGui_EndChild(ctx);
    }

    // Mode-change confirm popup. Hoisted to the editor scope (not nested in
    // the radio cell) so the popup ID-stack matches the BeginPopupModal
    // site — same pattern as the delete popup in the master view.
    if (g_pendingModeOpen) {
        ImGui_OpenPopup(ctx, "Change mode?###fxl_mode_popup", nullptr);
        g_pendingModeOpen = false;
    }
    // Fixed size so the wrapped explanation doesn't collapse the popup
    // to its minimum width (Frank 2026-05-12: previously a 2-pixel-wide
    // tower). Cond_Always so resizing isn't sticky between opens.
    int condAlways = ImGui_Cond_Always;
    ImGui_SetNextWindowSize(ctx, 420.0, 0.0, &condAlways);
    if (ImGui_BeginPopupModal(ctx, "Change mode?###fxl_mode_popup",
                              nullptr, nullptr)) {
        const char* targetLabel =
            (g_pendingModePrimary == 1) ? "CS" :
            (g_pendingModePrimary == 2) ? "BC" : "UF8 only";
        ImGui_Text(ctx, "Change primary mode?");
        ImGui_Spacing(ctx);
        char line[256];
        snprintf(line, sizeof(line),
            "  %s  →  %s",
            modeLabel_(editing->domain, editing->uf8Mode), targetLabel);
        ImGui_Text(ctx, line);
        ImGui_Spacing(ctx);
        ImGui_TextWrapped(ctx,
            "The SSL-Link slot bindings get swapped to the other domain's "
            "saved set (CS bindings stash to csSlotCache, BC bindings to "
            "bcSlotCache). Flip back any time to restore the previous "
            "bindings. UF8 bank / strip bindings stay intact.");
        ImGui_Spacing(ctx);

        if (ImGui_Button(ctx, "Apply##fxl_mode_ok", nullptr, nullptr)) {
            UserPluginMap copy = *editing;
            const uf8::Domain newDom =
                (g_pendingModePrimary == 1) ? uf8::Domain::ChannelStrip
              : (g_pendingModePrimary == 2) ? uf8::Domain::BusComp
              :                                uf8::Domain::None;
            // Stash the outgoing slot list into the matching cache so
            // a future swap restores it (Frank 2026-05-15). Only the
            // CS↔BC pair benefits — UF8-only has no slots. If a cache
            // already exists for the outgoing domain it gets replaced
            // with the latest set rather than merged: the active slots
            // are always the authoritative version.
            if (copy.domain == uf8::Domain::ChannelStrip) {
                copy.csSlotCache = copy.slots;
            } else if (copy.domain == uf8::Domain::BusComp) {
                copy.bcSlotCache = copy.slots;
            }
            // Restore the incoming domain's previously-stashed slots,
            // if any. Otherwise we land on an empty slot list as
            // before — first-time switch behaviour stays identical.
            if (newDom == uf8::Domain::ChannelStrip) {
                copy.slots = copy.csSlotCache;
            } else if (newDom == uf8::Domain::BusComp) {
                copy.slots = copy.bcSlotCache;
            } else {
                copy.slots.clear();
            }
            copy.domain  = newDom;
            copy.uf8Mode = copy.domain == uf8::Domain::None ? true : copy.uf8Mode;
            // GR-meter binding refers to a vst3Param index on the learned
            // plug-in — that's still valid across mode switches, so we
            // keep it. (User can clear it manually if they re-learn.)
            uf8::user_plugins::upsert(std::move(copy));
            persistAndReport_();
            g_pendingModeMatch.clear();
            g_pendingModePrimary = 0;
            ImGui_CloseCurrentPopup(ctx);
        }
        ImGui_SameLine(ctx, nullptr, nullptr);
        if (ImGui_Button(ctx, "Cancel##fxl_mode_cancel", nullptr, nullptr)) {
            g_pendingModeMatch.clear();
            g_pendingModePrimary = 0;
            ImGui_CloseCurrentPopup(ctx);
        }
        ImGui_EndPopup(ctx);
    }
}

} // namespace

void SettingsScreen::drawFxLearn(ImGui_Context* ctx)
{
    using namespace uf8;

    // Editor-View takes over when a map is being edited. Master-View is
    // the default. Switch is driven by g_editingMatch (set by the Edit
    // button below; cleared by the editor's "<- All maps" breadcrumb).
    if (!g_editingMatch.empty()) {
        drawFxLearnEditor_(ctx);
        return;
    }

    ImGui_Text(ctx, "FX Learn — User Plugin Maps");
    ImGui_Spacing(ctx);
    ImGui_TextWrapped(ctx,
        "Teach third-party plug-ins to behave as virtual Channel-Strip or "
        "Bus-Comp. Built-in maps (SSL CS 2 / 4K B/E/G / BC 2 / 360 Link) "
        "always win — user maps can't shadow them.");
    ImGui_Spacing(ctx);

    if (ImGui_Button(ctx, "+ New##fxl_new", nullptr, nullptr)) {
        std::memset(g_newMatch,   0, sizeof(g_newMatch));
        std::memset(g_newDisplay, 0, sizeof(g_newDisplay));
        g_newPrimaryMode = 1;
        g_newUf8Mode     = false;
        g_newError.clear();
        std::memset(g_pickerFilter, 0, sizeof(g_pickerFilter));
        g_pickerSelectedIdx = -1;
        if (g_installedFx.empty()) loadInstalledFx_();
        ImGui_OpenPopup(ctx, "fxl_new_popup", nullptr);
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (ImGui_Button(ctx, "Export…##fxl_export", nullptr, nullptr)) {
        std::string err;
        const std::string chosen = reasixty_fxLearnExportViaDialog(&err);
        if (chosen.empty()) {
            if (!err.empty()) g_lastSaveError = "Export failed: " + err;
        } else {
            g_lastSaveError = "Exported to " + chosen;
        }
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (ImGui_Button(ctx, "Import…##fxl_import", nullptr, nullptr)) {
        std::string err;
        if (reasixty_fxLearnImportViaDialog(&err)) {
            g_lastSaveError.clear();
            if (!err.empty()) g_lastSaveError = err;  // partial: in-memory ok
        } else if (!err.empty()) {
            g_lastSaveError = "Import failed: " + err;
        }
    }

    if (!g_lastSaveError.empty()) {
        ImGui_Spacing(ctx);
        // Use TextColored via packed RGBA (red, fully opaque).
        ImGui_TextColored(ctx, 0xCC4444FF, g_lastSaveError.c_str());
    }

    ImGui_Spacing(ctx);
    ImGui_Separator(ctx);
    ImGui_Spacing(ctx);

    // Lazy-load the installed-FX catalog so the Developer column has
    // something to look up. Cached for the session — same store the
    // "+ New" picker uses.
    if (g_installedFx.empty()) loadInstalledFx_();

    const auto& cat = user_plugins::get();

    if (cat.maps.empty()) {
        ImGui_TextDisabled(ctx,
            "No user plugin maps yet. Click '+ New' to teach a third-party "
            "plug-in.");
    } else {
        // Search field — matches case-insensitively against either the
        // map's `match` (FX name) or its derived developer string.
        double sw = 320;
        ImGui_PushItemWidth(ctx, sw);
        ImGui_InputTextWithHint(ctx, "##fxl_master_filter",
            "search name or developer",
            g_fxlMasterFilter,
            static_cast<int>(sizeof(g_fxlMasterFilter)),
            nullptr, nullptr);
        ImGui_PopItemWidth(ctx);
        ImGui_Spacing(ctx);

        std::string flt = g_fxlMasterFilter;
        for (auto& c : flt) c = static_cast<char>(std::tolower(c));
        auto lc = [](std::string s) {
            for (auto& c : s) c = static_cast<char>(std::tolower(c));
            return s;
        };

        // Resolve developer per row up front so the sort comparator
        // doesn't keep re-scanning g_installedFx (O(N*M) → O(N)).
        struct Row { size_t idx; std::string dev; };
        std::vector<Row> rows;
        rows.reserve(cat.maps.size());
        for (size_t i = 0; i < cat.maps.size(); ++i) {
            Row r{ i, fxlMasterDeveloperFor_(cat.maps[i].match) };
            if (!flt.empty()) {
                const std::string nameLc = lc(cat.maps[i].match);
                const std::string devLc  = lc(r.dev);
                if (nameLc.find(flt) == std::string::npos
                    && devLc.find(flt)  == std::string::npos) {
                    continue;
                }
            }
            rows.push_back(std::move(r));
        }

        // Sort state — manual instead of ImGui_TableFlags_Sortable
        // because ReaImGui's TableGetColumnSortSpecs hand-off always
        // returned colIdx=0/dir=0 here regardless of which header
        // was clicked (verified on macOS arm64 reaimgui 0.10,
        // 2026-05-17). Sortable columns render as buttons in the
        // header row.
        enum class FxlSort : uint8_t { Name, Developer };
        static FxlSort s_sortCol = FxlSort::Name;
        static bool    s_sortAsc = true;

        {
            const bool asc = s_sortAsc;
            const auto col = s_sortCol;
            std::sort(rows.begin(), rows.end(),
                [&](const Row& a, const Row& b) {
                    const std::string aKey = (col == FxlSort::Name)
                        ? lc(cat.maps[a.idx].match) : lc(a.dev);
                    const std::string bKey = (col == FxlSort::Name)
                        ? lc(cat.maps[b.idx].match) : lc(b.dev);
                    return asc ? (aKey < bKey) : (aKey > bKey);
                });
        }

        const int kCols = 7;
        int tblFlags = 0;
        // CRITICAL: each TableSetupColumn that passes a non-null init_width
        // MUST also pass WidthFixed in flags — otherwise ImGui interprets
        // the value as a stretch *weight*, which with values like 36..240
        // explodes the column layout and bricks the parent window for the
        // next frame (Begin returns false forever). See learnings.md
        // "ReaImGui TableSetupColumn pixel-width trap" (2026-05-03).
        if (ImGui_BeginTable(ctx, "fxl_master", kCols, &tblFlags,
                             nullptr, nullptr, nullptr)) {
            int wFlag = ImGui_TableColumnFlags_WidthFixed;
            // Scale every column with font size so the Short field +
            // Mode / Slots / Actions buttons all fit at Large.
            double wDefault = scaleW_(ctx, 36.0),
                   wShort   = scaleW_(ctx, 100.0),
                   wMatch   = scaleW_(ctx, 240.0),
                   wDev     = scaleW_(ctx, 140.0),
                   wDomain  = scaleW_(ctx, 72.0),
                   wSlots   = scaleW_(ctx, 64.0),
                   wActions = scaleW_(ctx, 100.0);
            ImGui_TableSetupColumn(ctx, "Default",      &wFlag, &wDefault, nullptr);
            ImGui_TableSetupColumn(ctx, "Short Max. 7", &wFlag, &wShort,   nullptr);
            ImGui_TableSetupColumn(ctx, "Name",         &wFlag, &wMatch,   nullptr);
            ImGui_TableSetupColumn(ctx, "Developer",    &wFlag, &wDev,     nullptr);
            ImGui_TableSetupColumn(ctx, "Mode",         &wFlag, &wDomain,  nullptr);
            ImGui_TableSetupColumn(ctx, "Slots",        &wFlag, &wSlots,   nullptr);
            ImGui_TableSetupColumn(ctx, "Actions",      &wFlag, &wActions, nullptr);

            // Manual header row — TableHeadersRow + ImGui sort flags
            // were no-ops on the current reaimgui build (see memory:
            // reaimgui-table-sort-broken). Static labels for non-
            // sortable columns; Name + Developer are buttons that
            // toggle direction (^ desc / v asc) or claim the sort.
            ImGui_TableNextRow(ctx, nullptr, nullptr);
            auto sortHeader = [&](const char* labelBase, FxlSort col) {
                const bool active = (s_sortCol == col);
                char btn[64];
                const char* arrow = active ? (s_sortAsc ? " v" : " ^") : "";
                snprintf(btn, sizeof(btn), "%s%s##fxl_sort_%d",
                    labelBase, arrow, static_cast<int>(col));
                if (ImGui_Button(ctx, btn, nullptr, nullptr)) {
                    if (active) s_sortAsc = !s_sortAsc;
                    else { s_sortCol = col; s_sortAsc = true; }
                }
            };
            ImGui_TableNextColumn(ctx); ImGui_TextDisabled(ctx, "Default");
            ImGui_TableNextColumn(ctx); ImGui_TextDisabled(ctx, "Short Max. 7");
            ImGui_TableNextColumn(ctx); sortHeader("Name",      FxlSort::Name);
            ImGui_TableNextColumn(ctx); sortHeader("Developer", FxlSort::Developer);
            ImGui_TableNextColumn(ctx); ImGui_TextDisabled(ctx, "Mode");
            ImGui_TableNextColumn(ctx); ImGui_TextDisabled(ctx, "Slots");
            ImGui_TableNextColumn(ctx); ImGui_TextDisabled(ctx, "Actions");

            // Index loop so per-row IDs are stable. A drop while iterating
            // would invalidate references; defer destructive actions to
            // post-loop via the popup.
            for (const auto& row : rows) {
                const size_t i = row.idx;
                const UserPluginMap& m = cat.maps[i];

                ImGui_TableNextRow(ctx, nullptr, nullptr);

                // Default — clickable star toggle.
                ImGui_TableNextColumn(ctx);
                {
                    char btnId[64];
                    snprintf(btnId, sizeof(btnId),
                        "%s##fxl_def_%zu", m.isDefault ? "*" : "-", i);
                    if (ImGui_Button(ctx, btnId, nullptr, nullptr)) {
                        UserPluginMap copy = m;
                        copy.isDefault = !copy.isDefault;
                        user_plugins::upsert(std::move(copy));
                        persistAndReport_();
                    }
                }

                // Short — editable inline so the user can rename the
                // colour-bar label without re-creating the map. Persists
                // on every keystroke (matches the V-Pot label edit
                // pattern in the FX-Learn schematic popup). Up to 7
                // chars (UF8 / UC1 colour-bar zone width).
                ImGui_TableNextColumn(ctx);
                {
                    char shortBuf[16] = {};
                    std::strncpy(shortBuf, m.displayShort.c_str(),
                                 sizeof(shortBuf) - 1);
                    char shortId[64];
                    snprintf(shortId, sizeof(shortId),
                                  "##fxl_short_%zu", i);
                    if (ImGui_InputTextWithHint(ctx, shortId, "USR",
                            shortBuf, 8, nullptr, nullptr))
                    {
                        UserPluginMap copy = m;
                        copy.displayShort = shortBuf;
                        if (copy.displayShort.size() > 7)
                            copy.displayShort.resize(7);
                        user_plugins::upsert(std::move(copy));
                        persistAndReport_();
                    }
                }

                ImGui_TableNextColumn(ctx);
                ImGui_Text(ctx, m.match.c_str());

                // Developer — parsed from the matching installed FX's
                // trailing "(Vendor)". Dimmed dash when no match.
                ImGui_TableNextColumn(ctx);
                if (row.dev.empty()) ImGui_TextDisabled(ctx, "—");
                else                 ImGui_Text(ctx, row.dev.c_str());

                ImGui_TableNextColumn(ctx);
                ImGui_Text(ctx, modeLabel_(m.domain, m.uf8Mode));

                ImGui_TableNextColumn(ctx);
                {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%zu", m.slots.size());
                    ImGui_Text(ctx, buf);
                }

                ImGui_TableNextColumn(ctx);
                {
                    char editId[64];
                    snprintf(editId, sizeof(editId),
                                  "Edit##fxl_edit_%zu", i);
                    if (ImGui_Button(ctx, editId, nullptr, nullptr)) {
                        g_editingMatch     = m.match;
                        g_listeningLinkIdx = -1;
                        std::memset(g_paramFilter, 0, sizeof(g_paramFilter));
                    }

                    ImGui_SameLine(ctx, nullptr, nullptr);
                    char delId[64];
                    snprintf(delId, sizeof(delId),
                                  "Del##fxl_del_%zu", i);
                    if (ImGui_Button(ctx, delId, nullptr, nullptr)) {
                        // Defer the OpenPopup to the outer scope so the
                        // popup ID-stack matches BeginPopupModal below;
                        // calling OpenPopup inside the table cell uses a
                        // deeper ID prefix, BeginPopupModal can't find it.
                        g_pendingDeleteMatch = m.match;
                        g_pendingDeleteOpen  = true;
                    }
                }
            }

            ImGui_EndTable(ctx);
        }
    }

    // Hoist the deferred OpenPopup for Delete out of the table-cell scope
    // (see g_pendingDeleteOpen comment).
    if (g_pendingDeleteOpen) {
        ImGui_OpenPopup(ctx, "Delete learned FX###fxl_del_popup", nullptr);
        g_pendingDeleteOpen = false;
    }

    // ---- "+ New" popup ----------------------------------------------------
    if (ImGui_BeginPopupModal(ctx, "fxl_new_popup", nullptr, nullptr)) {
        ImGui_Text(ctx, "New User Plugin Map");
        ImGui_Spacing(ctx);

        // -- Plugin browser --------------------------------------------------
        ImGui_Text(ctx, "Pick a plug-in:");
        ImGui_SameLine(ctx, nullptr, nullptr);
        char counter[64];
        snprintf(counter, sizeof(counter), "(%zu installed)",
                      g_installedFx.size());
        ImGui_TextDisabled(ctx, counter);
        ImGui_SameLine(ctx, nullptr, nullptr);
        if (ImGui_Button(ctx, "Reload##fxl_picker_reload", nullptr, nullptr)) {
            loadInstalledFx_();
            g_pickerSelectedIdx = -1;
        }

        ImGui_InputTextWithHint(ctx, "##fxl_picker_filter",
            "type to filter — e.g. 'fabfilter pro-q'",
            g_pickerFilter, static_cast<int>(sizeof(g_pickerFilter)),
            nullptr, nullptr);

        // Filterable list. Case-insensitive substring match.
        std::string flt = g_pickerFilter;
        for (auto& c : flt) c = static_cast<char>(std::tolower(c));

        // Lowercased haystacks computed lazily once per render — fine for
        // a few thousand entries; if it ever shows up in profiles we can
        // pre-compute on load.
        double childW = 0, childH = 240.0;
        int    childFlags = 0, winFlags = 0;
        if (ImGui_BeginChild(ctx, "fxl_picker_list", &childW, &childH,
                             &childFlags, &winFlags)) {
            for (size_t i = 0; i < g_installedFx.size(); ++i) {
                if (!flt.empty()) {
                    std::string lc = g_installedFx[i].name;
                    for (auto& c : lc) c = static_cast<char>(std::tolower(c));
                    if (lc.find(flt) == std::string::npos) continue;
                }
                bool selected = (int(i) == g_pickerSelectedIdx);
                int  selFlags = 0;
                char rowId[640];
                snprintf(rowId, sizeof(rowId), "%s##fxl_pick_%zu",
                              g_installedFx[i].name.c_str(), i);
                if (ImGui_Selectable(ctx, rowId, &selected, &selFlags,
                                     nullptr, nullptr)) {
                    g_pickerSelectedIdx = int(i);
                    // Auto-fill match (FX name minus "VSTn: " prefix &
                    // trailing " (vendor)") + 4-char display label.
                    std::string m = g_installedFx[i].name;
                    auto colon = m.find(": ");
                    if (colon != std::string::npos && colon < 8)
                        m = m.substr(colon + 2);
                    auto paren = m.rfind(" (");
                    if (paren != std::string::npos) m = m.substr(0, paren);
                    while (!m.empty() && m.front() == ' ') m.erase(m.begin());
                    while (!m.empty() && m.back()  == ' ') m.pop_back();
                    std::strncpy(g_newMatch, m.c_str(),
                                 sizeof(g_newMatch) - 1);
                    g_newMatch[sizeof(g_newMatch) - 1] = '\0';
                    std::string s = deriveShortLabel_(g_installedFx[i].name);
                    std::strncpy(g_newDisplay, s.c_str(), 7);
                    g_newDisplay[7] = '\0';
                }
            }
            ImGui_EndChild(ctx);
        }

        ImGui_Spacing(ctx);
        ImGui_Separator(ctx);
        ImGui_Spacing(ctx);

        ImGui_Text(ctx, "Match (substring of FX name — auto-filled, editable):");
        ImGui_InputTextWithHint(ctx, "##fxl_new_match",
            "e.g. 'FabFilter Pro-Q 4'",
            g_newMatch, static_cast<int>(sizeof(g_newMatch)),
            nullptr, nullptr);

        ImGui_Spacing(ctx);
        ImGui_Text(ctx, "Display label (1..7 chars, scribble-strip zone):");
        // displayShort caps at 7 chars; 8-byte buf includes terminator.
        ImGui_InputTextWithHint(ctx, "##fxl_new_short",
            "FFP4",
            g_newDisplay, 8, nullptr, nullptr);

        ImGui_Spacing(ctx);
        ImGui_Text(ctx, "Mode:");
        ImGui_SameLine(ctx, nullptr, nullptr);
        if (ImGui_RadioButton(ctx, "Channel-Strip##fxl_new_cs",
                              g_newPrimaryMode == 1))
            g_newPrimaryMode = 1;
        ImGui_SameLine(ctx, nullptr, nullptr);
        if (ImGui_RadioButton(ctx, "Bus-Comp##fxl_new_bc",
                              g_newPrimaryMode == 2))
            g_newPrimaryMode = 2;
        ImGui_SameLine(ctx, nullptr, nullptr);
        if (ImGui_RadioButton(ctx, "UF8 only##fxl_new_uf8only",
                              g_newPrimaryMode == 3))
        {
            g_newPrimaryMode = 3;
            g_newUf8Mode     = true;   // UF8-only requires the UF8 layer
        }

        // UF8 strip-layer checkbox. Only meaningful when the primary mode
        // is CS or BC — UF8-only locks it on (handled above).
        ImGui_Spacing(ctx);
        if (g_newPrimaryMode == 3) {
            ImGui_TextDisabled(ctx, "UF8 strip layer: enabled (required for UF8-only)");
        } else {
            ImGui_Checkbox(ctx, "Enable UF8 strip layer##fxl_new_uf8",
                           &g_newUf8Mode);
        }

        if (!g_newError.empty()) {
            ImGui_Spacing(ctx);
            ImGui_TextColored(ctx, 0xCC4444FF, g_newError.c_str());
        }

        ImGui_Spacing(ctx);
        ImGui_Separator(ctx);
        ImGui_Spacing(ctx);

        if (ImGui_Button(ctx, "Create##fxl_new_ok", nullptr, nullptr)) {
            g_newError.clear();

            std::string match = g_newMatch;
            std::string disp  = g_newDisplay;
            if (disp.size() > 7) disp.resize(7);

            // Trim leading/trailing whitespace from the match. Inner spaces
            // are preserved on purpose ('Pro-Q 4' is a real FX name).
            auto trim = [](std::string& s) {
                while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
                while (!s.empty() && (s.back()  == ' ' || s.back()  == '\t')) s.pop_back();
            };
            trim(match);

            if (match.empty()) {
                g_newError = "Match string is required.";
            } else if (disp.empty()) {
                g_newError = "Display label is required (1..7 ASCII chars).";
            } else if (g_newPrimaryMode < 1 || g_newPrimaryMode > 3) {
                g_newError = "Pick a mode.";
            } else if (user_plugins::collidesWithBuiltin(match)) {
                g_newError = "That match string collides with a built-in plugin map.";
            } else {
                // Reject duplicate match within the catalog up-front so the
                // user gets a clear message instead of a silent overwrite.
                bool dup = false;
                for (const auto& existing : user_plugins::get().maps) {
                    if (existing.match == match) { dup = true; break; }
                }
                if (dup) {
                    g_newError = "A user map with that match already exists.";
                } else {
                    UserPluginMap m;
                    m.match        = match;
                    m.displayShort = disp;
                    m.domain       = (g_newPrimaryMode == 2) ? Domain::BusComp
                                  : (g_newPrimaryMode == 1) ? Domain::ChannelStrip
                                  :                            Domain::None;
                    m.uf8Mode      = (g_newPrimaryMode == 3) ? true
                                                              : g_newUf8Mode;
                    m.isDefault    = false;
                    user_plugins::upsert(std::move(m));
                    persistAndReport_();
                    if (g_lastSaveError.empty())
                        ImGui_CloseCurrentPopup(ctx);
                    else
                        g_newError = g_lastSaveError;  // surfaced in popup too
                }
            }
        }
        ImGui_SameLine(ctx, nullptr, nullptr);
        if (ImGui_Button(ctx, "Cancel##fxl_new_cancel", nullptr, nullptr)) {
            ImGui_CloseCurrentPopup(ctx);
        }
        ImGui_EndPopup(ctx);
    }

    // ---- Delete confirm popup --------------------------------------------
    // Wider / shorter shape so the wrapped "fall back to no mapping" line
    // fits on one row and the match string isn't truncated (Frank
    // 2026-05-19). Cond_Always so resizing isn't sticky between opens.
    {
        int condAlways = ImGui_Cond_Always;
        ImGui_SetNextWindowSize(ctx, 520.0, 0.0, &condAlways);
    }
    if (ImGui_BeginPopupModal(ctx, "Delete learned FX###fxl_del_popup",
                              nullptr, nullptr)) {
        char line[256];
        snprintf(line, sizeof(line),
            "match: %s", g_pendingDeleteMatch.c_str());
        ImGui_Text(ctx, line);
        ImGui_Spacing(ctx);
        ImGui_TextWrapped(ctx,
            "Tracks hosting this plug-in fall back to no mapping.");
        ImGui_Spacing(ctx);

        if (ImGui_Button(ctx, "Delete##fxl_del_ok", nullptr, nullptr)) {
            if (!g_pendingDeleteMatch.empty())
                user_plugins::removeByMatch(g_pendingDeleteMatch);
            g_pendingDeleteMatch.clear();
            persistAndReport_();
            ImGui_CloseCurrentPopup(ctx);
        }
        ImGui_SameLine(ctx, nullptr, nullptr);
        if (ImGui_Button(ctx, "Cancel##fxl_del_cancel", nullptr, nullptr)) {
            g_pendingDeleteMatch.clear();
            ImGui_CloseCurrentPopup(ctx);
        }
        ImGui_EndPopup(ctx);
    }
}

// ---- Modes ---------------------------------------------------------------
// Per-Selection-Mode tweaks. Each section enables behaviour that only
// applies while that Selection Mode is engaged on the surface. Empty
// sections stay as headers so users see what's planned.
void SettingsScreen::drawModes(ImGui_Context* ctx)
{
    ImGui_Text(ctx, "Modes");
    ImGui_Spacing(ctx);
    ImGui_Separator(ctx);
    ImGui_Spacing(ctx);

    // Phase 2.8c — split the long flat section list into 6 sub-tabs.
    // ExtState "modes_subtab" remembers the last-active tab across
    // Settings opens. Restore is one-shot per Modes-pane entry: after
    // the first frame SetSelected is dropped so user clicks own the
    // active tab. drawModes only runs while the parent's Modes pane
    // is visible — detect the gap between calls (frame counter jumps
    // by >1) to re-arm the restore each time the user navigates
    // away and back.
    static int  s_savedTab      = -1;
    static bool s_savedConsumed = false;
    static int  s_lastWritten   = -1;
    static int  s_lastFrameSeen = -1;
    if (s_savedTab < 0) {
        const char* saved = GetExtState("rea_sixty", "modes_subtab");
        s_savedTab = (saved && *saved) ? std::atoi(saved) : 0;
        if (s_savedTab < 0 || s_savedTab > 4) s_savedTab = 0;
        s_lastWritten   = s_savedTab;
        s_savedConsumed = false;
    }
    const int frame = ImGui_GetFrameCount(ctx);
    if (s_lastFrameSeen >= 0 && frame > s_lastFrameSeen + 1) {
        s_savedConsumed = false;   // re-arm restore on pane re-entry
    }
    s_lastFrameSeen = frame;

    auto persistActive = [&](int idx) {
        if (idx == s_lastWritten) return;
        s_lastWritten = idx;
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", idx);
        SetExtState("rea_sixty", "modes_subtab", buf, true);
    };

    auto tabFlagsFor = [&](int idx) -> int {
        if (!s_savedConsumed && idx == s_savedTab) {
            return ImGui_TabItemFlags_SetSelected;
        }
        return 0;
    };

    int tabBarFlags = 0;
    if (!ImGui_BeginTabBar(ctx, "modes_subtabs", &tabBarFlags)) {
        return;
    }

    // --- AUTO -------------------------------------------------------
    int flagsAuto = tabFlagsFor(0);
    if (ImGui_BeginTabItem(ctx, "AUTO", nullptr, &flagsAuto)) {
        persistActive(0);
    bool hideRead = reasixty_autoHideReadTrim();
    if (ImGui_Checkbox(ctx,
                       "Show only tracks armed for automation writing "
                       "(hide Trim / Read)",
                       &hideRead))
    {
        reasixty_setAutoHideReadTrim(hideRead);
    }
    ImGui_Text(ctx,
        "  Active only while AUTO Selection Mode is engaged. Touch / "
        "Write / Latch / Latch-Preview tracks remain visible.");

    ImGui_Spacing(ctx);
    bool fillRight = reasixty_autoFillFromRight();
    if (ImGui_RadioButton(ctx, "Fill from left##auto_fill_left", !fillRight)) {
        reasixty_setAutoFillFromRight(false);
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (ImGui_RadioButton(ctx, "Fill from right##auto_fill_right", fillRight)) {
        reasixty_setAutoFillFromRight(true);
    }
    ImGui_Text(ctx,
        "  When fewer visible tracks than the 8 hardware strips, choose "
        "which side they collect on. Project order is preserved either "
        "way. Active only while AUTO Selection Mode is engaged.");

    ImGui_Spacing(ctx);
    // Selection-Set Auto-Mode binding. One global setting: whenever
    // any Selection Set is recalled, its tracks switch to this mode;
    // when the set is deactivated, they revert to Trim/Read (mode 0).
    // "None" disables the feature so selset recall leaves automation
    // modes untouched.
    const int amCur = reasixty_selsetAutoMode();
    const char* amLabels[7] = {
        "None", "Trim/Off", "Read", "Touch",
        "Write", "Latch", "Latch Preview"
    };
    const char* amPreview = (amCur < 0 || amCur > 5)
        ? amLabels[0] : amLabels[amCur + 1];
    ImGui_Text(ctx, "Selection-Set Auto-Mode:");
    ImGui_SameLine(ctx, nullptr, nullptr);
    ImGui_SetNextItemWidth(ctx, 160);
    if (ImGui_BeginCombo(ctx, "##selset_auto_mode", amPreview, nullptr)) {
        for (int opt = -1; opt <= 5; ++opt) {
            bool sel = (opt == amCur);
            if (ImGui_Selectable(ctx, amLabels[opt + 1], &sel,
                                 nullptr, nullptr, nullptr))
            {
                reasixty_setSelsetAutoMode(opt);
            }
        }
        ImGui_EndCombo(ctx);
    }
    ImGui_Text(ctx,
        "  When a Selection Set is recalled, its member tracks are "
        "forced into this REAPER automation mode. Deactivating the set "
        "reverts those tracks to Trim/Read (mode 0). None = disabled.");

        ImGui_EndTabItem(ctx);
    }

    // --- FX / Cycle -------------------------------------------------
    int flagsFx = tabFlagsFor(1);
    if (ImGui_BeginTabItem(ctx, "FX / Cycle", nullptr, &flagsFx)) {
        persistActive(1);
    ImGui_Text(ctx,
        "Active only while SEL Mode is on FX Cycle or Instance Cycle. "
        "Pick which physical controls drive the cycle; the active FX "
        "opens in the chosen view (V-Pot push only).");
    ImGui_Spacing(ctx);

    // Controls — multi-select. Bit 0 = V-POTS, 1 = UF8 Channel Encoder,
    // 2 = UC1 Encoder 1, 3 = UC1 Encoder 2. Unticked controls keep their
    // normal-state behaviour even while SEL Mode is engaged.
    constexpr int kBitVpots   = 0x01;
    constexpr int kBitUf8Enc  = 0x02;
    constexpr int kBitUc1Enc1 = 0x04;
    constexpr int kBitUc1Enc2 = 0x08;
    int ctlMask = reasixty_cycleControlMask();
    ImGui_Text(ctx, "Controls (multi-select):");
    auto bitToggle = [&](const char* label, int bit) {
        bool on = (ctlMask & bit) != 0;
        if (ImGui_Checkbox(ctx, label, &on)) {
            if (on) ctlMask |=  bit;
            else    ctlMask &= ~bit;
            reasixty_setCycleControlMask(ctlMask);
        }
    };
    bitToggle("UF8 V-Pots (per-strip cycle)##cycle_ctl_vpots", kBitVpots);
    bitToggle("UF8 Channel Encoder##cycle_ctl_uf8enc",         kBitUf8Enc);
    bitToggle("UC1 Encoder 1 (CHANNEL)##cycle_ctl_uc1enc1",    kBitUc1Enc1);
    bitToggle("UC1 Encoder 2 (BC)##cycle_ctl_uc1enc2",         kBitUc1Enc2);
    ImGui_Text(ctx,
        "  V-Pots cycle per-strip (each strip's own track). The three "
        "single encoders cycle the focused track and override their "
        "normal function while SEL Mode is engaged.");

    ImGui_Spacing(ctx);
    ImGui_Text(ctx, "V-Pot push opens active FX as:");
    int openMode = reasixty_cycleOpenMode();
    if (ImGui_RadioButton(ctx, "Floating window##cycle_open_float",
                          openMode == 0))
    {
        reasixty_setCycleOpenMode(0);
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (ImGui_RadioButton(ctx, "FX chain##cycle_open_chain",
                          openMode == 1))
    {
        reasixty_setCycleOpenMode(1);
    }

        ImGui_EndTabItem(ctx);
    }

    // --- REC --------------------------------------------------------
    // (The former Modes → "Device" sub-tab was removed 2026-05-20 —
    // Plug-in GUI auto-engage + Alt-drag snap-back moved into the main
    // Device sidebar pane. There's no SEL Mode called "Device", so the
    // sub-tab didn't belong here.)
    int flagsRec = tabFlagsFor(2);
    if (ImGui_BeginTabItem(ctx, "REC", nullptr, &flagsRec)) {
        persistActive(2);
    bool rmeOn = reasixty_recRmeEnabled();
    if (ImGui_Checkbox(ctx,
                       "Enable RME / TotalReaper integration",
                       &rmeOn))
    {
        reasixty_setRecRmeEnabled(rmeOn);
    }
    ImGui_Text(ctx,
        "  Requires the TotalReaper extension. While REC Selection Mode "
        "is active, the assignments below dispatch TotalReaper named "
        "actions against the strip's track. \"None\" leaves the button's "
        "default REC behaviour intact.");

    if (rmeOn) {
        ImGui_Spacing(ctx);
        bool gainRot = reasixty_recVpotRotateGain();
        if (ImGui_Checkbox(ctx,
                           "V-Pot rotation → Preamp gain ±1 dB",
                           &gainRot))
        {
            reasixty_setRecVpotRotateGain(gainRot);
        }
        bool shiftInputCh = reasixty_recVpotShiftInputCh();
        if (ImGui_Checkbox(ctx,
                           "V-Pot rotation + Shift → Change input channel",
                           &shiftInputCh))
        {
            reasixty_setRecVpotShiftInputCh(shiftInputCh);
        }

        // Per-button action assignment. Index order MUST match
        // RecRmeAction enum in main.cpp (None=0, Toggle48V=1, …).
        // BeginCombo + Selectable instead of ImGui_Combo: see line 2272 —
        // ReaImGui's ImGui_Combo silently renders nothing for the
        // \0-separated-items form here.
        static const char* kRecBtnNames[] = {
            "None",
            "Toggle 48V phantom",
            "Toggle pad",
            "Toggle phase invert",
            "Toggle AutoLevel",
        };
        constexpr int kRecBtnCount =
            static_cast<int>(sizeof(kRecBtnNames) / sizeof(kRecBtnNames[0]));

        auto pickerCombo = [&](const char* label, int curIdx,
                               void (*setter)(int))
        {
            const int clamped = std::clamp(curIdx, 0, kRecBtnCount - 1);
            ImGui_PushItemWidth(ctx, 240.0);
            if (ImGui_BeginCombo(ctx, label,
                                 kRecBtnNames[clamped], nullptr))
            {
                for (int i = 0; i < kRecBtnCount; ++i) {
                    bool sel = (i == clamped);
                    if (ImGui_Selectable(ctx, kRecBtnNames[i], &sel,
                                         nullptr, nullptr, nullptr))
                    {
                        setter(i);
                    }
                }
                ImGui_EndCombo(ctx);
            }
            ImGui_PopItemWidth(ctx);
        };

        pickerCombo("V-Pot push##rec_rme",
                    reasixty_recVpotPush(),  reasixty_setRecVpotPush);
        pickerCombo("Cut button##rec_rme",
                    reasixty_recCut(),       reasixty_setRecCut);
        pickerCombo("Solo button##rec_rme",
                    reasixty_recSolo(),      reasixty_setRecSolo);
    }

        ImGui_EndTabItem(ctx);
    }

    // --- NAV --------------------------------------------------------
    int flagsNav = tabFlagsFor(3);
    if (ImGui_BeginTabItem(ctx, "NAV", nullptr, &flagsNav)) {
        persistActive(3);

    // Activation — read-only mirror of the three bindable Nav toggles.
    // Shows which physical button currently fires each. The bind UI
    // lives in Settings → Bindings; we just point the user there.
    ImGui_Text(ctx, "Activation");
    ImGui_Separator(ctx);
    ImGui_Text(ctx,
        "Nav Mode is toggled via three bindable builtins. Assign any "
        "of these to a hardware button in Settings → Bindings:");
    ImGui_Spacing(ctx);

    auto modShort = [](uf8::bindings::Modifier m) -> const char* {
        switch (m) {
            case uf8::bindings::Modifier::Plain: return "";
            case uf8::bindings::Modifier::Shift: return " + Shift";
            case uf8::bindings::Modifier::Cmd:   return " + Cmd";
            case uf8::bindings::Modifier::Ctrl:  return " + Ctrl";
        }
        return "";
    };
    auto navMirror = [&](const char* builtinName,
                         const char* friendly)
    {
        int layer = 0;
        uf8::bindings::ButtonId id = uf8::bindings::ButtonId::None;
        uf8::bindings::Modifier m  = uf8::bindings::Modifier::Plain;
        bool longPress = false;
        const bool found = uf8::bindings::findFirstBoundTo(
            builtinName, &layer, &id, &m, &longPress);
        char line[256];
        if (found) {
            const char* name = uf8::bindings::toName(id);
            const char* lp   = longPress ? " (long press)" : "";
            snprintf(line, sizeof(line), "%s — L%d %s%s%s",
                          friendly,
                          layer + 1,
                          name ? name : "?",
                          modShort(m),
                          lp);
        } else {
            snprintf(line, sizeof(line), "%s — (unbound)", friendly);
        }
        ImGui_BulletText(ctx, line);
    };
    navMirror("marker_overlay_toggle",
              "Nav Mode (Markers & Regions): toggle");
    navMirror("marker_overlay_markers_only_toggle",
              "Nav Mode: Markers only (no drill)");
    navMirror("marker_overlay_regions_only_toggle",
              "Nav Mode: Regions only (no drill)");

    ImGui_Spacing(ctx);
    ImGui_Spacing(ctx);
    ImGui_Text(ctx, "View defaults");
    ImGui_Separator(ctx);

    ImGui_Text(ctx, "Default view on Nav Mode entry:");
    int dv = reasixty_navDefaultView();
    if (ImGui_RadioButton(ctx, "Regions##nav_dv_regions", dv == 0)) {
        reasixty_setNavDefaultView(0);
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (ImGui_RadioButton(ctx,
            "Markers in current region##nav_dv_mir", dv == 1))
    {
        reasixty_setNavDefaultView(1);
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (ImGui_RadioButton(ctx, "Markers (all)##nav_dv_all", dv == 2)) {
        reasixty_setNavDefaultView(2);
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (ImGui_RadioButton(ctx, "Last used##nav_dv_last", dv == 3)) {
        reasixty_setNavDefaultView(3);
    }
    ImGui_Text(ctx,
        "  Applied only by the plain marker_overlay_toggle. "
        "'Markers in current region' snaps to the region under the "
        "playhead; falls back to Regions if the playhead is in a gap.");

    ImGui_Spacing(ctx);
    ImGui_Text(ctx, "Region-press behaviour (UF8 top-soft-key):");
    int rp = reasixty_navRegionPress();
    if (ImGui_RadioButton(ctx, "Jump + Drill##nav_rp_both", rp == 0)) {
        reasixty_setNavRegionPress(0);
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (ImGui_RadioButton(ctx, "Jump only##nav_rp_jump", rp == 1)) {
        reasixty_setNavRegionPress(1);
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (ImGui_RadioButton(ctx, "Drill only##nav_rp_drill", rp == 2)) {
        reasixty_setNavRegionPress(2);
    }
    ImGui_Text(ctx,
        "  What a tap on a region's top-soft-key does. Jump = move "
        "transport to the region start; Drill = enter the region's "
        "marker list. RegionsOnly view-lock always suppresses Drill.");

    ImGui_Spacing(ctx);
    ImGui_Spacing(ctx);
    ImGui_Text(ctx, "UF8 strip display");
    ImGui_Separator(ctx);

    ImGui_Text(ctx, "Lower-row format:");
    int lr = reasixty_navLowerRow();
    if (ImGui_RadioButton(ctx, "Off (V-Pot value)##nav_lr_off", lr == 0)) {
        reasixty_setNavLowerRow(0);
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (ImGui_RadioButton(ctx, "Index (R03 / M07)##nav_lr_idx", lr == 1)) {
        reasixty_setNavLowerRow(1);
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (ImGui_RadioButton(ctx, "Timecode (MM:SS)##nav_lr_tc", lr == 2)) {
        reasixty_setNavLowerRow(2);
    }
    ImGui_Text(ctx,
        "  Off keeps the V-Pot value visible. Index / Timecode "
        "overlay marker metadata on the lower row (Phase 2.8a left "
        "the V-Pot field untouched by default).");

    ImGui_Spacing(ctx);
    bool pag = reasixty_navPaginate();
    if (ImGui_Checkbox(ctx,
            "Pagination hints (<<, >>) on strip 0 / 7 lower row",
            &pag))
    {
        reasixty_setNavPaginate(pag);
    }
    ImGui_Text(ctx,
        "  Only meaningful when Lower-row format is Index or "
        "Timecode. Hints replace the strip's lower-row text when a "
        "prev / next page exists.");

    ImGui_Spacing(ctx);
    ImGui_Text(ctx, "Color-bar source:");
    int cb = reasixty_navColorBar();
    if (ImGui_RadioButton(ctx,
            "REAPER marker colour##nav_cb_reaper", cb == 0))
    {
        reasixty_setNavColorBar(0);
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (ImGui_RadioButton(ctx,
            "Force palette grey##nav_cb_grey", cb == 1))
    {
        reasixty_setNavColorBar(1);
    }
    ImGui_Text(ctx,
        "  REAPER honours the colour override set on each marker / "
        "region. Force grey suppresses it so the cursor's "
        "top-soft-key ring is the only colour cue.");

    ImGui_Spacing(ctx);
    ImGui_Spacing(ctx);
    ImGui_Text(ctx, "UC1 Encoder 2");
    ImGui_Separator(ctx);

    bool takeover = reasixty_navUc1Takeover() != 0;
    if (ImGui_Checkbox(ctx,
            "Take over UC1 Encoder 2 while Nav Mode is active",
            &takeover))
    {
        reasixty_setNavUc1Takeover(takeover);
    }
    ImGui_Text(ctx,
        "  When off, Encoder 2 rotation stays bound to its normal "
        "action (bc_track_scroll by default) and the UC1 LCD does not "
        "switch to the marker carousel — only UF8 reflects Nav Mode.");

    ImGui_Spacing(ctx);
    ImGui_Text(ctx, "Carousel scope:");
    ImGui_RadioButton(ctx, "Mirror UF8 view##nav_uc1_scope_mirror", true);
    ImGui_Text(ctx,
        "  Other scopes (Always Regions / Always Markers / Always "
        "Markers-in-UF8-region) arrive in Phase 2.8d.");

    ImGui_Spacing(ctx);
    ImGui_Text(ctx, "Plain push action:");
    int up = reasixty_navUc1Push();
    if (ImGui_RadioButton(ctx, "Jump + Drill##nav_uc1_push_both", up == 0)) {
        reasixty_setNavUc1Push(0);
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (ImGui_RadioButton(ctx, "Jump only##nav_uc1_push_jump", up == 1)) {
        reasixty_setNavUc1Push(1);
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (ImGui_RadioButton(ctx, "Drill only##nav_uc1_push_drill", up == 2)) {
        reasixty_setNavUc1Push(2);
    }

    ImGui_Spacing(ctx);
    ImGui_Text(ctx, "Shift + push action:");
    int us = reasixty_navUc1PushShift();
    if (ImGui_RadioButton(ctx, "Drill##nav_uc1_pshift_drill", us == 0)) {
        reasixty_setNavUc1PushShift(0);
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (ImGui_RadioButton(ctx, "Back##nav_uc1_pshift_back", us == 1)) {
        reasixty_setNavUc1PushShift(1);
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (ImGui_RadioButton(ctx, "Toggle View##nav_uc1_pshift_toggle", us == 2)) {
        reasixty_setNavUc1PushShift(2);
    }

    ImGui_Spacing(ctx);
    ImGui_Text(ctx, "Long-press action:");
    int ul = reasixty_navUc1LongPress();
    if (ImGui_RadioButton(ctx, "Back##nav_uc1_long_back", ul == 0)) {
        reasixty_setNavUc1LongPress(0);
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (ImGui_RadioButton(ctx, "Add marker at playhead##nav_uc1_long_addmark",
                          ul == 1))
    {
        reasixty_setNavUc1LongPress(1);
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (ImGui_RadioButton(ctx, "Disabled##nav_uc1_long_off", ul == 2)) {
        reasixty_setNavUc1LongPress(2);
    }
    ImGui_Text(ctx,
        "  Long-press threshold is ~500 ms. While a view-lock toggle "
        "(Markers-only / Regions-only) is engaged, shift and long-press "
        "are suppressed — only plain push fires.");

    ImGui_Spacing(ctx);
    ImGui_Spacing(ctx);
    ImGui_Text(ctx, "Auto-Follow");
    ImGui_Separator(ctx);
    bool autoFollow = reasixty_navAutoFollow();
    if (ImGui_Checkbox(ctx,
                       "Auto-Follow playhead / edit cursor",
                       &autoFollow))
    {
        reasixty_setNavAutoFollow(autoFollow);
    }
    ImGui_Text(ctx,
        "  While Nav Mode is active, the cursor strip tracks whichever "
        "marker / region the playhead is on (or the edit cursor when "
        "stopped). In Markers-in-Region view, the overlay auto-rolls "
        "into the next region when the playhead crosses out.");
        ImGui_EndTabItem(ctx);
    }

    ImGui_EndTabBar(ctx);

    s_savedConsumed = true;
}

// ---- Selection Sets -------------------------------------------------------
// Phase 2.5b — eight project-scoped slots, each either a snapshot of
// REAPER track GUIDs or a live binding to a REAPER track group (1..64,
// ANY-category membership). Recall is toggle. Group bindings update
// live each tick — no manual refresh needed.
void SettingsScreen::drawSelectionSets(ImGui_Context* ctx)
{
    ImGui_Text(ctx, "Selection Sets");
    ImGui_Spacing(ctx);
    ImGui_Separator(ctx);
    ImGui_Spacing(ctx);
    ImGui_Text(ctx,
        "Eight slots. Recall toggles — press the active slot's button "
        "again to deactivate. Filter ANDs with Folder Mode / "
        "Show-Only-Selected / AUTO-mode filters.");
    ImGui_Text(ctx,
        "  Global = workspace-wide, persists immediately. "
        "Unchecked = project-scoped, persists with project save.");
    ImGui_Spacing(ctx);

    const int active = reasixty_selsetActive();
    for (int slot = 1; slot <= 8; ++slot) {
        char idtag[32];
        snprintf(idtag, sizeof(idtag), "selset_row_%d", slot);
        ImGui_PushID(ctx, idtag);

        // Slot label + active-row marker. Active row prefixes with a dot
        // since ReaImGui v0.10 has no reliable cell-background paint.
        char header[24];
        snprintf(header, sizeof(header), "%s Slot %d",
                      (active == slot) ? "•" : " ", slot);
        ImGui_Text(ctx, header);
        ImGui_SameLine(ctx, nullptr, nullptr);

        // Global checkbox — when on, the slot's content lives in
        // ExtState (workspace-global, persisted immediately). When off,
        // ProjExtState (per-project, only written to disk on project
        // save). Group slots benefit most from Global since "group N"
        // is a stable concept across projects.
        bool isGlobal = reasixty_selsetGlobal(slot);
        if (ImGui_Checkbox(ctx, "Global##gl", &isGlobal)) {
            reasixty_setSelsetGlobal(slot, isGlobal);
        }
        ImGui_SameLine(ctx, nullptr, nullptr);

        // Type picker — Snapshot or Group. Default Snapshot.
        const int typeInt = reasixty_selsetType(slot);
        const char* preview = (typeInt == 1) ? "Group" : "Snapshot";
        ImGui_SetNextItemWidth(ctx, scaleW_(ctx, 90.0));
        if (ImGui_BeginCombo(ctx, "##type", preview, nullptr)) {
            bool isSnap = (typeInt == 0);
            bool isGrp  = (typeInt == 1);
            if (ImGui_Selectable(ctx, "Snapshot", &isSnap,
                                 nullptr, nullptr, nullptr))
            {
                reasixty_setSelsetType(slot, 0);
            }
            if (ImGui_Selectable(ctx, "Group", &isGrp,
                                 nullptr, nullptr, nullptr))
            {
                reasixty_setSelsetType(slot, 1);
            }
            ImGui_EndCombo(ctx);
        }
        ImGui_SameLine(ctx, nullptr, nullptr);

        // Name field.
        char nameBuf[64] = {0};
        std::strncpy(nameBuf, reasixty_selsetName(slot), sizeof(nameBuf) - 1);
        ImGui_SetNextItemWidth(ctx, scaleW_(ctx, 180.0));
        if (ImGui_InputTextWithHint(ctx, "##name",
                                    "Slot name",
                                    nameBuf, sizeof(nameBuf),
                                    nullptr, nullptr))
        {
            reasixty_setSelsetName(slot, nameBuf);
        }
        ImGui_SameLine(ctx, nullptr, nullptr);

        // Group spinner only for Group slots.
        if (typeInt == 1) {
            int g = reasixty_selsetGroupIdx(slot);
            ImGui_SetNextItemWidth(ctx, scaleW_(ctx, 80.0));
            if (ImGui_InputInt(ctx, "Grp##gi", &g,
                               nullptr, nullptr, nullptr))
            {
                reasixty_setSelsetGroupIdx(slot, g);
            }
            ImGui_SameLine(ctx, nullptr, nullptr);
        }

        // Live track count.
        char info[32];
        snprintf(info, sizeof(info), "(%d tracks)",
                      reasixty_selsetTrackCount(slot));
        ImGui_Text(ctx, info);
        ImGui_SameLine(ctx, nullptr, nullptr);

        // Action buttons. Save only makes sense for Snapshot (it
        // overwrites the slot's GUID list with the current REAPER
        // selection — and converts a Group slot to Snapshot if
        // pressed). Recall toggles the active slot regardless of type.
        if (ImGui_Button(ctx, "Save##sv", 0, 0)) {
            reasixty_selsetSaveCurrent(slot);
        }
        ImGui_SameLine(ctx, nullptr, nullptr);
        const char* recallLabel = (active == slot)
            ? "Deactivate##rc" : "Recall##rc";
        if (ImGui_Button(ctx, recallLabel, 0, 0)) {
            reasixty_selsetRecallToggle(slot);
        }
        ImGui_SameLine(ctx, nullptr, nullptr);
        if (ImGui_Button(ctx, "Clear##cl", 0, 0)) {
            reasixty_selsetClear(slot);
        }

        ImGui_PopID(ctx);
    }

    ImGui_Spacing(ctx);
    ImGui_Spacing(ctx);
    ImGui_Text(ctx,
        "Hardware: bind buttons to \"Recall Selection Slot (toggle)\" "
        "(param 1..8) and / or");
    ImGui_Text(ctx,
        "\"Save current REAPER selection to slot\" (param 1..8).");
    ImGui_Text(ctx,
        "Group slots refresh live from REAPER track-group membership "
        "(ANY category).");
    ImGui_Text(ctx,
        "Selection-Set Auto-Mode is configured globally in Modes \xe2\x86\x92 Auto.");
}

// ---- Parameter Groups -----------------------------------------------------
// Eight slots. Each slot persists name + active flag in
// parameter_groups.json. Track membership is stored per-track in
// REAPER's P_EXT:reasixty:pg_mask. When at least one slot is active,
// any hardware-originated parameter write (UF8 V-Pots, UF8 Soft-Keys,
// UC1 CS/BC encoders + buttons) fans out from the leader (focused
// track) onto every member of every active slot.
void SettingsScreen::drawParameterGroups(ImGui_Context* ctx)
{
    ImGui_Text(ctx, "Parameter Groups");
    ImGui_Spacing(ctx);
    ImGui_Separator(ctx);
    ImGui_Spacing(ctx);
    ImGui_Text(ctx,
        "Eight slots. While a slot is active, plug-in tweaks on the "
        "focused track copy to every member track.");
    ImGui_Spacing(ctx);

    bool temp = uf8::param_groups::multiSelectAsTempGroup();
    if (ImGui_Checkbox(ctx,
            "Multi-Select acts as temporary Parameter Group",
            &temp))
    {
        uf8::param_groups::setMultiSelectAsTempGroup(temp);
    }
    ImGui_Text(ctx,
        "  No slot active + multiple tracks selected = those tracks "
        "are the group.");
    ImGui_Spacing(ctx);
    ImGui_Separator(ctx);
    ImGui_Spacing(ctx);

    // Count members per slot by walking all tracks once.
    int memberCount[uf8::param_groups::kSlotCount] = {0};
    const int nTracks = CountTracks(nullptr);
    for (int i = 0; i < nTracks; ++i) {
        MediaTrack* t = GetTrack(nullptr, i);
        if (!t) continue;
        const uint8_t m = uf8::param_groups::getMaskForTrack(t);
        for (int s = 0; s < uf8::param_groups::kSlotCount; ++s) {
            if (m & (1u << s)) memberCount[s]++;
        }
    }

    auto& st = uf8::param_groups::state();
    for (int slot = 0; slot < uf8::param_groups::kSlotCount; ++slot) {
        char idtag[32];
        snprintf(idtag, sizeof(idtag), "param_group_row_%d", slot);
        ImGui_PushID(ctx, idtag);

        // Active dot prefix mirrors the Selection Sets pattern.
        char header[24];
        snprintf(header, sizeof(header), "%s Slot %d",
                      st.slots[slot].active ? "\xe2\x80\xa2" : " ",
                      slot + 1);
        ImGui_Text(ctx, header);
        ImGui_SameLine(ctx, nullptr, nullptr);

        bool active = st.slots[slot].active;
        if (ImGui_Checkbox(ctx, "Active##act", &active)) {
            uf8::param_groups::toggleGroupActive(slot);
        }
        ImGui_SameLine(ctx, nullptr, nullptr);

        char nameBuf[64] = {0};
        std::strncpy(nameBuf, st.slots[slot].name.c_str(),
                     sizeof(nameBuf) - 1);
        ImGui_SetNextItemWidth(ctx, scaleW_(ctx, 220.0));
        if (ImGui_InputTextWithHint(ctx, "##name",
                                    "Slot name (e.g. Drums)",
                                    nameBuf, sizeof(nameBuf),
                                    nullptr, nullptr))
        {
            st.slots[slot].name = nameBuf;
            uf8::param_groups::save();
        }
        ImGui_SameLine(ctx, nullptr, nullptr);

        char info[40];
        snprintf(info, sizeof(info), "(%d members)", memberCount[slot]);
        ImGui_Text(ctx, info);
        ImGui_SameLine(ctx, nullptr, nullptr);

        if (ImGui_Button(ctx, "Add Selected##add", nullptr, nullptr)) {
            uf8::param_groups::addSelectedToGroup(slot);
        }
        ImGui_SameLine(ctx, nullptr, nullptr);
        if (ImGui_Button(ctx, "Clear##clr", nullptr, nullptr)) {
            uf8::param_groups::clearGroupMembership(slot);
        }

        ImGui_PopID(ctx);
    }

    ImGui_Spacing(ctx);
    ImGui_Separator(ctx);
    ImGui_Spacing(ctx);
    if (ImGui_Button(ctx,
            "Remove Selected Tracks from All Groups",
            nullptr, nullptr))
    {
        uf8::param_groups::removeSelectedFromAllGroups();
    }
    ImGui_Spacing(ctx);
    ImGui_Text(ctx,
        "Drive slots from hardware via the Param Group actions in "
        "Settings \xe2\x86\x92 Bindings.");
}

// ---- About ----------------------------------------------------------------
// Static text + a few buttons that shell out to `open` for browser /
// Finder reveal. No fancy hyperlink widget — ReaImGui v0.10 has none we
// can rely on; plain Text + Button is cross-version safe.
void SettingsScreen::drawAbout(ImGui_Context* ctx)
{
    ImGui_Text(ctx, "Rea-Sixty");
    ImGui_Text(ctx, "Open-source SSL 360 replacement for UF8 / UC1");
    ImGui_Spacing(ctx);
    static const char* kAuthorUrl = "https://stoersender-studio.ch";
    static const char* kBeerUrl   = "https://paypal.me/FrankAcklin";
    ImGui_Text(ctx,
        "Made by Frank Acklin @ Stoersender Studio, Switzerland");
    if (ImGui_Button(ctx, "stoersender-studio.ch",
                     /*size_w*/ nullptr, /*size_h*/ nullptr))
    {
        reasixty_openUrl(kAuthorUrl);
    }
    {
        char buf[96];
        snprintf(buf, sizeof(buf),
            "This project took %d commits so far.", REASIXTY_COMMIT_COUNT);
        ImGui_Text(ctx, buf);
    }
    ImGui_Text(ctx, "You can buy me a beer:");
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (ImGui_Button(ctx, "paypal.me/FrankAcklin",
                     /*size_w*/ nullptr, /*size_h*/ nullptr))
    {
        reasixty_openUrl(kBeerUrl);
    }
    ImGui_Spacing(ctx);
    ImGui_Spacing(ctx);

    ImGui_Text(ctx, "Versions");
    ImGui_Separator(ctx);
    char line[256];
    snprintf(line, sizeof(line), "  Build:    %s %s",
                  __DATE__, __TIME__);
    ImGui_Text(ctx, line);
    snprintf(line, sizeof(line), "  REAPER:   %s",
                  reasixty_reaperVersion());
    ImGui_Text(ctx, line);
    ImGui_Text(ctx, "  ReaImGui: bundled v0.10 ABI");

    ImGui_Spacing(ctx);
    ImGui_Spacing(ctx);
    ImGui_Text(ctx, "Project");
    ImGui_Separator(ctx);
    static const char* kRepoUrl = "https://github.com/acklin83/Rea-Sixty";
    snprintf(line, sizeof(line), "  Repository:  %s", kRepoUrl);
    ImGui_Text(ctx, line);
    if (ImGui_Button(ctx, "Open repository in browser",
                     /*size_w*/ nullptr, /*size_h*/ nullptr)) {
        reasixty_openUrl(kRepoUrl);
    }

    ImGui_Spacing(ctx);
    ImGui_Spacing(ctx);
    ImGui_Text(ctx, "Setup");
    ImGui_Separator(ctx);
    ImGui_Text(ctx,
        "  Export bundles bindings, plug-in maps, parameter-group");
    ImGui_Text(ctx,
        "  slot names and every Settings preference into one file.");
    ImGui_Text(ctx,
        "  Selection Sets + Parameter Group track membership stay");
    ImGui_Text(ctx,
        "  per-project and travel with the .RPP.");
    ImGui_Spacing(ctx);
    static std::string s_setupMsg;
    if (ImGui_Button(ctx, "Export setup…##setup_export",
                     /*size_w*/ nullptr, /*size_h*/ nullptr))
    {
        std::string err;
        const std::string chosen = reasixty_setupExportViaDialog(&err);
        if (chosen.empty()) {
            s_setupMsg = err.empty() ? "" : "Export failed: " + err;
        } else {
            s_setupMsg = "Exported to " + chosen;
        }
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (ImGui_Button(ctx, "Import setup…##setup_import",
                     /*size_w*/ nullptr, /*size_h*/ nullptr))
    {
        std::string err;
        if (reasixty_setupImportViaDialog(&err)) {
            s_setupMsg = err.empty()
                ? "Setup imported."
                : ("Setup imported (warning: " + err + ")");
        } else if (!err.empty()) {
            s_setupMsg = "Import failed: " + err;
        }
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    static bool s_factoryConfirmOpen = false;
    if (ImGui_Button(ctx, "Reset to factory defaults##setup_factory",
                     /*size_w*/ nullptr, /*size_h*/ nullptr))
    {
        s_factoryConfirmOpen = true;
    }
    if (s_factoryConfirmOpen) {
        ImGui_OpenPopup(ctx,
            "Reset to factory defaults?###setup_factory_popup", nullptr);
        s_factoryConfirmOpen = false;
    }
    {
        int condAlways = ImGui_Cond_Always;
        ImGui_SetNextWindowSize(ctx, 520.0, 0.0, &condAlways);
    }
    if (ImGui_BeginPopupModal(ctx,
            "Reset to factory defaults?###setup_factory_popup",
            nullptr, nullptr))
    {
        ImGui_TextWrapped(ctx,
            "Replaces bindings, learned FX, parameter-group slot meta "
            "and every Settings preference with the baked-in factory "
            "configuration. Per-project Selection Sets and Parameter "
            "Group track memberships are not touched.");
        ImGui_Spacing(ctx);
        if (ImGui_Button(ctx, "Reset##setup_factory_ok",
                         nullptr, nullptr))
        {
            std::string err;
            if (reasixty_setupRestoreFactoryDefaults(&err)) {
                s_setupMsg = err.empty()
                    ? "Factory defaults restored."
                    : ("Factory restore warning: " + err);
            } else {
                s_setupMsg = err.empty()
                    ? "Factory restore failed."
                    : ("Factory restore failed: " + err);
            }
            ImGui_CloseCurrentPopup(ctx);
        }
        ImGui_SameLine(ctx, nullptr, nullptr);
        if (ImGui_Button(ctx, "Cancel##setup_factory_cancel",
                         nullptr, nullptr))
        {
            ImGui_CloseCurrentPopup(ctx);
        }
        ImGui_EndPopup(ctx);
    }
    if (!s_setupMsg.empty()) {
        ImGui_Text(ctx, s_setupMsg.c_str());
    }

#ifdef _WIN32
    // Windows-only: bind UF8 + UC1 to WinUSB so libusb can claim them
    // without Zadig. Single UAC prompt; replaces SSL 360°'s driver
    // (SSL 360° will stop seeing the devices afterwards).
    ImGui_Spacing(ctx);
    ImGui_Spacing(ctx);
    ImGui_Text(ctx, "Windows USB driver");
    ImGui_Separator(ctx);
    ImGui_TextWrapped(ctx,
        "  Binds UF8 + UC1 to WinUSB. One-time setup, requires admin. "
        "SSL 360° stops seeing the devices after install — reinstall "
        "SSL 360° to revert.");
    ImGui_Spacing(ctx);
    static std::string s_winusbMsg;
    if (ImGui_Button(ctx, "Install UF8/UC1 WinUSB driver##winusb_install",
                     nullptr, nullptr))
    {
        std::string err;
        if (reasixty_installWinUsbDriver(&err)) {
            s_winusbMsg = "Driver install started — follow the UAC + "
                          "publisher prompts, then unplug + replug "
                          "the devices.";
        } else {
            s_winusbMsg = err.empty()
                ? "Driver install failed."
                : ("Driver install failed: " + err);
        }
    }
    if (!s_winusbMsg.empty()) {
        ImGui_TextWrapped(ctx, s_winusbMsg.c_str());
    }
#endif

#ifdef __linux__
    // Linux equivalent — install udev rule so libusb can claim UF8 +
    // UC1 without root. Single pkexec prompt, mirrors the Windows
    // WinUSB-installer UX. Required for ReaPack-installed packages
    // (ReaPack can drop the .so but can't sudo).
    ImGui_Spacing(ctx);
    ImGui_Spacing(ctx);
    ImGui_Text(ctx, "Linux udev rule");
    ImGui_Separator(ctx);
    ImGui_TextWrapped(ctx,
        "  Grants non-root USB access to UF8 + UC1 by installing "
        "/etc/udev/rules.d/99-rea-sixty.rules. One-time setup, "
        "requires sudo (graphical password prompt).");
    ImGui_Spacing(ctx);
    static std::string s_udevMsg;
    if (ImGui_Button(ctx, "Install Linux udev rule##udev_install",
                     nullptr, nullptr))
    {
        std::string err;
        if (reasixty_installLinuxUdevRule(&err)) {
            s_udevMsg = "udev rule installed. Unplug + replug UF8 + UC1, "
                        "then restart REAPER.";
        } else {
            s_udevMsg = err.empty()
                ? "udev install failed."
                : ("udev install failed: " + err);
        }
    }
    if (!s_udevMsg.empty()) {
        ImGui_TextWrapped(ctx, s_udevMsg.c_str());
    }
#endif

    ImGui_Spacing(ctx);
    ImGui_Spacing(ctx);
    ImGui_Text(ctx, "Logs");
    ImGui_Separator(ctx);
    ImGui_Text(ctx, "  /tmp/reaper_uf8_frames.log   (frame trace, when enabled)");
    ImGui_Text(ctx, "  /tmp/reaper_uf8_colors.log   (ColorSync push log)");
    if (ImGui_Button(ctx, "Reveal /tmp in Finder",
                     /*size_w*/ nullptr, /*size_h*/ nullptr)) {
        reasixty_revealInFinder("/tmp");
    }

    ImGui_Spacing(ctx);
    ImGui_Spacing(ctx);
    ImGui_Text(ctx, "Acknowledgements");
    ImGui_Separator(ctx);
    ImGui_Text(ctx, "  Built without affiliation with Solid State Logic.");
    ImGui_Text(ctx, "  ReaImGui (cfillion) handles all on-screen rendering.");
    ImGui_Text(ctx, "  libusb drives the UF8 / UC1 vendor-USB endpoints.");
}

} // namespace uf8
