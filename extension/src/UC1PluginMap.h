#pragma once
//
// UC1PluginMap — UC1 hardware control IDs → VST3 parameter indices per
// supported SSL plugin.
//
// UC1's surface is a fixed layout, so unlike UF8's pageable slots each
// UC1 knob / button maps to exactly one plugin parameter. Two plugin
// contexts matter:
//
//   * Bus Comp 2 — drives the top-center 7 V-Pots (knob IDs 0x0E..0x16)
//     and the Bus Comp IN button (0x0C).
//   * Channel Strip (CS 2 / 4K E / 4K G / 4K B) — drives the dedicated
//     EQ knobs (0x00..0x0B) and the Dyn/Gate knobs (0x17..0x1D), plus
//     the Channel-section buttons and plugin-bypass (Channel IN 0x1E).
//     When NO Bus Comp 2 is loaded SSL 360° repurposes the V-Pots 0x0C
//     and 0x16 to drive Channel-Strip Input Trim / Fader Level.
//
// A track can have both — UC1 drives whichever plugin each knob/button
// belongs to. `Bindings` is what the surface looks up at the top of
// every event handler.
//

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "UC1Protocol.h"

namespace uf8 { struct PushStep; }  // UserPluginCatalog.h

namespace uc1 {

// Sentinel "this control isn't mapped for this plugin" — skip the event.
constexpr int kParamNone = -1;

// Per-plugin mapping: indexed by UC1 control ID, returns VST3 param idx.
// Static tables live in UC1PluginMap.cpp.
struct PluginBindings {
    const char* match;             // substring of TrackFX_GetFXName
    const char* shortName;         // 4-char display ("CS 2", "BC 2", "4K E"…)

    // knob ID → VST3 param index. -1 = unmapped.
    // Covers all 30 UC1 knob IDs (0x00..0x1D). Index into this array is
    // the raw knob_id byte from a FF 24 event.
    int knobParam[0x20];

    // button ID → VST3 param index. -1 = unmapped.
    // Covers all 32 UC1 button IDs (0x00..0x1F). Index is the raw button
    // byte from a FF 22 event.
    int buttonParam[0x20];

    // VST3 index of the plugin's "bypass" parameter, if any. Used by
    // the Channel IN / Bus Comp IN buttons to bypass the whole plugin.
    // -1 = no mapped bypass (plugin bypass via TrackFX_SetEnabled).
    int bypassParam;

    // Semantic of bypassParam. SSL-stock plug-ins expose a "Bypass"
    // param where value=1 means bypassed → IN LED OFF. Some user
    // plug-ins (e.g. bx_townhouse Buss Comp) expose a "Comp In"
    // param with the inverted sense: value=1 means active → IN LED ON.
    // When inverted=true, the IN LED + readout treat value≥0.5 as
    // "active". Toggle write itself still flips between 0 and 1.
    bool bypassInverted = false;

    // Sign-flip per knob. For params where physical CW should decrease
    // the VST3 value (rare; most SSL params are CW=up). Indexed by knob
    // ID like knobParam.
    bool inverted[0x20];

    // Per-button invert (user FX-Learn override). Flips the LED + readout
    // "active" sense for a param-toggle button so a plug-in whose param
    // reads 1=off still lights the button when active. Indexed by button
    // ID like buttonParam. The IN / Bus-Comp-IN bypass buttons use
    // bypassInverted instead (linkIdx 0). Appended last so the positional
    // aggregate-init of the built-in SSL binding tables stays valid (this
    // member zero-fills). Frank 2026-06-02.
    bool buttonInverted[0x20];

    // POL button behaviour. true (default) = the Polarity button toggles
    // REAPER track phase (B_PHASE). false = it drives the FX-Learned param
    // on the Polarity slot like any other button. Copied from
    // UserPluginMap.useReaperTrackPolarity in synthesizeUserBinding_;
    // built-in SSL maps keep the default via the member initialiser.
    // Frank 2026-06-02.
    bool polarityUsesTrack = true;
};

// Resolved lookup for a given track state: two plugin contexts plus
// each plugin's FX index in the track's chain.
struct UC1Bindings {
    const PluginBindings* busCompMap    = nullptr;  // Bus Compressor 2 on track (or null)
    int                   busCompFxIdx  = -1;
    const PluginBindings* channelMap    = nullptr;  // Channel Strip plugin on track (or null)
    int                   channelFxIdx  = -1;
    // GR-meter override per slot — populated only when the binding came
    // from a learned (user-mapped) plugin AND the user designated a
    // VST3 parameter as the gain-reduction readout in Settings → FX
    // Learn. -1 means "fall back to TrackFX_GetNamedConfigParm
    // (GainReduction_dB)" (built-in plug-ins, or learned plug-ins with
    // no GR pick yet). Offset is added to the formatted-value parse so
    // a plug-in that reports negative-going GR can be shifted into
    // positive dB for the 0..max display.
    int                   busCompGrParam   = -1;
    double                busCompGrOffsetDb = 0.0;
    int                   channelGrParam   = -1;
    double                channelGrOffsetDb = 0.0;
    // Per-breakpoint correction tables — point into the owning
    // UserBindingEntry in g_userCache when a learned user plug-in is
    // bound; nullptr for built-ins (which apply identity calibration).
    // Lifetime: stable until the user_plugins generation bumps, which
    // only happens on the main thread between polls.
    //   busCompGrBcVuCal: 6 doubles at 0/4/8/12/16/20 dB (BC VU motor)
    //   channelGrLedsCal: 5 doubles at 3/6/10/14/20 dB (DYN GR LEDs + UF8 GR byte)
    const double*         busCompGrBcVuCal  = nullptr;
    const double*         channelGrLedsCal  = nullptr;
};

// Walk TrackFX_GetCount on a track and return both the Bus Comp and
// Channel Strip bindings (if any). The Nth-match per category is
// determined by bcInstanceIndex / csInstanceIndex — default 0 picks
// the first match, the Shift+Channel-Encoder cycle bumps it for the
// next render.
UC1Bindings lookupBindingsOnTrack(void* track /*MediaTrack**/);

// Lookup by raw FX name (substring). Exposed for tests.
const PluginBindings* lookupBindingsByName(std::string_view fxName);

// Resolve the VST3 param that a UC1-schematic control drives on `b`. The
// schematic identifies controls by SSL-Link `linkIdx` + domain; this joins
// that to the hardware knob/button ID (kCsLinkToUc1 / kBcLinkToUc1) and reads
// the binding's knobParam[] / buttonParam[]. `busComp` picks the table,
// `isButton` the knob-vs-button column. Returns kParamNone when the control
// isn't reachable from UC1 or the binding leaves it unmapped. When non-null,
// *outInverted receives the per-control invert flag. Read-only, FX-name based —
// used by the Learn-HUD publisher independent of the Settings editor.
int hudParamForControl(const PluginBindings* b, bool busComp,
                       int linkIdx, bool isButton, bool* outInverted);

// True when the SSL-Link `linkIdx` carries its OWN overlay on the FX-Learn
// layer that `b` represents (b = a user-map layer binding from
// lookupBindingsByName). Normal layer → always true. A modifier layer
// (Option/Control) → true only when the control was explicitly FX-Learned on
// that layer, false when it merely inherits the Normal mapping. Built-in /
// unknown bindings have no layers → always true. Lets the Learn-HUD show a
// modifier layer with ONLY its real overlays (inherited controls render
// unmapped) instead of the full inherited set lighting up. Frank 2026-06-16.
bool hudControlExplicitOnLayer(const PluginBindings* b, int linkIdx);

// Reverse of kCsLinkToUc1 / kBcLinkToUc1: the SSL-Link `linkIdx` a given UC1
// hardware control drives. Used to focus the control's OWN slot when an
// Option/Control FX-Learn layer remaps it to a param outside the Normal slot
// list (so the focused-param readout still lands on the correct row).
// Returns -1 when the control isn't in the table.
int linkIdxForControl(uint8_t controlId, bool busComp, bool isButton);

// Semantic "kind" of the quantity a CS SSL-Link slot controls, derived from the
// UC1 control the linkIdx drives (kCSHfFreq → Freq, kCSHfGain → Db,
// kCSCompRatio → Ratio, kCSCompRelease → Time, kCSHmfQ → Q, …). Lets value
// transfer between different CS plug-ins treat each control by its real meaning
// instead of guessing from the display string. Generic = no clear musical kind.
enum class CsQuantityKind { Generic, Db, Freq, Time, Ratio, Q, Pct };
CsQuantityKind csQuantityKind(int linkIdx);

// True when `paramName` matches a known name alias for the UC1 knob that SSL-Link
// `linkIdx` drives (case-insensitive substring). Lets CS value transfer resolve a
// control on a plug-in that only NAMES the param differently ("High Pass Filter
// (Hz)") and never got an explicit FX-Learn mapping. Knob controls only; returns
// false for buttons / unknown linkIdx. Pure string logic — no REAPER API.
bool controlAliasMatch(int linkIdx, const char* paramName);

// True when this binding belongs to the Bus Comp domain (BC 2 / 4K B /
// user-mapped BC). False covers Channel Strip and unrelated bindings;
// callers should null-check the input first.
bool isBusCompBinding(const PluginBindings* b);

// Per-button push-cycle steps for a user FX-Learn binding, or nullptr when
// the button has none (built-in plug-ins always return nullptr → legacy
// auto-cycle). The returned vector is non-empty and owned by the binding
// cache; consume it on the same main thread, do not store it across a
// user_plugins generation bump. See UserLinkSlot::pushSteps.
const std::vector<uf8::PushStep>*
pushStepsForButton(const PluginBindings* channelMap, uint8_t buttonId);

// Kind of a control — helps the surface decide which plugin slot to
// route a knob to when both Bus Comp and Channel Strip are on the track.
enum class ControlDomain {
    BusComp,        // top V-Pots + Bus Comp IN button
    ChannelStrip,   // everything else on the UC1
};

// Classify a knob ID — the UC1's top V-Pots (0x0E..0x16) go to Bus Comp
// when present, else fall back to Channel Strip repurposing.
ControlDomain classifyKnob(uint8_t knobId);

// Classify a button — Bus Comp IN (0x0C) is the sole Bus Comp button,
// everything else belongs to Channel Strip.
ControlDomain classifyButton(uint8_t buttonId);

// ---- Multi-instance picker -----------------------------------------------
// A track may host more than one BC and/or CS plug-in (e.g. built-in
// BC 2 followed by a user-learned BC). Each domain has its own active
// instance index; default 0 picks the first match in track-FX order.
// State is GUID-keyed and in-memory only — reset per session, not
// persisted.
int  bcInstanceIndex(void* track);
int  csInstanceIndex(void* track);
void setBcInstanceIndex(void* track, int idx);
void setCsInstanceIndex(void* track, int idx);
int  bcInstanceCount(void* track);
int  csInstanceCount(void* track);
// UF8-only user maps (domain==None, uf8Mode==true) — same per-track
// active-instance plumbing so the Shift+Encoder instance cycle can
// step through them alongside CS/BC. UC1 doesn't render UF8-only
// maps; the index is consumed by main.cpp's userStripCtxFocused_ so
// the UF8 strips route to the picked instance.
int  uf8OnlyInstanceIndex(void* track);
void setUf8OnlyInstanceIndex(void* track, int idx);
int  uf8OnlyInstanceCount(void* track);
// Resolve a MediaTrack* to its project-stable GUID string. Empty string
// on invalid/dangling pointer. Used as the key for any per-track state
// that needs to survive project save/load — pointers do not, GUIDs do.
std::string trackGuid(void* track);
// Map an FX index on a track back to its instance position within the
// domain. Returns -1 when the FX isn't a recognised CS/BC binding.
// Used by chaseLastTouchedFx so a click in REAPER's plug-in GUI
// snaps the active instance to whichever copy the user just touched.
int instanceIndexForFx(void* track, int fxIdx);

// Inverse of instanceIndexForFx: resolve the FX index of the
// `ordinal`-th instance in a domain (bc==true → Bus Comp, false →
// Channel Strip), using the same per-domain predicate as the
// instance-count / cursor functions. Returns -1 when `ordinal` is out
// of range or `track` has no matches. Main-thread-only.
int fxIndexForInstance(void* track, bool bc, int ordinal);

// Notify hook fired whenever the active CS/BC instance cursor changes
// (setBcInstanceIndex / setCsInstanceIndex). main.cpp registers this so
// the on-screen Inserts-list marker can repaint for the affected track.
// Null by default (no-op). Fires on the calling thread — setters run on
// the main thread, so the callback may touch REAPER track APIs.
using InstanceChangedFn = void (*)(void* /*track*/);
void setInstanceChangedCallback(InstanceChangedFn fn);

} // namespace uc1
