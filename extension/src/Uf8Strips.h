#pragma once
//
// One UF8 strip as a controller surface: what each cell shows, and the frames
// that put it there, sent only when a cell changed. Pulled out of main.cpp's
// MIDI CC layer painter (ccPaintSurface_, 29.10.2026, docs/orc-uf8-plan.md stage
// 5) so that ORC's TotalMix side-car paints the UF8 with the same cells, the
// same builders and the same order, rather than a second painter.
//
// ⛔ THE CACHE STAYS WITH THE CALLER. `Cells` are references into wherever the
// caller keeps "what the device shows": main.cpp hands in its g_last* arrays,
// which its other painters and the device reopen invalidate as before; ORC hands
// in its own. A cache of its own in here would be a second writer for the same
// cells (the Hue lesson of 27.08.2026).
//
// The cells, top to bottom (Protocol.h has the field map):
//   colour bar        not here: ColorSync in main.cpp, buildColorCommand in ORC
//   CS-type cell      csType (12 chars)         buildChannelStripType
//   channel number    chanNum                   buildChannelNumber
//   upper scribble    upper (8 chars, Latin-1)  buildStripTextUpper
//   fader readout     faderText + faderUnit     buildFaderDbReadout
//   value line        valueLine (19 chars)      buildValueLine
//   soft-key label    slotLabel                 buildPluginSlotName
//   soft-key lamp     topKey 0 off, 1 dim, 2 on buildTopSoftKeyLed
//   SEL / CUT / SOLO  sel / cut / solo          buildLedColourPair
//   motor             drive + faderPb           uf8fader::drive (not while touched)
//   ring              ring + ringMode           per unit: buildVPotReadoutBar, buildVPotMode
// Test: tests/test_uf8_strips.cpp.

#include "Protocol.h"
#include "Uf8Fader.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace uf8strips {

struct View {
    std::string   upper;
    std::string   csType;
    std::string   chanNum   = " ";
    std::string   faderText;
    std::string   faderUnit;
    std::string   valueLine;
    std::string   slotLabel;
    int           topKey    = 0;          // 0 off, 1 dim, 2 on
    std::uint32_t topKeyRgb = 0xFFFFFF;
    bool          sel       = false;
    std::uint32_t selRgb    = 0xFFFFFF;
    bool          cut       = false;
    bool          solo      = false;
    bool          drive     = false;      // the fader has a value to go to
    std::uint16_t faderPb   = 0;
    std::uint16_t ring      = 0;          // uf8::vpotPos*
    std::uint8_t  ringMode  = 0x03;       // 0x01 unipolar, 0x02 bipolar, 0x03 none
};

// What the device shows, the caller's storage. -1 / sentinel values mean
// "unknown", so the next paint sends the cell.
struct Cells {
    std::string&   upper;
    std::string&   csType;
    std::string&   chanNum;
    std::string&   faderText;
    std::string&   faderUnit;
    std::string&   valueLine;
    std::string&   slotLabel;
    std::int32_t&  topKey;
    std::int8_t&   sel;
    std::int8_t&   cut;
    std::int8_t&   solo;
    std::uint16_t& faderPb;
};

// One strip. `st` is the strip on its unit (0..7). `send(frame)` writes a
// frame, `leds(LedColourFrames)` an LED pair. `touched`: the cap is under a
// finger, the motor stays off it. `engaged` is the caller's motor flag. Returns
// true when it drove the motor (the caller records the target).
template <class Send, class Leds>
bool paintStrip(Cells c, const View& v, std::uint8_t st, bool touched, bool& engaged,
                Send&& send, Leds&& leds)
{
    if (c.upper != v.upper) {
        c.upper = v.upper;
        send(uf8::buildStripTextUpper(st, v.upper));
    }
    if (c.csType != v.csType) {
        c.csType = v.csType;
        send(uf8::buildChannelStripType(st, v.csType));
    }
    if (c.chanNum != v.chanNum) {
        c.chanNum = v.chanNum;
        send(uf8::buildChannelNumber(st, v.chanNum));
    }
    if (c.faderText != v.faderText || c.faderUnit != v.faderUnit) {
        c.faderText = v.faderText;
        c.faderUnit = v.faderUnit;
        send(uf8::buildFaderDbReadout(st, v.faderText, v.faderUnit));
    }
    if (c.valueLine != v.valueLine) {
        c.valueLine = v.valueLine;
        send(uf8::buildValueLine(st, v.valueLine));
    }
    if (c.slotLabel != v.slotLabel) {
        c.slotLabel = v.slotLabel;
        send(uf8::buildPluginSlotName(st, v.slotLabel));
    }
    if (c.topKey != v.topKey) {
        c.topKey = v.topKey;
        leds(uf8::buildTopSoftKeyLed(st,
            v.topKey == 2 ? uf8::TopSoftKeyState::On
                          : v.topKey == 1 ? uf8::TopSoftKeyState::Dim : uf8::TopSoftKeyState::Off,
            uf8::ledColourForTrackRgb(v.topKeyRgb)));
    }
    const std::int8_t sel = v.sel ? 1 : 0;
    if (c.sel != sel) {
        c.sel = sel;
        leds(uf8::buildLedColourPair(st, uf8::LedClass::Sel, v.sel,
                                     uf8::ledColourForTrackRgb(v.selRgb)));
    }
    const std::int8_t cut = v.cut ? 1 : 0;
    if (c.cut != cut) {
        c.cut = cut;
        leds(uf8::buildLedColourPair(st, uf8::LedClass::Cut, v.cut));
    }
    const std::int8_t solo = v.solo ? 1 : 0;
    if (c.solo != solo) {
        c.solo = solo;
        leds(uf8::buildLedColourPair(st, uf8::LedClass::Solo, v.solo));
    }
    // Motor: a limp strip takes the bit-7 echo first, then the enable, or the
    // firmware drops the position (uf8fader::drive).
    if (v.drive && !touched && (!engaged || c.faderPb != v.faderPb)) {
        c.faderPb = v.faderPb;
        for (auto& f : uf8fader::drive(st, v.faderPb, engaged)) send(std::move(f));
        return true;
    }
    return false;
}

// The rings of one unit: positions first, then the modes (cap37 ordering), each
// only when it changed or `init` is false. `lastBar` / `lastMode` point at the
// unit's eight cached entries.
template <class Send>
void paintRings(std::uint16_t* lastBar, std::uint8_t* lastMode, bool init,
                const std::array<std::uint16_t, 8>& bar, const std::array<std::uint8_t, 8>& mode,
                Send&& send)
{
    bool barChanged = !init, modeChanged = !init;
    for (int i = 0; i < 8; ++i) {
        if (lastBar[i] != bar[i])   barChanged  = true;
        if (lastMode[i] != mode[i]) modeChanged = true;
    }
    if (barChanged) {
        for (int i = 0; i < 8; ++i) lastBar[i] = bar[i];
        send(uf8::buildVPotReadoutBar(bar));
    }
    if (modeChanged) {
        for (int i = 0; i < 8; ++i) lastMode[i] = mode[i];
        send(uf8::buildVPotMode(mode));
    }
}

} // namespace uf8strips
