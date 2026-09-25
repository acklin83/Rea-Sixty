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
// ⇨ TOTALMIX' SHELVES: the Audio EQ Cookbook's analog shelving prototypes
// (RBJ), A = 10^(g/40), s = jf/f0, f0 the middle of the step. Fitted against
// nine TotalMix screenshots on 25.09.2026, low shelf Q 0.4..9.9 and a +20 dB
// high shelf: 0.17..0.65 dB RMS with the knob values as they are, which is one
// to two pixels of TotalMix' own line. The high shelf is the low one mirrored.
double cookbookLowShelfDb(double f, double f0, double g, double q)
{
    if (g == 0.0 || f0 <= 0.0 || q <= 0.0 || f <= 0.0) return 0.0;
    const double A  = std::pow(10.0, g / 40.0);
    const double w  = f / f0;                 // s = jw
    const double k  = std::sqrt(A) / q;
    const double w2 = w * w;
    // H(s) = A (s^2 + k s + A) / (A s^2 + k s + 1)
    const double numRe = A - w2, numIm = k * w;
    const double denRe = 1.0 - A * w2, denIm = k * w;
    return 20.0 * std::log10(A * std::hypot(numRe, numIm) / std::hypot(denRe, denIm));
}
double cookbookHighShelfDb(double f, double f0, double g, double q)
{
    if (g == 0.0 || f0 <= 0.0 || q <= 0.0 || f <= 0.0) return 0.0;
    const double A  = std::pow(10.0, g / 40.0);
    const double w  = f / f0;
    const double k  = std::sqrt(A) / q;
    const double w2 = w * w;
    // H(s) = A (A s^2 + k s + 1) / (s^2 + k s + A)
    const double numRe = 1.0 - A * w2, numIm = k * w;
    const double denRe = A - w2,       denIm = k * w;
    return 20.0 * std::log10(A * std::hypot(numRe, numIm) / std::hypot(denRe, denIm));
}
// ⇨ AND TOTALMIX' BAND FILTERS (band 1 and 3, types HiPass / LoPass): the
// Cookbook's second-order low and high pass, Q the resonance. Checked by eye
// against Frank's low-pass screenshots at 5 kHz, Q 0.4..9.9 (Q 4.1 peaks near
// +12 dB, Q 9.9 near +20); not fitted pixel by pixel like the shelves.
double cookbookLowPassDb(double f, double f0, double q)
{
    if (f0 <= 0.0 || q <= 0.0 || f <= 0.0) return 0.0;
    const double w = f / f0, a = 1.0 - w * w, b = w / q;
    return -10.0 * std::log10(a * a + b * b);
}
double cookbookHighPassDb(double f, double f0, double q)
{
    if (f0 <= 0.0 || q <= 0.0 || f <= 0.0) return 0.0;
    return cookbookLowPassDb(f0 * f0 / f, f0, q);   // w -> 1/w
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
        case Band::Kind::LowShelf:
            return b.totalMix ? cookbookLowShelfDb(f, b.freq, b.gainDb, b.q)
                            : lowShelfDb(f, b.freq, b.gainDb);
        case Band::Kind::HighShelf:
            return b.totalMix ? cookbookHighShelfDb(f, b.freq, b.gainDb, b.q)
                            : highShelfDb(f, b.freq, b.gainDb);
        case Band::Kind::HighPass:
            return b.totalMix ? cookbookHighPassDb(f, b.freq, b.q) : hpfDb(f, b.freq, b.order);
        case Band::Kind::LowPass:
            return b.totalMix ? cookbookLowPassDb(f, b.freq, b.q) : lpfDb(f, b.freq, b.order);
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
