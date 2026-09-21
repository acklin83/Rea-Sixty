#include "Uf1EqCurve.h"

#include <cmath>

namespace uf1eq {
namespace {

// ⛔ COPIED VERBATIM out of main.cpp, not re-derived. These five are
// capture-fitted display curves, and the first draft of this file replaced the
// peaking formula with a Gaussian that "looked like a bell" — the exact class
// of mistake the project rule about greppíng ingredients exists to stop. If any
// of them ever needs changing, it changes here and nowhere else, because
// main.cpp now calls into this file.

// Analog peaking-EQ magnitude (dB) at f for a bell centred at f0, gain g dB, Q.
double peakDb(double f, double f0, double g, double q)
{
    if (g == 0.0 || f0 <= 0.0 || q <= 0.0) return 0.0;
    const double A  = std::pow(10.0, g / 40.0);
    const double w2 = f * f, w0 = f0 * f0;
    const double d  = w0 - w2;
    const double num = std::hypot(d, f * f0 * A / q);
    const double den = std::hypot(d, f * f0 / (A * q));
    return 20.0 * std::log10(num / den);
}
// 1st-order-ish shelves (display curves): smooth g->0 transition around f0.
double lowShelfDb(double f, double f0, double g)
{
    if (g == 0.0 || f0 <= 0.0) return 0.0;
    return g / (1.0 + (f / f0) * (f / f0));
}
double highShelfDb(double f, double f0, double g)
{
    if (g == 0.0 || f0 <= 0.0) return 0.0;
    return g / (1.0 + (f0 / f) * (f0 / f));
}
// 2nd-order filter roll-offs (≈12 dB/oct) — only meaningful away from the rail.
//
// ⛔ THE RAILS ARE THE SAFETY, NOT DECORATION. A wrong parameter grab or a
// mis-scaled unit must never slam the whole graph onto the floor (the
// 2026-06-18 regression): a high pass at or under 9 Hz and a low pass at or
// over 19 kHz count as disengaged and contribute nothing.
//
// ⇨ `order` is the Butterworth order n in |H|^2 = 1 / (1 + r^2n), so the slope
// far from the corner is n x 6 dB/oct. It was always 2 here, and 2 is still the
// default: r^4 is exactly the old expression, so the SSL/REAPER graph does not
// move by a pixel. TotalMix' low cut sets 1..4 (slope index 0..3 = 6/12/18/24
// dB/oct, measured 2026-09-21).
double rollOff(double r, int order)
{
    if (order < 1) order = 1;
    if (order > 8) order = 8;
    return -10.0 * std::log10(1.0 + std::pow(r, 2.0 * order));
}
double hpfDb(double f, double fc, int order)
{
    if (fc <= 9.0) return 0.0;                        // below the control min (10 Hz) = off
    return rollOff(fc / f, order);
}
double lpfDb(double f, double fc, int order)
{
    if (fc >= 19000.0) return 0.0;                    // at/over the top rail = off
    return rollOff(f / fc, order);
}

}  // namespace

double bandDb(const Band& b, double f)
{
    switch (b.kind) {
        case Band::Kind::Bell:      return peakDb(f, b.freq, b.gainDb, b.q);
        case Band::Kind::LowShelf:  return lowShelfDb(f, b.freq, b.gainDb);
        case Band::Kind::HighShelf: return highShelfDb(f, b.freq, b.gainDb);
        case Band::Kind::HighPass:  return hpfDb(f, b.freq, b.order);
        case Band::Kind::LowPass:   return lpfDb(f, b.freq, b.order);
    }
    return 0.0;
}

std::uint8_t dbToHeight(double db)
{
    if (!std::isfinite(db)) db = 0.0;
    double h = 100.0 + db * 5.44;
    if (h < 0.0)   h = 0.0;
    if (h > 199.0) h = 199.0;
    return static_cast<std::uint8_t>(h + 0.5);
}

void render(const Model& m, std::array<std::uint8_t, 251>& col,
            std::uint8_t& tail)
{
    if (!m.on) { col.fill(100); return; }
    for (int x = 2; x < 252; ++x) {
        const double frac = (x - 2) / 249.0;
        const double f    = 20.0 * std::pow(1000.0, frac);
        double db = 0.0;
        for (const Band& b : m.bands) db += bandDb(b, f);
        const std::uint8_t h = dbToHeight(db);
        if (x < 251) col[static_cast<std::size_t>(x)] = h; else tail = h;
    }
}

}  // namespace uf1eq
