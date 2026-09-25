#pragma once
//
// RmeInput — what the UF1 does to TotalMix.
//
// ⇨ THE OTHER HALF OF Uf1Spread. The painter takes a state and writes frames
// into a sink; this takes an event and writes OSC addresses into a list. Same
// shape, same reason: the half worth testing should not be the half that needs
// a mixer and a surface plugged in.
//
// Up to 2026-09-25 this lived as 467 lines inside the extension's main.cpp. The
// compiler was asked what it actually depended on (probe TU, -fsyntax-only) and
// the answer was eighteen names, NONE of them the REAPER API: seven globals of
// side-car state, three host questions, four helpers and a constant. So this is
// a move, not a rewrite, and Rea-Sixty keeps calling the same code it always
// did.
//
// ⛔ NOTHING IN HERE SENDS. Every function fills a Writes list and the caller
// puts it on the wire. That is what lets a test say "this knob turn produces
// exactly this one address at this value" without a socket.
//
// ⛔ AND NOTHING IN HERE IS A FUNCTION STATIC. The channel encoder and the whole
// -step strip pots carry a remainder between events; in main.cpp those were
// `static double` inside the function, which means one surface's leftovers would
// have been the other's. They live in State, which the caller owns, exactly like
// Uf1Spread::Cache.

#include "RmeManager.h"
#include "RmeState.h"
#include "RmeStrip.h"
#include "RmeUf1.h"
#include "UF1Protocol.h"

#include <atomic>
#include <cstdint>
#include <functional>

namespace reasixty::rme::input {

namespace rmeu = reasixty::rme::uf1;
namespace rmes = reasixty::rme::strip;

using Writes = rmes::Writes;

// ⛔ ATOMIC, and not for decoration: input arrives on the device's worker thread
// and the painter reads this from another one. They were atomics as globals and
// they stay atomics here. The struct is therefore neither copyable nor movable,
// which is fine: there is exactly one per surface and the caller owns it.
struct State {
    std::atomic<int>  row{2};                       // rmeu::Row, 2 = Output
    std::atomic<int>  sel[3] = { {-1}, {-1}, {-1} };// chosen channel per row
    std::atomic<int>  submix{-1};                   // the output inputs write into
    std::atomic<bool> strip{false};                 // STRIP open
    std::atomic<int>  stripPage{0};                 // index into Config::stripPages
    std::atomic<int>  vpotBank{0};
    std::atomic<bool> windowShown{false};           // TotalMix' window is up

    // Encoder remainders. The channel encoder emits about four counts per
    // detent and the whole-step strip pots are gathered the same way; a
    // direction change throws the rest away.
    std::atomic<double> chAccum{0.0};
    std::atomic<double> potAccum[4] = { {0.0}, {0.0}, {0.0}, {0.0} };

    State() = default;
    State(const State&) = delete;
    State& operator=(const State&) = delete;
};

// The three things the input path cannot know by itself.
// ⛔ An unset callback is a STATE, not a failure, and each one has the answer
// that makes the surface behave as if the host had never heard of it: no mode
// menu, no fine mode, link up. ORC leaves the first two unset.
struct Host {
    std::function<bool()>   modeMenuOpen;  // the MODE menu owns the keys
    std::function<double()> knobScale;     // fine mode factor, 1.0 = normal
    std::function<bool()>   online;        // the link is answering
};

// ── queries the painter and the LEDs share with the input path ───────────────
// ⇨ ONE ANSWER FOR BOTH. If the painter worked out the selected channel or the
// strip page on its own, the screen and the knob would disagree the moment the
// rules differed by a line.

// The channel the fader and the EQ graph show. With nothing chosen: Main for
// outputs, else the first visible channel of the row.
int  selected(const State& s, const rme::State& st, rmeu::Row r);
// The page STRIP shows for this channel: the chosen one if the channel has it,
// else its first. -1 = the channel reports no settings at all.
int  stripPage(const State& s, const rme::State& st, rmeu::Row r, int sel,
               const Config& cfg);
// The parameter on pot or key `i` of that page, nullptr for an empty slot.
const rmes::Param* stripParam(const Config& cfg, int page, bool key, int i);

void select(State& s, rmeu::Row r, int ch);
void stepRow(State& s, int dir);
// ⇨ AND THE V-POT BANK FOLLOWS (Frank 25.09.: "wenn mit nav tasten die outputs
// gescrollt werden sollte das display folgen"). If the new submix sits on a pot
// of the other bank, 5-8's bank switches to it, so the output you stepped to is
// on the glass. An output on no pot leaves the bank alone.
void stepSubmix(State& s, const rme::State& st, const Config& cfg, int dir);

// Show or hide TotalMix' window. One decision for the nav centre and for the
// `rme_show_window` builtin.
void toggleWindow(State& s, const Host& h, Writes& out);

// Main onto the fader. One decision for the `rme_fader_main` builtin and for the
// MASTER key beside the fader, which does the same thing in the side-car.
void faderMainFire(State& s, const rme::State& st);
bool faderMainActive(const State& s, const rme::State& st);

// ── the input path ───────────────────────────────────────────────────────────
// Each returns true when the event is used up. Call them only while the
// side-car actually holds the surface: whether it does is the host's question,
// not this file's.

bool encoder(State& s, const Host& h, const rme::State& st, const Config& cfg,
             std::uint8_t id, int delta, Writes& out);
bool button(State& s, const Host& h, const rme::State& st, const Config& cfg,
            const ::uf1::InputEvent& ev, Writes& out);
// The four display soft keys and < > WHILE STRIP IS OPEN, where they belong to
// the page rather than to the soft-key banks. False when STRIP is closed, and
// then the host's own bank handling takes the event.
bool stripSoftKey(State& s, const Host& h, const rme::State& st, const Config& cfg,
                  const ::uf1::InputEvent& ev, Writes& out);

} // namespace reasixty::rme::input
