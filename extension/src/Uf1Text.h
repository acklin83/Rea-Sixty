#pragma once
//
// Uf1Text — the text rules of the SSL surfaces' LCD zones.
//
// ⇨ MOVED OUT OF main.cpp ON 2026-09-25, verbatim, so ORC paints its lines with
// the same rules the extension does. The names stay global and unchanged, so
// every call site in main.cpp reads as it did.
//
// No REAPER here: text in, text out.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

std::string formatPanReadout(double pan);
std::string composeValueLine(std::string_view label, std::string_view value);
// ⚠ The UF1 paints a value line in TWO FIXED ZONES, not as one 19-char string:
// characters 0-8 are the LABEL (white), 9-18 the VALUE (yellow). composeValueLine
// only trims the label when label+value+1 exceeds the full 19, which is true of
// the BUFFER but not of what the hardware draws — so a label longer than 9 spills
// into the value zone and turns yellow. Frank 2026-08-09: "LPF Frequency 22.0k"
// rendered as "LPF Frequ" + a yellow "ency 22.0k". Every UF1 line with a VARIABLE
// label must go through uf1ValueLine, never composeValueLine directly.
// SETTLED 2026-08-09 on the hardware (Frank): the white label field is
// officially 11 characters wide — from character 12 on the text is drawn in
// the YELLOW value zone. 9 was the shipped guess, 13 overran. Both dialogs
// that name this limit (Settings FX-Learn cell, HUD UF1 cell) say 11 too.
constexpr size_t kUf1LabelChars = 11;
// …and the YELLOW value zone is the other 8 (19 − 11). The label got its cap on
// 2026-08-09; the VALUE never did, and composeValueLine right-aligns it in the
// 19-char buffer — so anything longer than 8 characters starts left of column 12
// and is drawn in the WHITE label zone. "Concert Hall" (12) begins at column 7,
// which is why a forum user reported the first three characters coming out white
// ("Con") and, on other names, characters missing entirely where the label field
// overwrote them ("Sm th Plate", "oth Room"). Truncating is what he asked for and
// what the numbers want: "-12.3 dB" is exactly 8.
constexpr size_t kUf1ValueChars = 8;
// The UF1's SOFT-KEY label field (screen 0x0104) is 13 characters — measured on
// the hardware 2026-08-09. It was the one UF1 text zone with no cap at all, so a
// long user name or param name simply ran off the key. Abbreviated, never
// truncated: SmartAbbrev keeps every word visible ("LPF Frequency" → "LPFFrqncy").
constexpr size_t kUf1SoftKeyChars = 13;
std::string uf1ValueLine(std::string_view label, std::string_view value);
std::string formatDbReadout(double linearAmp);

// The UF1 small display's track-name cell, in characters.
inline constexpr int kUf1TrackNameChars = 8;

std::string uf8ValueLine(std::string_view label, std::string_view value);

// A soft key's name as the UF1 draws it: Latin-1, abbreviated (never cut) to
// the 13-character field. One rule for every UF1 soft-key painter.
std::string uf1SoftKeyText(std::string_view name);

// dBFS → VU byte (0..31), see Uf1Text.cpp.
uint8_t dbToVuByte_(double dbfs);
