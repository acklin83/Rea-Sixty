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
};

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

// 8 banks × 8 slots. Frank 2026-05-13: UF8 Plugin Mode now uses the
// 8 TopSoftKey cells as bank selectors (was the 6 V-POT/Soft 1-5
// Sub-Bank cells, which become no-op in Plugin Mode). Bank index
// 0..7 = TopSoftKey 1..8. The FX-Learn editor's bank combo follows.
// Older configs (6-bank serialised) load with banks 6+7 empty.
struct UserUf8BankSet {
    UserUf8BankSlot banks[8][8] = {};
};

// Bank count constant — surfaces in header so editor + dispatch
// can share a single source of truth.
constexpr int kUserUf8BankCount = 8;

// Per-bank, per-strip bindings (Fader / Solo / Cut / Sel). Frank 2026-05-16:
// the soft-key bank now switches the FULL strip context, not just V-POT —
// so fader/solo/cut/sel are also bank-aware (was a single array shared
// across all 8 banks pre-v6).
struct UserUf8StripBinding {
    int  faderVst3Param = -1;                 // -1 = fall through to track vol
    bool faderInverted  = false;
    int  soloVst3Param  = -1;                 // -1 = track solo
    int  cutVst3Param   = -1;                 // -1 = track mute
    int  selVst3Param   = -1;                 // -1 = track select
    // Per-LED colour overrides (0xRRGGBB; 0 = class default — yellow Solo,
    // red Cut, white Sel / track colour).
    uint32_t soloColour = 0;
    uint32_t cutColour  = 0;
    uint32_t selColour  = 0;
};

// Bank Left / Bank Right buttons in FX Learn (Frank 2026-05-16:
// "Bank buttons ins Mockup rein, default action 'Bank +/- 8',
// aber frei auf plugin params belegbar"). Single global binding (NOT
// per-bank — Bank L/R navigate banks; making them per-bank would loop).
// vst3Param == -1 → button keeps its default behaviour (bank_left /
// bank_right builtin = ±8-strip scroll). Otherwise the press toggles
// the bound VST3 param on the focused user-plug-in instance.
struct UserUf8NavBinding {
    int      vst3Param = -1;
    uint32_t colour    = 0;  // 0 = class default (white)
};

struct UserUf8Map {
    UserUf8BankSet       banks;
    // 8 banks × 8 strips. Older v5 catalogs serialised a flat strips[8];
    // load migration copies that single row into all 8 banks so existing
    // user maps behave identically on first reload after the v6 bump.
    UserUf8StripBinding  strips[kUserUf8BankCount][8] = {};
    // Per-bank TopSoftKey LED appearance (Plugin Mode). Index = bank
    // index 0..7; matches TopSoftKey position 1..8 on the hardware.
    UserUf8TopSoftKeyLed topSoftKeyLeds[kUserUf8BankCount] = {};
    UserUf8NavBinding    bankLeft;
    UserUf8NavBinding    bankRight;
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
