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

// Mute of a channel strip, /<input|playback|output>/<n>/mute. One place, because
// the V-Pot push and CUT both write it.
std::string muteAddress(Row r, int ch);

// ⇨ SOLO LIVES ON THE ROUTING, NOT ON THE STRIP. TotalMix solos an input or a
// playback INTO a submix (/mix/<in|pb>/<n>/<submix>/solo, TotalReaper's
// osc-paths-discovered.md); neither source names a solo for outputs. Empty
// string = nothing to solo (an output, or no submix yet).
std::string soloAddress(Row r, int ch, int submix);
bool        soloed(const State& st, Row r, int ch, int submix);

// ⇨ PAN of the fader channel (the V-Pot above the fader, 22.09.). An input or
// a playback pans INTO the submix (/mix/<in|pb>/<n>/<submix>/balpan), an output
// pans itself (/output/<n>/balpan). -1 left .. +1 right. Empty address = none.
std::string panAddress(Row r, int ch, int submix);
double      panValue(const State& st, Row r, int ch, int submix, bool& known);

// Name and palette colour of a channel, "" / -1 when unknown.
const Channel* channelOf(const State& st, Row r, int ch);

// ⇨ TOTALMIX' OWN NAME FOR A CHANNEL COLOUR, 0 (hidden) to 8.
// Read off the colour menu in TotalMix FX on 2026-09-25, in the order the menu
// lists them. Until then only two of the nine were written down anywhere in
// this project ("1 white .. 8 pink" in RmeManager.h) and the other six had no
// name at all, so a settings page could only show numbers. The vendor's own
// interface is a source; these are not derived from the RGB we paint.
// ⚠ These name the MIXER's colour, not the UF1's. What the surface shows is
// Config::colourMap[index], a palette entry with its own name in Palette.cpp.
const char* colourName(int index);

// ⇨ WHAT THE UF1 CALLS A CHANNEL. An output that carries a control-room role is
// shown by the ROLE ("Phones 3"), not by its strip name ("MADI 49/50"): the
// role is what the user put on the pot, the strip name is TotalMix' routing
// detail (Frank 21.09.: "Namen aufloesen!"). Everything else by its name.
std::string displayName(const State& st, Row r, int ch);

// The channel EQ as the UF1 graph's band list. Playbacks, and strips whose EQ
// never arrived, give an OFF model: flat, which is a statement.
uf1eq::Model eqModel(const State& st, Row r, int ch);

// ⇨ TOTALMIX' FLOOR FOR EVERY LEVEL, inputs, playbacks and outputs alike: a
// knob turned down stops at -64.5 dB and never goes off (Frank 25.09.2026:
// "bleibt bei -64.5 stehen, alle kanäle"). Stepping on below it sent values
// TotalMix ignored, and since TotalMix does not echo our own writes, the UF1
// showed them anyway.
constexpr double kLevelFloorDb = -64.5;

// A V-Pot or jog step: dB nudge from `cur`, clamped to kLevelFloorDb..+6. Off
// (-300, a fader pulled to its end) climbs to the floor first; turned down it
// stays off.
double nudgeDb(double cur, int detents, double stepDb);

}  // namespace reasixty::rme::uf1
