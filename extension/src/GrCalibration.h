#pragma once
//
// GrCalibration — per-breakpoint piecewise-linear correction for the
// gain-reduction display. Shared by UC1Surface (BC VU motor + DYN GR
// LEDs), main.cpp (UF8 GR byte), and SettingsScreen (FX Learn editor
// live preview), so all three stay in lock-step at every dB value.
//
// Each plug-in's UserMetering carries two tables — one per renderer
// scale. The tables sample correction offsets at the renderer's native
// dB ticks; between ticks the offset is lerped linearly; above the top
// tick the offset is held flat (extrapolation = constant tail).
//

namespace uf8 {

// BC VU mechanical needle: major ticks on the meter face.
inline constexpr double kBcVuBpDb[6] = {0.0, 4.0, 8.0, 12.0, 16.0, 20.0};
inline constexpr int    kBcVuBpCount = 6;

// DYN GR LEDs (UC1 Comp 0x5C..0x60 + Gate 0x61..0x65) and UF8 GR row:
// SSL plug-in piecewise segment boundaries. Matches the piecewise
// dB→sub-step formula in UC1Surface::pushGainReduction.
inline constexpr double kLedsBpDb[5] = {3.0, 6.0, 10.0, 14.0, 20.0};
inline constexpr int    kLedsBpCount = 5;

// Apply a piecewise-linear correction to a raw dB reading.
//   bp[]  — strictly increasing breakpoint dB values
//   off[] — correction offset in dB at each breakpoint (default 0 ⇒ identity)
//   n     — count of breakpoints (== length of bp[] and off[])
// All-zero `off` returns rawDb unchanged. Below bp[0] the function
// returns rawDb + off[0] — Frank may dial off[0] non-zero on BC VU to
// re-anchor the rest position when a plug-in's "no compression" output
// drifts. Above bp[n-1] the correction is held flat at off[n-1].
inline double applyGrCalibration(double rawDb,
                                 const double* bp,
                                 const double* off,
                                 int           n)
{
    if (!bp || !off || n <= 0) return rawDb;
    if (rawDb <= bp[0])      return rawDb + off[0];
    if (rawDb >= bp[n - 1])  return rawDb + off[n - 1];
    for (int i = 0; i < n - 1; ++i) {
        if (rawDb <= bp[i + 1]) {
            const double span = bp[i + 1] - bp[i];
            const double t    = (span > 0.0) ? (rawDb - bp[i]) / span : 0.0;
            const double o    = off[i] + t * (off[i + 1] - off[i]);
            return rawDb + o;
        }
    }
    return rawDb;
}

} // namespace uf8
