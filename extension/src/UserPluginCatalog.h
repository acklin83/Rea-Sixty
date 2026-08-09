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

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "FocusedParam.h"   // Domain
#include "PluginMap.h"      // PluginMap (returned from resolved view)

namespace uf8 {

// Parameter polarity. Forward-declared near the top of the namespace so
// UserLinkSlot (below) can carry a member of this type — the full
// description still lives next to the UF8 V-Pot bank structs further
// down, where the original V-Pot semantic comments live.
enum class VPotPolarity : uint8_t {
    Unipolar = 0,
    Bipolar  = 1,
};

// One entry in a per-button push-cycle. `norm` is the target value in
// normalised param space [0..1]. `enabled` lets an option stay in the list
// (and keep its position / stay visible in the editor) while being skipped
// by the push-cycle — that's how "untick to exclude" works without losing
// the option. See UserLinkSlot::pushSteps.
struct PushStep {
    int   vst3Param = -1;
    float norm      = 0.0f;
    bool  enabled   = true;
};

// Advance through a PushStep list by `delta` logical steps (signed), skipping
// !enabled / unbound entries. CLAMPS at the ends — unlike the UC1 button
// push-cycle (which wraps), a rotary should stop at the last option rather
// than jump back to the first. `curIdx < 0` means "nothing matched the live
// state": start at the first enabled step when turning up, the last when
// turning down. Returns the resulting index, or -1 if no step is live.
inline int stepCycleAdvance(const std::vector<PushStep>& steps,
                            int curIdx, int delta) {
    const int n = static_cast<int>(steps.size());
    auto live = [&](int i) {
        return i >= 0 && i < n && steps[i].vst3Param >= 0 && steps[i].enabled;
    };
    int firstE = -1, lastE = -1;
    for (int i = 0; i < n; ++i) if (live(i)) { if (firstE < 0) firstE = i; lastE = i; }
    if (firstE < 0) return -1;
    if (curIdx < 0) return delta >= 0 ? firstE : lastE;
    if (delta == 0) return live(curIdx) ? curIdx : firstE;
    const int dir = delta > 0 ? 1 : -1;
    int idx = live(curIdx) ? curIdx : (dir > 0 ? firstE : lastE);
    for (int remaining = delta > 0 ? delta : -delta; remaining > 0; --remaining) {
        int probe = idx + dir;
        while (probe >= 0 && probe < n && !live(probe)) probe += dir;
        if (probe < 0 || probe >= n) break;   // clamp at the end
        idx = probe;
    }
    return idx;
}

// FX-Learn modifier layers. The Normal layer is the base, always-active
// mapping; Option / Control / Control+Option are held-modifier overrides chosen
// at runtime by reasixty_fxLearnActiveLayer(). Shift stays reserved for Fine
// mode and is NOT a layer. Index values matter: UserLinkSlot::modLayers stores
// Option at [0], Control at [1], ControlOption at [2] (i.e. FxLayer value minus
// 1). ControlOption = both modifiers held together (was previously treated as
// the neutral Normal state). Frank 2026-06-17.
enum FxLayer { Normal = 0, Option = 1, Control = 2, ControlOption = 3 };
constexpr int kNumFxLayers = 4;

// One layer's worth of a control→param mapping. The Normal layer of every
// UserLinkSlot IS a SlotLayer (via inheritance below); the Option/Control
// modifier layers are extra SlotLayer instances. Splitting these mutable
// fields out lets the same physical control carry an independent param +
// invert + knob-travel + push-cycle per held modifier.
struct SlotLayer {
    // VST3 parameter index on this user plugin. -1 = unmapped on this layer
    // (a modifier layer left at -1 with no pushSteps falls back to Normal at
    // runtime). The base/Normal layer uses -1 for a macro-only slot.
    int  vst3Param = -1;
    bool inverted = false;
    // Per-slot display override — shown on scribble strips instead of the
    // default name (VST3 param name or canonical SSL slot name). Empty
    // string means "use default".
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
    // Polarity — same semantic as UserUf8BankSlot::polarity (uses the
    // same VPotPolarity enum, declared just above the UF8 V-Pot bank
    // structs below). UC1 hardware has no native bipolar ring mode
    // (see pushKnobRing_ in UC1Surface), so this currently only drives
    // the curve-editor Log/Exp preset shape (mirror around 0.5 when
    // Bipolar). Useful symmetry with the UF8 V-Pot right-click menu so
    // the same param doesn't carry different polarity defaults across
    // surfaces.
    VPotPolarity polarity = VPotPolarity::Unipolar;
    // Push-reset target in normalised param space [0..1]. UC1 has no
    // push, so this is dormant on UC1; persisted for the editor UI's
    // parity with UF8 V-Pots and as a slot-level hint for future UF8-
    // mirroring paths that fall back to the slot when no dedicated UF8
    // V-Pot binding exists. Default 0.5 mirrors the UF8 V-Pot default.
    double defaultNorm = 0.5;

    // Per-button push-cycle step list (additive; empty => legacy
    // behaviour). For UC1 channel-strip discrete buttons (EQ Type, HF/LF
    // Bell, Fast Attack, Peak, Expand, Gate Attack) bound to a stepped
    // param, an EMPTY list means "auto-cycle through ALL of the bound
    // param's discrete options" (the shipped behaviour). A NON-EMPTY list
    // overrides that: each press advances to the next entry and writes
    // `vst3Param := norm`. This serves two cases uniformly:
    //   1. a curated SUBSET of one param's options (all entries share the
    //      same vst3Param), and
    //   2. a multi-param MACRO (entries reference different params) — used
    //      when a third-party plugin splits an SSL-style concept across
    //      several toggle params.
    // A macro slot may carry vst3Param == -1 (no primary binding) and rely
    // solely on pushSteps. Only the advanced-to step's param is written;
    // sibling params are left as-is.
    std::vector<PushStep> pushSteps;
};

struct UserLinkSlot : SlotLayer {
    int  linkIdx;     // SSL 360 Link virtual-strip slot. 0..46 + 100..119.
    // Modifier overlays: [0]=Option, [1]=Control. The base SlotLayer above
    // is the Normal layer. A modifier layer is "mapped" when it carries a
    // bound param or a push-cycle (see fxLayerMapped); an unmapped modifier
    // layer makes runtime fall back to the Normal layer for that control.
    SlotLayer modLayers[kNumFxLayers - 1];
};

// Reference to the SlotLayer backing FX-Learn layer L: 0=Normal returns the
// slot itself (the base), 1=Option -> modLayers[0], 2=Control -> modLayers[1],
// 3=ControlOption -> modLayers[2].
inline SlotLayer& fxLayerOf(UserLinkSlot& s, int L) {
    return (L <= FxLayer::Normal) ? static_cast<SlotLayer&>(s)
                                  : s.modLayers[L - 1];
}
inline const SlotLayer& fxLayerOf(const UserLinkSlot& s, int L) {
    return (L <= FxLayer::Normal) ? static_cast<const SlotLayer&>(s)
                                  : s.modLayers[L - 1];
}
// True when a layer carries a real mapping (a bound param or a push-cycle).
inline bool fxLayerMapped(const SlotLayer& sl) {
    return sl.vst3Param >= 0 || !sl.pushSteps.empty();
}
// The SlotLayer that actually drives a control under FX-Learn layer L: the
// requested layer if it carries a mapping, otherwise the Normal/base layer.
// This is the single source of the "modifier overlay falls back to Normal
// for controls you didn't remap" rule — used by both synthesis and dispatch.
inline const SlotLayer& fxEffectiveLayer(const UserLinkSlot& s, int L) {
    const SlotLayer& lay = fxLayerOf(s, L);
    return fxLayerMapped(lay) ? lay : fxLayerOf(s, FxLayer::Normal);
}
// True when a layer differs from a freshly-constructed default — i.e. it is
// worth serialising. Used to keep JSON diffs minimal.
inline bool fxLayerNonDefault(const SlotLayer& sl) {
    return fxLayerMapped(sl) || sl.inverted || !sl.customLabel.empty()
        || sl.rangeMin != 0.0f || sl.rangeMax != 1.0f
        || sl.sensitivity != 1.0f || !sl.curvePoints.empty()
        || sl.polarity != VPotPolarity::Unipolar || sl.defaultNorm != 0.5;
}
// True when slot s has ANY modifier-layer override populated — used to keep
// JSON diffs minimal (only emit modLayers when present).
inline bool hasAnyModLayer(const UserLinkSlot& s) {
    for (const auto& ml : s.modLayers)
        if (fxLayerNonDefault(ml)) return true;
    return false;
}

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
//
// Primitive-argument variants exist so non-UserLinkSlot bindings (e.g.
// UF8 user-map per-strip fader / V-Pot records) can apply the same
// math without forcing every binding type to inherit a common base.
float applyCurve(float t, float rangeMin, float rangeMax,
                 const std::vector<std::pair<float, float>>& curvePoints);
float inverseCurve(float v, float rangeMin, float rangeMax,
                   const std::vector<std::pair<float, float>>& curvePoints);
// Take SlotLayer (the base of UserLinkSlot) so both the Normal layer and the
// Option/Control modifier layers evaluate with the same math.
inline float applyCurve(const SlotLayer& sl, float t) {
    return applyCurve(t, sl.rangeMin, sl.rangeMax, sl.curvePoints);
}
inline float inverseCurve(const SlotLayer& sl, float v) {
    return inverseCurve(v, sl.rangeMin, sl.rangeMax, sl.curvePoints);
}

// Stepped-parameter helpers. REAPER reports per-step size in normalised
// [0..1] space via TrackFX_GetParameterStepSizes — we treat that as the
// authoritative grid. The encoder handlers use these to:
//   - tickStepped: convert raw detents into logical step ticks, honouring
//     the user's sensitivity (interpreted as detent speed). Default sens=1
//     emits one step per 2 detents (matches the existing UC1 baseline
//     in handleKnob_'s stepped branch).
//   - snapToStep: round any normalised value to the nearest step grid
//     point, used for Min/Max clamps and push-reset targets on stepped
//     params so they always land exactly on a valid step.
//   - numStepsFor: derive the discrete value count from pStep for editor
//     display ("Stepped parameter — N values").
struct SteppedTickResult {
    int   logicalSteps;   // signed; 0 = sub-threshold, caller skips write
    float newAccum;       // residual to persist on caller's accumulator
};
SteppedTickResult tickStepped(float accum, int rawDetents, float sensitivity);
float snapToStep(float v, float pStep);
int   numStepsFor(float pStep);

// JSFX sliders mis-report pStep=1.0 (full range) for what are really
// continuous params, so the encoder handlers drive them as continuous.
// The firmware applies its own velocity acceleration (≈1 detent on a slow
// turn … up to ~14 on a fast flick); passing that through linearly makes a
// fast flick overshoot ~20 % of range while slow turns crawl. This maps a
// signed raw-detent count to a SIGNED normalised step magnitude with a
// floor-lifted sub-linear curve: a slow detent gets a usable base step
// (~1/33 of range, vs the old sluggish 1/64) and fast flicks are capped
// near ~1/16 so no single event overshoots. Identical on UC1 + UF8 so both
// surfaces feel the same; the per-surface global speed + fine factor scale
// the result downstream. Returns 0 for a zero detent count.
double jsfxContinuousStep(int rawDetents);

// Grid-fine mode for long continuous JSFX sliders. The analog
// jsfxContinuousStep floor (~1/33 of range, quartered in Fine) is still
// dozens-to-hundreds of native increments on a slider like <20,20000,1> —
// too coarse to set cleanly. This instead steps by whole native slider
// increments: `normStep` is the slider's own quantum (rawStep/range, from
// jsfxStepClassify). A slow detent moves exactly ONE increment (the finest
// the plug-in supports); a fast flick accelerates to a few increments so a
// hard spin still covers ground without leaving Fine. Returns a SIGNED
// normalised delta, already quantised to whole increments. 0 for a zero
// detent count or a non-positive normStep (gridless slider).
double jsfxGridFineStep(int rawDetents, double normStep);

// JSFX sliders are often coarsely quantised, so a small fine-mode nudge
// (~0.008 normalised) rounds straight back to the stored value and the
// param never moves. This accumulates the intended move against a caller-
// owned per-control virtual position `wantN`: `curN` is the live value,
// `intendedNext` what this event computed. Returns the un-rounded target
// to write — REAPER rounds it for storage but the residual persists in
// `wantN`, so sub-quantum moves add up until they cross the grid. Re-syncs
// `wantN` to `curN` when they diverge past a few quanta (external edit, or
// first use) so the encoder never fights a stale virtual position.
double jsfxAccumulate(double& wantN, double curN, double intendedNext);

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
    Value     = 0,   // continuous; rotate scrubs, push resets to defaultNorm
    Toggle    = 1,   // binary; rotate ignored, push flips 0↔1
    StepCycle = 2,   // discrete; rotate steps through vpotSteps (signed,
                     // clamps at ends), push advances one step. Reuses the
                     // PushStep model from the UC1 button push-cycle so a
                     // rotary can cycle a curated subset / custom order of a
                     // plug-in BUTTON's options, or a multi-param macro.
};

// VPotPolarity is declared near the top of the namespace (forward-
// referenced by UserLinkSlot). Semantic: Unipolar = LED ring sweeps
// L→R, Log/Exp bend toward one end; Bipolar = LED ring renders
// centre-out, Log/Exp mirror around 0.5. The hardware encoder itself
// is polarity-agnostic — polarity only changes the LED ring direction
// (UF8 V-Pots; UC1 ring is L→R only) and the curve presets produced
// by the editor.

// Per-binding knob-travel customisation. Same semantic as UserLinkSlot's
// rangeMin/rangeMax/sensitivity/curvePoints fields (see applyCurve /
// inverseCurve), but lives directly on UF8 strip / V-Pot bindings so
// each (faderBank, vpotBank, strip) entry can carry its own curve.
// Defaults make the math byte-identical to the previous linear path.
struct KnobTravel {
    float rangeMin    = 0.0f;
    float rangeMax    = 1.0f;
    float sensitivity = 1.0f;
    std::vector<std::pair<float, float>> curvePoints;
    // Cheap "is this customised?" probe — drives tick / colour overlays
    // in the editor without poking each field.
    bool isCustom() const {
        return rangeMin != 0.0f
            || rangeMax != 1.0f
            || sensitivity != 1.0f
            || !curvePoints.empty();
    }
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
    KnobTravel   travel{};                    // per V-Pot range/curve
    VPotPolarity polarity = VPotPolarity::Unipolar;
    // Rotary step-cycle (VPotMode::StepCycle). Same PushStep model as the
    // UC1 button push-cycle (UserLinkSlot::pushSteps): an ordered list of
    // {vst3Param, norm, enabled} the V-Pot scrubs through. Empty unless the
    // slot is in StepCycle mode. A macro entry references a different param;
    // disabled entries stay in the list (editor "untick to exclude") but are
    // skipped at runtime.
    std::vector<PushStep> vpotSteps;
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
    // Fader knob travel was tried 2026-05-26 and reverted same day:
    // absolute-position + motor-feedback creates round-trip races
    // (fader jumps during user motion, snaps on release). V-Pot keeps
    // the feature — see UserUf8BankSlot::travel. If someone wants to
    // revisit, the fix needs touch-snapshot echo, not curve math.
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

// FX-Learn modifier layers (Option/Control) were removed from UF8 — they remain
// a UC1-only feature (Option on UF8 collided with fader Alt-drag, and the
// overlay coverage was partial). These identity shims keep the V-Pot/strip
// dispatch + render call sites unchanged; the layer argument is ignored and the
// Normal/base binding is always returned. Frank 2026-06-15.
inline const UserUf8BankSlot& fxEffectiveVpot(const UserUf8BankSlot& base, int) {
    return base;
}
inline const UserUf8StripBinding& fxEffectiveStrip(const UserUf8StripBinding& base, int) {
    return base;
}

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

// ---- UF1 plugin-mode mapping (Frank 2026-08-08) ---------------------------
// Without one of these the UF1 shows a learned plug-in by SEQUENTIALLY filling
// the UC1 `slots` (buttons -> soft-keys, knobs -> V-Pots, sorted by linkIdx,
// 4 per page — uf1LearnedStreamSlots_ in main.cpp). That stays the fallback.
// An EXPLICIT map lets a UC1+UF1 owner put params on the UF1 that have no room
// on the UC1, or that today only live on a held modifier overlay.
//
// DELIBERATELY layer-free: no `modLayers`. The whole point is permanent,
// hold-nothing access — the fallback path keeps following the held layer
// (uf1LearnedEffParam_), an explicit map does not. UF8 layers were removed for
// related reasons 2026-06-15; don't reintroduce the pattern here.
//
// SPARSE: `pos` is the flat position (page*4 + idx) of the stream the slot
// lives in, so mapping only V-Pot position 7 costs one entry, and a map with
// no UF1 layer costs nothing. Inherits SlotLayer, so every existing tuning
// mutator (range/curve/sensitivity/polarity/label/invert/pushSteps) applies.
// v14 — the fixed non-parameter actions a UF1 soft-key can carry. The p188
// built-in pages hardcode some of these (PLUG-IN on soft-key 4 of page 1, HQ
// MODE / A/B on page 2); this lets the user put them on any soft-key, or take
// them off. None = the slot behaves exactly as it did before: plug-in param /
// push-cycle, or the built-in table's own action for that position.
// NOT a general action binding by design (Frank 2026-08-09 chose the small
// version): these five are the ones the UF1's plug-in mode actually needs.
enum class Uf1SkSpecial : uint8_t {
    None = 0, HQ = 1, AB = 2, StripMode = 3, StripModeGui = 4,
};
struct UserUf1Slot : SlotLayer {
    int pos = -1;                 // flat stream position: page*4 + idx
    // Soft-keys only — a V-Pot has no use for these. Stored as the raw enum
    // value so the JSON stays a plain int.
    uint8_t special = 0;          // Uf1SkSpecial; 0 = plug-in param as before
    // Per-soft-key LED colour, 0xRRGGBB. 0 = no override → the HW-verified
    // state-only bytes (what every key did before). Soft-keys only: a UF1
    // V-Pot has no LED of its own, it is drawn on the screen (v12, Frank
    // 2026-08-09 "Soft-Key LEDs sollen pro Soft-Key einstellbar sein").
    uint32_t ledRgb = 0;
};
// Scope decided with Frank 2026-08-08: V-Pots + soft-keys ONLY. The
// above-fader V-Pot and the fader stay out — the fader collides with Sticky
// Pot and the motor/inverse-curve echo (the same reason UF8 faders are
// excluded from FX-Learn/HUD tuning).
struct UserUf1Map {
    std::vector<UserUf1Slot> vpots;      // 4 per page
    std::vector<UserUf1Slot> softKeys;   // 4 per page
};
constexpr int kUserUf1PerPage = 4;
// True when the UF1 layer carries anything at all — the "is there an explicit
// map?" test used by runtime, the v10->v11 migration and the save filter.
inline bool uf1MapHasContent(const UserUf1Map& u)
{
    // A soft-key carrying only a special action (v14) is content too — without
    // this it would be dropped by the save filter and never survive a restart.
    for (const auto& s : u.vpots)    if (s.vst3Param >= 0 || !s.pushSteps.empty()) return true;
    for (const auto& s : u.softKeys) if (s.vst3Param >= 0 || !s.pushSteps.empty()
                                       || s.special) return true;
    return false;
}
// The slot at flat position `pos` of a stream, or nullptr when unmapped.
inline const UserUf1Slot* uf1SlotAt(const std::vector<UserUf1Slot>& v, int pos)
{
    if (pos < 0) return nullptr;
    for (const auto& s : v) if (s.pos == pos) return &s;
    return nullptr;
}
// Pages needed to show every mapped UF1 position (both streams share the page
// cursor, exactly like the sequential fallback). At least 1.
inline int uf1MapPageCount(const UserUf1Map& u)
{
    int hi = -1;
    for (const auto& s : u.vpots)    if (s.vst3Param >= 0 && s.pos > hi) hi = s.pos;
    for (const auto& s : u.softKeys) if (s.vst3Param >= 0 && s.pos > hi) hi = s.pos;
    return (hi < 0) ? 1 : (hi / kUserUf1PerPage) + 1;
}

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

// One user-chosen display name for a PARAMETER, read by every surface (v13).
// Kept separate from UserParamInfo because that one is a SNAPSHOT of the
// plug-in (re-taken on rescan and therefore not a safe home for user data).
struct UserParamLabel {
    int          vst3Param = -1;
    std::string  label;
};

// Plugin "mode" is encoded by (domain, uf8Mode) per the new domain
// structure (2026-05-12):
//   domain=ChannelStrip, uf8Mode=false → CS only
//   domain=ChannelStrip, uf8Mode=true  → CS + UF8
//   domain=BusComp,      uf8Mode=false → BC only
//   domain=BusComp,      uf8Mode=true  → BC + UF8
//   domain=None,         uf8Mode=true  → UF8 only
//   domain=None,         uf8Mode=false → invalid (filtered at load/save)
// One user-curated EXT FUNCS slot (UC1 BACK-button drill-down menu, CS mode
// only). The hidden menu's contents are normally the fixed SSL list
// (kExtFuncs in UC1Surface.cpp); for a non-SSL user-mapped CS plug-in the
// user can instead fill these 10 slots (2×5 grid under the CS mockup) with
// any of the plug-in's params. `name` doubles as the LCD header (full) and
// the 3-slot carousel label (truncated to 14). `vst3Param == -1` = empty
// slot (skipped in the carousel). Decoupled from `slots` on purpose — an
// EXT FUNCS param need not be on any physical control.
struct ExtFuncEntry {
    std::string name;
    int         vst3Param = -1;
};
constexpr int kUserExtFuncsCount = 10;

struct UserPluginMap {
    std::string                match;          // substring of TrackFX_GetFXName
    Domain                     domain = Domain::None;
    bool                       uf8Mode = false;
    std::string                displayShort;   // 4-char zone label
    bool                       isDefault = false;
    std::vector<UserLinkSlot>  slots;
    UserMetering               metering;
    UserUf8Map                 uf8;            // optional UF8 strip-mode bindings
    // Optional EXPLICIT UF1 plugin-mode mapping. Empty = the UF1 keeps
    // sequentially filling from `slots` (the shipped behaviour), so a map
    // without this block behaves byte-identically. Frank 2026-08-08.
    UserUf1Map                 uf1;
    bool                       uf1Mode = false;
    // v15 — draw OUR EQ graph on the UF1 for this plug-in. The built-in SSL
    // strips get it automatically because uf1PaintEqGraph_ finds their params by
    // SSL's own names ("HF Gain", "HMF Freq", …); a learned CS names them
    // differently, so the curve stayed blank. With this on, the EQ params are
    // resolved through the map's UC1 link slots instead — the same canonical
    // names the UC1 has engraved (Frank 2026-08-09).
    bool                       uf1EqGraph = false;
    // ONE user name per PARAMETER, shared by UC1, UF8 and UF1 (v13). The old
    // per-slot customLabel still wins where it is set (a deliberate per-control
    // override), but a name typed into any surface's "Display name" field lands
    // here, so it shows up on all three instead of drifting apart — the three
    // surfaces used to keep three independent names for the same parameter
    // (SlotLayer::customLabel, UserUf8BankSlot::label, UserUf1Slot::customLabel).
    // Sparse and additive: absent = no user name, and a map without one
    // serialises exactly as it did in v12. Frank 2026-08-09.
    std::vector<UserParamLabel> paramLabels;
    std::vector<UserParamInfo> paramSnapshot;  // last-seen VST3 param list
    int64_t                    snapshotTakenAt = 0;  // unix-sec; 0 = never
    // Per-domain slot caches (Frank 2026-05-15). When the user toggles
    // primary mode CS ↔ BC the slot list gets swapped onto the matching
    // cache rather than wiped, so flipping back and forth preserves
    // both sets. Empty until the user has built bindings in that
    // domain at least once.
    std::vector<UserLinkSlot>  csSlotCache;
    std::vector<UserLinkSlot>  bcSlotCache;
    // User-curated UC1 EXT FUNCS list (v8). CS-mode only; empty = the hidden
    // menu shows nothing for this plug-in (SSL built-ins keep their fixed
    // list, handled entirely in UC1Surface — they never reach this struct).
    std::array<ExtFuncEntry, kUserExtFuncsCount> extFuncs{};
    // UC1 POL button behaviour. Default true = the Polarity button toggles
    // REAPER's per-track phase (B_PHASE), as it always has. When false the
    // button instead drives whatever param is FX-Learned onto the Polarity
    // slot (linkIdx 5), like any other CS button. Additive — old files
    // default to true. Frank 2026-06-02.
    bool useReaperTrackPolarity = true;
    // Full plug-in factory name — REAPER's `original_name` — captured live
    // when the FX-Learn view follows the focused FX. `match` is a user-editable
    // substring and carries no manufacturer; this string does ("Name (Vendor)"
    // for VST/CLAP, "Vendor: Name" for AU), which is what lets the mapping
    // exchange resolve the vendor and the plug-in identity server-side.
    // Additive — empty until captured; the exchange falls back to `match`.
    // Frank 2026-07-19.
    std::string originalName;
    // Count of the plug-in's FUNCTIONAL parameters at snapshot time: total
    // params minus REAPER's MIDI-learn junk (2080 on some UADx plug-ins),
    // minus the :wet/:bypass/:delta wrapper params, minus anything the plug-in
    // marks non-automatable. This is the denominator for the exchange's
    // "parameter coverage" (how much of the plug-in the map controls), which
    // the pruned paramSnapshot can't provide. -1 = not captured. Frank 2026-07-20.
    int functionalParamCount = -1;
};

struct UserPluginCatalog {
    int                         formatVersion = 1;
    std::vector<UserPluginMap>  maps;
    // Quick-Learn skip list (v7). Match substrings the user explicitly
    // chose NOT to learn during a Quick-Learn project sweep. The sweep's
    // "next unlearned FX" resolver skips any FX whose identity name
    // contains one of these substrings, so junk / utility / analyzer
    // plug-ins stay out of the way across sessions. Independent of `maps`
    // — a skipped plug-in is neither learned nor offered.
    std::vector<std::string>    skipMatches;
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
// v7 (2026-05-28): added top-level `skipped_matches` array (Quick-Learn
// skip list). v6 readers seeing a v7 file ignore the array; v6 files load
// in v7 readers with an empty skip list (no behaviour change).
// v8 (2026-06-01): added `extFuncs` on UserPluginMap (user-curated UC1
// EXT FUNCS list, CS mode). v7 readers seeing a v8 file ignore the field;
// v8 readers seeing a v7 file load with an empty list (no behaviour change).
constexpr int kCurrentFormatVersion = 15;  // v15: + "uf1EqGraph" — draw our EQ graph on the UF1 for a LEARNED CS (params resolved via the UC1 link slots), emitted only when set. v14: + UF1 soft-key "special" (fixed non-parameter action: HQ / A/B / Strip Mode / Strip Mode+GUI), emitted only when set. v13: + "paramLabels" — ONE user display name per parameter, read by all three surfaces (emitted only when non-empty). v12: + per-soft-key UF1 LED colour (uf1 slot "ledRgb"). v11: + explicit UF1 plugin-mode map (uf1{vpots,softKeys} + uf1Mode). Older files load byte-identical.

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

// ----- Single-map sharing (.rea60map) --------------------------------------
// One map plus the metadata the mapping exchange needs, so a file exported
// today is still a valid upload later. Deliberately NOT a setup bundle: a
// .rea60config embeds bindings.json, which can carry REAPER action IDs and
// keyboard macros, so importing one runs whatever the author put in it. A
// plugin map is pure data — param indices, labels, curves, colours — which is
// what makes it safe to pass between strangers. See docs/mapping-exchange-plan.md.
struct MapShare {
    UserPluginMap map;
    std::string   vendor;       // not derivable from `match`; user-entered
    std::string   author;       // free text until the exchange has accounts
    std::string   description;  // optional, a line or two
    std::string   licence;      // SPDX id; "CC0-1.0" for exchange uploads
    int64_t       createdAt = 0;  // unix seconds, 0 = unknown
};

// Surface scope a map applies to, derived from (domain, uf8Mode):
// "uc1", "uf8", "uc1+uf8". Empty string for the invalid combination
// (domain=None, uf8Mode=false), which the catalog filters at load/save.
const char* surfaceScope(const UserPluginMap& m);

// Write ONE map as a .rea60map envelope. `paramSnapshot` is pruned to the
// params the map actually binds — an unpruned snapshot runs to thousands of
// entries on plug-ins like the UADx range, which is dead weight in a file
// meant to be passed around. `isDefault` is forced off: a shared map must not
// claim the recipient's Default slot.
bool exportMapToFile(const std::string& path, const MapShare& share,
                     std::string* errOut);

// Build the .rea60map envelope into `out` without writing a file — used by the
// in-app "Publish to exchange" upload, which POSTs the bytes. Same pruning and
// isDefault handling as exportMapToFile (which now delegates here).
bool serializeMapShare(const MapShare& share, std::string& out,
                       std::string* errOut);

// Read a .rea60map WITHOUT applying it — the caller decides what to do when
// the match already exists (upsert overwrites wholesale) or collides with a
// built-in (which would make save() refuse the WHOLE catalog). Returns false
// with a short reason in `*errOut` on any malformed input.
bool importMapFromFile(const std::string& path, MapShare& out,
                       std::string* errOut);

// Same as importMapFromFile but parses bytes already in memory — used by the
// in-app Exchange download, which has the .rea60map from the API, not a file.
bool importMapFromString(const std::string& contents, MapShare& out,
                         std::string* errOut);

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

// Record the live plug-in factory name (`original_name`) onto an existing
// map, keyed by `match`. Called when the FX-Learn view follows a focused
// live FX that resolves to an owned map — that is the one moment the full
// name (which carries the vendor) is available; `match` alone never is.
// Persists (save()) ONLY when the value actually changes, so the
// focus-follow hot path costs nothing after the first capture. No-op when
// the match is not in the catalog or `originalName` is empty. Returns true
// when it changed and was saved.
bool captureOriginalName(std::string_view match, std::string_view originalName);

// Sibling for the v3 functional-param count (parameter coverage). Same persist-
// on-change contract; no-op when the match is absent or `count` < 0. Called from
// the Share-time FX scan so a re-Share records the count for maps learned before
// v3 or never re-snapshotted this session.
bool captureFunctionalParamCount(std::string_view match, int count);

// Seed an EMPTY UF1 map from the UC1 slots so enabling the layer changes nothing
// visible (the sequential fallback's layout becomes editable). No-op when the
// UF1 map already carries something.
void seedUf1FromSlots(UserPluginMap& m);
// Enable + seed the UF1 layer for `match`. True when the catalog changed.
bool enableUf1Layer(std::string_view match);

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

// v13 — ONE user name per parameter, read by UC1, UF8 and UF1 alike.
// paramLabelFor returns "" when the plug-in has no map or the param no name.
// setParamLabel writes into a map the caller then upserts + saves; an empty
// label clears the entry. Returns true when something actually changed.
std::string paramLabelFor(std::string_view fxName, int vst3Param);
bool        setParamLabel(UserPluginMap& m, int vst3Param, std::string_view label);

// Return true iff `match` would also be matched by any built-in
// PluginMap's `match` substring (or vice versa). Used by the editor to
// warn the user before they save a name that built-ins claim.
bool collidesWithBuiltin(std::string_view match);

// Monotonic counter incremented on every mutating call (setAll, upsert,
// removeByMatch). Downstream caches (e.g. UC1's synthesized PluginBindings
// for user maps) compare against a stored snapshot to detect changes
// without having to walk the catalog content for diffs.
int generation();

// ----- Quick-Learn skip list (v7) ------------------------------------------
// Add `match` to the skip list (no-op if already present). Stages in
// memory like the other mutators — call save() to persist. Bumps
// generation().
void addSkip(std::string match);

// Remove `match` from the skip list (exact-string match against a stored
// entry). Returns true if an entry was removed. Bumps generation().
bool removeSkip(std::string_view match);

// True iff `fxName` contains any skip-list entry as a substring — same
// first-hit substring rule as lookupOwnedByName. Used by the Quick-Learn
// sweep to skip plug-ins the user opted out of.
bool isSkipped(std::string_view fxName);

// Read-only view of the skip list (for the management UI). Lifetime:
// until the next mutation.
const std::vector<std::string>& skips();

} // namespace user_plugins
} // namespace uf8
