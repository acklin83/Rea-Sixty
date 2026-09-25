#pragma once
//
// RmeSoftKeys — the RME side-car's four display soft keys: which bank, which
// half, what each key says and what a press does.
//
// ⇨ ONE ANSWER FOR BOTH PROGRAMS (26.09.2026). Until then the extension built
// these cells in main.cpp and ORC built its own, static only: TotalMix'
// snapshot and layout banks were resolved in main.cpp alone, so in ORC the
// banks were there and the keys were dead. Now both call this.
//
// ⇨ A BANK HAS TWO HALVES, and 5-8 switches between them (Frank 26.09.: "Bank 1:
// dim, mono, speaker b, talkback und dann über 5-8 ext in, main auf fader,
// totalmix fenster"). A static bank's second half is its Shift set, so SHIFT on
// the surface reaches it as well; in the extension the computer keyboard's Shift
// does too, because the extension feeds it to the bindings engine and ORC does
// not. A snapshot or layout bank's second half is items 5 to 8.
//
// No REAPER here. What the extension adds on top (the MODE menu owning the
// keys, the bank name on the time field) stays with the host.

#include "Bindings.h"
#include "RmeInput.h"
#include "Uf1Spread.h"

#include <array>
#include <functional>
#include <string>

namespace reasixty::rme::softkeys {

// The half on the keys: 0 = keys 1-4, 1 = keys 5-8. Latched by 5-8
// (State::skHalf), or held SHIFT.
int half(const input::State& s);

// The bank's kind, when it is one of TotalMix' two; None for a static bank. Any
// other dynamic kind (FX, favourites, ...) has no meaning without REAPER and is
// treated as an empty static bank.
uf8::bindings::DynamicBankKind rmeKind(int bank);

// One TotalMix snapshot or layout slot, 0..7. `led` 0 dark (no link), 1 dim,
// 2 lit (the active snapshot, changed or not, and the last layout loaded).
// ⛔ Reads TotalMix' names from disk (RmeNames): one thread per program.
struct DynSlot {
    std::string label;
    int         led = 0;
};
DynSlot dynSlot(uf8::bindings::DynamicBankKind kind, int slot);

// Load snapshot / layout `slot` (0..7). False when there is no link, and then
// nothing is sent. Thread-safe (the manager has its own lock).
bool loadDyn(uf8::bindings::DynamicBankKind kind, int slot);

// The four cells of `bank` (absolute) on `half`.
std::array<uf1spread::SkCell, 4> row(int bank, int half);

// A press of key `slot` (0..3). A static bank fires its slot on that half
// through the bindings engine, with the engine's long press and behaviour. A
// TotalMix bank loads on the press edge: through `load` when the host has to
// move it to another thread (the extension, which also flashes the name), else
// loadDyn right here (ORC).
using DynLoad = std::function<void(uf8::bindings::DynamicBankKind kind, int slot)>;
void press(int bank, int half, int slot, bool pressed, const DynLoad& load = {});

} // namespace reasixty::rme::softkeys
