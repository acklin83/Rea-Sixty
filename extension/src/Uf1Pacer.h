#pragma once
//
// Uf1Pacer — the one cycle the UF1 wants, and its timing.
//
// ⇨ SHARED SINCE 2026-09-25 so ORC puts header and meters on the wire exactly
// the way the extension does. Only the cycle itself moved; each host keeps its
// own loop around it (the extension's is data-locked to the meter plug-in's
// frames, ORC has no plug-in and runs on the ceiling, which is also what the
// extension does whenever no new frame arrives: the side-car never sets `seq`).
//
// The numbers and the reasons for them are the extension's, measured against
// SSL 360's own spacing; see uf1CyclePacerLoop_ in main.cpp.

#include "UF1Device.h"

#include <chrono>
#include <cstdint>
#include <vector>

namespace uf1pace {

// One cycle's frames: the meter plug-in's image, the meters, the trailer.
struct Parts {
    std::vector<std::vector<std::uint8_t>> img;     // 35 chunk frames
    std::vector<std::vector<std::uint8_t>> meters;  // 0009 000a 0015 0016
    std::vector<std::vector<std::uint8_t>> tail;    // 011c 0125 0126 0127 0128 011d
    std::uint64_t seq = 0;                          // the t10 array this was painted from
};

constexpr auto kFloor     = std::chrono::microseconds(25000);   // never faster than 40 Hz
constexpr auto kCeiling   = std::chrono::microseconds(41000);   // never slower than SSL
constexpr auto kMetersOff = std::chrono::microseconds(18000);
constexpr auto kTailOff   = std::chrono::microseconds(40300);

// Put one cycle on the wire, starting at `slot`. Blocks until the trailer is
// out (about 40 ms). ⛔ The three groups and the silences between them are ONE
// unit to the firmware: everything else is held out of it by begin/endCycle.
void emitCycle(uf1::UF1Device& dev, const Parts& p,
               std::chrono::steady_clock::time_point slot);

} // namespace uf1pace
