#pragma once
//
// Colour maths for Hue Mode. Pure arithmetic — no sockets, no REAPER, no JSON.
// Unit-tested in tests/test_hue.cpp.
//
// WHY A FILE OF ITS OWN. A Hue light is addressed in CIE xy, a V-Pot produces a
// hue angle, and the UF8 colour bar wants 0xRRGGBB. Three representations, six
// conversions, and every one of them is the kind of thing that is either right
// or off by a whole colour with no middle ground. Keeping it separate means the
// test can pin the numbers without a bridge in the room.
//
// WHERE THE MATRICES COME FROM. The RGB↔XYZ pair below is the "Wide RGB D65"
// conversion Philips published with their own sample code; the coefficients here
// were read out of Home Assistant's homeassistant/util/color.py
// (color_RGB_to_xy_brightness / color_xy_brightness_to_RGB), not recalled. They
// are NOT the sRGB matrices and substituting those shifts every colour.
//
// ⛔ THE ROUND TRIP IS NOT LOSSLESS AND MUST NOT CARRY CONTROL STATE.
// hueSatToXy → xyToHueSat comes back close but not equal: the gamma steps, the
// gamut clamp and the light's own mapping all round. So the surface keeps hue
// and saturation as ITS state and converts outwards only. xyToHueSat exists to
// answer "what is that light showing right now", never to read back what we
// just sent — feeding it into the V-Pot value would make the pot creep on its
// own every time the light reports in.

#include <cstdint>

namespace uf8::hue {

// A CIE chromaticity, both components 0..1. This is what the bridge speaks:
// "color": {"xy": {"x": 0.3, "y": 0.32}}.
struct Xy {
    double x = 0.0;
    double y = 0.0;
};

// The colour triangle one light can physically reach, straight out of its
// GET /clip/v2/resource/light entry ("color": {"gamut": {red, green, blue}}).
// `valid` is false for a light that reports no gamut (older colour lights, and
// every white-only light) — then nothing is clamped.
struct Gamut {
    Xy   red;
    Xy   green;
    Xy   blue;
    bool valid = false;
};

// ---- primitives -------------------------------------------------------------

// sRGB components 0..1 → chromaticity. Brightness is dropped: xy says WHICH
// colour, the bridge's `dimming` says how much of it.
Xy rgbToXy(double r, double g, double b);

// Chromaticity → sRGB components 0..1, scaled so the brightest component is 1.
// That is deliberate: the colour bar should show the hue of a lamp at 5 percent
// the same as at 100, and the dimness is already in the strip's fader.
void xyToRgb(Xy xy, double* r, double* g, double* b);

// Hue angle in degrees (0 red, 120 green, 240 blue; any value, wrapped) and
// saturation 0..1 → chromaticity. Saturation 0 lands on the white point.
Xy hueSatToXy(double hueDeg, double sat);

// The inverse, for DISPLAY only — see the warning at the top of this file.
// Returns the angle folded into 0..360.
void xyToHueSat(Xy xy, double* hueDeg, double* sat);

// 0xRRGGBB for the UF8 colour bar, ready for uf8::quantize (Palette.h).
uint32_t xyToRgb24(Xy xy);

// ---- gamut ------------------------------------------------------------------

// Is the chromaticity inside the light's triangle? Always true when the gamut
// is not valid (nothing known to be outside).
bool inGamut(Xy xy, const Gamut& g);

// The nearest reachable chromaticity: `xy` itself when it is inside, otherwise
// the closest point on the triangle's edge. A bridge clamps for us, but it
// clamps silently — doing it here is what lets the surface show the colour the
// lamp will ACTUALLY produce instead of the one we asked for.
Xy clampToGamut(Xy xy, const Gamut& g);

// ---- colour temperature -----------------------------------------------------

// Hue speaks mireds ("mirek"), 153 = coldest, 500 = warmest, and rejects
// anything outside. `warm01` runs 0 (cold) to 1 (warm) so a V-Pot can drive it
// the way every other 0..1 control on the surface is driven.
inline constexpr int kMirekMin = 153;
inline constexpr int kMirekMax = 500;
int mirekFromWarmth(double warm01);
double warmthFromMirek(int mirek);

// Roughly what a colour temperature looks like, for the colour bar and the
// settings preview. An approximation on purpose: nobody is colour-grading off a
// 16-entry LED palette.
uint32_t mirekToRgb24(int mirek);

} // namespace uf8::hue
