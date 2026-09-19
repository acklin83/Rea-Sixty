#pragma once
//
// Uf1EqCurve — the UF1's EQ graph as a list of bands, and the renderer that
// turns it into the 249 column heights element 0x0122 wants.
//
// ⇨ WHY THIS IS ITS OWN FILE. uf1PaintEqGraph_ in main.cpp has always been two
// halves that were never separated: a GATHERER that reads fifteen parameter
// indices off a REAPER FX, and a RENDERER that sums shelves and peaks over 249
// log-spaced columns. The renderer never knew anything about REAPER; it only
// ever needed frequencies, gains and Qs.
//
// Fixing the band set at HF / HMF / LMF / LF / HPF / LPF hid that, and a second
// source with a different shape then has nowhere to go: RME's channel EQ is
// three bands plus a low cut, and its Room EQ on an output is NINE bands.
// Both fit a list; neither fits six names.
//
// So adding a source is writing a gatherer, not a second graph — and because
// this half is pure, it is also the half that can carry a test
// (tests/test_uf1_eq.cpp).
//
// The curve maths is capture-derived and must not drift: cap73 fixes the
// height scale (0 dB = 100, +16 dB ≈ 187), and the two filters carry the
// "off" rails the painter has relied on since the 2026-06-18 regression
// (HPF ≤ 9 Hz and LPF ≥ 19 kHz mean "not engaged", so they draw nothing).
//

#include <array>
#include <cstdint>
#include <vector>

namespace uf1eq {

struct Band {
    enum class Kind : uint8_t { Bell, LowShelf, HighShelf, HighPass, LowPass };
    Kind   kind   = Kind::Bell;
    double freq   = 1000.0;
    double gainDb = 0.0;
    double q      = 0.7;   // ignored by the shelves and by the two filters
};

struct Model {
    // false = dead flat, and that is a STATEMENT ("EQ is out"), not a fallback
    // for "we found nothing". A blank graph on the right channel is correct; a
    // curve from the wrong one never is.
    bool on = false;
    // Summed in order. Six today from the SSL/REAPER gatherer, eleven once an
    // RME output's nine-band Room EQ arrives.
    std::vector<Band> bands;
};

// dB at one frequency, for one band.
double bandDb(const Band& b, double f);

// Height byte for a dB value. cap73: 0 dB = 100, +16 dB ≈ 187, clamped 0..199.
// Non-finite input reads as 0 dB (the cast would otherwise be UB).
std::uint8_t dbToHeight(double db);

// Render into the 0x0122 payload buffer. Columns live at indices 2..250 (249 of
// them, 20 Hz to 20 kHz logarithmic); indices 0..1 carry the "00 01" format
// header and are left alone here. `tail` is the 251st point, which the device
// wants in its own frame.
void render(const Model& m, std::array<std::uint8_t, 251>& col,
            std::uint8_t& tail);

}  // namespace uf1eq
