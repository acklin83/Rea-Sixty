#pragma once

// Knob "feel" presets — five named, globally-persisted bundles of the
// per-slot tuning fields (invert / range / sensitivity / curve / polarity /
// push-reset), independent of which param a knob is bound to. Saved + applied
// from the FX-Learn right-click menu on UC1 knobs and UF8 V-Pots so a tuned
// feel can be reused across knobs and plug-ins. Binding (vst3Param/linkIdx)
// and the param-specific display label are intentionally NOT part of a preset.
// Persisted to global ExtState (rea_sixty / knob_feel_presets) as JSON.

#include <array>
#include <string>
#include <utility>
#include <vector>

#include "UserPluginCatalog.h"   // uf8::VPotPolarity

namespace uf8 {
namespace feel_presets {

struct KnobFeel {
    bool         used        = false;
    std::string  name;
    bool         inverted    = false;
    float        rangeMin    = 0.0f;
    float        rangeMax    = 1.0f;
    float        sensitivity = 1.0f;
    std::vector<std::pair<float, float>> curvePoints;
    VPotPolarity polarity    = VPotPolarity::Unipolar;
    double       defaultNorm = 0.5;
};

constexpr int kCount = 5;

// Lazy-loaded global cache. The returned array is always kCount long;
// inspect each entry's `used` flag.
const std::array<KnobFeel, kCount>& get();

// Save a feel into a slot (forces used=true) and persist. Out-of-range
// slot is ignored.
void set(int slot, const KnobFeel& f);

// Mark a slot unused and persist.
void clear(int slot);

} // namespace feel_presets
} // namespace uf8
