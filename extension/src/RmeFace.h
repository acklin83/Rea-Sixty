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
    // Its half on the keys now (0/1), and whether it has a second one at all
    // (RmeSoftKeys::half / hasSecondHalf; both hosts answer with those).
    std::function<int()>  bankHalf;
    std::function<bool()> bankHasSecondHalf;

    // A short text for the time field (a bank name), "" when there is none.
    std::function<std::string()> tcFlash;

    // The V-Pot row goes through the host's ONE emitter, because the device
    // has one row and one cache for it ([[uf1-screen-owning-mode-checklist]]).
    std::function<void(const uf1spread::VpotRow&, bool force)> emitVpotRow;

    // Pieces that stay the host's.
    std::function<void(const std::array<uf1spread::SkCell, 4>&, bool force,
                       bool highlight, bool menuOpen)> stripSoftKeys;
    std::function<void(bool force)> sideCarSoftKeys;
    // ⇨ ONLY THE KEYS THAT PASS THROUGH (RmeInput::passesThrough: SHIFT,
    // transport, CYCLE, CLICK, 360). Every other lamp the side-car owns is
    // painted here, by keyLamps, in both programs.
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
    // keyLamps as last sent, lamp + 1 per entry, 0 = never.
    std::array<int, 18> sLamp{};
    std::chrono::steady_clock::time_point sNavSent{};
};

// ── the side-car's key lamps ─────────────────────────────────────────────────
// ⇨ A KEY WITH A FUNCTION GLOWS (Frank 27.09.: "LEDs mit Funktion leuchten
// lassen"). Three states, the UF8 panel's rule: dim = the key does something
// here, lit = its state is on (or, for < >, there is more that way), dark = it
// does nothing here. White, because the colour of a REAPER binding belongs to
// an action that does not run in the side-car. Until 27.09. the extension
// decided these in main.cpp from the REAPER bindings' colours, and ORC painted
// none of them at all.
enum class Lamp : std::uint8_t { Dark = 0, Dim = 1, Lit = 2 };
struct KeyLamp {
    std::uint8_t btn;   // UF1 button id; the LED id is btn - 0x18
    Lamp         lamp;
};
// What the painter knows and the lamps need, gathered in one place so the rule
// is a pure function a test can hold.
struct LampFacts {
    bool strip = false;
    bool moreLeft = false, moreRight = false;  // < >: STRIP pages, else banks
    int  half = 0;                             // the soft-key bank's half on the keys
    int  vpotBank = 0;                         // the V-Pot bank, 0 or 1 (5-8)
    bool secondHalf = false;                   // the bank has one
    bool online = false;                       // TotalMix answers
    bool window = false;                       // TotalMix' window shown
    bool haveMain = false, mainOnFader = false;
    bool haveChannel = false, stereo = false;  // the fader channel
};
// Every key the side-car owns and has a lamp for, except SOLO, CUT, SEL and the
// four soft keys (painted with the channel and the bank). Fixed order.
std::array<KeyLamp, 18> keyLamps(const LampFacts& f);

// Send `n` lamps: the bytes REAPER's own key pass sends (FF38 colour, FF39 0x00
// lit / 0x11 otherwise, FF3B on force), dim = white quartered. `cache` holds
// lamp + 1 per entry, 0 = never sent. Nav-cross keys re-send every 500 ms while
// they glow, through `navSent`. ORC uses it for the keys that pass through.
void emitLamps(const KeyLamp* lamps, std::size_t n, int* cache,
               std::chrono::steady_clock::time_point& navSent, const Out& o, bool force);

// A level as the side-car writes it: TotalMix' own readout, off as "-".
std::string dbText(double db);

// One pass. `force` = the surface knows nothing (just opened, or the host
// handed the screen over): repaint everything.
void paint(Cache& c, input::State& in, const Host& h, const Out& o, bool force);

} // namespace reasixty::rme::face
