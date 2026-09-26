#pragma once
//
// RmeFace — what TotalMix looks like on the UF1. The side-car painter.
//
// ⇨ THE PAIR TO RmeInput. That file says what a knob does to the mixer; this
// one says what the surface shows of it. Both were inside the extension's
// main.cpp until 2026-09-25 and both moved out VERBATIM, so ORC shows exactly
// what Rea-Sixty's side-car shows. Frank, the same day: "Mach die ganze Kiste
// einfach GANZ GENAU SO wie sie in rea-sixty als side-car funktioniert."
//
// ⛔ WHY NOT A SECOND PAINTER. ORC had one (orc/Surface.cpp, viewFor), built
// from memory. On its first run on the device it had no pot labels, no colour
// bars, text in the seven-segment field and nothing on bank 2. A painter
// written next to another one drifts from it on day one.
//
// What stays with the host, as callbacks: the soft keys, the button lamps, the
// MODE menu and the pacing of header and meters. In the extension those are
// shared with REAPER's own UF1 mode and read binding states and REAPER's
// transport; ORC answers them for itself (or leaves them unset) until it runs
// keys through the bindings engine too.
//
// No REAPER here. Frames go to Out, never to a device.

#include "RmeInput.h"
#include "Uf1Spread.h"

#include <array>
#include <chrono>
#include <climits>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace reasixty::rme::face {

struct Out {
    uf1spread::Sink send;
    uf1spread::Sink sendPriority;   // front of the queue (motor enable)
};

// ⛔ An unset callback is a STATE: no MODE menu, fader untouched, one bank,
// no flash, and nothing painted for a piece the host does not have.
struct Host {
    std::function<bool()>          modeMenuOpen;
    std::function<bool()>          faderTouched;
    std::function<bool()>          faderHasPos;
    std::function<std::uint16_t()> faderPos;

    // The side-car's soft-key bank: which one (0-based) and how many.
    std::function<int()> bankNow;
    std::function<int()> bankCount;

    // A short text for the time field (a bank name), "" when there is none.
    std::function<std::string()> tcFlash;

    // The V-Pot row goes through the host's ONE emitter, because the device
    // has one row and one cache for it ([[uf1-screen-owning-mode-checklist]]).
    std::function<void(const uf1spread::VpotRow&, bool force)> emitVpotRow;

    // Pieces that stay the host's.
    std::function<void(const std::array<uf1spread::SkCell, 4>&, bool force,
                       bool highlight, bool menuOpen)> stripSoftKeys;
    std::function<void(bool force)> sideCarSoftKeys;
    std::function<void(bool force, const uf1spread::BtnAvail&)> buttonLeds;
    std::function<void(bool force)> modeMenuOverlay;
    // Header and channel meter, handed over once per paint.
    std::function<void(std::vector<std::vector<std::uint8_t>> meters,
                       std::vector<std::vector<std::uint8_t>> tail)> publishCycle;
};

// ⛔ WHAT THE FUNCTION STATICS WERE. Each of these was a `static` inside the
// extension's painter; here they belong to the caller, one per surface.
struct Cache {
    // the small display
    std::string sName, sDb, sLine, sChSoft, sBarText;
    int sNo = INT_MIN, sPal = INT_MIN, sBar = INT_MIN;
    // the large one
    std::uint8_t sLayout = 0;
    int sAskRow = -1, sAskCh = -1;
    std::chrono::steady_clock::time_point sLastTouch =
        std::chrono::steady_clock::now() - std::chrono::seconds(10);
    std::uint16_t sMotorPos = 0xFFFF, sSentPos = 0xFFFF;
    // The hand had the fader: the host let the motor go limp on touch, and it
    // stays limp until this painter engages it again (see the fader block).
    bool sMotorLimp = false;
    int sMotorCh = INT_MIN, sMotorRow = -1;
    std::array<std::uint8_t, 4> sBars4{ 0xFF, 0xFF, 0xFF, 0xFF };
    std::array<std::uint8_t, 251> sCol{};
    std::uint8_t sTail = 0;
    bool sHave = false;
    bool sMenuWas = false;
    int sPacked = -1;
    std::array<std::uint8_t, 11> sTc{};
    bool sListOpen = false;
};

// A level as the side-car writes it: TotalMix' own readout, off as "-".
std::string dbText(double db);

// One pass. `force` = the surface knows nothing (just opened, or the host
// handed the screen over): repaint everything.
void paint(Cache& c, input::State& in, const Host& h, const Out& o, bool force);

} // namespace reasixty::rme::face
