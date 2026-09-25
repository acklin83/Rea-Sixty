#pragma once
//
// Uf1SoftKeys — the UF1's four display soft keys: names, lamps, highlight.
//
// ⇨ MOVED OUT OF main.cpp ON 2026-09-25, verbatim, so ORC lights its soft keys
// the way the extension does (label from the binding, lamp from the bound
// action's state, colour from the binding's LED settings). A probe translation
// unit said the emitter, the bank cell and the two colour helpers needed
// nothing from main.cpp but the device and one constant; the only REAPER
// question, an action's toggle state, went into the bindings host
// (Host::toggleState) together with the resolver.
//
// No REAPER here. Frames go to a sink.

#include "Bindings.h"
#include "Uf1Spread.h"

#include <array>
#include <climits>
#include <cstdint>
#include <string>

namespace uf1sk {

// What the emitter believes the row shows: the three caches that were statics
// inside uf1EmitSoftKeyRow_. One per surface, owned by the caller.
struct RowCache {
    std::array<std::string, 4> sSkLabel{};
    std::array<int, 4>         sSkLed{ -1, -1, -1, -1 };
    int                        sSkHi = INT_MIN;
};

// A colour as the UF1's 4-bit LED nibbles, dimmed in nibble space.
void keyColourNibbles(std::uint32_t rgb, bool bright,
                      std::uint8_t& g4, std::uint8_t& r4, std::uint8_t& b4);

// The lamp colour a binding asks for, on or off, as 0xRRGGBB (0 = dark).
std::uint32_t bindingLedColour(const uf8::bindings::Binding& bd,
                               const uf8::bindings::ActionSlot& colSlot, bool on);

// One static soft-bank slot as a cell: name, engaged state, lamp colour.
// `mod` >= 0 shows that set instead of the held one (the RME side-car's 5-8).
uf1spread::SkCell staticBankCell(int bankNo, int i, int mod = -1);

// Put the row on the surface: names, lamps and the highlight mask, each only
// when it changed or when `force`. `ledsBorrowed`: another painter owns the
// lamps right now. `menuOpen`: the MODE menu owns the keys (lamps parked,
// highlight off, names only when they changed).
void emitRow(const std::array<uf1spread::SkCell, 4>& cells,
             bool force, bool ledsBorrowed, bool menuOpen,
             RowCache& cc, const uf1spread::Sink& out);

} // namespace uf1sk
