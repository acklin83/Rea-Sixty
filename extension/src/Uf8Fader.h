#pragma once
//
// The UF8 motor fader as a controller: what a position frame means, and how to
// drive the motor back to a value. Pulled out of main.cpp (29.10.2026,
// docs/orc-uf8-plan.md stage 5) so ORC reads and drives the faders with the
// October fixes rather than a second copy: the end stops, the stream without
// bit 7, the re-engage after a touch. No REAPER, no device: values in, frames
// out. Test: tests/test_uf8_fader.cpp.

#include "Protocol.h"

#include <cstdint>
#include <vector>

namespace uf8fader {

// Hardware fader top: the highest pb14 the UF8 firmware emits (main.cpp has the
// calibration, kUf8FaderTopDb / pbToLinearVolume).
constexpr std::uint16_t kPbMax = 15583;

// ⛔ THE END STOPS, AS THE UF1 HAS THEM (uf1::kFaderEndSnap): both ends snap.
// Measured on Frank's UF8 2026-10-25 (uf8_fader_input.log, all 8 faders fast
// and slow to both stops). The raw readback never reaches the ends:
//  • bottom: frames with bit 7 stop at 128..142, never 0. The anchoring adds
//    the strip's own readback offset on top, so a fader on the stop wrote
//    ~130..280 to REAPER: about -100 dB, a different value on every channel
//    (forum 2026-10-25: "only -103 dB or so, each channel different").
//  • top: frames with bit 7 stop at 15389..15488, the ones without at
//    15546..15615. On a fast push the last frame before the stop is often one
//    WITHOUT bit 7, which the handler drops, so REAPER kept the last frame with
//    it, well short of +12 (Frank 2026-10-25: "bei schnellen Bewegungen").
// So a frame from EITHER stream inside these zones puts the fader on its end,
// past the anchor offset. The zones are wider than the UF1's because the stop
// itself sits up to ~100 LSB apart from fader to fader.
constexpr std::uint16_t kStopBottomHi = 200;     // bit 7 set
constexpr std::uint16_t kStopBottomLo = 320;     // bit 7 clear
constexpr std::uint16_t kStopTopHi    = 15380;   // bit 7 set
constexpr std::uint16_t kStopTopLo    = 15530;   // bit 7 clear
// How much higher a frame WITHOUT bit 7 reads than one with it, during slow
// movement: median 56..94 per fader in Frank's log of 2026-10-25 (sd 20..40).
// One value for all eight replayed as well as eight separate ones.
constexpr std::uint16_t kLoStreamOffset = 70;

// -1 = on the bottom stop, +1 = on the top stop, 0 = in between.
inline int stop(std::uint16_t rawPb, bool hiBit)
{
    if (rawPb <= (hiBit ? kStopBottomHi : kStopBottomLo)) return -1;
    if (rawPb >= (hiBit ? kStopTopHi    : kStopTopLo))    return +1;
    return 0;
}

// One FF 21 03 frame, read. pbIn = the frame in the bit-7 stream's terms (a
// frame without bit 7 shifted down by kLoStreamOffset). On a stop, from either
// stream, the frame stands for the end itself: pbOut is what gets written and
// echoed. `usable` is false for a frame without bit 7 before the touch's first
// frame with it has set the grab: there is no offset to apply yet (main.cpp,
// the FF 21 handler). A controller that never grabs (the MIDI CC layer, ORC)
// passes grabValid = false and so uses the bit-7 stream and the stops only.
struct Read {
    int           stop   = 0;
    std::uint16_t pbIn   = 0;
    std::uint16_t pbOut  = 0;
    bool          usable = true;
};

inline Read read(std::uint16_t pb14, bool hiBit, bool grabValid)
{
    Read r;
    r.stop  = stop(pb14, hiBit);
    r.pbIn  = hiBit ? pb14
                    : (pb14 > kLoStreamOffset ? static_cast<std::uint16_t>(pb14 - kLoStreamOffset)
                                              : std::uint16_t(0));
    r.pbOut = r.stop < 0 ? std::uint16_t(0) : r.stop > 0 ? kPbMax : r.pbIn;
    r.usable = hiBit || r.stop != 0 || grabValid;
    return r;
}

// The echo of a position under the hand: bit 7 set, which the firmware takes
// into its target buffer while the motor is limp, so it does not jump when the
// motor re-engages on release.
inline std::vector<std::uint8_t> echo(std::uint8_t localStrip, std::uint16_t pb)
{
    return uf8::buildFaderPosition(localStrip,
                                   static_cast<std::uint8_t>((pb & 0x7F) | 0x80),
                                   static_cast<std::uint8_t>((pb >> 7) & 0x7F));
}

// Touch down: the motor goes limp so the hand does not fight it.
inline std::vector<std::uint8_t> limp(std::uint8_t localStrip)
{
    return uf8::buildMotorEnable(localStrip, false);
}

// Drive the motor to `pb`. A limp strip takes the bit-7 echo first, then the
// enable, or the firmware drops the position (main.cpp's DynaMount, Hue, MIDI CC
// and track painters all do this; Frank 2026-06-28). `engaged` is the caller's
// per-strip flag.
inline std::vector<std::vector<std::uint8_t>> drive(std::uint8_t localStrip, std::uint16_t pb,
                                                    bool& engaged)
{
    if (pb > kPbMax) pb = kPbMax;
    const auto lsb = static_cast<std::uint8_t>(pb & 0x7F);
    const auto msb = static_cast<std::uint8_t>((pb >> 7) & 0x7F);
    std::vector<std::vector<std::uint8_t>> out;
    if (!engaged) {
        out.push_back(uf8::buildFaderPosition(localStrip, static_cast<std::uint8_t>(lsb | 0x80), msb));
        out.push_back(uf8::buildMotorEnable(localStrip, true));
        engaged = true;
    }
    out.push_back(uf8::buildFaderPosition(localStrip, lsb, msb));
    return out;
}

} // namespace uf8fader
