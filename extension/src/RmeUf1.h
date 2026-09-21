#pragma once
//
// RmeUf1 — what the UF1's RME side-car needs to know about TotalMix, as pure
// functions over RmeState. No REAPER, no device, no socket: the painter and the
// input handlers in main.cpp call these, and tests/test_rme_osc.cpp pins them.
//
// The whole plan is docs/uf1-spread-plan.md. The rules that shape this file:
//
//  · Three ROWS, as TotalMix has them: Input, Playback, Output. The channel
//    encoder walks the VISIBLE channels of one row (colour != 0 on this remote).
//  · "The selected channel" is what the fader rides and whose EQ the graph
//    draws. For an output that is its own volume. For an input or a playback it
//    is its NODE in the current submix, and the current submix is the output
//    selected last, exactly like clicking an output in TotalMix.
//  · Playbacks have no EQ (dump 2026-09-21): the graph is flat, and says so.
//
#include "RmeState.h"
#include "Uf1EqCurve.h"

#include <string>
#include <vector>

namespace reasixty::rme::uf1 {

enum class Row : int { Input = 0, Playback = 1, Output = 2 };
constexpr int kRowCount = 3;
const char* rowName(Row r);                 // "INPUT" / "PLAYBACK" / "OUTPUT"

// Visible channels of a row, ascending: named on this remote and colour != 0.
std::vector<int> visibleChannels(const State& st, Row r);

// Step through a list by `delta`, clamped at both ends. A `cur` not in the list
// lands on the nearest entry in the direction of travel.
int stepChannel(const std::vector<int>& list, int cur, int delta);

// A V-Pot or jog target from rme.json: a role TotalMix names itself ("main",
// "mainB", "phones1".."phones4", "talk"), or a fixed channel ("output:8",
// "input:0", "playback:2").
struct Target {
    Row  row      = Row::Output;
    int  ch       = -1;
    bool assigned = false;   // TotalMix named a channel for this role
    bool visible  = false;   // …and this remote can see it
};
Target resolveTarget(const State& st, const std::string& spec);

// The submix an input/playback node writes into: the given output if it is
// visible, else Main, else the first visible output, else -1.
int effectiveSubmix(const State& st, int selectedOutput);

// Level in dB of a channel as the fader sees it. `known` is false when TotalMix
// has not reported it (a node that never arrived is not the same as "off").
double levelDb(const State& st, Row r, int ch, int submix, bool& known);

// Address for writing a channel's level, faderlin or dB.
std::string levelAddress(Row r, int ch, int submix, bool faderlin);

// Name and palette colour of a channel, "" / -1 when unknown.
const Channel* channelOf(const State& st, Row r, int ch);

// The channel EQ as the UF1 graph's band list. Playbacks, and strips whose EQ
// never arrived, give an OFF model: flat, which is a statement.
uf1eq::Model eqModel(const State& st, Row r, int ch);

// A V-Pot step: dB nudge from `cur`, off (-300) climbs to -60 first so the knob
// does not sit on a dead range, clamped to +6.
double nudgeDb(double cur, int detents, double stepDb);

}  // namespace reasixty::rme::uf1
