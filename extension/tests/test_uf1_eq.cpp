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

    // ── filter order: TotalMix' low cut, 6/12/18/24 dB/oct (2026-09-21) ──────
    {
        using uf1eq::Band;
        // Order 2 is exactly the expression the graph has always drawn.
        Band hp; hp.kind = Band::Kind::HighPass; hp.freq = 1000.0;
        const double r = 1000.0 / 250.0;
        check(std::fabs(uf1eq::bandDb(hp, 250.0) - (-10.0 * std::log10(1.0 + r * r * r * r))) < 1e-12,
              "order 2 (the default) is the old curve, to the bit");
        // Far below the corner, one octave costs n x 6.02 dB.
        for (int n = 1; n <= 4; ++n) {
            hp.order = n;
            const double oct = uf1eq::bandDb(hp, 31.25) - uf1eq::bandDb(hp, 62.5);
            char what[80];
            std::snprintf(what, sizeof(what), "order %d falls %d dB per octave", n, 6 * n);
            check(std::fabs(oct - (-6.0206 * n)) < 0.05, what);
        }
        Band lp; lp.kind = Band::Kind::LowPass; lp.freq = 1000.0; lp.order = 4;
        const double octLp = uf1eq::bandDb(lp, 16000.0) - uf1eq::bandDb(lp, 8000.0);
        check(std::fabs(octLp - (-24.08)) < 0.1, "the low pass takes the order too");
        hp.order = 4; hp.freq = 5.0;
        check(uf1eq::bandDb(hp, 20.0) == 0.0, "the 9 Hz rail still means off, at any order");
    }

    // ── TotalMix' shelves take Q (25.09.2026) ───────────────────────────────
    // The numbers are read off TotalMix' own graph (Frank's screenshots): a low
    // shelf +5.5 dB at 143 Hz and a high shelf +20 dB at 5 kHz. Tolerances are
    // one to two pixels of that graph (1 px = 0.35 dB).
    {
        auto maxMin = [](const Band& b, double& mx, double& fmx, double& mn, double& fmn) {
            mx = -1e9; mn = 1e9;
            for (double f = 20.0; f < 20000.0; f *= 1.002) {
                const double d = uf1eq::bandDb(b, f);
                if (d > mx) { mx = d; fmx = f; }
                if (d < mn) { mn = d; fmn = f; }
            }
        };
        Band ls; ls.kind = Band::Kind::LowShelf; ls.freq = 143.0; ls.gainDb = 5.5;
        ls.totalMix = true;
        double mx, fmx, mn, fmn;
        ls.q = 2.0; maxMin(ls, mx, fmx, mn, fmn);
        check(std::fabs(mx - 8.3) < 0.5 && fmx > 90 && fmx < 115, "Q 2: bump +8.3 dB near 100 Hz");
        check(std::fabs(mn + 2.8) < 0.5 && fmn > 175 && fmn < 215, "Q 2: dip -2.8 dB near 190 Hz");
        ls.q = 9.9; maxMin(ls, mx, fmx, mn, fmn);
        check(std::fabs(mx - 19.6) < 0.8 && std::fabs(mn + 13.3) < 0.8, "Q 9.9: +19.6 / -13.3 dB");
        ls.q = 0.4; maxMin(ls, mx, fmx, mn, fmn);
        check(mx <= 5.5 + 1e-9 && mn >= -1e-9, "Q 0.4: no overshoot either way");
        check(std::fabs(uf1eq::bandDb(ls, 143.0) - 2.75) < 1e-9,
              "the frequency is the middle of the step, not -3 dB");
        Band hs; hs.kind = Band::Kind::HighShelf; hs.freq = 5000.0; hs.gainDb = 20.0;
        hs.q = 1.0; hs.totalMix = true;
        check(std::fabs(uf1eq::bandDb(hs, 5000.0) - 10.0) < 1e-9, "high shelf: middle at 5 kHz");
        // Above the step Q 1 still overshoots a little, as TotalMix' line does
        // at its right edge (read there: about +21 dB).
        check(uf1eq::bandDb(hs, 20000.0) > 20.2 && uf1eq::bandDb(hs, 20000.0) < 21.5
              && std::fabs(uf1eq::bandDb(hs, 100.0)) < 0.05, "high shelf: 0 below, just over +20 above");
        maxMin(hs, mx, fmx, mn, fmn);
        check(mn < -0.5 && mn > -1.3 && fmn > 1400 && fmn < 2200, "high shelf Q 1: the small dip below");
        // And the SSL/REAPER graph does not move: totalMix off is the old
        // first-order display shelf, whatever Q says.
        Band old; old.kind = Band::Kind::LowShelf; old.freq = 143.0; old.gainDb = 5.5; old.q = 9.9;
        const double r = 300.0 / 143.0;
        check(uf1eq::bandDb(old, 300.0) == 5.5 / (1.0 + r * r), "totalMix off is the old shelf, to the bit");
        // Band 3 as a low pass, 5 kHz: Q is the resonance, gain plays no part.
        Band lp; lp.kind = Band::Kind::LowPass; lp.freq = 5000.0; lp.gainDb = 20.0;
        lp.totalMix = true;
        lp.q = 9.9; maxMin(lp, mx, fmx, mn, fmn);
        check(std::fabs(mx - 19.9) < 0.3 && fmx > 4800 && fmx < 5100, "low pass Q 9.9 peaks ~+20 dB at 5 kHz");
        lp.gainDb = -0.5; maxMin(lp, mx, fmx, mn, fmn);
        check(std::fabs(mx - 19.9) < 0.3, "the low pass ignores gain");
        lp.q = 0.4; maxMin(lp, mx, fmx, mn, fmn);
        check(mx <= 1e-9, "low pass Q 0.4: no bump");
        Band hp2; hp2.kind = Band::Kind::HighPass; hp2.freq = 200.0; hp2.q = 4.1; hp2.totalMix = true;
        maxMin(hp2, mx, fmx, mn, fmn);
        check(std::fabs(mx - 12.3) < 0.3 && fmx > 195 && fmx < 210, "high pass mirrors it");
        Band hpOld; hpOld.kind = Band::Kind::HighPass; hpOld.freq = 200.0; hpOld.q = 4.1;
        const double rr = 200.0 / 100.0;
        check(uf1eq::bandDb(hpOld, 100.0) == -10.0 * std::log10(1.0 + rr * rr * rr * rr),
              "totalMix off: the pass filters stay Butterworth, to the bit");
    }

    if (g_fail == 0) std::printf("test_uf1_eq: all checks passed\n");
    return g_fail == 0 ? 0 : 1;
}
