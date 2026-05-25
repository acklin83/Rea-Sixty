#pragma once
//
// UserPluginCatalog — runtime catalogue of user-learned plugin maps.
//
// Built-in PluginMaps (CS 2 / 4K B/E/G / BC 2 / SSL 360 Link variants) live
// in PluginMap.cpp's compile-time `kMaps[]` registry. UserPluginMaps live
// here, persisted to <REAPER_RESOURCE>/rea_sixty/user_plugins.json. The
// two registries combine in a two-stage lookup: built-ins first, user maps
// second (see lookupPluginMapByName in PluginMap.cpp).
//
// Phase 2.5d-A Step 1 — data layer + JSON I/O only. No UI, no FX-Learn
// dispatch yet. See docs/plan-fx-learn-and-multi-instance.md for the full
// design.
//

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "FocusedParam.h"   // Domain
#include "PluginMap.h"      // PluginMap (returned from resolved view)

namespace uf8 {

struct UserLinkSlot {
    int  linkIdx;     // SSL 360 Link virtual-strip slot. 0..46 + 100..119.
    int  vst3Param;   // VST3 parameter index on this user plugin.
    bool inverted = false;
    // Per-slot display override — shown on scribble strips instead of the
    // default name (VST3 param name or canonical SSL slot name). Empty
    // string means "use default". Additive field — old readers silently
    // ignore it, no format-version bump needed.
    std::string customLabel;

    // Knob-travel customisation (additive; defaults are byte-identical
    // to the pre-feature behaviour). All values are in normalised param
    // space [0..1]. `sensitivity` scales the raw encoder delta before
    // clamp/curve so coarse-stepped params (compressor ratios, stepped
    // sidechain freqs) can be slowed down per-slot. `curvePoints` carries
    // intermediate (x,y) breakpoints for a piecewise-linear knob-travel
    // curve; endpoints are implicit at (0,rangeMin) and (1,rangeMax).
    // Empty curvePoints => pure linear with range.
    float rangeMin    = 0.0f;
    float rangeMax    = 1.0f;
    float sensitivity = 1.0f;
    std::vector<std::pair<float, float>> curvePoints;
};

// Knob-travel evaluators. `applyCurve(sl, t)` maps the encoder's
// virtual position t∈[0..1] to a normalised FX param value v∈[0..1],
// honouring rangeMin/rangeMax and any user-defined curve breakpoints.
// `inverseCurve(sl, v)` is the inverse — given the current FX param
// value, recover t so deltas can be applied in encoder-space and then
// re-mapped. Both are O(curvePoints.size()) — typical use is ≤10
// points so cost is negligible.
//
// Defaults (rangeMin=0, rangeMax=1, sensitivity=1, no curve) make
// applyCurve(t)=t and inverseCurve(v)=v — byte-identical to the
// pre-feature linear path.
//
// `sensitivity` is applied OUTSIDE these helpers — at the encoder-
// delta site, before clamp. Keeping it out of the curve math makes
// the inverse exact.
float applyCurve(const UserLinkSlot& sl, float t);
float inverseCurve(const UserLinkSlot& sl, float v);

struct UserMetering {
    // Set vst3Param ≥ 0 to enable; -1 means "not learned" (fall back to
    // REAPER's GainReduction_dB named-config-parm).
    int    grVst3Param = -1;
    // Pre-abs additive shift for plug-ins whose meter reads negative-going
    // dB or sits at a non-zero floor at rest. Applied before |abs|.
    double grOffsetDb  = 0.0;
    // Per-breakpoint correction tables — applied AFTER |abs|, piecewise
    // linear between sample points. Index aligns with the bp constants
    // declared in GrCalibration.h. Default 0.0 ⇒ identity. The two
    // tables are independent because the BC VU motor (continuous needle
    // at 0/4/8/12/16/20 dB ticks) and the DYN GR LED strip (quantised at
    // 3/6/10/14/20 dB SSL-plugin segment boundaries) have different
    // native scales.
    double grBcVuCalDb[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    double grLedsCalDb[5] = {0.0, 0.0, 0.0, 0.0, 0.0};
};

// V-Pot push behaviour for a UF8 bank slot. User chooses since user plug-in
// params don't carry SSL-style step-size hints we trust.
enum class VPotMode : uint8_t {
    Value  = 0,   // continuous; rotate scrubs, push resets to defaultNorm
    Toggle = 1,   // binary; rotate ignored, push flips 0↔1
};

// One slot in one of eight UF8 banks. vst3Param=-1 => empty slot
// (top-soft-key blank, V-Pot no-op).
struct UserUf8BankSlot {
    int          vst3Param   = -1;
    std::string  label;                       // top-soft-key label
    VPotMode     vpotMode    = VPotMode::Value;
    bool         inverted    = false;
    double       defaultNorm = 0.5;           // V-Pot push reset (Value mode)
    // LCD colour bar override (Frank 2026-05-13: "Farbe für Farbbalken
    // auf UF8 Display"). 0xRRGGBB picked from the SSL DAW-Colour
    // palette; 0 = "no override" (falls back to bank-track colour).
    // Per (bank, strip) — each strip's bar can carry its own colour
    // within a given bank. Was named `colour` and shared by V-Pot ring
    // + TopSoftKey LED + strip bar; the V-Pot/Soft-Key sharing was
    // removed when Frank split those into independent registers.
    uint32_t     stripColour = 0;
};

// TopSoftKey LED appearance — bank-scoped (Frank 2026-05-13:
// TopSoftKey N in Plugin Mode is bank-N's selector). One entry per
// bank. Single colour + a fixed-label string for the bank. Active
// state (bank == g_softKeyBank) always renders Bright, inactive
// always Dim — no separate per-state colour or brightness; Frank
// 2026-05-13: "nicht separat für inactive/active... nur eine farbe.
// active immer bright, inactive immer dimm".
struct UserUf8TopSoftKeyLed {
    uint32_t     colour = 0xFFFFFFu;   // white
    std::string  label;                // shown on TopSoftKey LCD;
                                        // bank-scoped so bank switches
                                        // don't rewrite the row.
};

// Bank count constants. Two orthogonal bank dimensions:
//   * V-Pot bank (8) — selected by Top-Soft-Keys 1..8 in UF8 Plugin
//     Mode. Switches which Layer of V-Pot params (Pan / EQ / Comp …)
//     the 8 physical V-Pots drive. State: g_softKeyBank.
//   * Fader bank (2) — selected by Bank ←/→ buttons in UF8 Plugin
//     Mode. Switches WHICH 8 of up-to-16 logical strips the 8
//     physical strips represent. Lets the user map plug-ins with 16
//     channels (e.g. SSL Sigma remote) onto a single UF8. State:
//     g_uf8FaderBank (Frank 2026-05-17).
constexpr int kUserUf8VpotBankCount  = 8;
constexpr int kUserUf8FaderBankCount = 2;
// Back-compat alias for callers that still refer to the old name. New
// code should use kUserUf8VpotBankCount.
constexpr int kUserUf8BankCount      = kUserUf8VpotBankCount;

// V-Pot params: 2 fader-banks × 8 V-Pot-banks × 8 strips. The Top-
// Soft-Key chooses the V-Pot layer (vpotBank), Bank ←/→ chooses which
// 8 of the 16 logical strips are surfaced. Older configs (v6 with
// banks[8][8]) migrate into faderBank=0; v5's flat strips[8] migrates
// into all (faderBank, vpotBank) pairs.
struct UserUf8BankSet {
    UserUf8BankSlot banks[kUserUf8FaderBankCount]
                         [kUserUf8VpotBankCount]
                         [8] = {};
};

// Per-fader-bank, per-strip bindings (Fader / Solo / Cut / Sel). Frank
// 2026-05-17: fader/solo/cut/sel do NOT vary with the Top-Soft-Key
// V-Pot layer — Top-Soft-Key only changes what the V-Pots drive, not
// which physical fader you're touching. They DO vary with the
// fader-bank so 16-channel plug-ins (e.g. SSL Sigma remote) can map
// each channel's fader independently. (v6 had strips[topSoftKey][slot]
// — that per-top-soft-key dimension is dropped, migration takes
// strips[0][slot] only.)
struct UserUf8StripBinding {
    int          faderVst3Param = -1;         // -1 = fall through to track vol
    bool         faderInverted  = false;
    std::string  faderLabel;                  // scribble-strip override for
                                              // the fader's bound param (1..7
                                              // chars). Empty = use the
                                              // plug-in's own param name.
    int  soloVst3Param  = -1;                 // -1 = track solo
    int  cutVst3Param   = -1;                 // -1 = track mute
    int  selVst3Param   = -1;                 // -1 = track select
    // Per-LED colour overrides (0xRRGGBB; 0 = class default — yellow Solo,
    // red Cut, white Sel / track colour).
    uint32_t soloColour = 0;
    uint32_t cutColour  = 0;
    uint32_t selColour  = 0;
    // Per-LED "Reverse" toggle. When true, the LED on/off state is XORed
    // before rendering so a plug-in whose Cut param reads 1 = inactive
    // (and 0 = active) still produces the conventional bright-when-active
    // behaviour. No effect for buttons falling through to track-state
    // (Vst3Param == -1).
    bool soloInvert = false;
    bool cutInvert  = false;
    bool selInvert  = false;
};

// Bank Left / Bank Right buttons are reserved for fader-bank switching
// inside UF8 Plugin Mode (Frank 2026-05-17 reversal of the 2026-05-16
// "buttons frei auf plugin params belegbar" — that VST3-param override
// is dropped). Outside UF8 Plugin Mode the buttons remain bindable
// through the regular Bindings system (default = ±8-strip scroll).

struct UserUf8Map {
    UserUf8BankSet       banks;
    // 2 fader-banks × 8 strips. v6 had strips[8 topSoftKey][8 slots]
    // — that dimension is dropped (see UserUf8StripBinding comment).
    // Older v5 catalogs serialised a flat strips[8]; migration copies
    // that single row into faderBank=0.
    UserUf8StripBinding  strips[kUserUf8FaderBankCount][8] = {};
    // Per-bank TopSoftKey LED appearance (Plugin Mode). Index = V-Pot
    // bank 0..7; matches TopSoftKey position 1..8 on the hardware.
    // Not faderBank-scoped — the Top-Soft-Key row layout is the same
    // regardless of which 8-of-16 logical strips are surfaced.
    UserUf8TopSoftKeyLed topSoftKeyLeds[kUserUf8VpotBankCount] = {};
};

// Snapshot of one VST3 parameter on the learned plug-in. Captured when an
// instance is present so the editor can offer the param list (V-Pot picker,
// GR-meter picker, listening fallback) even on sessions where the plug-in
// hasn't been instantiated. `name` is the live name at snapshot time;
// `defaultNorm` lives in [0..1]; `wasEnum` is a hint from
// TrackFX_GetParameterStepSizes (any non-zero step ⇒ likely a toggle/enum).
struct UserParamInfo {
    int          vst3Param   = -1;
    std::string  name;
    double       defaultNorm = 0.5;
    bool         wasEnum     = false;
};

// Plugin "mode" is encoded by (domain, uf8Mode) per the new domain
// structure (2026-05-12):
//   domain=ChannelStrip, uf8Mode=false → CS only
//   domain=ChannelStrip, uf8Mode=true  → CS + UF8
//   domain=BusComp,      uf8Mode=false → BC only
//   domain=BusComp,      uf8Mode=true  → BC + UF8
//   domain=None,         uf8Mode=true  → UF8 only
//   domain=None,         uf8Mode=false → invalid (filtered at load/save)
struct UserPluginMap {
    std::string                match;          // substring of TrackFX_GetFXName
    Domain                     domain = Domain::None;
    bool                       uf8Mode = false;
    std::string                displayShort;   // 4-char zone label
    bool                       isDefault = false;
    std::vector<UserLinkSlot>  slots;
    UserMetering               metering;
    UserUf8Map                 uf8;            // optional UF8 strip-mode bindings
    std::vector<UserParamInfo> paramSnapshot;  // last-seen VST3 param list
    int64_t                    snapshotTakenAt = 0;  // unix-sec; 0 = never
    // Per-domain slot caches (Frank 2026-05-15). When the user toggles
    // primary mode CS ↔ BC the slot list gets swapped onto the matching
    // cache rather than wiped, so flipping back and forth preserves
    // both sets. Empty until the user has built bindings in that
    // domain at least once.
    std::vector<UserLinkSlot>  csSlotCache;
    std::vector<UserLinkSlot>  bcSlotCache;
};

struct UserPluginCatalog {
    int                         formatVersion = 1;
    std::vector<UserPluginMap>  maps;
};

namespace user_plugins {

// Current on-disk schema version. Bump when introducing breaking changes.
// v2 (2026-05-08): added optional `uf8` block on UserPluginMap (UF8 strip-mode
// bindings). v1 readers seeing a v2 file with no `uf8` block load identically.
// v3 (2026-05-09): added `colour` on bank slots and per-strip Solo/Cut/Sel.
// Missing fields parse as 0 (= class default), so v3 files load fine in v2
// readers (they just ignore the colours), and v2 files load in v3 readers.
// v4 (2026-05-12): added `uf8Mode` (decoupled UF8 layer from CS/BC domain;
// enables UF8-only maps) and `paramSnapshot` (persisted param-name list so
// the editor stays usable without a live FX instance). v3 readers seeing a
// v4 file silently drop the new fields; v3 files load in v4 readers with
// uf8Mode derived from "is the uf8 block non-empty".
// v5 (2026-05-15): added `grBcVuCalDb[6]` and `grLedsCalDb[5]` on
// UserMetering — per-breakpoint correction tables for the BC VU motor
// (ticks 0/4/8/12/16/20 dB) and the DYN GR LEDs / UF8 GR row (segment
// boundaries 3/6/10/14/20 dB). v4 readers seeing v5 files drop the
// arrays (no calibration); v4 files load in v5 with arrays = all zeros
// (identity, no behaviour change).
// v6 (2026-05-16): strips[8] became strips[8][8] (per-bank × per-strip).
// On disk the new field is `stripsByBank` (2D array, 8 banks × 8 strips).
// v5 readers seeing v6 files drop the new field (revert to track default
// for fader/solo/cut/sel); v5 files load in v6 readers by replicating
// the single `strips` row into all 8 banks (behaviour preserved).
constexpr int kCurrentFormatVersion = 6;

// Result of a save attempt. `Collision` means at least one map's `match`
// would also hit a built-in plugin's match string — the save is refused
// to keep built-ins unshadowable. `IoError` covers fopen/fwrite/rename
// failures.
enum class SaveResult {
    Ok,
    Collision,
    IoError,
};

// Initialise from <REAPER_RESOURCE>/rea_sixty/user_plugins.json. Missing
// file is not an error (catalog stays empty). Parse errors leave the
// catalog empty and emit a single line to /tmp/rea_sixty.log.
void load();

// Atomic write: serialise to <path>.tmp, fsync optional, rename onto
// <path>. Pre-write backup to <path>.bak if the destination exists.
// Returns Collision (and skips writing) if any map's `match` substring
// would resolve to a built-in PluginMap.
SaveResult save();

// Export the current in-memory catalog to an arbitrary path (file
// dialog target). Same on-disk format as user_plugins.json. Returns
// true on success; on failure fills `*errOut` (if non-null) with a
// short message. Skipped catalogs go out as `{}` so re-import yields
// an empty list.
bool exportToFile(const std::string& path, std::string* errOut);

// Import a user_plugins.json from an arbitrary path. Parses first,
// then atomically REPLACES the in-memory catalog AND the on-disk
// copy at configPath_(). Returns false if parse fails or write
// fails — the old state stays intact in that case. `*errOut` (if
// non-null) carries a short reason.
bool importFromFile(const std::string& path, std::string* errOut);

// Absolute path where user_plugins.json is persisted.
std::string configPath();

// Read-only access to the in-memory catalog. Pointers are stable until
// the next mutating call.
const UserPluginCatalog& get();

// Mutators — replace the whole list, or add/remove/update one map. They
// stage changes in memory; call save() to persist. The collision check
// runs in save(), not here, so the editor UI can stage a partial state.
void setAll(UserPluginCatalog c);
void upsert(UserPluginMap m);              // matches by `match` field
bool removeByMatch(std::string_view match);

// Lookup by match-substring on an FX name. Mirrors built-in lookup
// semantics — first hit on substring wins. Returns a synthesised
// PluginMap view (lifetime: until the next mutation). Slot list and
// match string are stored inside the catalog; the returned PluginMap
// has `slots` pointing into it. Returns nullptr if no user map matches.
const PluginMap* lookupByName(std::string_view fxName);

// Same matching rule as lookupByName, but returns a pointer to the
// owned UserPluginMap (not the synthesised PluginMap view) so callers
// can read the uf8.* fields directly. Lifetime: until next mutation.
const UserPluginMap* lookupOwnedByName(std::string_view fxName);

// Lookup a single UserLinkSlot by (fxName, linkIdx). Walks the same
// match rule as lookupOwnedByName, then searches the resolved map's
// `slots` for one with `linkIdx == idx`. Returns nullptr when no map
// matches or no slot in the map has the requested linkIdx — at which
// point callers fall back to the default linear encoder path. Used at
// the V-Pot delta site to fetch knob-travel customisation (range,
// sensitivity, curve points) for user-learned FX params.
const UserLinkSlot* lookupOwnedSlot(std::string_view fxName, int linkIdx);

// Return true iff `match` would also be matched by any built-in
// PluginMap's `match` substring (or vice versa). Used by the editor to
// warn the user before they save a name that built-ins claim.
bool collidesWithBuiltin(std::string_view match);

// Monotonic counter incremented on every mutating call (setAll, upsert,
// removeByMatch). Downstream caches (e.g. UC1's synthesized PluginBindings
// for user maps) compare against a stored snapshot to detect changes
// without having to walk the catalog content for diffs.
int generation();

} // namespace user_plugins
} // namespace uf8
