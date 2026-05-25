#pragma once

#include <atomic>
#include <string>

// Track-name abbreviation shared between the UF8 (7-char scribble) and
// the UC1 (12-char CS carousel, 14-char BC carousel) display pipelines.
// Mode toggle lives in main.cpp; this header just exposes it.

enum TrackNameMode : int {
    TNM_Truncate    = 0,
    TNM_SmartAbbrev = 1,
};

extern std::atomic<int> g_trackNameMode;

std::string abbreviateTrackName_(const std::string& src, int maxLen);
