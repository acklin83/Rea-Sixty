#include "HueColor.h"

#include <algorithm>
#include <cmath>

namespace uf8::hue {
namespace {

double clamp01(double v)
{
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

// sRGB companding, forwards: display value → linear light.
double toLinear(double v)
{
    v = clamp01(v);
    return (v > 0.04045) ? std::pow((v + 0.055) / 1.055, 2.4)
                         : (v / 12.92);
}

// …and back.
double fromLinear(double v)
{
    if (v <= 0.0) return 0.0;
    const double out = (v <= 0.0031308) ? (12.92 * v)
                                        : (1.055 * std::pow(v, 1.0 / 2.4) - 0.055);
    return clamp01(out);
}

uint8_t to8(double v)
{
    const int i = static_cast<int>(clamp01(v) * 255.0 + 0.5);
    return static_cast<uint8_t>(i < 0 ? 0 : (i > 255 ? 255 : i));
}

// Squared distance, so the closest-edge search never needs a square root.
double dist2(Xy a, Xy b)
{
    const double dx = a.x - b.x, dy = a.y - b.y;
    return dx * dx + dy * dy;
}

// Closest point to `p` on the segment a..b. Degenerate segment → a.
Xy closestOnSegment(Xy a, Xy b, Xy p)
{
    const double abx = b.x - a.x, aby = b.y - a.y;
    const double len2 = abx * abx + aby * aby;
    if (len2 <= 0.0) return a;
    double t = ((p.x - a.x) * abx + (p.y - a.y) * aby) / len2;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    return Xy{ a.x + t * abx, a.y + t * aby };
}

// Sign of the cross product (b-a) × (p-a): which side of the edge p is on.
double side(Xy a, Xy b, Xy p)
{
    return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
}

// Hue angle + saturation → sRGB with value fixed at 1. Plain HSV, written out
// rather than pulled from anywhere: the surface never needs the value axis, the
// fader owns brightness.
void hsvToRgb(double hueDeg, double sat, double* r, double* g, double* b)
{
    double h = std::fmod(hueDeg, 360.0);
    if (h < 0.0) h += 360.0;
    sat = clamp01(sat);

    const double sector = h / 60.0;
    const int    i      = static_cast<int>(std::floor(sector)) % 6;
    const double f      = sector - std::floor(sector);
    const double p      = 1.0 - sat;
    const double q      = 1.0 - sat * f;
    const double t      = 1.0 - sat * (1.0 - f);

    switch (i) {
        case 0:  *r = 1.0; *g = t;   *b = p;   break;
        case 1:  *r = q;   *g = 1.0; *b = p;   break;
        case 2:  *r = p;   *g = 1.0; *b = t;   break;
        case 3:  *r = p;   *g = q;   *b = 1.0; break;
        case 4:  *r = t;   *g = p;   *b = 1.0; break;
        default: *r = 1.0; *g = p;   *b = q;   break;
    }
}

} // namespace

// ---- primitives -------------------------------------------------------------

Xy rgbToXy(double r, double g, double b)
{
    const double lr = toLinear(r), lg = toLinear(g), lb = toLinear(b);

    // Wide RGB D65 — see the header for where these came from.
    const double X = lr * 0.664511 + lg * 0.154324 + lb * 0.162028;
    const double Y = lr * 0.283881 + lg * 0.668433 + lb * 0.047685;
    const double Z = lr * 0.000088 + lg * 0.072310 + lb * 0.986039;

    const double sum = X + Y + Z;
    if (sum <= 0.0) return Xy{ 0.0, 0.0 };   // pure black has no chromaticity
    return Xy{ X / sum, Y / sum };
}

void xyToRgb(Xy xy, double* r, double* g, double* b)
{
    *r = *g = *b = 0.0;
    if (xy.y <= 0.0) return;                 // y == 0 would divide by zero below

    // Reconstruct XYZ at unit luminance; the caller wanted hue, not level.
    const double Y = 1.0;
    const double X = (Y / xy.y) * xy.x;
    const double Z = (Y / xy.y) * (1.0 - xy.x - xy.y);

    double lr =  X * 1.656492 - Y * 0.354851 - Z * 0.255038;
    double lg = -X * 0.707196 + Y * 1.655397 + Z * 0.036152;
    double lb =  X * 0.051713 - Y * 0.121364 + Z * 1.011530;

    // A chromaticity outside the sRGB triangle comes back with a negative
    // component. Lifting the whole triple to zero keeps the hue and gives up
    // the saturation, which is the honest way to show an unreachable colour.
    const double lift = std::min({ lr, lg, lb });
    if (lift < 0.0) { lr -= lift; lg -= lift; lb -= lift; }

    const double peak = std::max({ lr, lg, lb });
    if (peak <= 0.0) return;
    lr /= peak; lg /= peak; lb /= peak;

    *r = fromLinear(lr);
    *g = fromLinear(lg);
    *b = fromLinear(lb);
}

Xy hueSatToXy(double hueDeg, double sat)
{
    double r = 0.0, g = 0.0, b = 0.0;
    hsvToRgb(hueDeg, sat, &r, &g, &b);
    return rgbToXy(r, g, b);
}

void xyToHueSat(Xy xy, double* hueDeg, double* sat)
{
    double r = 0.0, g = 0.0, b = 0.0;
    xyToRgb(xy, &r, &g, &b);

    const double mx = std::max({ r, g, b });
    const double mn = std::min({ r, g, b });
    const double d  = mx - mn;

    if (hueDeg) {
        double h = 0.0;
        if (d > 0.0) {
            if      (mx == r) h = 60.0 * std::fmod((g - b) / d, 6.0);
            else if (mx == g) h = 60.0 * (((b - r) / d) + 2.0);
            else              h = 60.0 * (((r - g) / d) + 4.0);
        }
        if (h < 0.0) h += 360.0;
        *hueDeg = h;
    }
    if (sat) *sat = (mx > 0.0) ? (d / mx) : 0.0;
}

uint32_t xyToRgb24(Xy xy)
{
    double r = 0.0, g = 0.0, b = 0.0;
    xyToRgb(xy, &r, &g, &b);
    return (static_cast<uint32_t>(to8(r)) << 16)
         | (static_cast<uint32_t>(to8(g)) << 8)
         |  static_cast<uint32_t>(to8(b));
}

// ---- gamut ------------------------------------------------------------------

bool inGamut(Xy xy, const Gamut& gm)
{
    if (!gm.valid) return true;

    // Same sign on all three edges = inside. Zero counts as inside so a point
    // exactly on an edge is not pushed around by rounding.
    const double s1 = side(gm.red,   gm.green, xy);
    const double s2 = side(gm.green, gm.blue,  xy);
    const double s3 = side(gm.blue,  gm.red,   xy);

    const bool anyNeg = (s1 < 0.0) || (s2 < 0.0) || (s3 < 0.0);
    const bool anyPos = (s1 > 0.0) || (s2 > 0.0) || (s3 > 0.0);
    return !(anyNeg && anyPos);
}

Xy clampToGamut(Xy xy, const Gamut& gm)
{
    if (!gm.valid || inGamut(xy, gm)) return xy;

    const Xy a = closestOnSegment(gm.red,   gm.green, xy);
    const Xy b = closestOnSegment(gm.green, gm.blue,  xy);
    const Xy c = closestOnSegment(gm.blue,  gm.red,   xy);

    const double da = dist2(xy, a), db = dist2(xy, b), dc = dist2(xy, c);
    if (da <= db && da <= dc) return a;
    return (db <= dc) ? b : c;
}

// ---- colour temperature -----------------------------------------------------

int mirekFromWarmth(double warm01)
{
    const double t = clamp01(warm01);
    const int m = kMirekMin + static_cast<int>(
        t * static_cast<double>(kMirekMax - kMirekMin) + 0.5);
    return (m < kMirekMin) ? kMirekMin : (m > kMirekMax ? kMirekMax : m);
}

double warmthFromMirek(int mirek)
{
    if (mirek <= kMirekMin) return 0.0;
    if (mirek >= kMirekMax) return 1.0;
    return static_cast<double>(mirek - kMirekMin)
         / static_cast<double>(kMirekMax - kMirekMin);
}

uint32_t mirekToRgb24(int mirek)
{
    // Mired is a reciprocal scale: kelvin = 1e6 / mirek, so 153 → 6535 K and
    // 500 → 2000 K. The kelvin→RGB curve below is the usual piecewise fit; it is
    // an approximation and says so in the header.
    if (mirek < kMirekMin) mirek = kMirekMin;
    if (mirek > kMirekMax) mirek = kMirekMax;
    const double kelvin = 1000000.0 / static_cast<double>(mirek);
    const double t = kelvin / 100.0;

    double r, g, b;
    if (t <= 66.0) {
        r = 255.0;
        g = 99.4708025861 * std::log(t) - 161.1195681661;
        b = (t <= 19.0) ? 0.0
                        : (138.5177312231 * std::log(t - 10.0) - 305.0447927307);
    } else {
        r = 329.698727446 * std::pow(t - 60.0, -0.1332047592);
        g = 288.1221695283 * std::pow(t - 60.0, -0.0755148492);
        b = 255.0;
    }
    return (static_cast<uint32_t>(to8(r / 255.0)) << 16)
         | (static_cast<uint32_t>(to8(g / 255.0)) << 8)
         |  static_cast<uint32_t>(to8(b / 255.0));
}

} // namespace uf8::hue
