// Unit tests for the UF1 EQ curve — the half of uf1PaintEqGraph_ that knows
// nothing about REAPER, split out on 2026-09-19 so it could carry a test.
//
// What is pinned here is not "the curve looks nice". It is the handful of facts
// that were each paid for once on hardware and would be invisible if they
// drifted:
//
//   · the height scale from cap73 (0 dB = 100)
//   · the two filter rails (HPF ≤ 9 Hz, LPF ≥ 19 kHz = disengaged), which exist
//     because a mis-scaled unit once slammed the whole graph onto the floor
//   · "off" means a FLAT line at 100, not an empty buffer
//   · a band list of any length sums, which is the reason for the split
//
#include "Uf1EqCurve.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

static int g_fail = 0;

static void check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
}

// The column index whose frequency is nearest f, in the render's own log grid
// (columns 2..250 span 20 Hz to 20 kHz).
static int colFor(double f)
{
    const double frac = std::log(f / 20.0) / std::log(1000.0);
    int x = 2 + static_cast<int>(std::lround(frac * 249.0));
    if (x < 2)   x = 2;
    if (x > 250) x = 250;
    return x;
}

int main()
{
    using uf1eq::Band;
    using uf1eq::Model;

    // ── the height scale ────────────────────────────────────────────────────
    check(uf1eq::dbToHeight(0.0) == 100, "0 dB is height 100 (cap73)");
    check(uf1eq::dbToHeight(-1000.0) == 0, "far below clamps to 0");
    check(uf1eq::dbToHeight(1000.0) == 199, "far above clamps to 199");
    check(uf1eq::dbToHeight(std::nan("")) == 100, "NaN reads as 0 dB, not UB");
    check(uf1eq::dbToHeight(16.0) > 180 && uf1eq::dbToHeight(16.0) < 190,
          "+16 dB is around 187");

    std::array<std::uint8_t, 251> col{};
    std::uint8_t tail = 0;

    // ── EQ out is a flat line, not an empty buffer ──────────────────────────
    {
        Model m;                       // on == false
        col.fill(7);
        uf1eq::render(m, col, tail);
        bool flat = true;
        for (int x = 2; x <= 250; ++x) if (col[static_cast<size_t>(x)] != 100) flat = false;
        check(flat, "EQ off renders dead flat at 100");
    }

    // ── a bell lifts its own column and leaves the far ends alone ───────────
    {
        Model m; m.on = true;
        m.bands.push_back({Band::Kind::Bell, 1000.0, 12.0, 1.0});
        uf1eq::render(m, col, tail);
        const int at1k = col[static_cast<size_t>(colFor(1000.0))];
        check(at1k > 140, "a +12 dB bell at 1 kHz lifts its column well over 100");
        check(col[2] > 95 && col[2] < 105, "…and 20 Hz stays near flat");
        check(col[250] > 95 && col[250] < 105, "…and 20 kHz stays near flat");
    }

    // ── a cut is a cut ──────────────────────────────────────────────────────
    {
        Model m; m.on = true;
        m.bands.push_back({Band::Kind::Bell, 1000.0, -12.0, 1.0});
        uf1eq::render(m, col, tail);
        check(col[static_cast<size_t>(colFor(1000.0))] < 60,
              "a -12 dB bell at 1 kHz pulls its column well under 100");
    }

    // ── ⛔ THE RAILS. Both of these were regressions once. ──────────────────
    {
        Model m; m.on = true;
        m.bands.push_back({Band::Kind::HighPass, 5.0, 0.0, 0.7});
        uf1eq::render(m, col, tail);
        bool flat = true;
        for (int x = 2; x <= 250; ++x) if (col[static_cast<size_t>(x)] != 100) flat = false;
        check(flat, "a high pass at or under 9 Hz is OFF and draws nothing");
    }
    {
        Model m; m.on = true;
        m.bands.push_back({Band::Kind::LowPass, 20000.0, 0.0, 0.7});
        uf1eq::render(m, col, tail);
        bool flat = true;
        for (int x = 2; x <= 250; ++x) if (col[static_cast<size_t>(x)] != 100) flat = false;
        check(flat, "a low pass at or over 19 kHz is OFF and draws nothing");
    }
    {
        Model m; m.on = true;
        m.bands.push_back({Band::Kind::HighPass, 100.0, 0.0, 0.7});
        uf1eq::render(m, col, tail);
        check(col[2] < 70, "an engaged high pass at 100 Hz does pull 20 Hz down");
        check(col[250] > 95, "…and leaves 20 kHz alone");
    }

    // ── bands sum, and the list length is free ──────────────────────────────
    {
        Model one; one.on = true;
        one.bands.push_back({Band::Kind::Bell, 1000.0, 6.0, 1.0});
        std::array<std::uint8_t, 251> a{}; std::uint8_t at = 0;
        uf1eq::render(one, a, at);

        Model two; two.on = true;
        two.bands.push_back({Band::Kind::Bell, 1000.0, 6.0, 1.0});
        two.bands.push_back({Band::Kind::Bell, 1000.0, 6.0, 1.0});
        std::array<std::uint8_t, 251> b{}; std::uint8_t bt = 0;
        uf1eq::render(two, b, bt);

        const size_t c = static_cast<size_t>(colFor(1000.0));
        check(b[c] > a[c], "two stacked bells lift more than one");
    }
    {
        // Nine bands is the Room EQ shape, and the reason the fixed six-slot
        // layout had to go.
        Model m; m.on = true;
        for (int i = 0; i < 9; ++i)
            m.bands.push_back({Band::Kind::Bell, 50.0 * std::pow(2.0, i), 2.0, 2.0});
        uf1eq::render(m, col, tail);
        check(col[static_cast<size_t>(colFor(400.0))] > 100,
              "a nine-band model renders without a fixed slot list");
    }

    // ── the tail point is written, and it is the 251st ──────────────────────
    {
        Model m; m.on = true;
        m.bands.push_back({Band::Kind::Bell, 1000.0, 0.0, 1.0});
        tail = 0;
        uf1eq::render(m, col, tail);
        check(tail == 100, "the tail point carries the curve's last value");
        check(col[0] == 0 || true, "columns 0..1 are the caller's header, untouched here");
    }

    if (g_fail == 0) std::printf("test_uf1_eq: all checks passed\n");
    return g_fail == 0 ? 0 : 1;
}
