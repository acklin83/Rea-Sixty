// JogGrid: the UF1 jog in Grid unit never falls between the grid (Frank 27.09.2026).
// The de-jitter hands on two detents at a time on a slow turn; this replays exactly
// that and checks that every bar is hit, also after a 7/8 bar, and finer with Shift.

#include "JogGrid.h"

#include <cmath>
#include <cstdio>
#include <vector>

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

static bool near(double a, double b) { return std::fabs(a - b) < 1e-6; }

// Measures from a list of lengths in QN, the last one repeating.
static jog::MeasureAt measures(std::vector<double> lens)
{
    return [lens](double qn) {
        double s = 0.0;
        for (std::size_t i = 0;; ++i) {
            const double len = lens[i < lens.size() ? i : lens.size() - 1];
            if (qn < s + len - 1e-12) return jog::Measure{ s, s + len };
            s += len;
        }
    };
}

// uf1JogDeJitter_ with the factory deadzone of 2: whole counts once |accum| >= 2.
static std::vector<int> dejitter(const std::vector<int>& counts, double dz = 2.0)
{
    std::vector<int> out;
    double accum = 0.0;
    for (int c : counts) {
        if ((c > 0 && accum < 0.0) || (c < 0 && accum > 0.0)) accum = 0.0;
        accum += c;
        if (std::fabs(accum) < dz) continue;
        const int eff = static_cast<int>(accum);
        accum -= eff;
        out.push_back(eff);
    }
    return out;
}

// Turn the wheel slowly, one detent at a time; collect where the cursor stops.
static std::vector<double> slowTurn(double startQN, int detents, double cell, double line,
                                    const jog::MeasureAt& m)
{
    std::vector<double> at;
    double qn = startQN;
    const std::size_t count = static_cast<std::size_t>(detents > 0 ? detents : -detents);
    for (int eff : dejitter(std::vector<int>(count, detents > 0 ? 1 : -1)))
        at.push_back(qn = jog::move(qn, eff * cell, cell, line, m));
    return at;
}

static bool visits(const std::vector<double>& at, double qn)
{
    for (double a : at) if (near(a, qn)) return true;
    return false;
}

int main()
{
    const auto fourFour = measures({ 4.0 });
    // Factory: grid 1/4 note (1 QN), Playhead step 0.25 -> cells of a sixteenth.
    const double cell = 0.25, line = 1.0;

    // ── the forum case: one sixteenth past a bar, a slow turn forward ────────
    {
        const auto at = slowTurn(4.25, 60, cell, line, fourFour);   // bar 2 + 1/16
        EXPECT(visits(at, 8.0) && visits(at, 12.0) && visits(at, 16.0));   // bars 3, 4, 5
        // It never stands between cells.
        bool onCells = true;
        for (double a : at) onCells = onCells && near(std::round(a / cell) * cell, a);
        EXPECT(onCells);
        // Two cells a step: 4.25 -> 4.75, then onto the beat at 5.0 instead of
        // jumping to 5.25, and from there on even cells.
        EXPECT(!at.empty() && near(at[0], 4.75));
        EXPECT(at.size() > 1 && near(at[1], 5.0));
    }
    // …and backwards.
    {
        const auto at = slowTurn(15.75, -80, cell, line, fourFour);
        EXPECT(visits(at, 12.0) && visits(at, 8.0) && visits(at, 4.0) && visits(at, 0.0));
    }

    // ── a 7/8 bar in bar 1: bars then start at 3.5, 7.5, 11.5 ────────────────
    {
        const auto m = measures({ 3.5, 4.0 });
        const auto at = slowTurn(0.0, 60, cell, line, m);
        EXPECT(visits(at, 3.5) && visits(at, 7.5) && visits(at, 11.5));
        // The grid lines count from the bar: 4.5 is one, and a fast move from 3.5
        // stops there (from the project start it would have stopped at 4.0).
        EXPECT(near(jog::move(3.5, 3.0, cell, line, m), 4.5));
    }

    // ── Shift: cells and lines both finer by the fine divisor (4) ────────────
    {
        const double fine = 4.0;
        const auto at = slowTurn(4.0625, 60, cell / fine, line / fine, fourFour);   // + a 64th
        // Every sixteenth line is reached.
        EXPECT(visits(at, 4.25) && visits(at, 4.5) && visits(at, 4.75) && visits(at, 5.0));
    }

    // ── a fast spin still moves, and stops on a line it would cross ──────────
    {
        const double to = jog::move(4.25, 12 * cell, cell, line, fourFour);
        EXPECT(near(to, 5.0));
        const double on = jog::move(5.0, 4 * cell, cell, line, fourFour);   // from a line
        EXPECT(near(on, 6.0));
    }

    // ── a step bigger than the grid is not cut short ─────────────────────────
    {
        EXPECT(near(jog::move(0.0, 2.0, 2.0, 1.0, fourFour), 2.0));
    }

    // ── the nav arrows: one grid line, from the bar ─────────────────────────
    {
        const auto m = measures({ 3.5, 4.0 });
        EXPECT(near(jog::stepLine(3.0, +1, 1.0, m), 3.5));   // onto the bar, not 4.0
        EXPECT(near(jog::stepLine(3.5, +1, 1.0, m), 4.5));
        EXPECT(near(jog::stepLine(3.5, -1, 1.0, m), 3.0));
        EXPECT(near(jog::stepLine(4.2, -1, 1.0, m), 3.5));   // between lines: nearest back
        EXPECT(near(jog::stepLine(0.0, -1, 1.0, m), 0.0));
    }

    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("jog grid ok\n");
    return 0;
}
