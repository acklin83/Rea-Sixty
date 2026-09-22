#pragma once
//
// RmeStrip — the channel view (STRIP) of the RME side-car, as pure functions
// over RmeState. No REAPER, no device, no socket; tests/test_rme_osc.cpp pins it.
// Plan: docs/rme-strip-and-uf8-plan.md, sections 3, 5a, 5b.
//
// Three rules shape this file:
//
//  · THE CATALOGUE IS RME'S. Every leaf name comes from RME's protocol sheet
//    (OSCProtocoll_260721.ods, read 2026-09-21), every range and step from the
//    UFX+ manual (5b). A page is only a choice of ids from this catalogue; the
//    pages themselves live in rme.json (StripPage, RmeManager.h).
//  · WHAT A CHANNEL SHOWS IS TOTALMIX' ANSWER. A parameter is there when the
//    channel reported its leaf (Channel::leaves), and a page is there when at
//    least one of its parameters is. No table per device.
//  · STEREO OR MONO DECIDES TOO (Frank 21.09.). Width and M/S only on a stereo
//    strip; Phase R is the right half's phase (index + 1); gain and delay go to
//    both halves of a stereo pair (the L/R column of RME's sheet).
//
#include "RmeManager.h"
#include "RmeUf1.h"

#include <string>
#include <utility>
#include <vector>

namespace reasixty::rme::strip {

using uf1::Row;

enum class Kind : std::uint8_t {
    Toggle,   // 0 / 1, a soft key
    List,     // index into `names`; a pot steps it, a key cycles it
    Db,       // linear, "dB"
    Hz,       // logarithmic, a twelfth of an octave per detent
    Q,
    Ms,
    Sec,
    Ratio,
    Width,    // 0..1
    Pan,      // -1..1, L / C / R
    Int,      // plain number
};

enum class Need : std::uint8_t { Any, Stereo };

struct Param {
    const char* id;
    const char* leaf;
    const char* label;          // <= 8 characters: the Layout-1 text field
    Kind        kind;
    double      lo, hi, step;
    Need        need      = Need::Any;
    bool        right     = false;   // address the right half, index + 1
    bool        bothSides = false;   // on a stereo strip, write n and n + 1
    std::vector<const char*> names   = {};   // List: display per index
    std::vector<const char*> namesOut = {};  // List on an output, if different
};

// nullptr for an unknown id (a typo in rme.json is an empty slot, not a crash).
const Param* find(const std::string& id);

// "input" / "playback" / "output".
const char* section(Row r);

// The page may be shown on this row at all ("in,pb,out", empty = all).
bool pageAllowsRow(const StripPage& pg, Row r);

// The parameter exists on this channel: TotalMix reported its leaf, and the
// stereo condition holds.
bool available(const State& st, Row r, int ch, const Param& p);

// Pages this channel shows, as indices into `pages`, in order.
std::vector<int> availablePages(const State& st, Row r, int ch,
                                const std::vector<StripPage>& pages);

// The page draws the EQ graph (it carries an EQ or low-cut parameter), which
// on the UF1 means Layout 3; every other page is Layout 1 (plan 6a).
bool pageShowsGraph(const StripPage& pg);

// Current value. false when TotalMix has not reported it.
bool value(const State& st, Row r, int ch, const Param& p, double& out);

// Label as the surface shows it: a mono strip's "Phase" has no L.
std::string label(const State& st, Row r, int ch, const Param& p);

// Value text: "+3.5 dB", "1.20 kHz", "4.0:1", "on", "Bell" ...
std::string format(const Param& p, Row r, double v);

// Where 0..1 sits for the segment bar.
double norm(const Param& p, Row r, double v);

using Writes = std::vector<std::pair<std::string, float>>;

// A pot turned by `detents`: the messages to send (empty = nothing to do).
// `scale` is the surface's knob scale (Fine = 0.25 by default); it shrinks a
// continuous step and leaves lists, numbers and switches alone.
Writes nudge(const State& st, Row r, int ch, const Param& p, int detents,
             double scale = 1.0);

// Lists, plain numbers and switches move one whole entry per DETENT, not per
// raw encoder count (a UF1 pot sends ~4 counts per click; Frank 22.09.: "für
// EQ Type ist die Rasterung viel zu fein").
bool stepsWhole(const Param& p);

// ⇨ BACK TO THE DEFAULT on a V-Pot push (Frank 22.09.: "push auf V-Pot auf den
// Standard-Wert, 0 dB bei Gains etc."). Frank's table first (EQ 80 Hz Shelf /
// 1 kHz Bell / 5 kHz Shelf, Q 1.0; Low Cut 20 Hz 12 dB/oct; Comp gain 0, 10 ms,
// 300 ms, -30 dB, 1:1; Exp -60 dB 1:1; AutoLevel 6 dB, 6 dB, 5.0 s), then the
// neutral rule: 0 dB for a dB parameter whose range holds it, Width 1, Pan
// centre. Nothing else (ref level, crossfeed, delay): empty.
Writes resetWrites(const State& st, Row r, int ch, const Param& p);

// A key pressed: toggle, or the next list entry.
Writes press(const State& st, Row r, int ch, const Param& p);

// "/sendchan/input/3": asks TotalMix for every value of one channel.
std::string sendChanAddress(Row r, int ch);

}  // namespace reasixty::rme::strip
