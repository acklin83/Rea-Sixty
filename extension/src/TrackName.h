#pragma once

#include <atomic>
#include <string>
#include <string_view>

// Fold a UTF-8 string (REAPER track/marker/send names are UTF-8) down to
// Latin-1, the encoding the UF8/UC1 LCDs render. A code-point that fits in
// one Latin-1 byte (<= 0xFF, e.g. ä = U+00E4 → 0xE4) becomes that byte;
// anything higher becomes '?'. Invalid/truncated sequences pass their raw
// byte through so ASCII is never lost. Apply this ONCE per name, at the
// source — folding an already-folded string risks classic mojibake
// (Latin-1 "Ã©" 0xC3 0xA9 IS valid UTF-8 for é). After folding, one char ==
// one byte, so byte-wise truncation/abbreviation is character-safe.
std::string utf8ToLatin1(std::string_view in);

// Track-name abbreviation shared between the UF8 (7-char scribble) and
// the UC1 (12-char CS carousel, 14-char BC carousel) display pipelines.
// Mode toggle lives in main.cpp; this header just exposes it.

enum TrackNameMode : int {
    TNM_Truncate    = 0,
    TNM_SmartAbbrev = 1,
};

extern std::atomic<int> g_trackNameMode;

// forceMode < 0 → use the global g_trackNameMode; otherwise force that
// TrackNameMode for this call only (e.g. the HUD LCD always wants Smart).
std::string abbreviateTrackName_(const std::string& src, int maxLen,
                                 int forceMode = -1);
