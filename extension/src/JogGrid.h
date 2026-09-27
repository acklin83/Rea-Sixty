#pragma once
//
// JogGrid: where the UF1 jog puts the play cursor in Grid unit. Pure arithmetic in
// quarter notes; the caller answers "which measure is this QN in" (REAPER's
// TimeMap_QNToMeasures in main.cpp, a table in tests/test_jog_grid.cpp).
//
// ⇨ THE CURSOR CANNOT FALL BETWEEN THE GRID (Frank 27.09.2026, forum report: the jog
// "sometimes doesn't want to snap to the beginning of a bar"). Two causes, both here:
//
//  1. The de-jitter in front (uf1JogDeJitter_) holds a single detent back and hands
//     on two at once, so a slow turn moves two cells per step. From an odd cell it
//     then walked 19, 21, 23 ... and jumped over every bar at 16, 32, 48. Now a move
//     never crosses a LINE: if one lies between the cursor and where it would land,
//     the cursor stops on it.
//  2. Cells were counted in QN from the project start, so after a 7/8 or any bar
//     that is not a whole number of cells long, no cell sat on the bar line. Now the
//     grid starts again at every measure, and the measure start is always a line.
//
// Shift makes both finer by the same divisor (Frank: "mit shift natürlich auch den
// einstellungen entsprechend kleiner"): cell = grid × step ÷ fine, line = grid ÷ fine.
//
#include <cmath>
#include <functional>

namespace jog {

struct Measure {
    double startQN = 0.0;
    double endQN   = 4.0;
};
using MeasureAt = std::function<Measure(double qn)>;

constexpr double kEps = 1e-9;

// Nearest multiple of `size` from the start of `m`; the measure end counts as a
// place too (it is the next measure's start).
inline double snapInMeasure(double qn, double size, const Measure& m)
{
    if (!(size > 0.0)) return qn;
    double r = m.startQN + std::round((qn - m.startQN) / size) * size;
    if (r > m.endQN - kEps) r = m.endQN;
    if (r < m.startQN) r = m.startQN;
    return r;
}

// The first line strictly past `qn` in direction `dir` (+1 / -1).
inline double nextLine(double qn, int dir, double line, const MeasureAt& measureAt)
{
    if (dir > 0) {
        const Measure m = measureAt(qn);
        const double k = std::floor((qn - m.startQN) / line + kEps) + 1.0;
        const double l = m.startQN + k * line;
        return (l > m.endQN - kEps) ? m.endQN : l;
    }
    // Going back from a measure start means the measure before it.
    const Measure m = measureAt(qn - 1e-6);
    const double k = std::ceil((qn - m.startQN) / line - kEps) - 1.0;
    const double l = m.startQN + k * line;
    return (l < m.startQN) ? m.startQN : l;
}

// Where the cursor goes from `curQN` when the wheel asks for `deltaQN`. `cellQN` is
// the step (grid × amount, Shift-divided), `lineQN` the grid (Shift-divided). A cell
// larger than a line makes the cell the line, so a big step is not cut short.
inline double move(double curQN, double deltaQN, double cellQN, double lineQN,
                   const MeasureAt& measureAt)
{
    if (!(cellQN > 0.0)) return curQN + deltaQN;
    const double line = (lineQN > cellQN) ? lineQN : cellQN;
    const double want = curQN + deltaQN;
    double to = snapInMeasure(want, cellQN, measureAt(want));
    if (to < 0.0) to = 0.0;
    const int dir = (to > curQN + kEps) ? 1 : (to < curQN - kEps) ? -1 : 0;
    if (dir == 0) return to;
    const double l = nextLine(curQN, dir, line, measureAt);
    if (dir > 0 && l < to - kEps) return l;
    if (dir < 0 && l > to + kEps) return l;
    return to;
}

// One grid line on in `dir`, for the nav arrows: onto the next line, or onto the
// nearest one in that direction when the cursor sits between two.
inline double stepLine(double curQN, int dir, double lineQN, const MeasureAt& measureAt)
{
    if (!(lineQN > 0.0) || dir == 0) return curQN;
    const double l = nextLine(curQN, dir > 0 ? 1 : -1, lineQN, measureAt);
    return l < 0.0 ? 0.0 : l;
}

} // namespace jog
