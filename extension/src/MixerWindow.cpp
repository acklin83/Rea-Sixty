#include "MixerWindow.h"
#include "SettingsScreen.h"
#include "ManualView.h"
#include "ExchangeView.h"
#include "ThemeBridge.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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

// Font + base-size accessors consumed by the Manual renderer (ManualView.cpp).
// Refreshed each frame from the live Settings context so the renderer always
// uses the context's currently-attached faces and the active UI font size.
namespace {
ImGui_Font* g_uiSans = nullptr;
ImGui_Font* g_uiBold = nullptr;
ImGui_Font* g_uiMono = nullptr;
double      g_uiFontPx = 14.0;
}
ImGui_Font* reasixty_uiSansFont() { return g_uiSans; }
ImGui_Font* reasixty_uiBoldFont() { return g_uiBold; }
ImGui_Font* reasixty_uiMonoFont() { return g_uiMono; }
double      reasixty_uiFontPx()   { return g_uiFontPx; }

namespace uf8 {

namespace {

// Left-rail navigation. Settings only — Mixer entry was removed
// 2026-05-14 after the worktree-mixer-standalone experiment was
// reverted; the Plugin Mixer view will land later as its own
// dedicated window, not as a tab in here.
// Rail ORDER is set by kRail below, not by this enum. The active tab now
// persists as RailEntry::id (a stable lowercase string), so adding or
// renumbering a section no longer repoints anybody's saved "last tab" —
// see persistSelected / applyReopenLastTab. Frank 2026-08-09.
enum Section : int {
    kSecDevices = 0,
    kSecAppearance,
    kSecBindings,
    kSecModes,
    kSecFxLearn,
    kSecFavourites,
    kSecSelectionSets,
    kSecParameterGroups,
    kSecExchange,
    kSecManual,
    kSecAbout,
    kSecBehaviour,
};

struct RailEntry {
    const char* label;
    // Stable id written to ExtState as the "last tab". Never change one of
    // these once shipped — a user's saved tab is matched against it.
    const char* id;
    Section     section;
    bool        separatorBefore;
    void (*draw)(ImGui_Context*);
};

constexpr RailEntry kRail[] = {
    { "Devices",         "devices",         kSecDevices,        false, &SettingsScreen::drawDevices         },
    { "Appearance",      "appearance",      kSecAppearance,     false, &SettingsScreen::drawAppearance      },
    { "Behaviour",       "behaviour",       kSecBehaviour,      false, &SettingsScreen::drawBehaviour       },
    { "Bindings",        "bindings",        kSecBindings,       false, &SettingsScreen::drawBindings        },
    { "Modes",           "modes",           kSecModes,          false, &SettingsScreen::drawModes           },
    { "FX Learn",        "fxlearn",         kSecFxLearn,        false, &SettingsScreen::drawFxLearn         },
    { "Favourites",      "favourites",      kSecFavourites,     false, &SettingsScreen::drawFavourites      },
    { "Selection Sets",  "selectionsets",   kSecSelectionSets,  false, &SettingsScreen::drawSelectionSets   },
    { "Parameter Groups","parametergroups", kSecParameterGroups,false, &SettingsScreen::drawParameterGroups },
    { "Exchange",        "exchange",        kSecExchange,       true,  &ExchangeView::draw                  },
    { "Manual",          "manual",          kSecManual,         false, &ManualView::draw                    },
    { "About",           "about",           kSecAbout,          false, &SettingsScreen::drawAbout           },
};

// Section ⇄ stable id. Twelve entries, scanned on a rail click and on
// window open — a linear scan is the whole implementation.
const char* railIdFor(int section)
{
    for (const RailEntry& e : kRail)
        if (e.section == section) return e.id;
    return kRail[0].id;
}
// -1 when the string matches no rail entry (unknown / legacy / empty).
int railSectionFor(const char* id)
{
    if (!id || !*id) return -1;
    for (const RailEntry& e : kRail)
        if (std::strcmp(e.id, id) == 0) return (int)e.section;
    return -1;
}

// Before 2026-08-09 the last tab was persisted as a raw index into the
// Section enum, whose order back then is reproduced here. A stored value
// that is nothing but digits is one of those indices and gets migrated
// once; anything else falls through to the first pane, exactly as an
// unparsable value always did.
constexpr const char* kLegacyTabIds[] = {
    "devices", "appearance", "bindings", "modes", "fxlearn", "favourites",
    "selectionsets", "parametergroups", "exchange", "manual", "about",
};
bool isLegacyTabIndex(const char* v)
{
    if (!v || !*v) return false;
    for (const char* p = v; *p; ++p)
        if (*p < '0' || *p > '9') return false;
    return true;
}

constexpr double kRailWidthPx = 160.0;

// ---- Rail search ----------------------------------------------------------
// A filter field sits above the pane list. Empty query → the rail renders
// exactly as it always did. Non-empty → the pane list is replaced by a result
// list; clicking a result selects that result's pane (persisted through the
// normal persistSelected path) and asks it to scroll to the owning heading.
//
// Every label below is copied verbatim out of SettingsScreen.cpp — the index
// is a mirror of the code, not a description of it. All twelve panes are
// indexed control by control. They were not at first, and "rme" (Modes → REC →
// Enable RME / TotalReaper integration) found nothing, so the scope note that
// used to sit here was wrong in practice. Still headings-only: the per-slot and
// per-plug-in rows (Favourite slots, FX-Learn maps, Selection-Set slots), which
// are user data rather than settings and change under the index.
//
// A result row is TWO lines — the setting, then "Pane › Section" a size down.
// One line could only ever fit the label at 160 px, which made two hits from
// two different panes indistinguishable. Frank 2026-08-10.
struct SearchEntry {
    const char* label;    // the control's own label
    Section     section;  // pane it lives in
    const char* group;    // section heading inside that pane ("" = pane-level)
};

constexpr SearchEntry kSearchIndex[] = {
    // -- Appearance ---------------------------------------------------------
    { "Show MCP Inserts overlay",              kSecAppearance, "On-screen" },
    { "Show focused-track panel",              kSecAppearance, "On-screen" },
    { "Show mode-change banner",               kSecAppearance, "On-screen" },
    { "CS colour",                             kSecAppearance, "On-screen" },
    { "BC colour",                             kSecAppearance, "On-screen" },
    { "Selected FX colour",                    kSecAppearance, "On-screen" },
    { "Fill opacity",                          kSecAppearance, "On-screen" },
    { "Border opacity",                        kSecAppearance, "On-screen" },
    { "Inserts row height",                    kSecAppearance, "On-screen" },
    { "Inserts top offset",                    kSecAppearance, "On-screen" },
    { "SEL LED follows REAPER track colour",   kSecAppearance, "Surface display" },
    { "Long track-name handling",              kSecAppearance, "Surface display" },
    { "Theme",                                 kSecAppearance, "Theme" },
    { "Font Size",                             kSecAppearance, "Font Size" },
    { "Spelling",                              kSecAppearance, "Spelling" },
    { "Reopen on last tab viewed",             kSecAppearance, "Settings window" },
    // -- Devices ------------------------------------------------------------
    { "Connected devices",                     kSecDevices, "Connected devices" },
    // Drawn as "  LEDs" / "  LCDs" — the two leading spaces are layout
    // indent, not part of the name.
    { "LEDs",                                  kSecDevices, "Brightness" },
    { "LCDs",                                  kSecDevices, "Brightness" },
    { "GR meter source",                       kSecDevices, "Metering" },
    { "Combine GR across plug-ins (UF8 strips)", kSecDevices, "Metering" },
    { "Combine GR across plug-ins (UC1 Comp)", kSecDevices, "Metering" },
    { "UC1 Input",                             kSecDevices, "Metering" },
    { "UC1 Output",                            kSecDevices, "Metering" },
    { "UF8 Strips",                            kSecDevices, "Metering" },
    { "Copy Input to Output",                  kSecDevices, "Metering" },
    { "Shift activates Fine mode (V-Pots / encoders, not faders)",
                                               kSecDevices, "V-Pot / encoder feel" },
    { "Fine mode steps JSFX sliders by their native increment",
                                               kSecDevices, "V-Pot / encoder feel" },
    { "UF8 V-Pot speed",                       kSecDevices, "V-Pot / encoder feel" },
    { "UF8 Fine factor",                       kSecDevices, "V-Pot / encoder feel" },
    { "UC1 encoder speed",                     kSecDevices, "V-Pot / encoder feel" },
    { "UC1 Fine factor",                       kSecDevices, "V-Pot / encoder feel" },
    { "Virtual notch zone",                    kSecDevices, "V-Pot / encoder feel" },
    { "Notch fine step",                       kSecDevices, "V-Pot / encoder feel" },
    { "Notch hold",                            kSecDevices, "V-Pot / encoder feel" },
    { "BC VU meter (0/4/8/12/16/20 dB)",       kSecDevices, "UC1 GR calibration" },
    { "CS DYN GR LEDs (3/6/10/14/20 dB)",      kSecDevices, "UC1 GR calibration" },
    // -- Behaviour ----------------------------------------------------------
    { "TCP follows UF8 selection",             kSecBehaviour, "Tracks" },
    { "Surface mirrors:",                      kSecBehaviour, "Tracks" },
    { "Pinned tracks survive banking",         kSecBehaviour, "Tracks" },
    { "Touch selects channel",                 kSecBehaviour, "Tracks" },
    { "Track selection follows parameter change",
                                               kSecBehaviour, "Tracks" },
    { "Show Master as Track 0 on UC1",         kSecBehaviour, "Master track" },
    { "Pinned Master",                         kSecBehaviour, "Master track" },
    { "Don't show offline FX",                 kSecBehaviour, "Plug-ins" },
    { "Wrap Plug-in Cycle",                    kSecBehaviour, "Plug-ins" },
    { "SSL Strip Mode follows focused plug-in window",
                                               kSecBehaviour, "Plug-ins" },
    { "Plug-in GUI follows active Instance",   kSecBehaviour, "Plug-ins" },
    { "UF1 PLUG-IN key opens the plug-in GUI", kSecBehaviour, "Plug-ins" },
    { "Auto-engage UF8 Plug-in Mode for UF8-mapped plug-ins",
                                               kSecBehaviour, "Plug-ins" },
    { "Pin plug-in GUI position",              kSecBehaviour, "Plug-ins" },
    { "Pin FX-chain GUI position",             kSecBehaviour, "Plug-ins" },
    { "Parameter change switches soft-key bank",
                                               kSecBehaviour, "Soft-keys" },
    { "Engage a fixed soft-key bank at startup",
                                               kSecBehaviour, "Soft-keys" },
    { "Use current hardware bank",             kSecBehaviour, "Soft-keys" },
    { "Alt/Option + fader drag \xE2\x86\x92 snap back to original on release",
                                               kSecBehaviour, "Keyboard" },
    { "Keyboard Shift acts as Shift modifier", kSecBehaviour, "Keyboard" },
    { "Keyboard Cmd (\xE2\x8C\x98) acts as Cmd modifier",
                                               kSecBehaviour, "Keyboard" },
    { "Keyboard Ctrl acts as Ctrl modifier",   kSecBehaviour, "Keyboard" },
    // -- Remaining panes: headings only -------------------------------------
    { "Bindings",                              kSecBindings,        "" },
    { "Modes",                                 kSecModes,           "" },
    { "AUTO",                                  kSecModes,           "" },
    { "FX / Cycle",                            kSecModes,           "" },
    { "REC",                                   kSecModes,           "" },
    { "NAV",                                   kSecModes,           "" },
    { "Nudge",                                 kSecModes,           "" },
    { "Dynamount",                             kSecModes,           "" },
    { "FX Learn",                              kSecFxLearn,         "" },
    // The three modifier-layer flags moved out of Behaviour → Keyboard into
    // the FX Learn pane (2026-08-09), so they are indexed control-deep even
    // though the rest of that pane is not — a setting that changed home is
    // the one a user is most likely to go looking for. Frank 2026-08-09.
    { "Hold Option for the FX-Learn Option layer",
                                               kSecFxLearn, "Modifier layers" },
    { "Hold Control for the FX-Learn Control layer",
                                               kSecFxLearn, "Modifier layers" },
    { "Hold Control+Option for the combined FX-Learn layer",
                                               kSecFxLearn, "Modifier layers" },
    { "Favourites",                            kSecFavourites,      "" },
    { "Selection Sets",                        kSecSelectionSets,   "" },
    { "Parameter Groups",                      kSecParameterGroups, "" },
    { "Exchange",                              kSecExchange,        "" },
    { "Mapping Exchange",                      kSecExchange,        "" },
    { "Manual",                                kSecManual,          "" },
    { "About",                                 kSecAbout,           "" },
    { "Versions",                              kSecAbout,           "" },
    // The nine older panes were indexed by section heading only, which meant a
    // setting like "Enable RME / TotalReaper integration" was unreachable —
    // Frank typed "rme" and got nothing. Their checkbox / radio / combo labels
    // now sit here too. Still headings-only for the per-slot and per-plug-in
    // rows (Favourite slots, FX-Learn maps, Selection-Set slots): those are
    // user data, not settings, and they change under us.
    // -- Bindings --
    { "Scroll banks:",                                     kSecBindings, "" },
    { "Focus Set scope:",                                  kSecBindings, "" },
    { "UF1 Extender - 9th fader of the UF8 bank",          kSecBindings, "Focus Set scope" },
    { "UF8 sends follow the UF1 Focus Set track",          kSecBindings, "Focus Set scope" },
    { "Reset this layer to factory defaults",              kSecBindings, "" },
    // -- Modes --
    { "Show only tracks armed for automation writing",     kSecModes, "AUTO" },
    { "Selection-Set Auto-Mode:",                          kSecModes, "AUTO" },
    { "Focus-Set Auto-Mode:",                              kSecModes, "AUTO" },
    { "V-Pot push opens active FX as:",                    kSecModes, "FX / Cycle" },
    { "Enable RME / TotalReaper integration",              kSecModes, "REC" },
    { "V-Pot rotation \xE2\x86\x92 Preamp gain \xC2\xB1" "1 dB",
                                                           kSecModes, "REC" },
    { "Encoder 2 rotation \xE2\x86\x92 Preamp gain \xC2\xB1" "1 dB",
                                                           kSecModes, "REC" },
    { "Auto-follow playhead / edit cursor",                kSecModes, "NAV" },
    { "Take over LCD",                                     kSecModes, "NAV" },
    { "Lower-row format:",                                 kSecModes, "NAV" },
    { "Region press (UF8 top-soft-key):",                  kSecModes, "NAV" },
    { "Playhead nudge step",                               kSecModes, "Nudge" },
    { "Amount per detent",                                 kSecModes, "Nudge" },
    // -- FX Learn --
    { "Quick-Learn skip list",                             kSecFxLearn, "" },
    { "Show EQ Graph on the UF1",                          kSecFxLearn, "UF1 layer" },
    { "Fill from UC1",                                     kSecFxLearn, "UF1 layer" },
    { "Unbind all",                                        kSecFxLearn, "UF1 layer" },
    // -- Favourites --
    { "Copy only mapped parameters",                       kSecFavourites, "" },
    { "Favourites remember non-copied sections",           kSecFavourites, "" },
    { "This project uses its own Favourites",              kSecFavourites, "" },
    { "Multi-select: unify sets to focused track",         kSecFavourites, "" },
    { "Channel Strip: use own settings (vs copy values)",  kSecFavourites, "" },
    { "Bus Compressor: use own settings (vs copy values)", kSecFavourites, "" },
    // -- Parameter Groups --
    { "Multi-Select acts as temporary Parameter Group",    kSecParameterGroups, "" },
};

// Breadcrumb separator, shared by the tooltip trail and a row's second line.
constexpr const char kCrumbSep[] = " \xE2\x80\xBA ";   // " › "

// "Devices › Metering › GR meter source" — the full trail, used for matching
// and for the hover tooltip. The pane name always leads; the heading is
// dropped when it would only repeat the label (pane-level entries).
std::string searchBreadcrumb(const SearchEntry& e, bool withGroup)
{
    const char* pane = kRail[0].label;
    for (const RailEntry& r : kRail)
        if (r.section == e.section) { pane = r.label; break; }
    const char* kSep = kCrumbSep;
    std::string s = pane;
    if (withGroup && e.group && *e.group
        && std::strcmp(e.group, e.label) != 0) {
        s += kSep;
        s += e.group;
    }
    if (std::strcmp(e.label, pane) != 0) {
        s += kSep;
        s += e.label;
    }
    return s;
}

// Trim to the rail width, cutting on a UTF-8 boundary (the separator and a
// couple of labels are multi-byte) and appending "…".
std::string searchEllipsise(ImGui_Context* ctx, const std::string& s,
                            double maxW)
{
    static const char kEll[] = "\xE2\x80\xA6";     // "…"
    double w = 0.0, h = 0.0;
    ImGui_CalcTextSize(ctx, s.c_str(), &w, &h, nullptr, nullptr);
    if (w <= maxW || w <= 0.0) return s;
    // Guess the cut from the average glyph width so the walk-back below is
    // two or three measurements, not one per character.
    size_t n = static_cast<size_t>(s.size() * (maxW / w));
    if (n > s.size()) n = s.size();
    while (n > 0) {
        while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
        std::string probe = s.substr(0, n) + kEll;
        ImGui_CalcTextSize(ctx, probe.c_str(), &w, &h, nullptr, nullptr);
        if (w <= maxW) return probe;
        --n;
    }
    return kEll;
}

// The second line of a result row: "Modes \xE2\x80\xBA REC", or just the pane
// when the entry is the heading itself. The label goes on line one, so it is
// deliberately NOT repeated here.
std::string searchParentTrail(const SearchEntry& e)
{
    const char* pane = kRail[0].label;
    for (const RailEntry& r : kRail)
        if (r.section == e.section) { pane = r.label; break; }
    std::string s = pane;
    if (e.group && *e.group && std::strcmp(e.group, e.label) != 0) {
        s += kCrumbSep;
        s += e.group;
    }
    return s;
}

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
    // Bold + monospace faces for the Manual renderer (the embedded manual
    // leans heavily on **bold** and `code`). Created/attached alongside the
    // sans face and dropped with it on every context refresh.
    ImGui_Font*    fontBold = nullptr;
    ImGui_Font*    fontMono = nullptr;
    bool           visible = false;
    int            selected = kSecDevices;
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
    // Rail search query. Session-only — a filter is a momentary lens, not a
    // preference, so it is deliberately NOT persisted to ExtState.
    char           searchBuf[96] = {0};

    // Persist the active tab so "Reopen on last tab" (Settings → Appearance)
    // can restore it across REAPER restarts. Written on every selection
    // change; read only when the setting is enabled. Stored as the rail
    // entry's stable string id, never as the enum index — the index shifts
    // whenever a pane is added or reordered.
    void persistSelected()
    {
        SetExtState("rea_sixty", "settings_last_tab", railIdFor(selected),
                    /*persist*/ true);
    }
    // On open: if the user opted in, jump to the last tab they viewed; if NOT,
    // reset to the default Devices pane. The reset matters because `selected` is
    // a member that survives a close/open within a session — without it the
    // window would always reopen on the last tab even with the setting off
    // (Frank 2026-06-20).
    void applyReopenLastTab()
    {
        const char* en = GetExtState("rea_sixty", "settings_reopen_last_tab");
        const bool on  = en && *en && std::atoi(en) != 0;
        if (!on) { selected = kSecDevices; return; }
        const char* v = GetExtState("rea_sixty", "settings_last_tab");
        int sec = railSectionFor(v);
        if (sec < 0 && isLegacyTabIndex(v)) {
            // Pre-2026-08-09 install: the value is an index into the old
            // enum order. Translate it, then write the id back so this
            // branch never fires again for this user.
            const int i = std::atoi(v);
            if (i >= 0 && i < (int)(sizeof(kLegacyTabIds) /
                                    sizeof(kLegacyTabIds[0])))
                sec = railSectionFor(kLegacyTabIds[i]);
            if (sec >= 0)
                SetExtState("rea_sixty", "settings_last_tab", railIdFor(sec),
                            /*persist*/ true);
        }
        selected = (sec >= 0) ? sec : kSecDevices;
    }

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
        // The installed ReaImGui dropped CreateContext's size/pos args — the
        // current dylib sig is (label, config_flagsInOptional). We previously
        // passed &sizeW/&sizeH, which the dylib reinterpreted as config_flags
        // (= garbage flags). Pass nullptr for config_flags; the window size is
        // applied via SetNextWindowSize in onRunTick. Found in the 2026-06-17
        // full-header sig audit (learnings #21 / reaimgui-sig-audit).
        //
        // Context name carries a version suffix so a fresh ReaImGui
        // state file is allocated. Stale persisted state under the bare
        // "Rea-Sixty" key prevented the window from reopening across
        // recent debugging sessions — bumping the suffix forces v0.10
        // to treat us as a brand-new context with no carried-over
        // collapsed / off-screen / closed pose.
        ctx = ImGui_CreateContext("Rea-Sixty v2", /*config_flags*/ nullptr);

        // Load a generic sans-serif font and attach to the freshly
        // created context. CreateFont in v0.10 returns a sizeless
        // resource — size is chosen at PushFont time. The previous
        // font (if any) belonged to the now-orphaned context and is
        // GC'd by ReaImGui on the next defer cycle.
        font = ImGui_CreateFont("sans-serif", /*flagsInOptional*/ nullptr);
        if (ctx && font) ImGui_Attach(ctx, font);
        // Bold + monospace companions. FontFlags_Bold == 1 (the vendored
        // header omits the enum constant; the value is stable across ReaImGui
        // versions and confirmed against the installed dylib metadata).
        int boldFlag = 1;
        fontBold = ImGui_CreateFont("sans-serif", &boldFlag);
        if (ctx && fontBold) ImGui_Attach(ctx, fontBold);
        fontMono = ImGui_CreateFont("monospace", /*flagsInOptional*/ nullptr);
        if (ctx && fontMono) ImGui_Attach(ctx, fontMono);
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
        impl_->fontBold = nullptr;
        impl_->fontMono = nullptr;
        // Restore the last-viewed tab when the user opted in (Appearance →
        // "Reopen on last tab"); otherwise selected keeps its prior value.
        impl_->applyReopenLastTab();
    }
}

void MixerWindow::openToFxLearn()
{
    // Open first (which may restore the last tab if that setting is on),
    // then force the FX Learn rail entry — the explicit target always wins
    // over the reopen-last-tab restore.
    if (!impl_->visible) toggle();
    impl_->selected = kSecFxLearn;
    impl_->persistSelected();
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
    // Publish the live faces + size for the Manual renderer.
    g_uiSans   = impl_->font;
    g_uiBold   = impl_->fontBold;
    g_uiMono   = impl_->fontMono;
    g_uiFontPx = fontPx;
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
                // Filter field. Width set explicitly (rail minus ImGui's
                // 8 px window padding on each side) so it can never spill
                // past the 160 px rail at any Font Size.
                ImGui_SetNextItemWidth(impl_->ctx, kRailWidthPx - 16.0);
                ImGui_InputTextWithHint(impl_->ctx, "##rail_search",
                                        "Search settings\xE2\x80\xA6",
                                        impl_->searchBuf,
                                        (int)sizeof(impl_->searchBuf),
                                        /*flags*/ nullptr,
                                        /*callback*/ nullptr);
                ImGui_Spacing(impl_->ctx);

                // One token vector per frame, shared by every candidate —
                // the same additive matcher every other Settings search
                // field uses (SettingsScreen.cpp searchTokensLower_ /
                // searchAllTokensCI_, exposed via SettingsScreen.h).
                const std::vector<std::string> toks =
                    settingsSearchTokens(impl_->searchBuf);

                if (toks.empty()) {
                    for (const RailEntry& e : kRail) {
                        if (e.separatorBefore) ImGui_Separator(impl_->ctx);
                        bool isSelected = (impl_->selected == e.section);
                        if (ImGui_Selectable(impl_->ctx, e.label, &isSelected,
                                             /*flags*/ nullptr,
                                             /*size_w*/ nullptr,
                                             /*size_h*/ nullptr)) {
                            impl_->selected = e.section;
                            impl_->persistSelected();
                        }
                    }
                } else {
                    double availW = 0.0, availH = 0.0;
                    ImGui_GetContentRegionAvail(impl_->ctx, &availW, &availH);
                    int hits = 0;
                    for (const SearchEntry& s : kSearchIndex) {
                        const std::string crumb =
                            searchBreadcrumb(s, /*withGroup*/ true);
                        if (!settingsSearchMatches(toks, crumb)) continue;
                        ++hits;
                        // Two lines per hit: the setting on top, "Pane › Section"
                        // under it a size down. The rail is 160 px, so a single
                        // line could only ever fit the label — which left two
                        // hits from two different panes looking identical unless
                        // you hovered for the tooltip (Frank 2026-08-10).
                        // Rendered as a label-less Selectable of the right height
                        // with both lines painted over it, so the whole block is
                        // one click target.
                        const double lineH = ImGui_GetFontSize(impl_->ctx);
                        const double subPx = lineH * 0.82;
                        double rowH = lineH + subPx + 3.0;
                        double rowX = 0.0, rowY = 0.0;
                        ImGui_GetCursorScreenPos(impl_->ctx, &rowX, &rowY);
                        // The id carries the hit number, not the text: two
                        // labels can ellipsise to the same string.
                        char rowId[32];
                        snprintf(rowId, sizeof(rowId), "##hit_%d", hits);
                        bool picked = false;
                        const bool clicked =
                            ImGui_Selectable(impl_->ctx, rowId, &picked,
                                             /*flags*/ nullptr,
                                             /*size_w*/ nullptr, &rowH);
                        const bool hovered =
                            ImGui_IsItemHovered(impl_->ctx, /*flags*/ nullptr);
                        // Where the layout continues once the row is done.
                        double contX = 0.0, contY = 0.0;
                        ImGui_GetCursorScreenPos(impl_->ctx, &contX, &contY);

                        ImGui_SetCursorScreenPos(impl_->ctx, rowX, rowY);
                        ImGui_Text(impl_->ctx,
                                   searchEllipsise(impl_->ctx, s.label,
                                                   availW).c_str());
                        ImGui_SetCursorScreenPos(impl_->ctx, rowX,
                                                 rowY + lineH);
                        // Push the smaller face BEFORE measuring — ellipsising
                        // against the big font would cut the small line short.
                        if (impl_->font)
                            ImGui_PushFont(impl_->ctx, impl_->font, subPx);
                        ImGui_TextDisabled(
                            impl_->ctx,
                            searchEllipsise(impl_->ctx, searchParentTrail(s),
                                            availW).c_str());
                        if (impl_->font) ImGui_PopFont(impl_->ctx);
                        ImGui_SetCursorScreenPos(impl_->ctx, contX, contY);

                        if (clicked) {
                            impl_->selected = s.section;
                            impl_->persistSelected();
                            // Best-effort: the pane scrolls to this heading
                            // on the very next draw, which is this frame.
                            settingsRequestSectionScroll(s.group);
                        }
                        if (hovered)
                            ImGui_SetTooltip(impl_->ctx, crumb.c_str());
                    }
                    if (hits == 0)
                        ImGui_TextDisabled(impl_->ctx, "No matches");
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
                        // Take any pending rail-search scroll request for this
                        // one draw. Has to happen for EVERY pane, not just the
                        // three that own headings — otherwise a request whose
                        // target pane never draws (the user clicks a different
                        // rail entry first) stays armed and fires on an
                        // unrelated visit later. Frank 2026-08-09.
                        settingsLatchSectionScroll();
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
