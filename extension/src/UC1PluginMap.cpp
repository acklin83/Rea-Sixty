#include "UC1PluginMap.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "reaper_plugin_functions.h"

#include "FocusedParam.h"        // uf8::Domain
#include "PluginMap.h"           // uf8::fxIdentityName
#include "UserPluginCatalog.h"   // uf8::user_plugins

// Active FX-Learn modifier layer (0=Normal,1=Option,2=Control), resolved from
// held keyboard modifiers once per onTimer tick. Defined in main.cpp.
int reasixty_fxLearnActiveLayer();

namespace uc1 {

namespace {

// Helper to build a PluginBindings initialiser with most entries defaulted
// to kParamNone. We only want to spell out the knobs/buttons the plugin
// actually supports.
constexpr PluginBindings makeEmpty(const char* match, const char* shortName)
{
    PluginBindings b{};
    b.match     = match;
    b.shortName = shortName;
    for (auto& v : b.knobParam)    v = kParamNone;
    for (auto& v : b.buttonParam)  v = kParamNone;
    for (auto& v : b.inverted)     v = false;
    b.bypassParam = kParamNone;
    return b;
}

// ---- Bus Compressor 2 ------------------------------------------------------
//
// VST3 param indices from the existing PluginMap's kBusComp2Slots.
// Only the top-center V-Pots and the Bus Comp IN button are relevant here
// — Bus Comp 2 has no EQ, no filters, no dedicated CS knobs.

PluginBindings makeBusComp2Bindings()
{
    auto b = makeEmpty("Bus Compressor 2", "BC 2");
    b.knobParam[knob::kBCThreshold] = 2;
    b.knobParam[knob::kBCMakeup]    = 3;
    b.knobParam[knob::kBCAttack]    = 4;
    b.knobParam[knob::kBCRelease]   = 5;
    b.knobParam[knob::kBCRatio]     = 6;
    b.knobParam[knob::kBCScHpf]     = 7;
    b.knobParam[knob::kBCMix]       = 8;
    // BC IN button = the plug-in's own "Comp Bypass" param at vst3 idx 10
    // (not REAPER's TrackFX_Enabled). Inverted semantic: param=1 means
    // bypassed → IN is OFF, so the LED state is the inverse of the read.
    b.bypassParam = 10;
    return b;
}

// ---- SSL 360 Link Bus Compressor (the wrapper variant) --------------------
//
// Same UC1 layout as Native BC 2 but vst3 indices line up 1:1 with the
// SSL 360 Link strip (CompBypass at 0; Threshold/Makeup/Attack/Release/
// Ratio/HPF/Mix at 1..7). See
// docs/ssl-native-params/VST3__SSL_360_Link_Bus_Compressor_(SSL).md.

PluginBindings makeSsl360LinkBcBindings()
{
    auto b = makeEmpty("SSL 360 Link Bus Compressor", "L-BC");
    b.knobParam[knob::kBCThreshold] = 1;
    b.knobParam[knob::kBCMakeup]    = 2;
    b.knobParam[knob::kBCAttack]    = 3;
    b.knobParam[knob::kBCRelease]   = 4;
    b.knobParam[knob::kBCRatio]     = 5;
    b.knobParam[knob::kBCScHpf]     = 6;
    b.knobParam[knob::kBCMix]       = 7;
    // BC IN button → "CompBypass" at vst3 0 on the wrapper.
    b.bypassParam = 0;
    return b;
}

// ---- Native SSL Channel Strip 2 --------------------------------------------
//
// VST3 param indices from kCs2Slots in PluginMap.cpp. All UC1 knobs that
// drive Channel Strip params are covered here.
//
// Note: button IDs for Solo, Cut, Solo Clear, Channel IN, Fine don't
// correspond to plugin params — those live on the track or are surface
// modifiers. Left kParamNone here; surface handles them.

// Apply the "knob goes the wrong way" inversions the user reported after
// hardware testing 2026-04-24: Low Pass and the two Mid-Q knobs run
// physically CCW = VST3 value up, so we flip the sign at dispatch time.
static void applyCsInversions(PluginBindings& b)
{
    b.inverted[knob::kCSLowPass]        = true;
    b.inverted[knob::kCSHmfQ]           = true;
    b.inverted[knob::kCSLmfQ]           = true;
    b.inverted[knob::kCSCompThreshold]  = true;
    b.inverted[knob::kCSGateThreshold]  = true;
}

PluginBindings makeChannelStrip2Bindings()
{
    auto b = makeEmpty("Channel Strip 2", "CS 2");

    // Dedicated CS knobs (left side: filters + EQ)
    b.knobParam[knob::kCSLowPass]   = 4;
    b.knobParam[knob::kCSHighPass]  = 5;
    b.knobParam[knob::kCSHfGain]    = 19;
    b.knobParam[knob::kCSHfFreq]    = 18;
    b.knobParam[knob::kCSHmfGain]   = 15;
    b.knobParam[knob::kCSHmfFreq]   = 14;
    b.knobParam[knob::kCSHmfQ]      = 16;
    b.knobParam[knob::kCSLmfGain]   = 12;
    b.knobParam[knob::kCSLmfFreq]   = 11;
    b.knobParam[knob::kCSLmfQ]      = 13;
    b.knobParam[knob::kCSLfFreq]    =  9;
    b.knobParam[knob::kCSLfGain]    = 10;

    // V-Pot repurposing when no Bus Comp 2 is present.
    b.knobParam[knob::kCSInputTrim]  = 2;    // Input Trim
    b.knobParam[knob::kCSFaderLevel] = 38;   // Linkable Fader Level

    // Right side: dynamics + gate
    b.knobParam[knob::kCSCompThreshold] = 24;
    b.knobParam[knob::kCSCompRatio]     = 23;
    b.knobParam[knob::kCSCompRelease]   = 25;
    b.knobParam[knob::kCSGateThreshold] = 30;
    b.knobParam[knob::kCSGateRange]     = 31;
    b.knobParam[knob::kCSGateRelease]   = 32;
    b.knobParam[knob::kCSGateHold]      = 29;

    // Buttons — plugin-local toggles
    b.buttonParam[button::kHfBell]      = 17;  // HighEqBell
    b.buttonParam[button::kEqType]      =  7;  // EqType
    b.buttonParam[button::kEqIn]        =  6;  // EqIn
    b.buttonParam[button::kLfBell]      =  8;  // LowEqBell
    b.buttonParam[button::kFastAttComp] = 22;  // CompFastAttack
    b.buttonParam[button::kPeak]        = 21;  // CompPeak
    b.buttonParam[button::kDynIn]       = 20;  // DynamicsIn
    b.buttonParam[button::kExpand]      = 27;  // GateExpander
    b.buttonParam[button::kFastAttGate] = 28;  // GateAttack (Gate F.Attack)
    b.buttonParam[button::kScListen]    = 37;  // Listen

    // CS IN button (UC1 ChannelIn) → plug-in's own "Bypass" param at
    // vst3 idx 0 (NOT REAPER's TrackFX_Enabled). Inverted semantic.
    b.bypassParam = 0;

    applyCsInversions(b);
    return b;
}

// ---- SSL 360 Link (CS wrapper variant) -------------------------------------
//
// vst3 indices match the SSL 360 Link strip 1:1 (linkIdx == vst3). VST3
// value semantic matches the 4K-series wrappers (NOT Native CS 2) for
// the threshold/Q knobs — user confirmed 2026-05-01 that applying the
// CS-2 inversions here flips Comp/Gate Threshold and HMF/LMF Q. So we
// skip applyCsInversions. See
// docs/ssl-native-params/VST3__SSL_360_Link_(SSL).md.

PluginBindings makeSsl360LinkBindings()
{
    auto b = makeEmpty("SSL 360 Link", "Link");

    b.knobParam[knob::kCSLowPass]   = 6;
    b.knobParam[knob::kCSHighPass]  = 7;
    b.knobParam[knob::kCSHfGain]    = 9;
    b.knobParam[knob::kCSHfFreq]    = 10;
    b.knobParam[knob::kCSHmfGain]   = 11;
    b.knobParam[knob::kCSHmfFreq]   = 12;
    b.knobParam[knob::kCSHmfQ]      = 13;
    b.knobParam[knob::kCSLmfGain]   = 16;
    b.knobParam[knob::kCSLmfFreq]   = 17;
    b.knobParam[knob::kCSLmfQ]      = 18;
    b.knobParam[knob::kCSLfFreq]    = 19;
    b.knobParam[knob::kCSLfGain]    = 20;

    // V-Pot repurposing when no Bus Comp present — wrapper has Input Trim
    // at vst3 4 and Fader Level at vst3 1. Per the 2026-04-30 user
    // instruction we route Plugin-mode fader to "Fader Level" (1), not
    // the "Linkable Fader Level" alias (35).
    b.knobParam[knob::kCSInputTrim]  = 4;
    b.knobParam[knob::kCSFaderLevel] = 1;

    b.knobParam[knob::kCSCompThreshold] = 27;
    b.knobParam[knob::kCSCompRatio]     = 26;
    b.knobParam[knob::kCSCompRelease]   = 28;
    b.knobParam[knob::kCSGateThreshold] = 30;
    b.knobParam[knob::kCSGateRange]     = 29;
    b.knobParam[knob::kCSGateRelease]   = 31;
    b.knobParam[knob::kCSGateHold]      = 32;

    b.buttonParam[button::kHfBell]      = 8;   // HighEqBell
    b.buttonParam[button::kEqType]      = 14;
    b.buttonParam[button::kEqIn]        = 15;
    b.buttonParam[button::kLfBell]      = 21;  // LowEqBell
    b.buttonParam[button::kFastAttComp] = 24;  // CompFastAttack
    b.buttonParam[button::kPeak]        = 25;  // CompPeak
    b.buttonParam[button::kDynIn]       = 22;
    b.buttonParam[button::kExpand]      = 33;  // GateExpander
    b.buttonParam[button::kFastAttGate] = 34;  // GateAttack
    b.buttonParam[button::kScListen]    = 36;  // Listen

    // CS IN → wrapper's own Bypass at vst3 0 (matches Native CS 2 idiom).
    b.bypassParam = 0;
    // No applyCsInversions — see comment above.
    return b;
}

// ---- 4K E ------------------------------------------------------------------
// VST3 param indices from kCs2Slots' "4K E" counterpart (PluginMap.cpp).
// Same UC1 knob/button layout; differences: EQ Type is "EQ Colour", no
// Comp Peak, no Gate Hold.
PluginBindings make4kEBindings()
{
    auto b = makeEmpty("4K E", "4K E");
    b.knobParam[knob::kCSLowPass]       = 15;
    b.knobParam[knob::kCSHighPass]      = 14;
    b.knobParam[knob::kCSHfGain]        = 31;
    b.knobParam[knob::kCSHfFreq]        = 30;
    b.knobParam[knob::kCSHmfGain]       = 28;
    b.knobParam[knob::kCSHmfFreq]       = 27;
    b.knobParam[knob::kCSHmfQ]          = 29;
    b.knobParam[knob::kCSLmfGain]       = 25;
    b.knobParam[knob::kCSLmfFreq]       = 24;
    b.knobParam[knob::kCSLmfQ]          = 26;
    b.knobParam[knob::kCSLfFreq]        = 21;
    b.knobParam[knob::kCSLfGain]        = 22;
    b.knobParam[knob::kCSInputTrim]     =  2;
    b.knobParam[knob::kCSFaderLevel]    =  6;
    b.knobParam[knob::kCSCompThreshold] = 36;
    b.knobParam[knob::kCSCompRatio]     = 35;
    b.knobParam[knob::kCSCompRelease]   = 37;
    b.knobParam[knob::kCSGateThreshold] = 43;
    b.knobParam[knob::kCSGateRange]     = 42;
    b.knobParam[knob::kCSGateRelease]   = 44;
    // Gate Hold not present on 4K E
    b.buttonParam[button::kHfBell]      = 32;
    b.buttonParam[button::kEqType]      = 19;  // EQ Colour
    b.buttonParam[button::kEqIn]        = 18;
    b.buttonParam[button::kLfBell]      = 23;
    b.buttonParam[button::kFastAttComp] = 39;
    // Comp Peak not present on 4K E
    b.buttonParam[button::kDynIn]       = 33;
    b.buttonParam[button::kExpand]      = 46;
    b.buttonParam[button::kFastAttGate] = 45;
    b.buttonParam[button::kScListen]    = 47;
    // CS IN → plug-in Bypass at vst3 idx 0 (same across CS variants).
    b.bypassParam = 0;
    // 4K E has opposite VST3-value semantic from CS 2 for several knobs
    // (LP, HMF/LMF Q, Comp/Gate Threshold). User confirmed 2026-04-24:
    // applying the CS-2 inversions here makes the pot motion flip.
    return b;
}

// ---- 4K G ------------------------------------------------------------------
// Full-featured G-series strip. No Gate Hold, no Comp Peak.
PluginBindings make4kGBindings()
{
    auto b = makeEmpty("4K G", "4K G");
    b.knobParam[knob::kCSLowPass]       = 21;
    b.knobParam[knob::kCSHighPass]      = 20;
    b.knobParam[knob::kCSHfGain]        = 36;
    b.knobParam[knob::kCSHfFreq]        = 35;
    b.knobParam[knob::kCSHmfGain]       = 33;
    b.knobParam[knob::kCSHmfFreq]       = 32;
    b.knobParam[knob::kCSHmfQ]          = 34;
    b.knobParam[knob::kCSLmfGain]       = 29;
    b.knobParam[knob::kCSLmfFreq]       = 28;
    b.knobParam[knob::kCSLmfQ]          = 30;
    b.knobParam[knob::kCSLfFreq]        = 24;
    b.knobParam[knob::kCSLfGain]        = 25;
    b.knobParam[knob::kCSInputTrim]     =  6;
    b.knobParam[knob::kCSFaderLevel]    = 12;
    b.knobParam[knob::kCSCompThreshold] = 40;
    b.knobParam[knob::kCSCompRatio]     = 39;
    b.knobParam[knob::kCSCompRelease]   = 41;
    b.knobParam[knob::kCSGateThreshold] = 47;
    b.knobParam[knob::kCSGateRange]     = 46;
    b.knobParam[knob::kCSGateRelease]   = 48;
    b.buttonParam[button::kHfBell]      = 37;
    b.buttonParam[button::kEqType]      = 23;  // EQ Colour
    b.buttonParam[button::kEqIn]        = 22;
    b.buttonParam[button::kLfBell]      = 26;
    b.buttonParam[button::kFastAttComp] = 43;
    b.buttonParam[button::kDynIn]       = 38;
    b.buttonParam[button::kExpand]      = 50;
    b.buttonParam[button::kFastAttGate] = 49;
    b.buttonParam[button::kScListen]    = 51;
    b.bypassParam = 0;  // CS IN → plug-in Bypass at vst3 idx 0
    // 4K G skips CS-2 inversions (same VST3 semantic as 4K E — see above).
    return b;
}

// ---- 4K B ------------------------------------------------------------------
// Simpler B-series: no EQ Type, no Fast-Attack (Comp/Gate), no Gate Hold,
// no Comp Peak.
PluginBindings make4kBBindings()
{
    auto b = makeEmpty("4K B", "4K B");
    b.knobParam[knob::kCSLowPass]       = 11;
    b.knobParam[knob::kCSHighPass]      = 10;
    b.knobParam[knob::kCSHfGain]        = 26;
    b.knobParam[knob::kCSHfFreq]        = 25;
    b.knobParam[knob::kCSHmfGain]       = 23;
    b.knobParam[knob::kCSHmfFreq]       = 22;
    b.knobParam[knob::kCSHmfQ]          = 24;
    b.knobParam[knob::kCSLmfGain]       = 20;
    b.knobParam[knob::kCSLmfFreq]       = 19;
    b.knobParam[knob::kCSLmfQ]          = 21;
    b.knobParam[knob::kCSLfFreq]        = 16;
    b.knobParam[knob::kCSLfGain]        = 17;
    b.knobParam[knob::kCSInputTrim]     =  2;
    b.knobParam[knob::kCSFaderLevel]    =  6;
    b.knobParam[knob::kCSCompThreshold] = 31;
    b.knobParam[knob::kCSCompRatio]     = 30;
    b.knobParam[knob::kCSCompRelease]   = 32;
    b.knobParam[knob::kCSGateThreshold] = 35;
    b.knobParam[knob::kCSGateRange]     = 34;
    b.knobParam[knob::kCSGateRelease]   = 36;
    b.buttonParam[button::kHfBell]      = 27;
    // No EQ Type on 4K B
    b.buttonParam[button::kEqIn]        = 14;
    b.buttonParam[button::kLfBell]      = 18;
    b.buttonParam[button::kDynIn]       = 28;
    b.buttonParam[button::kExpand]      = 37;
    b.buttonParam[button::kScListen]    = 41;
    b.bypassParam = 0;  // CS IN → plug-in Bypass at vst3 idx 0
    // 4K B skips CS-2 inversions (same VST3 semantic as 4K E — see above).
    return b;
}

// Registry. Order: most-specific substring first (same convention as the
// UF8 PluginMap). BC 2's match string wouldn't collide with any of the
// Channel Strip variants so ordering there isn't critical, but the 4K
// series all contain "SSL" so putting the longer "4K G" before "4K E"
// before "4K B" keeps lookupBindingsByName unambiguous.
const PluginBindings& csReg()   { static auto v = makeChannelStrip2Bindings();   return v; }
const PluginBindings& bcReg()   { static auto v = makeBusComp2Bindings();        return v; }
const PluginBindings& linkReg() { static auto v = makeSsl360LinkBindings();      return v; }
const PluginBindings& linkBcReg(){static auto v = makeSsl360LinkBcBindings();    return v; }
const PluginBindings& e4Reg()   { static auto v = make4kEBindings();             return v; }
const PluginBindings& g4Reg()   { static auto v = make4kGBindings();             return v; }
const PluginBindings& b4Reg()   { static auto v = make4kBBindings();             return v; }

const PluginBindings* kChannelStripCandidates[] = {
    &csReg(), &g4Reg(), &e4Reg(), &b4Reg(), &linkReg(),
};

// BC variants — order matters for substring matching: "SSL 360 Link Bus
// Compressor" must come before "Bus Compressor 2" to win on its own
// name. (And linkBcReg's match string is more specific than bcReg's.)
const PluginBindings* kBusCompCandidates[] = {
    &linkBcReg(), &bcReg(),
};

// ---- User-map → UC1 binding bridge --------------------------------------
//
// When the FX-Learn editor produces a UserPluginMap (per-linkIdx
// vst3Param + inverted), UC1 needs the same data in its per-knob-id
// PluginBindings layout so the BC encoder / dispatch path treat the
// user's plugin like any built-in. We synthesize one PluginBindings per
// user map, cached by match string, invalidated via user_plugins's
// generation counter.

constexpr uint8_t kNoUc1 = 0xFF;

// Maps an SSL 360 Link CS slot index to the UC1 control(s) that drive it.
// linkIdx not in this table → not reachable from UC1 (fader / pan /
// width / output / extras / QA1..6 / SAT.* / GRP). Knob and button can
// both be filled when an SSL slot is exposed by both (none today).
struct LinkToUc1 {
    int     linkIdx;
    uint8_t knobId;
    uint8_t buttonId;
};

constexpr LinkToUc1 kCsLinkToUc1[] = {
    // FaderLevel (slot 1) → UC1 Output Gain knob (0x16). On SSL CS the
    // "Output Gain" knob IS the channel's Fader Level (same vst3 param,
    // dual label). For user-CS plug-ins that learn a FaderLevel slot,
    // wire the UC1 Output Gain knob to drive that param too — keeps
    // built-in CS and user-CS symmetric (Frank 2026-05-14).
    {  1, knob::kCSFaderLevel,   kNoUc1               },
    {  4, knob::kCSInputTrim,    kNoUc1               },
    {  5, kNoUc1,                button::kPolarity    },
    {  6, knob::kCSLowPass,      kNoUc1               },
    {  7, knob::kCSHighPass,     kNoUc1               },
    {  8, kNoUc1,                button::kHfBell      },
    {  9, knob::kCSHfGain,       kNoUc1               },
    { 10, knob::kCSHfFreq,       kNoUc1               },
    { 11, knob::kCSHmfGain,      kNoUc1               },
    { 12, knob::kCSHmfFreq,      kNoUc1               },
    { 13, knob::kCSHmfQ,         kNoUc1               },
    { 14, kNoUc1,                button::kEqType      },
    { 15, kNoUc1,                button::kEqIn        },
    { 16, knob::kCSLmfGain,      kNoUc1               },
    { 17, knob::kCSLmfFreq,      kNoUc1               },
    { 18, knob::kCSLmfQ,         kNoUc1               },
    { 19, knob::kCSLfFreq,       kNoUc1               },
    { 20, knob::kCSLfGain,       kNoUc1               },
    { 21, kNoUc1,                button::kLfBell      },
    { 22, kNoUc1,                button::kDynIn       },
    { 24, kNoUc1,                button::kFastAttComp },
    { 25, kNoUc1,                button::kPeak        },
    { 26, knob::kCSCompRatio,    kNoUc1               },
    { 27, knob::kCSCompThreshold,kNoUc1               },
    { 28, knob::kCSCompRelease,  kNoUc1               },
    { 29, knob::kCSGateRange,    kNoUc1               },
    { 30, knob::kCSGateThreshold,kNoUc1               },
    { 31, knob::kCSGateRelease,  kNoUc1               },
    { 32, knob::kCSGateHold,     kNoUc1               },
    { 33, kNoUc1,                button::kExpand      },
    { 34, kNoUc1,                button::kFastAttGate },
    { 36, kNoUc1,                button::kScListen    },
};

// Maps an SSL 360 Link BC slot index to the UC1 control. BC has only the
// top V-Pots + the IN button.
constexpr LinkToUc1 kBcLinkToUc1[] = {
    {  0, kNoUc1,                button::kBusCompIn   }, // bypass / IN
    {  1, knob::kBCThreshold,    kNoUc1               },
    {  2, knob::kBCMakeup,       kNoUc1               },
    {  3, knob::kBCAttack,       kNoUc1               },
    {  4, knob::kBCRelease,      kNoUc1               },
    {  5, knob::kBCRatio,        kNoUc1               },
    {  6, knob::kBCScHpf,        kNoUc1               },
    {  7, knob::kBCMix,          kNoUc1               },
};

// Fill a PluginBindings from a UserPluginMap. Caller owns the storage of
// `out` — match/shortName must already point to stable strings (we don't
// touch them here). `buttonSteps` (optional) receives each button's
// push-cycle step list, keyed by UC1 button id, so the runtime can cycle a
// curated subset / multi-param macro instead of the bound param's full
// option set. PluginBindings itself stays a literal type (it is built via
// constexpr makeEmpty), so the std::vector-backed steps live on the cache
// entry rather than inside PluginBindings.
void synthesizeUserBinding_(const uf8::UserPluginMap& um, PluginBindings& out,
                            int layer,
                            std::array<std::vector<uf8::PushStep>, 0x20>* buttonSteps = nullptr,
                            std::array<std::vector<uf8::PushStep>, 0x20>* knobSteps = nullptr)
{
    if (buttonSteps) for (auto& v : *buttonSteps) v.clear();
    if (knobSteps)   for (auto& v : *knobSteps)   v.clear();
    for (auto& v : out.knobParam)      v = kParamNone;
    for (auto& v : out.buttonParam)    v = kParamNone;
    for (auto& v : out.inverted)       v = false;
    for (auto& v : out.buttonInverted) v = false;
    out.bypassParam        = kParamNone;
    out.bypassInverted     = false;
    out.polarityUsesTrack  = um.useReaperTrackPolarity;

    const LinkToUc1* table     = nullptr;
    int              tableSize = 0;
    if (um.domain == uf8::Domain::BusComp) {
        table     = kBcLinkToUc1;
        tableSize = sizeof(kBcLinkToUc1) / sizeof(kBcLinkToUc1[0]);
    } else if (um.domain == uf8::Domain::ChannelStrip) {
        table     = kCsLinkToUc1;
        tableSize = sizeof(kCsLinkToUc1) / sizeof(kCsLinkToUc1[0]);
    } else {
        return;
    }

    for (const auto& slot : um.slots) {
        // Resolve which layer actually drives this control: the requested
        // FX-Learn layer if it carries a mapping, else fall back to Normal.
        // (Holding Option/Control only remaps the controls you've explicitly
        // overlaid; everything else keeps its Normal mapping.)
        const uf8::SlotLayer& eff = uf8::fxEffectiveLayer(slot, layer);
        const bool hasSteps = !eff.pushSteps.empty();
        // A macro slot may carry no primary param (vst3Param < 0) yet still
        // drive a button via its push-cycle steps. Keep processing such a
        // slot; skip only when it has neither a param nor steps.
        if (eff.vst3Param < 0 && !hasSteps) continue;
        // SSL Link slot 0 = plug-in bypass on both CS and BC. Persist
        // the bound vst3Param into bypassParam so the IN button toggles
        // the plug-in's own bypass param rather than REAPER's
        // TrackFX_Enabled (matches what built-in BC 2 does).
        // Carry the slot's inverted flag too: SSL "Bypass" semantic
        // means 1=off (LED OFF on 1), but plug-ins like bx_townhouse
        // expose "Comp In" (1=on, LED ON on 1). The FX-Learn UI's
        // inverted toggle on this slot drives bypassInverted, which
        // the IN-button + LED render paths consume.
        if (slot.linkIdx == 0 && eff.vst3Param >= 0) {
            out.bypassParam    = eff.vst3Param;
            out.bypassInverted = eff.inverted;
        }
        for (int i = 0; i < tableSize; ++i) {
            if (table[i].linkIdx != slot.linkIdx) continue;
            if (table[i].knobId != kNoUc1) {
                if (eff.vst3Param >= 0) {
                    out.knobParam[table[i].knobId] = eff.vst3Param;
                    out.inverted[table[i].knobId]  = eff.inverted;
                }
                // Rotary step-cycle on a UC1 knob: same pushSteps model as a
                // button, but the knob scrubs through them on turn.
                if (knobSteps && hasSteps)
                    (*knobSteps)[table[i].knobId] = eff.pushSteps;
            }
            if (table[i].buttonId != kNoUc1) {
                if (eff.vst3Param >= 0) {
                    out.buttonParam[table[i].buttonId]    = eff.vst3Param;
                    out.buttonInverted[table[i].buttonId] = eff.inverted;
                }
                if (buttonSteps && hasSteps)
                    (*buttonSteps)[table[i].buttonId] = eff.pushSteps;
            }
            break;
        }
    }
}

// Per-user-map cache entry. `match` and `shortName` strings own their
// storage so PluginBindings::match / shortName can point into them.
struct UserBindingEntry {
    std::string    matchOwned;
    std::string    shortNameOwned;
    // One synthesized PluginBindings per FX-Learn layer (Normal/Option/
    // Control). lookupBindingsByName returns the active layer's; identity
    // checks (owns / layerIndexOf) scan all layers so a BC plug-in stays
    // classified as BC regardless of which modifier is held.
    PluginBindings bindings[uf8::kNumFxLayers]{};
    uf8::Domain    domain = uf8::Domain::None;
    // Metering passthrough — copied from the source UserPluginMap so the
    // GR poll can read an explicit VST3 param when the plug-in doesn't
    // implement the PreSonus GainReduction_dB hook.
    int            grVst3Param = -1;
    double         grOffsetDb  = 0.0;
    // Per-breakpoint correction tables (v5 schema). UC1Bindings carries
    // pointers into these arrays; lifetime is the cache entry's lifetime
    // (rebuilt on user_plugins generation bump, main-thread only).
    double         bcVuCalDb[6] = {0,0,0,0,0,0};
    double         ledsCalDb[5] = {0,0,0,0,0};
    // Per-button push-cycle step lists (keyed by UC1 button id). Empty =>
    // legacy auto-cycle of the bound param. Lives here (not in
    // PluginBindings) so PluginBindings stays a constexpr-buildable literal
    // type. Lifetime = cache entry lifetime; pushStepsForButton() returns
    // pointers into this, consumed on the same main thread before any
    // rebuild.
    std::array<std::vector<uf8::PushStep>, 0x20> buttonSteps[uf8::kNumFxLayers];
    // Per-knob rotary step-cycle lists (keyed by UC1 knob id). Same model /
    // lifetime as buttonSteps; consumed by pushStepsForKnob().
    std::array<std::vector<uf8::PushStep>, 0x20> knobSteps[uf8::kNumFxLayers];

    // Learn-HUD: per-layer set of SSL-Link linkIdx that carry their OWN
    // overlay on that layer (vs inheriting Normal). Normal's entry holds every
    // mapped linkIdx; a modifier layer holds only the explicitly-overlaid ones.
    // Populated in rebuildUserCache_locked_ alongside the synthesized bindings.
    std::vector<int> explicitLinks[uf8::kNumFxLayers];

    // Identity helpers — `b` may point at any layer's PluginBindings.
    int layerIndexOf(const PluginBindings* b) const {
        for (int l = 0; l < uf8::kNumFxLayers; ++l)
            if (&bindings[l] == b) return l;
        return -1;
    }
    bool owns(const PluginBindings* b) const { return layerIndexOf(b) >= 0; }
};

std::mutex                                       g_userCacheMutex;
std::vector<std::unique_ptr<UserBindingEntry>>   g_userCache;
int                                              g_userCacheGeneration = -1;

// Rebuild the user-binding cache from current user_plugins state. Caller
// must hold g_userCacheMutex.
void rebuildUserCache_locked_()
{
    g_userCache.clear();
    const auto& cat = uf8::user_plugins::get();
    for (const auto& um : cat.maps) {
        if (um.domain != uf8::Domain::BusComp &&
            um.domain != uf8::Domain::ChannelStrip)
        {
            continue;
        }
        if (um.match.empty()) continue;
        auto e = std::make_unique<UserBindingEntry>();
        e->matchOwned     = um.match;
        e->shortNameOwned = um.displayShort.empty()
            ? std::string("USR")
            : um.displayShort;
        e->domain         = um.domain;
        // grVst3Param: when set (≥0) the GR read path uses this VST3
        // param directly instead of the PreSonus GainReduction_dB host
        // extension. Re-enabled 2026-05-25 — the host-extension auto-
        // detect doesn't reach every compressor, and the per-map
        // override gives users a manual route around that. Default is
        // -1 (fall back to GainReduction_dB), set via the small "GR"
        // popup next to the AutoLearn button in the FX Learn editor.
        e->grVst3Param    = um.metering.grVst3Param;
        e->grOffsetDb     = um.metering.grOffsetDb;
        for (int i = 0; i < 6; ++i) e->bcVuCalDb[i] = um.metering.grBcVuCalDb[i];
        for (int i = 0; i < 5; ++i) e->ledsCalDb[i] = um.metering.grLedsCalDb[i];
        for (int l = 0; l < uf8::kNumFxLayers; ++l) {
            e->bindings[l].match     = e->matchOwned.c_str();
            e->bindings[l].shortName = e->shortNameOwned.c_str();
            synthesizeUserBinding_(um, e->bindings[l], l, &e->buttonSteps[l],
                                   &e->knobSteps[l]);
            // Record which linkIdx own an overlay on THIS layer (no Normal
            // fallback) — the Learn-HUD uses it to render modifier layers with
            // only their real overlays. Normal collects every mapped linkIdx.
            e->explicitLinks[l].clear();
            for (const auto& slot : um.slots) {
                if (uf8::fxLayerMapped(uf8::fxLayerOf(slot, l)))
                    e->explicitLinks[l].push_back(slot.linkIdx);
            }
        }
        g_userCache.push_back(std::move(e));
    }
    g_userCacheGeneration =
        uf8::user_plugins::generation();
}

// Refresh cache if user_plugins changed since last build. Returns with
// g_userCacheMutex held; caller is responsible for unlocking before any
// further user_plugins:: read to avoid lock-order issues.
void refreshUserCache_()
{
    if (uf8::user_plugins::generation() == g_userCacheGeneration) return;
    std::lock_guard<std::mutex> lk(g_userCacheMutex);
    if (uf8::user_plugins::generation() == g_userCacheGeneration) return;
    rebuildUserCache_locked_();
}

} // namespace

// True when this binding's plug-in is a Bus Compressor variant. The
// older code identified BC by `shortName == "BC 2"`, which broke as
// soon as the SSL 360 Link wrapper came in with its own shortName.
bool isBusCompBinding(const PluginBindings* b)
{
    if (!b) return false;
    for (const auto* c : kBusCompCandidates) if (c == b) return true;
    // User-synthesized BC bindings: walk the cache and check ownership.
    std::lock_guard<std::mutex> lk(g_userCacheMutex);
    for (const auto& e : g_userCache) {
        if (e->owns(b)) return e->domain == uf8::Domain::BusComp;
    }
    return false;
}

const std::vector<uf8::PushStep>*
pushStepsForButton(const PluginBindings* channelMap, uint8_t buttonId)
{
    if (!channelMap || buttonId >= 0x20) return nullptr;
    // Built-in CS/4K bindings are never in g_userCache, so they fall
    // through to nullptr → the runtime keeps its legacy auto-cycle. Only
    // user FX-Learn maps own per-button step lists. The returned pointer is
    // consumed immediately on the same (main) thread that may rebuild the
    // cache, so no rebuild can intervene between this call and its use.
    std::lock_guard<std::mutex> lk(g_userCacheMutex);
    for (const auto& e : g_userCache) {
        const int l = e->layerIndexOf(channelMap);
        if (l < 0) continue;
        const auto& v = e->buttonSteps[l][buttonId];
        return v.empty() ? nullptr : &v;
    }
    return nullptr;
}

const std::vector<uf8::PushStep>*
pushStepsForKnob(const PluginBindings* channelMap, uint8_t knobId)
{
    if (!channelMap || knobId >= 0x20) return nullptr;
    std::lock_guard<std::mutex> lk(g_userCacheMutex);
    for (const auto& e : g_userCache) {
        const int l = e->layerIndexOf(channelMap);
        if (l < 0) continue;
        const auto& v = e->knobSteps[l][knobId];
        return v.empty() ? nullptr : &v;
    }
    return nullptr;
}

const PluginBindings* lookupBindingsByName(std::string_view fxName)
{
    // Built-ins win first (mirrors uf8::lookupPluginMapByName ordering)
    // — UserPluginCatalog::save guarantees no user map can shadow a
    // built-in match string anyway.
    for (const auto* b : kBusCompCandidates) {
        if (fxName.find(b->match) != std::string_view::npos) return b;
    }
    for (const auto* b : kChannelStripCandidates) {
        if (fxName.find(b->match) != std::string_view::npos) return b;
    }

    // User catalog fallback — synthesize a UC1 PluginBindings on first
    // touch, cache it, return the active FX-Learn layer's stable pointer.
    // The active layer is resolved from held Option/Control (recomputed once
    // per onTimer tick); a control with no overlay on that layer was
    // synthesized to fall back to its Normal mapping, so the returned binding
    // is correct for every control.
    int L = reasixty_fxLearnActiveLayer();
    if (L < 0 || L >= uf8::kNumFxLayers) L = uf8::FxLayer::Normal;
    refreshUserCache_();
    std::lock_guard<std::mutex> lk(g_userCacheMutex);
    for (const auto& e : g_userCache) {
        if (fxName.find(e->matchOwned) != std::string_view::npos) {
            return &e->bindings[L];
        }
    }
    return nullptr;
}

int hudParamForControl(const PluginBindings* b, bool busComp,
                       int linkIdx, bool isButton, bool* outInverted)
{
    if (outInverted) *outInverted = false;
    if (!b) return kParamNone;
    const LinkToUc1* table = busComp ? kBcLinkToUc1 : kCsLinkToUc1;
    const int n = busComp
        ? static_cast<int>(sizeof(kBcLinkToUc1) / sizeof(kBcLinkToUc1[0]))
        : static_cast<int>(sizeof(kCsLinkToUc1) / sizeof(kCsLinkToUc1[0]));
    for (int i = 0; i < n; ++i) {
        if (table[i].linkIdx != linkIdx) continue;
        if (isButton) {
            const uint8_t bid = table[i].buttonId;
            if (bid == kNoUc1) return kParamNone;
            if (outInverted) *outInverted = b->buttonInverted[bid];
            // Bypass buttons (linkIdx 0) drive plugin enable, not a mapped
            // param — buttonParam is -1 but the binding still owns the control
            // via bypassParam. Report that so the HUD shows it as "mapped".
            int p = b->buttonParam[bid];
            if (p < 0 && linkIdx == 0) p = b->bypassParam;
            return p;
        }
        const uint8_t kid = table[i].knobId;
        if (kid == kNoUc1) return kParamNone;
        if (outInverted) *outInverted = b->inverted[kid];
        return b->knobParam[kid];
    }
    return kParamNone;
}

bool hudControlExplicitOnLayer(const PluginBindings* b, int linkIdx)
{
    if (!b) return true;
    std::lock_guard<std::mutex> lk(g_userCacheMutex);
    for (const auto& e : g_userCache) {
        const int L = e->layerIndexOf(b);
        if (L < 0) continue;                       // not this entry's binding
        if (L == uf8::FxLayer::Normal) return true; // Normal: never inherited
        for (int li : e->explicitLinks[L]) if (li == linkIdx) return true;
        return false;                               // inherits Normal here
    }
    return true;  // built-in / unknown binding → no layers → always shown
}

int linkIdxForControl(uint8_t controlId, bool busComp, bool isButton)
{
    const LinkToUc1* table = busComp ? kBcLinkToUc1 : kCsLinkToUc1;
    const int n = busComp
        ? static_cast<int>(sizeof(kBcLinkToUc1) / sizeof(kBcLinkToUc1[0]))
        : static_cast<int>(sizeof(kCsLinkToUc1) / sizeof(kCsLinkToUc1[0]));
    for (int i = 0; i < n; ++i) {
        const uint8_t id = isButton ? table[i].buttonId : table[i].knobId;
        if (id != kNoUc1 && id == controlId) return table[i].linkIdx;
    }
    return -1;
}

// Common name aliases per UC1 knob — '|'-separated, lowercase. Lets value
// transfer resolve a control on a plug-in that only NAMES its param differently
// (e.g. Analog Molecule's "High Pass Filter (Hz)") and never got an explicit
// FX-Learn mapping. Tokens are deliberately DISTINCTIVE (multi-word where a bare
// word like "gain"/"release"/"level" would over-match) so a substring hit is
// almost certainly the right control. Knobs only — buttons stay explicit.
namespace {
struct CtrlAlias { uint8_t knobId; const char* aliases; };
constexpr CtrlAlias kCtrlAliases[] = {
    { knob::kCSHighPass,      "high pass|highpass|hi pass|hpf|hp filter|low cut|lo cut|locut|low-cut" },
    { knob::kCSLowPass,       "low pass|lowpass|lo pass|lpf|lp filter|high cut|hi cut|hicut|high-cut" },
    { knob::kCSHfGain,        "hf gain|high shelf gain|high frequency gain|treble gain|hf level" },
    { knob::kCSHfFreq,        "hf freq|hi freq|high shelf freq|treble freq|hf frequency" },
    { knob::kCSHmfGain,       "hmf gain|high mid gain|high-mid gain|hi mid gain|upper mid gain|himid gain" },
    { knob::kCSHmfFreq,       "hmf freq|high mid freq|high-mid freq|hi mid freq|upper mid freq|himid freq" },
    { knob::kCSHmfQ,          "hmf q|high mid q|high-mid q|hi mid q|upper mid q|himid q" },
    { knob::kCSLmfGain,       "lmf gain|low mid gain|low-mid gain|lo mid gain|lower mid gain|lomid gain" },
    { knob::kCSLmfFreq,       "lmf freq|low mid freq|low-mid freq|lo mid freq|lower mid freq|lomid freq" },
    { knob::kCSLmfQ,          "lmf q|low mid q|low-mid q|lo mid q|lower mid q|lomid q" },
    { knob::kCSLfFreq,        "lf freq|lo freq|low shelf freq|bass freq|lf frequency" },
    { knob::kCSLfGain,        "lf gain|low shelf gain|low frequency gain|bass gain|lf level" },
    { knob::kCSInputTrim,     "input trim|input gain|in gain|input level|preamp" },
    { knob::kCSFaderLevel,    "fader level|output gain|output level|out gain|makeup gain|make-up gain" },
    { knob::kCSCompThreshold, "comp threshold|compressor threshold|comp thresh" },
    { knob::kCSCompRatio,     "comp ratio|compressor ratio|compression ratio" },
    { knob::kCSCompRelease,   "comp release|compressor release|comp rel" },
    { knob::kCSGateThreshold, "gate threshold|expander threshold|gate thresh|exp threshold" },
    { knob::kCSGateRange,     "gate range|expander range|gate depth" },
    { knob::kCSGateRelease,   "gate release|expander release|gate rel" },
    { knob::kCSGateHold,      "gate hold|expander hold" },
};
}  // namespace

bool controlAliasMatch(int linkIdx, const char* paramName)
{
    if (!paramName || !*paramName) return false;
    uint8_t kid = kNoUc1;
    for (const auto& e : kCsLinkToUc1)
        if (e.linkIdx == linkIdx) { kid = e.knobId; break; }
    if (kid == kNoUc1) return false;
    const char* aliases = nullptr;
    for (const auto& a : kCtrlAliases)
        if (a.knobId == kid) { aliases = a.aliases; break; }
    if (!aliases) return false;

    std::string p;
    p.reserve(96);
    for (const char* s = paramName; *s; ++s)
        p.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*s))));

    for (const char* a = aliases; *a;) {
        const char* e = a;
        while (*e && *e != '|') ++e;
        if (e > a && p.find(std::string(a, e - a)) != std::string::npos) return true;
        a = (*e == '|') ? e + 1 : e;
    }
    return false;
}

CsQuantityKind csQuantityKind(int linkIdx)
{
    uint8_t kid = kNoUc1;
    for (const auto& e : kCsLinkToUc1)
        if (e.linkIdx == linkIdx) { kid = e.knobId; break; }
    switch (kid) {
        case knob::kCSLowPass:   case knob::kCSHighPass:
        case knob::kCSHfFreq:    case knob::kCSHmfFreq:
        case knob::kCSLmfFreq:   case knob::kCSLfFreq:
            return CsQuantityKind::Freq;
        case knob::kCSHfGain:    case knob::kCSHmfGain:
        case knob::kCSLmfGain:   case knob::kCSLfGain:
        case knob::kCSInputTrim: case knob::kCSFaderLevel:
        case knob::kCSCompThreshold: case knob::kCSGateThreshold:
        case knob::kCSGateRange:
            return CsQuantityKind::Db;
        case knob::kCSHmfQ:      case knob::kCSLmfQ:
            return CsQuantityKind::Q;
        case knob::kCSCompRatio:
            return CsQuantityKind::Ratio;
        case knob::kCSCompRelease: case knob::kCSGateRelease:
        case knob::kCSGateHold:
            return CsQuantityKind::Time;
        default:
            return CsQuantityKind::Generic;
    }
}

ControlDomain classifyKnob(uint8_t knobId)
{
    // Top V-Pots live in 0x0C..0x16. By physical layout on the SSL UC1:
    //   0x0C  Input Gain  → sits above the Input VU strip (CS-side)
    //   0x0E..0x14  BC pots (Ratio/ScHpf/Atk/Rel/Thresh/Makeup/Mix)
    //   0x16  Output Gain → sits above the Output VU strip (CS-side)
    // The two end knobs (Input/Output Gain) are wired by default to
    // Channel Strip Input Trim + Fader Level — even when a Bus Comp
    // plug-in is on the track. BC2 has no equivalent params for them.
    if (knobId == knob::kCSInputTrim || knobId == knob::kCSFaderLevel)
        return ControlDomain::ChannelStrip;
    if (knobId >= 0x0E && knobId <= 0x14) return ControlDomain::BusComp;
    return ControlDomain::ChannelStrip;
}

ControlDomain classifyButton(uint8_t buttonId)
{
    if (buttonId == button::kBusCompIn) return ControlDomain::BusComp;
    return ControlDomain::ChannelStrip;
}

// Track-GUID → active FX-GUID string. In-memory only, no persistence.
// Stores the FX's own GUID (TrackFX_GetFXGUID + guidToString) so an
// intra-domain reorder of the FX chain doesn't drag the cursor onto a
// different plug-in. The public API still speaks ordinals — set/get
// translate at the API boundary, the storage is GUID-only. Default
// missing-key = 0 = first match (lazily; deletions also clamp to 0).
// History: was MediaTrack*-keyed, then trackGuid + ordinal int. The
// ordinal-int form silently moved the cursor when the user reordered
// FX — fixed 2026-05-19 (plan-fx-identity.md). [[uf8-focused-strip-follow-cycle]]
std::mutex                                  g_instanceMutex;
std::unordered_map<std::string, std::string> g_bcInstanceFxGuid;
std::unordered_map<std::string, std::string> g_csInstanceFxGuid;
std::unordered_map<std::string, std::string> g_uf8OnlyInstanceFxGuid;

std::string trackGuid_(void* trackRaw)
{
    if (!trackRaw) return {};
    MediaTrack* tr = static_cast<MediaTrack*>(trackRaw);
    if (!ValidatePtr2(nullptr, tr, "MediaTrack*")) return {};
    char buf[64] = {0};
    GetSetMediaTrackInfo_String(tr, "GUID", buf, false);
    return std::string{buf};
}

std::string trackGuid(void* track) { return trackGuid_(track); }

UC1Bindings lookupBindingsOnTrack(void* trackRaw)
{
    UC1Bindings result;
    MediaTrack* tr = static_cast<MediaTrack*>(trackRaw);
    if (!tr) return result;

    // Crash 2026-04-29 (SetSurfaceSolo during LoadProjectFromContext):
    // REAPER frees old project's MediaTrack* before notifying surfaces,
    // so a cached focusedTrack pointer becomes dangling. ValidatePtr2
    // returns false for freed tracks; bail out instead of crashing in
    // TrackFX_GetCount.
    if (!ValidatePtr2(nullptr, tr, "MediaTrack*")) return result;

    // Active instance ordinal per domain. Derived from the stored
    // fxGuid on each call (see bcInstanceIndex / csInstanceIndex), so
    // a reorder of the FX chain moves the ordinal but not the cursor's
    // identity. Defaults to 0 if unset.
    const int wantBc = bcInstanceIndex(tr);
    const int wantCs = csInstanceIndex(tr);
    int seenBc = 0;
    int seenCs = 0;

    const int n = TrackFX_GetCount(tr);
    char buf[256];
    for (int i = 0; i < n; ++i) {
        if (!uf8::fxIdentityName(tr, i, buf, sizeof(buf))) continue;
        std::string_view name{buf};
        const PluginBindings* b = lookupBindingsByName(name);
        if (!b) continue;

        // BC variant identification — kBusCompCandidates lists every
        // binding that should populate `result.busCompMap`. The older
        // shortName == "BC 2" check broke on the SSL 360 Link Bus
        // Compressor wrapper which carries shortName "L-BC".
        const bool isBusComp = isBusCompBinding(b);

        // Look up the user-cache entry that owns `b` so we can pull the
        // metering override (grVst3Param + grOffsetDb + cal tables) into
        // the result. Built-in bindings have no entry → grVst3Param stays
        // -1 and the GR poll falls back to GainReduction_dB; cal pointers
        // stay nullptr so calibration is identity.
        int           grParam   = -1;
        double        grOff     = 0.0;
        const double* bcVuCal   = nullptr;
        const double* ledsCal   = nullptr;
        {
            std::lock_guard<std::mutex> lk(g_userCacheMutex);
            for (const auto& e : g_userCache) {
                if (e->owns(b)) {
                    grParam = e->grVst3Param;
                    grOff   = e->grOffsetDb;
                    bcVuCal = e->bcVuCalDb;
                    ledsCal = e->ledsCalDb;
                    break;
                }
            }
        }

        if (isBusComp) {
            if (!result.busCompMap) {
                if (seenBc == wantBc) {
                    result.busCompMap        = b;
                    result.busCompFxIdx      = i;
                    result.busCompGrParam    = grParam;
                    result.busCompGrOffsetDb = grOff;
                    result.busCompGrBcVuCal  = bcVuCal;
                }
                ++seenBc;
            }
        } else {
            if (!result.channelMap) {
                if (seenCs == wantCs) {
                    result.channelMap        = b;
                    result.channelFxIdx      = i;
                    result.channelGrParam    = grParam;
                    result.channelGrOffsetDb = grOff;
                    result.channelGrLedsCal  = ledsCal;
                }
                ++seenCs;
            }
        }
        // Don't break early here — we need the full counts to know
        // whether to clamp the index after the loop.
    }
    // Clamp: if the active index landed past the end (deletion / not
    // yet enough instances), fall back to the last available match by
    // re-walking with index = max-1. Cheap; happens at most once per
    // tick + only when stale.
    auto reseat = [&](bool bc, int total) {
        if (total <= 0) return;
        const int last = total - 1;
        int seen = 0;
        for (int i = 0; i < n; ++i) {
            if (!uf8::fxIdentityName(tr, i, buf, sizeof(buf))) continue;
            const PluginBindings* bb = lookupBindingsByName(std::string_view{buf});
            if (!bb) continue;
            const bool isBc = isBusCompBinding(bb);
            if (isBc != bc) continue;
            if (seen == last) {
                // Look up GR override for this entry.
                int           grParam = -1;
                double        grOff   = 0.0;
                const double* bcVuCal = nullptr;
                const double* ledsCal = nullptr;
                {
                    std::lock_guard<std::mutex> lk(g_userCacheMutex);
                    for (const auto& e : g_userCache) {
                        if (e->owns(bb)) {
                            grParam = e->grVst3Param;
                            grOff   = e->grOffsetDb;
                            bcVuCal = e->bcVuCalDb;
                            ledsCal = e->ledsCalDb;
                            break;
                        }
                    }
                }
                if (bc) {
                    result.busCompMap        = bb;
                    result.busCompFxIdx      = i;
                    result.busCompGrParam    = grParam;
                    result.busCompGrOffsetDb = grOff;
                    result.busCompGrBcVuCal  = bcVuCal;
                } else {
                    result.channelMap        = bb;
                    result.channelFxIdx      = i;
                    result.channelGrParam    = grParam;
                    result.channelGrOffsetDb = grOff;
                    result.channelGrLedsCal  = ledsCal;
                }
                return;
            }
            ++seen;
        }
    };
    if (!result.busCompMap && seenBc > 0) reseat(true,  seenBc);
    if (!result.channelMap && seenCs > 0) reseat(false, seenCs);
    return result;
}

// ---- Public multi-instance API -------------------------------------------

namespace {
int instanceCountFor_(MediaTrack* tr, bool bc)
{
    if (!tr || !ValidatePtr2(nullptr, tr, "MediaTrack*")) return 0;
    const int n = TrackFX_GetCount(tr);
    char buf[256];
    int count = 0;
    for (int i = 0; i < n; ++i) {
        if (!uf8::fxIdentityName(tr, i, buf, sizeof(buf))) continue;
        const PluginBindings* b = lookupBindingsByName(std::string_view{buf});
        if (!b) continue;
        if (isBusCompBinding(b) == bc) ++count;
    }
    return count;
}
} // namespace

int bcInstanceCount(void* trackRaw)
{
    return instanceCountFor_(static_cast<MediaTrack*>(trackRaw), true);
}
int csInstanceCount(void* trackRaw)
{
    return instanceCountFor_(static_cast<MediaTrack*>(trackRaw), false);
}

namespace {

// Predicates over a single FX slot, used by the walk helpers below.
// Returning true means "this FX qualifies for this domain's instance
// count". Built-in CS/BC use the UC1 binding cache; UF8-only uses the
// user-plugin catalogue directly because those entries never appear
// in the binding cache.
bool isBcFx_(MediaTrack* tr, int fxIdx, char* nameBuf, int nameBufSz)
{
    if (!uf8::fxIdentityName(tr, fxIdx, nameBuf, nameBufSz)) return false;
    const PluginBindings* b = lookupBindingsByName(std::string_view{nameBuf});
    return b && isBusCompBinding(b);
}
bool isCsFx_(MediaTrack* tr, int fxIdx, char* nameBuf, int nameBufSz)
{
    if (!uf8::fxIdentityName(tr, fxIdx, nameBuf, nameBufSz)) return false;
    const PluginBindings* b = lookupBindingsByName(std::string_view{nameBuf});
    return b && !isBusCompBinding(b);
}
bool isUf8OnlyFx_(MediaTrack* tr, int fxIdx, char* nameBuf, int nameBufSz)
{
    if (!uf8::fxIdentityName(tr, fxIdx, nameBuf, nameBufSz)) return false;
    const auto* um = uf8::user_plugins::lookupOwnedByName(nameBuf);
    return um && um->domain == uf8::Domain::None && um->uf8Mode;
}

// Walk `tr` and return the ordinal of the FX whose fxGuid matches
// `wantFxGuid` among the slots passing `predicate`. Returns -1 when
// not found (FX deleted / chunk-replaced / never present). 0 = first
// match, 1 = second, etc.
template <typename Pred>
int findOrdinalOfFxGuid_(MediaTrack* tr, const std::string& wantFxGuid, Pred pred)
{
    if (!tr || wantFxGuid.empty()) return -1;
    if (!ValidatePtr2(nullptr, tr, "MediaTrack*")) return -1;
    const int n = TrackFX_GetCount(tr);
    int seen = 0;
    char buf[256];
    for (int i = 0; i < n; ++i) {
        if (!pred(tr, i, buf, sizeof(buf))) continue;
        if (uf8::fxGuidString(tr, i) == wantFxGuid) return seen;
        ++seen;
    }
    return -1;
}

// Walk `tr` and return the fxGuid of the FX at ordinal `ord` among
// slots passing `predicate`. Empty string when ordinal is out of
// range or `tr` has no matches. Used by setBcInstanceIndex etc. to
// translate from "I want the N-th BC" into the FX-GUID we'll store.
template <typename Pred>
std::string findFxGuidAtOrdinal_(MediaTrack* tr, int ord, Pred pred)
{
    if (!tr || ord < 0) return {};
    if (!ValidatePtr2(nullptr, tr, "MediaTrack*")) return {};
    const int n = TrackFX_GetCount(tr);
    int seen = 0;
    char buf[256];
    for (int i = 0; i < n; ++i) {
        if (!pred(tr, i, buf, sizeof(buf))) continue;
        if (seen == ord) return uf8::fxGuidString(tr, i);
        ++seen;
    }
    return {};
}

int readInstanceOrdinal_(
    void* trackRaw,
    std::unordered_map<std::string, std::string>& storage,
    bool isBc)
{
    MediaTrack* tr = static_cast<MediaTrack*>(trackRaw);
    if (!tr) return 0;
    if (!ValidatePtr2(nullptr, tr, "MediaTrack*")) return 0;
    const std::string g = trackGuid_(tr);
    if (g.empty()) return 0;
    std::string wantFxGuid;
    {
        std::lock_guard<std::mutex> lk(g_instanceMutex);
        auto it = storage.find(g);
        if (it == storage.end()) return 0;
        wantFxGuid = it->second;
    }
    if (wantFxGuid.empty()) return 0;
    const int ord = isBc
        ? findOrdinalOfFxGuid_(tr, wantFxGuid, isBcFx_)
        : findOrdinalOfFxGuid_(tr, wantFxGuid, isCsFx_);
    return ord < 0 ? 0 : ord;
}

void writeInstanceOrdinal_(
    void* trackRaw,
    int idx,
    std::unordered_map<std::string, std::string>& storage,
    bool isBc)
{
    MediaTrack* tr = static_cast<MediaTrack*>(trackRaw);
    if (!tr) return;
    if (!ValidatePtr2(nullptr, tr, "MediaTrack*")) return;
    const std::string g = trackGuid_(tr);
    if (g.empty()) return;
    if (idx < 0) idx = 0;
    const std::string fxg = isBc
        ? findFxGuidAtOrdinal_(tr, idx, isBcFx_)
        : findFxGuidAtOrdinal_(tr, idx, isCsFx_);
    std::lock_guard<std::mutex> lk(g_instanceMutex);
    if (fxg.empty()) storage.erase(g);
    else             storage[g] = fxg;
}

} // namespace

namespace {
InstanceChangedFn g_instanceChangedFn = nullptr;
} // namespace

void setInstanceChangedCallback(InstanceChangedFn fn) { g_instanceChangedFn = fn; }

int bcInstanceIndex(void* trackRaw)
{
    return readInstanceOrdinal_(trackRaw, g_bcInstanceFxGuid, /*isBc*/ true);
}
int csInstanceIndex(void* trackRaw)
{
    return readInstanceOrdinal_(trackRaw, g_csInstanceFxGuid, /*isBc*/ false);
}

void setBcInstanceIndex(void* trackRaw, int idx)
{
    writeInstanceOrdinal_(trackRaw, idx, g_bcInstanceFxGuid, /*isBc*/ true);
    if (g_instanceChangedFn) g_instanceChangedFn(trackRaw);
}
void setCsInstanceIndex(void* trackRaw, int idx)
{
    writeInstanceOrdinal_(trackRaw, idx, g_csInstanceFxGuid, /*isBc*/ false);
    if (g_instanceChangedFn) g_instanceChangedFn(trackRaw);
}

int uf8OnlyInstanceIndex(void* trackRaw)
{
    MediaTrack* tr = static_cast<MediaTrack*>(trackRaw);
    if (!tr || !ValidatePtr2(nullptr, tr, "MediaTrack*")) return 0;
    const std::string g = trackGuid_(tr);
    if (g.empty()) return 0;
    std::string wantFxGuid;
    {
        std::lock_guard<std::mutex> lk(g_instanceMutex);
        auto it = g_uf8OnlyInstanceFxGuid.find(g);
        if (it == g_uf8OnlyInstanceFxGuid.end()) return 0;
        wantFxGuid = it->second;
    }
    if (wantFxGuid.empty()) return 0;
    const int ord = findOrdinalOfFxGuid_(tr, wantFxGuid, isUf8OnlyFx_);
    return ord < 0 ? 0 : ord;
}

void setUf8OnlyInstanceIndex(void* trackRaw, int idx)
{
    MediaTrack* tr = static_cast<MediaTrack*>(trackRaw);
    if (!tr || !ValidatePtr2(nullptr, tr, "MediaTrack*")) return;
    const std::string g = trackGuid_(tr);
    if (g.empty()) return;
    if (idx < 0) idx = 0;
    const std::string fxg = findFxGuidAtOrdinal_(tr, idx, isUf8OnlyFx_);
    std::lock_guard<std::mutex> lk(g_instanceMutex);
    if (fxg.empty()) g_uf8OnlyInstanceFxGuid.erase(g);
    else             g_uf8OnlyInstanceFxGuid[g] = fxg;
}

// Count UF8-only mapped plug-ins (domain==None, uf8Mode==true) on the
// track. Walks user_plugins directly — these maps don't appear in the
// UC1 user-binding cache, so isBusCompBinding / lookupBindingsByName
// won't find them.
int uf8OnlyInstanceCount(void* trackRaw)
{
    MediaTrack* tr = static_cast<MediaTrack*>(trackRaw);
    if (!tr || !ValidatePtr2(nullptr, tr, "MediaTrack*")) return 0;
    const int n = TrackFX_GetCount(tr);
    char buf[256];
    int count = 0;
    for (int i = 0; i < n; ++i) {
        if (!uf8::fxIdentityName(tr, i, buf, sizeof(buf))) continue;
        const auto* um = uf8::user_plugins::lookupOwnedByName(buf);
        if (!um) continue;
        if (um->domain == uf8::Domain::None && um->uf8Mode) ++count;
    }
    return count;
}

int instanceIndexForFx(void* trackRaw, int fxIdx)
{
    MediaTrack* tr = static_cast<MediaTrack*>(trackRaw);
    if (!tr || fxIdx < 0) return -1;
    if (!ValidatePtr2(nullptr, tr, "MediaTrack*")) return -1;
    const int n = TrackFX_GetCount(tr);
    if (fxIdx >= n) return -1;

    char nameTarget[256] = {0};
    if (!uf8::fxIdentityName(tr, fxIdx, nameTarget, sizeof(nameTarget))) return -1;
    const PluginBindings* bTarget = lookupBindingsByName(std::string_view{nameTarget});
    if (!bTarget) return -1;
    const bool isBc = isBusCompBinding(bTarget);

    int seen = 0;
    char buf[256];
    for (int i = 0; i < n; ++i) {
        if (!uf8::fxIdentityName(tr, i, buf, sizeof(buf))) continue;
        const PluginBindings* b = lookupBindingsByName(std::string_view{buf});
        if (!b) continue;
        if (isBusCompBinding(b) != isBc) continue;
        if (i == fxIdx) return seen;
        ++seen;
    }
    return -1;
}

int fxIndexForInstance(void* trackRaw, bool bc, int ordinal)
{
    MediaTrack* tr = static_cast<MediaTrack*>(trackRaw);
    if (!tr || ordinal < 0) return -1;
    if (!ValidatePtr2(nullptr, tr, "MediaTrack*")) return -1;
    const int n = TrackFX_GetCount(tr);
    int seen = 0;
    char buf[256];
    for (int i = 0; i < n; ++i) {
        const bool match = bc ? isBcFx_(tr, i, buf, sizeof(buf))
                              : isCsFx_(tr, i, buf, sizeof(buf));
        if (!match) continue;
        if (seen == ordinal) return i;
        ++seen;
    }
    return -1;
}

} // namespace uc1
