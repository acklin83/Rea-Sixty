//
// REAPER extension entry point. Glues ColorSync + UF8Device to REAPER's
// API. The visible-track resolution is the one piece that's REAPER-specific
// and deserves comment:
//
//   REAPER exposes the mixer view via TCP/MCP scroll state. Our extension
//   polls on a timer (33 ms) and resolves the 8 "currently visible" tracks
//   in the TCP order. For the first milestone we simply use CountTracks()
//   and show tracks 1..8 regardless of scroll — bank-shift hookup is a
//   follow-up once we confirm the pipe actually drives the hardware.
//

#define REAPERAPI_IMPLEMENT
#include "reaper_plugin.h"
#include "reaper_plugin_functions.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <vector>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <string>

#include <sys/stat.h>
#include <dlfcn.h>

#ifdef __APPLE__
#include <ApplicationServices/ApplicationServices.h>
#endif

#include "Bindings.h"
#include "ColorSync.h"
#include "FocusedParam.h"
#include "HidDevice.h"
#include "MidiBridge.h"
#include "MixerWindow.h"
#include "PluginChunkPatch.h"
#include "PluginMap.h"
#include "Protocol.h"
#include "UC1Device.h"
#include "UC1PluginMap.h"
#include "UC1Surface.h"
#include "UF8Device.h"
#include "UserPluginCatalog.h"
#include "VirtualNotch.h"

// File-scope forward decl — onTimer (inside the anonymous namespace below)
// drains this every tick. Definition lives further down with the other
// reasixty_* helpers; the file-scope linkage means SettingsScreen.cpp can
// also call reasixty_actionPickerStart / Cancel / etc.
void reasixty_actionPickerPoll();

namespace {

std::unique_ptr<uf8::UF8Device>   g_dev;
std::unique_ptr<uf8::ColorSync>   g_sync;
std::unique_ptr<uf8::MidiBridge>  g_midi;
std::unique_ptr<uf8::HidDevice>   g_hid;

// UC1 — optional. If the device isn't present on the bus we just skip
// it; UF8 continues to work independently. Opening UC1 as a separate
// libusb context keeps the two devices isolated on the bulk-transfer
// side.
std::unique_ptr<uc1::UC1Device>   g_uc1_dev;
std::unique_ptr<uc1::UC1Surface>  g_uc1_surface;

// Plugin Mixer / Settings window (Phase 2.6 + 2.7). Rendered from
// onTimer() so REAPER-API reads stay main-thread. Toggle is requested
// via the atomic flag below — never call toggle() directly from any
// path that may run on the USB-input worker thread (UF8/UC1 button
// handlers, libusb readCallbacks, etc.). ImGui_CreateContext crashes
// hard if invoked off-main; reproducible via the 360 button on either
// device, fixed by routing through onTimer().
uf8::MixerWindow g_mixerWindow;
std::atomic<bool> g_mixerToggleRequest{false};
// Drained on the main thread — UI ops (TrackFX_Show, AppKit windows) MUST
// run on main thread or AppKit raises NSException. Set by
// ssl_strip_mode_toggle_with_gui from the libusb input thread, and by
// applyInstanceCycle_ whenever the cycle lands on a CS plug-in while
// SSL Strip Mode is on (so the open GUI follows the active instance).
std::atomic<bool> g_pluginGuiSyncRequest{false};

// Entry-time snap for UF8 Plugin Mode. Set by uf8_plugin_mode_toggle{,_with_gui}
// from the libusb input thread when the mode is being switched ON. The
// main-thread GUI-sync drain consumes it via snapUf8PluginModeToFocusedFx_,
// which points the mode at whichever UF8-mapped plug-in window REAPER
// currently reports as focused (Frank 2026-05-14 "wenn UF8 Plugin Mode
// aufgerufen wird und ein UF8-gemappter Plug-in-Fenster offen ist, direkt
// Instance auf dieses Plugin setzen").
std::atomic<bool> g_uf8PluginModeSnapRequest{false};

// Last (track, fx) shown by the SSL Strip GUI handler. Read/written
// only on the main thread (inside the g_pluginGuiSyncRequest drain),
// so plain types are fine. Used to close the previous floating window
// before opening the new one on instance cycle — without this, every
// cycle adds another floating GUI to the pile until the user manually
// closes them.
void* g_csGuiShownTr = nullptr;
int   g_csGuiShownFx = -1;

// UF8 Plugin Mode "with GUI" variant — when this flag is on, entering
// UF8 Plugin Mode also pops the focused track's user-mapped plug-in
// GUI; leaving the mode closes it. Tracked as a separate flag so the
// plain `uf8_plugin_mode_toggle` builtin stays headless. Tr+Fx
// remember what window we opened so the next toggle / focus change
// can close it cleanly (main-thread access only).
std::atomic<bool> g_uf8PluginModeWithGui{false};
void* g_uf8GuiShownTr = nullptr;
int   g_uf8GuiShownFx = -1;

// V-POTS → FX Cycle GUI ownership. V-Pot push toggles ownership for a
// strip; the GUI sync drain opens/closes the floating window based on
// who owns it. V-Pot rotation on the owner strip re-points the open
// window to the new active FX automatically — no separate "with GUI"
// variant needed (Frank 2026-05-14 "kann weg - wir können eh mit V-Pot
// push GUI einblenden").
std::atomic<int>  g_instanceGuiOwnerStrip{-1};
void* g_instanceGuiShownTr = nullptr;
int   g_instanceGuiShownFx = -1;

// IReaperControlSurface subclass registered as a full control surface
// class ("Rea-Sixty") so users see and add it like any other surface.
// GetTouchState lets Touch-mode automation see the user is holding the
// fader; SetSurfaceVolume is how REAPER notifies us of the current
// track volume during automation playback.
class ReaSixtySurface : public IReaperControlSurface {
public:
    ReaSixtySurface();
    ~ReaSixtySurface() override;

    const char* GetTypeString()   override { return "REASIXTY"; }
    const char* GetDescString()   override { return "Rea-Sixty"; }
    const char* GetConfigString() override { return "0 0"; }

    bool GetTouchState(MediaTrack* tr, int isPan) override;

    // LED feedback. REAPER calls these whenever solo/mute/select/arm
    // state changes for any track. We translate to MCU note-on/off and
    // send to the UF8's MCU-MIDI port so the per-strip LEDs follow.
    void SetSurfaceSolo(MediaTrack* tr, bool solo)        override;
    void SetSurfaceMute(MediaTrack* tr, bool mute)        override;
    void SetSurfaceSelected(MediaTrack* tr, bool sel)     override;
    void SetSurfaceRecArm(MediaTrack* tr, bool arm)       override;
};

// Per-slot MediaTrack cache so GetTouchState can map REAPER's track
// pointer back to a strip index without a linear scan each call. Filled
// by the timer; main-thread-only.
std::array<MediaTrack*, 8> g_slotTrack{};

// Bank offset in 8-strip windows. Bank Left/Right shift by 8; scroll
// wheel (once implemented) will shift by 1. Clamped in [0, max track
// count − 1] so the last bank can end partially empty. Written from
// the USB-input thread on Bank-button press and consumed on the main
// thread by the timer.
std::atomic<int> g_bankOffset{0};

// When the bank offset changes the motor-echo dedup and ColorSync
// dedup both need a full re-push on the next timer tick, since every
// slot now points at a different track. Set by bank-shift, consumed
// by the timer.
std::atomic<bool> g_bankDirty{false};

// Re-render trigger for the timer when the focused-param slot changes.
// The actual focused-param state lives in FocusedParam.h
// (uf8::g_focusedParam, uf8::g_focusedDirty). This flag is the existing
// UF8-side "the labels/values need a forced re-push next tick" signal and
// is set in tandem with g_focusedDirty by anything that mutates the
// focused param. Kept as a separate flag because pushZonesForVisibleSlots
// also fires it on bank shifts, which don't touch g_focusedParam.
std::atomic<bool> g_pageDirty{false};

// When the user hits the PAN button, we globally override every strip's
// V-Pot to act as pan control regardless of whether the track hosts an
// SSL plug-in. Any V-Pot assignment soft key (0x68–0x6D) returns to
// automatic plug-in-param mode. Same invalidation path as a page change.
std::atomic<bool> g_forcePan{false};

// FLIP mode (button 0x54): swap fader and V-Pot. Fader drives the
// focused plug-in parameter; V-Pot drives track volume. Display zones
// follow the swap so the user sees param value above the fader and
// "Vol  -X.YdB" in the Value Line. Persisted across REAPER sessions.
std::atomic<bool> g_flip{false};

// Plugin-fader-mode toggle. Press of the global Plugin button (0x50)
// flips this. When true, UF8 faders should drive plug-in faders directly
// instead of REAPER track volume. Routing wireup TBD; this state +
// the Plugin LED feedback land in this commit.
std::atomic<bool> g_pluginFaderMode{false};

// SSL Strip Mode "with GUI" variant — when on, entering SSL Strip Mode
// pops the focused track's CS plug-in GUI and follows selection / Instance
// cycle changes; leaving the mode closes it. Tracked as a separate flag
// so the plain `ssl_strip_mode_toggle` builtin stays headless. Without
// this flag, the GUI-follow trigger relied on g_csGuiShownTr being
// non-null — which broke after the GUI was closed once (e.g. user
// selected a track with no CS plug-in): g_csGuiShownTr cleared, future
// selections never re-opened the GUI (Frank 2026-05-14).
std::atomic<bool> g_pluginFaderModeWithGui{false};

// Phase 2.5 mode toggles. State-of-record only — bind-able via builtins
// registered in registerBindingHandlers() and surfaced as Settings
// checkboxes. Filter / selection-set logic wires up in a follow-up phase.
//
// folder_mode: CSI-style surface filter — only parent tracks fill the 8
// strips; long-press SEL on a parent expands its children. NOT REAPER's
// I_FOLDERCOMPACT (the TCP folder state stays untouched).
//
// show_only_selected: surface filter that restricts strips to a saved
// Selection Set (8 slots, recalled via selset_recall param 1..8).
std::atomic<bool> g_folderMode{false};
std::atomic<bool> g_showOnlySelected{false};

// Selection-Mode group. Mutually-exclusive global state that retargets
// the 8 SEL LEDs and the V-Pot push/rotation. Norm = legacy behaviour
// (track-select, pan, etc.); Rec = SEL shows rec-arm status & push
// toggles I_RECARM; Auto = SEL coloured by per-track automation mode
// & V-Pot rotation scrolls auto-modes; Instance = V-Pot rotation
// cycles the plug-in instance on the strip's track. Bindable via the
// `selection_mode_*` builtins (registered in registerBindingHandlers).
enum class SelectionMode : uint8_t { Norm = 0, Rec, RecMon, Auto, Instance };
std::atomic<SelectionMode> g_selectionMode{SelectionMode::Norm};

inline const char* selectionModeStr(SelectionMode m)
{
    switch (m) {
        case SelectionMode::Rec:      return "rec";
        case SelectionMode::RecMon:   return "rec_mon";
        case SelectionMode::Auto:     return "auto";
        case SelectionMode::Instance: return "instance";
        case SelectionMode::Norm:
        default:                      return "norm";
    }
}

inline SelectionMode parseSelectionMode(const char* s)
{
    if (!s) return SelectionMode::Norm;
    if (std::strcmp(s, "rec")      == 0) return SelectionMode::Rec;
    if (std::strcmp(s, "rec_mon")  == 0) return SelectionMode::RecMon;
    if (std::strcmp(s, "auto")     == 0) return SelectionMode::Auto;
    if (std::strcmp(s, "instance") == 0) return SelectionMode::Instance;
    return SelectionMode::Norm;
}

// Forward decl — defined further down with the other clock helpers.
int64_t nowMs_();

// Forward decls for Pan overlay used by drainInputQueue. The state +
// formatters live further down with the other strip caches.
std::string formatPanReadout(double pan);
std::string composeValueLine(std::string_view label, std::string_view value);
constexpr int64_t kPanOverlayMs = 600;
extern std::array<int64_t, 8>     g_panOverlayUntilMs;
extern std::array<std::string, 8> g_panOverlayText;

// Folder Mode value-line override: parent tracks normally show "Folder"
// in the V-Pot value line; turning the V-Pot reveals the actual value
// for kFolderRevealMs, then it reverts to "Folder".
constexpr int64_t kFolderRevealMs = 3000;
extern std::array<int64_t, 8>     g_folderRevealUntilMs;

// Surface-visible track list — REAPER's full track set filtered by
// g_folderMode (parents-only: only top-level / depth-0 tracks pass,
// plus the children of g_spilledParent when one is held expanded) and
// g_showOnlySelected (live "currently selected" filter, not a saved
// selection set). Independent of REAPER's MCP collapse state — folder
// mode here is a pure surface filter. Rebuilt once per onTimer tick
// before strip rendering and input handling. ALL "bank index → track"
// lookups in surface contexts read from this list; non-surface callers
// (e.g. scanning all tracks for any-armed status) keep using REAPER's
// full track set via CountTracks/GetTrack.
std::vector<MediaTrack*> g_visibleTracks;

// Long-press SEL on a parent (folder-start) track temporarily exposes
// its direct children on the strips immediately to the right, until the
// user long-presses SEL on the same parent again or on a different
// parent. Only meaningful when g_folderMode is on. Stored as raw
// MediaTrack*; revalidated against ValidatePtr2 every rebuild because
// REAPER may free the track behind our back when the user reorders.
std::atomic<MediaTrack*> g_spilledParent{nullptr};

// Per-strip SEL press state for long-press detection. press_ms = epoch
// millis at the press edge (0 = not currently held). spill_fired = true
// once the long-press threshold elapsed and the spill toggle ran (so
// the held button doesn't keep firing every tick). Cleared on release.
std::array<std::atomic<int64_t>, 8> g_selPressMs{};
std::array<std::atomic<bool>, 8>    g_selSpillFired{};

constexpr int64_t kSelLongPressMs = 500;

inline int visibleTrackCount() {
    return static_cast<int>(g_visibleTracks.size());
}
inline MediaTrack* visibleTrackAt(int idx) {
    if (idx < 0 || idx >= static_cast<int>(g_visibleTracks.size())) return nullptr;
    return g_visibleTracks[idx];
}

void rebuildVisibleTrackList() {
    const bool folderMode = g_folderMode.load();
    const bool selOnly    = g_showOnlySelected.load();
    MediaTrack* spilled   = g_spilledParent.load();
    if (spilled && !ValidatePtr2(nullptr, spilled, "MediaTrack*")) {
        spilled = nullptr;
        g_spilledParent.store(nullptr);
    }
    const int  n = CountTracks(nullptr);
    g_visibleTracks.clear();
    g_visibleTracks.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        MediaTrack* tr = GetTrack(nullptr, i);
        if (!tr) continue;
        if (folderMode) {
            // Parents-only: a track passes when it's at top level
            // (GetTrackDepth == 0) — covers folder-start tracks AND
            // non-folder root tracks alike. Children pass only if they
            // are DIRECT children of the currently-spilled parent
            // (long-press SEL on that parent flipped the spill on).
            // Independent of REAPER's MCP collapse state — the user
            // wanted a hardware-only filter, not a mirror.
            const int depth = GetTrackDepth(tr);
            if (depth != 0) {
                if (!spilled) continue;
                if (GetParentTrack(tr) != spilled) continue;
            }
        }
        // Show-only-selected: live filter against current selection (not
        // a saved selection set). Each tick reflects the latest
        // I_SELECTED state so toggling track selection adds/removes
        // strips immediately.
        if (selOnly && !(GetMediaTrackInfo_Value(tr, "I_SELECTED") > 0.5))
            continue;
        g_visibleTracks.push_back(tr);
    }
}

// Long-press SEL → folder spill toggle. Polled at the start of onTimer
// (before rebuildVisibleTrackList) so the rebuilt list immediately
// reflects spill changes. Skips silently when folder_mode is off — no
// visible effect even if user happens to long-press SEL.
void checkSelLongPressSpill() {
    if (!g_folderMode.load()) return;
    const int64_t now = nowMs_();
    for (int s = 0; s < 8; ++s) {
        const int64_t pressMs = g_selPressMs[s].load();
        if (pressMs == 0) continue;
        if (g_selSpillFired[s].load()) continue;
        if (now - pressMs < kSelLongPressMs) continue;
        g_selSpillFired[s].store(true);
        MediaTrack* tr = g_slotTrack[s];
        if (!tr || !ValidatePtr2(nullptr, tr, "MediaTrack*")) continue;
        // Only folder-start tracks (I_FOLDERDEPTH == 1) carry children
        // worth spilling. Non-folder top-level tracks fail this gate
        // and the long-press is a no-op for them.
        if (GetMediaTrackInfo_Value(tr, "I_FOLDERDEPTH") < 0.5) continue;
        MediaTrack* cur = g_spilledParent.load();
        g_spilledParent.store(cur == tr ? nullptr : tr);
        g_bankDirty.store(true);
    }
}

// Active Selection-Set slot (1..8); 0 = none. selset_recall sets this,
// LED feedback uses it so the bound button lights for the live slot.
std::atomic<int>  g_selsetActive{0};

// V-Pot has dedicated Pan UX (Plugin button → plug-in Pan; PAN button →
// REAPER track pan; default → REAPER track pan). Clicking the Pan knob
// in the SSL plug-in GUI fires GetLastTouchedFX, which chaseLastTouchedFx
// turns into setFocus({ChannelStrip, 3}) — and that focus persists across
// Plugin/PAN button toggles, hijacking the V-Pot's Pan-mode tree. Fader
// + V-Pot drive/display sites treat Pan-focus as "no focus" so the
// Plugin/forcePan/REAPER tree retains authority over Pan.
inline bool isVPotPanFocus(const uf8::FocusedParam& f) {
    return f.domain == uf8::Domain::ChannelStrip && f.slotIdx == 3;
}

// Soft-Key Bank — which page of params is currently shown across the 8
// top-soft-key labels (and selectable via the per-strip 0x18..0x1F
// keys). Range is 0..5 (V-POT + Bank 1..5) for both domains; in BC
// mode banks 2..5 are present-but-empty per SSL UF8 User Guide.
// Layout from SSL UF8 User Guide p.180-181. Persisted across sessions.
std::atomic<int>  g_softKeyBank{0};
std::atomic<bool> g_softKeyDirty{false};

namespace softkey {
    constexpr int kNoSlot = -1;
    constexpr size_t kStrips = 8;

    // CS-mode banks (6 × 8). Values are SSL 360 Link slot indices
    // (linkIdx); uf8::ext::* refers to extension-defined synthetic IDs in
    // PluginMap.h for params not in the SSL 360 Link table.
    //   - Phase / A/B / HQ Mode: synthetic linkIdx — soft-key press sets
    //     focus to the synthetic, render path reads state per-strip
    //     (REAPER B_PHASE for Phase; SSL plug-in chunk for A/B + HQ),
    //     V-Pot push triggers the per-strip toggle. Same UX as any
    //     other CS param.
    //   - Pre / Mic-Drive / Imp In / Imp: only on 4K-series; CS2 strips
    //     render blank when soft-key pressed (graceful no-op).
    constexpr int kCsBanks[6][kStrips] = {
        // V-POT: BYPASS, IN TRIM, Ø, PRE, MIC/DRIVE, _, IMPEDANCE IN, IMPEDANCE
        // BYPASS uses linkIdx 0 — the plug-in's own Bypass param (NOT
        // REAPER's TrackFX_Enabled).
        { 0,  4, uf8::ext::TrackPhase, uf8::ext::Pre, uf8::ext::MicDrive, kNoSlot, uf8::ext::ImpedanceIn, uf8::ext::Impedance },
        // Bank 1: WIDTH, _, _, A/B, HIGH PASS, LOW PASS, EQ, EQ TYPE
        { 2, kNoSlot, kNoSlot, uf8::ext::PluginAB,  7,  6, 15, 14 },
        // Bank 2: LF FREQ, LF GAIN, LF TYPE, _, LMF FREQ, LMF GAIN, LMF Q, _
        { 19, 20, 21, kNoSlot, 17, 16, 18, kNoSlot },
        // Bank 3: HMF FREQ, HMF GAIN, HMF Q, _, _, HF FREQ, HF GAIN, HF TYPE
        { 12, 11, 13, kNoSlot, kNoSlot, 10,  9,  8 },
        // Bank 4: DYNAMICS, COMP MIX, COMP RATIO, COMP THR, COMP REL, COMP ATK, PEAK/RMS, _
        { 22, 23, 26, 27, 28, 24, 25, kNoSlot },
        // Bank 5: GATE REL, GATE THR, GATE RNG, GATE HLD, GATE ATK, GATE/EXP, HQ MODE, OUT TRIM
        { 31, 30, 29, 32, 34, 33, uf8::ext::PluginHQ, 37 },
    };
    constexpr const char* kCsLabels[6][kStrips] = {
        { "BYPASS",  "IN TRIM",   "PHASE",      "PRE",      "MIC/DRV",   "",          "IMP IN",  "IMP" },
        { "WIDTH",   "",          "",           "A/B",      "HPF",       "LPF",       "EQ",      "EQ TYPE" },
        { "LF FREQ", "LF GAIN",   "LF TYPE",    "",         "LMF FREQ",  "LMF GAIN",  "LMF Q",   "" },
        { "HMF FREQ","HMF GAIN",  "HMF Q",      "",         "",          "HF FREQ",   "HF GAIN", "HF TYPE" },
        { "DYNAMICS","COMP MIX",  "COMP RATIO", "COMP THR", "COMP REL",  "COMP F.ATK","PEAK/RMS","" },
        { "GATE REL","GATE THR",  "GATE RANGE", "GATE HOLD","GATE F.ATK","GATE/EXP",  "HQ MODE", "OUT TRIM" },
    };

    // BC-mode banks (6 × 8). Banks 2..5 are intentionally empty per SSL
    // UF8 User Guide — bank navigation cycles through them symmetrically
    // with CS mode even though BC has only two pages of params.
    constexpr int kBcBanks[6][kStrips] = {
        // V-POT: THRESHOLD, ATTACK, RELEASE, RATIO, S/C HPF, MIX, EXTERNAL S/C, BUS COMP
        // BUS COMP at pos 7 = the BC plug-in's own CompBypass param
        // (linkIdx 0 in the BC 360 Link layout). External S/C uses
        // uf8::ext::ExternalSC — only the Native BC2 plug-in exposes it (the
        // 360 Link wrapper does not), so other BC variants will no-op.
        { 1, 3, 4, 5, 6, 7, uf8::ext::ExternalSC, 0 },
        // Bank 1: OUTPUT GAIN (= MakeupGain in BC2 map), rest empty
        { 2, kNoSlot, kNoSlot, kNoSlot, kNoSlot, kNoSlot, kNoSlot, kNoSlot },
        // Banks 2..5: empty per SSL spec
        { kNoSlot, kNoSlot, kNoSlot, kNoSlot, kNoSlot, kNoSlot, kNoSlot, kNoSlot },
        { kNoSlot, kNoSlot, kNoSlot, kNoSlot, kNoSlot, kNoSlot, kNoSlot, kNoSlot },
        { kNoSlot, kNoSlot, kNoSlot, kNoSlot, kNoSlot, kNoSlot, kNoSlot, kNoSlot },
        { kNoSlot, kNoSlot, kNoSlot, kNoSlot, kNoSlot, kNoSlot, kNoSlot, kNoSlot },
    };
    constexpr const char* kBcLabels[6][kStrips] = {
        { "THR",    "ATTACK", "RELEASE","RATIO", "S/C HPF","MIX",  "EXT S/C","BUS COMP" },
        { "OUTGAIN","",       "",       "",      "",       "",     "",       "" },
        { "",       "",       "",       "",      "",       "",     "",       "" },
        { "",       "",       "",       "",      "",       "",     "",       "" },
        { "",       "",       "",       "",      "",       "",     "",       "" },
        { "",       "",       "",       "",      "",       "",     "",       "" },
    };

    constexpr int kCsMaxBank = 5;
    constexpr int kBcMaxBank = 5;

    // Domain-aware bank max so the bank index can be clamped after a
    // domain switch (BC has fewer banks than CS).
    inline int maxBankFor(uf8::Domain d) {
        return d == uf8::Domain::BusComp ? kBcMaxBank : kCsMaxBank;
    }

    // Resolve the current 8-slot view (linkIdx + label arrays) for a
    // given domain + bank. Caller clamps bank to maxBankFor(domain).
    struct View {
        const int*        linkIdx;
        const char* const* labels;
    };
    inline View viewFor(uf8::Domain d, int bank) {
        if (d == uf8::Domain::BusComp) {
            return { kBcBanks[bank], kBcLabels[bank] };
        }
        return { kCsBanks[bank], kCsLabels[bank] };
    }

    // Find which bank in `domain` contains a given linkIdx. -1 if not
    // found. Used to auto-follow the bank to externally-set focus
    // (UC1 knob, plugin GUI mouse, FocusedFX chase).
    inline int bankContaining(uf8::Domain d, int linkIdx) {
        const int max = maxBankFor(d);
        for (int b = 0; b <= max; ++b) {
            const View v = viewFor(d, b);
            for (size_t s = 0; s < kStrips; ++s) {
                if (v.linkIdx[s] == linkIdx) return b;
            }
        }
        return -1;
    }
}

// Per-strip cache for the SEL-follows-DAW-Colour LED state. Avoids
// sending FF 38/39 every tick — only on actual changes. Value encodes
// 0xFF for "bright" / 0x00 for "dim" / 0xFE as "unset".
uint8_t g_lastSelBright[8] = {0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE};
uint16_t g_lastSelMask = 0xFFFF;  // 0xFFFF = "unset" sentinel

// ---- Brightness management (LED + LCD on both devices) ----
//
// SSL 360° exposes 5 brightness steps ("dark / dim / half / bright /
// full"). Until the Phase 2 settings UI is built we expose two REAPER
// custom actions that cycle up/down through those steps, persisted to
// ExtState so the user's choice survives REAPER restarts.
enum BrightnessLevel {
    BL_Dark   = 0,
    BL_Dim    = 1,
    BL_Half   = 2,
    BL_Bright = 3,
    BL_Full   = 4,
};
// `g_brightness` is the LED step. `g_scribbleBrightness` is the LCD /
// scribble-strip / mech-VU step. Originally both followed a single index;
// Settings → Device exposes them as two sliders so users can crank LCD
// while keeping LEDs dim (or vice-versa). UC1 LCD + status follow the
// scribble step (visually they're all "displays"); UC1 LEDs follow the
// LED step.
std::atomic<int> g_brightness{BL_Full};
std::atomic<int> g_scribbleBrightness{BL_Full};

// SEL-follows-track-color toggle. Default ON (SSL 360° behaviour: the SEL
// LED inherits the REAPER track colour). When OFF, SEL falls back to
// plain white. Read inside ledColourFor() — see below. Persisted via
// ExtState; toggling re-fires bankDirty so per-strip SEL re-pushes.
std::atomic<bool> g_selFollowsColor{true};

// "Show any GR data" — when true (default), the CS GR strip on UF8
// AND the UC1 Comp meter fall back to ANY track FX exposing the
// PreSonus GainReduction_dB convention if no SSL CS / user-mapped
// CS plug-in is on the focused track. When false, GR is shown only
// when an SSL CS / mapped plug-in is present (matches pre-2026-05-06
// behaviour). Controlled via Settings → Device.
std::atomic<bool> g_grAnyFx{true};

// When true, a parameter change on a non-selected track auto-selects that
// track (Frank 2026-05-07 — V-Pot/SC/BC edits route to selection so UC1
// follows automatically rather than requiring a manual select).
std::atomic<bool> g_trackSelFollowsParam{false};

// When true AND SSL Strip Mode is active, the strips follow whichever
// plugin window the user focuses in REAPER (GetFocusedFX2 poll).
std::atomic<bool> g_stripFollowsFocusedFx{false};

// UF8 Plugin Mode (deep edit): all 8 strips drive params on ONE user-
// mapped plugin instance on the focused track. Separate from SSL Strip
// Mode (g_pluginFaderMode) which is per-track. Toggled via the
// uf8_plugin_mode_toggle builtin action; user-bindable, no default button.
std::atomic<bool> g_uf8PluginMode{false};

// Meter ballistic. Peak = raw peak per Track_GetPeakInfo (no smoothing
// here; REAPER's own ballistics already apply); VU = exp-smoothed dB
// with τ=300 ms; RMS = exp-smoothed linear power with τ=600 ms then
// converted to dB. Implementation lives next to peakToVuByte further
// below — declared here so loadBrightness() can read the persisted pref.
enum BallisticMode : int {
    BM_Peak = 0,
    BM_VU   = 1,
    BM_RMS  = 2,
};
std::atomic<int> g_ballisticMode{BM_Peak};

struct BrightnessBytes {
    uint8_t uf8_led; uint8_t uf8_lcd;
    uint8_t uc1_led; uint8_t uc1_lcd; uint8_t uc1_status;
};
constexpr BrightnessBytes kBrightnessTable[5] = {
    {0x05, 0x18, 0x0A, 0x18, 0x08},  // dark
    {0x0A, 0x30, 0x13, 0x30, 0x0F},  // dim
    {0x10, 0x50, 0x20, 0x50, 0x19},  // half
    {0x13, 0x60, 0x26, 0x60, 0x1E},  // bright
    {0x20, 0xA0, 0x40, 0xA0, 0x32},  // full
};

int clampLevel_(int level)
{
    if (level < 0) return 0;
    if (level > 4) return 4;
    return level;
}

void pushUf8Brightness(int ledLevel, int scribbleLevel)
{
    ledLevel      = clampLevel_(ledLevel);
    scribbleLevel = clampLevel_(scribbleLevel);
    const auto& bl = kBrightnessTable[ledLevel];
    const auto& bs = kBrightnessTable[scribbleLevel];
    if (g_dev && g_dev->isOpen()) {
        g_dev->send(uf8::buildLedBrightness(bl.uf8_led));
        g_dev->send(uf8::buildLcdBrightness(bs.uf8_lcd));
    }
}

void pushUc1Brightness(int ledLevel, int scribbleLevel)
{
    ledLevel      = clampLevel_(ledLevel);
    scribbleLevel = clampLevel_(scribbleLevel);
    const auto& bl = kBrightnessTable[ledLevel];
    const auto& bs = kBrightnessTable[scribbleLevel];
    if (g_uc1_dev && g_uc1_dev->isOpen()) {
        g_uc1_dev->send(uc1::buildLedBrightness(bl.uc1_led));
        g_uc1_dev->send(uc1::buildLcdBrightness(bs.uc1_lcd));
        g_uc1_dev->send(uc1::buildStatusBrightness(bs.uc1_status));
    }
}

void pushBrightness(int ledLevel, int scribbleLevel)
{
    pushUf8Brightness(ledLevel, scribbleLevel);
    pushUc1Brightness(ledLevel, scribbleLevel);
}

void applyBrightness()
{
    const int led = g_brightness.load();
    const int scr = g_scribbleBrightness.load();
    pushBrightness(led, scr);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%d", led);
    SetExtState("rea_sixty", "brightness", buf, true);
    std::snprintf(buf, sizeof(buf), "%d", scr);
    SetExtState("rea_sixty", "scribble_brightness", buf, true);
}

void loadBrightness()
{
    const char* led = GetExtState("rea_sixty", "brightness");
    if (led && *led) {
        g_brightness.store(clampLevel_(std::atoi(led)));
    }
    const char* scr = GetExtState("rea_sixty", "scribble_brightness");
    if (scr && *scr) {
        g_scribbleBrightness.store(clampLevel_(std::atoi(scr)));
    } else {
        // First-run migration: no separate scribble pref yet → mirror the
        // LED level so the install upgrade is invisible.
        g_scribbleBrightness.store(g_brightness.load());
    }
    const char* selFollow = GetExtState("rea_sixty", "sel_follows_color");
    if (selFollow && *selFollow) {
        g_selFollowsColor.store(std::atoi(selFollow) != 0);
    }
    const char* grAny = GetExtState("rea_sixty", "gr_any_fx");
    if (grAny && *grAny) {
        g_grAnyFx.store(std::atoi(grAny) != 0);
    }
    const char* tselFp = GetExtState("rea_sixty", "track_sel_follows_param");
    if (tselFp && *tselFp) {
        g_trackSelFollowsParam.store(std::atoi(tselFp) != 0);
    }
    const char* sff = GetExtState("rea_sixty", "strip_follows_focused_fx");
    if (sff && *sff) {
        g_stripFollowsFocusedFx.store(std::atoi(sff) != 0);
    }
    const char* upm = GetExtState("ReaSixty", "uf8PluginMode");
    if (upm && *upm) {
        g_uf8PluginMode.store(std::atoi(upm) != 0);
    }
    const char* selm = GetExtState("ReaSixty", "selectionMode");
    if (selm && *selm) {
        g_selectionMode.store(parseSelectionMode(selm));
    }
    const char* bm = GetExtState("rea_sixty", "ballistic_mode");
    if (bm && *bm) {
        const int v = std::atoi(bm);
        if (v >= BM_Peak && v <= BM_RMS) g_ballisticMode.store(v);
    }
}

// ---- Identify Unit (LCD/LED flash) ----------------------------------------
// Settings → Device exposes per-device "Identify" buttons. Pulses LED + LCD
// brightness between Dark and Full at 4 Hz for kIdentifyDurationMs to make
// it obvious which physical box is selected when several are connected.
// State machine ticks on onTimer so we don't block the main thread.
constexpr int64_t kIdentifyDurationMs = 2000;
constexpr int64_t kIdentifyFlashMs    = 250;

std::atomic<int64_t> g_identifyUf8UntilMs{0};
std::atomic<int64_t> g_identifyUc1UntilMs{0};
int g_identifyUf8LastLevel = -1;
int g_identifyUc1LastLevel = -1;

int64_t nowMs_()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void tickIdentify()
{
    const int64_t now = nowMs_();

    // UF8
    {
        const int64_t until = g_identifyUf8UntilMs.load();
        if (until > 0) {
            if (now < until) {
                const int64_t remaining = until - now;
                const int phase = (remaining / kIdentifyFlashMs) & 1;
                const int target = phase ? BL_Dark : BL_Full;
                if (target != g_identifyUf8LastLevel) {
                    // During identify, drive both LED and scribble at the
                    // same level so the whole unit visibly pulses.
                    pushUf8Brightness(target, target);
                    g_identifyUf8LastLevel = target;
                }
            } else {
                g_identifyUf8UntilMs.store(0);
                g_identifyUf8LastLevel = -1;
                pushUf8Brightness(g_brightness.load(),
                                  g_scribbleBrightness.load());
            }
        }
    }

    // UC1
    {
        const int64_t until = g_identifyUc1UntilMs.load();
        if (until > 0) {
            if (now < until) {
                const int64_t remaining = until - now;
                const int phase = (remaining / kIdentifyFlashMs) & 1;
                const int target = phase ? BL_Dark : BL_Full;
                if (target != g_identifyUc1LastLevel) {
                    pushUc1Brightness(target, target);
                    g_identifyUc1LastLevel = target;
                }
            } else {
                g_identifyUc1UntilMs.store(0);
                g_identifyUc1LastLevel = -1;
                pushUc1Brightness(g_brightness.load(),
                                  g_scribbleBrightness.load());
            }
        }
    }
}

bool brightnessUp()
{
    int cur = g_brightness.load();
    if (cur >= BL_Full) return false;
    g_brightness.store(cur + 1);
    applyBrightness();
    return true;
}

bool brightnessDown()
{
    int cur = g_brightness.load();
    if (cur <= BL_Dark) return false;
    g_brightness.store(cur - 1);
    applyBrightness();
    return true;
}

bool brightnessLcdsUp()
{
    int cur = g_scribbleBrightness.load();
    if (cur >= BL_Full) return false;
    g_scribbleBrightness.store(cur + 1);
    applyBrightness();
    return true;
}

bool brightnessLcdsDown()
{
    int cur = g_scribbleBrightness.load();
    if (cur <= BL_Dark) return false;
    g_scribbleBrightness.store(cur - 1);
    applyBrightness();
    return true;
}

// Both stepped independently — if one is already at the rail, the other
// still moves. Returns true if either changed.
bool brightnessBothUp()
{
    bool changed = false;
    int led = g_brightness.load();
    if (led < BL_Full) { g_brightness.store(led + 1); changed = true; }
    int scr = g_scribbleBrightness.load();
    if (scr < BL_Full) { g_scribbleBrightness.store(scr + 1); changed = true; }
    if (changed) applyBrightness();
    return changed;
}

bool brightnessBothDown()
{
    bool changed = false;
    int led = g_brightness.load();
    if (led > BL_Dark) { g_brightness.store(led - 1); changed = true; }
    int scr = g_scribbleBrightness.load();
    if (scr > BL_Dark) { g_scribbleBrightness.store(scr - 1); changed = true; }
    if (changed) applyBrightness();
    return changed;
}

// Read the "UI" volume for a track — reflects the same value the REAPER
// mixer displays, including automation playback and the effect of a
// just-applied CSurf_OnVolumeChange. Safer than GetMediaTrackInfo_Value
// for motor-echo purposes. Same pattern CSI uses.
double uiVolLinear(MediaTrack* tr)
{
    double vol = 1.0;
    double pan = 0.0;
    GetTrackUIVolPan(tr, &vol, &pan);
    return vol;
}

// REAPER's C API expects calls on the main thread. The UF8 input handler
// runs on libusb's transfer-callback thread, so per-strip input events
// (buttons / fader / v-pot) are pushed into this queue and drained in
// onTimer(), which REAPER invokes on the main thread at ~30 Hz.
// Forward decls — full definitions live near the LCD helpers below;
// PanDelta/PanCenter handlers consume them earlier in the file.
bool isBinarySlot(const uf8::LinkSlot& s);
bool isBipolarSlot(const uf8::LinkSlot& s);

struct PendingInput {
    enum Kind : uint8_t {
        SoloToggle,
        MuteToggle,
        SelectToggle,    // additive (Shift held) — toggles selection on this track
        SelectExclusive, // no modifier — selects only this track
        VolumeAbs,       // value = linear volume (1.0 == 0 dB)
        PanDelta,        // value = signed pan delta (−1..+1 is full sweep)
        PanCenter,       // reset pan to 0 (center)
        SelectRelative,  // value = signed track-index delta (channel encoder, Nav mode)
        PlayheadNudge,   // value = signed seconds delta (channel encoder, Nudge mode)
        MouseScroll,     // value = signed scroll delta (channel encoder, Focus mode)
        InstanceCycle,   // value = signed delta; Shift+channel-encoder cycles
                         // the active CS/BC instance on the focused (or
                         // BC-anchor) track regardless of encoder mode.
        EncoderRotation, // value = signed6 raw delta from the channel
                         // encoder. drainInputQueue accumulates and
                         // dispatches via bindings::dispatchEncoder so
                         // the user can rebind Plain / Shift / Cmd / Ctrl
                         // slots on ButtonId::ChannelEncoder.
        MainAction,      // value = REAPER action ID (Main_OnCommand)
        AutomationMode,  // value = REAPER automation mode (0..5) on selected track
        AutomationModeGlobal, // value = REAPER mode (0..5) for the global
                              // automation override (SetGlobalAutomationOverride)
        FocusSelected,   // re-scroll REAPER MCP + UF8 bank to currently selected track

        // Selection-Mode per-strip events. Strip = 0..7. SEL-press in
        // REC mode → RecArmToggle; SEL-press in AUTO mode →
        // AutoModeStep. V-Pot push in Instance mode → OpenFxWindow.
        // V-Pot rotation in Instance mode → InstanceCycleDelta.
        // (REC and AUTO leave V-Pots on the Norm wiring.)
        RecArmToggle,        // REC: SEL push toggles I_RECARM
        RecArmMonToggle,     // REC+MON: SEL push toggles I_RECARM + I_RECMON
        AutoModeStep,        // AUTO: SEL push cycles auto-mode 0..5 wraparound
        AutoModeSet,         // AUTO: V-Pot push sets strip auto-mode = value
        AutoModeDelta,       // AUTO: V-Pot rotation steps strip auto-mode
        StripInstanceDelta,  // Instance: V-Pot rotation cycles strip's
                             //           track's FX list per-strip
        StripInstanceOpen,   // Instance: V-Pot push opens / closes strip's
                             //           track's active FX window
    };
    Kind    kind;
    uint8_t strip;
    double  value;
};

// Encoder-mode state. Default = Nav (= track select) matching the UF8's
// out-of-box feel. Toggled by the NAV (0x73), NUDGE (0x74), FOCUS (0x75)
// buttons and pushed back to Nav by the channel-encoder push (0x76).
// Instance mode is bindable via the `encoder_instance` builtin — no
// dedicated UF8 cell, the user picks any button. Plain rotation in
// Instance mode cycles the focused-domain plug-in instance (same logic
// as the Shift+Channel-Encoder shortcut, just without the modifier).
enum class EncoderMode : uint8_t { Nav, Nudge, Focus, Instance };
std::atomic<EncoderMode> g_encoderMode{EncoderMode::Nav};

// Send/Receive-routing modes for the V-Pots and faders. Four
// independent state pairs (V-Pot vs Fader × Send vs Receive); each
// pair carries one of three states:
//   default                — V-Pots / faders show their normal source
//   AllTracksN      (0..7) — all 8 strips show the same send/receive
//                            index for whatever 8 tracks are in the bank
//   ThisTrack              — the 8 strips show the focused track's
//                            first 8 sends/receives instead of 8 tracks
// Within a domain (e.g. "V-Pots showing sends") the AllTracks-N and
// ThisTrack states are mutually exclusive — turning AllTracksSend3 on
// cancels AllTracksSend1 and ThisTrackSends. Send and Receive modes
// on the same physical output (V-Pot vs Fader) are ALSO mutually
// exclusive: enabling AllTracksReceive3 on the V-Pots cancels any
// active Send mode on the V-Pots, since both want to redraw the same
// strip area. The two physical outputs (V-Pots, Faders) stay
// independent of each other.
//
// Phase: state model only. The actual V-Pot / fader render pipeline
// reads these atomics in a follow-up commit; for now the bindings
// just flip them so the UI can be wired and tested.
std::atomic<int>  g_sendVpotAllIdx     {-1};   // 0..7 = active, -1 = off
std::atomic<bool> g_sendVpotThisTrack  {false};
std::atomic<int>  g_sendFaderAllIdx    {-1};
std::atomic<bool> g_sendFaderThisTrack {false};
std::atomic<int>  g_recvVpotAllIdx     {-1};
std::atomic<bool> g_recvVpotThisTrack  {false};
std::atomic<int>  g_recvFaderAllIdx    {-1};
std::atomic<bool> g_recvFaderThisTrack {false};

// Set by the routing-mode handlers; consumed by pushZonesForVisibleSlots
// to force a full re-push of the strip caches when the routing source
// changes (otherwise the previous mode's last-sent values pin the
// scribble strips / V-Pot bars to stale state).
std::atomic<bool> g_routingDirty{false};

// Per-layer Quick context state.
//   g_activeQuick[layer]   = -1 when no Quick is engaged on that layer
//                          (top-soft-keys behave as the layer's default —
//                          on Layer 1 that's the SSL plug-in maps).
//                          0..2 = Q1/Q2/Q3 engaged.
//   g_activeSubBank[layer] = 0..5 — V-POT default + Soft 1..5. The
//                          hardware bank-select row mutates this in
//                          user-Quick context (in SSL CS/BC context it
//                          mutates g_softKeyBank instead).
// Replaces the legacy g_activeUserBank (flat 12-bank model) and
// g_activeUserDomain (global radio without per-layer scope).
std::atomic<int> g_activeQuick[3]   = { -1, -1, -1 };
std::atomic<int> g_activeSubBank[3] = {  0,  0,  0 };

// Helper: clear every other Send/Receive mode on the same physical
// output (V-Pot or Fader) so the user can never end up with two
// conflicting routing modes pointing at the same strip area.
void clearVpotRouting_()
{
    g_sendVpotAllIdx.store(-1);
    g_sendVpotThisTrack.store(false);
    g_recvVpotAllIdx.store(-1);
    g_recvVpotThisTrack.store(false);
    g_routingDirty.store(true);
}
void clearFaderRouting_()
{
    g_sendFaderAllIdx.store(-1);
    g_sendFaderThisTrack.store(false);
    g_recvFaderAllIdx.store(-1);
    g_recvFaderThisTrack.store(false);
    g_routingDirty.store(true);
}

// One strip's data source for V-Pot OR Fader. When `valid` is true the
// strip's value/scribble pull from a send/receive instead of the bank
// track's volume/pan. A "no route" struct (valid=false, sendCategory=
// kRouteNone) tells the caller to fall through to the existing
// track-direct logic.
constexpr int kRouteNone = INT_MAX;
struct StripRoute {
    MediaTrack* track        = nullptr;
    int         sendCategory = kRouteNone;   // 0 = send, -1 = receive
    int         sendIndex    = -1;
    bool        valid        = false;
    bool        active() const { return sendCategory != kRouteNone; }
};

StripRoute makeRoute_(int strip, int bankOffset, int /*trackCount*/,
                      int allIdx, bool thisTrack, int category)
{
    StripRoute r;
    if (allIdx >= 0) {
        const int rs = strip + bankOffset;
        // Surface-aware lookup: in folder_mode / show_only_selected the
        // strip→track mapping is filtered, so a literal GetTrack(nullptr,
        // rs) would point at the wrong track. visibleTrackAt does the
        // bounds check internally and returns null past the end.
        r.track        = visibleTrackAt(rs);
        r.sendCategory = category;
        r.sendIndex    = allIdx;
    } else if (thisTrack) {
        // Focused track for the strip-as-send-list mode. GetLastTouchedTrack
        // usually survives mixer-scroll, but it can briefly return null
        // mid-edit (e.g. during a SetSurfaceSelected re-broadcast triggered
        // by SetTrackSendInfo_Value). When that happens the route invalidates
        // for every strip and the fader display snaps to -inf dB. Sticky
        // cache: remember the last non-null result and reuse it after
        // validating against REAPER's pointer table.
        static MediaTrack* s_lastTouchedCache = nullptr;
        MediaTrack* lt = GetLastTouchedTrack();
        if (lt) {
            s_lastTouchedCache = lt;
        } else if (s_lastTouchedCache
                   && ValidatePtr2(nullptr, s_lastTouchedCache, "MediaTrack*")) {
            lt = s_lastTouchedCache;
        } else {
            s_lastTouchedCache = nullptr;
        }
        r.track        = lt;
        r.sendCategory = category;
        r.sendIndex    = strip;
    } else {
        return r;  // not in this routing mode
    }
    if (r.track && r.sendIndex >= 0) {
        r.valid = (r.sendIndex < GetTrackNumSends(r.track, category));
    }
    return r;
}

StripRoute resolveVpotRoute_(int strip, int bankOffset, int trackCount)
{
    if (g_sendVpotAllIdx.load() >= 0 || g_sendVpotThisTrack.load()) {
        return makeRoute_(strip, bankOffset, trackCount,
                          g_sendVpotAllIdx.load(),
                          g_sendVpotThisTrack.load(), 0);
    }
    if (g_recvVpotAllIdx.load() >= 0 || g_recvVpotThisTrack.load()) {
        return makeRoute_(strip, bankOffset, trackCount,
                          g_recvVpotAllIdx.load(),
                          g_recvVpotThisTrack.load(), -1);
    }
    return {};
}
StripRoute resolveFaderRoute_(int strip, int bankOffset, int trackCount)
{
    if (g_sendFaderAllIdx.load() >= 0 || g_sendFaderThisTrack.load()) {
        return makeRoute_(strip, bankOffset, trackCount,
                          g_sendFaderAllIdx.load(),
                          g_sendFaderThisTrack.load(), 0);
    }
    if (g_recvFaderAllIdx.load() >= 0 || g_recvFaderThisTrack.load()) {
        return makeRoute_(strip, bankOffset, trackCount,
                          g_recvFaderAllIdx.load(),
                          g_recvFaderThisTrack.load(), -1);
    }
    return {};
}

// Volume read/write helper that funnels through GetTrack/SetTrackSendInfo
// when a route is active, else returns the track-direct linear volume.
double readRouteVolumeLinear_(const StripRoute& r, MediaTrack* fallbackTr)
{
    if (r.valid) {
        return GetTrackSendInfo_Value(r.track, r.sendCategory,
                                      r.sendIndex, "D_VOL");
    }
    // Fall back to the bank track's UI volume (post-envelope).
    if (!fallbackTr) return 1.0;
    double vol = 1.0, pan = 0.0;
    GetTrackUIVolPan(fallbackTr, &vol, &pan);
    return vol;
}

void writeRouteVolumeLinear_(const StripRoute& r, double v)
{
    if (!r.valid) return;
    SetTrackSendInfo_Value(r.track, r.sendCategory, r.sendIndex,
                           "D_VOL", v);
}

// REAPER's GetTrackSendName / GetTrackReceiveName produce the
// send/receive's display name (usually the destination/source track's
// name, or a user-overridden label). The scribble strips will pull
// these when a Send/Receive-mode is active so each strip's top line
// shows e.g. "REVERB" instead of generic "Send 3". Return empty
// string when the index doesn't exist or REAPER didn't fill the buffer.
std::string getTrackSendName(MediaTrack* tr, int sendIdx)
{
    if (!tr || sendIdx < 0) return {};
    char buf[256] = {0};
    if (!GetTrackSendName(tr, sendIdx, buf, sizeof(buf))) return {};
    return std::string(buf);
}
std::string getTrackReceiveName(MediaTrack* tr, int recvIdx)
{
    if (!tr || recvIdx < 0) return {};
    char buf[256] = {0};
    if (!GetTrackReceiveName(tr, recvIdx, buf, sizeof(buf))) return {};
    return std::string(buf);
}

std::string routeName_(const StripRoute& r)
{
    if (!r.valid) return {};
    return (r.sendCategory == 0)
        ? getTrackSendName(r.track, r.sendIndex)
        : getTrackReceiveName(r.track, r.sendIndex);
}

// Destination track for a send / source track for a receive. Returns
// nullptr for hardware-output sends (which have no MediaTrack on the
// far side). Used by the strip rendering to colour the strip with the
// route target's colour and key per-track LED helpers off it.
MediaTrack* routeTargetTrack_(const StripRoute& r)
{
    if (!r.active() || !r.valid || !r.track) return nullptr;
    const char* tag = (r.sendCategory == 0) ? "P_DESTTRACK" : "P_SRCTRACK";
    auto* other = static_cast<MediaTrack*>(GetSetTrackSendInfo(
        r.track, r.sendCategory, r.sendIndex, tag, nullptr));
    if (other && ValidatePtr2(nullptr, other, "MediaTrack*")) return other;
    return nullptr;
}

// Nudge step per physical detent (seconds). User-settings-facing later;
// hard-coded for now.
constexpr double kNudgeSecondsPerStep = 1.0;

// Follow mode: when a track becomes selected, either snap the UF8 bank
// to the 8-wide bucket that contains it (BucketSnap) or make the
// selected track always the leftmost strip (LeftmostStrip). Same
// setting applies to REAPER MCP scroll. User-settings-facing later.
enum class FollowMode : uint8_t { BucketSnap, LeftmostStrip };
std::atomic<FollowMode> g_followMode{FollowMode::BucketSnap};

// Global modifier-key state, set from the USB-input thread, read from the
// main thread in drainInputQueue(). Atomic read/write is sufficient since
// each modifier is a single bool.
std::atomic<bool> g_shiftHeld{false};

// Fractional accumulators for the channel encoder. The UF8 emits
// several events per physical detent (each with speed 1-2), so we
// divide by the scale and only consume whole integer steps. Separate
// accumulators per mode so mode switches don't bleed fractional state
// across. Main-thread only.
double g_selectAccum   = 0.0;
double g_nudgeAccum    = 0.0;
double g_instanceAccum = 0.0;
double g_encoderAccum  = 0.0;   // unified accumulator for the bindings-routed
                                // EncoderRotation pipeline (Plain / Shift / Cmd /
                                // Ctrl all share so the user can change
                                // modifier mid-rotation without losing detents).

// Selection-Mode AUTO V-Pot rotation accumulators — per strip so each
// V-Pot scrolls its own track's auto-mode. Slower scale than pan
// (kAutoModeRotateScale) so one mode-step takes a few detents instead
// of a flick.
double g_autoModeAccum[8] = {0, 0, 0, 0, 0, 0, 0, 0};
constexpr double kAutoModeRotateScale = 6.0;

// Selection-Mode Instance per-strip V-Pot rotation accumulators.
// Each strip cycles its own track's FX list independently — the V-Pot
// doesn't touch global focus or the track's selection (Frank
// 2026-05-14 "soll ohne track selection gehen - einfach nur den track
// des v-pots verändern").
double g_stripInstanceAccum[8] = {0, 0, 0, 0, 0, 0, 0, 0};

// Per-track active-FX index for Selection-Mode Instance. Survives
// bank scrolls so the user's last-cycled FX on each track stays
// remembered. Keyed by MediaTrack* — entries for deleted tracks
// linger harmlessly until plug-in reload.
std::unordered_map<MediaTrack*, int> g_stripInstanceFxIdx;

// Returns the current Instance-mode active FX index for `tr`,
// defaulting to 0 when the user hasn't cycled this track yet (or 0
// when the index drifted out of bounds because the user removed FX).
int stripInstanceActiveFx_(MediaTrack* tr)
{
    if (!tr) return -1;
    const int n = TrackFX_GetCount(tr);
    if (n <= 0) return -1;
    auto it = g_stripInstanceFxIdx.find(tr);
    int idx = (it == g_stripInstanceFxIdx.end()) ? 0 : it->second;
    if (idx < 0)  idx = 0;
    if (idx >= n) idx = n - 1;
    return idx;
}

// Forward decls — implementations live further down (followSelectedInMixer
// after the encoder mode toggles, emitMouseScroll near the input dispatch).
void followSelectedInMixer(MediaTrack* tr);
void emitMouseScroll(int32_t delta);

// Encoder-action helpers — extracted so both the legacy per-mode drain
// handlers (kept for backward-compat) AND the bindings-routed dispatch
// (EncoderRotation → bindings::dispatchEncoder → builtins) end up
// running the same logic for each step. All three run on the main
// thread (drainInputQueue caller).
void applySelectRelative_(int step)
{
    if (step == 0) return;
    const int trackCount = CountTracks(nullptr);
    if (trackCount == 0) return;
    int cur = -1;
    for (int t = 0; t < trackCount; ++t) {
        if (GetMediaTrackInfo_Value(GetTrack(nullptr, t), "I_SELECTED") > 0.5) {
            cur = t;
            break;
        }
    }
    int next = (cur >= 0 ? cur : 0) + step;
    if (next < 0) next = 0;
    if (next > trackCount - 1) next = trackCount - 1;
    if (MediaTrack* tr = GetTrack(nullptr, next)) {
        SetOnlyTrackSelected(tr);
        followSelectedInMixer(tr);
    }
}
void applyPlayheadNudge_(int step)
{
    if (step == 0) return;
    const double cur = GetCursorPosition();
    SetEditCurPos(cur + step * kNudgeSecondsPerStep, true, false);
}
void applyMouseScroll_(int delta)
{
    if (delta == 0) return;
    emitMouseScroll(static_cast<int32_t>(delta));
}

// Cycle the active CS or BC plug-in instance by `step` slots. Shared
// between the Shift+Channel-Encoder dispatch (drainInputQueue) and the
// bindable instance_next / instance_prev builtins so the same wraparound
// + repaint logic runs from either entry point. Domain follows the
// focused-param domain (Quick1 = CS, Quick2 = BC).
//
// BC gate: cycling targets the BC anchor (because UC1 BC controls read
// from the anchor, so the user expects the visible result of cycling to
// hit the same plug-in the knobs already drive). But it ONLY fires when
// focused track == BC anchor — i.e. the user has explicitly selected the
// channel UC1 currently shows in its BC module. Encoder 2 alone (which
// only shifts the BC module's view) is NOT enough to enable cycling;
// the user must Channel-1 (selection encoder) onto that track first.
// Without this gate the encoder would silently cycle a track UC1 isn't
// even displaying — confusing, no visible feedback.
//
// CS has no anchor concept, so it always targets the focused track.
void applyInstanceCycle_(int step)
{
    if (step == 0) return;
    if (!g_uc1_surface) return;

    // Walk learned plug-ins ON THE SELECTED TRACK ONLY. CS and BC
    // hits both come from the same track — pulling BC from a separate
    // bcAnchor track meant the cycle silently picked up neighbours
    // ("welche links und rechts mit"), surprising the user. Frank
    // 2026-05-12: "instance cycle soll nur den selected channel
    // durchgehen". When the cycle lands on a BC plug-in we still move
    // the BC anchor onto the focused track so Encoder 2 + the carousel
    // line up with what just got highlighted.
    MediaTrack* tr = static_cast<MediaTrack*>(g_uc1_surface->focusedTrack());
    if (!tr) tr = GetSelectedTrack(nullptr, 0);
    if (!tr || !ValidatePtr2(nullptr, tr, "MediaTrack*")) return;

    struct Hit { int fxIdx; uf8::Domain dom; int instIdx; };
    std::vector<Hit> hits;
    int csCount = 0, bcCount = 0, uf8OnlyCount = 0;
    const int n = TrackFX_GetCount(tr);
    char fxName[256];
    for (int i = 0; i < n; ++i) {
        if (!TrackFX_GetFXName(tr, i, fxName, sizeof(fxName))) continue;
        const auto* pm = uf8::lookupPluginMapByName(fxName);
        if (pm && pm->domain == uf8::Domain::ChannelStrip) {
            hits.push_back({i, uf8::Domain::ChannelStrip, csCount++});
            continue;
        }
        if (pm && pm->domain == uf8::Domain::BusComp) {
            hits.push_back({i, uf8::Domain::BusComp, bcCount++});
            continue;
        }
        // UF8-only user maps don't surface via lookupPluginMapByName
        // (their synthesised PluginMap carries Domain::None and no
        // slots) but they still represent instances the user might
        // want to focus the UF8 strips on. Pull the owned record so
        // we can include them in the cycle (Frank 2026-05-12).
        const auto* um = uf8::user_plugins::lookupOwnedByName(fxName);
        if (um && um->domain == uf8::Domain::None && um->uf8Mode) {
            hits.push_back({i, uf8::Domain::None, uf8OnlyCount++});
        }
    }
    if (hits.size() < 2) return;   // nothing meaningful to cycle through

    const auto fp = uf8::getFocusedParam();
    uf8::Domain curDom = fp.domain;
    // No CS/BC focus → start from whichever instance class actually
    // anchors the active surface. UF8-only is the default fallback
    // when neither CS nor BC currently drives UC1.
    if (curDom == uf8::Domain::None && uf8OnlyCount == 0) {
        curDom = (csCount > 0) ? uf8::Domain::ChannelStrip
              : (bcCount > 0) ? uf8::Domain::BusComp
              :                  uf8::Domain::None;
    }
    const int curInstIdx =
        (curDom == uf8::Domain::BusComp)      ? uc1::bcInstanceIndex(tr)
      : (curDom == uf8::Domain::ChannelStrip) ? uc1::csInstanceIndex(tr)
      :                                          uc1::uf8OnlyInstanceIndex(tr);
    int curK = 0;
    for (size_t k = 0; k < hits.size(); ++k) {
        if (hits[k].dom == curDom && hits[k].instIdx == curInstIdx) {
            curK = (int)k; break;
        }
    }
    int nextK = (curK + step) % static_cast<int>(hits.size());
    if (nextK < 0) nextK += static_cast<int>(hits.size());
    const Hit& target = hits[nextK];
    // Sync the per-strip Instance index on the focused track so the
    // Selection-Mode Instance colour-bar readout (stripInstanceActiveFx_)
    // moves with the Encoder cycle on the focused/selected strip. Other
    // strips keep their own per-strip g_stripInstanceFxIdx, so the
    // Encoder Cycle only visibly affects the selected channel (Frank
    // 2026-05-14 "soll nur beim selected channel wie vorher"). The
    // per-strip V-Pot path in StripInstanceDelta still updates this
    // map independently for non-focused strips.
    g_stripInstanceFxIdx[tr] = target.fxIdx;
    if (target.dom == uf8::Domain::ChannelStrip) {
        uf8::setFocus({target.dom, 0});
        uc1::setCsInstanceIndex(tr, target.instIdx);
        // Only follow with the GUI when the "with GUI" variant of SSL
        // Strip Mode is active. Plain ssl_strip_mode_toggle leaves the
        // with-Gui flag false, so the instance cycle stays headless.
        if (g_pluginFaderMode.load() && g_pluginFaderModeWithGui.load()) {
            g_pluginGuiSyncRequest.store(true);
        }
    } else if (target.dom == uf8::Domain::BusComp) {
        uf8::setFocus({target.dom, 0});
        uc1::setBcInstanceIndex(tr, target.instIdx);
        // Pin the BC anchor to the focused track so Encoder 2 + the
        // BC carousel agree with the cycle's selection. Idempotent
        // when tr already == bcAnchor.
        g_uc1_surface->setBcAnchorTrack(tr);
    } else {
        uf8::setFocus({uf8::Domain::None, 0});
        uc1::setUf8OnlyInstanceIndex(tr, target.instIdx);
        if (g_pluginFaderMode.load() && g_pluginFaderModeWithGui.load()) {
            g_pluginGuiSyncRequest.store(true);
        }
    }
    g_uc1_surface->invalidateCache();
    g_uc1_surface->refresh();
    g_pageDirty.store(true);
    g_bankDirty.store(true);
}
constexpr double kChannelEncoderScale = 4.0;

// When set, the Channel-Encoder Select also shifts the UF8 bank and the
// REAPER MCP scroll so the newly-selected track stays visible. Planned
// as a user-facing settings option (see memory backlog); currently
// hard-coded ON because the mixer-follow behaviour is what makes the
// wheel feel right in every test session.
constexpr bool kSelectFollowsMixer = true;

// Short, LCD-friendly plug-in name. Strips "VST3i: " / "VST: " / etc.
// prefixes and any trailing " (Vendor)" suffix. Generic fallback used
// when the FX is not a recognised Instance — see fxCycleDisplayName_
// for the Instance-aware variant the FX-Cycle colour-bar uses.
std::string shortFxName_(MediaTrack* tr, int fxIdx)
{
    if (!tr || fxIdx < 0) return std::string{};
    char buf[64] = {0};
    if (!TrackFX_GetFXName(tr, fxIdx, buf, sizeof(buf))) return std::string{};
    std::string s(buf);
    static const char* kPrefixes[] = {
        "VST3i: ", "VST3: ", "VSTi: ", "VST: ", "AU: ", "AUi: ", "JS: "
    };
    for (auto* p : kPrefixes) {
        const size_t l = std::strlen(p);
        if (s.size() >= l && s.compare(0, l, p) == 0) {
            s.erase(0, l);
            break;
        }
    }
    if (auto p = s.find(" ("); p != std::string::npos) s.erase(p);
    return s;
}

// Display name for the FX-Cycle colour-bar — when the FX is an
// Instance (built-in PluginMap or user-mapped via FX Learn), use the
// same `displayShort` that Encoder → Instance Cycle uses ("Townhou",
// not "bx_town"). Falls back to the prefix-stripped REAPER name for
// non-Instance FX. Frank 2026-05-14: "Für FX Cycle bitte dieselben
// Namen für Instances verwenden, wie bei Instance Cycle".
std::string fxCycleDisplayName_(MediaTrack* tr, int fxIdx)
{
    if (!tr || fxIdx < 0) return std::string{};
    char buf[256] = {0};
    if (!TrackFX_GetFXName(tr, fxIdx, buf, sizeof(buf))) return std::string{};
    if (const auto* pm = uf8::lookupPluginMapByName(buf)) {
        return pm->displayShort;
    }
    if (const auto* um = uf8::user_plugins::lookupOwnedByName(buf)) {
        return um->displayShort;
    }
    return shortFxName_(tr, fxIdx);
}

void followSelectedInMixer(MediaTrack* tr)
{
    if (!kSelectFollowsMixer || !tr) return;

    // REAPER MCP: scroll so the selected track becomes leftmost (or
    // stays within the visible range if REAPER decides to keep context).
    SetMixerScroll(tr);

    // Bank-snap operates in surface-visible space — under folder_mode /
    // show_only_selected the bank coordinates are filtered indices, not
    // raw REAPER track indices. If the selected track happens to be
    // filtered out (e.g. show-only-selected was just toggled), the
    // followSelectedInMixer call exits without scrolling.
    const int trackCount = visibleTrackCount();
    int idx = -1;
    for (int t = 0; t < trackCount; ++t) {
        if (visibleTrackAt(t) == tr) { idx = t; break; }
    }
    if (idx < 0) return;

    int bank = g_bankOffset.load();
    if (g_followMode.load() == FollowMode::LeftmostStrip) {
        // Selected track always at strip 0. Clamp so we don't scroll
        // past the last track.
        bank = idx;
        if (bank > trackCount - 1) bank = trackCount > 0 ? trackCount - 1 : 0;
    } else {
        // Bucket snap: only shift if the selection fell outside the
        // current 8-wide window.
        if (idx < bank || idx >= bank + 8) bank = (idx / 8) * 8;
    }
    if (bank != g_bankOffset.exchange(bank)) g_bankDirty.store(true);
}

// Synthesise a mouse-wheel scroll event at the cursor's current screen
// position so the "Focus" encoder mode emulates a real scroll wheel —
// hover the mouse over a plug-in parameter and spin the encoder.
#ifdef __APPLE__
void emitMouseScroll(int32_t delta)
{
    CGEventRef ev = CGEventCreateScrollWheelEvent(
        nullptr, kCGScrollEventUnitLine, 1, delta);
    if (!ev) return;
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
}
#else
void emitMouseScroll(int32_t) {}   // non-macOS: TBD
#endif
std::mutex                  g_inQueueMutex;
std::vector<PendingInput>   g_inQueue;

// Forward declarations for helpers defined later in the file but
// referenced from drainInputQueue's FLIP-mode path.
uint16_t linearVolumeToPb(double linear);
double   pbToLinearVolume(uint16_t pb14);
double   uiVolLinear(MediaTrack* tr);

// Hardware fader top — the actual highest pb14 the UF8 firmware emits.
// Defined here so drainInputQueue can normalise FLIP-mode fader values
// against the real hardware range, not the 14-bit protocol max (16383).
// Full rationale + calibration in the kUf8FaderTopDb / pbToLinearVolume
// block further down. Kept in sync with that block.
constexpr uint16_t kUf8FaderPbMax = 15583;

// FX Learn UF8 — locate the user-mapped plug-in instance currently
// targeted by the focused track + instance switcher. Used by the SSL
// Strip Mode + GUI builtin (Phase 4) and the per-strip dispatch path
// (Phase 5+). Domain-aware: counts CS-domain user plug-ins via
// uc1::csInstanceIndex, BC-domain via uc1::bcInstanceIndex; domain=None
// user plug-ins don't participate (their instance state isn't tracked).
struct UserPluginCtx {
    MediaTrack*               tr;
    int                       fxIdx;
    const uf8::UserPluginMap* map;
};
UserPluginCtx findUserPluginOnTrack_(MediaTrack* tr, uf8::Domain wantDomain)
{
    UserPluginCtx out{ tr, -1, nullptr };
    if (!tr) return out;
    if (!ValidatePtr2(nullptr, tr, "MediaTrack*")) return out;

    // Gate: only maps with uf8Mode==true participate in UF8 strip-mode.
    // CS-only / BC-only maps stay visible on UC1 but are invisible to
    // the UF8 strip engine. UF8-only maps (domain==None, uf8Mode==true)
    // match any wantDomain — they're the catch-all for plug-ins that
    // don't fit the SSL CS/BC schematic at all (Frank 2026-05-12).
    auto tryDomain = [&](uf8::Domain dom) -> bool {
        const int wantIdx = (dom == uf8::Domain::ChannelStrip)
            ? uc1::csInstanceIndex(tr)
            : (dom == uf8::Domain::BusComp)
                ? uc1::bcInstanceIndex(tr)
                : uc1::uf8OnlyInstanceIndex(tr);
        int seen = 0;
        const int n = TrackFX_GetCount(tr);
        char fxName[256];
        for (int i = 0; i < n; ++i) {
            if (!TrackFX_GetFXName(tr, i, fxName, sizeof(fxName))) continue;
            const auto* pm = uf8::lookupPluginMapByName(fxName);
            if (!pm || pm->domain != dom) continue;
            if (seen == wantIdx) {
                const auto* um = uf8::user_plugins::lookupOwnedByName(fxName);
                if (um && um->uf8Mode) {
                    out.fxIdx = i;
                    out.map   = um;
                }
                return true;
            }
            ++seen;
        }
        return false;
    };

    if (wantDomain == uf8::Domain::None) {
        if (tryDomain(uf8::Domain::None)) return out;
        if (tryDomain(uf8::Domain::ChannelStrip)) return out;
        tryDomain(uf8::Domain::BusComp);
        return out;
    }
    if (tryDomain(wantDomain)) return out;
    const uf8::Domain other = (wantDomain == uf8::Domain::ChannelStrip)
        ? uf8::Domain::BusComp
        : uf8::Domain::ChannelStrip;
    if (tryDomain(other)) return out;
    tryDomain(uf8::Domain::None);
    return out;
}

// Cached resolver — returns the user-plug-in context for the currently
// focused track when SSL Strip Mode is on AND the focused track has a
// user-mapped plug-in. Returns {nullptr,-1,nullptr} otherwise. All eight
// UF8 strips drive params on this ONE plug-in instance per strip-N
// binding in `map->uf8.strips[N]` / `map->uf8.banks.banks[bank][N]`.
//
// Cache invalidation on:
//   - g_pluginFaderMode change
//   - focused track changed
//   - user_plugins::generation() bumped (catalog edited)
//   - TrackFX_GetCount on the focused track changed (FX add/remove)
UserPluginCtx userStripCtxFocused_()
{
    static UserPluginCtx s_cache{nullptr, -1, nullptr};
    static bool          s_cacheMode      = false;
    static void*         s_cacheTr        = nullptr;
    static int           s_cacheGen       = -1;
    static int           s_cacheFxCount   = -1;
    static int           s_cacheCsInst    = -1;
    static int           s_cacheBcInst    = -1;
    static int           s_cacheUf8Inst   = -1;
    static uf8::Domain   s_cacheDomain    = uf8::Domain::None;

    const bool curMode = g_uf8PluginMode.load();
    if (!curMode) {
        s_cacheTr = nullptr;
        return {nullptr, -1, nullptr};
    }

    MediaTrack* tr = nullptr;
    if (g_uc1_surface) {
        tr = static_cast<MediaTrack*>(g_uc1_surface->focusedTrack());
    }
    if (!tr) tr = GetSelectedTrack(nullptr, 0);
    if (!tr) {
        s_cacheTr = nullptr;
        return {nullptr, -1, nullptr};
    }

    const int curGen     = uf8::user_plugins::generation();
    const int curFxCount =
        ValidatePtr2(nullptr, tr, "MediaTrack*") ? TrackFX_GetCount(tr) : 0;
    const int curCsInst  = uc1::csInstanceIndex(tr);
    const int curBcInst  = uc1::bcInstanceIndex(tr);
    const int curUf8Inst = uc1::uf8OnlyInstanceIndex(tr);
    auto fp = uf8::getFocusedParam();
    uf8::Domain wantDom = fp.domain;

    if (curMode    == s_cacheMode  &&
        tr         == s_cacheTr    &&
        curGen     == s_cacheGen   &&
        curFxCount == s_cacheFxCount &&
        curCsInst  == s_cacheCsInst &&
        curBcInst  == s_cacheBcInst &&
        curUf8Inst == s_cacheUf8Inst &&
        wantDom    == s_cacheDomain)
    {
        return s_cache;
    }

    s_cache        = findUserPluginOnTrack_(tr, wantDom);
    s_cacheMode    = curMode;
    s_cacheTr      = tr;
    s_cacheGen     = curGen;
    s_cacheFxCount = curFxCount;
    s_cacheCsInst  = curCsInst;
    s_cacheBcInst  = curBcInst;
    s_cacheUf8Inst = curUf8Inst;
    s_cacheDomain  = wantDom;
    return s_cache;
}

// SSL Strip Mode's per-track CS resolver. Walks all FX on the track
// and returns the highest-priority CS-domain plug-in for fader / pan /
// Type-zone routing. Priority (Frank 2026-05-14):
//   1. User-CS map with isDefault=true (overrides built-in SSL CS on
//      tracks where the user has nominated a different "channel strip"
//      e.g. bx SSL9k as their default).
//   2. Built-in SSL CS variant (CS 2 / 4K G/E/B / SSL 360 Link CS).
//   3. User-CS map without isDefault (fallback for tracks that have
//      ONLY a user CS and no built-in SSL CS).
//
// Differs from uf8::lookupPluginOnTrack(tr, ChannelStrip): that one
// walks first-hit and bumps via the instance-cycle index. This one
// applies the isDefault tiebreak and ignores instance cycling, because
// in SSL Strip Mode the user has explicitly nominated the priority
// plug-in for fader / pan / Type-zone — the V-Pot instance cycle is
// a separate concern (which CS instance's params show on V-Pots).
struct CsStripPick {
    int                fxIndex;
    const uf8::PluginMap* map;
    bool               isUser;
};
CsStripPick csForStripModeOnTrack_(MediaTrack* tr)
{
    CsStripPick out{ -1, nullptr, false };
    if (!tr) return out;
    if (!ValidatePtr2(nullptr, tr, "MediaTrack*")) return out;
    const int n = TrackFX_GetCount(tr);
    int fxBuiltin = -1, fxUser = -1;
    const uf8::PluginMap* mapBuiltin = nullptr;
    const uf8::PluginMap* mapUser    = nullptr;
    char buf[512];
    for (int fx = 0; fx < n; ++fx) {
        buf[0] = 0;
        TrackFX_GetFXName(tr, fx, buf, sizeof(buf));
        if (buf[0] == 0) continue;
        const uf8::PluginMap* m = uf8::lookupPluginMapByName(buf);
        if (!m || m->domain != uf8::Domain::ChannelStrip) continue;

        const uf8::UserPluginMap* owned = uf8::user_plugins::lookupOwnedByName(buf);
        if (owned) {
            if (owned->isDefault) {
                // Highest tier — no need to keep walking.
                return { fx, m, true };
            }
            if (fxUser < 0) { fxUser = fx; mapUser = m; }
        } else {
            if (fxBuiltin < 0) { fxBuiltin = fx; mapBuiltin = m; }
        }
    }
    if (fxBuiltin >= 0) return { fxBuiltin, mapBuiltin, false };
    if (fxUser    >= 0) return { fxUser,    mapUser,    true  };
    return out;
}

// CS plug-in's "Fader Level" param (= the SSL strip's own internal
// fader, distinct from REAPER's track volume). Used when the Plugin
// button is engaged so the UF8 motor faders drive the SSL strip
// fader instead of post-effect track volume. For built-in CS variants
// the vst3 param index is hardcoded (FaderLevel is NOT a Link-map slot
// for the native plug-ins — only the SSL 360 Link wrapper exposes it
// at linkIdx 1). For SSL 360 Link CS + user-CS maps we fall back to
// the FaderLevel slot lookup. Returns {-1, -1} when the track has no
// CS plug-in or the user CS has no FaderLevel slot mapped.
struct CsFaderHandle { int fxIndex; int vst3Param; };
CsFaderHandle csFaderForTrack(MediaTrack* tr)
{
    const auto pick = csForStripModeOnTrack_(tr);
    if (!pick.map) return { -1, -1 };
    int p = -1;
    const char* sn = pick.map->displayShort;
    if      (std::strcmp(sn, "CS 2") == 0) p = 38;
    else if (std::strcmp(sn, "4K G") == 0) p = 12;
    else if (std::strcmp(sn, "4K E") == 0) p = 6;
    else if (std::strcmp(sn, "4K B") == 0) p = 6;
    else {
        // SSL 360 Link CS + all user-CS maps: slot lookup.
        if (const auto* sl = uf8::findSlotByLinkIdx(*pick.map, 1 /*FaderLevel*/))
            p = sl->vst3Param;
    }
    return { pick.fxIndex, p };
}

// Per-track user-plugin fader lookup. Returns the fader binding from
// uf8.strips[0] of the first user-mapped plugin on this track in the
// given domain. Used by SSL Strip Mode (per-track) so each UF8 strip
// drives its own track's user-plugin fader param.
struct UserFaderHandle { int fxIndex; int vst3Param; bool inverted; };
UserFaderHandle userFaderForTrack(MediaTrack* tr, uf8::Domain domain)
{
    if (!tr) return {-1, -1, false};
    auto match = uf8::lookupPluginOnTrack(tr, domain);
    if (!match.map) return {-1, -1, false};
    char fxName[512];
    TrackFX_GetFXName(tr, match.fxIndex, fxName, sizeof(fxName));
    const auto* um = uf8::user_plugins::lookupOwnedByName(fxName);
    if (!um || !um->uf8Mode) return {-1, -1, false};
    const int fp = um->uf8.strips[0].faderVst3Param;
    if (fp < 0) return {-1, -1, false};
    return {match.fxIndex, fp, um->uf8.strips[0].faderInverted};
}

// CS plug-in's Pan param (linkIdx 3 across all CS variants). In SSL
// Strip Mode, the V-Pot's Pan-fallback drives this instead of REAPER's
// track pan, so the SSL strip Pan stays the surface's source-of-truth.
// Uses the same isDefault-aware resolver as csFaderForTrack so the
// fader + pan target stay in sync on multi-CS tracks.
struct CsPanHandle { int fxIndex; int vst3Param; };
CsPanHandle csPanForTrack(MediaTrack* tr)
{
    const auto pick = csForStripModeOnTrack_(tr);
    if (!pick.map) return { -1, -1 };
    const auto* sl = uf8::findSlotByLinkIdx(*pick.map, 3);
    if (!sl) return { -1, -1 };
    return { pick.fxIndex, sl->vst3Param };
}

void queueInput(PendingInput e)
{
    std::lock_guard<std::mutex> lk(g_inQueueMutex);
    // Coalesce volume updates: only the latest absolute position matters,
    // so collapse runs of fader events into a single pending entry per strip.
    if (e.kind == PendingInput::VolumeAbs) {
        for (auto& p : g_inQueue) {
            if (p.kind == PendingInput::VolumeAbs && p.strip == e.strip) {
                p.value = e.value;
                return;
            }
        }
    }
    g_inQueue.push_back(e);
}

void drainInputQueue()
{
    std::vector<PendingInput> local;
    {
        std::lock_guard<std::mutex> lk(g_inQueueMutex);
        local.swap(g_inQueue);
    }
    // Two counts: trackCount = REAPER's full track set (used by global
    // navigation like SelectRelative, which operates on the project, not
    // the filtered surface). surfaceCount = visibleTrackCount() which
    // honours folder_mode + show_only_selected — used to map strip index
    // to track for fader / V-Pot / button events.
    const int trackCount    = CountTracks(nullptr);
    const int surfaceCount  = visibleTrackCount();
    const int bankOffset    = g_bankOffset.load();
    for (const auto& e : local) {
        // Global-scope events (no strip) are dispatched before the
        // per-strip track resolution below.
        if (e.kind == PendingInput::MainAction) {
            Main_OnCommand(static_cast<int>(e.value), 0);
            continue;
        }
        if (e.kind == PendingInput::AutomationMode) {
            if (MediaTrack* tr = GetSelectedTrack(nullptr, 0)) {
                SetTrackAutomationMode(tr, static_cast<int>(e.value));
            }
            continue;
        }
        if (e.kind == PendingInput::AutomationModeGlobal) {
            SetGlobalAutomationOverride(static_cast<int>(e.value));
            continue;
        }
        if (e.kind == PendingInput::FocusSelected) {
            if (MediaTrack* tr = GetSelectedTrack(nullptr, 0)) {
                followSelectedInMixer(tr);
            }
            continue;
        }
        if (e.kind == PendingInput::PlayheadNudge) {
            g_nudgeAccum += e.value / kChannelEncoderScale;
            int step = 0;
            if (g_nudgeAccum >=  1.0) { step = static_cast<int>(g_nudgeAccum); g_nudgeAccum -= step; }
            if (g_nudgeAccum <= -1.0) { step = static_cast<int>(g_nudgeAccum); g_nudgeAccum -= step; }
            if (step != 0) {
                const double cur = GetCursorPosition();
                SetEditCurPos(cur + step * kNudgeSecondsPerStep, true, false);
            }
            continue;
        }
        if (e.kind == PendingInput::MouseScroll) {
            // Mouse-wheel feels natural with per-event events: high rate
            // produces fast scroll, low rate slow. No accumulation.
            emitMouseScroll(static_cast<int32_t>(e.value));
            continue;
        }
        if (e.kind == PendingInput::InstanceCycle) {
            g_instanceAccum += e.value / kChannelEncoderScale;
            int step = 0;
            if (g_instanceAccum >=  1.0) { step = static_cast<int>(g_instanceAccum); g_instanceAccum -= step; }
            if (g_instanceAccum <= -1.0) { step = static_cast<int>(g_instanceAccum); g_instanceAccum -= step; }
            applyInstanceCycle_(step);
            continue;
        }
        if (e.kind == PendingInput::EncoderRotation) {
            // Bindings-routed encoder dispatch. Accumulate raw signed6
            // detents into integer steps, then hand off to the
            // ChannelEncoder binding so the active modifier slot's
            // builtin runs. Builtin gets `param = step` (signed) so
            // delta-aware actions (cycle, scroll) can react in
            // proportion to user input.
            g_encoderAccum += e.value / kChannelEncoderScale;
            int step = 0;
            if (g_encoderAccum >=  1.0) { step = static_cast<int>(g_encoderAccum); g_encoderAccum -= step; }
            if (g_encoderAccum <= -1.0) { step = static_cast<int>(g_encoderAccum); g_encoderAccum -= step; }
            if (step != 0) {
                uf8::bindings::dispatchEncoder(
                    uf8::bindings::ButtonId::ChannelEncoder, step);
            }
            continue;
        }
        if (e.kind == PendingInput::SelectRelative) {
            if (trackCount == 0) continue;
            // Accumulate scaled fractional deltas — the UF8 emits
            // multiple events per physical detent, so we divide each
            // per-event magnitude and only consume whole-track steps.
            g_selectAccum += e.value / kChannelEncoderScale;
            int step = 0;
            if (g_selectAccum >=  1.0) { step = static_cast<int>(g_selectAccum);        g_selectAccum -= step; }
            if (g_selectAccum <= -1.0) { step = static_cast<int>(g_selectAccum);        g_selectAccum -= step; }
            if (step == 0) continue;

            int cur = -1;
            for (int t = 0; t < trackCount; ++t) {
                if (GetMediaTrackInfo_Value(GetTrack(nullptr, t), "I_SELECTED") > 0.5) {
                    cur = t;
                    break;
                }
            }
            int next = (cur >= 0 ? cur : 0) + step;
            if (next < 0) next = 0;
            if (next > trackCount - 1) next = trackCount - 1;
            if (MediaTrack* tr = GetTrack(nullptr, next)) {
                SetOnlyTrackSelected(tr);
                followSelectedInMixer(tr);
            }
            continue;
        }

        const int slot = e.strip + bankOffset;
        if (slot >= surfaceCount) continue;
        MediaTrack* tr = visibleTrackAt(slot);
        if (!tr) continue;
        switch (e.kind) {
            case PendingInput::SoloToggle: {
                // Routing mode: Solo lights up the SEND/RECEIVE target
                // track ("listen to this return only"). Same precedence
                // as the CUT/MuteToggle handler — fader-routing wins
                // over V-Pot routing if both are active. Hardware-output
                // sends have no target track; eat the press silently
                // there. Without this, a user in 8-sends mode pressing
                // Solo on a return-strip soloed the source bank track,
                // not the return — Frank 2026-05-08.
                StripRoute sr = resolveFaderRoute_(e.strip, bankOffset, surfaceCount);
                if (!sr.active())
                    sr = resolveVpotRoute_(e.strip, bankOffset, surfaceCount);
                if (sr.active()) {
                    if (auto* target = routeTargetTrack_(sr)) {
                        CSurf_OnSoloChange(target, -1);
                    }
                    // active-but-no-target (hardware-output send / invalid
                    // route) → eat the press, do NOT fall through.
                    break;
                }
                // FX Learn UF8: user-strip Solo toggles a bound vst3
                // param on the focused user-plug-in instance (e.g. a
                // bypass / mute on that plug-in). Empty binding eats
                // the event — LEDs / scribble are blank in that mode,
                // so falling through to track Solo would be invisible
                // and surprising (Frank 2026-05-09).
                if (g_uf8PluginMode.load()) {
                    if (auto uctx = userStripCtxFocused_(); uctx.map) {
                        const auto& sb = uctx.map->uf8.strips[
                            static_cast<int>(e.strip)];
                        if (sb.soloVst3Param >= 0) {
                            const double cur = TrackFX_GetParamNormalized(
                                uctx.tr, uctx.fxIdx, sb.soloVst3Param);
                            TrackFX_SetParamNormalized(uctx.tr, uctx.fxIdx,
                                sb.soloVst3Param, cur < 0.5 ? 1.0 : 0.0);
                        }
                        break;
                    }
                }
                CSurf_OnSoloChange(tr, -1);
                break;
            }
            case PendingInput::MuteToggle: {
                // Routing mode: when the strip represents a send/receive
                // (V-Pot or fader routing active), the CUT button toggles
                // the routed entity's mute, not the bank track's. Prefer
                // fader route if both are active — fader is the "primary"
                // routing context for the CUT-fader region.
                StripRoute mr = resolveFaderRoute_(e.strip, bankOffset, surfaceCount);
                if (!mr.active())
                    mr = resolveVpotRoute_(e.strip, bankOffset, surfaceCount);
                if (mr.active() && mr.valid) {
                    const double cur = GetTrackSendInfo_Value(
                        mr.track, mr.sendCategory, mr.sendIndex, "B_MUTE");
                    SetTrackSendInfo_Value(mr.track, mr.sendCategory,
                                           mr.sendIndex, "B_MUTE",
                                           cur > 0.5 ? 0.0 : 1.0);
                    break;
                }
                if (mr.active()) break;     // routed but slot empty — eat the press
                if (g_uf8PluginMode.load()) {
                    if (auto uctx = userStripCtxFocused_(); uctx.map) {
                        const auto& sb = uctx.map->uf8.strips[
                            static_cast<int>(e.strip)];
                        if (sb.cutVst3Param >= 0) {
                            const double cur = TrackFX_GetParamNormalized(
                                uctx.tr, uctx.fxIdx, sb.cutVst3Param);
                            TrackFX_SetParamNormalized(uctx.tr, uctx.fxIdx,
                                sb.cutVst3Param, cur < 0.5 ? 1.0 : 0.0);
                        }
                        break;
                    }
                }
                CSurf_OnMuteChange(tr, -1);
                break;
            }
            case PendingInput::SelectToggle: {
                if (g_uf8PluginMode.load()) {
                    if (auto uctx = userStripCtxFocused_(); uctx.map) {
                        const auto& sb = uctx.map->uf8.strips[
                            static_cast<int>(e.strip)];
                        if (sb.selVst3Param >= 0) {
                            const double cur = TrackFX_GetParamNormalized(
                                uctx.tr, uctx.fxIdx, sb.selVst3Param);
                            TrackFX_SetParamNormalized(uctx.tr, uctx.fxIdx,
                                sb.selVst3Param, cur < 0.5 ? 1.0 : 0.0);
                        }
                        // Empty binding: eat the event (see Solo above).
                        break;
                    }
                }
                CSurf_OnSelectedChange(tr, -1);
                break;
            }
            case PendingInput::SelectExclusive:
                if (g_uf8PluginMode.load()) {
                    if (auto uctx = userStripCtxFocused_(); uctx.map) {
                        const auto& sb = uctx.map->uf8.strips[
                            static_cast<int>(e.strip)];
                        if (sb.selVst3Param >= 0) {
                            const double cur = TrackFX_GetParamNormalized(
                                uctx.tr, uctx.fxIdx, sb.selVst3Param);
                            TrackFX_SetParamNormalized(uctx.tr, uctx.fxIdx,
                                sb.selVst3Param, cur < 0.5 ? 1.0 : 0.0);
                        }
                        break;
                    }
                }
                SetOnlyTrackSelected(tr);
                followSelectedInMixer(tr);
                break;
            case PendingInput::VolumeAbs: {
                // Send/Receive routing wins over every other fader-input
                // path: when the user explicitly turned on a routing
                // mode, the fader writes the routed level (no plugin-fader,
                // no track-vol). FLIP swaps fader/V-Pot inside send mode:
                // fader writes the routed D_PAN, V-Pot takes vol.
                {
                    const StripRoute fr = resolveFaderRoute_(
                        e.strip, bankOffset, trackCount);
                    if (fr.active() && fr.valid) {
                        if (g_flip.load()) {
                            const uint16_t pbF = linearVolumeToPb(e.value);
                            double pan = static_cast<double>(pbF) /
                                         static_cast<double>(kUf8FaderPbMax);
                            pan = pan * 2.0 - 1.0;
                            if (pan < -1.0) pan = -1.0;
                            if (pan >  1.0) pan =  1.0;
                            SetTrackSendInfo_Value(fr.track, fr.sendCategory,
                                                   fr.sendIndex, "D_PAN", pan);
                            g_panOverlayUntilMs[e.strip] = nowMs_() + kPanOverlayMs;
                            g_panOverlayText[e.strip]    =
                                composeValueLine("Pan", formatPanReadout(pan));
                        } else {
                            SetTrackSendInfo_Value(fr.track, fr.sendCategory,
                                                   fr.sendIndex, "D_VOL", e.value);
                        }
                        break;
                    }
                    if (fr.active()) break;   // routed but slot doesn't exist — eat the event
                }
                // FLIP: fader drives the focused plug-in parameter on this
                // strip's track instead of track volume. Read the raw
                // pb14 straight from the touch buffer — bypass the volume
                // calibration (generic params want a clean 0..1 sweep,
                // not REAPER's slider law) and divide by kUf8FaderPbMax
                // (15583, the actual hardware top — see fader-top probe
                // 2026-04-29) so norm reaches 1.0 at mechanical top.
                // 16383 here would cap norm at 0.951, leaving e.g. Pan
                // 5% short of full R.
                const auto focusedF = uf8::getFocusedParam();
                auto mmF = uf8::lookupPluginOnTrack(tr, focusedF.domain);
                const bool forcePanF = g_forcePan.load();
                const uf8::LinkSlot* slF = (!forcePanF && mmF.map)
                    ? uf8::findSlotByLinkIdx(*mmF.map, focusedF.slotIdx)
                    : nullptr;
                // Pan-focus is owned by Plugin/PAN buttons — don't let it
                // hijack the fader either (FLIP+Pan would conflict with
                // Plugin-fader mode's fader→CS-Fader routing).
                if (isVPotPanFocus(focusedF)) slF = nullptr;
                if (g_flip.load() && slF) {
                    const uint16_t pbF = linearVolumeToPb(e.value);
                    double normF = static_cast<double>(pbF) /
                                   static_cast<double>(kUf8FaderPbMax);
                    if (slF->inverted) normF = 1.0 - normF;
                    if (normF < 0.0) normF = 0.0;
                    if (normF > 1.0) normF = 1.0;
                    TrackFX_SetParamNormalized(tr, mmF.fxIndex,
                        slF->vst3Param, normF);
                    break;
                }
                // FLIP without slot, route, or Plugin-Fader mode: fader
                // writes track D_PAN. The default split is V-Pots = pan,
                // Faders = volume; FLIP swaps them, so faders take pan
                // duty. Earlier this only kicked in when forcePan was
                // also held — Frank 2026-05-08: just FLIP should be
                // enough, no PAN-button-modifier required.
                if (g_flip.load() && !g_pluginFaderMode.load()
                    && !g_uf8PluginMode.load()) {
                    const uint16_t pbF = linearVolumeToPb(e.value);
                    double n = static_cast<double>(pbF) /
                               static_cast<double>(kUf8FaderPbMax);
                    if (n < 0.0) n = 0.0;
                    if (n > 1.0) n = 1.0;
                    double pan = n * 2.0 - 1.0;
                    if (pan < -1.0) pan = -1.0;
                    if (pan >  1.0) pan =  1.0;
                    SetMediaTrackInfo_Value(tr, "D_PAN", pan);
                    break;
                }
                // Plugin-fader mode: route the fader to the SSL strip's
                // internal Fader Level param (vst3 index varies per
                // variant) instead of REAPER's post-FX track volume.
                // Same pb14/kUf8FaderPbMax mapping the FLIP path uses
                // — gives an even 0..1 sweep the plug-in expects.
                if (g_uf8PluginMode.load()) {
                    if (auto uctx = userStripCtxFocused_(); uctx.map) {
                        const int s = static_cast<int>(e.strip);
                        const auto& sb = uctx.map->uf8.strips[s];
                        if (sb.faderVst3Param >= 0) {
                            const uint16_t pbU = linearVolumeToPb(e.value);
                            double n = static_cast<double>(pbU) /
                                       static_cast<double>(kUf8FaderPbMax);
                            if (sb.faderInverted) n = 1.0 - n;
                            if (n < 0.0) n = 0.0;
                            if (n > 1.0) n = 1.0;
                            TrackFX_SetParamNormalized(uctx.tr, uctx.fxIdx,
                                sb.faderVst3Param, n);
                        }
                        break;
                    }
                }
                if (g_pluginFaderMode.load()) {
                    const auto cs = csFaderForTrack(tr);
                    if (cs.vst3Param >= 0) {
                        const uint16_t pbCs = linearVolumeToPb(e.value);
                        double n = static_cast<double>(pbCs) /
                                   static_cast<double>(kUf8FaderPbMax);
                        if (n < 0.0) n = 0.0;
                        if (n > 1.0) n = 1.0;
                        TrackFX_SetParamNormalized(tr, cs.fxIndex,
                            cs.vst3Param, n);
                        break;
                    }
                }
                // Normal: CSurf_OnVolumeChange applies the new position to
                // the track AND broadcasts to other surfaces. We do not
                // cache the value — motor echo reads GetTrackUIVolPan
                // on each tick so it always reflects whatever REAPER
                // actually has (including envelope playback).
                CSurf_OnVolumeChange(tr, e.value, false);
                break;
            }
            case PendingInput::PanDelta: {
                // Folder Mode reveal: any V-Pot rotation on a parent strip
                // briefly shows the real value before snapping back to
                // "Folder". Set unconditionally — the value-line resolver
                // gates on folder_mode + parent-track itself.
                if (e.strip < 8)
                    g_folderRevealUntilMs[e.strip] = nowMs_() + kFolderRevealMs;
                // Send/Receive routing on the V-Pot wins everything else.
                // Treat V-Pot detents as a volume scrub against the routed
                // level — same conversion the FLIP fader path uses below
                // so a single detent moves about 1/128 of the full sweep
                // (with shift = quarter-fine).
                {
                    // Pan routing decision tree:
                    //   1. Fader has the route (default mode) → V-Pot is
                    //      free, so pan ALWAYS writes the route's D_PAN
                    //      (PAN button irrelevant — fader+vpot already
                    //      naturally split into vol+pan).
                    //   2. V-Pot has the route → V-Pot defaults to D_VOL;
                    //      PAN button switches it to D_PAN.
                    //   3. No routing — fall through to plug-in param /
                    //      track pan logic below.
                    const StripRoute fr = resolveFaderRoute_(
                        e.strip, bankOffset, trackCount);
                    const StripRoute vr = resolveVpotRoute_(
                        e.strip, bankOffset, trackCount);
                    auto writePan = [&](const StripRoute& r, double dv, int strip) -> double {
                        const double cur = GetTrackSendInfo_Value(
                            r.track, r.sendCategory, r.sendIndex, "D_PAN");
                        const double next = uf8::applyVirtualNotch(
                            cur, dv, /*center*/0.0, /*zone*/0.025,
                            -1.0, 1.0);
                        SetTrackSendInfo_Value(r.track, r.sendCategory,
                                               r.sendIndex, "D_PAN", next);
                        g_panOverlayUntilMs[strip] = nowMs_() + kPanOverlayMs;
                        g_panOverlayText[strip]    =
                            composeValueLine("Pan", formatPanReadout(next));
                        return next;
                    };
                    auto writeRouteVolDelta = [](const StripRoute& r, double dv) {
                        const double curLin = GetTrackSendInfo_Value(
                            r.track, r.sendCategory, r.sendIndex, "D_VOL");
                        const uint16_t curPb = linearVolumeToPb(curLin);
                        double dPb = dv * 16383.0;
                        int newPb = static_cast<int>(std::round(
                            static_cast<double>(curPb) + dPb));
                        if (newPb < 0)     newPb = 0;
                        if (newPb > 16383) newPb = 16383;
                        const double newLin = pbToLinearVolume(
                            static_cast<uint16_t>(newPb));
                        SetTrackSendInfo_Value(r.track, r.sendCategory,
                                               r.sendIndex, "D_VOL", newLin);
                    };
                    if (fr.active()) {
                        if (fr.valid) {
                            double delta = e.value;
                            if (g_shiftHeld.load()) delta *= 0.25;
                            if (g_flip.load() && g_forcePan.load()) {
                                // FLIP+PAN held → V-Pot drives the strip
                                // track's own pan (P_PAN), not the send.
                                const double cur = GetMediaTrackInfo_Value(tr, "D_PAN");
                                const double next = uf8::applyVirtualNotch(
                                    cur, delta, /*center*/0.0, /*zone*/0.025,
                                    -1.0, 1.0);
                                SetMediaTrackInfo_Value(tr, "D_PAN", next);
                            } else if (g_flip.load()) {
                                writeRouteVolDelta(fr, delta);
                            } else {
                                writePan(fr, delta, e.strip);
                            }
                        }
                        break;   // active route consumes the event either way
                    }
                    if (vr.active()) {
                        if (vr.valid) {
                            if (g_forcePan.load()) {
                                double delta = e.value;
                                if (g_shiftHeld.load()) delta *= 0.25;
                                writePan(vr, delta, e.strip);
                            } else {
                                double dPb = e.value;
                                if (g_shiftHeld.load()) dPb *= 0.25;
                                writeRouteVolDelta(vr, dPb);
                            }
                        }
                        break;
                    }
                }
                // FX Learn UF8: when SSL Strip Mode is on AND the focused
                // track has a user-mapped plug-in, the V-Pot drives the
                // active bank's slot[strip] param on that focused
                // instance. Toggle slots ignore rotation. Empty slots
                // eat the event so the legacy CS/BC dispatch below
                // doesn't hijack a different track's plug-in.
                if (g_uf8PluginMode.load() && !g_flip.load()) {
                    if (auto uctx = userStripCtxFocused_(); uctx.map) {
                        const int s = static_cast<int>(e.strip);
                        const int bank = std::clamp(g_softKeyBank.load(),
                                                    0, 5);
                        const auto& bs =
                            uctx.map->uf8.banks.banks[bank][s];
                        if (bs.vst3Param >= 0
                            && bs.vpotMode == uf8::VPotMode::Value)
                        {
                            const double cur = TrackFX_GetParamNormalized(
                                uctx.tr, uctx.fxIdx, bs.vst3Param);
                            double delta = e.value
                                * (bs.inverted ? -1.0 : 1.0);
                            if (g_shiftHeld.load()) delta *= 0.25;
                            double next = cur + delta;
                            if (next < 0.0) next = 0.0;
                            if (next > 1.0) next = 1.0;
                            TrackFX_SetParamNormalized(uctx.tr,
                                uctx.fxIdx, bs.vst3Param, next);
                            break;
                        }
                        break;
                    }
                }
                // V-pot rotation: if the strip's track hosts a plug-in of
                // the focused domain (CS / BC) AND we're not in global Pan
                // mode, drive the focused parameter on that strip's track.
                // Otherwise fall back to track pan.
                //
                // FLIP exception: with a slot present, the V-Pot drives
                // track volume in pb14 space instead of the param (the
                // fader has taken over the param).
                const auto focused = uf8::getFocusedParam();
                // Synthetic toggles ignore rotation — push-only per user
                // instruction (no continuous value to scrub).
                if (focused.domain == uf8::Domain::ChannelStrip
                    && (focused.slotIdx == uf8::ext::TrackPhase
                        || focused.slotIdx == uf8::ext::PluginAB
                        || focused.slotIdx == uf8::ext::PluginHQ)) {
                    break;
                }
                auto mm = uf8::lookupPluginOnTrack(tr, focused.domain);
                const bool forcePan = g_forcePan.load();
                const uf8::LinkSlot* slPtr = (!forcePan && mm.map)
                    ? uf8::findSlotByLinkIdx(*mm.map, focused.slotIdx)
                    : nullptr;
                // Pan-focus ignored for V-Pot drive (Plugin/PAN buttons own
                // the Pan-mode tree). FLIP is also bypassed — flipping Pan
                // onto the fader would conflict with Plugin-fader mode.
                if (isVPotPanFocus(focused)) slPtr = nullptr;
                if (g_flip.load() && slPtr) {
                    // Map detent fraction (signed6/128) to pb14 delta —
                    // single detent ≈ 128 pb (1/128 of full sweep, same
                    // feel as the V-Pot driving the param). Fine quarters.
                    const uint16_t curPb = linearVolumeToPb(uiVolLinear(tr));
                    double dPb = e.value * 16383.0;
                    if (g_shiftHeld.load()) dPb *= 0.25;
                    int newPb = static_cast<int>(std::round(
                        static_cast<double>(curPb) + dPb));
                    if (newPb < 0) newPb = 0;
                    if (newPb > 16383) newPb = 16383;
                    const double newLin = pbToLinearVolume(
                        static_cast<uint16_t>(newPb));
                    CSurf_OnVolumeChange(tr, newLin, false);
                    break;
                }
                // FLIP+PAN no-slot swap: V-Pot writes track volume.
                // Same pb14-delta math as the FLIP+slot path above so
                // detent feel matches. Active when no plug-in slot is
                // present (otherwise FLIP+slot wins).
                if (g_flip.load() && forcePan) {
                    const uint16_t curPb = linearVolumeToPb(uiVolLinear(tr));
                    double dPb = e.value * 16383.0;
                    if (g_shiftHeld.load()) dPb *= 0.25;
                    int newPb = static_cast<int>(std::round(
                        static_cast<double>(curPb) + dPb));
                    if (newPb < 0) newPb = 0;
                    if (newPb > 16383) newPb = 16383;
                    CSurf_OnVolumeChange(tr,
                        pbToLinearVolume(static_cast<uint16_t>(newPb)), false);
                    break;
                }
                if (slPtr) {
                    const uf8::LinkSlot& sl = *slPtr;
                    const double cur = TrackFX_GetParamNormalized(tr, mm.fxIndex,
                                                                  sl.vst3Param);
                    // e.value is pan-scaled (≈1/128 per event). The
                    // earlier 4× multiplier (32-detent full sweep) was
                    // too coarse — values jumped past target. Drop to
                    // 1× for a 128-detent sweep, matching SSL's V-Pot
                    // feel in 360°. Fine mode (Shift) quarters.
                    double delta = e.value * (sl.inverted ? -1.0 : 1.0);
                    if (g_shiftHeld.load()) delta *= 0.25;
                    double next = cur + delta;
                    if (next < 0.0) next = 0.0;
                    if (next > 1.0) next = 1.0;
                    TrackFX_SetParamNormalized(tr, mm.fxIndex, sl.vst3Param, next);
                } else if (g_pluginFaderMode.load() && !forcePan) {
                    // Plugin mode + no focused slot → V-Pot drives the
                    // SSL strip's own Pan param (linkIdx 3) instead of
                    // REAPER track pan, so the plug-in remains the
                    // surface's source of truth for the strip's panorama.
                    const auto pn = csPanForTrack(tr);
                    if (pn.vst3Param >= 0) {
                        const double cur = TrackFX_GetParamNormalized(
                            tr, pn.fxIndex, pn.vst3Param);
                        double delta = e.value * 0.5;  // pan range 0..1, half-scale of REAPER's -1..+1
                        if (g_shiftHeld.load()) delta *= 0.25;
                        const double next = uf8::applyVirtualNotch(
                            cur, delta, /*center*/0.5, /*zone*/0.012,
                            0.0, 1.0);
                        TrackFX_SetParamNormalized(tr, pn.fxIndex,
                            pn.vst3Param, next);
                        break;
                    }
                    // Fall through to REAPER pan if no CS plug-in.
                    const double cur = GetMediaTrackInfo_Value(tr, "D_PAN");
                    const double next = uf8::applyVirtualNotch(
                        cur, e.value, /*center*/0.0, /*zone*/0.025,
                        -1.0, 1.0);
                    SetMediaTrackInfo_Value(tr, "D_PAN", next);
                } else {
                    const double cur = GetMediaTrackInfo_Value(tr, "D_PAN");
                    const double next = uf8::applyVirtualNotch(
                        cur, e.value, /*center*/0.0, /*zone*/0.025,
                        -1.0, 1.0);
                    SetMediaTrackInfo_Value(tr, "D_PAN", next);
                }
                break;
            }
            case PendingInput::PanCenter: {
                if (e.strip < 8)
                    g_folderRevealUntilMs[e.strip] = nowMs_() + kFolderRevealMs;
                // V-pot push: with a plug-in of the focused domain present
                // (and not in global Pan mode), reset the focused param to
                // its midpoint. Otherwise, re-center pan.
                //
                // FLIP exception: with a slot present, the V-Pot push
                // resets track volume to 0 dB (linear 1.0) — the V-Pot
                // is driving volume.
                {
                    // Push semantics mirror the rotate semantics:
                    //   * Fader has route → V-Pot push centers route pan.
                    //   * V-Pot has route + PAN → center route pan.
                    //   * V-Pot has route + no PAN → reset route vol to 0 dB.
                    const StripRoute fr = resolveFaderRoute_(
                        e.strip, bankOffset, trackCount);
                    const StripRoute vr = resolveVpotRoute_(
                        e.strip, bankOffset, trackCount);
                    if (fr.active()) {
                        if (fr.valid) {
                            if (g_flip.load() && g_forcePan.load()) {
                                SetMediaTrackInfo_Value(tr, "D_PAN", 0.0);
                            } else if (g_flip.load()) {
                                SetTrackSendInfo_Value(fr.track, fr.sendCategory,
                                                       fr.sendIndex, "D_VOL", 1.0);
                            } else {
                                SetTrackSendInfo_Value(fr.track, fr.sendCategory,
                                                       fr.sendIndex, "D_PAN", 0.0);
                                g_panOverlayUntilMs[e.strip] = nowMs_() + kPanOverlayMs;
                                g_panOverlayText[e.strip]    =
                                    composeValueLine("Pan", formatPanReadout(0.0));
                            }
                        }
                        break;
                    }
                    if (vr.active()) {
                        if (vr.valid) {
                            if (g_forcePan.load()) {
                                SetTrackSendInfo_Value(vr.track, vr.sendCategory,
                                                       vr.sendIndex, "D_PAN", 0.0);
                            } else {
                                SetTrackSendInfo_Value(vr.track, vr.sendCategory,
                                                       vr.sendIndex, "D_VOL", 1.0);
                            }
                        }
                        break;
                    }
                }
                // FX Learn UF8: V-Pot push on a user-bank slot.
                //   Toggle slot → flip 0↔1 on the bound param.
                //   Value slot  → reset to defaultNorm.
                //   Empty slot  → eat the event (don't fall through to
                //                 built-in CS dispatch, which would
                //                 hijack a different track).
                if (g_uf8PluginMode.load() && !g_flip.load()) {
                    if (auto uctx = userStripCtxFocused_(); uctx.map) {
                        const int s = static_cast<int>(e.strip);
                        const int bank = std::clamp(g_softKeyBank.load(),
                                                    0, 5);
                        const auto& bs =
                            uctx.map->uf8.banks.banks[bank][s];
                        if (bs.vst3Param >= 0) {
                            if (bs.vpotMode == uf8::VPotMode::Toggle) {
                                const double cur =
                                    TrackFX_GetParamNormalized(uctx.tr,
                                        uctx.fxIdx, bs.vst3Param);
                                TrackFX_SetParamNormalized(uctx.tr,
                                    uctx.fxIdx, bs.vst3Param,
                                    cur < 0.5 ? 1.0 : 0.0);
                            } else {
                                double dn = bs.defaultNorm;
                                if (dn < 0.0) dn = 0.0;
                                if (dn > 1.0) dn = 1.0;
                                TrackFX_SetParamNormalized(uctx.tr,
                                    uctx.fxIdx, bs.vst3Param, dn);
                            }
                        }
                        break;
                    }
                }
                const auto focused = uf8::getFocusedParam();
                // Synthetic toggles (Phase / A/B / HQ) are not VST3 params
                // — handled directly here on the strip's track. Push
                // toggles, rotation is ignored.
                if (focused.domain == uf8::Domain::ChannelStrip) {
                    if (focused.slotIdx == uf8::ext::TrackPhase) {
                        const double cur = GetMediaTrackInfo_Value(tr, "B_PHASE");
                        SetMediaTrackInfo_Value(tr, "B_PHASE", cur > 0.5 ? 0.0 : 1.0);
                        break;
                    }
                    if (focused.slotIdx == uf8::ext::PluginAB) {
                        uf8::togglePluginAB(tr);
                        break;
                    }
                    if (focused.slotIdx == uf8::ext::PluginHQ) {
                        uf8::togglePluginHQ(tr);
                        break;
                    }
                }
                auto mm = uf8::lookupPluginOnTrack(tr, focused.domain);
                const bool forcePan = g_forcePan.load();
                const uf8::LinkSlot* slPtr = (!forcePan && mm.map)
                    ? uf8::findSlotByLinkIdx(*mm.map, focused.slotIdx)
                    : nullptr;
                // Pan-focus ignored for V-Pot push (Plugin/PAN buttons own
                // the Pan-mode tree).
                if (isVPotPanFocus(focused)) slPtr = nullptr;
                if (g_flip.load() && slPtr) {
                    CSurf_OnVolumeChange(tr, 1.0, false);
                    break;
                }
                if (slPtr) {
                    if (isBinarySlot(*slPtr)) {
                        // V-Pot push cycles to next discrete step. For a
                        // 2-state toggle (EQ In, Dyn In, S/C Listen) this
                        // collapses to 0↔1. For a multi-step enumeration
                        // (4K G EQ Colour: Black/Pink — VST3 reports 2
                        // steps but plain 0↔1 was hitting an unused 3rd
                        // value, hence the user's "3 push" complaint),
                        // step-size cycling lands on each defined value.
                        double step = 0.0, smallstep = 0.0, largestep = 0.0;
                        bool istoggle = false;
                        const bool haveSteps = TrackFX_GetParameterStepSizes(
                            tr, mm.fxIndex, slPtr->vst3Param,
                            &step, &smallstep, &largestep, &istoggle);
                        const double cur = TrackFX_GetParamNormalized(
                            tr, mm.fxIndex, slPtr->vst3Param);
                        double next;
                        if (!haveSteps || istoggle || step <= 0.0 || step >= 1.0) {
                            next = (cur < 0.5) ? 1.0 : 0.0;
                        } else {
                            next = cur + step;
                            if (next > 1.0 + step * 0.5) next = 0.0;
                            if (next > 1.0) next = 1.0;
                        }
                        TrackFX_SetParamNormalized(tr, mm.fxIndex,
                            slPtr->vst3Param, next);
                    } else {
                        const double resetVal = slPtr->deflt.value_or(0.5);
                        TrackFX_SetParamNormalized(tr, mm.fxIndex,
                            slPtr->vst3Param, resetVal);
                    }
                } else if (g_pluginFaderMode.load() && !forcePan) {
                    // Plugin mode → reset SSL strip's own Pan to centre
                    // (norm 0.5 = C). forcePan overrides this so PAN
                    // button always resets REAPER track pan instead.
                    const auto pn = csPanForTrack(tr);
                    if (pn.vst3Param >= 0) {
                        TrackFX_SetParamNormalized(tr, pn.fxIndex,
                            pn.vst3Param, 0.5);
                    } else {
                        SetMediaTrackInfo_Value(tr, "D_PAN", 0.0);
                    }
                } else {
                    SetMediaTrackInfo_Value(tr, "D_PAN", 0.0);
                }
                break;
            }

            // ---- Selection-Mode per-strip events --------------------------
            case PendingInput::RecArmToggle: {
                // REC mode SEL push: toggle I_RECARM via the canonical
                // CSurf path. SetMediaTrackInfo_Value direct triggers
                // REAPER's "auto-select armed tracks" preference (if
                // on) which fires SetSurfaceSelected → followSelected
                // InMixer → bank jumps to the armed track. CSurf_On
                // RecArmChange skips that broadcast, matching how the
                // user clicking the TCP arm button behaves (Frank
                // 2026-05-14: "wenn nicht auf bank 1 hüpft er nach
                // rec-arm sofort zurück zu bank 1").
                CSurf_OnRecArmChange(tr, -1);
                break;
            }
            case PendingInput::RecArmMonToggle: {
                // REC+MON mode SEL push: arm + monitor flip together.
                // Same CSurf_OnRecArmChange call (toggle) for the arm
                // half; monitor follows the new arm state — set
                // I_RECMON=1 when arming, =0 when disarming. We toggle
                // via CSurf_OnInputMonitorChange to stay surface-aware
                // and avoid the SetMediaTrackInfo direct-write path.
                const double prevArm =
                    GetMediaTrackInfo_Value(tr, "I_RECARM");
                CSurf_OnRecArmChange(tr, -1);
                const bool nowArmed = prevArm < 0.5; // post-toggle state
                CSurf_OnInputMonitorChange(tr, nowArmed ? 1 : 0);
                break;
            }
            case PendingInput::AutoModeStep: {
                if (e.strip < 8)
                    g_folderRevealUntilMs[e.strip] = nowMs_() + kFolderRevealMs;
                // AUTO mode SEL push: cycle auto-mode 0..5 with
                // wraparound, ledColourFor and the scribble strip's
                // value line update on the next render tick.
                const int cur  = GetTrackAutomationMode(tr);
                const int next = (cur + 1) % 6;
                SetTrackAutomationMode(tr, next);
                break;
            }
            case PendingInput::AutoModeSet: {
                if (e.strip < 8)
                    g_folderRevealUntilMs[e.strip] = nowMs_() + kFolderRevealMs;
                // AUTO mode V-Pot push: snap the strip's auto-mode to
                // the value carried in e.value (0 = Trim/Read by
                // default). Frank 2026-05-14 "v-pot push soll auf
                // trim/read schalten".
                int mode = static_cast<int>(e.value);
                if (mode < 0) mode = 0;
                if (mode > 5) mode = 5;
                SetTrackAutomationMode(tr, mode);
                break;
            }
            case PendingInput::AutoModeDelta: {
                if (e.strip < 8)
                    g_folderRevealUntilMs[e.strip] = nowMs_() + kFolderRevealMs;
                // AUTO mode V-Pot rotation: accumulate detents, step
                // strip's auto-mode ±1 within [0..5]. Per-strip accum
                // so two V-Pots scrolling at once don't fight.
                const uint8_t s = e.strip < 8 ? e.strip : 0;
                g_autoModeAccum[s] += e.value / kAutoModeRotateScale;
                int step = 0;
                if (g_autoModeAccum[s] >=  1.0) {
                    step = static_cast<int>(g_autoModeAccum[s]);
                    g_autoModeAccum[s] -= step;
                }
                if (g_autoModeAccum[s] <= -1.0) {
                    step = static_cast<int>(g_autoModeAccum[s]);
                    g_autoModeAccum[s] -= step;
                }
                if (step != 0) {
                    int cur = GetTrackAutomationMode(tr);
                    int next = cur + step;
                    if (next < 0) next = 0;
                    if (next > 5) next = 5;
                    if (next != cur) SetTrackAutomationMode(tr, next);
                }
                break;
            }
            case PendingInput::StripInstanceDelta: {
                if (e.strip < 8)
                    g_folderRevealUntilMs[e.strip] = nowMs_() + kFolderRevealMs;
                // Instance mode V-Pot rotation: cycle the strip's
                // track's FX list per-strip, no focus / selection
                // changes (Frank 2026-05-14 "ohne track selection,
                // einfach den track des v-pots verändern"). Walks all
                // FX on the track, wraps. Per-strip accumulator.
                const uint8_t s = e.strip < 8 ? e.strip : 0;
                g_stripInstanceAccum[s] += e.value / kChannelEncoderScale;
                int step = 0;
                if (g_stripInstanceAccum[s] >=  1.0) {
                    step = static_cast<int>(g_stripInstanceAccum[s]);
                    g_stripInstanceAccum[s] -= step;
                }
                if (g_stripInstanceAccum[s] <= -1.0) {
                    step = static_cast<int>(g_stripInstanceAccum[s]);
                    g_stripInstanceAccum[s] -= step;
                }
                if (step != 0) {
                    const int n = TrackFX_GetCount(tr);
                    if (n > 0) {
                        const int cur = stripInstanceActiveFx_(tr);
                        int next = ((cur + step) % n + n) % n;
                        g_stripInstanceFxIdx[tr] = next;
                        g_bankDirty.store(true);   // refresh scribble strip
                        // If this strip owns the open FX-Cycle GUI,
                        // re-point the window to the new active FX so
                        // the user sees the cycle in the floating
                        // editor too. Strips that haven't been pushed
                        // own no GUI → no-op.
                        if (g_instanceGuiOwnerStrip.load() == s) {
                            g_pluginGuiSyncRequest.store(true);
                        }
                    }
                }
                break;
            }
            case PendingInput::StripInstanceOpen: {
                if (e.strip < 8)
                    g_folderRevealUntilMs[e.strip] = nowMs_() + kFolderRevealMs;
                // Instance mode V-Pot push: toggle the FX window for
                // this strip's active instance. Tracks ownership so
                // the with-GUI rotation handler knows which strip's
                // cycle to follow. The unified GUI sync drain at
                // tickFollowSelected_ (g_pluginGuiSyncRequest exchange)
                // does the show/hide; we just flip ownership here.
                const int s = static_cast<int>(e.strip);
                if (g_instanceGuiOwnerStrip.load() == s) {
                    g_instanceGuiOwnerStrip.store(-1);   // toggle off
                } else {
                    g_instanceGuiOwnerStrip.store(s);    // claim
                }
                g_pluginGuiSyncRequest.store(true);
                break;
            }
        }
    }
}

// UF8 fader calibration. Two layers:
//
//   1. Hardware facts (probed 2026-04-29): the fader emits pb14 ≈ 15583
//      at mechanical top, not the protocol max 16383 — there's a ~5%
//      deadband at the top of travel.
//
//   2. Curve mismatch: REAPER's default slider law is not the same as
//      UF8's printed scale. Even after stretching (1) to put +12 at
//      mechanical top, intermediate marks land 2–14 dB hot (e.g. UF8 "0"
//      read +4.07, UF8 "-30" read -21). The user's calibration table
//      below maps from REAPER-current-reading → UF8-printed dB so the
//      printed marks line up with REAPER's volume display.
constexpr double   kUf8FaderTopDb  = 12.0;
// kUf8FaderPbMax is forward-declared earlier (used by drainInputQueue).

// Calibration sample: with kUf8FaderTopDb=12 and kUf8FaderPbMax=15583
// the bare slider-law mapping placed each UF8 mark at this much in
// REAPER. We piecewise-linear interpolate to push the printed marks
// onto their stated dB values.
//
// Sorted descending in current_db so the lookup walks from the top.
struct FaderCalPoint { double current_db; double target_db; };
constexpr FaderCalPoint kFaderCal[] = {
    {  +12.00,  +12.0 },   // mechanical top (slider already at top)
    {   +8.40,   +6.0 },
    {   +4.07,    0.0 },
    {   +0.74,   -5.0 },
    {   -5.65,  -10.0 },
    {  -13.70,  -20.0 },
    {  -21.00,  -30.0 },
    {  -32.00,  -40.0 },
    {  -46.00,  -60.0 },
    { -150.00, -150.0 },   // silence floor
};

// Monotonic cubic Hermite (Fritsch-Carlson) interpolation through the
// calibration points. Piecewise-linear was producing visible jerks at
// the table joints when the user pulled the fader smoothly — adjacent
// segments had slope ratios up to 2× (e.g. 1.50 → 0.78 around 0 dB),
// so REAPER's volume changed velocity abruptly at every joint. PCHIP
// keeps the curve C1-continuous AND monotone (no overshoot), so the
// fader feels smooth while still hitting every measured point exactly.
constexpr size_t kFaderCalN = std::size(kFaderCal);

struct FaderCalTangents { double dy[kFaderCalN]; };

static FaderCalTangents computeFaderCalTangents_(bool forward)
{
    double x[kFaderCalN], y[kFaderCalN];
    for (size_t i = 0; i < kFaderCalN; ++i) {
        x[i] = forward ? kFaderCal[i].current_db : kFaderCal[i].target_db;
        y[i] = forward ? kFaderCal[i].target_db  : kFaderCal[i].current_db;
    }
    // Table is descending in both columns, so h[i] is negative and m[i]
    // is positive (negative/negative). Fritsch-Carlson math is sign-
    // agnostic — only the products and harmonic mean structure matter.
    double h[kFaderCalN - 1], m[kFaderCalN - 1];
    for (size_t i = 0; i + 1 < kFaderCalN; ++i) {
        h[i] = x[i + 1] - x[i];
        m[i] = (y[i + 1] - y[i]) / h[i];
    }
    FaderCalTangents r{};
    r.dy[0]                = m[0];
    r.dy[kFaderCalN - 1]   = m[kFaderCalN - 2];
    for (size_t i = 1; i + 1 < kFaderCalN; ++i) {
        if (m[i - 1] * m[i] <= 0.0) {
            r.dy[i] = 0.0;                      // flat at extrema
        } else {
            const double w1 = 2.0 * h[i] + h[i - 1];
            const double w2 = h[i] + 2.0 * h[i - 1];
            r.dy[i] = (w1 + w2) / (w1 / m[i - 1] + w2 / m[i]);
        }
    }
    return r;
}

// Diagnostic toggle — when off, interpFaderCal is a pass-through. Used
// to A/B whether the calibration math is contributing to the perceived
// fader-not-smooth symptom. Toggled via the REASIXTY_TOGGLE_FADER_CAL
// custom action. Default: enabled.
std::atomic<bool> g_faderCalEnabled{true};

double interpFaderCal(double x, bool current_to_target)
{
    if (!g_faderCalEnabled.load()) return x;

    static const FaderCalTangents fwdT = computeFaderCalTangents_(true);
    static const FaderCalTangents invT = computeFaderCalTangents_(false);
    const auto& tang = current_to_target ? fwdT : invT;

    auto getX = [&](size_t i) {
        return current_to_target
            ? kFaderCal[i].current_db
            : kFaderCal[i].target_db;
    };
    auto getY = [&](size_t i) {
        return current_to_target
            ? kFaderCal[i].target_db
            : kFaderCal[i].current_db;
    };

    if (x >= getX(0))                 return getY(0);
    if (x <= getX(kFaderCalN - 1))    return getY(kFaderCalN - 1);

    for (size_t i = 1; i < kFaderCalN; ++i) {
        if (x >= getX(i)) {
            const size_t  k   = i - 1;
            const double  xk  = getX(k),  xk1 = getX(i);
            const double  yk  = getY(k),  yk1 = getY(i);
            const double  h   = xk1 - xk;
            const double  t   = (x - xk) / h;
            const double  t2  = t * t;
            const double  t3  = t2 * t;
            const double  h00 =  2.0 * t3 - 3.0 * t2 + 1.0;
            const double  h10 =        t3 - 2.0 * t2 + t;
            const double  h01 = -2.0 * t3 + 3.0 * t2;
            const double  h11 =        t3 -       t2;
            return h00 * yk  + h10 * h * tang.dy[k]
                 + h01 * yk1 + h11 * h * tang.dy[i];
        }
    }
    return getY(kFaderCalN - 1);
}

// Convert a 14-bit pb value to linear REAPER volume. Two stages:
//   pb14 → REAPER's slider-law dB (the "raw" reading) → calibrated dB.
double pbToLinearVolume(uint16_t pb14)
{
    if (pb14 == 0) return 0.0;
    if (pb14 > kUf8FaderPbMax) pb14 = kUf8FaderPbMax;
    const double topSlider = DB2SLIDER(kUf8FaderTopDb);
    const double slider    = static_cast<double>(pb14) /
                             static_cast<double>(kUf8FaderPbMax) * topSlider;
    const double db_raw    = SLIDER2DB(slider);
    const double db_cal    = interpFaderCal(db_raw, /*current_to_target=*/true);
    return std::pow(10.0, db_cal / 20.0);
}

// Inverse of pbToLinearVolume. Used to echo REAPER volume back onto the
// UF8 motor so the physical fader follows mouse edits in the DAW.
uint16_t linearVolumeToPb(double linear)
{
    if (!(linear > 0.0)) return 0;                  // catches 0 and NaN
    const double db_target = 20.0 * std::log10(linear);
    const double db_raw    = interpFaderCal(db_target, /*current_to_target=*/false);
    const double slider    = DB2SLIDER(db_raw);
    const double topSlider = DB2SLIDER(kUf8FaderTopDb);
    if (!(topSlider > 0.0)) return 0;
    double pb = slider / topSlider * static_cast<double>(kUf8FaderPbMax);
    if (pb < 0)                                pb = 0;
    if (pb > static_cast<double>(kUf8FaderPbMax)) pb = kUf8FaderPbMax;
    return static_cast<uint16_t>(pb + 0.5);
}

// Parse a UF8 vendor-USB IN report and, when it's an input event, convert
// to the equivalent MCU MIDI message and push it back up to REAPER via the
// virtual MIDI source. The UF8 firmware packs events into this EP 0x81 IN
// stream, inter-mingled with its continuous polling heartbeat (which we
// just skip).
//
// Observed event framing — all starting from a position past the 31 60
// session prefix (stripped here if present):
//
//   FF 21 03 <strip> <A> <B> CKSUM    — fader position (16-bit abs.)
//   FF 22 03 <id>    00 <s> CKSUM     — button press/release
//   FF 23 02 <strip> <s>    CKSUM     — fader touch
//   FF 24 02 <strip> <d>    CKSUM     — v-pot rotation (delta)
//   FF 33 02 <strip> <s>    CKSUM     — v-pot push toggle (tbc)
//
// Plus the "FF 04 02 9d 01 a4" and partner "FF 04 02 94 01 9b" polling
// packets which repeat at ~100 Hz — we ignore those.
// Per-strip touch state with debounce. The UF8 touch sensor bounces
// during a single sustained touch (~9 press+release pairs per second
// observed in captures). Emitting every one of those as MCU note-on/off
// would make REAPER's automation engine stutter (e.g. Touch mode would
// release-then-reacquire constantly). Instead:
//   - On any touch-press IN event: emit MCU Note-on immediately (if not
//     already "reported as touched"). Update last-press timestamp.
//   - On touch-release IN event: do NOTHING immediately.
//   - The REAPER timer (30 Hz) emits Note-off when (now - lastPress)
//     exceeds the hold threshold, which means the user really let go.
// Touch-state is debounced because the UF8 capacitive sensor bounces
// (~9 press+release pairs per second during a sustained touch — confirmed
// in captures). Press events commit immediately so the motor releases
// right as the user grabs the fader; release events are deferred until
// the sensor has stayed quiet for kTouchDebounceQuiet — if any press
// arrives during that window the release is cancelled.
std::array<std::atomic<bool>, 8>                       g_touchReported{};
std::array<std::atomic<bool>, 8>                       g_touchReleasePending{};
std::array<std::chrono::steady_clock::time_point, 8>   g_touchLastPress{};
constexpr auto kTouchDebounceQuiet = std::chrono::milliseconds(150);

bool ReaSixtySurface::GetTouchState(MediaTrack* tr, int isPan)
{
    if (isPan != 0) return false;   // fader touch only
    for (int s = 0; s < 8; ++s) {
        if (!g_touchReported[s].load()) continue;
        if (g_slotTrack[s] == tr) return true;
    }
    return false;
}

// Button LEDs — coloured path (cap31, 2026-04-26).
//   FF 38 04 <cell> 00 <a> <b> CKSUM   +
//   FF 39 04 <cell> 00 <a> <b> CKSUM
// Cell formula: cell = 0x17 - 3*strip - led_offset, with led_offset 0=SOLO,
// 1=CUT, 2=SEL. Strip 0 (leftmost UF8 strip) → cells 0x15..0x17, strip 7 →
// 0x00..0x02. Matches the FF 3B id map — the legacy mono-on/off path lives
// in the same id space — but unlike FF 3B this pair sets the LED to its
// proper colour: SOLO yellow, CUT orange, SEL white.
//
// REC ARM has no colour-pair mapping yet; left on the FF 3B path and gated
// off until a cap23b verifies its id range.
enum class LedClass : uint8_t { Sel = 0, Mute = 1, Solo = 2, Arm = 3 };

// Resolve the LED colour for a given class on a given track. SEL pulls from
// REAPER's track-colour (snapped to SSL360's DAW-Colour palette); SOLO and
// CUT use class defaults (yellow / red). When the user later wires a
// settings UI for per-class overrides, this is the spot to read them.
uf8::LedColour ledColourFor(LedClass cls, MediaTrack* tr)
{
    if (cls == LedClass::Sel && tr) {
        const auto mode  = g_selectionMode.load();
        const bool armed = GetMediaTrackInfo_Value(tr, "I_RECARM") > 0.5;
        // Selection-Mode overrides for the SEL palette:
        //   REC      — red dim/bright per I_RECARM (selection bit
        //              invisible in this mode by design).
        //   AUTO     — colour per track automation mode.
        //   Instance — fall through to Norm (track colour). Instance
        //              keeps the SEL row readable so the user still
        //              sees track selection while cycling FX windows.
        //   Norm     — legacy: rec-armed-red, then SEL-follows-colour.
        switch (mode) {
            case SelectionMode::Rec:
            case SelectionMode::RecMon:
                return armed ? uf8::ledColourRedBrightSolid()
                             : uf8::ledColourRedDimSolid();
            case SelectionMode::Auto:
                return uf8::ledColourForAutoMode(
                    GetTrackAutomationMode(tr));
            case SelectionMode::Norm:
            case SelectionMode::Instance:
            default:
                break;
        }
        if (armed) {
            return uf8::ledColourRed();
        }
        if (!g_selFollowsColor.load()) {
            return uf8::ledColourWhite();
        }
        const uint32_t rgb = static_cast<uint32_t>(GetTrackColor(tr)) & 0x00FFFFFFu;
        return uf8::ledColourForTrackRgb(rgb);
    }
    switch (cls) {
        case LedClass::Solo: return uf8::ledColourYellow();
        case LedClass::Mute: return uf8::ledColourRed();
        case LedClass::Sel:  return uf8::ledColourWhite();
        default:             return uf8::ledColourWhite();
    }
}

uf8::LedClass toUf8LedClass(LedClass cls)
{
    switch (cls) {
        case LedClass::Solo: return uf8::LedClass::Solo;
        case LedClass::Mute: return uf8::LedClass::Cut;
        case LedClass::Sel:  return uf8::LedClass::Sel;
        default:             return uf8::LedClass::Sel;
    }
}

void sendLedFrames(uf8::LedColourFrames frames)
{
    if (!g_dev) return;
    if (!frames.ff38.empty())   g_dev->send(std::move(frames.ff38));
    if (!frames.ff39.empty())   g_dev->send(std::move(frames.ff39));
    if (!frames.legacy.empty()) g_dev->send(std::move(frames.legacy));
}

// Forward declarations for helpers defined further down (used by
// sendSelRenderTrigger to restore the AutoTrim LED after the cap33
// trigger sequence, and by drainInputQueue's FLIP path which routes
// fader pb14 → focused-param normalised position).
void pushAutoModeLedsMixed_(int perTrackMode, int globalMode,
                            int activeLayer);
void pushLayerLeds(int active);
uint16_t linearVolumeToPb(double linear);
// Folds the active layer's per-binding LED colour into a global-LED push.
// Defined alongside pushUf8GlobalLeds — see comment there.
void sendUf8GlobalLed(uf8::Uf8GlobalLed cell, bool on);
void sendUf8GlobalLed(uf8::Uf8GlobalLed cell, uf8::GlobalLedState state);
// Drop a single cell's dedup entry so the next sendUf8GlobalLed for it
// writes through unconditionally. Needed when raw frames bypass the
// cache and the next state-assertion would otherwise be skipped.
void invalidateGlobalLedCell_(uf8::Uf8GlobalLed cell);
// True iff any modifier-slot in the binding has an active stateful
// action (toggle on, REAPER GetToggleCommandState2 == 1).
bool bindingHasActiveSlot_(const uf8::bindings::Binding& bd);
extern int g_lastAutoMode;

// Cell 0x24 sits outside the per-strip LED range. cap33 shows SSL360
// firing a fixed off→on toggle on this cell at every selection event,
// always before the selected-strip bitmask. Replicate the exact 4-frame
// sequence — values are constant (no track-colour involved).
//
// Quirk: cell 0x24 ALSO happens to be the AutoTrim LED in MCU mode
// (`0x3F 0xF0` = AutoTrim's bright-orange). cap33 was recorded with the
// captured track in REAPER mode 0 (Trim/Read), so SSL360 lighting the
// AutoTrim LED looked indistinguishable from a "render trigger" pulse.
// In our extension, this fires on every SEL change regardless of auto
// mode, leaving TRIM pinned on next to whatever auto-LED is the actual
// active mode. Re-assert the correct AutoTrim state at the end of the
// sequence so the user sees only the LED that matches the track's mode.
void sendSelRenderTrigger()
{
    if (!g_dev) return;
    g_dev->send({0xFF, 0x38, 0x04, 0x24, 0x00, 0x12, 0xF0, 0x62});
    g_dev->send({0xFF, 0x39, 0x04, 0x24, 0x00, 0x12, 0xF0, 0x63});
    g_dev->send({0xFF, 0x38, 0x04, 0x24, 0x00, 0x3F, 0xF0, 0x8F});
    g_dev->send({0xFF, 0x39, 0x04, 0x24, 0x00, 0x00, 0xF0, 0x51});
    // The 4 raw frames above leave cell 0x24 in a bright-ish leftover
    // state (a=0x3F 0xF0, b=0x00 0xF0). The dedup cache in
    // sendUf8GlobalLed is unaware of those raw writes and still thinks
    // AutoTrim is at whatever state we last *wrote through it* — so a
    // plain re-assertion gets dedup'd out and the bright leftover sticks
    // on the LED. Invalidate the cache entry first, then force Off.
    invalidateGlobalLedCell_(uf8::Uf8GlobalLed::AutoTrim);
    sendUf8GlobalLed(uf8::Uf8GlobalLed::AutoTrim,
                     uf8::GlobalLedState::Off);
}

// Push the selected-strip bitmask. cap33: SSL360 sends this after the
// cell 0x24 toggle, on every selection change.
void pushSelectedStripBitmask()
{
    if (!g_dev) return;
    uint16_t mask = 0;
    for (int s = 0; s < 8; ++s) {
        MediaTrack* t = g_slotTrack[s];
        if (t && GetMediaTrackInfo_Value(t, "I_SELECTED") > 0.5) {
            mask |= static_cast<uint16_t>(1u << s);
        }
    }
    g_dev->send(uf8::buildSelectedStripBitmask(mask));
}

void sendLed(LedClass cls, MediaTrack* tr, bool on)
{
    if (!g_dev) return;
    if (cls == LedClass::Arm) return;   // gate ARM until its colour-pair is captured
    // FX Learn UF8: in user-strip mode, Solo / Cut / Sel LEDs are driven
    // by the per-tick poll in pushZonesForVisibleSlots from user-binding
    // state — let it own the LED so REAPER's track-state callbacks don't
    // overwrite it. Other LED classes (e.g. focused-FX) keep flowing.
    if (g_uf8PluginMode.load()
        && (cls == LedClass::Solo
         || cls == LedClass::Mute
         || cls == LedClass::Sel))
    {
        if (auto u = userStripCtxFocused_(); u.map) return;
    }
    for (int s = 0; s < 8; ++s) {
        if (g_slotTrack[s] != tr) continue;
        const uf8::LedClass devCls = toUf8LedClass(cls);
        const uf8::LedColour col = ledColourFor(cls, tr);
        sendLedFrames(uf8::buildLedColourPair(static_cast<uint8_t>(s), devCls, on, col));
        if (cls == LedClass::Sel) {
            sendSelRenderTrigger();
            pushSelectedStripBitmask();
        }
        return;
    }
}

void ReaSixtySurface::SetSurfaceSolo(MediaTrack* tr, bool solo)
{
    sendLed(LedClass::Solo, tr, solo);
    // UC1's Solo / Solo Clear LEDs track REAPER state. Solo Clear
    // reflects "any track soloed anywhere", so every SetSurfaceSolo
    // callback has to refresh regardless of which track fired it.
    (void)solo;
    if (g_uc1_surface) g_uc1_surface->refresh();
}
void ReaSixtySurface::SetSurfaceMute(MediaTrack* tr, bool mute)
{
    sendLed(LedClass::Mute, tr, mute);
    // Only the focused track's Cut LED matters on UC1 — skip refresh
    // when REAPER reports a different track's mute change.
    (void)mute;
    if (g_uc1_surface && tr && g_uc1_surface->focusedTrack() == tr) {
        g_uc1_surface->refresh();
    }
}
void ReaSixtySurface::SetSurfaceSelected(MediaTrack* tr, bool sel)
{
    sendLed(LedClass::Sel, tr, sel);
    // UC1 always follows whichever track just became selected. Ignoring
    // the sel=false side means we keep the last-focused track driving
    // UC1 until a new one is picked — matches SSL 360°'s Focus Mode.
    if (sel && g_uc1_surface) {
        g_uc1_surface->setFocusedTrack(tr);
        // When the "with GUI" variant of SSL Strip Mode is active,
        // follow the track selection so the floating window switches
        // to the new track's CS plug-in. Plain ssl_strip_mode_toggle
        // (no GUI) leaves g_pluginFaderModeWithGui false → no GUI
        // churn (Frank 2026-05-12 "GUI bei select eines anderen
        // channels auf den neu gewählten wechseln"). Use the
        // with-Gui flag rather than the g_csGuiShownTr pointer
        // because the latter clears whenever the drain closes a GUI
        // (e.g. user selected a track with no CS plug-in) — leaving
        // the follow logic permanently disarmed for the rest of the
        // session (Frank 2026-05-14).
        if (g_pluginFaderMode.load() && g_pluginFaderModeWithGui.load()) {
            g_pluginGuiSyncRequest.store(true);
        }
    }
    // UF8 bank follows REAPER selection. Any selection change — clicks
    // in the TCP/MCP, ReaScript, another surface — rebanks so the
    // active track is visible on the UF8. Uses whichever FollowMode is
    // set globally (BucketSnap by default; LeftmostStrip is the
    // planned settings toggle). Only the sel=true edge triggers; a
    // deselect shouldn't move the view.
    if (sel) followSelectedInMixer(tr);
}
void ReaSixtySurface::SetSurfaceRecArm(MediaTrack* tr, bool arm)
{
    // Rec-arm doesn't have its own dedicated LED on the UF8 — SSL360
    // repaints the SEL LED in red when the track is armed and back to
    // track-colour/white when disarmed. Push a SEL refresh so the
    // colour switches even if I_SELECTED didn't change.
    sendLed(LedClass::Sel, tr, GetMediaTrackInfo_Value(tr, "I_SELECTED") > 0.5);
    (void)arm;
}

// Device lifecycle: the surface instance owns the UF8 connection and the
// timer registration. REAPER creates the instance when the user adds
// "Rea-Sixty" in Control Surface settings, and destroys it on removal or
// on shutdown.
void onTimer();
void onUf8Input(const uint8_t* data, size_t len);
void faderInputLog_(const char* kind, int strip, int pb14, int prevPb,
                    int delta, const char* decision);
void onMidiFromReaper(std::span<const uint8_t> bytes);

ReaSixtySurface::ReaSixtySurface()
{
    // Open the virtual MIDI ports first — harmless if unused, and keeps
    // the legacy MCU-SysEx scribble pipe available for anyone still
    // running CSI alongside us.
    g_midi = std::make_unique<uf8::MidiBridge>();
    if (g_midi->open("reaper_uf8")) {
        g_midi->setIncomingHandler(onMidiFromReaper);
        // Also try to open the UF8's own OS-level MCU MIDI port so we
        // can drive the per-strip LEDs via MCU note-on/off. Silently
        // no-ops if no matching destination exists (e.g. SSL 360° not
        // installed and the UF8 not exposed as native MIDI) — LED
        // feedback just stays dark in that case.
        g_midi->openUf8Output();
    }

    // UF8 open — optional now. Either UF8, UC1, or both may be on the bus
    // during any given session. Previously the surface bailed out if UF8
    // couldn't be opened, which meant a UC1-only setup never reached the
    // UC1 init below.
    g_dev = std::make_unique<uf8::UF8Device>();
    const bool uf8Opened = g_dev->open();
    if (!uf8Opened) {
        ShowConsoleMsg(("Rea-Sixty UF8: " + g_dev->lastError()
                        + "  (UF8 optional — continuing)\n").c_str());
        g_dev.reset();
    } else {
        g_sync = std::make_unique<uf8::ColorSync>(*g_dev);
        g_sync->invalidate();
        g_dev->setRawInputHandler(onUf8Input);

        // UF8 firmware powers up with every SEL/MUTE/SOLO LED lit; we want
        // them all dark until REAPER state says otherwise. Push the OFF
        // colour-pair for every per-strip LED at open time so the initial
        // display matches an idle REAPER session.
        for (uint8_t s = 0; s < 8; ++s) {
            sendLedFrames(uf8::buildLedColourPair(s, uf8::LedClass::Solo, false));
            sendLedFrames(uf8::buildLedColourPair(s, uf8::LedClass::Cut,  false));
            sendLedFrames(uf8::buildLedColourPair(s, uf8::LedClass::Sel,  false));
        }

        // Force the bank-change re-sync block to fire on the very first
        // timer tick so LED state, slot caches and colors all reflect
        // REAPER's actual state from the get-go (rather than whatever stale
        // values the surface booted with).
        g_bankDirty.store(true);

        // Zero the fader positions captured in the layer-replay blob.
        const uint16_t pb0dB = linearVolumeToPb(1.0);
        const uint8_t  lsb   = static_cast<uint8_t>(pb0dB & 0x7F);
        const uint8_t  msb   = static_cast<uint8_t>((pb0dB >> 7) & 0x7F);
        for (uint8_t s = 0; s < 8; ++s) {
            g_dev->send(uf8::buildFaderPosition(s, lsb, msb));
        }
    }

    // Best-effort UC1 attach — absence is fine, UF8 runs standalone.
    g_uc1_dev = std::make_unique<uc1::UC1Device>();
    if (g_uc1_dev->open()) {
        g_uc1_surface = std::make_unique<uc1::UC1Surface>();
        g_uc1_surface->attach(*g_uc1_dev);
        // Focus whatever track REAPER currently considers "last touched"
        // so UC1 has something meaningful to drive from the first click.
        if (auto* tr = GetLastTouchedTrack()) {
            g_uc1_surface->setFocusedTrack(tr);
        }
    } else {
        ShowConsoleMsg(("Rea-Sixty UC1: " + g_uc1_dev->lastError()
                        + "  (UC1 optional — UF8 continues)\n").c_str());
        g_uc1_dev.reset();
    }

    plugin_register("timer", reinterpret_cast<void*>(onTimer));

    // Push the persisted brightness level to both devices now that
    // they're open. If no ExtState yet (first-run), defaults to "full".
    loadBrightness();
    applyBrightness();
}

ReaSixtySurface::~ReaSixtySurface()
{
    plugin_register("-timer", reinterpret_cast<void*>(onTimer));
    g_sync.reset();
    if (g_midi) g_midi->close();
    g_midi.reset();
    if (g_dev) g_dev->close();
    g_dev.reset();
    g_uc1_surface.reset();
    if (g_uc1_dev) g_uc1_dev->close();
    g_uc1_dev.reset();
    g_slotTrack.fill(nullptr);
}

IReaperControlSurface* createReaSixty(const char* /*type*/, const char* /*config*/,
                                      int* /*errStats*/)
{
    return new ReaSixtySurface();
}

HWND reaSixtyShowConfig(const char* /*type*/, HWND /*parent*/, const char* /*config*/)
{
    // No user-configurable options yet — we auto-detect the USB device.
    return nullptr;
}

reaper_csurf_reg_t g_csurfReg = {
    "REASIXTY",
    "Rea-Sixty",
    createReaSixty,
    reaSixtyShowConfig,
};

// De-dup for pitch-bend so REAPER's motor echo doesn't re-trigger us.
std::array<std::atomic<uint8_t>, 8> g_lastMsbOut{};
std::array<std::atomic<uint8_t>, 8> g_lastLsbOut{};

// Latest raw fader position (pb14) seen during a touch — recorded
// regardless of deadband, so commitDebouncedTouchReleases can snap
// REAPER to where the user's hand actually left the fader. Without
// this the motor jerks back to pre-touch on release because the
// >=4-LSB deadband swallowed the tiny finger-induced shift.
std::array<std::atomic<uint16_t>, 8> g_lastTouchPb{};
std::array<std::atomic<bool>, 8>     g_lastTouchPbValid{};

// Last fader motor position we actually wrote to the UF8 — lets the timer
// dedup motor-echo pushes, and lets the touch-release handler prime the
// firmware with the user's new position before re-enabling the motor.
std::array<uint16_t, 8>             g_lastFaderPb{};
bool                                g_faderPbInit{false};

// Carry-over buffer for frames split across USB bulk-IN URBs. UF8
// firmware emits FF-framed events back-to-back without aligning them
// to USB packet boundaries, so a `FF 20 02 <strip> <state>` touch
// frame can arrive as `FF 20 02` in one URB and `<strip> <state>` in
// the next. Without stitching, both halves are dropped (`unknown:` log)
// — manifests as Fader 8 (or any fader, but statistically Fader 8 most
// often) silently failing to limp because the TOUCH event never reaches
// the dispatcher. Capped to prevent unbounded growth from corrupt input.
static std::vector<uint8_t> g_inputResidual;
constexpr size_t kInputResidualMax = 64;

void onUf8Input(const uint8_t* dataIn, size_t lenIn)
{
    // Debug: log every non-trivial IN packet that reaches this handler,
    // including payloads that might be interesting (anything not a pure
    // 31 60 / poll pair).
    if (FILE* f = std::fopen("/tmp/reaper_uf8_in_dispatch.log", "a")) {
        std::fprintf(f, "[%zu] ", lenIn);
        for (size_t k = 0; k < lenIn && k < 32; ++k) std::fprintf(f, "%02x ", dataIn[k]);
        std::fprintf(f, "\n");
        std::fclose(f);
    }

    if (!g_midi) return;

    // Stitch any leftover bytes from the previous URB to the front of
    // this one before parsing. The thread-safety story: this handler
    // runs on the libusb event thread and is the SOLE writer/reader of
    // g_inputResidual.
    std::vector<uint8_t> stitched;
    const uint8_t* data = dataIn;
    size_t         len  = lenIn;
    if (!g_inputResidual.empty()) {
        stitched.reserve(g_inputResidual.size() + lenIn);
        stitched.insert(stitched.end(), g_inputResidual.begin(), g_inputResidual.end());
        stitched.insert(stitched.end(), dataIn, dataIn + lenIn);
        g_inputResidual.clear();
        data = stitched.data();
        len  = stitched.size();
    }

    // Walk past each FF frame in the buffer (multiple frames can arrive
    // concatenated, optionally preceded by a 31 60 / 31 00 session prefix).
    size_t i = 0;
    while (i < len) {
        // Session-prefix byte 0x31 + flag byte: skip both.
        if (data[i] == 0x31 && i + 1 < len) { i += 2; continue; }
        if (data[i] != 0xFF) { ++i; continue; }
        // Need at least FF + cmd to know how much more to expect. If
        // we only have a lone FF at the end, drop it — it's most likely
        // a stray byte (frame checksum that happened to land on 0xFF),
        // not the start of a real frame. Saving it as residual would
        // mis-frame the next URB. If we have FF+cmd, only save when
        // the cmd byte is one we actually know — otherwise the FF was
        // also a stray.
        if (i + 1 >= len) {
            // Lone FF at the very end — drop.
            break;
        }
        if (i + 2 >= len) {
            // FF + cmd at the end. Only carry over if cmd is recognised.
            const uint8_t peekCmd = data[i + 1];
            const bool known = (peekCmd == 0x04 || peekCmd == 0x20 ||
                                peekCmd == 0x21 || peekCmd == 0x22 ||
                                peekCmd == 0x23 || peekCmd == 0x24 ||
                                peekCmd == 0x33);
            if (known && len - i <= kInputResidualMax) {
                g_inputResidual.assign(data + i, data + len);
            }
            break;
        }

        // Figure out frame size based on the command byte.
        const uint8_t cmd = data[i + 1];
        size_t frameSize = 0;
        switch (cmd) {
            // FF 04 02 XX 01 XX = 6 bytes total. Two payload variants
            // (02 9d 01 a4 / 02 94 01 9b) cycle alternately. Previous
            // frameSize=7 was wrong: it skipped past the actual end of
            // the poll frame, swallowing whatever FF byte came next —
            // which is exactly how bundled "FF 04 ... FF 21 03 ..."
            // and "FF 04 ... FF 20 02 ..." packets lost their touch /
            // fader events. Symptom: motor stays engaged because the
            // firmware sees its FF 21 03 echoes never come back from
            // host (we never receive the events to echo them).
            case 0x04: frameSize = 6; break;   // poll
            case 0x21: frameSize = 7; break;   // fader position
            case 0x22: frameSize = 7; break;   // button
            case 0x20: frameSize = 6; break;   // fader touch (capacitive)
            case 0x23: frameSize = 6; break;   // pressure sensor (TBD)
            case 0x24: frameSize = 6; break;   // v-pot rotation
            case 0x33: frameSize = 6; break;   // pressure sensor (TBD)
            default:   frameSize = 0; break;
        }
        if (frameSize == 0) {
            // Unknown command — log and skip one byte.
            if (FILE* f = std::fopen("/tmp/reaper_uf8_unknown.log", "a")) {
                std::fprintf(f, "unknown:");
                const size_t show = std::min<size_t>(len - i, 12);
                for (size_t k = 0; k < show; ++k) std::fprintf(f, " %02X", data[i + k]);
                std::fprintf(f, "\n");
                std::fclose(f);
            }
            ++i;
            continue;
        }
        if (i + frameSize > len) {
            // Truncated frame at end of buffer — known opcode but the
            // payload extends past the URB boundary. Carry the partial
            // frame into the next URB rather than dropping it (which
            // is how Fader 8 TOUCH events and others were silently lost).
            if (len - i <= kInputResidualMax) {
                g_inputResidual.assign(data + i, data + len);
            }
            break;
        }

        // Dispatch by command.
        if (cmd == 0x21 && data[i + 2] == 0x03) {
            // Fader position: FF 21 03 strip A B cksum
            // A = MCU LSB (high bit is a flag, masked), B = MCU MSB.
            // Native route: push into the input queue as an absolute volume
            // (coalesced by strip) — the timer will apply it to the track.
            // We only queue while the user is actively touching the fader,
            // so REAPER's motor echo doesn't feed back.
            //
            // Strip indexing: 0-indexed direct on this opcode AND on
            // FF 20 02 TOUCH. cap51_ssl360_fader8_drag captured both
            // directions on user's UF8 (Windows + SSL 360°): Fader 1
            // = byte 00, Fader 8 = byte 07. No shift, no asymmetry.
            const uint8_t strip   = data[i + 3];
            const uint8_t rawA    = data[i + 4];
            const uint8_t rawHigh = data[i + 4] & 0x80;     // diag: was bit 7 set?
            if (strip < 8 && g_touchReported[strip].load()) {
                const uint8_t lsb = rawA & 0x7F;
                const uint8_t msb = data[i + 5] & 0x7F;
                const uint16_t pb14 = static_cast<uint16_t>(lsb | (msb << 7));
                // Diag: log whether bit 7 of LSB was set on the inbound
                // frame. If the firmware echoes back our `lsb|0x80` touch
                // echoes as position events, those echoes carry the bit
                // back and we can filter them. Logged as a separate
                // synthetic "kind" so it doesn't disturb the main POS
                // line until we know what's happening.
                if (rawHigh) {
                    faderInputLog_("ECHO", strip, pb14, 0, int(rawA), "HIBIT");
                }
                // Bit 7 of byte 4 is a firmware-side flag, not an
                // echo of our outbound bit-7-set frames. Verified by
                // capturing /tmp/uf8_fader_input.log with our outbound
                // echoes DISABLED — bit-7-set frames still arrived,
                // and they were the only ones that formed a clean
                // monotonic stream during a smooth pull. The bit-7-
                // CLEAR frames carry the +50..+100 LSB upward spikes
                // ("Werte korrigieren minimal gegen oben") and appear
                // to be some secondary signal (capacitive vs mech
                // sensor, or motor-state polling — exact semantics
                // TBD). For volume tracking we only want the
                // authoritative bit-7-set stream.
                if (!rawHigh) {
                    faderInputLog_("POS", strip, pb14, 0, 0, "DROP_NOHI");
                    i += frameSize;   // cmd 0x21 = 7 bytes; was hardcoded
                                      // 6 → parser slid 1 byte into the
                                      // checksum on every drop. The
                                      // skip-byte recovery at the top of
                                      // the loop usually masked this, but
                                      // when the lone checksum happened
                                      // to be 0xFF the next iteration
                                      // misparsed it as a frame start —
                                      // most visibly affecting fader 8
                                      // (last-pushed-byte position in
                                      // typical bundle layouts).
                    continue;
                }
                // Always record the raw position so the touch-release
                // commit can snap REAPER to where the fader physically
                // ended up, even if every frame this touch was sub-deadband.
                g_lastTouchPb[strip].store(pb14);
                g_lastTouchPbValid[strip].store(true);
                // Echo the position back to UF8 with bit 7 of LSB SET.
                // SSL360 does this throughout every touch (cap32 OUT
                // frames). The firmware uses these echoes to update
                // its motor-target buffer WITHOUT engaging the motor.
                // When the touch ends the firmware implicitly
                // re-engages on this target — no jerk because the
                // target matches the user's last touch position.
                // Without these echoes the motor falls back to a
                // stale internal value (typically near 0) on release.
                if (g_dev) {
                    g_dev->send(uf8::buildFaderPosition(strip, lsb | 0x80, msb));
                }
                // Deadband filter on the pb14 value, NOT on lsb alone.
                // Splitting into msb/lsb produced upward "blips" while the
                // user pulled the fader down: every MSB boundary crossed
                // during slow motion + ±1-3 LSB hardware jitter would land
                // a single noise sample in the next msb bucket, satisfying
                // `msb != prevMsb` regardless of direction → REAPER saw a
                // tiny step backwards. Computing the delta in pb14-space
                // means an upward jitter near a boundary still has to clear
                // the threshold to register. Threshold stays at 4 LSB
                // (≈ 0.003 dB at top, ≈ 0.04 dB at mid range).
                const uint16_t prevPb = static_cast<uint16_t>(
                    (uint16_t(g_lastMsbOut[strip].load()) << 7) |
                     uint16_t(g_lastLsbOut[strip].load()));
                const int signedDelta = int(pb14) - int(prevPb);
                if (std::abs(signedDelta) >= 4) {
                    g_lastMsbOut[strip].store(msb);
                    g_lastLsbOut[strip].store(lsb);
                    queueInput({PendingInput::VolumeAbs, strip, pbToLinearVolume(pb14)});
                    faderInputLog_("POS", strip, pb14, prevPb, signedDelta, "ACC");
                } else {
                    faderInputLog_("POS", strip, pb14, prevPb, signedDelta, "REJ");
                }
            }
        } else if (cmd == 0x22 && data[i + 2] == 0x03) {
            // Button: FF 22 03 id 00 state cksum
            //
            // UF8 PM-mode button ID map (see docs/protocol-notes.md). The
            // firmware does NOT follow the MCU-standard per-strip layout —
            // for example 0x18..0x1F are the top soft-keys above each
            // scribble, not MCU SELECT.
            //
            // Per-strip (still hardcoded in v1):
            //   0x08..0x0F  V-Pot push           (stride 1)
            //   0x18..0x1F  Top soft-key         (stride 1)
            //   0x20..0x37  SOLO/CUT/SEL         (3-byte group per strip)
            //
            // Global buttons (Phase 2.7 Bindings Phase A) route through
            // uf8::bindings::dispatch, which looks up the binding for the
            // active layer and fires the configured builtin / REAPER
            // action. Factory defaults match the previously hardcoded
            // dispatch byte-for-byte; users can rebind from the
            // Settings → Bindings tab once Phase C lands.
            //
            // Anything still unbound falls through to MCU passthrough so
            // the legacy CSI escape hatch keeps working.
            const uint8_t id    = data[i + 3];
            const uint8_t state = data[i + 5];
            const bool pressed  = state == 0x01;

            bool handledNatively = false;

            // UF8 Plugin Mode: TopSoftKey 1..8 (0x18..0x1F) become bank
            // selectors for the user-mapped plug-in's 8 V-Pot banks
            // (Frank 2026-05-13). Press → g_softKeyBank = (id - 0x18);
            // mirrors the FX-Learn UF8 window's bank-combo behaviour
            // exactly. SSL Soft-Key Bank cells (V-POT/Soft 1-5) are
            // no-op in this mode — handled in softkey_bank_select.
            //
            // Unassigned banks (no V-Pot bindings on any of 8 strips)
            // are skipped — Frank 2026-05-13: "unzugewiesene Soft-Key
            // V-Pot banks no-function machen".
            if (id >= 0x18 && id <= 0x1F && g_uf8PluginMode.load()) {
                if (pressed) {
                    const int target = id - 0x18;
                    bool anyAssigned = false;
                    if (auto uctx = userStripCtxFocused_(); uctx.map) {
                        for (int sIdx = 0; sIdx < 8; ++sIdx) {
                            if (uctx.map->uf8.banks
                                .banks[target][sIdx].vst3Param >= 0)
                            {
                                anyAssigned = true; break;
                            }
                        }
                    }
                    if (anyAssigned
                        && g_softKeyBank.exchange(target) != target) {
                        g_softKeyDirty.store(true);
                        g_bankDirty.store(true);
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "%d", target);
                        SetExtState("ReaSixty", "softKeyBank", buf, true);
                    }
                }
                handledNatively = true;
            }

            // Top-soft-key user-Quick override has the highest priority:
            // when a Quick is engaged on the active layer, route the
            // press into the matching (layer, quick, sub-bank, slot)
            // user-Quick binding, bypassing the default ssl_softkey
            // factory binding that would otherwise steal the press
            // through bindings::dispatch.
            //
            // UF8 Plugin Mode bypasses this so a stale activeQuick from
            // before the mode-flip doesn't hijack presses meant for the
            // FX-Learn-mapped plug-in param under each strip (Frank
            // 2026-05-13: "die Quick Buttons dürfen nichts an den
            // Soft-Keys ändern, sonst kommt man nicht mehr auf die
            // gelearnten Parameter").
            if (id >= 0x18 && id <= 0x1F && !g_uf8PluginMode.load()
                && !handledNatively) {
                const int layer = uf8::bindings::getActiveLayer();
                const int aq = (layer >= 0 && layer <= 2)
                               ? g_activeQuick[layer].load() : -1;
                if (aq >= 0) {
                    const int sb = g_activeSubBank[layer].load();
                    uf8::bindings::dispatchUserQuickSlot(
                        layer, aq, sb, id - 0x18, pressed);
                    handledNatively = true;
                }
            }

            // Bindings layer — globals previously hardcoded inline. Now
            // also covers top-soft-keys (TopSoftKey1..8) which carry an
            // ssl_softkey factory binding by default — same semantics as
            // SSL 360°, but the user can rebind via Settings.
            if (!handledNatively) {
                if (auto bid = uf8::bindings::fromUf8DeviceId(id);
                    bid != uf8::bindings::ButtonId::None) {
                    if (uf8::bindings::dispatch(bid, pressed)) {
                        handledNatively = true;
                    }
                }
            }

            // Per-strip dispatch — stay hardcoded in v1. Soft-key bank
            // selectors (0x68..0x6D) used to live here too, now route
            // through bindings::dispatch above with a default
            // softkey_bank_select factory binding.
            if (!handledNatively) {
                if (id >= 0x08 && id <= 0x0F) {
                    // V-Pot push. Routed by Selection Mode:
                    //   Norm / REC / REC+MON → PanCenter (modes
                    //                          only affect SEL).
                    //   AUTO                 → set strip's auto-mode to
                    //                          Trim/Read (0).
                    //   Instance             → toggle FX window of the
                    //                          strip's track's active
                    //                          instance.
                    if (pressed) {
                        const uint8_t strip =
                            static_cast<uint8_t>(id - 0x08);
                        switch (g_selectionMode.load()) {
                            case SelectionMode::Auto:
                                queueInput({PendingInput::AutoModeSet,
                                            strip, 0.0});
                                break;
                            case SelectionMode::Instance:
                                queueInput({PendingInput::StripInstanceOpen,
                                            strip, 0.0});
                                break;
                            case SelectionMode::Norm:
                            case SelectionMode::Rec:
                            case SelectionMode::RecMon:
                            default:
                                queueInput({PendingInput::PanCenter,
                                            strip, 0.0});
                                break;
                        }
                    }
                    handledNatively = true;
                } else if (id >= 0x20 && id <= 0x37) {
                    const uint8_t strip = static_cast<uint8_t>((id - 0x20) / 3);
                    const int which     = (id - 0x20) % 3;   // 0=SOLO 1=CUT 2=SEL
                    if (pressed) {
                        PendingInput::Kind k = PendingInput::SoloToggle;
                        if (which == 1) {
                            k = PendingInput::MuteToggle;
                        } else if (which == 2) {
                            // SEL press is the only per-strip input the
                            // Selection-Mode group hijacks (V-Pots stay
                            // on their Norm wiring for REC / RecMon /
                            // AUTO).
                            switch (g_selectionMode.load()) {
                                case SelectionMode::Rec:
                                    k = PendingInput::RecArmToggle;
                                    break;
                                case SelectionMode::RecMon:
                                    k = PendingInput::RecArmMonToggle;
                                    break;
                                case SelectionMode::Auto:
                                    k = PendingInput::AutoModeStep;
                                    break;
                                case SelectionMode::Norm:
                                case SelectionMode::Instance:
                                default:
                                    k = g_shiftHeld.load()
                                        ? PendingInput::SelectToggle
                                        : PendingInput::SelectExclusive;
                                    // Arm long-press detection — only
                                    // relevant when SEL is doing track-
                                    // selection (Norm / Instance). The
                                    // mode-specific cases short-circuit
                                    // before the spill timer touches.
                                    g_selPressMs[strip].store(nowMs_());
                                    g_selSpillFired[strip].store(false);
                                    break;
                            }
                        }
                        queueInput({k, strip, 0.0});
                    } else if (which == 2) {
                        // SEL release — clear long-press timer so onTimer
                        // stops checking. spill_fired stays as a record
                        // of whether the spill ran during this hold (not
                        // re-read after release, but cheap to leave).
                        g_selPressMs[strip].store(0);
                    }
                    handledNatively = true;
                }
                // Top-soft-keys (0x18..0x1F) are now handled at the top
                // of the switch block via the user-bank override + the
                // bindings::dispatch path with a default `ssl_softkey`
                // factory binding. No inline fall-through needed here.
            }

            if (!handledNatively) {
                const uint8_t mcu[3] = {0x90, id, pressed ? uint8_t{0x7F} : uint8_t{0x00}};
                g_midi->send(std::span<const uint8_t>(mcu, 3));
            }
        } else if (cmd == 0x20 && data[i + 2] == 0x02) {
            // Fader touch: FF 20 02 strip state cksum
            //
            // Capacitive touch — hardware-debounced. We track the state so
            // fader-position events only apply while the user is touching
            // (kills the motor-echo feedback loop), and release the motor
            // so the user's hand isn't fighting it.
            //
            // Strip indexing — 0-indexed end-to-end across FF 20
            // (touch), FF 21 (position), FF 33 (secondary sensor),
            // and outbound FF 1D (motor enable/limp). Verified
            // 2026-05-07 from a per-fader Windows USBPcap capture:
            // each physical fader 1..8 emits exactly strip byte
            // (fader_number - 1) on every opcode it produces.
            // Earlier 1-indexed-with-wraparound theories (commits
            // 7d02274 / 07d786d) were misreads of partial logs and
            // are conclusively dead.
            const uint8_t rawStrip = data[i + 3];
            const uint8_t state    = data[i + 4];
            if (rawStrip > 7) {
                i += frameSize;
                continue;
            }
            const uint8_t strip = rawStrip;
            if (strip < 8) {
                // Diag log — same path as f73201c. Append-mode, one line
                // per touch event so we can correlate with FF 1B keepalive
                // and FF 1D motor commands logged from the worker thread.
                if (FILE* lg = std::fopen("/tmp/reaper_uf8_motor.log", "a")) {
                    const auto t = std::chrono::system_clock::now().time_since_epoch();
                    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t).count();
                    std::fprintf(lg, "[%lld] TOUCH rawStrip=%u strip=%u state=%u\n",
                                 static_cast<long long>(ms), rawStrip, strip, state);
                    std::fclose(lg);
                }
                faderInputLog_("TOUCH", strip, state, 0, 0,
                               state ? "ON" : "OFF");
                // UF8 in PM mode does not auto-release the fader motor
                // on capacitive touch (that behaviour is only present in
                // DAW/MCU mode), so we send the motor-limp command
                // ourselves. On press we disable immediately so the user
                // isn't fighting the motor; on release we commit through
                // a debounced two-phase sequence (position first, motor
                // re-enable one timer tick later) to work around the
                // firmware's tendency to drive toward its stale target
                // between re-enable and the next position command.
                if (state != 0) {
                    g_touchLastPress[strip] = std::chrono::steady_clock::now();
                    g_touchReleasePending[strip].store(false);
                    g_touchReported[strip].store(true);
                    // Send LIMP unconditionally on every touch-ON edge —
                    // a previously-lost OFF event would otherwise pin
                    // touchReported true and skip the LIMP. Re-sending
                    // on every ON is cheap (one 6-byte frame) and
                    // idempotent.
                    if (g_dev) g_dev->sendPriority(uf8::buildMotorEnable(strip, false));
                } else {
                    g_touchReleasePending[strip].store(true);
                }
            }
        } else if (cmd == 0x24 && data[i + 2] == 0x02) {
            // V-pot rotation: FF 24 02 strip raw cksum
            //
            // `raw` is a 6-bit signed detent delta (two's complement) in
            // the low 6 bits:
            //   0x01..0x1F =  +1 .. +31  (clockwise)
            //   0x3F..0x20 =  -1 .. -32  (counter-clockwise)
            // Bits 0x40/0x80 are unused — confirmed empirically on a live
            // UF8 by capturing slow CW vs. CCW rotations.
            const uint8_t strip = data[i + 3];
            const uint8_t raw   = data[i + 4];
            int8_t signed6 = static_cast<int8_t>(raw & 0x3F);
            if (signed6 & 0x20) signed6 |= 0xC0;   // sign-extend from 6 bits
            if (strip < 8) {
                // Per-strip V-Pot rotation. Scale: pan keeps signed6/
                // 128 (1 detent = 0.78% pan); AUTO + Instance need raw
                // signed6 so their accumulators feel like the channel-
                // encoder Shift+rotate path. Norm / REC / REC+MON
                // stay on the legacy pan wiring.
                switch (g_selectionMode.load()) {
                    case SelectionMode::Auto:
                        queueInput({PendingInput::AutoModeDelta,
                                    strip,
                                    static_cast<double>(signed6)});
                        break;
                    case SelectionMode::Instance:
                        queueInput({PendingInput::StripInstanceDelta,
                                    strip,
                                    static_cast<double>(signed6)});
                        break;
                    case SelectionMode::Norm:
                    case SelectionMode::Rec:
                    case SelectionMode::RecMon:
                    default:
                        queueInput({PendingInput::PanDelta, strip,
                                    static_cast<double>(signed6) / 128.0});
                        break;
                }
            } else if (strip == 0x08) {
                // Channel encoder — dispatched through the bindings
                // system (ButtonId::ChannelEncoder) from the drain
                // handler. The active-modifier slot's builtin decides
                // what each detent does. Default Plain =
                // encoder_mode_dispatch (legacy Nav/Nudge/Focus/Instance
                // mode system). Default Shift = instance_cycle. Cmd /
                // Ctrl unbound until the user picks an action in
                // Settings → Bindings → Channel Encoder.
                queueInput({PendingInput::EncoderRotation, 0,
                            static_cast<double>(signed6)});
            }
        }
        // cmd 0x33 — secondary "user-actively-manipulating-cap" sensor.
        // Decoded 2026-05-07 from per-fader Windows USBPcap capture: fires
        // only when the user pushes the fader cap (top-of-cap conductive
        // contact); pure side-touch produces FF 20 alone. State-byte toggles
        // (OFF/ON pulses) during a push; identical 0-indexed strip byte as
        // FF 20. We deliberately do NOT process it: FF 20 + FF 21 already
        // give us reliable touch + position. Treating FF 33 as touch
        // double-counts the user's hand and causes spurious LIMPs.

        i += frameSize;
    }
}

// Log raw HID reports until we've reverse-engineered the report format.
void logHid(const uint8_t* data, size_t len)
{
    if (FILE* f = std::fopen("/tmp/reaper_uf8_hid.log", "a")) {
        for (size_t i = 0; i < len; ++i) std::fprintf(f, "%02x ", data[i]);
        std::fprintf(f, "\n");
        std::fclose(f);
    }
}

// Very simple rolling log file so we can see what CSI sends us without
// needing REAPER's console plumbing. Written from the Core-MIDI thread —
// kept fopen/fprintf/fclose for simplicity; REAPER's MIDI rate is low.
void logMidi(std::span<const uint8_t> bytes)
{
    FILE* f = std::fopen("/tmp/reaper_uf8_midi.log", "a");
    if (!f) return;
    for (auto b : bytes) std::fprintf(f, "%02x ", b);
    std::fprintf(f, "\n");
    std::fclose(f);
}

// Translate an incoming MCU MIDI packet to UF8 vendor-USB commands.
// Currently handles the scribble-strip SysEx only — enough to prove the
// end-to-end pipe. Fuller mapping (meters, LEDs, V-pot, fader) is next.
void onMidiFromReaper(std::span<const uint8_t> bytes)
{
    logMidi(bytes);
    if (!g_dev || !g_dev->isOpen()) return;

    // MCU meter forwarding DISABLED — the UF8 meter command layout
    // (FF 38 04 <X> 00 <Y> <Z>) isn't fully decoded yet. Empirically
    // the expected `X = strip * 3` mapping doesn't match the byte
    // values seen in non-fader captures. Correlated audio-playback
    // capture on Windows is the next step — then we can safely
    // translate MCU D0 events without sending garbage to the device.
    (void)bytes;

    // Scribble-strip SysEx: F0 00 00 66 14 12 <pos> <text> F7
    // <pos> indexes into a 56-char-wide virtual display (7 chars * 8 strips),
    // row 0 (upper) at 0..0x37, row 1 (lower) at 0x38..0x6F.
    if (bytes.size() >= 8
        && bytes[0] == 0xF0 && bytes[1] == 0x00 && bytes[2] == 0x00
        && bytes[3] == 0x66 && bytes[4] == 0x14 && bytes[5] == 0x12)
    {
        const uint8_t startPos = bytes[6];

        size_t textStart = 7;
        size_t textEnd   = bytes.size();
        if (bytes.back() == 0xF7) --textEnd;

        // Walk the payload 7 chars at a time, one UF8 command per strip.
        size_t cursor = textStart;
        uint8_t pos = startPos;
        while (cursor < textEnd) {
            const size_t chunkLen = (textEnd - cursor) < 7u ? (textEnd - cursor) : 7u;
            std::string_view chunk(
                reinterpret_cast<const char*>(bytes.data() + cursor), chunkLen);

            // Trim trailing spaces — SSL 360° sends upper-row at natural
            // (unpadded) length. Keeping the text unpadded matches the
            // captured command format.
            auto last = chunk.find_last_not_of(' ');
            std::string_view trimmed = (last == std::string_view::npos)
                                       ? chunk.substr(0, 0) : chunk.substr(0, last + 1);

            if (pos < 0x38) {
                // Upper row (track names)
                const uint8_t strip = pos / 7;
                if (strip < 8) g_dev->send(uf8::buildStripTextUpper(strip, trimmed));
            } else {
                // Lower row (v-pot / value display)
                const uint8_t strip = (pos - 0x38) / 7;
                if (strip < 8) g_dev->send(uf8::buildStripTextLower(strip, chunk));
            }

            cursor += chunkLen;
            pos    += 7;
        }
        return;
    }

    // MCU pitch-bend (E<ch> LSB MSB) was previously forwarded as
    // FF 1E motor-drive — but pushZonesForVisibleSlots already streams
    // motor targets natively via uiVolLinear() and gates on
    // g_touchReported, while this MIDI path did NOT. Result: any user
    // who left an MCU surface configured against our virtual MIDI port
    // (legacy CSI setup) had REAPER's volume-echo pitchbend re-engaging
    // the motor mid-touch — fader felt locked because every motor-limp
    // we sent was undone microseconds later by the next pitchbend.
    // Drop the forward; native motor echo handles the same job correctly.
    if (bytes.size() == 3 && (bytes[0] & 0xF0) == 0xE0) {
        return;
    }
}

uint32_t reaperColorForVisibleSlot(int slot)
{
    const int trackCount = visibleTrackCount();
    const int bankOffset = g_bankOffset.load();
    const int realSlot   = slot + bankOffset;

    // FX Learn UF8 user-strip mode: the focused track has a user-mapped
    // plug-in and the 8 strips drive its params. The colour bar on each
    // LCD reflects the V-Pot binding's user colour (per active bank).
    // Empty bank slots without any per-strip binding paint the strip OFF
    // so it visually matches the blanked scribble / channel# / LEDs.
    // (Frank 2026-05-09.)
    if (g_uf8PluginMode.load() && slot >= 0 && slot < 8) {
        if (auto u = userStripCtxFocused_(); u.map) {
            const int bank = std::clamp(g_softKeyBank.load(), 0, uf8::kUserUf8BankCount - 1);
            const auto& bs   = u.map->uf8.banks.banks[bank][slot];
            const auto& strp = u.map->uf8.strips[slot];
            if (bs.stripColour != 0) {
                return bs.stripColour & 0x00FFFFFFu;
            }
            const bool anyBound = (bs.vst3Param >= 0)
                || (strp.faderVst3Param >= 0)
                || (strp.soloVst3Param  >= 0)
                || (strp.cutVst3Param   >= 0)
                || (strp.selVst3Param   >= 0);
            if (!anyBound) return 0;
            // Bound but no user colour → fall through to bank-track colour
            // so the strip still shows something familiar.
        }
    }

    // Routing modes (Send/Receive on V-Pot or fader): the strip represents
    // a send/receive, not a track. Colour the strip with the OTHER end of
    // the route — destination track for a send, source track for a receive
    // — so the user can see at a glance which bus / aux each strip points
    // at. V-Pot routing wins over fader routing if both are somehow active
    // (clearVpotRouting_ / clearFaderRouting_ keep them mutually exclusive
    // by design, but match the per-strip rendering precedence here).
    StripRoute r = resolveVpotRoute_(slot, bankOffset, trackCount);
    if (!r.active()) r = resolveFaderRoute_(slot, bankOffset, trackCount);
    if (r.active()) {
        if (!r.valid) {
            // Routing mode active but the send/receive slot doesn't exist
            // on the source track (e.g. focused track has 4 sends, strip
            // 5..8 in "Sends of Focused" mode). Strip should be visually
            // empty — return 0 so ColorSync paints OFF.
            return 0;
        }
        if (r.track) {
            const char* tag = (r.sendCategory == 0) ? "P_DESTTRACK" : "P_SRCTRACK";
            MediaTrack* other = static_cast<MediaTrack*>(GetSetTrackSendInfo(
                r.track, r.sendCategory, r.sendIndex, tag, nullptr));
            if (other && ValidatePtr2(nullptr, other, "MediaTrack*")) {
                const int oc = GetTrackColor(other);
                return static_cast<uint32_t>(oc) & 0x00FFFFFFu;
            }
            // Hardware-output sends (no destination track): fall through to
            // bank-track colour so the strip stays coloured rather than
            // going dark.
        }
    }

    if (realSlot >= trackCount) return 0;
    MediaTrack* tr = visibleTrackAt(realSlot);
    if (!tr) return 0;
    // REAPER returns native color as int. Bit 0x1000000 is "color set";
    // low 24 bits are 0xBBGGRR on Windows, 0xRRGGBB on mac/Linux via the
    // "native" encoding. GetTrackColor wraps that — low 24 bits are what
    // we want, matching quantize()'s 0xRRGGBB expectation.
    const int c = GetTrackColor(tr);
    return static_cast<uint32_t>(c) & 0x00FFFFFFu;
}

// If the track hosts an SSL 360°-enabled plug-in, return the short label
// the UF8 would display in Plug-in Mixer / Channel Strip Mode's "Channel
// Strip Type" zone ("CS 2", "4K B", "4K E", "BusComp"). Empty string
// means "no SSL plug-in on this track" — caller falls back to REAPER-native
// rendering.
//
// Detection is name-based: the VST3 plug-ins SSL ships expose their
// identity via TrackFX_GetFXName, prefixed with "VST3: SSL Native ...".
// We match the substring rather than the exact name to be forgiving of
// version bumps.
std::string sslPluginShortName(MediaTrack* tr)
{
    if (!tr) return {};
    if (!ValidatePtr2(nullptr, tr, "MediaTrack*")) return {};
    const int fxCount = TrackFX_GetCount(tr);
    char buf[256];
    for (int fx = 0; fx < fxCount; ++fx) {
        if (!TrackFX_GetFXName(tr, fx, buf, sizeof(buf))) continue;
        std::string n(buf);
        // TrackFX_GetFXName returns e.g. "VST3: SSL Native Channel Strip 2 (SSL)"
        if (n.find("Channel Strip 2") != std::string::npos) return "CS 2";
        if (n.find("Channel Strip")   != std::string::npos) return "CS";
        if (n.find("4K B")            != std::string::npos) return "4K B";
        if (n.find("4K E")            != std::string::npos) return "4K E";
        if (n.find("Bus Compressor")  != std::string::npos) return "BusComp";
        if (n.find("360 Link")        != std::string::npos) return "360 Link";
    }
    return {};
}

// Label shown in the "Currently Selected Parameter" zone (`FF 66 <n> 04
// <strip>`) — also the command that keeps the color bar rendered.
//
// Two-mode logic (matches the user's request 2026-04-20):
//   - Track hosts SSL 360° plug-in → show plug-in short name (e.g. "CS 2")
//     so the UF8 displays the plug-in identity the same way SSL 360° would.
//   - Otherwise → show the REAPER track name (truncated to 12 chars, as
//     that's the widest this zone renders cleanly). Empty track name →
//     "CH N" fallback so the slot still reads as populated.
std::string slotLabelForVisibleSlot(int slot)
{
    const int trackCount = visibleTrackCount();
    const int realSlot   = slot + g_bankOffset.load();
    if (realSlot >= trackCount) {
        char fallback[8];
        std::snprintf(fallback, sizeof(fallback), "CH %d", realSlot + 1);
        return fallback;
    }
    MediaTrack* tr = visibleTrackAt(realSlot);
    if (!tr) return "";

    if (auto ssl = sslPluginShortName(tr); !ssl.empty()) return ssl;

    // REAPER track name via P_NAME config parm.
    char name[256] = {0};
    if (GetSetMediaTrackInfo_String(tr, "P_NAME", name, false) && name[0] != 0) {
        std::string s(name);
        if (s.size() > 12) s.resize(12);
        return s;
    }
    char fallback[8];
    std::snprintf(fallback, sizeof(fallback), "CH %d", realSlot + 1);
    return fallback;
}

// Convert REAPER linear-amplitude volume (0..~4) to a dB string for the
// O/PdB zone's 6-char value slot: "-inf", "-6.0", "0.0", "12.0", "-12.5",
// "-100".
//
// REAPER stores fader position as a linear multiplier — 1.0 = 0 dB.
// Below ~10^-5 we call it "-inf" to match what the SSL LCD shows at
// the fader bottom. Always one decimal where it fits in 6 chars; values
// past -100 dB drop the decimal to keep the leading minus visible.
std::string formatDbReadout(double linearAmp)
{
    if (linearAmp < 1e-5) return "-inf";
    const double dB = 20.0 * std::log10(linearAmp);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.1f", dB);
    std::string s(buf);
    if (s.size() > 6) {
        std::snprintf(buf, sizeof(buf), "%.0f", dB);
        s.assign(buf);
        if (s.size() > 6) s.resize(6);
    }
    return s;
}

// Is this slot a binary in/out toggle (driven by a UC1 button on the
// physical surface)? Binary params render as full-or-empty on the
// V-Pot bar (no gradient) and respond to V-Pot push as a 0↔1 toggle
// instead of a "reset to default". Match by slot id rather than
// linkIdx so any future button additions show up here automatically.
bool isBinarySlot(const uf8::LinkSlot& s)
{
    if (!s.id) return false;
    std::string_view id{s.id};
    return id == "Bypass"          || id == "CompBypass"
        || id == "EqIn"            || id == "DynamicsIn"
        || id == "Listen"          || id == "HighEqBell"
        || id == "LowEqBell"       || id == "CompFastAttack"
        || id == "CompPeak"        || id == "GateExpander"
        || id == "GateAttack"      || id == "EqType"
        || id == "Pre"             || id == "ImpedanceIn"
        // Frank 2026-05-06: V-Pot push should toggle / cycle these
        // EXT_FUNCS slots instead of resetting to 0.5. WidthMode is a
        // 3-state enum (Full / Low / High) — the multi-step cycling
        // path inside the binary branch (TrackFX_GetParameterStepSizes)
        // walks all three values per push.
        || id == "AutoMakeup"      || id == "WidthMode"
        || id == "FiltersIn";
}

// Is this slot a bipolar param with a meaningful centre detent (0 dB
// Gain, centred Trim/Fader)? Bipolar slots render as fill from centre
// outward with a centre marker (cap37 HF Gain). Unipolar slots
// (frequencies, Q, thresholds — anything with a min..max linear sweep
// and no neutral position) render as a single line moving across the
// bar (cap38 HF Freq).
bool isBipolarSlot(const uf8::LinkSlot& s)
{
    if (!s.id) return false;
    std::string_view id{s.id};
    return id == "InputTrim"          || id == "OutputTrim"
        || id == "LinkableFaderLevel" || id == "HighEqGain"
        || id == "HighMidEqGain"      || id == "LowMidEqGain"
        || id == "LowEqGain";
}

// Format a -1..+1 pan value as SSL-convention "L100" / "C" / "R50".
// Centred values render as bare "C" so the label-value gap is obvious.
std::string formatPanReadout(double pan)
{
    if (pan < -1.0) pan = -1.0;
    if (pan >  1.0) pan =  1.0;
    if (std::abs(pan) < 0.005) return "C";
    int pct = static_cast<int>(std::round(std::abs(pan) * 100.0));
    if (pct > 100) pct = 100;
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%c%d", pan < 0 ? 'L' : 'R', pct);
    return buf;
}

// Compose the Value Line (19 chars) — e.g. "Vol        -6.0dB".
// Left-justified label, right-justified value. Truncates both if needed
// so the total fits within 19 chars.
std::string composeValueLine(std::string_view label, std::string_view value)
{
    constexpr size_t kWidth = 19;
    if (label.size() + value.size() + 1 > kWidth) {
        // Prefer to show full value; trim label.
        const size_t labelMax = kWidth - value.size() - 1;
        label = label.substr(0, labelMax);
    }
    std::string out(label);
    const size_t padding = kWidth - label.size() - value.size();
    out.append(padding, ' ');
    out.append(value);
    return out;
}

// Slot-level state caches — push only on change to avoid hammering the
// OUT endpoint 30× per second.
std::array<std::string, 8> g_lastTrackName{};
std::array<std::string, 8> g_lastSlotLabel{};
// Top-soft-key LED dedup. -1 = unset. Encodes the TopSoftKeyState as
// 0=Off / 1=Dim / 2=On so transitions between any two visible levels
// trigger a re-push.
std::array<int8_t, 8>      g_lastTopSoftKey{-1, -1, -1, -1, -1, -1, -1, -1};
std::array<std::string, 8> g_lastCsType{};
std::array<std::string, 8> g_lastValueLine{};
std::array<std::string, 8> g_lastFaderDb{};
std::array<std::string, 8> g_lastChanNum{};
std::array<uint16_t, 8>    g_lastVPotBar{};      // 16-bit LE per strip
std::array<uint8_t, 8>     g_lastVPotMode{};     // FF 66 09 0D mode byte per strip

// Routed pan-tweak overlay: when the user moves a control that writes
// the routed D_PAN, the value-line briefly shows the formatted pan
// readout instead of the send name, then reverts after kPanOverlayMs.
// Touched from drainInputQueue (main thread) and read from the timer
// (also main thread) — plain types are sufficient.
std::array<int64_t, 8>     g_panOverlayUntilMs{};
std::array<std::string, 8> g_panOverlayText{};

// Folder Mode reveal timestamps — bumped by V-Pot-driven inputs in
// drainInputQueue so a parent strip briefly shows the real value before
// reverting to "Folder". See kFolderRevealMs in the forward decls.
std::array<int64_t, 8>     g_folderRevealUntilMs{};

// CUT LED last-pushed state per strip — int8_t with -1 = unknown / force
// re-push, 0/1 = effective mute state. Effective mute follows routing:
// in Send/Receive routing mode the LED reflects the routed entity's
// B_MUTE, otherwise the bank track's B_MUTE. REAPER's SetSurfaceMute
// callback only fires for track mute changes, so a per-tick poll is the
// only way to keep the LED in sync with send-mute toggles.
std::array<int8_t, 8>      g_lastCutLed{-1, -1, -1, -1, -1, -1, -1, -1};
// Per-strip Solo / Sel last-pushed state used by the user-strip-mode
// LED override (Plugin Mode + a learned plug-in focused). Outside user-
// strip mode, Solo/Sel LEDs are pushed event-driven via sendLed from
// REAPER callbacks — these caches are unused there. -1 = unknown.
std::array<int8_t, 8>      g_lastSoloLed{-1, -1, -1, -1, -1, -1, -1, -1};
std::array<int8_t, 8>      g_lastSelLed {-1, -1, -1, -1, -1, -1, -1, -1};
bool                       g_vpotBarInit{false};

// Resolve the LinkSlot for one visible strip at the current page index.
// Returns nullptr when:
//   - no track
//   - PAN mode is forced globally (treat every strip as if it had no plug-in)
//   - the focused-param domain is None (no plugin selected at all)
//   - the track's plug-in isn't in our PluginMap registry
//   - the focused slot index is past the plug-in's slot count (e.g. BC2
//     has 7 slots, walking further reveals pan fallback)
// Must be called on the main thread — touches REAPER API.
//
// Domain-aware: a track with both CS2 + BC2 returns the slot of the plug-in
// matching the focused domain (so a focused BC param doesn't render against
// the CS plug-in and miss).
const uf8::LinkSlot* slotForStrip(MediaTrack* tr,
                                  const uf8::FocusedParam& focused,
                                  int* outFxIdx)
{
    if (!tr) return nullptr;
    if (g_forcePan.load()) return nullptr;
    auto match = uf8::lookupPluginOnTrack(tr, focused.domain);
    if (!match.map) return nullptr;
    // Resolve the Link slot index against THIS track's plugin map —
    // different tracks may host different CS variants (CS 2 vs 4K E
    // vs 4K G vs 4K B), each with its own slot ordering.
    const uf8::LinkSlot* slot = uf8::findSlotByLinkIdx(*match.map,
                                                       focused.slotIdx);
    if (!slot) return nullptr;
    if (outFxIdx) *outFxIdx = match.fxIndex;
    return slot;
}

// V-Pot bar 16-bit LE encoding from cap37 (HF Gain ±20 dB on 4K E)
// and cap38 (HF Freq sweep). Mode register is paired 0x08 per strip
// (bipolar centre-out render in firmware).
//
//   Bipolar (Gain, Pan, Trim):
//     byte0 = signed 8-bit two's complement, range [-100..+100]
//       0x00 → centre   (REQUIRES byte1 = 0x80 anchor)
//       0x64 → +full    (cap37 +20 dB max)
//       0x9C → -full    (cap37 -20 dB max, = -100 signed)
//     byte1 = 0x80 ONLY when byte0 = 0x00, else 0x00.
//
//   Unipolar (Freq, Q, Threshold):
//     byte0 = 0x01..0x62 linear (cap38 full sweep), byte1 = 0x00.
//
// Wire order matters: cap37 sends position (FF 66 11 0F) FIRST,
// then mode (FF 66 09 0D) ~55 µs later. Sending mode-then-position
// with mismatched encoding renders a wrong-direction bar.
uint16_t vpotPosFromBipolar(double v)
{
    if (v < -1.0) v = -1.0;
    if (v >  1.0) v =  1.0;
    int b0 = static_cast<int>(std::round(v * 100.0));
    if (b0 == 0) return 0x8000;  // centre anchor: byte0=0x00, byte1=0x80
    if (b0 < -100) b0 = -100;
    if (b0 >  100) b0 =  100;
    return static_cast<uint16_t>(static_cast<uint8_t>(static_cast<int8_t>(b0)));
}

// Mode 0x01 unipolar: byte0 = 0..0x64 (0..100), byte1 = 0x00.
// cap15 t=11.052/13.206/14.795 mode-register `01 01 01 01 03 03 03 03`
// with positions `00 00`/`32 00`/`64 00` cover 0%/50%/100%.
uint16_t vpotPosFromUnipolar(double v)
{
    if (v < 0.0) v = 0.0;
    if (v > 1.0) v = 1.0;
    int b0 = static_cast<int>(std::round(v * 100.0));
    if (b0 < 0)   b0 = 0;
    if (b0 > 100) b0 = 100;
    return static_cast<uint16_t>(b0 & 0xFF);
}

uint16_t vpotPosFromNormalized(double v) { return vpotPosFromUnipolar(v); }

uint16_t vpotPosFromPan(double pan) { return vpotPosFromBipolar(pan); }

void pushZonesForVisibleSlots()
{
    if (!g_dev || !g_dev->isOpen()) return;

    const int trackCount = visibleTrackCount();
    const int bankOffset = g_bankOffset.load();
    const auto focused   = uf8::getFocusedParam();

    std::array<uint16_t, 8> vpotBar{};

    // Full re-push on a bank change — every cached "last" value refers
    // to a different track now, so dedup would suppress legitimate
    // updates for a few ticks until each value happens to drift. Also
    // re-sync SOLO/MUTE/SEL/ARM LEDs to the new bank's tracks, since
    // REAPER only calls SetSurface* on actual state changes, not on
    // bank shifts.
    //
    // Same story for page changes: every strip's Parameter Label and
    // Value Line now refer to a different plugin slot, so dedup must
    // re-fire the pushes.
    const bool bankChanged    = g_bankDirty.exchange(false);
    // Routing-mode change (Send/Receive toggles) flips what every strip
    // points at. Treat it as a bank-shift for cache-invalidation purposes:
    // every g_last* needs to repaint so the previous mode's stale values
    // don't pin the display.
    const bool routingChanged = g_routingDirty.exchange(false);
    // g_focusedDirty is the canonical "focused param changed" signal,
    // set by anyone who mutates uf8::g_focusedParam (Stage 2+ adds
    // cross-device writers). g_pageDirty is the older UF8-local flag —
    // currently set in tandem from main.cpp; both are drained here so
    // either path forces a full label/value re-push next tick.
    const bool focusChanged   = uf8::g_focusedDirty.exchange(false);
    const bool pageChanged    = g_pageDirty.exchange(false) || focusChanged;
    // Bank-follow-focus: when an external writer (UC1, plugin GUI,
    // chase) changes the focused param, switch the soft-key bank to
    // whichever bank in the new domain holds that linkIdx. Skips when
    // the linkIdx isn't in any bank (custom param, or not yet
    // registered in the soft-key tables).
    if (focusChanged) {
        const int b = softkey::bankContaining(focused.domain, focused.slotIdx);
        if (b >= 0 && g_softKeyBank.exchange(b) != b) {
            g_softKeyDirty.store(true);
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%d", b);
            SetExtState("ReaSixty", "softKeyBank", buf, true);
        }
    }
    const bool softKeyChanged = g_softKeyDirty.exchange(false);
    // Modifier-snapshot change forces a top-soft-key label repaint —
    // each strip's binding may carry a per-modifier label override
    // that the LCD should switch to / away from on Shift / Cmd / Ctrl
    // press / release. Cheap atomic compare; this tick decides which
    // labels go out below.
    static uf8::bindings::Modifier s_lastModifier =
        uf8::bindings::Modifier::Plain;
    const auto curModifier = uf8::bindings::currentModifierSnapshot();
    const bool modifierChanged = (curModifier != s_lastModifier);
    s_lastModifier = curModifier;
    if (pageChanged || softKeyChanged || modifierChanged) {
        g_lastSlotLabel.fill({});
        g_lastValueLine.fill({});
        g_lastTopSoftKey.fill(-1);
    }
    if (pageChanged) {
        // The Plugin / FLIP / forcePan / focus toggles all set pageDirty
        // because they flip what the fader and V-Pot represent on each
        // strip. Force a full re-push of motor pb14, O/PdB readout, and
        // V-Pot bar — their dedup caches would otherwise pin the strip
        // to the previous mode's source (e.g. fader stays at the SSL
        // CS Fader position even after Plugin mode is toggled off).
        g_lastFaderPb.fill(0xFFFF);
        g_lastFaderDb.fill({});
        g_lastVPotBar.fill(0xFFFF);
    }
    // FX Learn UF8: when the user edits a user-plugin map (binding /
    // colour / label change), the catalog generation bumps. Invalidate
    // all per-strip caches so colour, label, and binding-state edits
    // surface on the device on the next tick rather than being pinned
    // by stale dedup state.
    static int s_lastUserPluginsGen = -1;
    const int curUserPluginsGen = uf8::user_plugins::generation();
    if (curUserPluginsGen != s_lastUserPluginsGen) {
        s_lastUserPluginsGen = curUserPluginsGen;
        g_lastCutLed.fill(-1);
        g_lastSoloLed.fill(-1);
        g_lastSelLed.fill(-1);
        g_lastTrackName.fill({});
        g_lastValueLine.fill({});
        g_lastTopSoftKey.fill(-1);
        g_lastChanNum.fill({});
        g_lastFaderDb.fill({});
        g_lastFaderPb.fill(0xFFFF);
        g_lastVPotBar.fill(0xFFFF);
        if (g_sync) g_sync->invalidate();
    }
    if (bankChanged || routingChanged) {
        g_lastTrackName.fill({});
        g_lastSlotLabel.fill({});
        g_lastCsType.fill({});
        g_lastValueLine.fill({});
        g_lastFaderDb.fill({});
        g_lastChanNum.fill({});
        g_lastFaderPb.fill(0xFFFF);
        g_lastTopSoftKey.fill(-1);
        g_lastCutLed.fill(-1);
        g_vpotBarInit = false;
        if (g_sync) g_sync->invalidate();

        for (int s = 0; s < 8; ++s) {
            const int rs = s + bankOffset;
            MediaTrack* t = visibleTrackAt(rs);
            const bool solo = t && GetMediaTrackInfo_Value(t, "I_SOLO")     > 0.5;
            const bool mute = t && GetMediaTrackInfo_Value(t, "B_MUTE")     > 0.5;
            const bool sel  = t && GetMediaTrackInfo_Value(t, "I_SELECTED") > 0.5;
            const bool arm  = t && GetMediaTrackInfo_Value(t, "I_RECARM")   > 0.5;
            // LED bank re-sync: push SEL/MUTE/SOLO for each strip's new
            // track. ARM LED ID mapping still unverified (cap23b needed)
            // so it's gated inside sendLed itself.
            if (g_dev) {
                const auto strip = static_cast<uint8_t>(s);
                sendLedFrames(uf8::buildLedColourPair(strip, uf8::LedClass::Sel,  sel,
                                                      ledColourFor(LedClass::Sel,  t)));
                sendLedFrames(uf8::buildLedColourPair(strip, uf8::LedClass::Cut,  mute,
                                                      ledColourFor(LedClass::Mute, t)));
                sendLedFrames(uf8::buildLedColourPair(strip, uf8::LedClass::Solo, solo,
                                                      ledColourFor(LedClass::Solo, t)));
                (void)arm;
            }
        }
        // After bank shift, g_slotTrack hasn't been refreshed yet (that
        // happens in the next loop), so compute the bitmask directly from
        // the new bank's tracks instead of pushSelectedStripBitmask().
        if (g_dev) {
            uint16_t mask = 0;
            for (int s = 0; s < 8; ++s) {
                const int rs = s + bankOffset;
                MediaTrack* t = visibleTrackAt(rs);
                if (t && GetMediaTrackInfo_Value(t, "I_SELECTED") > 0.5) {
                    mask |= static_cast<uint16_t>(1u << s);
                }
            }
            g_dev->send(uf8::buildSelectedStripBitmask(mask));
        }
    }

    for (int s = 0; s < 8; ++s) {
        const int realSlot = s + bankOffset;
        MediaTrack* tr = visibleTrackAt(realSlot);

        // Keep the slot→track mapping fresh so GetTouchState can map
        // REAPER's track pointer back to a strip index.
        g_slotTrack[s] = tr;

        // Send/Receive routing — when a routing mode is on, this strip's
        // V-Pot / fader / scribble pull from a send or receive instead
        // of the bank track. Resolved once per strip then queried below.
        const StripRoute vpotRoute  = resolveVpotRoute_(s, bankOffset, trackCount);
        const StripRoute faderRoute = resolveFaderRoute_(s, bankOffset, trackCount);
        const bool routedVpot  = vpotRoute.active();
        const bool routedFader = faderRoute.active();

        // FX Learn UF8: when Plugin Mode is on AND a learned plug-in is
        // focused, all 8 strips' Solo/Cut/Sel LEDs reflect the user-
        // binding state instead of the bank track's. Empty bindings
        // render off (Frank's "buttons ohne Funktion ausblenden"
        // request — strips without any function in the active mode
        // shouldn't pretend to show track state).
        bool userStripActive = false;
        UserPluginCtx userS{nullptr, -1, nullptr};
        int userBoundFader = -1;
        int userBoundSolo  = -1;
        int userBoundCut   = -1;
        int userBoundSel   = -1;
        int userBoundVpot  = -1;
        uint32_t userColourSolo = 0;
        uint32_t userColourCut  = 0;
        uint32_t userColourSel  = 0;
        if (g_uf8PluginMode.load()) {
            userS = userStripCtxFocused_();
            if (userS.map) {
                userStripActive = true;
                const auto& sb = userS.map->uf8.strips[s];
                userBoundFader = sb.faderVst3Param;
                userBoundSolo  = sb.soloVst3Param;
                userBoundCut   = sb.cutVst3Param;
                userBoundSel   = sb.selVst3Param;
                userColourSolo = sb.soloColour;
                userColourCut  = sb.cutColour;
                userColourSel  = sb.selColour;
                const int bank = std::clamp(g_softKeyBank.load(), 0, uf8::kUserUf8BankCount - 1);
                userBoundVpot  = userS.map->uf8.banks.banks[bank][s].vst3Param;
            }
        }

        // CUT LED — effective mute follows routing. Send/Receive mute
        // changes don't fire SetSurfaceMute, so we poll once per tick
        // and dedup against g_lastCutLed[s]. Empty strips (no track or
        // routed-but-invalid) render off. User-strip mode overrides
        // with the bound param's state.
        {
            bool effMute = false;
            if (userStripActive) {
                if (userBoundCut >= 0) {
                    effMute = TrackFX_GetParamNormalized(
                        userS.tr, userS.fxIdx, userBoundCut) >= 0.5;
                }
                // userBoundCut < 0 → effMute stays false → LED off.
            } else if (tr) {
                if (routedFader && faderRoute.valid) {
                    effMute = GetTrackSendInfo_Value(
                        faderRoute.track, faderRoute.sendCategory,
                        faderRoute.sendIndex, "B_MUTE") > 0.5;
                } else if (routedVpot && vpotRoute.valid) {
                    effMute = GetTrackSendInfo_Value(
                        vpotRoute.track, vpotRoute.sendCategory,
                        vpotRoute.sendIndex, "B_MUTE") > 0.5;
                } else if (!routedFader && !routedVpot) {
                    effMute = GetMediaTrackInfo_Value(tr, "B_MUTE") > 0.5;
                }
                // routedFader/routedVpot active but invalid → effMute stays
                // false (empty send slot, nothing to mute).
            }
            const int8_t cutKey = effMute ? 1 : 0;
            if (cutKey != g_lastCutLed[s]) {
                g_lastCutLed[s] = cutKey;
                // Colour follows the route target in routing modes so
                // the Cut LED matches the strip's colour bar (which is
                // already route-coloured via reaperColorForVisibleSlot).
                MediaTrack* colourTr = tr;
                if (userStripActive) colourTr = userS.tr;
                else if (routedFader && faderRoute.valid) {
                    if (auto* rt = routeTargetTrack_(faderRoute)) colourTr = rt;
                } else if (routedVpot && vpotRoute.valid) {
                    if (auto* rt = routeTargetTrack_(vpotRoute)) colourTr = rt;
                }
                uf8::LedColour cutCol = (userStripActive && userColourCut != 0)
                    ? uf8::ledColourForTrackRgb(userColourCut)
                    : ledColourFor(LedClass::Mute, colourTr);
                sendLedFrames(uf8::buildLedColourPair(
                    static_cast<uint8_t>(s), uf8::LedClass::Cut, effMute,
                    cutCol));
            }
        }

        // SOLO + SEL LEDs — only pushed per-tick in user-strip mode.
        // Outside user-strip mode, REAPER's SetSurfaceSolo / Selected
        // callbacks (event-driven via sendLed) handle these. We push
        // here when entering / cycling / clearing user-strip mode so
        // empty bindings go off. Cache resets to -1 on bank-shift to
        // force a re-push when leaving user-strip mode.
        if (userStripActive) {
            const bool soloOn = (userBoundSolo >= 0)
                && TrackFX_GetParamNormalized(
                       userS.tr, userS.fxIdx, userBoundSolo) >= 0.5;
            const int8_t soloKey = soloOn ? 1 : 0;
            if (soloKey != g_lastSoloLed[s]) {
                g_lastSoloLed[s] = soloKey;
                const uf8::LedColour soloCol = (userColourSolo != 0)
                    ? uf8::ledColourForTrackRgb(userColourSolo)
                    : ledColourFor(LedClass::Solo, userS.tr);
                sendLedFrames(uf8::buildLedColourPair(
                    static_cast<uint8_t>(s), uf8::LedClass::Solo, soloOn,
                    soloCol));
            }

            const bool selOn = (userBoundSel >= 0)
                && TrackFX_GetParamNormalized(
                       userS.tr, userS.fxIdx, userBoundSel) >= 0.5;
            const int8_t selKey = selOn ? 1 : 0;
            if (selKey != g_lastSelLed[s]) {
                g_lastSelLed[s] = selKey;
                const uf8::LedColour selCol = (userColourSel != 0)
                    ? uf8::ledColourForTrackRgb(userColourSel)
                    : ledColourFor(LedClass::Sel, userS.tr);
                sendLedFrames(uf8::buildLedColourPair(
                    static_cast<uint8_t>(s), uf8::LedClass::Sel, selOn,
                    selCol));
            }
        } else {
            // Reset cache so the next entry into user-strip mode (or a
            // bank shift) re-pushes against fresh data.
            g_lastSoloLed[s] = -1;
            g_lastSelLed[s]  = -1;
        }

        // Top-zone label + LED (FF 66 .. 04 + cells 0x18..0x1F).
        // Label = soft-key bank's row N. LED = bright when this strip's
        // bank position holds the currently-focused param, dim otherwise.
        // (UF8 manual p.174 "soft-key label" + cap41 LED decode.)
        //
        // Runs BEFORE the empty-strip branch so soft-key labels still
        // appear above strips whose bank position is past the last track
        // — labels are bank-global, not per-track. Synthetic toggles read
        // tr only inside `if (tr)` guards, so this block is safe when
        // tr == nullptr.
        {
            const auto domSk = (focused.domain == uf8::Domain::BusComp)
                ? uf8::Domain::BusComp : uf8::Domain::ChannelStrip;
            const int bankSk = std::clamp(g_softKeyBank.load(),
                0, softkey::maxBankFor(domSk));
            auto vSk = softkey::viewFor(domSk, bankSk);

            // FX Learn UF8: synthesise an 8-slot view from the focused
            // user-plug-in's bank table, overriding the hardcoded CS/BC
            // labels. labels[] points into per-tick thread-local
            // storage; linkIdx[] uses 0 = present / kNoSlot = empty so
            // the existing LED + label resolution downstream reads
            // "present" exactly when the user has bound the slot.
            static thread_local char  s_userBankLabelBuf[8][16];
            static thread_local const char* s_userBankLabelPtr[8];
            static thread_local int   s_userBankLink[8];
            if (g_uf8PluginMode.load()) {
                if (auto uctx = userStripCtxFocused_(); uctx.map) {
                    const int bank = std::clamp(g_softKeyBank.load(),
                        0, uf8::kUserUf8BankCount - 1);
                    for (int i = 0; i < 8; ++i) {
                        // TopSoftKey N's label is bank-N-scoped (Frank
                        // 2026-05-13: "soft-key press soll nicht die
                        // anderen soft-keys verändern"), so bank-switch
                        // doesn't rewrite the row. Read from
                        // topSoftKeyLeds[i] which is independent of the
                        // currently active bank.
                        std::string lbl =
                            uctx.map->uf8.topSoftKeyLeds[i].label;
                        if (lbl.size() > 11) lbl.resize(11);
                        std::snprintf(s_userBankLabelBuf[i],
                            sizeof(s_userBankLabelBuf[i]),
                            "%s", lbl.c_str());
                        s_userBankLabelPtr[i] = s_userBankLabelBuf[i];
                        // linkIdx surfaces "present vs empty" for the
                        // existing LED resolver. With bank-scoped
                        // TopSoftKey labels the per-(bank, strip) V-Pot
                        // mapping no longer drives the TopSoftKey label
                        // — keep linkIdx=present so any param assigned
                        // to the strip column in the active bank still
                        // counts as "bound" downstream.
                        const auto& bs =
                            uctx.map->uf8.banks.banks[bank][i];
                        s_userBankLink[i] =
                            (bs.vst3Param >= 0) ? 0 : softkey::kNoSlot;
                    }
                    vSk.labels  = s_userBankLabelPtr;
                    vSk.linkIdx = s_userBankLink;
                }
            }

            // User-Quick override: when a Quick is engaged on the
            // current layer, each top-soft-key shows the matching
            // (layer, quick, sub-bank, slot) binding's label instead
            // of the plugin-driven param label. The LED tracks slot
            // populated/empty rather than focused-param state.
            //
            // UF8 Plugin Mode bypasses this entirely — the top-soft-
            // key row hosts the FX-Learn-mapped plug-in's params, and
            // letting a stale activeQuick paint over them would hide
            // the user's gelearnte parameter (Frank 2026-05-13).
            const int curLayer  = uf8::bindings::getActiveLayer();
            const bool pluginModeLocal = g_uf8PluginMode.load();
            const int curQuick  = (!pluginModeLocal
                                   && curLayer >= 0 && curLayer <= 2)
                                  ? g_activeQuick[curLayer].load()  : -1;
            const int curSub    = (curLayer >= 0 && curLayer <= 2)
                                  ? g_activeSubBank[curLayer].load() : 0;
            std::string userLabel;
            bool userBankSlotPresent = false;
            if (curQuick >= 0) {
                const auto userSlot = uf8::bindings::getUserQuickSlot(
                    curLayer, curQuick, curSub, s);
                userLabel = userSlot.label;
                const auto& sp = userSlot.shortPress[
                    static_cast<int>(uf8::bindings::Modifier::Plain)];
                userBankSlotPresent =
                    sp.type != uf8::bindings::ActionType::Noop
                    || !sp.action.empty();
                if (userLabel.empty() && userBankSlotPresent) {
                    userLabel = sp.action;
                    if (userLabel.size() > 8) userLabel.resize(8);
                }
            }

            // Per-binding label override — top wins, fall through to
            // the user-bank / SSL-plugin default. Resolution order:
            //   1. shortPress[currentModifier].label  (if non-empty)
            //   2. shortPress[Plain].label            (if non-empty)
            //   3. user-bank slot label (when a user bank is active)
            //   4. SSL plug-in's softkey::viewFor label
            // Lets the user type a custom name in the editor and have
            // it show on the LCD, AND have a different name appear
            // when Shift / Cmd / Ctrl is held (one binding can carry a
            // separate label per modifier slot).
            std::string label;
            {
                static const uf8::bindings::ButtonId kTskIds[8] = {
                    uf8::bindings::ButtonId::TopSoftKey1,
                    uf8::bindings::ButtonId::TopSoftKey2,
                    uf8::bindings::ButtonId::TopSoftKey3,
                    uf8::bindings::ButtonId::TopSoftKey4,
                    uf8::bindings::ButtonId::TopSoftKey5,
                    uf8::bindings::ButtonId::TopSoftKey6,
                    uf8::bindings::ButtonId::TopSoftKey7,
                    uf8::bindings::ButtonId::TopSoftKey8,
                };
                const auto bd = uf8::bindings::getBinding(
                    uf8::bindings::getActiveLayer(), kTskIds[s]);
                const int mIdx = static_cast<int>(curModifier);
                if (mIdx >= 0 && mIdx < uf8::bindings::kModifierCount
                    && !bd.shortPress[mIdx].label.empty()) {
                    label = bd.shortPress[mIdx].label;
                } else {
                    const auto& spPlain = bd.shortPress[
                        static_cast<int>(uf8::bindings::Modifier::Plain)];
                    if (!spPlain.label.empty()) {
                        label = spPlain.label;
                    }
                }
            }
            if (label.empty()) {
                label = (curQuick >= 0)
                    ? userLabel : std::string(vSk.labels[s]);
            }
            // Pad to 12 chars centred (leading + trailing spaces) so
            //  - shorter / empty labels actively overwrite any longer
            //    residue left in the LCD zone from the previous bank;
            //  - the firmware doesn't left-justify our padded output
            //    (which broke centring after the original space-pad
            //    fix that only added trailing spaces).
            // An empty payload (`FF 66 02 04 <strip>`) would flip the
            // strip into "slot empty" mode and darken the colour bar —
            // not what we want; we just want the label cleared.
            if (label.size() < 12) {
                const size_t pad = 12 - label.size();
                const size_t lead = pad / 2;
                label = std::string(lead, ' ') + label
                      + std::string(pad - lead, ' ');
            }
            const int slotLink = (curQuick >= 0)
                ? (userBankSlotPresent ? 0 : softkey::kNoSlot)
                : vSk.linkIdx[s];
            // Synthetic toggle columns: read the per-strip state directly
            // (not the focused state) so each column's LED reflects the
            // toggle's actual on/off for THIS strip's track. Only ONE
            // strip per bank carries a synthetic, so at most one chunk
            // read per render tick — same cost as a normal param.
            bool isToggleCell = false;
            int  toggleOn = 0;
            if (slotLink == uf8::ext::TrackPhase) {
                isToggleCell = true;
                if (tr) {
                    toggleOn = (GetMediaTrackInfo_Value(tr, "B_PHASE") > 0.5) ? 1 : 0;
                }
            } else if (slotLink == uf8::ext::PluginAB) {
                isToggleCell = true;
                if (tr) {
                    int ab = -1, hq = -1;
                    uf8::readPluginToggleStates(tr, ab, hq);
                    toggleOn = (ab == 0) ? 1 : 0;  // bright = comparing (B active)
                }
            } else if (slotLink == uf8::ext::PluginHQ) {
                isToggleCell = true;
                if (tr) {
                    int ab = -1, hq = -1;
                    uf8::readPluginToggleStates(tr, ab, hq);
                    toggleOn = (hq == 1) ? 1 : 0;
                }
            }
            uf8::TopSoftKeyState tssk;
            int8_t ledCacheKey;
            if (pluginModeLocal) {
                // UF8 Plugin Mode: TopSoftKey N = bank-N selector.
                // Active bank bright, assigned-but-inactive dim,
                // unassigned banks Off (Frank 2026-05-13: "unzu-
                // gewiesene Soft-Key V-Pot banks no-function" →
                // visually dark so the user sees which banks are
                // empty and can't navigate there).
                const int activeBank = std::clamp(
                    g_softKeyBank.load(), 0,
                    uf8::kUserUf8BankCount - 1);
                bool bankAssigned = false;
                if (auto uctxBank = userStripCtxFocused_(); uctxBank.map) {
                    for (int sIdx = 0; sIdx < 8; ++sIdx) {
                        if (uctxBank.map->uf8.banks
                            .banks[s][sIdx].vst3Param >= 0)
                        {
                            bankAssigned = true; break;
                        }
                    }
                }
                const bool isActive = (s == activeBank);
                if (!bankAssigned) {
                    tssk = uf8::TopSoftKeyState::Off;
                    ledCacheKey = 11;
                } else if (isActive) {
                    tssk = uf8::TopSoftKeyState::On;
                    ledCacheKey = 10;
                } else {
                    tssk = uf8::TopSoftKeyState::Dim;
                    ledCacheKey = 9;
                }
            } else if (curQuick >= 0) {
                // User-Quick context. Three-tier rendering:
                //   empty slot                       → Dim (row stays
                //                                       visibly populated)
                //   filled, action's toggle ON       → On
                //   filled, no active toggle state   → Dim
                // Previously any filled slot rendered permanent-Bright
                // (Frank 2026-05-13: "user-defined leuchten immer hell,
                // bringen sie nicht auf dim") which made stateless one-
                // shot bindings indistinguishable from latched toggles.
                bool slotActive = false;
                if (userBankSlotPresent) {
                    const auto userSlot = uf8::bindings::getUserQuickSlot(
                        curLayer, curQuick, curSub, s);
                    slotActive = bindingHasActiveSlot_(userSlot);
                }
                tssk = slotActive
                    ? uf8::TopSoftKeyState::On
                    : uf8::TopSoftKeyState::Dim;
                ledCacheKey = static_cast<int8_t>(slotActive ? 8 : 7);
            } else if (isToggleCell) {
                // Bright when this synthetic-toggle slot is the focused
                // parameter OR the toggle's own state is "on". Frank
                // 2026-05-07: previously the Phase soft-key LED only
                // followed B_PHASE state, so pressing the soft-key
                // (which sets focus to TrackPhase) didn't light up
                // unless B_PHASE was already inverted — breaking the
                // "focused = bright" convention every other CS soft-key
                // follows.
                const bool focusHit =
                    (slotLink != softkey::kNoSlot
                     && slotLink == focused.slotIdx);
                const bool bright = focusHit || toggleOn;
                tssk = bright ? uf8::TopSoftKeyState::On
                              : uf8::TopSoftKeyState::Dim;
                ledCacheKey = static_cast<int8_t>(bright ? 6 : 5);
            } else if (slotLink != softkey::kNoSlot && slotLink == focused.slotIdx) {
                tssk = uf8::TopSoftKeyState::On;         // bright = focused
                ledCacheKey = 2;
            } else {
                // All other strips — including kNoSlot positions — render
                // dim so the row stays visibly populated. Lighting only
                // strips with a wired slot would leave gaps the user
                // reads as "broken LEDs". Per user instruction 2026-04-30:
                // bright only when the parameter is selected.
                tssk = uf8::TopSoftKeyState::Dim;
                ledCacheKey = 1;
            }
            // Per-TopSoftKey LED colour resolution. Three sources, in
            // order of precedence:
            //   1. UF8 Plugin Mode: each TopSoftKey N (= bank N selector)
            //      carries one bank-scoped colour in
            //      uf8.topSoftKeyLeds[N].colour. Brightness is fixed —
            //      active bank Bright, inactive Dim (Frank 2026-05-13).
            //   2. User-Quick context: the user-defined slot Binding's
            //      colour / inactiveColor; picked by `tssk`.
            //   3. Layer-level Binding on TopSoftKey1..8 (the per-button
            //      override the user can edit in the Bindings editor —
            //      Frank 2026-05-14 "LED Farbe für Soft-Keys werden
            //      nicht übernommen von bindings"). Same active /
            //      inactive split.
            // Falls back to white if nothing overrides.
            uf8::LedColour ledClr = uf8::ledColourWhite();
            uint32_t ledColRgb = 0xFFFFFFu;
            auto packRgb_ = [](const uint8_t (&c)[3]) -> uint32_t {
                return (uint32_t(c[0]) << 16)
                     | (uint32_t(c[1]) << 8)
                     |  uint32_t(c[2]);
            };
            const bool wantActive = (tssk == uf8::TopSoftKeyState::On);
            if (pluginModeLocal) {
                if (auto uctx = userStripCtxFocused_(); uctx.map) {
                    const auto& tl = uctx.map->uf8.topSoftKeyLeds[s];
                    const uint32_t bc = tl.colour & 0x00FFFFFFu;
                    if (bc != 0) {
                        ledColRgb = bc;
                        ledClr = uf8::ledColourForTrackRgb(bc);
                    }
                }
            } else if (curQuick >= 0 && userBankSlotPresent) {
                const auto userSlot = uf8::bindings::getUserQuickSlot(
                    curLayer, curQuick, curSub, s);
                const uint32_t bc = wantActive
                    ? packRgb_(userSlot.color)
                    : packRgb_(userSlot.inactiveColor);
                ledColRgb = bc;
                ledClr = uf8::ledColourForTrackRgb(bc);
            } else {
                static const uf8::bindings::ButtonId kTskIdsForLed[8] = {
                    uf8::bindings::ButtonId::TopSoftKey1,
                    uf8::bindings::ButtonId::TopSoftKey2,
                    uf8::bindings::ButtonId::TopSoftKey3,
                    uf8::bindings::ButtonId::TopSoftKey4,
                    uf8::bindings::ButtonId::TopSoftKey5,
                    uf8::bindings::ButtonId::TopSoftKey6,
                    uf8::bindings::ButtonId::TopSoftKey7,
                    uf8::bindings::ButtonId::TopSoftKey8,
                };
                const auto bid = kTskIdsForLed[s];
                if (uf8::bindings::hasBinding(curLayer, bid)) {
                    const auto bd =
                        uf8::bindings::getBinding(curLayer, bid);
                    const uint32_t bc = wantActive
                        ? packRgb_(bd.color)
                        : packRgb_(bd.inactiveColor);
                    ledColRgb = bc;
                    ledClr = uf8::ledColourForTrackRgb(bc);
                }
            }
            // Cache key folds tssk state + 24-bit colour into a single
            // hash so colour changes re-emit even when state is steady.
            const int32_t composite =
                (int32_t(ledCacheKey) << 24)
              ^ (int32_t(static_cast<int>(tssk)) << 28)
              ^ static_cast<int32_t>(ledColRgb & 0x00FFFFFFu);
            // g_lastTopSoftKey is int8_t — keep just a hash; identical
            // composites short-circuit, distinct composites push.
            const int8_t shortKey =
                static_cast<int8_t>(((composite >> 16) ^ composite) & 0x7F);
            if (shortKey != g_lastTopSoftKey[s]) {
                g_lastTopSoftKey[s] = shortKey;
                sendLedFrames(uf8::buildTopSoftKeyLed(
                    static_cast<uint8_t>(s), tssk, ledClr));
            }
            if (label != g_lastSlotLabel[s]) {
                g_lastSlotLabel[s] = label;
                g_dev->send(uf8::buildPluginSlotName(static_cast<uint8_t>(s), label));
            }
        }

        // Empty strip — either the bank window extends past the last
        // track (no `tr`) OR a routing mode is active and this strip's
        // send/receive slot doesn't exist on the source track (e.g. in
        // "Sends of Focused Track" mode, focused track has only 4 sends
        // → strips 5..8 are empty send slots). In both cases blank every
        // per-track zone so the last bucket's residue doesn't linger;
        // without this, the strip continues to show the bank track's
        // name + colour with a -inf fader.
        //
        // Dedup subtlety: the bankChanged branch above clears every
        // g_last* cache to "". That means after a bank shift, "cache ==
        // target" is indistinguishable between "display is already
        // blank" and "display state unknown, need to push". Force the
        // first-tick push via bankChanged so the blanks actually reach
        // the device; subsequent ticks dedup normally.
        //
        // Slot-label is NOT cleared here — the soft-key block above
        // already pushed the bank's label for this strip. Empty strips
        // must keep the label visible (labels are bank-global).
        const bool routedButInvalid =
            (routedFader && !faderRoute.valid)
         || (routedVpot  && !vpotRoute.valid);
        if (!tr || routedButInvalid) {
            const std::string blankCs   = "";   // empty → NUL-padded to width
            const std::string blankDb   = "    ";
            const std::string blankName = "       ";   // 7 spaces — overwrites any
                                                        // residual text. Empty payload
                                                        // doesn't reliably clear the
                                                        // StripTextUpper zone (the
                                                        // firmware retains last text);
                                                        // a space-pad always wins.
            const std::string blankVal  = std::string(19, ' ');
            const std::string blankCh   = "  ";
            if (bankChanged || g_lastCsType[s] != blankCs) {
                g_lastCsType[s] = blankCs;
                g_dev->send(uf8::buildChannelStripType(static_cast<uint8_t>(s), blankCs));
            }
            if (bankChanged || g_lastChanNum[s] != blankCh) {
                g_lastChanNum[s] = blankCh;
                g_dev->send(uf8::buildChannelNumber(static_cast<uint8_t>(s), blankCh));
            }
            if (bankChanged || g_lastTrackName[s] != blankName) {
                g_lastTrackName[s] = blankName;
                g_dev->send(uf8::buildStripTextUpper(static_cast<uint8_t>(s), blankName));
            }
            if (bankChanged || g_lastFaderDb[s] != blankDb) {
                g_lastFaderDb[s] = blankDb;
                g_dev->send(uf8::buildFaderDbReadout(static_cast<uint8_t>(s), blankDb));
            }
            if (bankChanged || g_lastValueLine[s] != blankVal) {
                g_lastValueLine[s] = blankVal;
                g_dev->send(uf8::buildValueLine(static_cast<uint8_t>(s), blankVal));
            }

            // Routing-mode-active + invalid slot → park the motor at
            // -INF (pb14 = 0) for the duration of the mode. The fader
            // physically moves to the bottom so the user sees at a
            // glance that this strip's send/receive doesn't exist.
            // Skipped while the user is touching the fader (so a
            // simultaneous touch isn't fought by the motor); next
            // render tick after release re-pushes 0. When the routing
            // mode exits, the regular render path takes over and
            // drives the fader to the track's actual volume.
            if (routedButInvalid && !g_touchReported[s].load()) {
                if (!g_faderPbInit || g_lastFaderPb[s] != 0) {
                    g_lastFaderPb[s] = 0;
                    g_dev->send(uf8::buildFaderPosition(
                        static_cast<uint8_t>(s), 0, 0));
                }
            }

            vpotBar[s] = 0;
            continue;
        }

        // Resolve the SSL plug-in (if any) and the currently-paged slot
        // for this strip. `slot == nullptr` means "no SSL plug-in on
        // this track" OR "plug-in has fewer slots than the current page
        // demands" (e.g. BC2 on page 8 — only 7 slots exist). In both
        // cases we fall back to the REAPER-native display (track name +
        // volume).
        int fxIdx = -1;
        const uf8::LinkSlot* slot = slotForStrip(tr, focused, &fxIdx);
        // Plug-in map for the CS Type zone — derived from track presence
        // alone (NOT gated on `slot`). The Type zone should reflect what
        // SSL plug-in is on the strip's track regardless of whether a
        // specific slot is currently focused. Without this decoupling,
        // a fresh-load focused.slotIdx=-1 collapses the lookup and
        // every strip reads "RPR" until the user toggles Q1→Q2→Q1 to
        // bump slotIdx to 0.
        const uf8::PluginMap* map = nullptr;
        {
            // SSL Strip Mode (Plugin button) routes the fader to the CS
            // instance's Fader Level param. The colour-bar Channel Strip
            // Type zone should reflect what the fader actually drives,
            // so force CS lookup regardless of UC1 focus domain when
            // pluginFaderMode is on. Outside SSL Strip Mode the zone
            // follows focused.domain as before (Frank 2026-05-12 "auf
            // UF8 color-bars die instanz anzeigen, die kontrolliert
            // wird").
            //
            // In SSL Strip Mode, csForStripModeOnTrack_ applies the
            // isDefault tiebreak so the Type zone tracks the same
            // user-CS that csFaderForTrack/csPanForTrack route to —
            // not the instance-cycle CS, which would desync the label
            // from the actual fader target on tracks with multiple CS
            // plug-ins (Frank 2026-05-14).
            if (g_pluginFaderMode.load()) {
                const auto pick = csForStripModeOnTrack_(tr);
                map = pick.map;
                if (!map) {
                    auto mm2 = uf8::lookupPluginOnTrack(tr, uf8::Domain::BusComp);
                    map = mm2.map;
                }
            } else {
                const auto typeDom = focused.domain;
                auto mm = uf8::lookupPluginOnTrack(tr, typeDom);
                map = mm.map;
                if (!map) {
                    const auto otherDom = (typeDom == uf8::Domain::BusComp)
                        ? uf8::Domain::ChannelStrip
                        : uf8::Domain::BusComp;
                    auto mm2 = uf8::lookupPluginOnTrack(tr, otherDom);
                    map = mm2.map;
                }
            }
        }

        // Channel Strip Type zone: the plug-in's short name if
        // recognised, else a REAPER mnemonic. Pass the natural text
        // length — buildChannelStripType pads with NULs internally so
        // the trailing LCD cells render blank instead of showing space
        // characters past the text (Frank 2026-05-09).
        //
        // Frank 2026-05-14: plug-in / FX names belong here in the
        // colour bar, NEVER in the upper track-name zone. Two modes
        // override the default CS/BC variant label:
        //   - SSL Strip Mode (g_pluginFaderMode): the CS variant name
        //     must show while the mode is active, even with no UC1
        //     focus, and even if Instance Selection Mode is also
        //     active — SSL Strip wins because the fader is literally
        //     routed to the CS instance's Fader Level param, so the
        //     colour bar must reflect that target.
        //   - Instance Selection Mode: each strip's colour bar shows
        //     that strip's active FX (rotates with the strip's own
        //     V-Pot). Encoder Instance Cycle only updates the focused
        //     track's g_stripInstanceFxIdx (see applyInstanceCycle_),
        //     so it visibly moves only the selected strip — other
        //     strips stay parked on whatever their V-Pots have dialed
        //     in (Frank 2026-05-14 "Encoder Instance Cycle soll nur
        //     beim selected channel" AND "v-pots → instance cycle
        //     geht nicht mit nur v-pot wenn track nicht selected").
        std::string csType;
        if (userStripActive) {
            csType = userS.map->displayShort;
        } else if (g_pluginFaderMode.load()) {
            if (map) csType = map->displayShort;
        } else if (g_selectionMode.load() == SelectionMode::Instance) {
            const int instFxIdx = stripInstanceActiveFx_(tr);
            if (instFxIdx >= 0) csType = fxCycleDisplayName_(tr, instFxIdx);
            if (csType.empty()) csType = "-";
        } else if (focused.domain == uf8::Domain::None) {
            auto uf8Ctx = findUserPluginOnTrack_(tr, uf8::Domain::None);
            if (uf8Ctx.map) csType = uf8Ctx.map->displayShort;
        } else {
            csType = map ? std::string(map->displayShort) : std::string{};
        }
        if (csType.empty()) csType = "REAPER";
        if (csType.size() > 7) csType.resize(7);
        if (csType != g_lastCsType[s]) {
            g_lastCsType[s] = csType;
            g_dev->send(uf8::buildChannelStripType(static_cast<uint8_t>(s), csType));
        }

        // (Top-zone soft-key label + LED block lives BEFORE the empty-strip
        // branch above — it must run for every strip in the bank, not just
        // strips that map to a track.)

        // Channel Number Zone — the tiny digit top-left of each strip's
        // color bar. REAPER track index is 0-based; UF8 expects 1-based
        // ASCII. In user-strip mode, blank when the strip has no user
        // bindings at all (Frank's "buttons ohne Funktion ausblenden").
        {
            std::string chan;
            const bool stripHasUserFunction = userStripActive
                && (userBoundFader >= 0 || userBoundVpot >= 0
                 || userBoundSolo  >= 0 || userBoundCut  >= 0
                 || userBoundSel   >= 0);
            if (userStripActive && !stripHasUserFunction) {
                chan = "  ";
            } else {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "%d", realSlot + 1);
                chan = buf;
            }
            if (chan != g_lastChanNum[s]) {
                g_lastChanNum[s] = chan;
                g_dev->send(uf8::buildChannelNumber(static_cast<uint8_t>(s), chan));
            }
        }

        // FLIP active on this strip = global flip + a usable plug-in
        // slot. Without a slot there's no parameter to flip onto the
        // fader, so the strip falls back to normal mode silently.
        const bool flipActive = g_flip.load() && slot && fxIdx >= 0;

        // FLIP without a plug-in slot to swap onto: V-Pots show volume,
        // faders show pan (the simple swap). Used to require holding the
        // PAN button (g_forcePan); Frank 2026-05-08 wants FLIP alone to
        // be enough — the swap is exactly what FLIP means without other
        // context. plugin-fader mode still wins when active (its fader
        // role is explicit and overrides the swap).
        const bool flipPanSwap = g_flip.load()
                              && !flipActive
                              && !g_pluginFaderMode.load()
                              && !g_uf8PluginMode.load();

        struct UserFaderHandle {
            MediaTrack* tr; int fxIdx; int vst3Param; bool inverted;
        };
        UserFaderHandle userF{nullptr, -1, -1, false};
        if (g_uf8PluginMode.load() && !flipActive) {
            if (auto uctx = userStripCtxFocused_(); uctx.map) {
                const auto& sb = uctx.map->uf8.strips[s];
                if (sb.faderVst3Param >= 0) {
                    userF = { uctx.tr, uctx.fxIdx,
                              sb.faderVst3Param, sb.faderInverted };
                }
            }
        }
        const bool userFaderActive = (userF.vst3Param >= 0);

        // Plugin-fader mode active on this strip = global plugin-fader
        // toggle ON + a CS plug-in is loaded. FLIP wins if both are on
        // (FLIP is per-strip and explicit; plugin-fader is global).
        // User-fader wins over built-in CS-fader.
        const auto cs = (g_pluginFaderMode.load() && !flipActive
                          && !userFaderActive)
                          ? csFaderForTrack(tr) : CsFaderHandle{-1, -1};
        const bool csFaderActive = cs.vst3Param >= 0;

        // V-Pot Readout Bar position. Binary toggle slots render as
        // full (0xFF) or empty (0x00) — no in-between gradient; their
        // V-Pot push toggles the param (handled in PanCenter). Other
        // plugin params + pan render as a single-dot indicator at the
        // normalised position. The mode register (FF 66 09 0D, set
        // below) stays at 0x01 so the firmware draws a single line
        // instead of the linear-fill animation mode 0x02 produces.
        // Decision order (top wins):
        //   0. Send/Receive routing → V-Pot shows the routed level.
        //   1. FLIP        → V-Pot mirrors track volume.
        //   2. forcePan    → V-Pot is REAPER track pan, period. Overrides
        //                    Plugin-mode and any focus, since the user
        //                    just pressed PAN to *demand* REAPER pan.
        //   3. focused slot resolves on this track → drive that param.
        //   4. focused but unavailable here → blank (collapsed bar).
        //   5. Plugin mode → SSL strip Pan (linkIdx 3).
        //   6. default     → REAPER track pan.
        if (routedFader) {
            // Fader carries the route's volume → V-Pot is free for pan.
            // FLIP swaps: fader=pan, V-Pot=volume. FLIP+PAN held shows
            // the track's own pan on the V-Pot (transient overlay).
            if (faderRoute.valid) {
                if (g_flip.load() && g_forcePan.load()) {
                    const double pan = GetMediaTrackInfo_Value(tr, "D_PAN");
                    vpotBar[s] = vpotPosFromPan(pan);
                } else if (g_flip.load()) {
                    const double volLin = GetTrackSendInfo_Value(
                        faderRoute.track, faderRoute.sendCategory,
                        faderRoute.sendIndex, "D_VOL");
                    const uint16_t pbVol = linearVolumeToPb(volLin);
                    vpotBar[s] = vpotPosFromUnipolar(
                        static_cast<double>(pbVol) / 16383.0);
                } else {
                    const double pan = GetTrackSendInfo_Value(
                        faderRoute.track, faderRoute.sendCategory,
                        faderRoute.sendIndex, "D_PAN");
                    vpotBar[s] = vpotPosFromPan(pan);
                }
            } else {
                vpotBar[s] = (uint16_t{0x00} | (uint16_t{0x80} << 8));
            }
        } else if (routedVpot) {
            if (vpotRoute.valid) {
                if (g_forcePan.load()) {
                    // PAN held in routing mode → V-Pot bar shows the
                    // routed entity's D_PAN as a bipolar centre-out
                    // sweep (matches the input handler that writes
                    // D_PAN instead of D_VOL when PAN is held).
                    const double pan = GetTrackSendInfo_Value(
                        vpotRoute.track, vpotRoute.sendCategory,
                        vpotRoute.sendIndex, "D_PAN");
                    vpotBar[s] = vpotPosFromPan(pan);
                } else {
                    const double volLin = readRouteVolumeLinear_(vpotRoute, tr);
                    const uint16_t pbVol = linearVolumeToPb(volLin);
                    vpotBar[s] = vpotPosFromUnipolar(
                        static_cast<double>(pbVol) / 16383.0);
                }
            } else {
                // Routed but the send/receive doesn't exist on this track —
                // collapsed bar so the user sees the slot is empty.
                vpotBar[s] = (uint16_t{0x00} | (uint16_t{0x80} << 8));
            }
        } else if (flipActive || flipPanSwap) {
            // FLIP: V-Pot reads track volume. Map pb14 → 0..100 unipolar.
            // Same path for the FLIP+PAN swap (no plug-in slot needed).
            const double volLinFlip = uiVolLinear(tr);
            const uint16_t pbVol = linearVolumeToPb(volLinFlip);
            vpotBar[s] = vpotPosFromUnipolar(
                static_cast<double>(pbVol) / 16383.0);
        } else if (g_uf8PluginMode.load() && !flipActive
                && [&]{ if (auto u = userStripCtxFocused_(); u.map) return true; return false; }())
        {
            // FX Learn UF8: user-bank V-Pot bar. Toggle slots show
            // collapsed-bar centre marker for OFF / full-positive for
            // ON. Value slots show unipolar L→R sweep on the bound
            // param. Empty slots show collapsed-bar.
            auto uctx = userStripCtxFocused_();
            const int bank = std::clamp(g_softKeyBank.load(), 0, uf8::kUserUf8BankCount - 1);
            const auto& bs = uctx.map->uf8.banks.banks[bank][s];
            if (bs.vst3Param < 0) {
                vpotBar[s] = (uint16_t{0x00} | (uint16_t{0x80} << 8));
            } else {
                const double norm = TrackFX_GetParamNormalized(
                    uctx.tr, uctx.fxIdx, bs.vst3Param);
                if (bs.vpotMode == uf8::VPotMode::Toggle) {
                    vpotBar[s] = (norm >= 0.5)
                        ? static_cast<uint16_t>(0x7F)
                        : (uint16_t{0x00} | (uint16_t{0x80} << 8));
                } else {
                    const double v = bs.inverted ? 1.0 - norm : norm;
                    vpotBar[s] = vpotPosFromUnipolar(v);
                }
            }
        } else if (g_forcePan.load()) {
            const double pan = GetMediaTrackInfo_Value(tr, "D_PAN");
            vpotBar[s] = vpotPosFromPan(pan);
        } else if (slot && fxIdx >= 0 && !isVPotPanFocus(focused)) {
            const double norm = TrackFX_GetParamNormalized(tr, fxIdx, slot->vst3Param);
            const double visual = slot->inverted ? 1.0 - norm : norm;
            if (isBinarySlot(*slot)) {
                // ON = max positive (0x7F in signed). OFF = 0x00 +
                // byte1=0x80 (centre marker = collapsed bar).
                vpotBar[s] = (norm >= 0.5)
                    ? static_cast<uint16_t>(0x7F)
                    : (uint16_t{0x00} | (uint16_t{0x80} << 8));
            } else if (isBipolarSlot(*slot)) {
                vpotBar[s] = vpotPosFromBipolar(visual * 2.0 - 1.0);
            } else {
                vpotBar[s] = vpotPosFromUnipolar(visual);
            }
        } else if (focused.slotIdx != -1 && !isVPotPanFocus(focused)) {
            // A param is focused but this strip's plug-in doesn't have
            // it (e.g. IMP IN focused while track hosts CS 2). Render
            // the V-Pot blank so the user isn't misled.
            vpotBar[s] = (uint16_t{0x00} | (uint16_t{0x80} << 8));
        } else if (g_pluginFaderMode.load()) {
            const auto pn = csPanForTrack(tr);
            if (pn.vst3Param >= 0) {
                const double norm = TrackFX_GetParamNormalized(
                    tr, pn.fxIndex, pn.vst3Param);
                vpotBar[s] = vpotPosFromBipolar(norm * 2.0 - 1.0);
            } else {
                const double pan = GetMediaTrackInfo_Value(tr, "D_PAN");
                vpotBar[s] = vpotPosFromPan(pan);
            }
        } else {
            const double pan = GetMediaTrackInfo_Value(tr, "D_PAN");
            vpotBar[s] = vpotPosFromPan(pan);
        }

        // Upper scribble row — track name (max 7 chars). In Send/Receive
        // routing mode the strip represents the routed entity, so show
        // the send/receive name here (Frank 2026-05-07: each strip's
        // upper line should reflect the routing target since the user
        // is editing the route, not the bank track). Falls back to bank
        // track P_NAME outside routing. SSL Strip Mode keeps P_NAME
        // here — the CS variant ("CS 2" / "4K G" / "Link" / user
        // displayShort) lives in the Channel Strip Type colour bar
        // above, not in the track-name zone (Frank 2026-05-12).
        {
            std::string n;
            if (routedFader || routedVpot) {
                const StripRoute& r = routedFader ? faderRoute : vpotRoute;
                n = routeName_(r);
            }
            // FX Learn UF8: when a user-mapped fader binding exists for
            // this strip, show the bound param's name instead of the
            // track name. Lets the user see at a glance which param
            // each fader drives.
            if (userFaderActive) {
                char pn[64] = {0};
                TrackFX_GetParamName(userF.tr, userF.fxIdx,
                    userF.vst3Param, pn, sizeof(pn));
                if (pn[0]) n = pn;
            }
            // FX Learn UF8: in user-strip mode without a fader binding
            // for this strip, blank the upper scribble — Frank's
            // "channel displays ohne Funktion ausblenden". Otherwise
            // we'd be showing the bank track's name on a strip that
            // doesn't actually control that track in the active mode.
            const bool blankInUserStripMode = userStripActive
                && !userFaderActive;
            if (n.empty() && !blankInUserStripMode) {
                char name[256] = {0};
                GetSetMediaTrackInfo_String(tr, "P_NAME", name, false);
                n = name;
            }
            if (n.empty() && !blankInUserStripMode) {
                char fallback[8];
                std::snprintf(fallback, sizeof(fallback), "CH %d", realSlot + 1);
                n = fallback;
            }
            if (blankInUserStripMode) {
                n = "       ";   // 7 spaces, matches blank-strip path
            }
            if (n.size() > 7) n.resize(7);
            if (n != g_lastTrackName[s]) {
                g_lastTrackName[s] = n;
                g_dev->send(uf8::buildStripTextUpper(static_cast<uint8_t>(s), n));
            }
        }

        // O/PdB Fader Readout — normally track volume in dB. In FLIP
        // mode this zone shows the focused plug-in parameter's
        // formatted value (truncated to 6 chars to fit the zone), so
        // the fader has its own readout matching what it now drives.
        // In Plugin-Fader mode the readout shows the SSL strip's
        // internal Fader Level dB instead of REAPER's track volume.
        // In a Send/Receive routing mode the fader controls the
        // routed level — show its dB value.
        double volLin;
        if (routedFader && faderRoute.valid) {
            volLin = readRouteVolumeLinear_(faderRoute, tr);
        } else if (routedFader) {
            volLin = 0.0;       // routed but slot doesn't exist → -inf dB
        } else {
            volLin = uiVolLinear(tr);
        }
        std::string dbStr;
        if (routedFader) {
            dbStr = formatDbReadout(volLin);
        } else if (userFaderActive) {
            char paramBuf[64] = {0};
            const double norm = TrackFX_GetParamNormalized(
                userF.tr, userF.fxIdx, userF.vst3Param);
            const double v = userF.inverted ? 1.0 - norm : norm;
            TrackFX_FormatParamValueNormalized(userF.tr, userF.fxIdx,
                userF.vst3Param, v, paramBuf, sizeof(paramBuf));
            // Same sanitisation as the flipActive/csFaderActive branch
            // below — fall through to the same post-processing block.
            std::string s2(paramBuf);
            for (size_t p = 0; p + 2 < s2.size(); ) {
                if (static_cast<unsigned char>(s2[p])     == 0xE2 &&
                    static_cast<unsigned char>(s2[p + 1]) == 0x88 &&
                    static_cast<unsigned char>(s2[p + 2]) == 0x9E) {
                    s2.replace(p, 3, "INF"); p += 3;
                } else ++p;
            }
            for (auto& c : s2) {
                const unsigned char u = static_cast<unsigned char>(c);
                if (u < 0x20 || u > 0x7E) c = '-';
            }
            while (!s2.empty() && s2.front() == ' ') s2.erase(0, 1);
            if (s2.size() > 6) s2.resize(6);
            dbStr = s2;
        } else if (flipActive || csFaderActive) {
            char paramBuf[64] = {0};
            const int useFx = flipActive ? fxIdx : cs.fxIndex;
            const int useParam = flipActive ? slot->vst3Param : cs.vst3Param;
            const double norm = TrackFX_GetParamNormalized(tr, useFx, useParam);
            TrackFX_FormatParamValueNormalized(tr, useFx, useParam,
                                               norm, paramBuf, sizeof(paramBuf));
            std::string s2(paramBuf);
            // Squash UTF-8 ∞ then non-printable, trim leading spaces — same
            // sanitisation as the value-line path so the LCD stays clean.
            for (size_t p = 0; p + 2 < s2.size(); ) {
                if (static_cast<unsigned char>(s2[p])     == 0xE2 &&
                    static_cast<unsigned char>(s2[p + 1]) == 0x88 &&
                    static_cast<unsigned char>(s2[p + 2]) == 0x9E) {
                    s2.replace(p, 3, "INF"); p += 3;
                } else ++p;
            }
            for (auto& c : s2) {
                const unsigned char u = static_cast<unsigned char>(c);
                if (u < 0x20 || u > 0x7E) c = '-';
            }
            while (!s2.empty() && s2.front() == ' ') s2.erase(0, 1);
            // Drop space between number and unit so "16.00 KHz" → "16.00KHz".
            for (size_t p = s2.size(); p > 1; --p) {
                if (s2[p - 1] != ' ') continue;
                const char prev = s2[p - 2];
                if ((prev >= '0' && prev <= '9') || prev == '.') s2.erase(p - 1, 1);
                break;
            }
            // dB readout zone has "dB" baked into the protocol frame
            // (Protocol.cpp:378). SSL's own formatter returns "-0.6 dB"
            // → space-strip leaves "-0.6dB" → frame appends "dB" → the
            // LCD renders "-0.6dBdB". Strip a trailing dB suffix so the
            // CS Fader value lines up with REAPER-volume formatting (no
            // unit; the frame supplies it).
            if (s2.size() >= 2) {
                const char a = s2[s2.size() - 2];
                const char b = s2[s2.size() - 1];
                if ((a == 'd' || a == 'D') && (b == 'B' || b == 'b')) {
                    s2.erase(s2.size() - 2);
                    while (!s2.empty() && s2.back() == ' ') s2.pop_back();
                }
            }
            if (s2.size() > 6) s2.resize(6);
            dbStr = s2;
        } else if (userStripActive) {
            // User-strip mode + no fader binding for this strip: blank the
            // numeric readout to match the blanked scribble / channel# /
            // colour bar. The "dB" suffix is appended unconditionally by
            // buildFaderDbReadout (firmware-side glyph), so we can't
            // remove it — just show empty digits, same as the blank-strip
            // routedButInvalid path (Frank 2026-05-09).
            dbStr = "    ";
        } else {
            dbStr = formatDbReadout(volLin);
        }
        if (dbStr != g_lastFaderDb[s]) {
            g_lastFaderDb[s] = dbStr;
            g_dev->send(uf8::buildFaderDbReadout(static_cast<uint8_t>(s), dbStr));
        }

        // Motor echo: push the fader target every tick — but NOT while
        // the strip is touch-reported. Position commands during touch
        // re-engage the motor against the user's hand: the user moves
        // the fader, drainInputQueue applies the new volume, and the
        // very next tick reads the new volume back, sees pb changed,
        // and sends a fresh fader-position frame. Firmware treats that
        // as "drive to target" — not the limp-target-update we'd hoped
        // for. commitDebouncedTouchReleases sends the authoritative
        // position multiple times right after motor-enable, so we lose
        // nothing by staying silent during touch.
        //
        // In FLIP mode the fader target is the focused parameter's
        // normalised position (0..1) scaled to pb14, so the motor
        // travels to where the plug-in param actually is.
        if (!g_touchReported[s].load()) {
            uint16_t pb;
            if (routedFader) {
                if (g_flip.load() && faderRoute.valid) {
                    // FLIP+route: fader carries the send's D_PAN. Map
                    // pan -1..+1 → 0..kUf8FaderPbMax so the motor parks
                    // at full-left / centre / full-right cleanly.
                    const double pan = GetTrackSendInfo_Value(
                        faderRoute.track, faderRoute.sendCategory,
                        faderRoute.sendIndex, "D_PAN");
                    double n = (pan + 1.0) * 0.5;
                    if (n < 0.0) n = 0.0;
                    if (n > 1.0) n = 1.0;
                    int p14 = static_cast<int>(std::round(
                        n * static_cast<double>(kUf8FaderPbMax)));
                    if (p14 < 0)              p14 = 0;
                    if (p14 > kUf8FaderPbMax) p14 = kUf8FaderPbMax;
                    pb = static_cast<uint16_t>(p14);
                } else {
                    // routedFader → motor parks at the routed level.
                    // volLin was already overridden above to the send/
                    // receive volume (or 0.0 for an invalid route), so
                    // the same conversion works as track-volume.
                    pb = linearVolumeToPb(volLin);
                }
            } else if (userFaderActive) {
                // User-plugin strip mode: motor echoes the bound param
                // on the FOCUSED track instance (not this strip's bank
                // track). All 8 strips drive different params on the
                // same plug-in instance.
                const double norm = TrackFX_GetParamNormalized(
                    userF.tr, userF.fxIdx, userF.vst3Param);
                const double v = userF.inverted ? 1.0 - norm : norm;
                int p14 = static_cast<int>(std::round(
                    v * static_cast<double>(kUf8FaderPbMax)));
                if (p14 < 0)              p14 = 0;
                if (p14 > kUf8FaderPbMax) p14 = kUf8FaderPbMax;
                pb = static_cast<uint16_t>(p14);
            } else if (flipActive || csFaderActive) {
                const int useFx = flipActive ? fxIdx : cs.fxIndex;
                const int useParam = flipActive ? slot->vst3Param : cs.vst3Param;
                const bool inverted = flipActive ? slot->inverted : false;
                const double norm = TrackFX_GetParamNormalized(tr, useFx, useParam);
                const double v = inverted ? 1.0 - norm : norm;
                // Scale to the actual hardware fader top (kUf8FaderPbMax)
                // so a fully-right param parks the motor at mechanical top
                // exactly. 16383 here would aim past the hardware deadband.
                int p14 = static_cast<int>(std::round(
                    v * static_cast<double>(kUf8FaderPbMax)));
                if (p14 < 0)              p14 = 0;
                if (p14 > kUf8FaderPbMax) p14 = kUf8FaderPbMax;
                pb = static_cast<uint16_t>(p14);
            } else if (flipPanSwap) {
                // FLIP+PAN no-slot swap: fader parks at the track's pan
                // position (centre = 0). Map -1..+1 → 0..kUf8FaderPbMax.
                const double pan = GetMediaTrackInfo_Value(tr, "D_PAN");
                double n = (pan + 1.0) * 0.5;
                if (n < 0.0) n = 0.0;
                if (n > 1.0) n = 1.0;
                int p14 = static_cast<int>(std::round(
                    n * static_cast<double>(kUf8FaderPbMax)));
                if (p14 < 0)              p14 = 0;
                if (p14 > kUf8FaderPbMax) p14 = kUf8FaderPbMax;
                pb = static_cast<uint16_t>(p14);
            } else if (userStripActive) {
                // User-strip mode + this strip has no fader binding:
                // park the motor at -INF so it visually matches the
                // blanked scribble / channel# / LEDs. Same convention
                // as routedButInvalid above (Frank 2026-05-09).
                pb = 0;
            } else {
                pb = linearVolumeToPb(volLin);
            }
            if (!g_faderPbInit || pb != g_lastFaderPb[s]) {
                g_lastFaderPb[s] = pb;
                const uint8_t lsb = static_cast<uint8_t>(pb & 0x7F);
                const uint8_t msb = static_cast<uint8_t>((pb >> 7) & 0x7F);
                g_dev->send(uf8::buildFaderPosition(static_cast<uint8_t>(s), lsb, msb));
            }
        }

        // Value Line — for SSL plug-ins: slot name + formatted param value
        // ("HF Freq    8.00kHz"). Otherwise: track volume ("Vol   -6.0dB").
        // In FLIP mode the fader is driving the parameter, so we show
        // track volume here — the data the V-Pot is now driving.
        std::string valLine;
        const bool synthFocused = (focused.domain == uf8::Domain::ChannelStrip)
            && (focused.slotIdx == uf8::ext::TrackPhase
             || focused.slotIdx == uf8::ext::PluginAB
             || focused.slotIdx == uf8::ext::PluginHQ);

        // Selection-Mode overrides for the value line — AUTO shows the
        // per-strip automation-mode name; Instance shows the focused
        // track's active plug-in instance name (same on all 8 strips
        // since the cycle targets the focused track). Both win over
        // the legacy resolution chain.
        bool selectionModeHandled = false;
        if (g_selectionMode.load() == SelectionMode::Auto) {
            const char* modeName = "Trim";
            switch (GetTrackAutomationMode(tr)) {
                case 0: modeName = "Trim";  break;
                case 1: modeName = "Read";  break;
                case 2: modeName = "Touch"; break;
                case 3: modeName = "Write"; break;
                case 4: modeName = "Latch"; break;
                case 5: modeName = "LtPrv"; break;
                default: break;
            }
            valLine = composeValueLine("Auto", modeName);
            selectionModeHandled = true;
        }
        // Instance mode: the active FX name is rendered in the
        // colour-bar Channel-Strip-Type zone (see csType override
        // below), so the value line is left to the normal pipeline
        // (volume, pan, focused-param). Frank 2026-05-14 — the
        // "Inst:" label took too much space AND duplicated content
        // with the colour-bar zone.

        // FX Learn UF8: when SSL Strip Mode is on and the focused track
        // has a user-mapped plug-in, the value line shows the V-Pot's
        // bound bank-slot param — name + formatted value. Empty bank
        // slots leave the line blank.
        bool userValHandled = selectionModeHandled;
        if (g_uf8PluginMode.load() && !flipActive && !routedFader
            && !routedVpot)
        {
            if (auto uctx = userStripCtxFocused_(); uctx.map) {
                const int bank = std::clamp(g_softKeyBank.load(), 0, uf8::kUserUf8BankCount - 1);
                const auto& bs = uctx.map->uf8.banks.banks[bank][s];
                if (bs.vst3Param >= 0) {
                    char pn[64]  = {0};
                    char vbuf[64] = {0};
                    TrackFX_GetParamName(uctx.tr, uctx.fxIdx,
                        bs.vst3Param, pn, sizeof(pn));
                    const double norm = TrackFX_GetParamNormalized(
                        uctx.tr, uctx.fxIdx, bs.vst3Param);
                    const double v = bs.inverted ? 1.0 - norm : norm;
                    TrackFX_FormatParamValueNormalized(uctx.tr,
                        uctx.fxIdx, bs.vst3Param, v, vbuf, sizeof(vbuf));
                    std::string valStr(vbuf);
                    for (size_t p = 0; p + 2 < valStr.size(); ) {
                        if (static_cast<unsigned char>(valStr[p])     == 0xE2 &&
                            static_cast<unsigned char>(valStr[p + 1]) == 0x88 &&
                            static_cast<unsigned char>(valStr[p + 2]) == 0x9E) {
                            valStr.replace(p, 3, "INF"); p += 3;
                        } else ++p;
                    }
                    for (auto& c : valStr) {
                        const unsigned char u = static_cast<unsigned char>(c);
                        if (u < 0x20 || u > 0x7E) c = '-';
                    }
                    while (!valStr.empty() && valStr.front() == ' ')
                        valStr.erase(0, 1);
                    valLine = composeValueLine(pn, valStr);
                } else {
                    valLine = std::string(19, ' ');
                }
                userValHandled = true;
            }
        }
        if (userValHandled) {
            // skip the legacy resolution chain
        } else
        if (routedFader || routedVpot) {
            // Routing mode value line. The strip's UPPER line already
            // shows the route name (Frank 2026-05-07), so the value
            // line is freed to show the parameter the V-Pot is driving:
            //   routedFader (default) — V-Pots free for pan → "Pan ±xx"
            //   routedVpot  (Flip)     — V-Pots driving the route level →
            //                            show route name here so the
            //                            strip still self-identifies.
            // Pan-overlay (recent tweak) wins over both for kPanOverlayMs
            // when a route is active so the formatted pan readout stays
            // visible while the user is twiddling.
            const StripRoute& r = routedFader ? faderRoute : vpotRoute;
            if (r.valid) {
                if (g_panOverlayUntilMs[s] > nowMs_()) {
                    valLine = g_panOverlayText[s];
                } else if (routedFader) {
                    // Pan readout — track's own pan (V-Pots are free).
                    const double pan = GetMediaTrackInfo_Value(tr, "D_PAN");
                    valLine = composeValueLine("Pan", formatPanReadout(pan));
                } else {
                    std::string n = routeName_(r);
                    if (n.size() > 19) n.resize(19);
                    valLine = std::move(n);
                    valLine.resize(19, ' ');
                }
            } else {
                valLine = std::string(19, ' ');
            }
        } else if (flipActive) {
            valLine = composeValueLine("Vol", formatDbReadout(volLin));
        } else if (flipPanSwap) {
            // FLIP+PAN no-slot swap: V-Pot shows volume, fader shows pan.
            // Value line follows the V-Pot ("Vol") so the dB readout
            // matches what the V-Pot is actually displaying.
            valLine = composeValueLine("Vol", formatDbReadout(volLin));
        } else if (g_forcePan.load()) {
            // forcePan overrides Plugin mode + focus. Pure REAPER pan.
            const double pan = GetMediaTrackInfo_Value(tr, "D_PAN");
            valLine = composeValueLine("Pan", formatPanReadout(pan));
        } else if (synthFocused) {
            // Synthetic toggle focused: render this strip's own state.
            // No VST3 param to format — read directly from REAPER /
            // SSL plug-in chunk. tr was guaranteed non-null above
            // (empty-strip block returned earlier).
            if (focused.slotIdx == uf8::ext::TrackPhase) {
                const bool on = GetMediaTrackInfo_Value(tr, "B_PHASE") > 0.5;
                valLine = composeValueLine("Phase", on ? "INV" : "OFF");
            } else {
                int ab = -1, hq = -1;
                uf8::readPluginToggleStates(tr, ab, hq);
                if (focused.slotIdx == uf8::ext::PluginAB) {
                    const char* v = (ab == 1) ? "A" : (ab == 0) ? "B" : "-";
                    valLine = composeValueLine("A/B", v);
                } else { // PluginHQ
                    const char* v = (hq == 1) ? "ON" : (hq == 0) ? "OFF" : "-";
                    valLine = composeValueLine("HQ", v);
                }
            }
        } else if (slot && fxIdx >= 0 && !isVPotPanFocus(focused)) {
            char paramBuf[64] = {0};
            const double norm = TrackFX_GetParamNormalized(tr, fxIdx, slot->vst3Param);
            TrackFX_FormatParamValueNormalized(tr, fxIdx, slot->vst3Param,
                                               norm, paramBuf, sizeof(paramBuf));
            std::string valStr(paramBuf);
            // Replace UTF-8 ∞ (E2 88 9E) with "INF" before the non-ASCII
            // squash — Comp Ratio at max returns "∞:1", which would
            // otherwise become "---:1" hieroglyphs on the LCD.
            for (size_t p = 0; p + 2 < valStr.size(); ) {
                if (static_cast<unsigned char>(valStr[p])     == 0xE2 &&
                    static_cast<unsigned char>(valStr[p + 1]) == 0x88 &&
                    static_cast<unsigned char>(valStr[p + 2]) == 0x9E) {
                    valStr.replace(p, 3, "INF");
                    p += 3;
                } else {
                    ++p;
                }
            }
            // Squash any remaining non-printable-ASCII so character count
            // and cursor positions stay correct on the LCD.
            for (auto& c : valStr) {
                const unsigned char u = static_cast<unsigned char>(c);
                if (u < 0x20 || u > 0x7E) c = '-';
            }
            while (!valStr.empty() && valStr.front() == ' ') valStr.erase(0, 1);
            // Non-numeric value (e.g. "OUT Hz" when LP/HP filter is bypassed,
            // "Off Hz") — drop the unit suffix so the LCD reads just "OUT".
            if (!valStr.empty()) {
                const char first = valStr.front();
                const bool numeric = (first >= '0' && first <= '9')
                                  || first == '-' || first == '+' || first == '.';
                if (!numeric) {
                    const size_t sp = valStr.find(' ');
                    if (sp != std::string::npos) valStr.erase(sp);
                }
            }
            // Strip the space between numeric value and unit suffix so
            // "16.00 KHz" → "16.00KHz" (saves one char so the leading
            // digit doesn't get clipped by the LCD value zone). Match
            // both lower- and upper-case unit variants — 4K E returns
            // "KHz" with capital K, CS 2 uses lowercase. Generic walk:
            // find the rightmost space whose preceding char is a digit
            // or '.' and erase it.
            for (size_t p = valStr.size(); p > 1; --p) {
                if (valStr[p - 1] != ' ') continue;
                const char prev = valStr[p - 2];
                if ((prev >= '0' && prev <= '9') || prev == '.') {
                    valStr.erase(p - 1, 1);
                }
                break;
            }
            valLine = composeValueLine(slot->name, valStr);
        } else if (focused.slotIdx != -1 && !isVPotPanFocus(focused)) {
            // Param is focused but unavailable on this strip's plug-in
            // — leave the Value Line blank instead of falling back to
            // Pan, which would mislead the user into thinking the
            // V-Pot controls something on this strip.
            valLine = std::string(19, ' ');
        } else if (g_pluginFaderMode.load()) {
            // Plugin mode → show the SSL strip's own Pan instead of
            // REAPER track pan. Plug-in pan is normalised 0..1 with
            // 0.5 = centre; convert to REAPER's -1..+1 for the
            // existing formatPanReadout helper. Falls back to track
            // pan when there's no CS plug-in on the track.
            const auto pn = csPanForTrack(tr);
            if (pn.vst3Param >= 0) {
                const double norm = TrackFX_GetParamNormalized(
                    tr, pn.fxIndex, pn.vst3Param);
                valLine = composeValueLine("Pan",
                    formatPanReadout(norm * 2.0 - 1.0));
            } else {
                const double pan = GetMediaTrackInfo_Value(tr, "D_PAN");
                valLine = composeValueLine("Pan", formatPanReadout(pan));
            }
        } else {
            // Nothing focused → V-Pot controls Pan; reflect that in
            // the Value Line. Fader dB stays in the dedicated O/PdB
            // zone above, so we don't need to repeat volume here.
            const double pan = GetMediaTrackInfo_Value(tr, "D_PAN");
            valLine = composeValueLine("Pan", formatPanReadout(pan));
        }
        // Folder Mode override — final stop in the value-line chain.
        // For folder-parent tracks, swap the value line for "Folder"
        // unless the user just turned the V-Pot (reveal window from
        // drainInputQueue). Independent of whatever pan / param /
        // routing branch wrote the value above. The LCD splits the
        // 19-char zone at position 11 (left = label/white, right =
        // value/yellow) so "Folder" must live entirely in the label
        // half — composeValueLine left-aligns it for free.
        if (g_folderMode.load()
            && nowMs_() >= g_folderRevealUntilMs[s]
            && GetMediaTrackInfo_Value(tr, "I_FOLDERDEPTH") > 0.5)
        {
            valLine = composeValueLine("Folder", "");
        }
        if (valLine != g_lastValueLine[s]) {
            g_lastValueLine[s] = valLine;
            g_dev->send(uf8::buildValueLine(static_cast<uint8_t>(s), valLine));
        }
    }
    g_faderPbInit = true;

    // V-Pot Readout Bar — per cap37, SSL360 sends position (FF 66 11 0F)
    // FIRST, then mode (FF 66 09 0D) ~55 µs later. Reverse order with
    // mismatched encoding makes the firmware render the bar wrong.
    //
    // Mode register is init-/transition-only (sent when going inactive
    // → active, never re-asserted per tick). Per-strip mode:
    //   0x08 = bipolar centre-out (Gain/Pan/Trim — cap37 mode register)
    //   0x01 = unipolar L→R full sweep (Freq/Q/Threshold — cap15 mode
    //          register `01 01 01 01 03 03 03 03` with byte0=0..0x64)
    //   0x03 = empty / disabled (no track in bank, or binary toggle)
    std::array<uint8_t, 8> vpotMode{};
    {
        const int trackCount = visibleTrackCount();
        const auto focused = uf8::getFocusedParam();
        const int bankOffset = g_bankOffset.load();
        for (uint8_t s = 0; s < 8; ++s) {
            const int realSlot = static_cast<int>(s) + bankOffset;
            if (realSlot >= trackCount) {
                vpotMode[s] = 0x03;
                continue;
            }
            // Routing wins. Two flavours:
            //   * Fader has the route → V-Pot is always pan (bipolar),
            //     regardless of PAN button — fader carries vol, V-Pot
            //     carries pan as the natural complement.
            //   * V-Pot has the route → V-Pot defaults to vol (unipolar);
            //     PAN button switches to pan (bipolar).
            // Empty send slots collapse to the disabled mode.
            const StripRoute fRoute = resolveFaderRoute_(
                static_cast<int>(s), bankOffset, trackCount);
            if (fRoute.active()) {
                if (!fRoute.valid) {
                    vpotMode[s] = 0x03;
                } else if (g_flip.load() && g_forcePan.load()) {
                    vpotMode[s] = 0x08;       // FLIP+PAN held → track pan
                } else if (g_flip.load()) {
                    vpotMode[s] = 0x01;       // FLIP → V-Pot = volume
                } else {
                    vpotMode[s] = 0x08;       // default → V-Pot = send pan
                }
                continue;
            }
            const StripRoute vRoute = resolveVpotRoute_(
                static_cast<int>(s), bankOffset, trackCount);
            if (vRoute.active()) {
                if (!vRoute.valid)            vpotMode[s] = 0x03;
                else if (g_forcePan.load())   vpotMode[s] = 0x08;
                else                          vpotMode[s] = 0x01;
                continue;
            }
            // FX Learn UF8: user-bank V-Pot mode register.
            //   Toggle slot → 0x03 (binary indicator, no bar)
            //   Value slot  → 0x01 (unipolar L→R)
            //   Empty slot  → 0x03 (no bar)
            if (g_uf8PluginMode.load() && !g_flip.load()) {
                if (auto uctx = userStripCtxFocused_(); uctx.map) {
                    const int bank = std::clamp(g_softKeyBank.load(), 0, uf8::kUserUf8BankCount - 1);
                    const auto& bs =
                        uctx.map->uf8.banks.banks[bank][s];
                    if (bs.vst3Param < 0
                        || bs.vpotMode == uf8::VPotMode::Toggle)
                    {
                        vpotMode[s] = 0x03;
                    } else {
                        vpotMode[s] = 0x01;
                    }
                    continue;
                }
            }
            MediaTrack* tr = visibleTrackAt(realSlot);
            int fxIdx = -1;
            const uf8::LinkSlot* slot = slotForStrip(tr, focused, &fxIdx);
            // Pan-focus is treated as no-V-Pot-focus by the position +
            // value-line render branches (defers to the Plugin/forcePan/
            // REAPER pan tree, which is bipolar centre-out). Mode register
            // must agree — otherwise the firmware renders the bipolar
            // centre encoding (byte0=0x00, byte1=0x80) as left-edge in
            // unipolar mode 0x01.
            if (slot && isVPotPanFocus(focused)) slot = nullptr;
            const bool flipHere = g_flip.load() && slot && fxIdx >= 0;
            const bool flipPanHere = g_flip.load() && g_forcePan.load() && !flipHere;
            if (flipHere || flipPanHere) {
                vpotMode[s] = 0x01;  // FLIP / FLIP+PAN: V-Pot = volume (unipolar)
            } else if (!slot && focused.slotIdx != -1
                       && !isVPotPanFocus(focused)
                       && !g_forcePan.load()
                       && !g_pluginFaderMode.load()
                       && !g_uf8PluginMode.load()) {
                // A param is focused but doesn't resolve on this strip:
                // synthetic toggles (Phase / A/B / HQ) that aren't VST3
                // params, or a continuous param this strip's plug-in
                // lacks. Position render sends the collapsed-bar marker
                // 0x8000; mode 0x08 (bipolar) would draw that marker as
                // a centre dot — leaving the previous V-Pot ring stuck
                // visible. 0x03 ("no bar") clears the ring.
                vpotMode[s] = 0x03;
            } else if (!slot) {
                vpotMode[s] = 0x08;  // pan fallback — bipolar centre
            } else if (isBinarySlot(*slot)) {
                vpotMode[s] = 0x03;  // binary — no bar
            } else if (isBipolarSlot(*slot)) {
                vpotMode[s] = 0x08;  // bipolar centre-out
            } else {
                vpotMode[s] = 0x01;  // unipolar L→R
            }
        }
    }
    bool modeChanged = !g_vpotBarInit;
    if (!modeChanged) {
        for (uint8_t s = 0; s < 8; ++s) {
            if (vpotMode[s] != g_lastVPotMode[s]) { modeChanged = true; break; }
        }
    }
    bool barChanged = false;
    for (uint8_t s = 0; s < 8; ++s) {
        if (vpotBar[s] != g_lastVPotBar[s]) { barChanged = true; break; }
    }
    // Position FIRST per cap37 ordering.
    if (barChanged) {
        g_lastVPotBar = vpotBar;
        g_dev->send(uf8::buildVPotReadoutBar(vpotBar));
    }
    if (modeChanged) {
        g_lastVPotMode = vpotMode;
        std::vector<uint8_t> mf{0xFF, 0x66, 0x09, 0x0D};
        for (auto m : vpotMode) mf.push_back(m);
        uint8_t cks = 0;
        for (size_t i = 1; i < mf.size(); ++i) cks += mf[i];
        mf.push_back(cks);
        g_dev->send(std::move(mf));
        g_vpotBarInit = true;
    }
}

// Mouse-edits in the SSL plug-in GUI: REAPER tracks the most recently
// touched (track, fx, param) tuple via GetLastTouchedFX. Polling it on
// each timer tick lets UF8 + UC1 chase plugin-GUI moves without the
// user needing to first nudge a knob on UC1 / V-Pot on UF8.
//
// Dedup against the previous tuple so we don't pay map lookups every
// tick when the user is idle. Static state is fine: timer is the
// single caller, all on the main thread.
//
// Caveats handled inline:
//   - master track (trWord low word == 0): skip
//   - take-FX (high word of trWord nonzero): skip
//   - record-FX (bit 24 of fxWord set): skip
// Anything we don't understand falls through silently — chase is
// best-effort.
void chaseLastTouchedFx()
{
    int trWord = -1, fxWord = -1, paramIdx = -1;
    if (!GetLastTouchedFX(&trWord, &fxWord, &paramIdx)) return;

    if ((trWord & 0xFFFF0000) != 0) return;        // take-FX
    const int trLow = trWord & 0xFFFF;
    if (trLow <= 0) return;                        // master / invalid
    MediaTrack* tr = GetTrack(nullptr, trLow - 1);
    if (!tr) return;

    if ((fxWord >> 24) & 0x01) return;             // record-FX
    const int fxIdx = fxWord & 0x00FFFFFF;

    char fxName[512] = {0};
    TrackFX_GetFXName(tr, fxIdx, fxName, sizeof(fxName));
    const uf8::PluginMap* map = uf8::lookupPluginMapByName(fxName);
    if (!map) return;

    const int linkIdx = uf8::slotIdxForVst3Param(*map, paramIdx);
    if (linkIdx < 0) return;

    // Re-apply focus when the user is ACTIVELY editing — either a new
    // (tr, fx, param) touch OR the param value moved since last tick.
    // GetLastTouchedFX returns the same (tr, fx, param) forever after
    // the user touches it once, so a naive "fire setFocus every tick"
    // overrides legitimate FocusedParam state set by encoders / domain
    // builtins (e.g. {BC, 0} from BC-encoder, slot 0 from domain_bc) —
    // that broke instance cycling. The value-changed gate makes chase
    // yield FocusedParam to other paths during pure navigation, while
    // still re-applying through dedup-blocked stale state when the
    // user is genuinely turning a knob.
    static int    lastTr = -2, lastFx = -2, lastParam = -2;
    static double lastValue = -999.0;
    const double  curValue = TrackFX_GetParamNormalized(tr, fxIdx, paramIdx);
    const bool inputChanged = (trWord != lastTr || fxWord != lastFx
                               || paramIdx != lastParam);
    const bool valueChanged = (curValue != lastValue);
    lastValue = curValue;

    if (inputChanged || valueChanged) {
        uf8::setFocus({map->domain, linkIdx});
        uf8::g_focusedFxTrack.store(static_cast<void*>(tr),
                                    std::memory_order_relaxed);
    }
    if (!inputChanged) return;
    lastTr = trWord; lastFx = fxWord; lastParam = paramIdx;

    // Multi-instance follow: a plug-in GUI click on a copy that isn't
    // currently the active instance should snap UC1 to that copy. We
    // map fxIdx → instance index within the domain on the touched
    // track and update g_(bc|cs)InstanceMap. Works for both selected
    // and non-selected tracks; UC1's own focus gate below decides
    // whether the surface follows.
    {
        const int instIdx = uc1::instanceIndexForFx(tr, fxIdx);
        if (instIdx >= 0) {
            if (map->domain == uf8::Domain::BusComp) {
                uc1::setBcInstanceIndex(tr, instIdx);
            } else if (map->domain == uf8::Domain::ChannelStrip) {
                uc1::setCsInstanceIndex(tr, instIdx);
            }
            // Force the next render tick to re-resolve bindings + push
            // the new short-name letter, knob rings, etc.
            if (g_uc1_surface) g_uc1_surface->invalidateCache();
            g_pageDirty.store(true);
            g_bankDirty.store(true);
        }
    }

    // UC1 mirrors the SELECTED track. Without this gate, a UF8 V-Pot edit
    // on a non-selected strip touches that track's plug-in param, which
    // updates GetLastTouchedFX globally — chaseLastTouchedFx would then
    // hijack UC1 to the non-selected track. UF8 V-Pots are explicit
    // per-strip edits and shouldn't steal track focus from UC1; the
    // focused-param/slot update above is enough to keep the soft-key
    // bank in sync.
    //
    // Settings opt-in "Track selection follows parameter change":
    // domain-specific routing because the CS Channel (encoder 1) and BC
    // Channel (encoder 2) on UC1 are independent track focuses. Frank
    // 2026-05-07:
    //   - SC/CS-domain edit → SetOnlyTrackSelected → REAPER selection
    //     follows → focusedTrack_ + UF8 bank update via SetSurfaceSelected.
    //   - BC-domain edit → only bcAnchorTrack_ updates → BC encoder/
    //     section follows on UC1 without disturbing CS Channel or UF8 bank.
    //
    // BC anchor follows UNCONDITIONALLY whenever a BC-domain param is
    // touched, even when the track is REAPER-selected. Frank 2026-05-12:
    // "Parameter im display werden nur angezeigt, wenn encoder 2 auch
    // auf dem kanal ist". The BC readout zone + UC1 BC knob rings both
    // poll bcAnchor every tick, so a UF8 V-Pot edit on a non-anchor
    // BC plug-in (e.g. selected track different from where Encoder 2
    // last landed) would otherwise leave UC1 painting the wrong track's
    // BC values. CS still gates on the opt-in setting because CS edits
    // already drive REAPER selection through SetOnlyTrackSelected.
    const bool isSelected = GetMediaTrackInfo_Value(tr, "I_SELECTED") > 0.5;
    if (map->domain == uf8::Domain::BusComp) {
        if (g_uc1_surface) g_uc1_surface->setBcAnchorTrack(tr);
        // Selected? Mirror focusedTrack_ too — same as the legacy
        // isSelected branch below. Non-selected? Stay quiet: the BC
        // anchor move is enough to keep UC1's BC half on the right
        // plug-in without disturbing the CS Channel or UF8 bank.
        if (isSelected && g_uc1_surface) {
            g_uc1_surface->setFocusedTrack(tr);
        }
        return;
    }
    if (!isSelected && g_trackSelFollowsParam.load()) {
        if (map->domain == uf8::Domain::ChannelStrip) {
            SetOnlyTrackSelected(tr);
            followSelectedInMixer(tr);
            return;  // SetSurfaceSelected fires and updates UC1
        }
    }
    if (g_uc1_surface && isSelected) {
        g_uc1_surface->setFocusedTrack(tr);
    }
}

// One-shot snap on UF8 Plugin Mode entry. Two-pass:
//   1. If REAPER reports a focused FX (`GetFocusedFX2 & 1`) and it's a
//      UF8-mapped (`uf8Mode`) plug-in, snap to that.
//   2. Otherwise walk every track / FX looking for an OPEN window
//      (`TrackFX_GetOpen`) on a UF8-mapped plug-in and snap to the
//      first hit. This is the case Frank cares about — "Fenster ist
//      offen" doesn't always mean "REAPER says it's focused" (the
//      user may have clicked elsewhere since opening it).
// Snap = select the track, set the UC1 Instance index for the matched
// plug-in's domain, point `g_uc1_surface->focusedTrack` at it, dirty
// the page so `userStripCtxFocused_`'s cache invalidates on the next
// render tick. No-op if nothing qualifies.
bool snapUf8PluginModeToFocusedFx_()
{
    if (!g_uf8PluginMode.load()) return false;

    MediaTrack* targetTr = nullptr;
    int targetFx = -1;
    uf8::Domain targetDom = uf8::Domain::None;

    auto qualify = [&](MediaTrack* tr, int fxIdx) -> bool {
        if (!tr || !ValidatePtr2(nullptr, tr, "MediaTrack*")) return false;
        if (fxIdx < 0 || fxIdx >= TrackFX_GetCount(tr)) return false;
        char fxName[512] = {0};
        if (!TrackFX_GetFXName(tr, fxIdx, fxName, sizeof(fxName))) return false;
        const auto* um = uf8::user_plugins::lookupOwnedByName(fxName);
        if (!um || !um->uf8Mode) return false;
        targetTr  = tr;
        targetFx  = fxIdx;
        targetDom = um->domain;
        return true;
    };

    // Pass 1: REAPER's focused FX.
    int trNum = -1, itemNum = -1, fxNum = -1;
    const int ret = GetFocusedFX2(&trNum, &itemNum, &fxNum);
    if ((ret & 1) && trNum > 0) {
        MediaTrack* tr = GetTrack(nullptr, trNum - 1);
        qualify(tr, fxNum & 0x00FFFFFF);
    }
    // Pass 2: any open window on any track. First UF8-mapped hit wins.
    if (!targetTr) {
        const int trackCount = CountTracks(nullptr);
        for (int t = 0; t < trackCount && !targetTr; ++t) {
            MediaTrack* tr = GetTrack(nullptr, t);
            if (!tr) continue;
            const int n = TrackFX_GetCount(tr);
            for (int f = 0; f < n; ++f) {
                if (!TrackFX_GetOpen(tr, f)) continue;
                if (qualify(tr, f)) break;
            }
        }
    }

    if (!targetTr || targetFx < 0) return false;

    if (targetDom == uf8::Domain::None) {
        // UF8-only mapping — count this FX's position among UF8-only
        // mapped plug-ins on the same track, snap that Instance index.
        int seen = 0;
        char buf[256];
        const int n = TrackFX_GetCount(targetTr);
        for (int i = 0; i < n; ++i) {
            if (!TrackFX_GetFXName(targetTr, i, buf, sizeof(buf))) continue;
            const auto* u = uf8::user_plugins::lookupOwnedByName(buf);
            if (!u || u->domain != uf8::Domain::None || !u->uf8Mode)
                continue;
            if (i == targetFx) {
                uc1::setUf8OnlyInstanceIndex(targetTr, seen);
                break;
            }
            ++seen;
        }
        // Drop any stale CS/BC focused-domain — `userStripCtxFocused_`
        // dispatches `findUserPluginOnTrack_` with this domain first.
        // Leaving it as CS/BC after snapping to a UF8-only plug-in
        // means the cached resolver burns a domain attempt before
        // falling through to None.
        if (uf8::getFocusedParam().domain != uf8::Domain::None) {
            uf8::setFocus({uf8::Domain::None, 0});
        }
    } else {
        const int instIdx = uc1::instanceIndexForFx(targetTr, targetFx);
        if (instIdx >= 0) {
            if (targetDom == uf8::Domain::BusComp)
                uc1::setBcInstanceIndex(targetTr, instIdx);
            else if (targetDom == uf8::Domain::ChannelStrip)
                uc1::setCsInstanceIndex(targetTr, instIdx);
        }
        uf8::setFocus({targetDom, 0});
    }

    uf8::g_focusedFxTrack.store(static_cast<void*>(targetTr),
                                std::memory_order_relaxed);

    if (GetMediaTrackInfo_Value(targetTr, "I_SELECTED") < 0.5) {
        SetOnlyTrackSelected(targetTr);
    }
    if (g_uc1_surface) {
        g_uc1_surface->invalidateCache();
        g_uc1_surface->setFocusedTrack(targetTr);
    }
    g_pageDirty.store(true);
    g_bankDirty.store(true);
    return true;
}

// Poll GetFocusedFX2 and switch the SSL strip context to whichever
// user-mapped plugin window the user brings to front. Only active
// when SSL Strip Mode is on AND the setting is enabled.
void chaseFocusedFxWindow()
{
    if (!g_pluginFaderMode.load() && !g_uf8PluginMode.load()) return;
    if (!g_stripFollowsFocusedFx.load()) return;

    int trNum = -1, itemNum = -1, fxNum = -1;
    const int ret = GetFocusedFX2(&trNum, &itemNum, &fxNum);
    if (!(ret & 1)) return;          // no track-FX window focused
    if (trNum <= 0) return;          // master or invalid

    MediaTrack* tr = GetTrack(nullptr, trNum - 1);
    if (!tr) return;

    const int fxIdx = fxNum & 0x00FFFFFF;

    static MediaTrack* s_lastTr  = nullptr;
    static int         s_lastFx  = -1;
    if (tr == s_lastTr && fxIdx == s_lastFx) return;
    s_lastTr = tr; s_lastFx = fxIdx;

    char fxName[512] = {0};
    TrackFX_GetFXName(tr, fxIdx, fxName, sizeof(fxName));

    const auto* um = uf8::user_plugins::lookupOwnedByName(fxName);
    if (!um) return;

    const int instIdx = uc1::instanceIndexForFx(tr, fxIdx);
    if (instIdx >= 0) {
        if (um->domain == uf8::Domain::BusComp)
            uc1::setBcInstanceIndex(tr, instIdx);
        else if (um->domain == uf8::Domain::ChannelStrip)
            uc1::setCsInstanceIndex(tr, instIdx);
    }
    // UF8-only maps don't show up in instanceIndexForFx (lookupBindings
    // ByName ignores them). Walk the track ourselves to find this FX's
    // position among UF8-only mapped plug-ins, so the cycle's index
    // snaps to whichever UF8-only window the user just clicked.
    if (um->domain == uf8::Domain::None && um->uf8Mode) {
        int seen = 0;
        char buf[256];
        const int nFx = TrackFX_GetCount(tr);
        for (int i = 0; i < nFx; ++i) {
            if (!TrackFX_GetFXName(tr, i, buf, sizeof(buf))) continue;
            const auto* u = uf8::user_plugins::lookupOwnedByName(buf);
            if (!u || u->domain != uf8::Domain::None || !u->uf8Mode) continue;
            if (i == fxIdx) {
                uc1::setUf8OnlyInstanceIndex(tr, seen);
                break;
            }
            ++seen;
        }
    }

    // UF8-only maps (domain==None) shouldn't clobber the CS/BC focus
    // when the user touches them — they have no UC1 representation.
    if (um->domain != uf8::Domain::None) {
        uf8::setFocus({um->domain, 0});
    }
    uf8::g_focusedFxTrack.store(static_cast<void*>(tr),
                                std::memory_order_relaxed);

    const bool isSelected = GetMediaTrackInfo_Value(tr, "I_SELECTED") > 0.5;
    if (!isSelected) SetOnlyTrackSelected(tr);

    if (g_uc1_surface) {
        g_uc1_surface->invalidateCache();
        g_uc1_surface->setFocusedTrack(tr);
    }
    g_pageDirty.store(true);
    g_bankDirty.store(true);
}

void commitDebouncedTouchReleases()
{
    const auto now = std::chrono::steady_clock::now();
    for (uint8_t s = 0; s < 8; ++s) {
        if (!g_touchReleasePending[s].load()) continue;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - g_touchLastPress[s]).count();
        if (elapsed < kTouchDebounceQuiet.count()) continue;
        g_touchReleasePending[s].store(false);
        const bool wasReported = g_touchReported[s].exchange(false);
        if (!wasReported) continue;

        if (!g_dev) continue;
        MediaTrack* tr = g_slotTrack[s];
        if (!tr || !ValidatePtr2(nullptr, tr, "MediaTrack*")) continue;

        // Snap REAPER (or the active plug-in target) to the user's last
        // raw fader position (regardless of the >=4-LSB deadband). The
        // fader drives:
        //   - routed send/recv level in Send/Receive mode,
        //   - focused param          in FLIP mode (when the strip has a slot),
        //   - SSL CS Fader           in Plugin-Fader mode,
        //   - track volume           otherwise.
        const bool userMoved = g_lastTouchPbValid[s].load();
        if (userMoved) {
            const uint16_t touchPb = g_lastTouchPb[s].load();
            g_lastTouchPbValid[s].store(false);

            // Routing wins: the touch-release writeback must follow the
            // same precedence as the live fader handler (drainInputQueue
            // VolumeAbs case). Without this, releasing a fader in
            // "Sends of Focused Track" mode wrote the touch position to
            // the bank track's main volume — when the focused track's
            // bank position coincided with the released strip, the
            // selected channel's fader visibly jumped to the touch level.
            const StripRoute fr = resolveFaderRoute_(
                static_cast<int>(s), g_bankOffset.load(), visibleTrackCount());
            if (fr.active()) {
                if (fr.valid) {
                    // Mirror the live fader handler's FLIP precedence:
                    // FLIP on → write the route's D_PAN, FLIP off →
                    // write the route's D_VOL. Earlier this branch
                    // unconditionally wrote D_VOL, which combined with
                    // the live handler's D_PAN writes during FLIP to
                    // change BOTH pan and send volume on every fader
                    // move (Frank 2026-05-07).
                    if (g_flip.load()) {
                        double pan = static_cast<double>(touchPb)
                                   / static_cast<double>(kUf8FaderPbMax);
                        pan = pan * 2.0 - 1.0;
                        if (pan < -1.0) pan = -1.0;
                        if (pan >  1.0) pan =  1.0;
                        SetTrackSendInfo_Value(fr.track, fr.sendCategory,
                                               fr.sendIndex, "D_PAN", pan);
                    } else {
                        SetTrackSendInfo_Value(fr.track, fr.sendCategory,
                                               fr.sendIndex, "D_VOL",
                                               pbToLinearVolume(touchPb));
                    }
                }
                // active-but-invalid: eat the writeback, do NOT fall
                // through to track-volume.
            } else {
                const auto focusedT = uf8::getFocusedParam();
                auto mmT = uf8::lookupPluginOnTrack(tr, focusedT.domain);
                const uf8::LinkSlot* slT = (!g_forcePan.load() && mmT.map)
                    ? uf8::findSlotByLinkIdx(*mmT.map, focusedT.slotIdx)
                    : nullptr;
                if (isVPotPanFocus(focusedT)) slT = nullptr;
                const auto csT = csFaderForTrack(tr);
                if (g_flip.load() && slT) {
                    double normT = static_cast<double>(touchPb) /
                                   static_cast<double>(kUf8FaderPbMax);
                    if (slT->inverted) normT = 1.0 - normT;
                    if (normT < 0.0) normT = 0.0;
                    if (normT > 1.0) normT = 1.0;
                    TrackFX_SetParamNormalized(tr, mmT.fxIndex,
                        slT->vst3Param, normT);
                } else if (g_flip.load() && !g_pluginFaderMode.load()
                           && !g_uf8PluginMode.load()) {
                    double n = static_cast<double>(touchPb) /
                               static_cast<double>(kUf8FaderPbMax);
                    if (n < 0.0) n = 0.0;
                    if (n > 1.0) n = 1.0;
                    double pan = n * 2.0 - 1.0;
                    if (pan < -1.0) pan = -1.0;
                    if (pan >  1.0) pan =  1.0;
                    SetMediaTrackInfo_Value(tr, "D_PAN", pan);
                } else if (g_uf8PluginMode.load()) {
                    if (auto uctxT = userStripCtxFocused_(); uctxT.map) {
                        const auto& sb = uctxT.map->uf8.strips[
                            static_cast<int>(s)];
                        if (sb.faderVst3Param >= 0) {
                            double n = static_cast<double>(touchPb) /
                                       static_cast<double>(kUf8FaderPbMax);
                            if (sb.faderInverted) n = 1.0 - n;
                            if (n < 0.0) n = 0.0;
                            if (n > 1.0) n = 1.0;
                            TrackFX_SetParamNormalized(uctxT.tr,
                                uctxT.fxIdx, sb.faderVst3Param, n);
                        }
                    } else {
                        CSurf_OnVolumeChange(tr,
                            pbToLinearVolume(touchPb), false);
                    }
                } else if (g_pluginFaderMode.load()) {
                    if (csT.vst3Param >= 0) {
                        double n = static_cast<double>(touchPb) /
                                   static_cast<double>(kUf8FaderPbMax);
                        if (n < 0.0) n = 0.0;
                        if (n > 1.0) n = 1.0;
                        TrackFX_SetParamNormalized(tr, csT.fxIndex,
                            csT.vst3Param, n);
                    } else {
                        CSurf_OnVolumeChange(tr,
                            pbToLinearVolume(touchPb), false);
                    }
                } else {
                    CSurf_OnVolumeChange(tr, pbToLinearVolume(touchPb), false);
                }
            }
        }

        // Re-engage the motor ONLY if the user actually moved the fader
        // during this touch. SSL 360° on Windows verified 2026-05-06 via
        // captures/fader8_test.pcapng (touch-only: 22 LIMPs, 0 re-enables)
        // and captures/fader8_push.pcapng (touch+drag: 8 LIMPs, 2 re-
        // enables) — the rule is: re-enable only follows movement.
        // Re-enabling on every brief touch made the motor engage while
        // the user still had a finger on the strip, which the user felt
        // as "alle Fader sperren". The pre-existing `uf8-pm-mode-
        // invariants.md` memory entry warned about this — we'd violated
        // it. The firmware's target buffer is already pointing at the
        // user's final touch position thanks to the bit-7-set echoes,
        // so the re-enable lands at the correct target with no jerk.
        if (userMoved) {
            uint16_t pb = linearVolumeToPb(uiVolLinear(tr));
            if (g_uf8PluginMode.load()) {
                if (auto uctx = userStripCtxFocused_(); uctx.map) {
                    const auto& sb = uctx.map->uf8.strips[s];
                    if (sb.faderVst3Param >= 0) {
                        double norm = TrackFX_GetParamNormalized(
                            uctx.tr, uctx.fxIdx, sb.faderVst3Param);
                        if (sb.faderInverted) norm = 1.0 - norm;
                        int p14 = static_cast<int>(std::round(
                            norm * static_cast<double>(kUf8FaderPbMax)));
                        if (p14 < 0) p14 = 0;
                        if (p14 > kUf8FaderPbMax) p14 = kUf8FaderPbMax;
                        pb = static_cast<uint16_t>(p14);
                    }
                }
            } else if (g_pluginFaderMode.load()) {
                const auto cs = csFaderForTrack(tr);
                if (cs.vst3Param >= 0) {
                    const double norm = TrackFX_GetParamNormalized(
                        tr, cs.fxIndex, cs.vst3Param);
                    int p14 = static_cast<int>(std::round(
                        norm * static_cast<double>(kUf8FaderPbMax)));
                    if (p14 < 0) p14 = 0;
                    if (p14 > kUf8FaderPbMax) p14 = kUf8FaderPbMax;
                    pb = static_cast<uint16_t>(p14);
                }
            }
            g_dev->send(uf8::buildMotorEnable(s, true));
            g_lastFaderPb[s] = pb;
        }
    }
}

// Linear peak (0..1) → UF8 VU byte (0..31). -55 dBFS → 0, 0 dBFS → 31.
// The cutoff is intentionally above -60 dB: REAPER's track-peak ballistics
// have a slow decay tail that drifts through -60..-55 even on silent
// tracks, which would otherwise flicker the bottom LED. Snapping anything
// below -55 to 0 gives a clean noise-floor.
uint8_t peakToVuByte(double peak)
{
    if (peak <= 0.0) return 0;
    const double dbfs = 20.0 * std::log10(peak);
    if (dbfs >= 0.0)   return 0x1F;
    if (dbfs <= -55.0) return 0x00;
    const double f = (dbfs + 55.0) / 55.0;
    const int byte = static_cast<int>(f * 31.0 + 0.5);
    return static_cast<uint8_t>(std::clamp(byte, 0, 0x1F));
}

uint8_t dbToVuByte_(double dbfs)
{
    if (dbfs >= 0.0)   return 0x1F;
    if (dbfs <= -55.0) return 0x00;
    const double f = (dbfs + 55.0) / 55.0;
    const int byte = static_cast<int>(f * 31.0 + 0.5);
    return static_cast<uint8_t>(std::clamp(byte, 0, 0x1F));
}

// Per-channel envelope state for the BM_VU / BM_RMS smoothers (8 strips
// × L/R). Reset to silence on init. Mode switch also resets — see
// reasixty_setBallisticMode below.
double g_vuEnvDb_[16];
double g_rmsEnvPow_[16];
bool   g_meterEnvInit_ = false;
std::chrono::steady_clock::time_point g_meterEnvLast_{};

uint8_t peakToVuByteBallistic(double peakLin, int channel, double dtSec)
{
    const int mode = g_ballisticMode.load();
    if (mode == BM_Peak || channel < 0 || channel >= 16) {
        return peakToVuByte(peakLin);
    }
    if (mode == BM_VU) {
        const double dbInst = (peakLin <= 0.0) ? -120.0
                                               : 20.0 * std::log10(peakLin);
        constexpr double kTau = 0.30;  // 300 ms
        const double alpha = 1.0 - std::exp(-dtSec / kTau);
        g_vuEnvDb_[channel] += alpha * (dbInst - g_vuEnvDb_[channel]);
        return dbToVuByte_(g_vuEnvDb_[channel]);
    }
    // RMS: smooth in linear-power domain, then convert to dB.
    const double pow = peakLin * peakLin;
    constexpr double kTau = 0.60;  // 600 ms
    const double alpha = 1.0 - std::exp(-dtSec / kTau);
    g_rmsEnvPow_[channel] += alpha * (pow - g_rmsEnvPow_[channel]);
    const double dbf = (g_rmsEnvPow_[channel] <= 0.0)
                     ? -120.0
                     : 10.0 * std::log10(g_rmsEnvPow_[channel]);
    return dbToVuByte_(dbf);
}

std::array<uint8_t, 16> g_lastVuLevels{};
bool g_vuInit = false;
std::chrono::steady_clock::time_point g_lastVuPushTime{};

// UF8 GR bytes — one per visible strip. Carried by the FF 66 09 15
// heartbeat at offsets 4..11 (strip 1 → byte 4, strip 8 → byte 11).
// Byte 0x00 = no LEDs lit (true rest); ramps up monotonically to ~0x18
// at full GR. Only the strip that hosts the focused CS plug-in carries
// nonzero values; other strips stay at 0x00 (cleared each tick).
std::array<uint8_t, 8> g_uf8GrBytes{};

void pushVuMeter()
{
    if (!g_dev || !g_dev->isOpen()) return;

    // UF8 Plugin Mode: scribble strips host FX-Learn-mapped plug-in
    // params, not REAPER tracks — the per-strip Peak-Meter LEDs would
    // show track levels that aren't related to what the strip is
    // controlling. Force all levels to 0 (silent) so the meter row is
    // blank. The s_held cache + g_lastVuLevels dedup below still apply,
    // so we only emit the "all zero" frame on the transition into
    // Plugin Mode; subsequent ticks short-circuit (Frank 2026-05-13).
    const bool pluginModeBlank = g_uf8PluginMode.load();

    const int trackCount = visibleTrackCount();
    const int bankOffset = g_bankOffset.load();
    std::array<uint8_t, 16> levels{};

    // Byte-level hysteresis per strip × channel. REAPER's peak ballistics
    // produce 1-byte drift on continuous audio (block boundaries don't
    // align with the cycle), which flickers the LED at the boundary
    // between two byte values. Up moves go through immediately; a drop
    // is only accepted if the new byte is at least 2 below the held byte
    // — except dropping to silence (raw == 0), which always passes so
    // the bottom LED clears cleanly when audio stops (otherwise held=1
    // would stick forever after any activity, leaving the lowest segment
    // permanently lit).
    static std::array<uint8_t, 16> s_held{};
    auto stepByte = [](uint8_t& held, uint8_t raw) {
        if (raw > held) held = raw;
        else if (raw == 0 || held - raw >= 2) held = raw;
        // else: hold previous
        return held;
    };

    // Tick delta for the ballistic envelope follower. First call seeds
    // the previous timestamp so dt = ~0 and the smoother starts at the
    // first measured value. After that we use real elapsed time.
    const auto tNow = std::chrono::steady_clock::now();
    double dtSec = 0.033;  // safe default ~30 Hz
    if (g_meterEnvInit_) {
        const auto delta = tNow - g_meterEnvLast_;
        dtSec = std::chrono::duration<double>(delta).count();
        if (dtSec <= 0.0)  dtSec = 0.001;
        if (dtSec >  1.0)  dtSec = 1.0;  // clamp gross stalls
    } else {
        for (int i = 0; i < 16; ++i) { g_vuEnvDb_[i] = -120.0; g_rmsEnvPow_[i] = 0.0; }
        g_meterEnvInit_ = true;
    }
    g_meterEnvLast_ = tNow;

    for (int s = 0; s < 8; ++s) {
        const int idx = s + bankOffset;
        uint8_t rawL = 0, rawR = 0;
        if (!pluginModeBlank && idx < trackCount) {
            if (MediaTrack* tr = visibleTrackAt(idx)) {
                // Left = channel 0, right = channel 1. REAPER's peak is
                // the channel's post-fader tap; pre-fader VU isn't
                // exposed via this call, so "in" and "out" end up
                // mirroring each other for mono fader moves. Good
                // enough until a JSFX probe exposes pre-fader.
                rawL = peakToVuByteBallistic(Track_GetPeakInfo(tr, 0),
                                             s * 2 + 0, dtSec);
                rawR = peakToVuByteBallistic(Track_GetPeakInfo(tr, 1),
                                             s * 2 + 1, dtSec);
            }
        }
        levels[s * 2 + 0] = stepByte(s_held[s * 2 + 0], rawL);  // "input"
        levels[s * 2 + 1] = stepByte(s_held[s * 2 + 1], rawR);  // "output"
    }
    // Throttle: peak bytes are 0..31, so a single audio sample drift can
    // toggle one byte every tick → 30 Hz of OUT frames during playback,
    // saturating the queue and pushing latency-sensitive frames (motor-
    // limp on touch press) hundreds of ms behind. Push only when either
    //   (a) some byte moved by ≥ 2 (suppresses jitter), OR
    //   (b) ≥ 50 ms passed AND any byte changed at all (so slow decays
    //       still update smoothly without flooding).
    if (g_vuInit) {
        if (levels == g_lastVuLevels) return;
        uint8_t maxDelta = 0;
        for (size_t i = 0; i < levels.size(); ++i) {
            const uint8_t d = levels[i] > g_lastVuLevels[i]
                            ? levels[i] - g_lastVuLevels[i]
                            : g_lastVuLevels[i] - levels[i];
            if (d > maxDelta) maxDelta = d;
        }
        const auto now = std::chrono::steady_clock::now();
        if (maxDelta < 2 && now - g_lastVuPushTime < std::chrono::milliseconds(50)) {
            return;
        }
        g_lastVuPushTime = now;
    } else {
        g_lastVuPushTime = std::chrono::steady_clock::now();
    }
    g_vuInit = true;
    g_lastVuLevels = levels;
    auto frames = uf8::buildVuMeter(levels);
    g_dev->send(std::move(frames[0]));
    g_dev->send(std::move(frames[1]));
}

void pushSelColourBar()
{
    if (!g_dev || !g_dev->isOpen()) return;

    const int trackCount = visibleTrackCount();
    const int bankOffset = g_bankOffset.load();

    // Determine each visible strip's selection state + compute the mask
    // for the selected strip. Empty slots (past trackCount) default to
    // unselected. The 16-bit mask has bit (strip+1) set for the
    // currently-selected strip (T1=0x02, T2=0x04, …, T8=0x0100).
    uint16_t mask = 0;
    for (int s = 0; s < 8; ++s) {
        const int idx = s + bankOffset;
        MediaTrack* tr = (idx < trackCount) ? visibleTrackAt(idx) : nullptr;
        const bool sel = tr && GetMediaTrackInfo_Value(tr, "I_SELECTED") > 0.5;
        const uint8_t target = sel ? 0xFF : 0x00;
        if (sel) mask |= static_cast<uint16_t>(1 << s);
        if (g_lastSelBright[s] != target) {
            g_lastSelBright[s] = target;
            // White-mode LED update. DAW-Colour byte encoding is still
            // partial; white mode is visually correct even without the
            // per-track palette-to-bytes decode, since unselected=dim
            // and selected=bright matches SSL's default behaviour.
            auto frames = uf8::buildSelWhite(static_cast<uint8_t>(s), sel);
            if (!frames[0].empty()) g_dev->send(std::move(frames[0]));
            if (!frames[1].empty()) g_dev->send(std::move(frames[1]));
        }
    }
    if (mask != g_lastSelMask) {
        g_lastSelMask = mask;
        g_dev->send(uf8::buildSelectedStripMask(mask));
    }
}

// UF8 global-button LED state. We only drive the LEDs that map cleanly
// to REAPER state we can read on every tick: the Automation mode of the
// selected track (Read/Write/Trim/Latch/Touch — radio group), and the
// global Rec/ALL indicator (lit when any track is rec-armed).
//
// The other ~25 global LEDs (Layer/Soft/Modifier/Zoom rows) are not
// wired up yet — they need surface-internal mode tracking which the
// extension doesn't currently model. Cell map for those is in
// docs/uf8-global-led-map.md and Protocol::buildUf8GlobalLed when we
// get to it.
int g_lastAutoMode = -2;          // -1 = no track, 0..5 = REAPER auto modes
int g_lastGlobalAutoMode = -2;    // REAPER's global automation override
                                  // (GetGlobalAutomationOverride). -1 =
                                  // no override, 0..5 = override modes,
                                  // 6 = bypass.
bool g_lastAnyArmed = false;
bool g_lastForcePan = false;
bool g_lastFlip = false;
int  g_lastSoftKeyBank = -1;
bool g_lastShiftHeld = false;
EncoderMode g_lastEncoderMode = EncoderMode::Nav;
int  g_lastPageLeftLit  = -1;     // -1 = unknown / 0 = off / 1 = on
int  g_lastPageRightLit = -1;
int  g_lastPluginLit    = -1;     // -1 = unknown / 0 = dim / 1 = bright (mode)
int  g_lastDomainLed    = -1;     // -1 = unknown, 0 = CS, 1 = BC
int  g_lastActiveLayer  = -1;     // -1 = unknown, 0..2 = Layer 1..3 (Phase B)

// Routing-state cache for the LED-render dedup. Packs sendVpot/sendFader/
// recvVpot/recvFader all-track indices + the two this-track flags into
// one 64-bit key so a single comparison decides whether the SP1..SP8
// row + Flip LED need a re-push. Init to all-bits-set so the first tick
// after load always paints.
uint64_t g_lastRoutingKey = ~uint64_t(0);
bool g_globalLedsInit = false;

// The 6 physical Auto-row cells on the UF8. pushAutoModeLedsMixed_
// walks this list and decides each cell's state based on the binding
// currently in place — see autoCellBinding_ for the per-cell decision.
// LED on/off semantics:
//   - Cells bound to a per-track auto_X builtin light when the SELECTED
//     track's automation mode matches X. REAPER per-track mode 0
//     (Trim/Read) is REAPER's default for every fresh track, so the
//     LED is suppressed there — otherwise TRIM/OFF would be pinned ON
//     for every project (Frank 2026-05-14).
//   - Cells bound to an auto_X_global builtin light when the GLOBAL
//     automation override matches X. Mode 0 is NOT suppressed for the
//     global path — the user actively chose it (vs default -1).
//   - auto_latch / auto_latch_global light their cell for both REAPER
//     mode 4 (Latch) and 5 (Latch Preview).
//   - Cells bound to anything else fall back to boundActionIsActive_
//     so user-remapped Auto buttons follow their new binding's state.
constexpr uf8::Uf8GlobalLed kAutoLeds[6] = {
    uf8::Uf8GlobalLed::AutoOff,
    uf8::Uf8GlobalLed::AutoRead,
    uf8::Uf8GlobalLed::AutoWrite,
    uf8::Uf8GlobalLed::AutoTrim,
    uf8::Uf8GlobalLed::AutoLatch,
    uf8::Uf8GlobalLed::AutoTouch,
};

// Translate a global-LED cell to the bindings::ButtonId that the user
// edits in Settings → Bindings. Returns ButtonId::None for cells with
// no bindable button (Norm/Auto/Rec live elsewhere). Used by
// sendUf8GlobalLed to fold per-binding LED-colour overrides into the
// existing pushUf8GlobalLeds flow without disturbing state logic.
uf8::bindings::ButtonId buttonIdForGlobalLed(uf8::Uf8GlobalLed cell)
{
    using L = uf8::Uf8GlobalLed;
    using B = uf8::bindings::ButtonId;
    switch (cell) {
        case L::Layer1:       return B::Layer1;
        case L::Layer2:       return B::Layer2;
        case L::Layer3:       return B::Layer3;
        case L::Quick1:       return B::Quick1;
        case L::Quick2:       return B::Quick2;
        case L::Quick3:       return B::Quick3;
        case L::Channel:      return B::Channel;
        case L::Btn360:       return B::Btn360;
        case L::SendPlugin1:  return B::SendPlugin1;
        case L::SendPlugin2:  return B::SendPlugin2;
        case L::SendPlugin3:  return B::SendPlugin3;
        case L::SendPlugin4:  return B::SendPlugin4;
        case L::SendPlugin5:  return B::SendPlugin5;
        case L::SendPlugin6:  return B::SendPlugin6;
        case L::SendPlugin7:  return B::SendPlugin7;
        case L::SendPlugin8:  return B::SendPlugin8;
        case L::Plugin:       return B::PluginBtn;
        case L::PageLeft:     return B::PageLeft;
        case L::PageRight:    return B::PageRight;
        case L::Flip:         return B::Flip;
        case L::AutoOff:      return B::AutoOff;
        case L::AutoRead:     return B::AutoRead;
        case L::AutoWrite:    return B::AutoWrite;
        case L::AutoTrim:     return B::AutoTrim;
        case L::AutoLatch:    return B::AutoLatch;
        case L::AutoTouch:    return B::AutoTouch;
        case L::VPotBank:     return B::VPotBank;
        case L::Soft1:        return B::SoftKey1Bank;
        case L::Soft2:        return B::SoftKey2Bank;
        case L::Soft3:        return B::SoftKey3Bank;
        case L::Soft4:        return B::SoftKey4Bank;
        case L::Soft5:        return B::SoftKey5Bank;
        case L::Pan:          return B::Pan;
        case L::Fine:         return B::Fine;
        case L::Nav:          return B::Nav;
        case L::Nudge:        return B::Nudge;
        case L::Focus:        return B::EncFocus;
        case L::BankLeft:     return B::BankLeft;
        case L::BankRight:    return B::BankRight;
        case L::ZoomUp:       return B::ZoomUp;
        case L::ZoomLeft:     return B::ZoomLeft;
        case L::ZoomCenter:   return B::ZoomCenter;
        case L::ZoomRight:    return B::ZoomRight;
        case L::ZoomDown:     return B::ZoomDown;
        // Selection-mode row — bindable since 2026-05-14 (Selection Mode
        // feature). LEDs follow whatever binding the user attaches; the
        // factory ships them unbound so users opt in via Settings.
        case L::Norm:         return B::SelectionNorm;
        case L::Rec:          return B::SelectionRec;
        case L::Auto:         return B::SelectionAuto;
    }
    return B::None;
}

// Resolve which Modifier slot drives the LED right now. While Shift /
// Cmd / Ctrl is physically held, the corresponding modifier slot wins
// — so a bound Shift+Pan with a custom colour previews that colour on
// the Pan LED for as long as Shift is down. Release returns to Plain.
// This mirrors the press-time behaviour: dispatch() snapshots the same
// modifier when the button actually fires, so what you see is what
// you'll get.
uf8::bindings::Modifier liveModifierForLed_()
{
    using M = uf8::bindings::Modifier;
    // Precedence matches dispatch(): Ctrl > Cmd > Shift > Plain.
    if (uf8::bindings::modifierHeld(M::Ctrl))  return M::Ctrl;
    if (uf8::bindings::modifierHeld(M::Cmd))   return M::Cmd;
    if (uf8::bindings::modifierHeld(M::Shift)) return M::Shift;
    return M::Plain;
}

// If the user has set a non-default colour on the binding for this
// cell on the active layer, return it as an LedColour ready for the
// 3-arg buildUf8GlobalLed overload. Otherwise std::nullopt = caller
// uses the cell's table-default colour. The "user customised" guard
// is "rgb != white"; default {0xFF,0xFF,0xFF} → factory bindings stay
// visually identical to today, only deliberate edits surface.
//
// Color-from-binding only — state (when LED is on/off, Bright/Dim)
// stays driven by main.cpp's existing logic except for the modifier-
// preview path in sendUf8GlobalLed which forces Bright when a held
// modifier has a non-noop slot here.
// Resolve the (state, colour) tuple for a global LED cell from the
// active layer's binding. Honours Brightness::Off/Dim/Bright from the
// LedOverride / Binding so the user can set "Active=Off" or
// "Active=Dim" and have it actually render that way — until 2026-05-06
// we ignored brightness entirely and used only the caller's state,
// which made every active LED bright regardless of binding settings.
struct ResolvedLed {
    uf8::GlobalLedState           state;
    std::optional<uf8::LedColour> colour;  // nullopt → use cell's table-default colour
};

uf8::GlobalLedState brightnessToState_(uf8::bindings::Brightness b)
{
    switch (b) {
        case uf8::bindings::Brightness::Off:    return uf8::GlobalLedState::Off;
        case uf8::bindings::Brightness::Dim:    return uf8::GlobalLedState::Dim;
        case uf8::bindings::Brightness::Bright: return uf8::GlobalLedState::Bright;
    }
    return uf8::GlobalLedState::Dim;
}

ResolvedLed resolveLed_(uf8::Uf8GlobalLed cell,
                         bool active,
                         uf8::bindings::Modifier mod,
                         bool useLongPressSlot = false)
{
    ResolvedLed r;
    r.state = active ? uf8::GlobalLedState::Bright
                      : uf8::GlobalLedState::Dim;
    const auto bid = buttonIdForGlobalLed(cell);
    if (bid == uf8::bindings::ButtonId::None) return r;
    const int activeLayer = uf8::bindings::getActiveLayer();

    // Layer indicator LEDs (Layer1/2/3) are system state indicators,
    // NOT user-chosen feature LEDs — they have to follow activeLayer
    // regardless of whether the user has a binding for them on the
    // active layer. Without this bypass, switching to a layer whose
    // bindings map is missing the corresponding Layer1/2/3 entry
    // would leave the indicator dark (observed for Layer 3 on
    // long-lived configs, 2026-05-13). Default-render bright white
    // when the cell is asked to be Bright; respect the binding if
    // one happens to exist for colour/brightness overrides.
    const bool isLayerIndicator =
        cell == uf8::Uf8GlobalLed::Layer1
     || cell == uf8::Uf8GlobalLed::Layer2
     || cell == uf8::Uf8GlobalLed::Layer3;
    // Per-(Layer, Quick) Sub-Bank LED override. When a Quick is engaged
    // on the active layer, the V-POT / Soft 1-5 buttons render with
    // colours from g_cfg.userQuicks[layer].quicks[quick].subBankLeds[sb]
    // instead of the layer-wide binding. Lets each (L, Q) coordinate
    // light its sub-bank row distinctly (Frank 2026-05-13: "Soll auch
    // pro Quick-Layer einstellbar sein").
    int sbIdx = -1;
    switch (cell) {
        case uf8::Uf8GlobalLed::VPotBank: sbIdx = 0; break;
        case uf8::Uf8GlobalLed::Soft1:    sbIdx = 1; break;
        case uf8::Uf8GlobalLed::Soft2:    sbIdx = 2; break;
        case uf8::Uf8GlobalLed::Soft3:    sbIdx = 3; break;
        case uf8::Uf8GlobalLed::Soft4:    sbIdx = 4; break;
        case uf8::Uf8GlobalLed::Soft5:    sbIdx = 5; break;
        default: break;
    }
    // UF8 Plugin Mode: Sub-Bank cells (V-POT/Soft 1-5) are no-function
    // in this mode (bank navigation moved to the 8 TopSoftKeys). Force
    // their LEDs to plain Dim white so the row stays visibly there
    // but doesn't track any active/inactive state (Frank 2026-05-13:
    // "Soft-Key Banks machen LED mit — sollen sie nicht — alle dimm").
    if (sbIdx >= 0 && g_uf8PluginMode.load()) {
        r.state  = uf8::GlobalLedState::Dim;
        r.colour = uf8::ledColourForTrackRgb(0xFFFFFF);
        return r;
    }
    if (sbIdx >= 0 && activeLayer >= 0 && activeLayer <= 2) {
        const int engagedQ = g_activeQuick[activeLayer].load();
        if (engagedQ >= 0 && engagedQ < uf8::bindings::kQuicksPerLayer) {
            const auto a = uf8::bindings::getSubBankLed(
                activeLayer, engagedQ, sbIdx);
            uint8_t rgb[3];
            uf8::bindings::Brightness bri;
            if (active) {
                rgb[0] = a.color[0]; rgb[1] = a.color[1]; rgb[2] = a.color[2];
                bri    = a.brightness;
            } else {
                rgb[0] = a.inactiveColor[0]; rgb[1] = a.inactiveColor[1];
                rgb[2] = a.inactiveColor[2];
                bri    = a.inactiveBrightness;
            }
            r.state = brightnessToState_(bri);
            const uint32_t packed = (uint32_t(rgb[0]) << 16)
                                  | (uint32_t(rgb[1]) << 8)
                                  |  uint32_t(rgb[2]);
            r.colour = uf8::ledColourForTrackRgb(packed);
            return r;
        }
    }

    if (!uf8::bindings::hasBinding(activeLayer, bid)) {
        if (isLayerIndicator) {
            // No binding → fall back to plain bright/dim with the
            // hardware-default colour (white from the protocol table).
            // r.state already carries the caller's intent.
            r.colour = uf8::ledColourForTrackRgb(0xFFFFFF);
            return r;
        }
        // Frank 2026-05-07: every other LED is user-chosen, full stop.
        // Without a binding entry the cell stays dark.
        r.state = uf8::GlobalLedState::Off;
        return r;
    }

    // Layer indicator special-case (Layer1/2/3). These are system state
    // indicators — they MUST be visible when the layer is active,
    // regardless of any user-customised brightness on the binding.
    // Otherwise a stale brightness=Off in the binding would leave the
    // hardware showing "no layer active" (Frank 2026-05-13: "Layer 3
    // LED leuchtet nicht"). Allow colour customisation from the
    // binding, but force the state to caller's intent (Bright when
    // active, Dim otherwise).
    if (isLayerIndicator) {
        const auto bd = uf8::bindings::getBinding(activeLayer, bid);
        const uint32_t packed = (uint32_t(bd.color[0]) << 16)
                              | (uint32_t(bd.color[1]) << 8)
                              |  uint32_t(bd.color[2]);
        r.state  = active ? uf8::GlobalLedState::Bright
                          : uf8::GlobalLedState::Dim;
        r.colour = uf8::ledColourForTrackRgb(packed);
        return r;
    }

    const auto bd = uf8::bindings::getBinding(activeLayer, bid);
    // Pick the slot the active state is being driven by. Long-press
    // actions (e.g. send_this on FLIP) live in bd.longPress[]; reading
    // their LedOverride requires honouring lastFiredWasLongPress so the
    // user's per-long-press colour choice in the Bindings UI lands.
    const auto& slot = useLongPressSlot
                         ? bd.longPress[static_cast<int>(mod)]
                         : bd.shortPress[static_cast<int>(mod)];

    // Slot has no action to be "active" against, but the binding entry
    // exists — user opened the editor and customised the LED without
    // assigning an action. Render the Active appearance so their
    // bd.color/brightness choice is actually visible. Without this the
    // resolver fell through to effectiveLedInactive (= bd.inactiveColor
    // @ Dim default), and a freshly-customised button looked dark on
    // hardware regardless of what the user picked.
    bool useActive = active;
    if (!useActive && slot.type == uf8::bindings::ActionType::Noop) {
        useActive = true;
    }

    uint8_t rgb[3];
    uf8::bindings::Brightness bri;
    if (useActive) uf8::bindings::effectiveLedActive  (bd, slot, rgb, bri);
    else           uf8::bindings::effectiveLedInactive(bd, slot, rgb, bri);
    r.state = brightnessToState_(bri);
    const uint32_t packed = (uint32_t(rgb[0]) << 16)
                          | (uint32_t(rgb[1]) << 8)
                          |  uint32_t(rgb[2]);
    r.colour = uf8::ledColourForTrackRgb(packed);
    return r;
}

// "Is the bound action currently engaged?" Used by the stateless-cell
// sweep below. Stateful actions (builtin with stateOf, REAPER toggle
// action) report their actual state. Stateless actions (one-shot
// REAPER, builtin without stateOf, keyboard, MIDI) report INACTIVE so
// the LED shows the binding's inactive colour/brightness — earlier
// "treat stateless as active" rendered Channel + every zoom button
// permanently bright (Frank 2026-05-06). Press feedback through the
// builtin handler (sendUf8GlobalLed(led, true) at press, false at
// release) still gives the brief highlight users expect.
bool bindingHasActiveSlot_(const uf8::bindings::Binding& bd)
{
    using AT = uf8::bindings::ActionType;
    auto slotActive = [](const uf8::bindings::ActionSlot& s) -> bool {
        switch (s.type) {
            case AT::Noop: return false;
            case AT::Builtin:
                return uf8::bindings::builtinHasState(s.action)
                    && uf8::bindings::builtinStateOf(s.action, s.param);
            case AT::Reaper: {
                if (s.action.empty()) return false;
                int aid = std::atoi(s.action.c_str());
                if (aid <= 0) aid = NamedCommandLookup(s.action.c_str());
                if (aid > 0) {
                    const int st = GetToggleCommandState2(
                        SectionFromUniqueID(0), aid);
                    if (st >= 0) return st == 1;
                }
                return false;
            }
            case AT::Keyboard:
            case AT::Midi:
                return false;
        }
        return false;
    };
    // Walk all modifier slots (short + long press) — a stateful action
    // bound to e.g. Shift+Btn360 should light the cell after toggling
    // even when Plain is Noop. Matches the routing-row LED convention.
    for (const auto& s : bd.shortPress) if (slotActive(s)) return true;
    for (const auto& s : bd.longPress)  if (slotActive(s)) return true;
    return false;
}

bool boundActionIsActive_(uf8::bindings::ButtonId bid)
{
    if (bid == uf8::bindings::ButtonId::None) return false;
    return bindingHasActiveSlot_(uf8::bindings::getBinding(
        uf8::bindings::getActiveLayer(), bid));
}



// True when the currently-held modifier has a bindable, non-noop slot
// on this cell — i.e. holding the modifier "armed" an action that a
// press would fire. Used to force-Bright the LED while previewing.
bool modifierSlotArmed_(uf8::Uf8GlobalLed cell, uf8::bindings::Modifier mod)
{
    if (mod == uf8::bindings::Modifier::Plain) return false;
    const auto bid = buttonIdForGlobalLed(cell);
    if (bid == uf8::bindings::ButtonId::None) return false;
    const auto bd = uf8::bindings::getBinding(
        uf8::bindings::getActiveLayer(), bid);
    const auto& slot = bd.shortPress[static_cast<int>(mod)];
    return slot.type != uf8::bindings::ActionType::Noop;
}

// Wrapper around sendLedFrames(buildUf8GlobalLed(...)) that folds in
// the user's per-binding colour AND brightness, plus the modifier-
// preview path. Use this instead of the raw 2-arg buildUf8GlobalLed
// at every global-LED push site.
//
// `callerState` is the state-machine's "is this cell active right now?"
// answer (Bright = active, Dim/Off = inactive). The binding's LED
// brightness setting (Off/Dim/Bright) replaces it, so a user can set
// "Active=Off" or "Active=Dim" and have it actually render.
//
// Behaviour matrix (mod = currently-held modifier, lastFired = the
// slot whose action last actually fired on this button):
//
//   mod held, slot armed, callerActive=false                 → preview Dim with mod slot's INACTIVE
//   mod held, slot armed, callerActive=true, lastFired!=mod  → preview Dim with mod slot's INACTIVE
//   mod held, slot armed, callerActive=true, lastFired==mod  → state-driven via mod slot's ACTIVE
//   no mod held / slot not armed                             → state-driven with lastFiredModifier promotion
// Per-cell push cache so sendUf8GlobalLed can be called every tick
// without hammering the OUT endpoint. Keyed on (state, packed RGB |
// 0x10000 sentinel for "use table colour"). Cleared when the bindings
// generation bumps (a colour edit in Settings invalidates everything)
// and on g_globalLedsInit reset (project load / reopen).
struct GlobalLedKey {
    int  state    = -1;
    int  colour32 = -1;  // 0xRRGGBB, or 0x10000 for "no override"
    bool valid    = false;
};
constexpr size_t kGlobalLedCacheSize = 64;  // Uf8GlobalLed enum has ~46 entries
std::array<GlobalLedKey, kGlobalLedCacheSize> g_lastGlobalLedPush{};

void invalidateGlobalLedCache_()
{
    for (auto& k : g_lastGlobalLedPush) k.valid = false;
}

void invalidateGlobalLedCell_(uf8::Uf8GlobalLed cell)
{
    const size_t idx = static_cast<size_t>(cell);
    if (idx < g_lastGlobalLedPush.size()) {
        g_lastGlobalLedPush[idx].valid = false;
    }
}

void sendUf8GlobalLed(uf8::Uf8GlobalLed cell, uf8::GlobalLedState callerState)
{
    const bool callerActive = (callerState == uf8::GlobalLedState::Bright);
    const auto mod = liveModifierForLed_();
    const bool armed = (mod != uf8::bindings::Modifier::Plain) &&
                        modifierSlotArmed_(cell, mod);
    bool lastFiredMatchesHeld = false;
    if (armed) {
        const auto bid = buttonIdForGlobalLed(cell);
        if (bid != uf8::bindings::ButtonId::None) {
            lastFiredMatchesHeld =
                (uf8::bindings::lastFiredModifier(bid) == mod);
        }
    }

    ResolvedLed r;
    if (armed && !(callerActive && lastFiredMatchesHeld)) {
        // Preview: render the held modifier slot's INACTIVE
        // appearance (colour + brightness). Preview is a hold-state
        // signal, not a press-time decision, so always read the
        // short-press slot for the held modifier.
        r = resolveLed_(cell, /*active*/ false, mod, /*long*/ false);
    } else {
        // Normal path. For the active branch, resolve from the slot
        // whose action last actually fired — including whether it was
        // a long-press (so e.g. send_this active on FLIP renders the
        // user's long-press LedOverride colour).
        uf8::bindings::Modifier resolveMod = uf8::bindings::Modifier::Plain;
        bool useLongPress = false;
        if (callerActive) {
            const auto bid = buttonIdForGlobalLed(cell);
            if (bid != uf8::bindings::ButtonId::None) {
                resolveMod   = uf8::bindings::lastFiredModifier(bid);
                useLongPress = uf8::bindings::lastFiredWasLongPress(bid);
            }
        }
        r = resolveLed_(cell, callerActive, resolveMod, useLongPress);
    }

    // Per-cell dedup. Same (state, colour) as last push → skip USB
    // write. Lets the caller poll us every tick (which it must, so
    // mixer-toggle-driven Btn360 brightens the moment the mixer
    // window opens — Frank 2026-05-06 flagged that without this
    // cache, Btn360 only updates after some other state change
    // happens to invalidate the outer dedup).
    const size_t idx = static_cast<size_t>(cell);
    int colour32 = 0x10000;  // "no override"
    if (r.colour) {
        // Encode the LedColour bytes deterministically. LedColour has
        // four bytes (aBright/bBright/aDim/bDim) but the same input
        // RGB always produces the same LedColour, so packing
        // {aBright,bBright,aDim,bDim} into 32 bits is stable.
        colour32 = (int(r.colour->aBright) << 24)
                 | (int(r.colour->bBright) << 16)
                 | (int(r.colour->aDim)    <<  8)
                 |  int(r.colour->bDim);
    }
    if (idx < g_lastGlobalLedPush.size()) {
        auto& last = g_lastGlobalLedPush[idx];
        if (last.valid && last.state == static_cast<int>(r.state) &&
            last.colour32 == colour32) {
            return;
        }
        last.state    = static_cast<int>(r.state);
        last.colour32 = colour32;
        last.valid    = true;
    }

    if (r.colour) {
        sendLedFrames(uf8::buildUf8GlobalLed(cell, r.state, *r.colour));
    } else {
        sendLedFrames(uf8::buildUf8GlobalLed(cell, r.state));
    }
}

void sendUf8GlobalLed(uf8::Uf8GlobalLed cell, bool on)
{
    sendUf8GlobalLed(cell,
        on ? uf8::GlobalLedState::Bright : uf8::GlobalLedState::Dim);
}

// Per-cell binding lookup for the Auto row. Returns the REAPER mode the
// cell should light for + whether the binding is the GLOBAL variant
// (auto_X_global) or the per-track variant (auto_X). targetMode == -1
// means the cell isn't bound to an auto_* builtin — caller falls back
// to boundActionIsActive_ so user-remapped Auto buttons still light.
struct AutoCellBinding {
    int  targetMode = -1;
    bool isGlobal   = false;
};

AutoCellBinding autoCellBinding_(uf8::Uf8GlobalLed cell, int activeLayer)
{
    AutoCellBinding out;
    const auto bid = buttonIdForGlobalLed(cell);
    if (bid == uf8::bindings::ButtonId::None) return out;
    if (!uf8::bindings::hasBinding(activeLayer, bid)) return out;
    const auto bd = uf8::bindings::getBinding(activeLayer, bid);
    const auto& sp = bd.shortPress[static_cast<int>(
        uf8::bindings::Modifier::Plain)];
    if (sp.type != uf8::bindings::ActionType::Builtin) return out;

    struct Row { const char* name; int mode; bool global; };
    static constexpr Row kMap[] = {
        {"auto_off",              0, false},
        {"auto_trim",             0, false},
        {"auto_read",             1, false},
        {"auto_touch",            2, false},
        {"auto_write",            3, false},
        {"auto_latch",            4, false},
        {"auto_latch_prv",        5, false},
        {"auto_off_global",       0, true },
        {"auto_trim_global",      0, true },
        {"auto_read_global",      1, true },
        {"auto_touch_global",     2, true },
        {"auto_write_global",     3, true },
        {"auto_latch_global",     4, true },
        {"auto_latch_prv_global", 5, true },
    };
    for (const auto& r : kMap) {
        if (sp.action == r.name) {
            out.targetMode = r.mode;
            out.isGlobal   = r.global;
            return out;
        }
    }
    return out;
}

// Push the 6-button Auto LED state. Each cell is decided independently
// from its binding's primary builtin — see kAutoLeds for the semantics.
// Used both by the periodic LED refresh in pushUf8GlobalLeds and by the
// auto_* press handlers, which pre-empt the firmware's transition flash
// through TRIM that would otherwise be visible during the ~33 ms gap
// before our next tick reads the new mode back from REAPER.
void pushAutoModeLedsMixed_(int perTrackMode, int globalMode,
                            int activeLayer)
{
    if (!g_dev || !g_dev->isOpen()) return;
    for (auto cell : kAutoLeds) {
        const auto ab = autoCellBinding_(cell, activeLayer);
        bool active = false;
        if (ab.targetMode < 0) {
            // Non-auto binding — defer to its own active-state.
            const auto bid = buttonIdForGlobalLed(cell);
            if (bid != uf8::bindings::ButtonId::None) {
                active = boundActionIsActive_(bid);
            }
        } else {
            const int src = ab.isGlobal ? globalMode : perTrackMode;
            active = (src == ab.targetMode);
            // auto_latch covers REAPER mode 5 (Latch Preview) too.
            if (ab.targetMode == 4 && src == 5) active = true;
            // Per-track mode 0 is REAPER's default for every fresh
            // track; suppress so OFF/TRIM don't get pinned ON. Global
            // mode 0 is NOT suppressed — user explicitly chose it.
            if (ab.targetMode == 0 && !ab.isGlobal) active = false;
        }
        sendUf8GlobalLed(cell, active ? uf8::GlobalLedState::Bright
                                      : uf8::GlobalLedState::Off);
    }
    g_lastAutoMode       = perTrackMode;
    g_lastGlobalAutoMode = globalMode;
}

// Layer 1/2/3 radio LEDs — one bright, two dim. Used both by the
// LED-refresh dedup in pushUf8GlobalLeds and by the layer_select press
// handler so the LED moves on the same tick the user presses (matches
// pushAutoModeLeds's role on the Auto row).
void pushLayerLeds(int active)
{
    if (!g_dev || !g_dev->isOpen()) return;
    sendUf8GlobalLed(uf8::Uf8GlobalLed::Layer1, active == 0);
    sendUf8GlobalLed(uf8::Uf8GlobalLed::Layer2, active == 1);
    sendUf8GlobalLed(uf8::Uf8GlobalLed::Layer3, active == 2);
    g_lastActiveLayer = active;
}

void pushUf8GlobalLeds()
{
    if (!g_dev || !g_dev->isOpen()) return;

    // Automation mode of the focused/selected track. REAPER values:
    //   0 = Trim/Read, 1 = Read, 2 = Touch, 3 = Write, 4 = Latch,
    //   5 = Latch Preview.
    int autoMode = -1;
    MediaTrack* sel = GetSelectedTrack(nullptr, 0);
    if (sel) autoMode = GetTrackAutomationMode(sel);

    // Any armed track in the project — drives the global Rec LED.
    bool anyArmed = false;
    const int n = CountTracks(nullptr);
    for (int i = 0; i < n; ++i) {
        if (GetMediaTrackInfo_Value(GetTrack(nullptr, i), "I_RECARM") > 0.5) {
            anyArmed = true; break;
        }
    }

    const bool forcePan        = g_forcePan.load();
    const bool flip            = g_flip.load();
    const bool shiftHeld       = g_shiftHeld.load();
    const EncoderMode encMode  = g_encoderMode.load();
    const int  softKeyBank     = g_softKeyBank.load();
    const int  domainLed       = (uf8::getFocusedParam().domain == uf8::Domain::BusComp)
                                     ? 1 : 0;
    const int  activeLayer     = uf8::bindings::getActiveLayer();

    // Send/Receive routing state — drives the Send/Plugin row LEDs and
    // the Flip LED colour override.
    const int  sendVAll  = g_sendVpotAllIdx.load();
    const int  sendFAll  = g_sendFaderAllIdx.load();
    const int  recvVAll  = g_recvVpotAllIdx.load();
    const int  recvFAll  = g_recvFaderAllIdx.load();
    const bool sendThis  = g_sendVpotThisTrack.load() || g_sendFaderThisTrack.load();
    const bool recvThis  = g_recvVpotThisTrack.load() || g_recvFaderThisTrack.load();
    // -1 → 0xFF (unsigned), packed into bytes — the all-bits-set sentinel
    // for the cache makes the first tick always repaint.
    const uint64_t routingKey =
          (uint64_t(uint8_t(sendVAll)) <<  0)
        | (uint64_t(uint8_t(sendFAll)) <<  8)
        | (uint64_t(uint8_t(recvVAll)) << 16)
        | (uint64_t(uint8_t(recvFAll)) << 24)
        | (uint64_t(sendThis ? 1 : 0)  << 32)
        | (uint64_t(recvThis ? 1 : 0)  << 33);

    // Page Left LED — confirmed via probe 2026-04-30 to live at cell
    // 0x2D (NOT 0x5D as cap35/36 originally suggested — that earlier
    // assumption is what caused the "2 buttons selected" surface bug
    // in 2026-04-28; 0x5D actually lights Soft 2). Page navigates the
    // soft-key bank with wrap-around so the LED stays lit always while
    // the extension is active. Page Right LED still unwired — its
    // real cell hasn't been discovered (0x5C is suspect).

    // Plugin button: bright while ANY bound slot's stateful action is
    // active — covers Plain → ssl_strip_mode_toggle (g_pluginFaderMode)
    // AND Shift → uf8_plugin_mode_toggle (g_uf8PluginMode) together, so
    // toggling either modifier-slot binding keeps the LED lit after the
    // modifier key is released. Falls back to g_pluginFaderMode when no
    // bound builtin reports state (e.g. user wiped the binding).
    const bool pluginActive =
        boundActionIsActive_(uf8::bindings::ButtonId::PluginBtn)
        || g_pluginFaderMode.load()
        || g_uf8PluginMode.load();
    const int pluginLit = pluginActive ? 1 : 0;

    // Bindings generation — bumped on any setBinding/clearBinding/load/
    // import. Polling here keeps colour edits in Settings → Bindings
    // visible on the next tick without needing a press to dirty state.
    static uint64_t s_lastBindingsGen = 0;
    const uint64_t bindingsGen = uf8::bindings::generation();
    if (bindingsGen != s_lastBindingsGen) {
        g_globalLedsInit = false;
        invalidateGlobalLedCache_();
        s_lastBindingsGen = bindingsGen;
    }

    // Live modifier — affects every LED via sendUf8GlobalLed's preview
    // path. Track edges so the row repaints on Shift / Cmd / Ctrl
    // press AND release.
    const uf8::bindings::Modifier liveMod = liveModifierForLed_();
    static uf8::bindings::Modifier s_lastLiveMod = uf8::bindings::Modifier::Plain;
    if (liveMod != s_lastLiveMod) {
        g_globalLedsInit = false;
        invalidateGlobalLedCache_();
        s_lastLiveMod = liveMod;
    }

    if (!g_globalLedsInit) invalidateGlobalLedCache_();

    // No outer early-return any more. Each section below has its own
    // dedup against last-known state, AND sendUf8GlobalLed has a
    // per-cell cache so re-evaluating cheaply skips redundant USB
    // writes. The stateless-cell sweep at the bottom NEEDS this
    // every-tick run — its active state comes from runtime queries
    // (mixer-window visibility, REAPER toggle commands) that aren't
    // in any of the dedup keys. Without this, Btn360 only redrew
    // after some unrelated state change happened to break the gate
    // (Frank 2026-05-06).

    const int globalAutoMode = GetGlobalAutomationOverride();
    if (autoMode      != g_lastAutoMode
     || globalAutoMode != g_lastGlobalAutoMode
     || !g_globalLedsInit)
    {
        pushAutoModeLedsMixed_(autoMode, globalAutoMode, activeLayer);
    }

    // Rec LED no longer driven by anyArmed — it's now a bindable
    // Selection-Mode button (ButtonId::SelectionRec) and goes through
    // the stateless-cell sweep at the bottom so the binding's
    // colour / brightness apply. g_lastAnyArmed kept updated for any
    // other consumers that may still read it (Frank 2026-05-14).
    g_lastAnyArmed = anyArmed;

    // Pan LED — driven through the stateless-cell sweep below so the
    // binding's combined short+long-press state lights the cell. The
    // older hardcoded `forcePan` path lost the long-press LED state on
    // any unrelated refresh (Frank 2026-05-14: folder_mode long-press
    // on PAN went dim after bank switch / pan toggle). g_lastForcePan
    // stays touched for callers that still read it.
    g_lastForcePan = forcePan;

    // FLIP LED — bright when V-Pots are being used for something OTHER
    // than pan: flip toggle on (V-Pots driving plug-in params), or any
    // routing builtin that targets V-Pots (send_this / recv_this /
    // specific-slot send/recv with "Flip onto V-Pots" checked). Fader-
    // routing variants leave the V-Pots free for pan and so they do
    // NOT light FLIP (Frank 2026-05-07: pressing CHANNEL bound to "8
    // sends of focused track" with default-Faders was lighting FLIP
    // even though the V-Pots stayed in pan mode).
    const bool vpotRoutingActive =
           g_sendVpotThisTrack.load()
        || g_recvVpotThisTrack.load()
        || g_sendVpotAllIdx.load() >= 0
        || g_recvVpotAllIdx.load() >= 0;
    sendUf8GlobalLed(uf8::Uf8GlobalLed::Flip, flip || vpotRoutingActive);
    g_lastFlip = flip;

    // Shift/Fine LED — momentary, follows the held state of 0x6F.
    if (shiftHeld != g_lastShiftHeld || !g_globalLedsInit) {
        sendUf8GlobalLed(uf8::Uf8GlobalLed::Fine, shiftHeld);
        g_lastShiftHeld = shiftHeld;
    }

    // Soft-key bank LEDs — exactly one of V-POT / Soft 1..5 is lit.
    // In user-Quick context the row tracks g_activeSubBank[layer] (0..5);
    // otherwise it tracks the SSL plug-in's PAGE bank in g_softKeyBank.
    // Always call sendUf8GlobalLed every tick — the per-cell dedup in
    // sendUf8GlobalLed catches no-change cases. An outer dedup on the
    // selected sub-bank would block colour edits via the per-(L, Q)
    // override editor whenever the user keeps the same active
    // sub-bank (Frank 2026-05-13).
    const int activeQuickForBanks = (activeLayer >= 0 && activeLayer <= 2)
                                    ? g_activeQuick[activeLayer].load() : -1;
    const int subBankForLeds      = (activeQuickForBanks >= 0
                                     && activeLayer >= 0 && activeLayer <= 2)
                                    ? g_activeSubBank[activeLayer].load()
                                    : softKeyBank;
    sendUf8GlobalLed(uf8::Uf8GlobalLed::VPotBank, subBankForLeds == 0);
    sendUf8GlobalLed(uf8::Uf8GlobalLed::Soft1,    subBankForLeds == 1);
    sendUf8GlobalLed(uf8::Uf8GlobalLed::Soft2,    subBankForLeds == 2);
    sendUf8GlobalLed(uf8::Uf8GlobalLed::Soft3,    subBankForLeds == 3);
    sendUf8GlobalLed(uf8::Uf8GlobalLed::Soft4,    subBankForLeds == 4);
    sendUf8GlobalLed(uf8::Uf8GlobalLed::Soft5,    subBankForLeds == 5);
    g_lastSoftKeyBank = subBankForLeds;

    // Page LEDs intentionally not driven — see comment at the top of
    // pushUf8GlobalLeds explaining the cell collision with Soft 2/3.

    // Plugin button — bright while ANY bound modifier-slot action is
    // active (see pluginLit above). Pushed every tick because the
    // colour also depends on lastFiredModifier(PluginBtn), which can
    // flip between Plain ↔ Shift slots while pluginLit itself stays at
    // 1 (e.g. both g_pluginFaderMode and g_uf8PluginMode true). The
    // per-cell cache in sendUf8GlobalLed dedupes redundant USB writes.
    sendUf8GlobalLed(uf8::Uf8GlobalLed::Plugin, pluginLit == 1);
    g_lastPluginLit = pluginLit;

    // Quick 1 / Quick 2 / Quick 3 LEDs — driven by whatever action is
    // bound, via boundActionIsActive_. Earlier this was hardcoded to
    // domainLed (ChannelStrip ↔ BusComp), which chaseLastTouchedFx
    // could clobber the same frame the user pressed Q1/Q2 — the LED
    // flickered or never lit (Frank 2026-05-12 "Quick Key 2 selbst
    // wird nicht aktiv wenn gewählt"). With stateful bindings
    // (domain_cs / domain_bc / user_domain_*) each Q LED reflects
    // its bound action's reality directly. Cells: Q1 0x3C, Q2 0x3B,
    // Q3 0x3A (probe 2026-04-30).
    {
        constexpr uf8::Uf8GlobalLed kQuickLeds[] = {
            uf8::Uf8GlobalLed::Quick1,
            uf8::Uf8GlobalLed::Quick2,
            uf8::Uf8GlobalLed::Quick3,
        };
        for (auto led : kQuickLeds) {
            const auto bid = buttonIdForGlobalLed(led);
            sendUf8GlobalLed(led, boundActionIsActive_(bid));
        }
    }
    // domainLed dedup keeper — still drives a couple of indirect
    // consumers (the BC carousel chase). Touching it here keeps the
    // change/refresh signalling alive.
    g_lastDomainLed = domainLed;

    // Layer 1/2/3 LEDs — radio group reflecting bindings::getActiveLayer().
    // Always pushed (no outer dedup): per-cell dedup in sendUf8GlobalLed
    // catches no-change. An outer activeLayer != g_lastActiveLayer gate
    // would block re-pushes when the user edits the Layer LED's colour
    // / brightness via the binding editor without switching layers
    // (Frank 2026-05-13: "Layer 3 LED leuchtet nicht").
    pushLayerLeds(activeLayer);

    // Stateless-cell sweep: cells that have no hardcoded state machine
    // in this pusher (Btn360, Channel, BankL/R, Zoom*, Norm/Rec/Auto)
    // — drive their Bright/Dim from the bound builtin's stateOf, same
    // pattern as the encoder-mode block below. A user-bound toggle
    // (e.g. mixer_toggle on Btn360, selection_mode_rec on Rec) lights
    // up bright while engaged; a bound momentary builtin returns false
    // from stateOf so the cell stays dim until the press handler
    // flashes it. Norm/Rec/Auto live here so the binding editor's
    // colour + brightness overrides apply (Frank 2026-05-14).
    {
        constexpr uf8::Uf8GlobalLed kStateless[] = {
            uf8::Uf8GlobalLed::Btn360,
            uf8::Uf8GlobalLed::Channel,
            uf8::Uf8GlobalLed::BankLeft, uf8::Uf8GlobalLed::BankRight,
            uf8::Uf8GlobalLed::ZoomUp, uf8::Uf8GlobalLed::ZoomDown,
            uf8::Uf8GlobalLed::ZoomLeft, uf8::Uf8GlobalLed::ZoomRight,
            uf8::Uf8GlobalLed::ZoomCenter,
            uf8::Uf8GlobalLed::Norm, uf8::Uf8GlobalLed::Rec,
            uf8::Uf8GlobalLed::Auto,
            uf8::Uf8GlobalLed::Pan,
        };
        for (auto led : kStateless) {
            const auto bid = buttonIdForGlobalLed(led);
            const bool active = boundActionIsActive_(bid);
            sendUf8GlobalLed(led, active);
        }
    }

    // Send/Plugin 1..8 LEDs — bright when the matching send/recv index
    // is the active routing target on EITHER physical output (V-Pot or
    // Fader). Same row drives both sends and receives by binding
    // convention (Plain = sends, Shift+ = receives), so the LED can't
    // distinguish the two — it just shows "this index is currently the
    // routing target".
    if (routingKey != g_lastRoutingKey || !g_globalLedsInit) {
        constexpr uf8::Uf8GlobalLed kSpLeds[8] = {
            uf8::Uf8GlobalLed::SendPlugin1, uf8::Uf8GlobalLed::SendPlugin2,
            uf8::Uf8GlobalLed::SendPlugin3, uf8::Uf8GlobalLed::SendPlugin4,
            uf8::Uf8GlobalLed::SendPlugin5, uf8::Uf8GlobalLed::SendPlugin6,
            uf8::Uf8GlobalLed::SendPlugin7, uf8::Uf8GlobalLed::SendPlugin8,
        };
        for (int i = 0; i < 8; ++i) {
            const bool sendHit = (sendVAll == i || sendFAll == i);
            const bool recvHit = (recvVAll == i || recvFAll == i);
            const bool active  = sendHit || recvHit;
            sendUf8GlobalLed(kSpLeds[i], active);
        }
    }
    g_lastRoutingKey = routingKey;

    // Page Left / Page Right LEDs — dim baseline, bright momentarily
    // while held (driven from the press handler, not here). At init
    // we just ensure both are at known dim state. Cells PL 0x2D,
    // PR 0x2C (cap48 2026-04-30). Both are 3-state legacy LEDs.
    if (!g_globalLedsInit) {
        sendUf8GlobalLed(uf8::Uf8GlobalLed::PageLeft, false);
        sendUf8GlobalLed(uf8::Uf8GlobalLed::PageRight, false);
        g_lastPageLeftLit = 0;
        g_lastPageRightLit = 0;
    }

    // Channel-encoder mode LEDs. Driven by the binding actually wired to
    // each cell, NOT by a hardcoded mode comparison — so when the user
    // remaps e.g. the FOCUS button to `encoder_instance`, its LED lights
    // bright in Instance mode instead of Focus mode. Each mode builtin
    // (encoder_nav/nudge/focus/instance) exposes a stateOf that returns
    // true when its mode is active; we look up the bound builtin name
    // for each cell's ButtonId and ask its stateOf.
    if (encMode != g_lastEncoderMode || !g_globalLedsInit) {
        const int activeLayer = uf8::bindings::getActiveLayer();
        auto cellActive = [&](uf8::bindings::ButtonId id) -> bool {
            const auto bd = uf8::bindings::getBinding(activeLayer, id);
            const auto& sp = bd.shortPress[
                static_cast<int>(uf8::bindings::Modifier::Plain)];
            if (sp.type != uf8::bindings::ActionType::Builtin) return false;
            return uf8::bindings::builtinStateOf(sp.action, sp.param);
        };
        sendUf8GlobalLed(uf8::Uf8GlobalLed::Nav,
            cellActive(uf8::bindings::ButtonId::Nav));
        sendUf8GlobalLed(uf8::Uf8GlobalLed::Nudge,
            cellActive(uf8::bindings::ButtonId::Nudge));
        sendUf8GlobalLed(uf8::Uf8GlobalLed::Focus,
            cellActive(uf8::bindings::ButtonId::EncFocus));
        g_lastEncoderMode = encMode;
    }

    g_globalLedsInit = true;
}

// Tick counter since extension load. Used to fire a "settle-time"
// re-refresh on the UC1 a moment after open, so the LED rings start
// in correct state even if the firmware's own init flood overwrote
// our first refresh. REAPER's onTimer fires at ~30 Hz, so 60 ticks
// ≈ 2 s.
int g_uc1RefireAtTick = 60;
int g_tickCounter = 0;

void onTimer()
{
    ++g_tickCounter;
    // Long-press SEL → folder-spill toggle BEFORE rebuilding the visible
    // list, so a just-fired spill flips the list this tick rather than
    // next.
    checkSelLongPressSpill();
    // Rebuild the filtered surface track list FIRST so drainInputQueue
    // and every render helper sees a consistent snapshot for this tick.
    // Without this, the input queue could resolve a strip's track via
    // an unfiltered REAPER lookup while the renderer expected a filtered
    // mapping (Bug 2 fix: Folder Collapse / Show Only Selected).
    rebuildVisibleTrackList();
    if (g_tickCounter == g_uc1RefireAtTick && g_uc1_surface) {
        // Force a full LED re-push once the device + REAPER project
        // are settled. Without this, the first refresh races with
        // UC1's init-flood and leaves rings stuck until the next
        // focus change.
        g_uc1_surface->invalidateCache();
        g_uc1_surface->refresh();
    }
    // UF8 init / project-load refire: when track count goes from 0
    // (no project, or project-load mid-flight) to >0 (project ready),
    // re-arm bank state and global-LED dedup. Without this:
    //  - the open-time g_bankDirty.store(true) gets consumed by the
    //    first onTimer tick before REAPER has finished loading, the
    //    loop pushes blank/off LEDs, and bankDirty clears leaving SEL
    //    LEDs in firmware-default white state.
    //  - on subsequent project loads, dedup keeps Pan/Shift/Nav LEDs
    //    in their pre-load committed state, but the firmware may have
    //    cleared them — invalidating g_globalLedsInit forces re-push.
    static int g_lastTrackCountForReinit = 0;
    const int currentTrackCount = CountTracks(nullptr);
    if (g_dev && g_dev->isOpen() &&
        currentTrackCount > 0 && g_lastTrackCountForReinit == 0) {
        g_bankDirty.store(true);
        g_globalLedsInit = false;
        // Re-apply persisted encoder mode after project load. Without
        // this, a phantom ChannelPush-on-init or stray binding
        // dispatch can clobber Instance/Nudge/Focus back to Nav,
        // forcing the user to manually re-toggle the mode after each
        // project open. Reading from ExtState is the source of truth.
        if (const char* m = GetExtState("ReaSixty", "encoderMode"); m && *m) {
            if      (std::strcmp(m, "Instance") == 0) g_encoderMode.store(EncoderMode::Instance);
            else if (std::strcmp(m, "Nudge")    == 0) g_encoderMode.store(EncoderMode::Nudge);
            else if (std::strcmp(m, "Focus")    == 0) g_encoderMode.store(EncoderMode::Focus);
            else                                      g_encoderMode.store(EncoderMode::Nav);
        }
    }
    g_lastTrackCountForReinit = currentTrackCount;
    chaseLastTouchedFx();
    chaseFocusedFxWindow();
    uf8::bindings::tickPending();
    drainInputQueue();
    commitDebouncedTouchReleases();
    if (g_sync) g_sync->refresh(reaperColorForVisibleSlot);
    pushZonesForVisibleSlots();
    pushUf8GlobalLeds();
    // pushSelColourBar() removed: it was a per-tick fallback that wrote
    // SEL LEDs in white-only mode (buildSelWhite). With track-colour SEL
    // now driven through sendLed() + the bank-shift refresh, this fallback
    // was overwriting the coloured frames with plain white on every tick.
    pushVuMeter();
    // UC1 stereo VU.
    //   Input  meter L/R: AudioAccessor (samples "immediately pre-FX"
    //                     per REAPER docs). Pre-CS, pre-everything.
    //   Output meter L/R: by default Track_GetPeakInfo (post-FX-chain,
    //                     == what REAPER's track meter shows). User
    //                     toggle via ExtState ("Rea-Sixty" /
    //                     "cs_output_source"): "track" (default) shows
    //                     post-FX-chain; "off" silences the strip.
    //                     Default-on because the post-FX level is what
    //                     the user hears, which is genuinely useful;
    //                     toggle exists for purist SSL UC1 fidelity.
    if (g_uc1_surface) {
        void* focus = g_uc1_surface->focusedTrack();
        static MediaTrack*    s_lastVuTrack = nullptr;
        static AudioAccessor* s_vuAccessor  = nullptr;
        auto teardownAccessor = [&] {
            if (s_vuAccessor) {
                DestroyAudioAccessor(s_vuAccessor);
                s_vuAccessor = nullptr;
            }
            s_lastVuTrack = nullptr;
        };
        if (!focus) {
            teardownAccessor();
        } else {
            MediaTrack* tr = static_cast<MediaTrack*>(focus);
            auto peakToDb = [](double p) -> float {
                if (p <= 0.0) return -120.f;
                return static_cast<float>(20.0 * std::log10(p));
            };

            // Output meter source — ExtState toggle, default = track meter.
            float dbOutL = -120.f;
            float dbOutR = -120.f;
            const char* outSrc = GetExtState("Rea-Sixty", "cs_output_source");
            if (!outSrc || !*outSrc || std::strcmp(outSrc, "track") == 0) {
                dbOutL = peakToDb(Track_GetPeakInfo(tr, 0));
                dbOutR = peakToDb(Track_GetPeakInfo(tr, 1));
            }
            // "off" or any unrecognised value → leave at -120 (silent).

            if (tr != s_lastVuTrack) {
                teardownAccessor();
                s_vuAccessor = CreateTrackAudioAccessor(tr);
                s_lastVuTrack = tr;
            }
            // Peak-hold with decay, applied to all four channels (in L/R
            // + out L/R). Without it a steady sine produces 1-2 dB peak
            // jitter per tick (block size doesn't align with the sine
            // cycle), which flickers the LED at the boundary between
            // two thresholds. Decay constant ~150 ms — fast enough that
            // the meter falls off audibly after a transient, slow
            // enough to absorb sample-block variability.
            static double s_holdInL  = -120.0, s_holdInR  = -120.0;
            static double s_holdOutL = -120.0, s_holdOutR = -120.0;
            // Linear-dB rate-limited fall. Original "0.85 of distance-from-
            // floor" formula (commit 2026-04-28) decayed exponentially in
            // linear-distance-from-floor space — h=-3 fell to h=-59 in 5
            // ticks, fighting REAPER's internal peak ballistics and producing
            // multi-LED flicker on the UC1 VU strip. Linear-dB at 1.5 dB/tick
            // ≈ 45 dB/sec gives full-scale fall in ~1.3 s — standard VU look.
            constexpr double kDbPerTick = 1.5;
            auto holdPeak = [&](double& hold, double raw) {
                if (raw > hold) hold = raw;
                else {
                    const double next = hold - kDbPerTick;
                    hold = (next > raw) ? next : raw;
                }
            };

            float dbInL = -120.f;
            float dbInR = -120.f;
            // Gate on transport playing/recording. AudioAccessor returns
            // valid samples at the project playhead even when stopped, so
            // without this gate the input meter freezes at the audio level
            // present at the stop position — instead of decaying silently.
            // GetPlayState bits: 1 = playing, 2 = paused, 4 = recording.
            const int playState = GetPlayState();
            const bool transportLive = (playState & 1) || (playState & 4);
            if (s_vuAccessor && transportLive) {
                AudioAccessorValidateState(s_vuAccessor);
                constexpr int kBlock  = 512;
                constexpr int kNchans = 2;
                constexpr int kSr     = 48000;
                static double buf[kBlock * kNchans];
                const double t = GetPlayPosition();
                const int rc = GetAudioAccessorSamples(
                    s_vuAccessor, kSr, kNchans, t, kBlock, buf);
                if (rc > 0) {
                    double pL = 0.0, pR = 0.0;
                    for (int i = 0; i < kBlock; ++i) {
                        const double sL = std::fabs(buf[i * 2]);
                        const double sR = std::fabs(buf[i * 2 + 1]);
                        if (sL > pL) pL = sL;
                        if (sR > pR) pR = sR;
                    }
                    // Pre-FX raw input level — fader / pan stay out. With
                    // no plug-in on the track, the input meter shows
                    // unprocessed level entering the FX chain; the
                    // output meter (Track_GetPeakInfo, post-FX-post-fader)
                    // shows what the user hears after fader. The two are
                    // expected to differ unless fader is at unity AND no
                    // FX is loaded — that's the SSL I/O-meter convention.
                    dbInL = peakToDb(pL);
                    dbInR = peakToDb(pR);
                }
            }
            holdPeak(s_holdInL,  dbInL);
            holdPeak(s_holdInR,  dbInR);
            holdPeak(s_holdOutL, dbOutL);
            holdPeak(s_holdOutR, dbOutR);
            g_uc1_surface->pushCsVu(static_cast<float>(s_holdInL),
                                    static_cast<float>(s_holdInR),
                                    static_cast<float>(s_holdOutL),
                                    static_cast<float>(s_holdOutR));
        }
    }
    // UF8 GR — driven from the focused track's CS plug-in via
    // TrackFX_GetNamedConfigParm("GainReduction_dB"). The frame
    // FF 66 09 15 <s1>..<s8> <chk> carries one GR byte per visible
    // strip — earlier mistake was treating bytes 5..11 as zero-padding,
    // so all GR rendered onto strip 1 only. Now: locate the focused
    // track inside the visible bank and stamp its strip's byte.
    //
    // Calibration: identical 30-sub-step ladder as UC1 (UC1Surface.cpp
    // pushGainReduction). One sub-step = 0.667 dB. SSL360's capture
    // (dual_35_cs_gr_ramp) showed UF8 byte 0x05 ↔ UC1 LED 0 full
    // (sub 6), 0x0A ↔ LED 1 full (sub 12), 0x18 ↔ LED 4 sub 3 (sub 27).
    // Map sub-step → byte: 0 → 0x00 off; 1..30 → round(0x02 + sub×22/30).
    // Both meters now light up in lock-step at every dB level.
    // Ballistics: up immediately, down 1 byte/tick — Plugin's
    // GainReduction_dB tracks the signal envelope tightly so the raw
    // byte swings several positions per audio beat without rate-limit.
    {
        std::array<uint8_t, 8> targetBytes{};  // all zero by default
        int focStrip = -1;
        if (g_uc1_surface) {
            if (auto* tr = static_cast<MediaTrack*>(g_uc1_surface->focusedTrack())) {
                if (!ValidatePtr2(nullptr, tr, "MediaTrack*")) tr = nullptr;
                const int trackCount = visibleTrackCount();
                const int bankOffset = g_bankOffset.load();
                int focIdx = -1;
                if (tr) for (int i = 0; i < trackCount; ++i) {
                    if (visibleTrackAt(i) == tr) { focIdx = i; break; }
                }
                if (focIdx >= bankOffset && focIdx < bankOffset + 8) {
                    focStrip = focIdx - bankOffset;
                    uc1::UC1Bindings b = uc1::lookupBindingsOnTrack(tr);
                    // GR readback mirrors UC1Surface::readGr: raw
                    // TrackFX_GetParam for user-picked GR params
                    // (most VST3 meter outputs are exposed there
                    // directly), GainReduction_dB named-config-parm
                    // as the built-in fallback. When the track has
                    // no SSL CS / user-mapped CS plug-in, walk the
                    // FX chain and use the first FX exposing the
                    // PreSonus GainReduction_dB convention (Frank
                    // 2026-05-06: ReaComp / FabFilter Pro-C2 etc.
                    // should still drive the UF8 strip).
                    bool gotIt = false;
                    double gr = 0.0;
                    double offsetDb = 0.0;
                    if (b.channelMap && b.channelFxIdx >= 0) {
                        offsetDb = b.channelGrOffsetDb;
                        if (b.channelGrParam >= 0) {
                            double mn = 0.0, mx = 0.0;
                            gr = TrackFX_GetParam(tr, b.channelFxIdx,
                                                  b.channelGrParam, &mn, &mx);
                            gotIt = true;
                        } else {
                            char buf[64] = {0};
                            if (TrackFX_GetNamedConfigParm(
                                    tr, b.channelFxIdx, "GainReduction_dB",
                                    buf, sizeof(buf))) {
                                gr = std::atof(buf);
                                gotIt = true;
                            }
                        }
                    }
                    if (!gotIt && g_grAnyFx.load()) {
                        const int fxCount = TrackFX_GetCount(tr);
                        for (int fx = 0; fx < fxCount; ++fx) {
                            char buf[64] = {0};
                            if (!TrackFX_GetNamedConfigParm(
                                    tr, fx, "GainReduction_dB",
                                    buf, sizeof(buf))) continue;
                            gr = std::atof(buf);
                            gotIt = true;
                            break;
                        }
                    }
                    if (gotIt) {
                        gr += offsetDb;
                        if (gr < 0) gr = -gr;
                        if (gr > 20.0) gr = 20.0;
                        // Piecewise dB → sub-step matching the SSL
                        // plug-in's GR meter (3/6/10/14/20 dB segments).
                        // Identical to UC1Surface::subStepFromDb so
                        // both meters render in lock-step.
                        double s;
                        if      (gr <=  3.0) s =        (gr       ) * (6.0 / 3.0);
                        else if (gr <=  6.0) s =  6.0 + (gr -  3.0) * (6.0 / 3.0);
                        else if (gr <= 10.0) s = 12.0 + (gr -  6.0) * (6.0 / 4.0);
                        else if (gr <= 14.0) s = 18.0 + (gr - 10.0) * (6.0 / 4.0);
                        else                  s = 24.0 + (gr - 14.0) * (6.0 / 6.0);
                        int sub = static_cast<int>(std::lround(s));
                        if (sub < 0)  sub = 0;
                        if (sub > 30) sub = 30;
                        const uint8_t newByte = (sub == 0)
                            ? uint8_t(0x00)
                            : static_cast<uint8_t>(
                                std::lround(0x02 + sub * (22.0 / 30.0)));
                        uint8_t& held = g_uf8GrBytes[focStrip];
                        if (newByte > held) held = newByte;
                        else if (held > newByte) --held;
                        targetBytes[focStrip] = held;
                    }
                }
            }
        }
        // Clear holders for non-focused strips so a previously-focused
        // strip doesn't keep displaying its last GR after focus moves.
        for (int s = 0; s < 8; ++s) {
            if (s != focStrip) g_uf8GrBytes[s] = 0;
        }
        if (g_dev && g_dev->isOpen()) {
            g_dev->setGrBytes(targetBytes);
        }
    }
    if (g_uc1_surface) g_uc1_surface->poll();

    // Once-per-second UC1 wire stats — disabled. Earlier dev diagnostic
    // that called ShowConsoleMsg from onTimer; a crash log captured a
    // PC=0 fault on this exact call site so we route any future stat
    // logging through the file path instead.
    static auto lastStat = std::chrono::steady_clock::now();
    static uint64_t prevOutFrames = 0, prevOutErrors = 0;
    if (g_uc1_dev) {
        auto now = std::chrono::steady_clock::now();
        if (now - lastStat >= std::chrono::seconds(1)) {
            const auto of = uc1::debugOutFrames();
            const auto oe = uc1::debugOutErrors();
            const auto dOf = of - prevOutFrames;
            const auto dOe = oe - prevOutErrors;
            if ((dOe > 0 || dOf < 20)) {
                if (FILE* f = std::fopen("/tmp/rea_sixty_uc1_stats.log", "a")) {
                    std::fprintf(f,
                        "UC1 WARN: OUT=%llu frames/s errs=%llu (expected ~50)\n",
                        (unsigned long long)dOf, (unsigned long long)dOe);
                    std::fclose(f);
                }
            }
            prevOutFrames = of; prevOutErrors = oe;
            lastStat = now;
        }
    }

    // Plugin Mixer toggle — drained on the main thread. Off-thread
    // button handlers set the flag; only this site is allowed to call
    // toggle() because ImGui_CreateContext is main-thread-only.
    if (g_mixerToggleRequest.exchange(false)) {
        g_mixerWindow.toggle();
    }

    // ssl_strip_mode_toggle_with_gui + instance-cycle GUI follow:
    // open / close the CS-domain plug-in at the active csInstanceIndex
    // on the focused track. Filter is uf8::Domain::ChannelStrip via
    // lookupPluginMapByName so BOTH built-in SSL variants (Channel Strip
    // 2 / 4K G / 4K E / 4K B / Link) AND user-mapped CS replacements
    // (e.g. Townhouse Channel Strip) qualify — Frank explicitly wants
    // his learned CS plug-ins to show too. BC-domain maps are skipped:
    // SSL Strip Mode is a CS-side mode, BC has its own toggle.
    //
    // Instance cycle integration: applyInstanceCycle_ raises this flag
    // whenever the cycle lands on a CS hit while g_pluginFaderMode is
    // on, so the open GUI follows the cycle. We close the previously-
    // shown floating window before opening the new one (tracked in
    // g_csGuiShownTr / g_csGuiShownFx) — without that, every cycle
    // detent stacks another floating GUI.
    // UF8 Plugin Mode entry snap — fires before the GUI sync drain so
    // the focused-track / Instance-index moves first; the GUI sync below
    // then opens the floating window for the just-snapped target.
    if (g_uf8PluginModeSnapRequest.exchange(false)) {
        snapUf8PluginModeToFocusedFx_();
    }

    if (g_pluginGuiSyncRequest.exchange(false)) {
        MediaTrack* tr = nullptr;
        if (g_uc1_surface) {
            tr = static_cast<MediaTrack*>(g_uc1_surface->focusedTrack());
        }
        if (!tr) tr = GetSelectedTrack(nullptr, 0);

        // Only open a CS GUI when SSL Strip Mode's with-GUI variant is
        // active. The plain variant raises the sync request too (so it
        // can close a GUI opened by a previous with-GUI session) but
        // must not pop a new one.
        const bool wantOpen = g_pluginFaderMode.load()
                           && g_pluginFaderModeWithGui.load();
        MediaTrack* targetTr = nullptr;
        int         targetFx = -1;
        if (tr && ValidatePtr2(nullptr, tr, "MediaTrack*")) {
            const int wantInst = uc1::csInstanceIndex(tr);
            const int n = TrackFX_GetCount(tr);
            char nameBuf[256];
            int seen = 0;
            int lastCsFx = -1;
            for (int fx = 0; fx < n; ++fx) {
                nameBuf[0] = 0;
                TrackFX_GetFXName(tr, fx, nameBuf, sizeof(nameBuf));
                if (!nameBuf[0]) continue;
                const auto* pm = uf8::lookupPluginMapByName(nameBuf);
                if (!pm) continue;
                if (pm->domain != uf8::Domain::ChannelStrip) continue;
                lastCsFx = fx;
                if (seen == wantInst) { targetFx = fx; break; }
                ++seen;
            }
            if (targetFx < 0) targetFx = lastCsFx;
            // No CS instance found — fall back to a UF8-only mapped
            // plug-in's GUI when the cycle landed on one. Same active-
            // instance plumbing, just keyed by uf8OnlyInstanceIndex.
            if (targetFx < 0) {
                const int wantUf8 = uc1::uf8OnlyInstanceIndex(tr);
                int seenU = 0, lastUf8Fx = -1;
                for (int fx = 0; fx < n; ++fx) {
                    nameBuf[0] = 0;
                    TrackFX_GetFXName(tr, fx, nameBuf, sizeof(nameBuf));
                    if (!nameBuf[0]) continue;
                    const auto* um = uf8::user_plugins::lookupOwnedByName(nameBuf);
                    if (!um || um->domain != uf8::Domain::None || !um->uf8Mode)
                        continue;
                    lastUf8Fx = fx;
                    if (seenU == wantUf8) { targetFx = fx; break; }
                    ++seenU;
                }
                if (targetFx < 0) targetFx = lastUf8Fx;
            }
            if (targetFx >= 0) targetTr = tr;
        }

        // Close the previously-shown CS GUI if it differs from the new
        // target (or if we're turning SSL Strip Mode off). Validate the
        // pointer first — REAPER reuses MediaTrack* across project
        // edits, and a deleted track returns nullptr-ish on
        // TrackFX_Show. ValidatePtr2 is the canonical check.
        if (g_csGuiShownTr
            && (g_csGuiShownTr != targetTr || g_csGuiShownFx != targetFx
                || !wantOpen))
        {
            if (ValidatePtr2(nullptr, g_csGuiShownTr, "MediaTrack*")) {
                TrackFX_Show(static_cast<MediaTrack*>(g_csGuiShownTr),
                             g_csGuiShownFx, /*hide floating*/ 2);
            }
            g_csGuiShownTr = nullptr;
            g_csGuiShownFx = -1;
        }

        if (wantOpen && targetTr && targetFx >= 0) {
            // showflag: 3 = floating GUI shown.
            TrackFX_Show(targetTr, targetFx, 3);
            g_csGuiShownTr = targetTr;
            g_csGuiShownFx = targetFx;
        }

        // ---- UF8 Plugin Mode (with GUI) -----------------------------
        // Open the plug-in the strips are currently driving — same
        // (tr, fxIdx) that `userStripCtxFocused_` resolved, which
        // already honours the snap-on-entry and the UC1 Instance index.
        // Previously this block opened the FIRST UF8-mapped plug-in on
        // the focused track regardless of Instance index, so opening
        // UF8 Plugin Mode while Trigger (instance 1) was open snapped
        // the strips to Trigger but then re-opened Traps (instance 0)
        // and chaseFocusedFxWindow re-followed → strips landed on
        // Traps instead of Trigger (Frank 2026-05-14). Separate (tr,
        // fx) tracking so SSL Strip Mode and UF8 Plugin Mode can each
        // own a window independently.
        {
            const bool uf8WantOpen =
                g_uf8PluginMode.load() && g_uf8PluginModeWithGui.load();
            MediaTrack* uf8TargetTr = nullptr;
            int         uf8TargetFx = -1;
            if (uf8WantOpen) {
                auto uctx = userStripCtxFocused_();
                if (uctx.map && uctx.tr && uctx.fxIdx >= 0
                    && ValidatePtr2(nullptr, uctx.tr, "MediaTrack*"))
                {
                    uf8TargetTr = uctx.tr;
                    uf8TargetFx = uctx.fxIdx;
                }
            }
            if (g_uf8GuiShownTr
                && (g_uf8GuiShownTr != uf8TargetTr
                 || g_uf8GuiShownFx != uf8TargetFx
                 || !uf8WantOpen))
            {
                if (ValidatePtr2(nullptr, g_uf8GuiShownTr, "MediaTrack*")) {
                    TrackFX_Show(static_cast<MediaTrack*>(g_uf8GuiShownTr),
                                 g_uf8GuiShownFx, 2);
                }
                g_uf8GuiShownTr = nullptr;
                g_uf8GuiShownFx = -1;
            }
            if (uf8WantOpen && uf8TargetTr && uf8TargetFx >= 0) {
                TrackFX_Show(uf8TargetTr, uf8TargetFx, 3);
                g_uf8GuiShownTr = uf8TargetTr;
                g_uf8GuiShownFx = uf8TargetFx;
            }
        }

        // ---- Selection-Mode Instance (push toggle, with-GUI follow) -
        // Owner-strip drives which strip's active FX is shown. -1 =
        // no GUI requested. Used in BOTH plain and with-GUI variants —
        // plain only toggles on push (rotation does nothing); with-GUI
        // also re-fires this drain on rotation so the window follows.
        {
            const int ownerStrip = g_instanceGuiOwnerStrip.load();
            const bool instWantOpen =
                g_selectionMode.load() == SelectionMode::Instance
                && ownerStrip >= 0 && ownerStrip < 8;
            MediaTrack* instTargetTr = nullptr;
            int         instTargetFx = -1;
            if (instWantOpen) {
                const int bankOffset   = g_bankOffset.load();
                const int surfaceCount = visibleTrackCount();
                const int slot = ownerStrip + bankOffset;
                if (slot >= 0 && slot < surfaceCount) {
                    MediaTrack* stripTr = visibleTrackAt(slot);
                    if (stripTr
                        && ValidatePtr2(nullptr, stripTr, "MediaTrack*"))
                    {
                        const int fxIdx = stripInstanceActiveFx_(stripTr);
                        if (fxIdx >= 0) {
                            instTargetTr = stripTr;
                            instTargetFx = fxIdx;
                        }
                    }
                }
            }
            if (g_instanceGuiShownTr
                && (g_instanceGuiShownTr != instTargetTr
                 || g_instanceGuiShownFx != instTargetFx
                 || !instWantOpen))
            {
                if (ValidatePtr2(nullptr, g_instanceGuiShownTr,
                                 "MediaTrack*"))
                {
                    TrackFX_Show(
                        static_cast<MediaTrack*>(g_instanceGuiShownTr),
                        g_instanceGuiShownFx, 2);
                }
                g_instanceGuiShownTr = nullptr;
                g_instanceGuiShownFx = -1;
            }
            if (instWantOpen && instTargetTr && instTargetFx >= 0) {
                TrackFX_Show(instTargetTr, instTargetFx, 3);
                g_instanceGuiShownTr = instTargetTr;
                g_instanceGuiShownFx = instTargetFx;
            }
        }
    }

    // REAPER Action picker poll — drives the Bindings editor's
    // "Browse Action..." flow. Cheap when no session is active.
    reasixty_actionPickerPoll();

    tickIdentify();

    // ImGui frame for the Plugin Mixer / Settings window. No-op while the
    // window is closed; when open, drives the entire ReaImGui paint cycle
    // for this tick. Kept last so any REAPER-API reads above (track
    // peaks, GR, focus state) are settled before the UI samples them.
    //
    // Edge-detect visibility around the paint so onMixerVisibilityChanged
    // fires once per open/close transition (Phase B: drives the Layer 2/3
    // auto-switch). The paint itself can flip visibility off if the user
    // hits the OS close button, which is why the read happens AFTER
    // onRunTick.
    static bool s_lastMixerVisible = false;
    g_mixerWindow.onRunTick();
    const bool nowVisible = g_mixerWindow.isOpen();
    if (nowVisible != s_lastMixerVisible) {
        uf8::bindings::onMixerVisibilityChanged(nowVisible);
        s_lastMixerVisible = nowVisible;
    }
}

// Brightness custom actions — registered at plugin entry point. REAPER
// dispatches via hookcommand. Unique-section-id 0 = main section.
custom_action_register_t g_actionBrightnessUp{
    0, "REASIXTY_BRIGHTNESS_UP", "Rea-Sixty: Brightness up", nullptr,
};
custom_action_register_t g_actionBrightnessDown{
    0, "REASIXTY_BRIGHTNESS_DOWN", "Rea-Sixty: Brightness down", nullptr,
};
int g_cmdBrightnessUp = 0;
int g_cmdBrightnessDown = 0;

// LED-cell discovery probe — for buttons whose LED cell hasn't been
// captured yet. Each call dims the previous candidate cell, lights
// the next bright-white, and logs the cell ID. User runs the action
// repeatedly while watching the hardware to discover the mapping.
//
// Round 4: 0x30..0x37 produced ZERO visible LEDs in round 3 even
// though cap35/36 originally placed Send/Plugin there — the original
// decode was wrong (same fate as Layer 1/2 / Page Left). Same outcome
// for 0x64..0x77. Send/Plugin + Page Right must live in cells we
// haven't touched yet, or use a different frame family. Sweep the
// remaining never-probed gaps first; if still nothing, the LEDs are
// likely on `FF 3B 03 <id>` (legacy mono path) instead of FF 38/39 04.
constexpr uint8_t kProbeCells[] = {
    // Tiny gap above the per-strip top-soft-keys
    0x20, 0x21,
    // Wide unexplored range above 0x77
    0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F,
    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
    0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
    0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
};
constexpr size_t kProbeCellCount = sizeof(kProbeCells) / sizeof(kProbeCells[0]);
int g_probeCellIndex = -1;  // -1 = nothing lit yet

uint8_t probeChecksum(std::span<const uint8_t> payload)
{
    unsigned sum = 0;
    for (auto b : payload) sum += b;
    return static_cast<uint8_t>(sum & 0xFF);
}

void sendProbeFrame(uint8_t cmd, uint8_t cell, uint8_t a, uint8_t b)
{
    std::vector<uint8_t> frame{0xFF, cmd, 0x04, cell, 0x00, a, b};
    std::span<const uint8_t> payload{frame.data() + 1, frame.size() - 1};
    frame.push_back(probeChecksum(payload));
    if (g_dev && g_dev->isOpen()) g_dev->send(frame);
}

void probeNextLedCell()
{
    if (!g_dev || !g_dev->isOpen()) {
        ShowConsoleMsg("Rea-Sixty probe: UF8 not open\n");
        return;
    }
    // Dim the previously-lit cell (if any) so only one lights at a time.
    if (g_probeCellIndex >= 0) {
        const uint8_t prev = kProbeCells[g_probeCellIndex];
        sendProbeFrame(0x38, prev, 0x11, 0xF1);
        sendProbeFrame(0x39, prev, 0x11, 0xF1);
    }
    g_probeCellIndex = (g_probeCellIndex + 1) % static_cast<int>(kProbeCellCount);
    const uint8_t cell = kProbeCells[g_probeCellIndex];
    // Bright white: FF 38 = FF FF, FF 39 = 00 F0 (matches standard LED on-state).
    sendProbeFrame(0x38, cell, 0xFF, 0xFF);
    sendProbeFrame(0x39, cell, 0x00, 0xF0);
    char line[80];
    std::snprintf(line, sizeof(line),
        "Rea-Sixty probe: cell 0x%02X bright (idx %d/%zu)\n",
        static_cast<unsigned>(cell), g_probeCellIndex + 1, kProbeCellCount);
    ShowConsoleMsg(line);
}

custom_action_register_t g_actionProbeLed{
    0, "REASIXTY_PROBE_LED", "Rea-Sixty: Probe next global LED cell",
    nullptr,
};
int g_cmdProbeLed = 0;

// Legacy mono-LED probe — same idea as the colour-pair probe above
// but uses the `FF 3B 03 <id> 00 <state>` frame family that Plugin
// (0x2F) is documented as a "2-state outlier" for. Send/Plugin row +
// Page Right LEDs aren't reachable via FF 38/39 04 (verified via the
// 2026-04-30 sweep of 0x18..0xAF), so this is the next family to try.
constexpr uint8_t kLegacyProbeCells[] = {
    // Suspects from cap35/36 Send/Plugin decode — cells already cleared
    // empty under FF 38/39 04, may respond here.
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    // 0x2C — symmetric to PageLeft 0x2D, top suspect for PageRight.
    0x2C,
    // Other cells that came up empty under the colour-pair probe.
    0x28, 0x29, 0x2A, 0x38,
    0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x50, 0x51,
};
constexpr size_t kLegacyProbeCellCount =
    sizeof(kLegacyProbeCells) / sizeof(kLegacyProbeCells[0]);
int g_legacyProbeCellIndex = -1;

void probeNextLegacyLedCell()
{
    if (!g_dev || !g_dev->isOpen()) {
        ShowConsoleMsg("Rea-Sixty legacy probe: UF8 not open\n");
        return;
    }
    // Turn off the previously-lit cell (state 0x00).
    if (g_legacyProbeCellIndex >= 0) {
        g_dev->send(uf8::buildLedCommand(
            kLegacyProbeCells[g_legacyProbeCellIndex], false));
    }
    g_legacyProbeCellIndex =
        (g_legacyProbeCellIndex + 1) % static_cast<int>(kLegacyProbeCellCount);
    const uint8_t cell = kLegacyProbeCells[g_legacyProbeCellIndex];
    g_dev->send(uf8::buildLedCommand(cell, true));
    char line[80];
    std::snprintf(line, sizeof(line),
        "Rea-Sixty legacy probe: id 0x%02X on (idx %d/%zu)\n",
        static_cast<unsigned>(cell), g_legacyProbeCellIndex + 1,
        kLegacyProbeCellCount);
    ShowConsoleMsg(line);
}

custom_action_register_t g_actionProbeLegacyLed{
    0, "REASIXTY_PROBE_LEGACY_LED",
    "Rea-Sixty: Probe next legacy mono-LED id", nullptr,
};
int g_cmdProbeLegacyLed = 0;

// Diagnostic — toggle a per-frame USB trace into /tmp/reaper_uf8_frames.log.
// Used to compare Rea-Sixty's frame stream against an SSL360 baseline when
// the motor-lock symptom recurs. Off by default (zero overhead).
void toggleFrameTrace()
{
    if (!g_dev) {
        ShowConsoleMsg("Rea-Sixty trace: UF8 not open\n");
        return;
    }
    const bool on = !g_dev->frameTrace();
    g_dev->setFrameTrace(on);
    if (on) {
        // Drop any prior log so the trace starts clean.
        std::remove("/tmp/reaper_uf8_frames.log");
        ShowConsoleMsg(
            "Rea-Sixty trace ON — logging to /tmp/reaper_uf8_frames.log\n");
    } else {
        ShowConsoleMsg("Rea-Sixty trace OFF\n");
    }
}

custom_action_register_t g_actionFrameTrace{
    0, "REASIXTY_FRAME_TRACE",
    "Rea-Sixty: Toggle USB frame trace", nullptr,
};
int g_cmdFrameTrace = 0;

void toggleFaderCal()
{
    const bool now = !g_faderCalEnabled.load();
    g_faderCalEnabled.store(now);
    ShowConsoleMsg(now
        ? "Rea-Sixty: fader calibration ON (PCHIP correction active)\n"
        : "Rea-Sixty: fader calibration OFF (raw slider-law passthrough)\n");
}

custom_action_register_t g_actionToggleFaderCal{
    0, "REASIXTY_TOGGLE_FADER_CAL",
    "Rea-Sixty: Toggle fader calibration (diagnostic)", nullptr,
};
int g_cmdToggleFaderCal = 0;

// Diagnostic — log every inbound FF 21 03 fader-position frame plus
// the deadband filter's accept/reject decision into a tab-separated
// file so we can see exactly what the hardware emits vs what REAPER
// receives. Off by default. Toggle via REASIXTY_TOGGLE_FADER_INPUT_LOG.
//
// Format: t_ms<TAB>kind<TAB>strip<TAB>pb14<TAB>prevPb<TAB>delta<TAB>decision
//   kind = "POS" (position frame) | "TOUCH" (touch on/off)
//   delta = signed pb14 - prevPb (positive = upward, negative = downward)
//   decision = "ACC" (accepted, queued to REAPER) | "REJ" (filtered out)
std::atomic<bool>             g_faderInputLog{false};
std::mutex                    g_faderInputLogMutex;
std::chrono::steady_clock::time_point g_faderInputLogStart{};

void faderInputLog_(const char* kind, int strip, int pb14, int prevPb,
                    int delta, const char* decision)
{
    if (!g_faderInputLog.load()) return;
    std::lock_guard<std::mutex> lk(g_faderInputLogMutex);
    static FILE* f = nullptr;
    if (!f) {
        f = std::fopen("/tmp/uf8_fader_input.log", "w");
        if (!f) return;
        std::fprintf(f, "t_ms\tkind\tstrip\tpb14\tprevPb\tdelta\tdecision\n");
    }
    const auto now = std::chrono::steady_clock::now();
    const auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - g_faderInputLogStart).count();
    std::fprintf(f, "%lld\t%s\t%d\t%d\t%d\t%+d\t%s\n",
                 (long long)ms, kind, strip, pb14, prevPb, delta, decision);
    std::fflush(f);
}

void toggleFaderInputLog()
{
    const bool now = !g_faderInputLog.load();
    if (now) {
        g_faderInputLogStart = std::chrono::steady_clock::now();
        std::remove("/tmp/uf8_fader_input.log");
        g_faderInputLog.store(true);
        ShowConsoleMsg(
            "Rea-Sixty: fader input log ON -> /tmp/uf8_fader_input.log\n"
            "Pull a fader slowly once, then toggle OFF and send the file.\n");
    } else {
        g_faderInputLog.store(false);
        ShowConsoleMsg(
            "Rea-Sixty: fader input log OFF\n");
    }
}

custom_action_register_t g_actionToggleFaderInputLog{
    0, "REASIXTY_TOGGLE_FADER_INPUT_LOG",
    "Rea-Sixty: Toggle fader input log (diagnostic)", nullptr,
};
int g_cmdToggleFaderInputLog = 0;

// Diagnostic — replay the EXACT byte sequence captured from SSL360
// at uc1_40 t=8.700 (first indicator frame in the sweep). Bypasses
// our build* functions entirely. If this still doesn't paint the
// yellow indicator, we're missing a precursor setup frame SSL360
// sent earlier; if it does paint, our slot-lookup / encoding has
// a bug.
void uc1FireTestIndicator()
{
    if (!g_uc1_dev || !g_uc1_dev->isOpen()) {
        ShowConsoleMsg("Rea-Sixty UC1 test: device not open\n");
        return;
    }
    // Verbatim from uc1_40 capture: banner(06) + header "Mix" +
    // triple "PLUG-IN/COMP MIX/PAN" + value "99.3" + unit "%" +
    // indicator FF FF FF (n=23 = max).
    const std::vector<std::vector<uint8_t>> burst = {
        {0xff,0x66,0x03,0x00,0x06,0x00,0x6f},                                                                   // banner
        {0xff,0x66,0x04,0x01,0x4d,0x69,0x78,0x99},                                                              // header "Mix"
        {0xff,0x66,0x2b,0x04,                                                                                    // triple — large 3-slot
         0x50,0x4c,0x55,0x47,0x2d,0x49,0x4e, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,                                // "PLUG-IN" + pad
         0x43,0x4f,0x4d,0x50,0x20,0x4d,0x49,0x58, 0x00,0x00,0x00,0x00,0x00,0x00,                                // "COMP MIX" + pad
         0x50,0x41,0x4e, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,                                // "PAN" + pad
         0xad},
        {0xff,0x66,0x05,0x0e,0x39,0x39,0x2e,0x33,0x4c},                                                          // value "99.3"
        {0xff,0x66,0x02,0x0f,0x25,0x9c},                                                                         // unit "%"
        {0xff,0x66,0x04,0x0d,0xff,0xff,0xff,0x74},                                                               // indicator FF FF FF
    };
    for (const auto& f : burst) g_uc1_dev->send(f);
    ShowConsoleMsg("Rea-Sixty UC1 test: replayed uc1_40 t=8.700 verbatim\n");
}

custom_action_register_t g_actionUc1TestIndicator{
    0, "REASIXTY_UC1_TEST_INDICATOR",
    "Rea-Sixty: UC1 test EXT_FUNCS indicator (debug)", nullptr,
};
int g_cmdUc1TestIndicator = 0;

// Diagnostic: poll a battery of TrackFX_GetNamedConfigParm candidate
// names against the focused track's CS plug-in and dump the returned
// values to the REAPER console. Used to find the data source for Gate
// GR (currently no known parmname; SSL CS2 only documents combined
// "GainReduction_dB"). User runs the action while moving the Gate
// Range knob — whichever name returns a non-zero value that varies
// with Range is our Gate GR API.
void probeGateGrSources()
{
    MediaTrack* tr = GetSelectedTrack(nullptr, 0);
    if (!tr) {
        ShowConsoleMsg("Gate-GR probe: no selected track\n");
        return;
    }
    auto match = uf8::lookupPluginOnTrack(tr, uf8::Domain::ChannelStrip);
    if (!match.map) {
        ShowConsoleMsg("Gate-GR probe: focused track has no CS plug-in\n");
        return;
    }
    // Candidate parmnames — PreSonus VST3 conventions + likely SSL
    // extensions. Anything that returns a non-empty buffer is logged.
    static constexpr const char* kCandidates[] = {
        "GainReduction_dB",
        "GateReduction_dB",
        "GateGainReduction_dB",
        "GateGR_dB",
        "GR_dB",
        "GR_Gate_dB",
        "GainReductionGate_dB",
        "Gate_dB",
        "ExpReduction_dB",
        "ExpansionReduction_dB",
        "GateAttenuation_dB",
        "InputGainReduction_dB",
        "OutputGainReduction_dB",
        "ParamValue_GateRange",
        "GateRangeApplied_dB",
    };
    char header[128];
    std::snprintf(header, sizeof(header),
        "Gate-GR probe — track=%s fx=%d\n",
        match.map->displayShort, match.fxIndex);
    ShowConsoleMsg(header);
    char buf[128];
    for (auto* name : kCandidates) {
        buf[0] = '\0';
        const bool ok = TrackFX_GetNamedConfigParm(
            tr, match.fxIndex, name, buf, sizeof(buf));
        char line[256];
        if (ok) {
            std::snprintf(line, sizeof(line),
                "  [%s] = %s\n", name, buf);
        } else {
            std::snprintf(line, sizeof(line),
                "  [%s] not exposed\n", name);
        }
        ShowConsoleMsg(line);
    }
}

custom_action_register_t g_actionProbeGateGr{
    0, "REASIXTY_PROBE_GATE_GR",
    "Rea-Sixty: Probe Gate GR sources on focused CS plug-in", nullptr,
};
int g_cmdProbeGateGr = 0;

// Diagnostic: dump every VST3 param's name + ident + current normalised
// value for the focused CS plug-in. Use to find candidate Gate-GR
// parameters when the named-config-parm probe comes up empty (the SSL
// CS plug-in doesn't expose the PreSonus VST3 GR readback for the Gate
// section — verified 2026-05-01). Run twice at different Range knob
// positions and diff to find params whose value tracks Range.
void dumpCsPluginParams()
{
    MediaTrack* tr = GetSelectedTrack(nullptr, 0);
    if (!tr) {
        ShowConsoleMsg("Param-dump: no selected track\n");
        return;
    }
    auto match = uf8::lookupPluginOnTrack(tr, uf8::Domain::ChannelStrip);
    if (!match.map) {
        ShowConsoleMsg("Param-dump: focused track has no CS plug-in\n");
        return;
    }
    const int n = TrackFX_GetNumParams(tr, match.fxIndex);
    char header[128];
    std::snprintf(header, sizeof(header),
        "Param-dump %s — %d params\n", match.map->displayShort, n);
    ShowConsoleMsg(header);
    for (int p = 0; p < n; ++p) {
        char name[128] = {};
        char ident[128] = {};
        char fmt[64]   = {};
        TrackFX_GetParamName(tr, match.fxIndex, p, name, sizeof(name));
        TrackFX_GetParamIdent(tr, match.fxIndex, p, ident, sizeof(ident));
        const double v = TrackFX_GetParamNormalized(tr, match.fxIndex, p);
        TrackFX_FormatParamValueNormalized(tr, match.fxIndex, p, v,
                                           fmt, sizeof(fmt));
        char line[512];
        std::snprintf(line, sizeof(line),
            "  [%3d] %-32s ident=%-40s norm=%.4f val=%s\n",
            p, name, ident, v, fmt);
        ShowConsoleMsg(line);
    }
}

custom_action_register_t g_actionDumpParams{
    0, "REASIXTY_DUMP_CS_PARAMS",
    "Rea-Sixty: Dump all CS plug-in params (find Gate GR)", nullptr,
};
int g_cmdDumpParams = 0;

// Diagnostic: dump the focused CS plug-in's full track-state chunk to
// /tmp/reasixty_cs_chunk.txt. Use to find the XML attribute name SSL
// uses for the processing-order routing setting (10 orders + 'b'
// variants per UC1 manual p.20). Run twice at two different routing
// orders, diff, and the differing attribute is the patch target.
void dumpCsChunk()
{
    MediaTrack* tr = GetSelectedTrack(nullptr, 0);
    if (!tr) {
        ShowConsoleMsg("Chunk-dump: no selected track\n");
        return;
    }
    int chunkSize = 0;
    char* chunk = GetSetObjectState(tr, "");
    if (!chunk) {
        ShowConsoleMsg("Chunk-dump: GetSetObjectState returned null\n");
        return;
    }
    const std::string body{chunk};
    FreeHeapPtr(chunk);
    const char* path = "/tmp/reasixty_cs_chunk.txt";
    FILE* f = std::fopen(path, "w");
    if (!f) {
        ShowConsoleMsg("Chunk-dump: cannot open /tmp/reasixty_cs_chunk.txt\n");
        return;
    }
    std::fwrite(body.data(), 1, body.size(), f);
    std::fclose(f);
    char line[256];
    std::snprintf(line, sizeof(line),
        "Chunk-dump: %zu bytes → %s\n", body.size(), path);
    ShowConsoleMsg(line);
    chunkSize = static_cast<int>(body.size());
    (void)chunkSize;
}

custom_action_register_t g_actionDumpChunk{
    0, "REASIXTY_DUMP_CS_CHUNK",
    "Rea-Sixty: Dump focused track's chunk (find SSL routing attr)",
    nullptr,
};
int g_cmdDumpChunk = 0;

// Diagnostic: dump just the 5 SSL routing flags + 3 section In/Out
// flags from the focused CS plug-in. Compact one-line output so the
// user can run it at each of the 10 SSL360 routing orders and we
// build the preset table from the results.
void dumpRoutingFlags()
{
    MediaTrack* tr = GetSelectedTrack(nullptr, 0);
    if (!tr) {
        ShowConsoleMsg("Routing-flags: no selected track\n");
        return;
    }
    auto match = uf8::lookupPluginOnTrack(tr, uf8::Domain::ChannelStrip);
    if (!match.map) {
        ShowConsoleMsg("Routing-flags: focused track has no CS plug-in\n");
        return;
    }
    // Slot ids → vst3 param indices, resolved via PluginMap so this
    // works across all four CS variants (CS 2 / 4K E / 4K G / 4K B).
    auto val = [&](const char* slotId) -> int {
        for (const auto& s : match.map->slots) {
            if (s.id && std::strcmp(s.id, slotId) == 0 && s.vst3Param >= 0) {
                const double v = TrackFX_GetParamNormalized(
                    tr, match.fxIndex, s.vst3Param);
                return v >= 0.5 ? 1 : 0;
            }
        }
        return -1;  // not in this plug-in's map
    };
    char line[256];
    std::snprintf(line, sizeof(line),
        "Routing %s: ExtSC=%d FiltIn=%d FiltSC=%d EqSC=%d DynPreEq=%d  |  Filt=%d Eq=%d Dyn=%d\n",
        match.map->displayShort,
        val("ExternalSC"),
        val("FiltersToInput"),
        val("FiltersToSC"),
        val("EqToSC"),
        val("DynamicsPreEq"),
        val("FiltersIn"),
        val("EqIn"),
        val("DynamicsIn"));
    ShowConsoleMsg(line);
}

custom_action_register_t g_actionDumpRouting{
    0, "REASIXTY_DUMP_ROUTING_FLAGS",
    "Rea-Sixty: Dump SSL routing flags (run at each of 10 orders)",
    nullptr,
};
int g_cmdDumpRouting = 0;

// Custom action descriptor for the mixer-toggle. The instance itself is
// declared earlier in this TU so onTimer() can call it; this block keeps
// the action wiring next to its brightness siblings for readability.
custom_action_register_t g_actionToggleMixer{
    0, "REASIXTY_TOGGLE_SETTINGS", "Rea-Sixty: Open / Close Rea-Sixty Settings", nullptr,
};
int g_cmdToggleMixer = 0;

// hookcommand2 is the correct hook for custom_action dispatch per SDK
// note at reaper_plugin.h:1086. hookcommand (v1) only catches actions
// triggered via menu/keyboard, not custom_action registered entries.
bool hookCommand2(KbdSectionInfo* /*sec*/, int command,
                  int /*val*/, int /*val2*/, int /*relmode*/,
                  HWND /*hwnd*/)
{
    if (command == 0) return false;
    if (command == g_cmdBrightnessUp)   { brightnessUp();   return true; }
    if (command == g_cmdBrightnessDown) { brightnessDown(); return true; }
    if (command == g_cmdProbeLed)       { probeNextLedCell();       return true; }
    if (command == g_cmdProbeLegacyLed) { probeNextLegacyLedCell(); return true; }
    if (command == g_cmdFrameTrace)     { toggleFrameTrace();       return true; }
    if (command == g_cmdToggleFaderCal) { toggleFaderCal();         return true; }
    if (command == g_cmdToggleFaderInputLog) { toggleFaderInputLog(); return true; }
    if (command == g_cmdUc1TestIndicator) { uc1FireTestIndicator();  return true; }
    if (command == g_cmdProbeGateGr)    { probeGateGrSources();     return true; }
    if (command == g_cmdDumpParams)     { dumpCsPluginParams();     return true; }
    if (command == g_cmdDumpChunk)      { dumpCsChunk();            return true; }
    if (command == g_cmdDumpRouting)    { dumpRoutingFlags();       return true; }
    if (command == g_cmdToggleMixer)    { g_mixerToggleRequest.store(true); return true; }
    return false;
}

} // anonymous

// External hook so UC1Surface (different TU) can trigger the same
// MCP-scroll + UF8-rebank behaviour that the UF8 select/encoder paths
// use. Anonymous-namespace internals (g_bankOffset, g_followMode…) stay
// private; this wrapper is the only symbol exposed. The name differs
// from the internal helper because giving the anonymous-namespace
// version external linkage would conflict with this definition.
void reasixty_followSelectedInMixer(MediaTrack* tr)
{
    followSelectedInMixer(tr);
}

// External hook for UC1Surface (different TU): request a Plugin Mixer
// toggle. Same threading rule as the UF8 path — UC1 button events run
// on the USB worker thread, so we must NOT touch ImGui from here. The
// onTimer() drain on the main thread does the actual toggle.
void reasixty_toggleMixerWindow()
{
    g_mixerToggleRequest.store(true);
}

// Settings-screen accessors. SettingsScreen.cpp is a separate TU and
// must not reach into the anonymous-namespace globals directly; these
// wrappers are the only legal hook into runtime state from the UI side.
// All called from the main thread (onTimer → ImGui → these), so plain
// non-atomic loads are fine where the underlying state is atomic anyway.

bool reasixty_uf8Connected()
{
    return g_dev && g_dev->isOpen();
}

bool reasixty_uc1Connected()
{
    return g_uc1_dev && g_uc1_dev->isOpen();
}

// SSL plug-in soft-key bank labels for the Settings → Soft-Key Banks
// editor's read-only "stock" tabs. domain: 0 = ChannelStrip,
// 1 = BusComp. Returns the 8-element label array for the bank, or
// nullptr on out-of-range. Caller treats empty strings as "no slot".
const char* const* reasixty_softkeyStockLabels(int domain, int bank)
{
    if (bank < 0 || bank > 5) return nullptr;
    if (domain == 1) return softkey::kBcLabels[bank];
    return softkey::kCsLabels[bank];
}
int reasixty_softkeyStockBankCount() { return 6; }

// Live focused-param domain — 0 = ChannelStrip (Q1 default),
// 1 = BusComp (Q2 default). Read by the Settings hardware schematic
// so the top-soft-key label row reflects the user's current Q1/Q2
// selection AND any binding that calls domain_cs / domain_bc /
// uf8::setFocus directly. Defaults to ChannelStrip when no domain is
// set yet (matches the device-side rendering convention).
int reasixty_focusedDomain()
{
    const auto fp = uf8::getFocusedParam();
    return (fp.domain == uf8::Domain::BusComp) ? 1 : 0;
}

// -1 when no Quick is engaged on the active layer; otherwise the active
// Quick index 0..2. Settings preview overlays the slot labels for the
// current Quick + sub-bank when this is >= 0.
int reasixty_activeUserBank()
{
    const int layer = uf8::bindings::getActiveLayer();
    if (layer < 0 || layer > 2) return -1;
    return g_activeQuick[layer].load();
}

// Per-layer accessors so the Bindings editor can show a live-state
// badge for whichever layer the user is editing (not just the
// currently-active one). Out-of-range returns -1 / 0 defaults.
int reasixty_activeQuickFor(int layer)
{
    if (layer < 0 || layer > 2) return -1;
    return g_activeQuick[layer].load();
}
int reasixty_activeSubBankFor(int layer)
{
    if (layer < 0 || layer > 2) return 0;
    return g_activeSubBank[layer].load();
}

// "Which Quick button looks engaged from the user's perspective" —
// handles Layer 1's special case where Q1 = SSL CS focus and Q2 = SSL
// BC focus instead of going through g_activeQuick. Priority order:
// (1) g_activeQuick wins (user-Quick toggle is the explicit choice),
// (2) Layer 1 focus.domain falls back to Q1/Q2, (3) else -1.
int reasixty_engagedQuickFor(int layer)
{
    if (layer < 0 || layer > 2) return -1;
    const int gq = g_activeQuick[layer].load();
    if (gq >= 0) return gq;
    if (layer == 0) {
        const auto dom = uf8::getFocusedParam().domain;
        if (dom == uf8::Domain::ChannelStrip) return 0;   // Q1 = CS
        if (dom == uf8::Domain::BusComp)      return 1;   // Q2 = BC
    }
    return -1;
}

// User-Quick slot label for the Settings preview. `bank` is interpreted
// as the active Quick index (0..2) on the currently-active layer; the
// active sub-bank decides which of the 6 slot tables the label comes
// from. Returns nullptr on out-of-range / empty slots so the preview
// falls back to the plug-in label. The string lives in caller-owned
// thread-local storage and is stable until the next call.
const char* reasixty_userBankSlotLabel(int bank, int slot)
{
    if (bank < 0 || bank >= uf8::bindings::kQuicksPerLayer) return nullptr;
    if (slot < 0 || slot >= uf8::bindings::kSlotsPerSubBank) return nullptr;
    const int layer = uf8::bindings::getActiveLayer();
    if (layer < 0 || layer > 2) return nullptr;
    const int sub = g_activeSubBank[layer].load();
    static thread_local std::string s_buf;
    const auto userSlot = uf8::bindings::getUserQuickSlot(layer, bank, sub, slot);
    s_buf = userSlot.label;
    if (s_buf.empty()) {
        // Synthesise from the action name so the preview shows
        // SOMETHING rather than going blank — mirrors the device-side
        // userLabel-fallback path in pushZonesForVisibleSlots.
        const auto& sp = userSlot.shortPress[
            static_cast<int>(uf8::bindings::Modifier::Plain)];
        const bool slotPresent =
            sp.type != uf8::bindings::ActionType::Noop
            || !sp.action.empty();
        if (slotPresent) {
            s_buf = sp.action;
            if (s_buf.size() > 8) s_buf.resize(8);
        }
    }
    return s_buf.empty() ? nullptr : s_buf.c_str();
}

// Current SSL soft-key PAGE bank (0 = V-POT, 1..5 = Bank N) — read by
// the Settings editor so the per-soft-key edit header shows the live
// bank context ("Soft-Key N - Bank M (Layer L)") and the schematic
// can highlight the active V-POT/Bank tile.
// FX-Learn editor's direct g_softKeyBank accessor — bypasses
// reasixty_softkeyCurrentBank's user-Quick branch so the editor mockup
// always shows the live hardware FX-Learn bank (0..7) regardless of
// what's happening with user-Quick sub-banks. Independent of the
// Plugin Mode gate.
int reasixty_softKeyBankRaw()
{
    return std::clamp(g_softKeyBank.load(), 0, uf8::kUserUf8BankCount - 1);
}

// FX-Learn editor's mockup TopSoftKey click → bank switch (Frank
// 2026-05-13: "klick auf UF8 mockup soft-key" should drive hardware).
// Same path as the hardware TopSoftKey press handler — exchanges
// g_softKeyBank, dirties the bank/softkey state machines, persists the
// new bank to ExtState. Layer/Quick state untouched.
void reasixty_setSoftKeyBank(int b)
{
    if (b < 0) b = 0;
    if (b >= uf8::kUserUf8BankCount) b = uf8::kUserUf8BankCount - 1;
    if (g_softKeyBank.exchange(b) != b) {
        g_softKeyDirty.store(true);
        g_bankDirty.store(true);
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%d", b);
        SetExtState("ReaSixty", "softKeyBank", buf, true);
    }
}

int reasixty_softkeyCurrentBank()
{
    // When a Quick is engaged on the active layer, the sub-bank
    // selector follows g_activeSubBank[layer] (V-POT / Soft 1-5
    // within that user-Quick). Otherwise the SSL plug-in PAGE bank
    // in g_softKeyBank wins. The schematic ring and the per-binding
    // header derive their "current bank" from this — without the
    // user-Quick branch the ring would stick to g_softKeyBank even
    // after the user navigated sub-banks in a Quick (Frank 2026-05-13
    // "UF8 → Bindings doesn't reflect sub-bank press").
    const int layer = uf8::bindings::getActiveLayer();
    if (layer >= 0 && layer <= 2) {
        const int q = g_activeQuick[layer].load();
        if (q >= 0) {
            return std::clamp(g_activeSubBank[layer].load(), 0, 5);
        }
    }
    return std::clamp(g_softKeyBank.load(), 0, uf8::kUserUf8BankCount - 1);
}
const char* reasixty_softkeyCurrentBankName()
{
    static const char* kNames[6] = {
        "V-POT", "Bank 1", "Bank 2", "Bank 3", "Bank 4", "Bank 5",
    };
    const int b = reasixty_softkeyCurrentBank();
    return kNames[b];
}

const char* reasixty_uf8Serial()
{
    if (!g_dev) return "";
    return g_dev->serial().c_str();
}

const char* reasixty_uc1Serial()
{
    if (!g_uc1_dev) return "";
    return g_uc1_dev->serial().c_str();
}

int reasixty_brightnessLevel()
{
    return g_brightness.load();
}

int reasixty_scribbleBrightnessLevel()
{
    return g_scribbleBrightness.load();
}

void reasixty_setBrightnessLevel(int level)
{
    g_brightness.store(clampLevel_(level));
    applyBrightness();
}

void reasixty_setScribbleBrightnessLevel(int level)
{
    g_scribbleBrightness.store(clampLevel_(level));
    applyBrightness();
}

void reasixty_identifyUf8()
{
    g_identifyUf8UntilMs.store(nowMs_() + kIdentifyDurationMs);
}

void reasixty_identifyUc1()
{
    g_identifyUc1UntilMs.store(nowMs_() + kIdentifyDurationMs);
}

bool reasixty_selFollowsColor()
{
    return g_selFollowsColor.load();
}

void reasixty_setSelFollowsColor(bool follow)
{
    g_selFollowsColor.store(follow);
    SetExtState("rea_sixty", "sel_follows_color", follow ? "1" : "0", true);
    // Force a per-strip SEL re-push so the new colour mode lands without
    // requiring the user to bank-shift or click around.
    g_bankDirty.store(true);
}

bool reasixty_grAnyFx()
{
    return g_grAnyFx.load();
}

void reasixty_setGrAnyFx(bool enabled)
{
    g_grAnyFx.store(enabled);
    SetExtState("rea_sixty", "gr_any_fx", enabled ? "1" : "0", true);
}

bool reasixty_trackSelFollowsParam()
{
    return g_trackSelFollowsParam.load();
}

void reasixty_setTrackSelFollowsParam(bool follow)
{
    g_trackSelFollowsParam.store(follow);
    SetExtState("rea_sixty", "track_sel_follows_param", follow ? "1" : "0", true);
}

bool reasixty_stripFollowsFocusedFx()
{
    return g_stripFollowsFocusedFx.load();
}

void reasixty_setStripFollowsFocusedFx(bool follow)
{
    g_stripFollowsFocusedFx.store(follow);
    SetExtState("rea_sixty", "strip_follows_focused_fx", follow ? "1" : "0", true);
}

// Surface filters. Backing atomics are also driven by the `folder_mode`
// and `show_only_selected` builtins (so a hardware-bound button stays in
// sync with the Settings UI). ExtState key matches the builtin handler.
bool reasixty_folderMode()
{
    return g_folderMode.load();
}

void reasixty_setFolderMode(bool on)
{
    g_folderMode.store(on);
    if (!on) g_spilledParent.store(nullptr);
    g_pageDirty.store(true);
    g_bankDirty.store(true);
    SetExtState("ReaSixty", "folderMode", on ? "1" : "0", true);
}

bool reasixty_showOnlySelected()
{
    return g_showOnlySelected.load();
}

void reasixty_setShowOnlySelected(bool on)
{
    g_showOnlySelected.store(on);
    g_pageDirty.store(true);
    g_bankDirty.store(true);
    SetExtState("ReaSixty", "showOnlySelected", on ? "1" : "0", true);
}

// Build a diagnostic .zip on the user's Desktop with extension state +
// any of our existing trace logs (frame trace, ColorSync log) so a user
// can email us a single archive when something misbehaves. Cheap; runs
// synchronously on the click. macOS-only path for now (Desktop convention
// + system zip CLI). Cross-platform variant when we tackle Win/Linux.
const char* reasixty_reaperVersion()
{
    const char* v = GetAppVersion();
    return v ? v : "";
}

// ---- REAPER Action picker (Settings → Bindings) -------------------------
// Wraps REAPER's PromptForAction so the Bindings editor can let users pick
// an action from REAPER's Action List or load a ReaScript file. State lives
// here (main.cpp owns the onTimer) so the poll keeps running even if the
// user navigates away from the editor while the picker dialog is open.
// One picker session at a time; starting a new one cancels the previous.
//
// Two destination modes: Layer mode writes back to layers[L].bindings[id]
// (the original use); UserQuick mode writes back to userQuicks[L].quicks[Q]
// .subBanks[SB].slots[s]. Picker editor calls Start for the matching mode;
// the poll branches accordingly.
namespace {
    enum class PickerMode { Layer, UserQuick };
    bool                         g_pickerActive    = false;
    PickerMode                   g_pickerMode      = PickerMode::Layer;
    int                          g_pickerLayer     = 0;
    uf8::bindings::ButtonId      g_pickerId        = uf8::bindings::ButtonId::None;
    bool                         g_pickerLongPress = false;
    // Modifier slot (Plain/Shift/Cmd/Ctrl) being edited. -1 = legacy default
    // = Plain. Tracked so chains under non-Plain modifier slots route to
    // the right destination on poll.
    int                          g_pickerModIdx    = 0;
    // Step index inside the chain — 0 = inline step (legacy), >0 = the
    // (idx-1)'th element of extraSteps. Without this, the poll writes
    // every picked action to step 0 regardless of which step's "Browse
    // Action..." button was clicked (Frank 2026-05-07 — Lua picked for
    // step 2 landed in step 1).
    int                          g_pickerStepIdx   = 0;
    // User-Quick destination coords. Only valid when mode == UserQuick.
    int                          g_pickerUQLayer   = 0;
    int                          g_pickerUQQuick   = 0;
    int                          g_pickerUQSubBank = 0;
    int                          g_pickerUQSlot    = 0;
}

void reasixty_actionPickerStart(int layer, uf8::bindings::ButtonId id,
                                bool longPress, int modIdx, int stepIdx)
{
    g_pickerMode      = PickerMode::Layer;
    g_pickerLayer     = layer;
    g_pickerId        = id;
    g_pickerLongPress = longPress;
    g_pickerModIdx    = modIdx;
    g_pickerStepIdx   = stepIdx;
    g_pickerActive    = true;
    PromptForAction(/*session_mode*/ 1, /*init_id*/ 0, /*section_id*/ 0);
}

bool reasixty_actionPickerActiveFor(int layer, uf8::bindings::ButtonId id,
                                    bool longPress, int modIdx, int stepIdx)
{
    return g_pickerActive
        && g_pickerMode == PickerMode::Layer
        && g_pickerLayer == layer
        && g_pickerId == id
        && g_pickerLongPress == longPress
        && g_pickerModIdx == modIdx
        && g_pickerStepIdx == stepIdx;
}

void reasixty_actionPickerStartUserQuick(int uqLayer, int uqQuick,
                                         int uqSubBank, int uqSlot,
                                         int modIdx, int stepIdx)
{
    g_pickerMode      = PickerMode::UserQuick;
    g_pickerUQLayer   = uqLayer;
    g_pickerUQQuick   = uqQuick;
    g_pickerUQSubBank = uqSubBank;
    g_pickerUQSlot    = uqSlot;
    g_pickerLongPress = false;       // user-Quick editor currently exposes
                                     // only shortPress[Plain] — long-press
                                     // can be added later.
    g_pickerModIdx    = modIdx;
    g_pickerStepIdx   = stepIdx;
    g_pickerActive    = true;
    PromptForAction(/*session_mode*/ 1, /*init_id*/ 0, /*section_id*/ 0);
}

bool reasixty_actionPickerActiveForUserQuick(int uqLayer, int uqQuick,
                                             int uqSubBank, int uqSlot,
                                             int modIdx, int stepIdx)
{
    return g_pickerActive
        && g_pickerMode == PickerMode::UserQuick
        && g_pickerUQLayer == uqLayer
        && g_pickerUQQuick == uqQuick
        && g_pickerUQSubBank == uqSubBank
        && g_pickerUQSlot == uqSlot
        && g_pickerModIdx == modIdx
        && g_pickerStepIdx == stepIdx;
}

void reasixty_actionPickerCancel()
{
    if (!g_pickerActive) return;
    PromptForAction(/*session_mode*/ -1, 0, 0);
    g_pickerActive = false;
}

// Resolve a stored action string ("40044" or "_RS123abc") back to a human-
// readable name via REAPER's kbd_getTextFromCmd. Returns empty for empty
// input, "(unresolved)" if NamedCommandLookup fails (stale named cmd).
std::string reasixty_resolveActionName(const std::string& action)
{
    if (action.empty()) return "";
    int cmd = (action[0] == '_')
        ? NamedCommandLookup(action.c_str())
        : std::atoi(action.c_str());
    if (cmd <= 0) return "(unresolved)";
    const char* n = kbd_getTextFromCmd(cmd, nullptr);
    return (n && *n) ? std::string(n) : std::string("(no name)");
}

// True when the action stored in this binding is a REAPER toggle —
// GetToggleCommandState2 returns 0 or 1 (state known) rather than -1
// (no toggle state). The bindings editor uses this to auto-fire toggle
// actions on the inactive edge (no opt-in needed) while exposing a
// "fire again on inactive" checkbox for one-shot actions.
bool reasixty_actionIsToggle(const std::string& action)
{
    if (action.empty()) return false;
    int cmd = (action[0] == '_')
        ? NamedCommandLookup(action.c_str())
        : std::atoi(action.c_str());
    if (cmd <= 0) return false;
    return GetToggleCommandState2(SectionFromUniqueID(0), cmd) >= 0;
}

// Save-file dialog. macOS goes straight to NSSavePanel via the small
// Cocoa stub in `macos_save_dialog.mm` — SWELL's BrowseForSaveFile is
// not reliably reachable from a REAPER extension on macOS 15 (both
// rec->GetFunc("BrowseForSaveFile") and dlsym(RTLD_DEFAULT, ...) return
// null because REAPER doesn't export the symbol under the hardened
// runtime). Win/Linux still try the legacy SWELL path. g_reaperGetFunc
// is captured at REAPER_PLUGIN_ENTRY for other GetFunc-backed lookups.
static void* (*g_reaperGetFunc)(const char*) = nullptr;

#ifdef __APPLE__
namespace uf8 {
std::string macosSaveDialog(const char* title,
                            const char* defaultName,
                            const char* extension);
}
#else
using BrowseForSaveFile_t = bool(*)(const char* text, const char* initialdir,
                                    const char* initialfile, const char* extlist,
                                    char* fn, int fnsize);
static BrowseForSaveFile_t loadBrowseForSaveFile_()
{
    static BrowseForSaveFile_t p = nullptr;
    if (p) return p;
    if (g_reaperGetFunc) {
        p = reinterpret_cast<BrowseForSaveFile_t>(
            g_reaperGetFunc("BrowseForSaveFile"));
        if (p) return p;
    }
    p = reinterpret_cast<BrowseForSaveFile_t>(
        dlsym(RTLD_DEFAULT, "BrowseForSaveFile"));
    return p;
}
#endif

// Invalidate the LED dedup so the next pushLayerLeds actually emits,
// then push immediately. Called when the editor switches the active
// layer — the 30 Hz tick would catch up eventually but at the cost of
// a visible delay on the hardware LEDs (observed for Layer 3).
void reasixty_onActiveLayerChanged()
{
    g_lastActiveLayer = -1;
    pushLayerLeds(uf8::bindings::getActiveLayer());
}

// Per-layer export — Save-As dialog → write the single layer as a
// {"type":"layer", ...} JSON file. The default filename embeds the layer
// number so users with three saved layers can tell them apart.
bool reasixty_exportLayerViaDialog(int layer)
{
    FILE* lg = std::fopen("/tmp/rea_sixty.log", "a");
    if (lg) std::fprintf(lg, "[exportLayer] enter layer=%d\n", layer);

    char defName[64];
    std::snprintf(defName, sizeof(defName),
                  "rea-sixty-layer-%d.json", layer + 1);
    char title[64];
    std::snprintf(title, sizeof(title),
                  "Export Rea-Sixty layer %d", layer + 1);

    std::string chosen;
#ifdef __APPLE__
    chosen = uf8::macosSaveDialog(title, defName, "json");
    if (chosen.empty()) {
        if (lg) { std::fprintf(lg, "[exportLayer] NSSavePanel cancel/empty\n"); std::fclose(lg); }
        return false;
    }
#else
    auto* browse = loadBrowseForSaveFile_();
    if (!browse) {
        if (lg) { std::fprintf(lg, "[exportLayer] BrowseForSaveFile not loaded\n"); std::fclose(lg); }
        return false;
    }
    char fn[4096] = {0};
    if (!browse(title, nullptr, defName,
                "JSON files (*.json)\0*.json\0All files (*.*)\0*.*\0\0",
                fn, sizeof(fn))) {
        if (lg) { std::fprintf(lg, "[exportLayer] browse() returned 0 (cancel or OS issue), fn='%s'\n", fn); std::fclose(lg); }
        return false;
    }
    chosen = fn;
#endif
    if (lg) std::fprintf(lg, "[exportLayer] dialog OK, path='%s'\n", chosen.c_str());
    const bool ok = uf8::bindings::exportLayerTo(layer, chosen);
    if (lg) { std::fprintf(lg, "[exportLayer] exportLayerTo => %s\n", ok ? "true" : "false"); std::fclose(lg); }
    return ok;
}

// Per-layer import — Open dialog → parse + replace named layer. Returns
// false on cancel or parse error.
bool reasixty_importLayerViaDialog(int layer)
{
    char buf[4096] = {0};
    char title[64];
    std::snprintf(title, sizeof(title),
                  "Import into Rea-Sixty layer %d", layer + 1);
    if (!GetUserFileNameForRead(buf, title, "json")) {
        return false;
    }
    return uf8::bindings::importLayerFrom(layer, buf);
}

// File picker → register ReaScript → return the action string suitable
// for storage in Binding.action. Empty string on cancel/error.
std::string reasixty_loadReaScript()
{
    char buf[4096] = {0};
    if (!GetUserFileNameForRead(buf, "Pick a ReaScript",
                                "lua;eel;py")) {
        return "";
    }
    int cmdId = AddRemoveReaScript(/*add*/ true, /*sectionID*/ 0,
                                   buf, /*commit*/ true);
    if (cmdId <= 0) return "";
    const char* nc = ReverseNamedCommandLookup(cmdId);
    if (nc && *nc) return std::string("_") + nc;
    return std::to_string(cmdId);
}

// Drained from onTimer. Polls PromptForAction; on result writes back to
// the binding flagged at session start. session_mode=0 returns 0 while
// pending, -1 once on cancel, or the picked command-id on accept.
void reasixty_actionPickerPoll()
{
    if (!g_pickerActive) return;
    int r = PromptForAction(/*session_mode*/ 0, 0, 0);
    if (r == 0) return;          // pending
    g_pickerActive = false;
    if (r == -1) return;         // cancelled
    // Convert to stored representation. Built-in REAPER actions are
    // numeric; ReaScripts and custom actions get a "_<name>" prefix.
    std::string actionStr;
    const char* nc = ReverseNamedCommandLookup(r);
    if (nc && *nc) actionStr = std::string("_") + nc;
    else           actionStr = std::to_string(r);
    using namespace uf8::bindings;
    Binding bd = (g_pickerMode == PickerMode::UserQuick)
        ? getUserQuickSlot(g_pickerUQLayer, g_pickerUQQuick,
                           g_pickerUQSubBank, g_pickerUQSlot)
        : getBinding(g_pickerLayer, g_pickerId);
    const int modIdx = (g_pickerModIdx < 0 || g_pickerModIdx >= kModifierCount)
                         ? static_cast<int>(Modifier::Plain)
                         : g_pickerModIdx;
    auto& slot = g_pickerLongPress ? bd.longPress[modIdx]
                                    : bd.shortPress[modIdx];
    // Walk to the step the picker was opened for. stepCount(slot) == 1 + N
    // extraSteps; idx 0 is the inline step, idx >= 1 indexes into
    // extraSteps. Out-of-range falls back to step 0 — defensive guard
    // against a stale picker session that outlived a "+ Add step" undo.
    const int stepIdx = (g_pickerStepIdx >= 0
                         && g_pickerStepIdx < stepCount(slot))
                          ? g_pickerStepIdx : 0;
    ActionStep& target = stepAt(slot, stepIdx);
    target.action = actionStr;
    // Picking a REAPER action via Browse implies the user wants this
    // step to BE a REAPER action — set the type so the inline picker
    // re-renders into the REAPER section without a separate radio click.
    target.type   = ActionType::Reaper;
    if (g_pickerMode == PickerMode::UserQuick) {
        setUserQuickSlot(g_pickerUQLayer, g_pickerUQQuick,
                         g_pickerUQSubBank, g_pickerUQSlot, bd);
    } else {
        setBinding(g_pickerLayer, g_pickerId, bd);
    }
}

// About tab uses these to launch the system handler. macOS-only path
// (`open` CLI); cross-platform later. URL is shell-quoted by single
// quotes — caller is trusted (hard-coded in UI).
void reasixty_openUrl(const char* url)
{
    if (!url || !*url) return;
    char cmd[1024];
    std::snprintf(cmd, sizeof(cmd), "/usr/bin/open '%s'", url);
    std::system(cmd);
}

void reasixty_revealInFinder(const char* path)
{
    if (!path || !*path) return;
    char cmd[1024];
    std::snprintf(cmd, sizeof(cmd), "/usr/bin/open '%s'", path);
    std::system(cmd);
}

void reasixty_exportDiagnostic()
{
    char resultPath[512];
    resultPath[0] = '\0';

    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char ts[32];
    std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", &local);

    char tmpDir[256];
    std::snprintf(tmpDir, sizeof(tmpDir), "/tmp/rea_sixty_diag_%s", ts);
    if (mkdir(tmpDir, 0755) != 0 && errno != EEXIST) {
        char emsg[256];
        std::snprintf(emsg, sizeof(emsg),
                      "Could not create temp dir:\n%s", tmpDir);
        ShowMessageBox(emsg, "Rea-Sixty diagnostic", 0);
        return;
    }

    char infoPath[512];
    std::snprintf(infoPath, sizeof(infoPath), "%s/info.txt", tmpDir);
    if (FILE* f = std::fopen(infoPath, "w")) {
        std::fprintf(f, "Rea-Sixty diagnostic report\n");
        std::fprintf(f, "Generated: %s\n", ts);
        std::fprintf(f, "Build: %s %s\n", __DATE__, __TIME__);
        std::fprintf(f, "REAPER: %s\n", GetAppVersion());
        std::fprintf(f, "\n--- Devices ---\n");
        std::fprintf(f, "UF8 connected: %s\n",
                     (g_dev && g_dev->isOpen()) ? "yes" : "no");
        std::fprintf(f, "UF8 serial:    %s\n",
                     g_dev ? g_dev->serial().c_str() : "");
        std::fprintf(f, "UC1 connected: %s\n",
                     (g_uc1_dev && g_uc1_dev->isOpen()) ? "yes" : "no");
        std::fprintf(f, "UC1 serial:    %s\n",
                     g_uc1_dev ? g_uc1_dev->serial().c_str() : "");
        std::fprintf(f, "\n--- Settings ---\n");
        std::fprintf(f, "Brightness LED:      %d\n", g_brightness.load());
        std::fprintf(f, "Brightness scribble: %d\n", g_scribbleBrightness.load());
        std::fprintf(f, "SEL follows colour:  %s\n",
                     g_selFollowsColor.load() ? "yes" : "no");
        std::fprintf(f, "Ballistic mode:      %d  (0=Peak 1=VU 2=RMS)\n",
                     g_ballisticMode.load());
        std::fclose(f);
    }

    // Pull in our existing trace logs if they exist. Best-effort; missing
    // files don't fail the report.
    char cmd[1024];
    std::snprintf(cmd, sizeof(cmd),
        "cp /tmp/reaper_uf8_frames.log %s/ 2>/dev/null; "
        "cp /tmp/reaper_uf8_colors.log %s/ 2>/dev/null",
        tmpDir, tmpDir);
    std::system(cmd);

    const char* home = std::getenv("HOME");
    if (!home || !*home) home = "/tmp";
    std::snprintf(resultPath, sizeof(resultPath),
                  "%s/Desktop/rea_sixty_diag_%s.zip", home, ts);

    std::snprintf(cmd, sizeof(cmd),
        "cd /tmp && /usr/bin/zip -r '%s' 'rea_sixty_diag_%s' >/dev/null 2>&1",
        resultPath, ts);
    const int zipRc = std::system(cmd);

    // Always clean tmp dir, success or not.
    std::snprintf(cmd, sizeof(cmd), "rm -rf '%s'", tmpDir);
    std::system(cmd);

    char msg[1024];
    if (zipRc == 0) {
        std::snprintf(msg, sizeof(msg),
                      "Diagnostic report written:\n\n%s", resultPath);
    } else {
        std::snprintf(msg, sizeof(msg),
                      "Diagnostic report failed (zip rc=%d).", zipRc);
    }
    ShowMessageBox(msg, "Rea-Sixty diagnostic", 0);
}

int reasixty_ballisticMode()
{
    return g_ballisticMode.load();
}

void reasixty_setBallisticMode(int mode)
{
    if (mode < BM_Peak) mode = BM_Peak;
    if (mode > BM_RMS)  mode = BM_RMS;
    g_ballisticMode.store(mode);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%d", mode);
    SetExtState("rea_sixty", "ballistic_mode", buf, true);
    // Reset envelopes so the new mode starts cleanly from silence rather
    // than carrying old smoothed values that don't match the new τ.
    for (int i = 0; i < 16; ++i) { g_vuEnvDb_[i] = -120.0; g_rmsEnvPow_[i] = 0.0; }
}

// Phase 2.7 Bindings — Phase A. Registers the builtin handlers that
// uf8::bindings::dispatch invokes for the previously-hardcoded global
// buttons. Each handler keeps using the existing atomic globals
// (g_flip, g_pluginFaderMode, g_forcePan, g_shiftHeld, g_encoderMode,
// g_softKeyBank, g_bankOffset, g_mixerToggleRequest, g_pageDirty,
// g_softKeyDirty, g_bankDirty) as state of record so behaviour matches
// the pre-refactor dispatch byte-for-byte.
//
// The handlers run on the libusb worker thread (same as the old inline
// branches did). Anything that mutates REAPER state goes through
// queueInput() so Main_OnCommand / SetTrackAutomationMode run on the
// timer's main-thread drain. Direct atomic stores + sendLedFrames are
// safe from the USB thread (they only touch our own state and the USB
// device's send queue).
void registerBindingHandlers()
{
    using uf8::bindings::BuiltinDescriptor;
    using uf8::bindings::registerBuiltin;

    using DescBuilder = BuiltinDescriptor;

    // Sentinel used by ActionType::Reaper dispatch — funnels Main_OnCommand
    // through the main-thread queue so REAPER's API contract is honoured.
    registerBuiltin("__reaper_action__", DescBuilder{
        [](bool firing, bool /*pressed*/, int actionId) {
            if (firing && actionId > 0) {
                queueInput({PendingInput::MainAction, 0,
                            static_cast<double>(actionId)});
            }
        },
        nullptr, "", false
    });

    registerBuiltin("flip", DescBuilder{
        [](bool firing, bool /*pressed*/, int /*param*/) {
            if (!firing) return;
            const bool next = !g_flip.load();
            g_flip.store(next);
            g_pageDirty.store(true);
            SetExtState("ReaSixty", "flip", next ? "1" : "0", true);
            // Invalidate any in-flight touch context — the captured pb14
            // was meaningful in the OLD FLIP state (e.g. driving D_PAN
            // when FLIP was on). After toggling, the touch-release
            // writeback would otherwise commit that pan-encoded position
            // as a track volume, jumping the fader to the pan value.
            // Clearing g_lastTouchPbValid converts any pending release
            // into a no-op; a fresh move under the new FLIP state will
            // start a clean touch.
            for (auto& v : g_lastTouchPbValid) v.store(false);
        },
        [](int) { return g_flip.load(); },
        "Toggle FLIP (fader ↔ V-Pot)", false
    });

    // ---- Selection-Mode toggles --------------------------------------
    // Four mutually-exclusive modes; firing a mode while it is already
    // active returns to Norm. Setting a different mode while another is
    // active switches directly (no Norm intermediate step). Each toggle
    // persists via ExtState so REAPER restarts preserve the mode.
    auto registerSelectionModeToggle =
        [](const char* name, SelectionMode mode, const char* display) {
            registerBuiltin(name, DescBuilder{
                [mode](bool firing, bool /*pressed*/, int /*param*/) {
                    if (!firing) return;
                    const auto cur  = g_selectionMode.load();
                    const auto next = (cur == mode) ? SelectionMode::Norm
                                                    : mode;
                    g_selectionMode.store(next);
                    // Leaving Instance → drop any GUI ownership and
                    // ask the sync drain to close an open window.
                    if (cur == SelectionMode::Instance
                        && next != SelectionMode::Instance) {
                        g_instanceGuiOwnerStrip.store(-1);
                        g_pluginGuiSyncRequest.store(true);
                    }
                    SetExtState("ReaSixty", "selectionMode",
                                selectionModeStr(next), true);
                    // Force a full LED + bank re-push so the SEL row
                    // recolours immediately and the mode-button LEDs
                    // (driven by StateOf via the bindings layer) flip
                    // on / off without waiting for the next track event.
                    g_pageDirty.store(true);
                    g_bankDirty.store(true);
                },
                [mode](int) {
                    return g_selectionMode.load() == mode;
                },
                display, false
            });
        };
    registerSelectionModeToggle("selection_mode_rec",
                                SelectionMode::Rec,
                                "Selection Mode → REC");
    registerSelectionModeToggle("selection_mode_rec_mon",
                                SelectionMode::RecMon,
                                "Selection Mode → REC + MON");
    registerSelectionModeToggle("selection_mode_auto",
                                SelectionMode::Auto,
                                "Selection Mode → AUTO");
    // Instance Mode — per-strip V-Pot rotation walks ALL FX on the strip's
    // track (not limited to learned CS/BC/UF8 plug-ins, which is what
    // Encoder Instance Cycle does). V-Pot push toggles the GUI of the
    // active FX on the rotating strip; rotation while the GUI is open
    // auto-follows the cycle to the new FX.
    registerSelectionModeToggle("selection_mode_instance",
                                SelectionMode::Instance,
                                "Selection Mode → Instance");

    // Explicit "back to Norm" — bind to the Norm/CLEAR hardware button.
    // Always sets Norm (no toggle); pressing it from Norm is a no-op
    // change but still forces a re-push so the LED layer stays in sync.
    registerBuiltin("selection_mode_norm", DescBuilder{
        [](bool firing, bool /*pressed*/, int /*param*/) {
            if (!firing) return;
            g_selectionMode.store(SelectionMode::Norm);
            SetExtState("ReaSixty", "selectionMode", "norm", true);
            g_pageDirty.store(true);
            g_bankDirty.store(true);
        },
        [](int) { return g_selectionMode.load() == SelectionMode::Norm; },
        "Selection Mode → Norm", false
    });

    // ---- One-shot "all tracks" actions -------------------------------
    // Match the SSL UF8 mode buttons' secondary hardware labels
    // (CLEAR / ALL / ZERO). Standalone so the user binds them wherever
    // they like — no factory long-press defaults seeded.
    registerBuiltin("selection_clear_all", DescBuilder{
        [](bool firing, bool /*pressed*/, int /*param*/) {
            if (!firing) return;
            // REAPER action 40297 = "Track: Unselect (clear selection
            // of) all tracks". Funnel through MainAction so the call
            // lands on the main thread (Main_OnCommand contract).
            queueInput({PendingInput::MainAction, 0, 40297.0});
        },
        nullptr, "Selection: Clear All Tracks", false
    });
    registerBuiltin("tracks_arm_all", DescBuilder{
        [](bool firing, bool /*pressed*/, int /*param*/) {
            if (!firing) return;
            // Toggle: any unarmed track → arm everything; all armed →
            // unarm everything. REAPER action 40490 only arms (no
            // toggle), which left users stuck with everything armed
            // (Frank 2026-05-14). Loop is fine from the input thread —
            // same pattern as the auto_off / auto_read / automation_
            // zero_all builtins.
            const int n = CountTracks(nullptr);
            bool anyUnarmed = false;
            for (int i = 0; i < n && !anyUnarmed; ++i) {
                if (auto* tr = GetTrack(nullptr, i)) {
                    if (GetMediaTrackInfo_Value(tr, "I_RECARM") < 0.5)
                        anyUnarmed = true;
                }
            }
            const int targetArm = anyUnarmed ? 1 : 0;
            for (int i = 0; i < n; ++i) {
                if (auto* tr = GetTrack(nullptr, i)) {
                    const bool armed =
                        GetMediaTrackInfo_Value(tr, "I_RECARM") > 0.5;
                    if (armed != (targetArm == 1))
                        CSurf_OnRecArmChange(tr, targetArm);
                }
            }
        },
        nullptr, "Tracks: Arm All / Unarm All (toggle)", false
    });
    registerBuiltin("automation_zero_all", DescBuilder{
        [](bool firing, bool /*pressed*/, int /*param*/) {
            if (!firing) return;
            // SSL "ZERO" semantic interpreted as: every track's auto
            // mode → Trim/Read (0). Frank can rebind for a different
            // interpretation. SetTrackAutomationMode pattern matches
            // the existing AutoOff/AutoRead builtins (called direct
            // from the input thread).
            const int n = CountTracks(nullptr);
            for (int i = 0; i < n; ++i) {
                if (MediaTrack* tr = GetTrack(nullptr, i))
                    SetTrackAutomationMode(tr, 0);
            }
        },
        nullptr, "Automation: Zero All Tracks (→ Trim/Read)", false
    });

    registerBuiltin("ssl_strip_mode_toggle", DescBuilder{
        [](bool firing, bool /*pressed*/, int /*param*/) {
            if (!firing) return;
            const bool next = !g_pluginFaderMode.load();
            g_pluginFaderMode.store(next);
            // Plain variant is headless — drop the with-GUI flag so a
            // user switching from the GUI builtin to the plain one stops
            // following the GUI. The sync drain then closes any window
            // we'd previously opened.
            g_pluginFaderModeWithGui.store(false);
            g_pluginGuiSyncRequest.store(true);
            // Mutually exclusive with UF8 Plugin Mode — turning SSL Strip
            // on shadows the other anyway (the UF8 path is checked first
            // in every fader/V-Pot branch), so leaving both true just
            // confuses the LED and the user's mental model.
            if (next && g_uf8PluginMode.load()) {
                g_uf8PluginMode.store(false);
                SetExtState("ReaSixty", "uf8PluginMode", "0", true);
            }
            g_pageDirty.store(true);
            // Force LED + display re-push when entering / leaving the
            // user-strip-mode override so unmapped LEDs blank out (and
            // re-illuminate from track state on exit).
            g_bankDirty.store(true);
            SetExtState("ReaSixty", "pluginFaderMode",
                        next ? "1" : "0", true);
        },
        [](int) { return g_pluginFaderMode.load()
                       && !g_pluginFaderModeWithGui.load(); },
        "Toggle SSL Strip Mode", false
    });

    // Same as ssl_strip_mode_toggle but additionally opens / closes the
    // focused track's CS-domain plug-in GUI — built-in (Channel Strip 2 /
    // 4K G / 4K E / 4K B / Link) OR user-learned (e.g. Townhouse CS), at
    // whatever instance the cycle currently points at. Default factory
    // binding is Shift+Plugin (PluginBtn modifier slot). BC-domain
    // plug-ins are skipped — that's uf8_plugin_mode_toggle's domain.
    //
    // The GUI side-effect must run on the main thread (TrackFX_Show
    // creates an AppKit window). This handler runs from the libusb
    // input thread, so it just toggles the mode + sets a request flag.
    // The main-thread timer drains the flag and calls TrackFX_Show.
    registerBuiltin("ssl_strip_mode_toggle_with_gui", DescBuilder{
        [](bool firing, bool /*pressed*/, int /*param*/) {
            if (!firing) return;
            const bool next = !g_pluginFaderMode.load();
            g_pluginFaderMode.store(next);
            g_pluginFaderModeWithGui.store(next);
            // See ssl_strip_mode_toggle for the mutex rationale.
            if (next && g_uf8PluginMode.load()) {
                g_uf8PluginMode.store(false);
                SetExtState("ReaSixty", "uf8PluginMode", "0", true);
            }
            g_pageDirty.store(true);
            // Force LED + display re-push when entering / leaving the
            // user-strip-mode override so unmapped LEDs blank out (and
            // re-illuminate from track state on exit).
            g_bankDirty.store(true);
            SetExtState("ReaSixty", "pluginFaderMode",
                        next ? "1" : "0", true);
            g_pluginGuiSyncRequest.store(true);
        },
        [](int) { return g_pluginFaderMode.load()
                       && g_pluginFaderModeWithGui.load(); },
        "Toggle SSL Strip Mode (with GUI)", false
    });

    registerBuiltin("uf8_plugin_mode_toggle", DescBuilder{
        [](bool firing, bool /*pressed*/, int /*param*/) {
            if (!firing) return;
            const bool next = !g_uf8PluginMode.load();
            g_uf8PluginMode.store(next);
            // Plain variant is headless — drop the with-GUI flag if
            // it was set (e.g. user switched from the GUI builtin to
            // the plain one). The next sync drain will then close any
            // GUI we'd opened.
            g_uf8PluginModeWithGui.store(false);
            g_pluginGuiSyncRequest.store(true);
            // Mutex with SSL Strip Mode — see ssl_strip_mode_toggle.
            if (next && g_pluginFaderMode.load()) {
                g_pluginFaderMode.store(false);
                g_pluginFaderModeWithGui.store(false);
                SetExtState("ReaSixty", "pluginFaderMode", "0", true);
            }
            // Mutex with Selection Mode — the V-Pot rotation/push
            // dispatcher (3507, 3351) routes through SelectionMode
            // first, so an active Instance/Auto/REC mode preempts
            // UF8 Plugin Mode's user-param V-Pot routing. Kicking
            // Selection Mode back to Norm on entry lets the deep-edit
            // controls actually reach the strip (Frank 2026-05-14
            // "instance mit v-pots ausgewählt, UF8 Plugin Mode öffnet
            // nur GUI aber nicht die controls auf UF8").
            if (next && g_selectionMode.load() != SelectionMode::Norm) {
                g_selectionMode.store(SelectionMode::Norm);
                g_instanceGuiOwnerStrip.store(-1);
                SetExtState("ReaSixty", "selectionMode", "norm", true);
            }
            // Snap-on-entry: if the currently focused FX in REAPER is a
            // UF8-mapped plug-in, the next main-thread drain points the
            // mode at it (track + Instance index) so UF8 starts driving
            // the open plug-in instead of whatever the focus/selection
            // happened to be.
            if (next) g_uf8PluginModeSnapRequest.store(true);
            g_pageDirty.store(true);
            g_bankDirty.store(true);
            SetExtState("ReaSixty", "uf8PluginMode",
                        next ? "1" : "0", true);
        },
        [](int) { return g_uf8PluginMode.load()
                       && !g_uf8PluginModeWithGui.load(); },
        "Toggle UF8 Plugin Mode", false
    });

    // GUI variant: same as uf8_plugin_mode_toggle but pops the user
    // plug-in window on entry and closes it on exit. Frank 2026-05-14.
    registerBuiltin("uf8_plugin_mode_toggle_with_gui", DescBuilder{
        [](bool firing, bool /*pressed*/, int /*param*/) {
            if (!firing) return;
            const bool next = !g_uf8PluginMode.load();
            g_uf8PluginMode.store(next);
            g_uf8PluginModeWithGui.store(next);
            if (next && g_pluginFaderMode.load()) {
                g_pluginFaderMode.store(false);
                g_pluginFaderModeWithGui.store(false);
                SetExtState("ReaSixty", "pluginFaderMode", "0", true);
            }
            // Mutex with Selection Mode — see uf8_plugin_mode_toggle.
            if (next && g_selectionMode.load() != SelectionMode::Norm) {
                g_selectionMode.store(SelectionMode::Norm);
                g_instanceGuiOwnerStrip.store(-1);
                SetExtState("ReaSixty", "selectionMode", "norm", true);
            }
            // Snap-on-entry: see uf8_plugin_mode_toggle.
            if (next) g_uf8PluginModeSnapRequest.store(true);
            g_pageDirty.store(true);
            g_bankDirty.store(true);
            SetExtState("ReaSixty", "uf8PluginMode",
                        next ? "1" : "0", true);
            g_pluginGuiSyncRequest.store(true);
        },
        [](int) { return g_uf8PluginMode.load()
                       && g_uf8PluginModeWithGui.load(); },
        "Toggle UF8 Plugin Mode (with GUI)", false
    });

    registerBuiltin("pan_force", DescBuilder{
        [](bool firing, bool /*pressed*/, int /*param*/) {
            if (!firing) return;
            const bool next = !g_forcePan.load();
            g_forcePan.store(next);
            g_pageDirty.store(true);
            SetExtState("ReaSixty", "forcePan", next ? "1" : "0", true);
        },
        [](int) { return g_forcePan.load(); },
        "Toggle V-Pots → Pan", false
    });

    registerBuiltin("mixer_toggle", DescBuilder{
        [](bool firing, bool /*pressed*/, int /*param*/) {
            if (firing) g_mixerToggleRequest.store(true);
        },
        // Without a stateOf, the LED pusher's stateless-cell sweep
        // (boundActionIsActive_) treats this as "always active" and
        // pins Btn360 bright — Frank 2026-05-06. Returning the actual
        // window-visible state lets the LED track open/closed
        // honestly.
        [](int /*param*/) -> bool { return g_mixerWindow.isOpen(); },
        "Open / Close Rea-Sixty Settings", false
    });

    // ---- Phase 2.5 surface-filter modes ----------------------------------
    // Toggles only — actual filter/expand/selection-set logic lands in a
    // follow-up phase. Bind-able now so users can wire them to hardware
    // and the LED feedback loop is in place when the rendering side ships.
    // Pattern mirrors flip / pan_force above: atomic + ExtState + LED-cb.
    registerBuiltin("folder_mode", DescBuilder{
        [](bool firing, bool /*pressed*/, int /*param*/) {
            if (!firing) return;
            const bool next = !g_folderMode.load();
            g_folderMode.store(next);
            // Spilled parent is meaningless without folder mode — drop
            // the reference so toggling folder mode back on starts
            // collapsed (long-press SEL re-spills as needed).
            if (!next) g_spilledParent.store(nullptr);
            g_pageDirty.store(true);
            // Bank-dirty re-renders every strip from the new filtered
            // list — without this, dedup pins each strip to its previous
            // (unfiltered) track until something else changes.
            g_bankDirty.store(true);
            SetExtState("ReaSixty", "folderMode", next ? "1" : "0", true);
        },
        [](int) { return g_folderMode.load(); },
        "Toggle Folder Mode (parents only)", false
    });

    registerBuiltin("show_only_selected", DescBuilder{
        [](bool firing, bool /*pressed*/, int /*param*/) {
            if (!firing) return;
            const bool next = !g_showOnlySelected.load();
            g_showOnlySelected.store(next);
            g_pageDirty.store(true);
            g_bankDirty.store(true);
            SetExtState("ReaSixty", "showOnlySelected",
                        next ? "1" : "0", true);
        },
        [](int) { return g_showOnlySelected.load(); },
        "Toggle Show Only Selected", false
    });

    // Selection-Set recall — single param-driven builtin. param = slot 1..8.
    // Tap recalls; long-press = save (wired by the Long-Press dispatcher
    // when the Selection-Sets editor in Settings is built out).
    registerBuiltin("selset_recall", DescBuilder{
        [](bool firing, bool /*pressed*/, int param) {
            if (!firing) return;
            if (param < 1 || param > 8) return;
            g_selsetActive.store(param);
            g_pageDirty.store(true);
            // TODO Phase 2.5b: pull GUID list from project ExtState
            // (key "selset_<param>") and apply as surface filter.
        },
        [](int param) { return g_selsetActive.load() == param; },
        "Recall Selection Slot", true   // usesParam → Slot spinner in UI
    });

    // FX Learn lebt als Settings-Tab im Mixer-Window, nicht als Builtin —
    // Mapping ist eine Sit-Down-Aktivität (drag-and-drop, click-and-turn),
    // nicht etwas das man auf einen Hardware-Knopf legt.

    // Modifier builtins. `param` selects mode:
    //   0 = Momentary  → modifier active while button is held
    //   1 = Toggle     → press flips active state, second press releases
    // Each handler publishes the resulting state into Bindings so
    // dispatch() can snapshot the active modifier at press-edge of any
    // other button. g_shiftHeld stays in sync for the legacy in-TU
    // fader/scrub-speed readers — mod_shift shadows both atomics.
    auto modHandler = [](uf8::bindings::Modifier m, std::atomic<bool>* legacyMirror) {
        return [m, legacyMirror](bool firing, bool pressed, int param) {
            const bool toggleMode = (param == 1);
            bool newState;
            if (toggleMode) {
                if (!firing || !pressed) return;   // press-edge only
                newState = !uf8::bindings::modifierHeld(m);
            } else {
                newState = pressed;
            }
            uf8::bindings::setModifierHeld(m, newState);
            if (legacyMirror) legacyMirror->store(newState);
        };
    };
    registerBuiltin("mod_shift", DescBuilder{
        modHandler(uf8::bindings::Modifier::Shift, &g_shiftHeld),
        [](int) { return g_shiftHeld.load(); },
        "Modifier: Shift / Fine", true
    });
    registerBuiltin("mod_cmd", DescBuilder{
        modHandler(uf8::bindings::Modifier::Cmd, nullptr),
        nullptr, "Modifier: Cmd", true
    });
    registerBuiltin("mod_ctrl", DescBuilder{
        modHandler(uf8::bindings::Modifier::Ctrl, nullptr),
        nullptr, "Modifier: Ctrl", true
    });

    registerBuiltin("encoder_nav", DescBuilder{
        [](bool firing, bool /*pressed*/, int /*param*/) {
            if (!firing) return;
            g_encoderMode.store(EncoderMode::Nav);
            SetExtState("ReaSixty", "encoderMode", "Nav", true);
        },
        [](int) { return g_encoderMode.load() == EncoderMode::Nav; },
        "Encoder Mode → Nav", false
    });
    registerBuiltin("encoder_nudge", DescBuilder{
        [](bool firing, bool /*pressed*/, int /*param*/) {
            if (!firing) return;
            g_encoderMode.store(EncoderMode::Nudge);
            SetExtState("ReaSixty", "encoderMode", "Nudge", true);
        },
        [](int) { return g_encoderMode.load() == EncoderMode::Nudge; },
        "Encoder Mode → Nudge", false
    });
    registerBuiltin("encoder_focus", DescBuilder{
        [](bool firing, bool /*pressed*/, int /*param*/) {
            if (!firing) return;
            g_encoderMode.store(EncoderMode::Focus);
            SetExtState("ReaSixty", "encoderMode", "Focus", true);
        },
        [](int) { return g_encoderMode.load() == EncoderMode::Focus; },
        "Encoder Mode → Focus", false
    });
    registerBuiltin("encoder_instance", DescBuilder{
        [](bool firing, bool /*pressed*/, int /*param*/) {
            if (!firing) return;
            g_encoderMode.store(EncoderMode::Instance);
            SetExtState("ReaSixty", "encoderMode", "Instance", true);
        },
        [](int) { return g_encoderMode.load() == EncoderMode::Instance; },
        "Encoder Mode → Instance Cycle", false
    });

    // ---- Channel-encoder rotation builtins ------------------------------
    // Bindable to ChannelEncoder.shortPress[modifier]. dispatchEncoder
    // calls run() with `param = stepDelta` (signed integer detents) —
    // these builtins all consume that.
    registerBuiltin("encoder_mode_dispatch", DescBuilder{
        [](bool firing, bool /*pressed*/, int param) {
            if (!firing) return;
            const int step = param;
            switch (g_encoderMode.load()) {
                case EncoderMode::Nav:      applySelectRelative_(step); break;
                case EncoderMode::Nudge:    applyPlayheadNudge_(step);  break;
                case EncoderMode::Focus:    applyMouseScroll_(step);    break;
                case EncoderMode::Instance: applyInstanceCycle_(step);  break;
            }
        },
        nullptr,
        "Encoder: dispatch by current mode (Nav/Nudge/Focus/Instance)",
        false
    });
    registerBuiltin("instance_cycle", DescBuilder{
        [](bool firing, bool /*pressed*/, int param) {
            if (!firing) return;
            applyInstanceCycle_(param);
        },
        nullptr, "Encoder: cycle plug-in instance", false
    });
    registerBuiltin("select_relative", DescBuilder{
        [](bool firing, bool /*pressed*/, int param) {
            if (!firing) return;
            applySelectRelative_(param);
        },
        nullptr, "Encoder: select prev/next track", false
    });
    registerBuiltin("playhead_nudge", DescBuilder{
        [](bool firing, bool /*pressed*/, int param) {
            if (!firing) return;
            applyPlayheadNudge_(param);
        },
        nullptr, "Encoder: nudge playhead", false
    });
    registerBuiltin("mouse_scroll", DescBuilder{
        [](bool firing, bool /*pressed*/, int param) {
            if (!firing) return;
            applyMouseScroll_(param);
        },
        nullptr, "Encoder: scroll mouse-wheel under cursor", false
    });

    // Brightness step builtins — same handlers as the REAPER custom_actions
    // (REASIXTY_BRIGHTNESS_LEDS_UP etc.). Both routes coexist so users can
    // bind via Bindings UI (Native combo) OR via REAPER's keyboard shortcut
    // list.
    registerBuiltin("brightness_leds_up", DescBuilder{
        [](bool firing, bool /*pressed*/, int /*param*/) {
            if (!firing) return;
            brightnessUp();
        },
        nullptr, "Brightness LEDs +", false
    });
    registerBuiltin("brightness_leds_down", DescBuilder{
        [](bool firing, bool /*pressed*/, int /*param*/) {
            if (!firing) return;
            brightnessDown();
        },
        nullptr, "Brightness LEDs -", false
    });
    registerBuiltin("brightness_lcds_up", DescBuilder{
        [](bool firing, bool /*pressed*/, int /*param*/) {
            if (!firing) return;
            brightnessLcdsUp();
        },
        nullptr, "Brightness LCDs +", false
    });
    registerBuiltin("brightness_lcds_down", DescBuilder{
        [](bool firing, bool /*pressed*/, int /*param*/) {
            if (!firing) return;
            brightnessLcdsDown();
        },
        nullptr, "Brightness LCDs -", false
    });
    registerBuiltin("brightness_both_up", DescBuilder{
        [](bool firing, bool /*pressed*/, int /*param*/) {
            if (!firing) return;
            brightnessBothUp();
        },
        nullptr, "Brightness Both (LEDs+LCDs) +", false
    });
    registerBuiltin("brightness_both_down", DescBuilder{
        [](bool firing, bool /*pressed*/, int /*param*/) {
            if (!firing) return;
            brightnessBothDown();
        },
        nullptr, "Brightness Both (LEDs+LCDs) -", false
    });

    // domain_cs / domain_bc — SSL CS/BC focus buttons. EXACT pre-Quick-
    // refactor behaviour: Momentary, sets focused-param domain, clears
    // any active user-Quick on the current layer so the top-soft-key
    // row goes back to plug-in-driven labels. NEVER engages
    // g_activeQuick — that would route the row to empty user-Quick
    // slots and break the SSL CS/BC plug-in maps (Bug 5, 2026-05-13).
    auto domainFocus = [](uf8::Domain target) {
        return DescBuilder{
            [target](bool firing, bool /*pressed*/, int /*param*/) {
                if (!firing) return;
                // UF8 Plugin Mode: top-soft-keys host FX-Learn params,
                // changing focused-param domain would re-route them
                // away from the user-mapped plug-in. Frank 2026-05-13:
                // "die Quick Buttons dürfen nichts an den Soft-Keys
                // ändern, sonst kommt man nicht mehr auf die gelearnten
                // Parameter."
                if (g_uf8PluginMode.load()) return;
                if (uf8::getFocusedParam().domain != target) {
                    uf8::setFocus({target, 0});
                }
                // Drop user-Quick on this layer so the SSL plug-in
                // row reappears immediately.
                const int layer = uf8::bindings::getActiveLayer();
                if (layer >= 0 && layer <= 2
                    && g_activeQuick[layer].exchange(-1) != -1) {
                    g_bankDirty.store(true);
                    g_softKeyDirty.store(true);
                }
            },
            [target](int) {
                if (uf8::getFocusedParam().domain != target) return false;
                const int layer = uf8::bindings::getActiveLayer();
                if (layer < 0 || layer > 2) return true;
                return g_activeQuick[layer].load() < 0;
            },
            "", false
        };
    };
    {
        auto d = domainFocus(uf8::Domain::ChannelStrip);
        d.displayName = "Focus → Channel Strip";
        registerBuiltin("domain_cs", d);
    }
    {
        auto d = domainFocus(uf8::Domain::BusComp);
        d.displayName = "Focus → Bus Comp";
        registerBuiltin("domain_bc", d);
    }

    // Soft-Key Bank 1..9 — direct (Layer, Quick) jumps. Banks 1-3 are
    // Layer 1 Q1-Q3, 4-6 are Layer 2 Q1-Q3, 7-9 are Layer 3 Q1-Q3.
    // Pressing switches the active layer AND engages that layer's
    // Quick in one shot. Always engages (no toggle — Frank 2026-05-13:
    // "Toggle für Quick macht null Sinn — dann wären ja alle Soft-Key
    // Banks leer"). The 9 builtins replace the older quick_select_X
    // / user_domain_X / show_user_bank set entirely.
    auto softKeyBank = [](int bankN) {
        const int layer = (bankN - 1) / 3;
        const int quick = (bankN - 1) % 3;
        return DescBuilder{
            [layer, quick](bool firing, bool /*pressed*/, int /*param*/) {
                if (!firing) return;
                // UF8 Plugin Mode: locked out — same reason as
                // domain_cs/bc above. The Quick-jump would engage a
                // user-Quick which would replace the FX-Learn-driven
                // top-soft-key labels and break access to gelearnte
                // Parameter (Frank 2026-05-13).
                if (g_uf8PluginMode.load()) return;
                if (uf8::bindings::getActiveLayer() != layer) {
                    uf8::bindings::setActiveLayer(layer);
                    pushLayerLeds(layer);
                }
                if (g_activeQuick[layer].exchange(quick) != quick) {
                    g_bankDirty.store(true);
                    g_softKeyDirty.store(true);
                }
            },
            [layer, quick](int) {
                return uf8::bindings::getActiveLayer() == layer
                    && g_activeQuick[layer].load() == quick;
            },
            "", false
        };
    };
    for (int n = 1; n <= 9; ++n) {
        auto d = softKeyBank(n);
        char name[32]; std::snprintf(name, sizeof(name), "softkey_bank_%d", n);
        char dn[40];   std::snprintf(dn,   sizeof(dn),   "Soft-Key Bank %d", n);
        d.displayName = dn;
        registerBuiltin(name, d);
    }

    // Multi-instance picker — bindable equivalents of Shift+Channel-Encoder.
    // Domain follows the focused-param domain (Quick1 → CS, Quick2 → BC),
    // same convention as the encoder cycle. cycleInstance wraps modulo
    // count, so repeated presses walk the ring without stopping at the end.
    registerBuiltin("instance_next", DescBuilder{
        [](bool firing, bool /*pressed*/, int /*param*/) {
            if (!firing) return;
            applyInstanceCycle_(+1);
        },
        nullptr, "Instance: next (focused domain)", false
    });
    registerBuiltin("instance_prev", DescBuilder{
        [](bool firing, bool /*pressed*/, int /*param*/) {
            if (!firing) return;
            applyInstanceCycle_(-1);
        },
        nullptr, "Instance: previous (focused domain)", false
    });

    // Layer select — one builtin per layer so the picker shows
    // self-documenting rows (no magic param). The legacy
    // "layer_select" + param entry stays registered for backwards-
    // compat with bindings.json files saved before the split.
    auto layerSelect = [](int target) {
        return DescBuilder{
            [target](bool firing, bool /*pressed*/, int /*param*/) {
                if (!firing) return;
                uf8::bindings::setActiveLayer(target);
                pushLayerLeds(target);
            },
            nullptr,
            (target == 0 ? "Switch to Layer 1"
             : target == 1 ? "Switch to Layer 2"
                           : "Switch to Layer 3"),
            false
        };
    };
    registerBuiltin("layer_select_1", layerSelect(0));
    registerBuiltin("layer_select_2", layerSelect(1));
    registerBuiltin("layer_select_3", layerSelect(2));

    // Automation modes — one builtin per REAPER mode so the picker
    // shows self-documenting names. Off and Trim both map to mode 0
    // (REAPER has no separate "off"; SSL convention puts both on the
    // hardware row). LED pre-empt re-evaluates all 6 Auto-row cells with
    // the new mode + the current global override so cells bound to
    // either variant land on the right state instantly, masking the
    // firmware's transition flash through TRIM.
    auto autoMode = [](int reaperMode, const char* label) {
        return DescBuilder{
            [reaperMode](bool firing, bool /*pressed*/, int /*param*/) {
                if (!firing) return;
                queueInput({PendingInput::AutomationMode, 0,
                            static_cast<double>(reaperMode)});
                pushAutoModeLedsMixed_(reaperMode,
                                       GetGlobalAutomationOverride(),
                                       uf8::bindings::getActiveLayer());
            },
            // stateOf — selected-track mode matches reaperMode. Mode 0
            // is suppressed (REAPER's per-track default applies to
            // every fresh track, so reporting auto_off/auto_trim as
            // active everywhere would pin TRIM/OFF on). auto_latch
            // also reports active for Latch Preview (mode 5) since the
            // shared LATCH LED covers both — keeps cells bound to
            // auto_latch outside the Auto row consistent with the
            // Auto-row LATCH cell's special-case.
            [reaperMode](int) -> bool {
                if (reaperMode == 0) return false;
                MediaTrack* sel = GetSelectedTrack(nullptr, 0);
                if (!sel) return false;
                const int cur = GetTrackAutomationMode(sel);
                if (reaperMode == 4 && cur == 5) return true;
                return cur == reaperMode;
            },
            label, false
        };
    };
    registerBuiltin("auto_off",       autoMode(0, "Automation: Off / Trim"));
    registerBuiltin("auto_read",      autoMode(1, "Automation: Read"));
    registerBuiltin("auto_write",     autoMode(3, "Automation: Write"));
    registerBuiltin("auto_trim",      autoMode(0, "Automation: Trim"));
    registerBuiltin("auto_latch",     autoMode(4, "Automation: Latch"));
    registerBuiltin("auto_latch_prv", autoMode(5, "Automation: Latch Prv"));
    registerBuiltin("auto_touch",     autoMode(2, "Automation: Touch"));

    // Global automation override — mirrors every per-track action above
    // but routes through REAPER's SetGlobalAutomationOverride so the
    // chosen mode applies to all tracks regardless of their per-track
    // setting. Same LED pre-empt as the per-track variants, with the
    // new value supplied as the global override.
    auto autoModeGlobal = [](int reaperMode, const char* label) {
        return DescBuilder{
            [reaperMode](bool firing, bool /*pressed*/, int /*param*/) {
                if (!firing) return;
                queueInput({PendingInput::AutomationModeGlobal, 0,
                            static_cast<double>(reaperMode)});
                int perTrack = -1;
                if (MediaTrack* sel = GetSelectedTrack(nullptr, 0)) {
                    perTrack = GetTrackAutomationMode(sel);
                }
                pushAutoModeLedsMixed_(perTrack, reaperMode,
                                       uf8::bindings::getActiveLayer());
            },
            // stateOf — global override matches reaperMode. No mode-0
            // suppression here: a global override of 0 is an explicit
            // user choice (vs default -1 = no override). auto_latch
            // also covers Latch Preview (mode 5) to match the per-
            // track variant.
            [reaperMode](int) -> bool {
                const int cur = GetGlobalAutomationOverride();
                if (reaperMode == 4 && cur == 5) return true;
                return cur == reaperMode;
            },
            label, false
        };
    };
    registerBuiltin("auto_off_global",
                    autoModeGlobal(0, "Automation: Off / Trim (Global)"));
    registerBuiltin("auto_read_global",
                    autoModeGlobal(1, "Automation: Read (Global)"));
    registerBuiltin("auto_write_global",
                    autoModeGlobal(3, "Automation: Write (Global)"));
    registerBuiltin("auto_trim_global",
                    autoModeGlobal(0, "Automation: Trim (Global)"));
    registerBuiltin("auto_latch_global",
                    autoModeGlobal(4, "Automation: Latch (Global)"));
    registerBuiltin("auto_latch_prv_global",
                    autoModeGlobal(5, "Automation: Latch Prv (Global)"));
    registerBuiltin("auto_touch_global",
                    autoModeGlobal(2, "Automation: Touch (Global)"));

    registerBuiltin("bank_left", DescBuilder{
        [](bool firing, bool pressed, int /*param*/) {
            sendUf8GlobalLed(uf8::Uf8GlobalLed::BankLeft, pressed);
            if (!firing) return;
            const int trackCount = visibleTrackCount();
            const int maxStart   = trackCount > 1 ? trackCount - 1 : 0;
            int next = g_bankOffset.load() - 8;
            if (next < 0)        next = 0;
            if (next > maxStart) next = maxStart;
            if (next != g_bankOffset.exchange(next)) g_bankDirty.store(true);
        },
        nullptr, "Bank ← (8-strip scroll left)", false
    });
    registerBuiltin("bank_right", DescBuilder{
        [](bool firing, bool pressed, int /*param*/) {
            sendUf8GlobalLed(uf8::Uf8GlobalLed::BankRight, pressed);
            if (!firing) return;
            const int trackCount = visibleTrackCount();
            const int maxStart   = trackCount > 1 ? trackCount - 1 : 0;
            int next = g_bankOffset.load() + 8;
            if (next < 0)        next = 0;
            if (next > maxStart) next = maxStart;
            if (next != g_bankOffset.exchange(next)) g_bankDirty.store(true);
        },
        nullptr, "Bank → (8-strip scroll right)", false
    });

    // ---- Send / Receive routing builtins -------------------------------
    // 8 + 1 per category (sends, receives) = 18 builtins. Each one
    // toggles a routing mode on/off; the binding's `param` selects the
    // physical output:
    //   param = 0  → Faders   (default, shown as "Flip" unchecked)
    //   param = 1  → V-Pots   (shown as "Flip" checked)
    // Each handler reads its own param to decide which atomic to flip.
    // Modes within one physical output stay mutually exclusive (handled
    // by clearVpotRouting_ / clearFaderRouting_).

    auto allHandler = [](std::atomic<int>* outFader, std::atomic<int>* outVpot,
                         void (*clearVpotOthers)(), void (*clearFaderOthers)())
    {
        return [outFader, outVpot, clearVpotOthers, clearFaderOthers](int n) {
            return DescBuilder{
                [outFader, outVpot, clearVpotOthers, clearFaderOthers, n]
                (bool firing, bool, int param) {
                    if (!firing) return;
                    auto* out = (param == 1) ? outVpot : outFader;
                    auto  clr = (param == 1) ? clearVpotOthers : clearFaderOthers;
                    if (out->load() == n) {
                        out->store(-1);                     // toggle off
                    } else {
                        clr();
                        out->store(n);
                    }
                },
                // Both atomics light the LED; mode is "active" if EITHER
                // physical output is currently routed to this index.
                [outFader, outVpot, n](int) {
                    return outFader->load() == n || outVpot->load() == n;
                },
                "",
                true   // usesParam = true (Flip flag)
            };
        };
    };
    auto sendAll = allHandler(&g_sendFaderAllIdx, &g_sendVpotAllIdx,
                              &clearVpotRouting_, &clearFaderRouting_);
    auto recvAll = allHandler(&g_recvFaderAllIdx, &g_recvVpotAllIdx,
                              &clearVpotRouting_, &clearFaderRouting_);

    for (int n = 0; n < 8; ++n) {
        char nameBuf[24], descBuf[64];

        std::snprintf(nameBuf, sizeof(nameBuf), "send_all_%d", n + 1);
        std::snprintf(descBuf, sizeof(descBuf),
                      "Send %d ↦ all tracks", n + 1);
        auto s = sendAll(n); s.displayName = descBuf;
        registerBuiltin(nameBuf, s);

        std::snprintf(nameBuf, sizeof(nameBuf), "recv_all_%d", n + 1);
        std::snprintf(descBuf, sizeof(descBuf),
                      "Receive %d ↦ all tracks", n + 1);
        auto r = recvAll(n); r.displayName = descBuf;
        registerBuiltin(nameBuf, r);
    }

    // "This track, 8 sends/receives" handler — same param convention.
    auto thisHandler = [](std::atomic<bool>* outFader, std::atomic<bool>* outVpot,
                          void (*clearVpotOthers)(), void (*clearFaderOthers)())
    {
        return DescBuilder{
            [outFader, outVpot, clearVpotOthers, clearFaderOthers]
            (bool firing, bool, int param) {
                if (!firing) return;
                auto* out = (param == 1) ? outVpot : outFader;
                auto  clr = (param == 1) ? clearVpotOthers : clearFaderOthers;
                if (out->load()) {
                    out->store(false);
                } else {
                    clr();
                    out->store(true);
                }
            },
            [outFader, outVpot](int) {
                return outFader->load() || outVpot->load();
            },
            "", true
        };
    };
    {
        auto d = thisHandler(&g_sendFaderThisTrack, &g_sendVpotThisTrack,
                             &clearVpotRouting_, &clearFaderRouting_);
        d.displayName = "8 sends of focused track";
        registerBuiltin("send_this", d);
    }
    {
        auto d = thisHandler(&g_recvFaderThisTrack, &g_recvVpotThisTrack,
                             &clearVpotRouting_, &clearFaderRouting_);
        d.displayName = "8 receives of focused track";
        registerBuiltin("recv_this", d);
    }

    // SOFTKEY_BANK_SELECT — switches the SSL plug-in's PAGE bank
    // (0 = V-POT, 1..5 = Bank N). Default for the V-POT/Bank1..5
    // hardware row, equivalent to SSL 360°'s PAGE ← / →. Pressing also
    // clears the global Pan override (matches SSL paradigm: "I want
    // params, not pan"). Bank index is clamped to whatever
    // softkey::maxBankFor reports for the focused domain.
    registerBuiltin("softkey_bank_select", DescBuilder{
        [](bool firing, bool /*pressed*/, int param) {
            if (!firing) return;
            // UF8 Plugin Mode: SSL Soft-Key Bank cells (V-POT + Soft
            // 1-5) are no-function — bank navigation moves to the 8
            // TopSoftKeys (Frank 2026-05-13: "Soft-Key Banks
            // no-function in UF8 plugin mode").
            if (g_uf8PluginMode.load()) return;
            const int layer = uf8::bindings::getActiveLayer();
            const int activeQuick = (layer >= 0 && layer <= 2)
                ? g_activeQuick[layer].load() : -1;
            // User-Quick context: the same hardware bank-row keys now
            // pick which of the 6 sub-banks (V-POT + Soft 1..5) drives
            // the top-soft-key row. SSL g_softKeyBank stays untouched
            // so re-entering CS/BC context resumes whatever the user
            // last navigated on the plug-in side.
            if (activeQuick >= 0) {
                int target = param;
                if (target < 0) target = 0;
                if (target >= uf8::bindings::kSubBanksPerQuick)
                    target = uf8::bindings::kSubBanksPerQuick - 1;
                if (g_activeSubBank[layer].exchange(target) != target) {
                    g_softKeyDirty.store(true);
                    g_bankDirty.store(true);
                }
                return;
            }
            const auto fp = uf8::getFocusedParam();
            const auto domain = (fp.domain == uf8::Domain::BusComp)
                ? uf8::Domain::BusComp : uf8::Domain::ChannelStrip;
            const int maxBank = softkey::maxBankFor(domain);
            int target = param;
            if (target < 0)        target = 0;
            if (target > maxBank)  target = maxBank;
            if (g_softKeyBank.exchange(target) != target) {
                g_softKeyDirty.store(true);
                char buf[8];
                std::snprintf(buf, sizeof(buf), "%d", target);
                SetExtState("ReaSixty", "softKeyBank", buf, true);
            }
            if (g_forcePan.load()) {
                g_forcePan.store(false);
                g_pageDirty.store(true);
                SetExtState("ReaSixty", "forcePan", "0", true);
            }
        },
        [](int param) {
            const int layer = uf8::bindings::getActiveLayer();
            const int activeQuick =
                (layer >= 0 && layer <= 2) ? g_activeQuick[layer].load() : -1;
            if (activeQuick >= 0) {
                return g_activeSubBank[layer].load() == param;
            }
            return g_softKeyBank.load() == param;
        },
        "Select soft-key bank (param 0..5)", true
    });

    // SSL_SOFTKEY — default action for the per-strip top-soft-keys.
    // param 0..7 = which strip the binding sits on. Looks up the
    // CURRENT PAGE bank in the focused-domain plugin map (so the row
    // changes meaning as the user steps banks via V-POT/Bank1..5 — the
    // SSL 360° default behaviour). Slots mapped to kNoSlot in the
    // plugin tables silently no-op.
    registerBuiltin("ssl_softkey", DescBuilder{
        [](bool firing, bool /*pressed*/, int param) {
            if (!firing) return;
            if (param < 0 || param >= 8) return;
            const auto fp = uf8::getFocusedParam();
            const auto domain = (fp.domain == uf8::Domain::BusComp)
                ? uf8::Domain::BusComp : uf8::Domain::ChannelStrip;
            const int bank = std::clamp(g_softKeyBank.load(),
                0, softkey::maxBankFor(domain));
            const auto v = softkey::viewFor(domain, bank);
            const int linkIdx = v.linkIdx[param];
            if (linkIdx != softkey::kNoSlot) {
                uf8::setFocus({domain, linkIdx});
            }
        },
        nullptr, "SSL Soft-Key (current bank, slot 0..7)", true
    });

    // SSL_BANK_* — explicit-bank versions. Six builtins (V-POT + Bank
    // 1..5), each with param 0..7 selecting the slot WITHIN THAT
    // SPECIFIC bank. Lets the user wire any button to a fixed SSL
    // parameter (e.g. "always focus HMF Q") without depending on the
    // global PAGE-bank state. Picker presents the param as a combo
    // listing the actual function names from softkey::kCsLabels so the
    // user picks "HMF Q" rather than the numeric 2.
    auto sslBankHandler = [](int bankIdx) {
        return DescBuilder{
            [bankIdx](bool firing, bool /*pressed*/, int param) {
                if (!firing) return;
                if (param < 0 || param >= 8) return;
                const auto fp = uf8::getFocusedParam();
                const auto domain = (fp.domain == uf8::Domain::BusComp)
                    ? uf8::Domain::BusComp : uf8::Domain::ChannelStrip;
                const int b = std::clamp(bankIdx,
                    0, softkey::maxBankFor(domain));
                const auto v = softkey::viewFor(domain, b);
                const int linkIdx = v.linkIdx[param];
                if (linkIdx != softkey::kNoSlot) {
                    uf8::setFocus({domain, linkIdx});
                }
            },
            nullptr, "", true
        };
    };
    {
        auto d = sslBankHandler(0);
        d.displayName = "SSL Standard Bank: V-POT";
        registerBuiltin("ssl_bank_vpot", d);
    }
    for (int b = 1; b <= 5; ++b) {
        char name[24], desc[40];
        std::snprintf(name, sizeof(name), "ssl_bank_%d", b);
        std::snprintf(desc, sizeof(desc), "SSL Standard Bank %d", b);
        auto d = sslBankHandler(b);
        d.displayName = desc;
        registerBuiltin(name, d);
    }

    // HOME — one-press exit from every routing toggle. Restores V-Pots
    // and faders to their normal track-volume + pan view. Default for
    // the Channel button so the user always has a "go back" button no
    // matter how many routing modes they layered on. Also drops the
    // active Quick on the current layer so the top-soft-key row returns
    // to its plugin-driven labels.
    registerBuiltin("home", DescBuilder{
        [](bool firing, bool /*pressed*/, int /*param*/) {
            if (!firing) return;
            clearVpotRouting_();
            clearFaderRouting_();
            const int layer = uf8::bindings::getActiveLayer();
            if (layer >= 0 && layer <= 2
                && g_activeQuick[layer].exchange(-1) != -1) {
                g_bankDirty.store(true);
                g_softKeyDirty.store(true);
            }
        },
        nullptr, "Home (clear routing toggles)", false
    });

    auto pageStep = [](int delta) {
        const auto fp = uf8::getFocusedParam();
        const auto domain = (fp.domain == uf8::Domain::BusComp)
            ? uf8::Domain::BusComp : uf8::Domain::ChannelStrip;
        const int maxBank = softkey::maxBankFor(domain);
        int next = g_softKeyBank.load() + delta;
        if (next < 0)       next = 0;
        if (next > maxBank) next = maxBank;
        if (g_softKeyBank.exchange(next) != next) {
            g_softKeyDirty.store(true);
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%d", next);
            SetExtState("ReaSixty", "softKeyBank", buf, true);
        }
    };
    registerBuiltin("page_left", DescBuilder{
        [pageStep](bool firing, bool pressed, int /*param*/) {
            sendUf8GlobalLed(uf8::Uf8GlobalLed::PageLeft, pressed);
            if (firing) pageStep(-1);
        },
        nullptr, "Page ← (soft-key bank prev)", false
    });
    registerBuiltin("page_right", DescBuilder{
        [pageStep](bool firing, bool pressed, int /*param*/) {
            sendUf8GlobalLed(uf8::Uf8GlobalLed::PageRight, pressed);
            if (firing) pageStep(1);
        },
        nullptr, "Page → (soft-key bank next)", false
    });

    // Zoom pad — bundled builtins (REAPER action + LED feedback). Phase B
    // collapses these into ActionType::Reaper once per-binding LED config
    // lands.
    auto zoomBuiltin = [](uf8::Uf8GlobalLed led, int actionId, const char* label) {
        return DescBuilder{
            [led, actionId](bool firing, bool pressed, int /*param*/) {
                sendUf8GlobalLed(led, pressed);
                if (firing && actionId) {
                    queueInput({PendingInput::MainAction, 0,
                                static_cast<double>(actionId)});
                }
            },
            nullptr, label, false
        };
    };
    registerBuiltin("zoom_up",     zoomBuiltin(uf8::Uf8GlobalLed::ZoomUp,     40112, "Zoom in vertically"));
    registerBuiltin("zoom_down",   zoomBuiltin(uf8::Uf8GlobalLed::ZoomDown,   40111, "Zoom out vertically"));
    registerBuiltin("zoom_left",   zoomBuiltin(uf8::Uf8GlobalLed::ZoomLeft,   1011,  "Zoom out horizontally"));
    registerBuiltin("zoom_right",  zoomBuiltin(uf8::Uf8GlobalLed::ZoomRight,  1012,  "Zoom in horizontally"));
    registerBuiltin("zoom_center", zoomBuiltin(uf8::Uf8GlobalLed::ZoomCenter, 40295, "Zoom to fit project"));
}

extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(
    REAPER_PLUGIN_HINSTANCE hInstance, reaper_plugin_info_t* rec)
{
    if (!rec) {
        // Unload. REAPER destroys our ReaSixtySurface instances (if any
        // still exist) via IReaperControlSurface's virtual destructor;
        // those destructors tear down the USB device and timer. Here we
        // just un-register the class so no new instances can be created.
        plugin_register("-csurf", &g_csurfReg);
        return 0;
    }

    if (rec->caller_version != REAPER_PLUGIN_VERSION) return 0;
    if (REAPERAPI_LoadAPI(rec->GetFunc) != 0) return 0;

    // Capture rec->GetFunc for SWELL APIs not in the plug-in SDK
    // (e.g. BrowseForSaveFile — see reasixty_exportLayerViaDialog).
    g_reaperGetFunc = rec->GetFunc;

    // Multi-instance picker glue: wire the active-instance lookup so
    // uf8::lookupPluginOnTrack(tr, domain) returns the Nth matching FX
    // instead of always the first. Without this, when the user picks
    // a non-default BC instance via the cycle encoder, UF8 V-Pot
    // rendering AND UC1's chase-back-to-UF8 mapping (UC1Surface.cpp
    // ~line 750) would still resolve against the FIRST BC's vst3 →
    // linkIdx mapping — producing visibly wrong slot names (e.g.
    // turning Townhouse's Threshold showed BC2's "Attack" because
    // BC2's slot at vst3=4 has linkIdx=3=Attack while Townhouse's
    // vst3=4 was bound to linkIdx=1=Threshold).
    uf8::setInstanceIdxProvider([](void* tr, uf8::Domain d) -> int {
        if (d == uf8::Domain::BusComp)      return uc1::bcInstanceIndex(tr);
        if (d == uf8::Domain::ChannelStrip) return uc1::csInstanceIndex(tr);
        return 0;
    });

    // Restore persisted UI mode flags (Pan override, encoder mode) so
    // they survive REAPER restarts. ExtState is global per-extension —
    // persistAcrossSessions=true writes through to reaper-extstate.ini.
    if (const char* v = GetExtState("ReaSixty", "forcePan");
        v && v[0] == '1') {
        g_forcePan.store(true);
    }
    if (const char* v = GetExtState("ReaSixty", "folderMode");
        v && v[0] == '1') {
        g_folderMode.store(true);
    }
    if (const char* v = GetExtState("ReaSixty", "showOnlySelected");
        v && v[0] == '1') {
        g_showOnlySelected.store(true);
    }
    // Don't restore FLIP state — start with FLIP off so the user has
    // a known-good baseline. Re-enable persistence once the FLIP code
    // paths are fully de-risked.
    SetExtState("ReaSixty", "flip", "0", true);
    if (const char* m = GetExtState("ReaSixty", "encoderMode"); m && *m) {
        if (std::strcmp(m, "Nudge") == 0)         g_encoderMode.store(EncoderMode::Nudge);
        else if (std::strcmp(m, "Focus") == 0)    g_encoderMode.store(EncoderMode::Focus);
        else if (std::strcmp(m, "Instance") == 0) g_encoderMode.store(EncoderMode::Instance);
        else                                      g_encoderMode.store(EncoderMode::Nav);
    }
    // softKeyBank intentionally NOT restored from ExtState — every
    // REAPER load starts on V-POT (bank 0) so the row matches what
    // the user sees on the SSL plug-in immediately. The atomic's
    // default-init to 0 covers it; the ExtState write side stays so
    // the value can survive a mid-session extension reload, but a
    // fresh REAPER session always boots on V-POT.

    // Phase 2.7 Bindings — register builtins + load JSON config BEFORE
    // the csurf class registration. The surface ctor opens USB, which
    // starts the input thread; that thread calls bindings::dispatch on
    // the first key press, so builtins must be registered and the
    // active-layer config must be loaded by then. Order: register
    // handlers, then load (which may seed factories on first run and
    // write bindings.json under the REAPER resource path), then csurf.
    registerBindingHandlers();
    uf8::bindings::load();
    // Phase 2.5d-A — user-learned plugin catalogue. Load after the
    // built-in PluginMap registry is initialised (it's static so it's
    // always ready). Two-stage lookup in lookupPluginMapByName falls
    // through to this catalogue.
    uf8::user_plugins::load();

    // Register as a full control-surface class. The user adds a
    // "Rea-Sixty" entry in Preferences → Control/OSC/Web; REAPER then
    // calls createReaSixty() to instantiate ReaSixtySurface, which
    // opens the UF8 and starts the timer.
    plugin_register("csurf", &g_csurfReg);

    // Custom actions: brightness up/down. REAPER assigns a command ID
    // when we register — stash it for dispatch in hookCommand.
    g_cmdBrightnessUp   = plugin_register("custom_action", &g_actionBrightnessUp);
    g_cmdBrightnessDown = plugin_register("custom_action", &g_actionBrightnessDown);
    g_cmdProbeLed       = plugin_register("custom_action", &g_actionProbeLed);
    g_cmdProbeLegacyLed = plugin_register("custom_action", &g_actionProbeLegacyLed);
    g_cmdFrameTrace     = plugin_register("custom_action", &g_actionFrameTrace);
    g_cmdToggleFaderCal = plugin_register("custom_action", &g_actionToggleFaderCal);
    g_cmdToggleFaderInputLog = plugin_register("custom_action", &g_actionToggleFaderInputLog);
    g_cmdUc1TestIndicator = plugin_register("custom_action", &g_actionUc1TestIndicator);
    g_cmdProbeGateGr    = plugin_register("custom_action", &g_actionProbeGateGr);
    g_cmdDumpParams     = plugin_register("custom_action", &g_actionDumpParams);
    g_cmdDumpChunk      = plugin_register("custom_action", &g_actionDumpChunk);
    g_cmdDumpRouting    = plugin_register("custom_action", &g_actionDumpRouting);
    g_cmdToggleMixer    = plugin_register("custom_action", &g_actionToggleMixer);
    plugin_register("hookcommand2", reinterpret_cast<void*>(hookCommand2));

    return 1;
}
