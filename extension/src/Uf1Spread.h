#pragma once
//
// Uf1Spread — the SPREAD view of the UF1 side-car, drawn from a value object.
//
// ⛔ ONE PAINTER, AND THIS IS IT. ORC and Rea-Sixty both draw these screens, and
// this project has paid twice for the alternative: the V-Pot row ran on two
// caches that drifted apart, and the EQ curve came back with its peaking formula
// replaced by a Gaussian when it was split in half. Both were repaired on
// 2026-09-19. So the shape here follows Uf1EqCurve, which is the repair that
// stuck: a gatherer that knows where the numbers come from, and a renderer that
// does not.
//
//   viewFor(state, ...)  knows TotalMix          (the gatherer, host-side)
//   paint(view, cache, sink)  knows the UF1 only (the renderer, here)
//
// Because paint() takes a View and writes into a Sink, it can be driven by a
// test with no mixer and no surface attached. That is the whole reason for the
// split: this file is the first UF1 painting code in the project that can be
// tested at all.
//
// ⛔ THE CACHE IS THE CALLER'S. The version of this that lived in main.cpp kept
// its "what did I send last" in function statics. Two views could not coexist,
// a test could not reset it, and a second copy of the same statics is exactly
// how the V-Pot row drifted. Cache is an object; a default-constructed one means
// "assume the surface knows nothing", which is what the old `force` flag meant.
//

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "Uf1EqCurve.h"

namespace uf1spread {

// The painter hands finished frames over; the caller decides what a frame is
// for. Rea-Sixty and ORC pass their device's send(); the test collects them.
using Sink = std::function<void(std::vector<std::uint8_t>)>;

// ── the V-Pot row ─────────────────────────────────────────────────────────────
// The row as the extension has always composed it: text already folded to
// Latin-1, bars and styles already computed. It moved here from main.cpp so that
// there is exactly one emitter for these four elements. There used to be two,
// each with its own cache, and leaving a mode left the other mode's labels on
// the glass because the second believed they were already there (19.09.2026).
struct VpotRow {
    std::array<std::string, 4> line{};
    std::array<std::uint8_t, 8> bars{};
    // 0x03 = empty. The channel painter starts from "all four blank" and lights
    // only what it fills; Hue sets all four every time.
    std::array<std::uint8_t, 4> styles{ 0x03, 0x03, 0x03, 0x03 };
    // ⇨ LAYOUT 1: the name goes into the per-pot text field and `line` carries
    // the value alone, 14 capitals wide there. Styles 0x01 and 0x08 hide the
    // whole row in Layout 1.
    bool layout1 = false;
    std::array<std::string, 4> names{};
};

// What the row is believed to be showing. Default = nothing known, so the next
// call writes every cell. That is what the old `force` flag meant, made into a
// value the caller holds instead of a static nobody could reset.
struct VpotCache {
    bool valid = false;
    std::array<std::string, 4> line{};
    std::array<std::uint8_t, 8> bars{};
    std::array<std::uint8_t, 4> styles{};
    std::array<std::string, 4> names{};
    bool namesShown = false;
};

int paintVpotRow(const VpotRow& row, VpotCache& cache, const Sink& out);

struct Pot {
    std::string name;            // Layout-1 per-pot text, 8 characters
    std::string line;            // the value line under it
    double      norm    = 0.0;   // 0..1
    bool        bipolar = false; // draws from the centre
    bool        empty   = true;  // nothing on this pot

    bool operator==(const Pot&) const = default;
};

// Everything SPREAD draws, already resolved. No RME type appears here on
// purpose: the renderer must not be able to ask the mixer anything, and a test
// must be able to build one of these by hand.
struct View {
    std::string header;              // 8 x 25 ASCII header row
    std::string timecode;            // 11-character segment field
    std::array<Pot, 4> pots{};

    std::string chName;              // channel zone
    std::string chDb;
    std::string chNumber;
    int         palette  = 0;        // colour-bar palette index
    bool        chActive = false;    // the firmware's own "populated" flag

    int         faderPos = -1;       // 15-bit; -1 leaves the motor alone

    bool operator==(const View&) const = default;
};

// What the surface is believed to be showing. Default = believed to show
// nothing, so the next paint writes every element.
struct Cache {
    bool  valid = false;
    View  shown{};
};

// Writes only what differs from `cache`, updates it, and returns the number of
// frames handed to `out`. A second paint of an unchanged View writes nothing,
// which is the property the test pins.
int paint(const View& v, Cache& cache, const Sink& out);

// ── STRIP ─────────────────────────────────────────────────────────────────────
// The channel view: one TotalMix channel across the colour bar, its name, its
// value line, and the EQ curve under them.
struct StripView {
    std::string name;
    std::string db;
    std::string line;       // the value line: the parameter under the hand
    std::string number;
    std::string csType;     // the type cell, which carries the page name here
    std::string chSoft;     // label of the soft key beside the channel
    int         palette  = 0;
    bool        active   = false;
    int         barPos   = 0;      // -100..100
    bool        barCentre = false; // draw the bar from the middle

    // ⛔ on == false is a STATEMENT, not a fallback. A flat graph means the EQ
    // is out; a curve from the wrong channel is never right. That distinction
    // cost a day when the graph was first shared, so it travels in the type.
    uf1eq::Model eq;
};

struct StripCache {
    bool valid = false;
    StripView shown{};
    // The rendered curve, not the model. What decides a redraw is whether the
    // picture changed, and two different models can draw the same picture.
    std::array<std::uint8_t, 251> col{};
    std::uint8_t tail = 0;
};

int paintStrip(const StripView& v, StripCache& cache, const Sink& out);

} // namespace uf1spread
