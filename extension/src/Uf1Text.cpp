#include "Uf1Text.h"

#include "TrackName.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

std::string formatDbReadout(double linearAmp)
{
    if (linearAmp < 1e-5) return "-inf";
    const double dB = 20.0 * std::log10(linearAmp);
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", dB);
    std::string s(buf);
    if (s.size() > 6) {
        snprintf(buf, sizeof(buf), "%.0f", dB);
        s.assign(buf);
        if (s.size() > 6) s.resize(6);
    }
    return s;
}

std::string formatPanReadout(double pan)
{
    if (pan < -1.0) pan = -1.0;
    if (pan >  1.0) pan =  1.0;
    if (std::abs(pan) < 0.005) return "C";
    int pct = static_cast<int>(std::round(std::abs(pan) * 100.0));
    if (pct > 100) pct = 100;
    char buf[8];
    snprintf(buf, sizeof(buf), "%c%d", pan < 0 ? 'L' : 'R', pct);
    return buf;
}

// Compose the Value Line (19 chars) — e.g. "Vol        -6.0dB".
// Left-justified label, right-justified value. Truncates both if needed
// so the total fits within 19 chars.
// Fit the label into the UF1's 9-character label zone, then lay the line out as
// usual. ABBREVIATES rather than truncating (Frank 2026-08-09) — the same
// smart-abbreviation the track scribbles use, so "LPF Frequency" becomes
// "LPFFrqncy" instead of "LPF Frequ": every word stays visible. Acronyms (LPF,
// HMF, EQ) survive untouched. A value longer than its own 10-char zone still
// steals from the label (composeValueLine's rule) — the whole value matters more
// than the name.
// foldLatin1 = false: the UF1 text path folds at its own source; folding twice
// would double-encode umlauts ([[surface-lcd-latin1-umlauts]]).
std::string uf1ValueLine(std::string_view label, std::string_view value)
{
    std::string lab(label);
    if (lab.size() > kUf1LabelChars)
        lab = abbreviateTrackName_(lab, static_cast<int>(kUf1LabelChars),
                                   TNM_SmartAbbrev, /*foldLatin1*/ false);
    // ⛔ CAP THE VALUE TOO, not just the label. composeValueLine right-aligns the
    // value in a 19-char buffer, but the firmware colours by fixed COLUMN, so an
    // over-long value does not overflow to the right where it would simply be cut
    // — it grows LEFTWARDS into the white label zone. See kUf1ValueChars.
    // Truncated, NOT SmartAbbrev'd: these are formatted parameter values, and
    // vowel-dropping "12 dB/oct" or "-3.5 dB" produces nonsense. Trailing spaces
    // go so a clipped word does not look like a stray gap.
    std::string val(value);
    if (val.size() > kUf1ValueChars) {
        val.resize(kUf1ValueChars);
        while (!val.empty() && val.back() == ' ') val.pop_back();
    }
    return composeValueLine(lab, val);
}

// The UF8's value line is 19 characters, but the LCD does not draw them as one
// field. MEASURED ON THE HARDWARE 2026-09-16 (Frank typed eleven M and eleven 8):
// the WHITE label zone shows the first EIGHT characters, 9 to 11 are drawn
// nowhere at all, and from character 12 the text sits in the YELLOW value zone.
// So a parameter name longer than eight loses its tail into the dead gap and
// then comes back in the wrong colour — "die Param-Namen werden gelb", found
// while mapping Nolly X. composeValueLine alone cannot see this: it only trims
// when label + value + 1 exceeds the full 19, which is the BUFFER, not what the
// firmware paints. Same shape as the UF1's uf1ValueLine, other numbers (11 + 8
// there, 8 + 8 here — [[surface-text-field-widths]] had the 8 for the UF8 all
// along).
// Label is ABBREVIATED, never cut: "LPF Frequency" keeps both words. The value
// is TRUNCATED, because vowel-dropping "-12.3 dB" produces nonsense, and because
// a value longer than 8 starts left of column 12 and falls into the dead gap.
// foldLatin1 = false: the callers hand in text that has already been through
// their own name resolution, and folding twice double-encodes umlauts
// ([[surface-lcd-latin1-umlauts]]).
constexpr size_t kUf8ValueLabelChars = 8;
constexpr size_t kUf8ValueValueChars = 8;
std::string uf8ValueLine(std::string_view label, std::string_view value)
{
    std::string lab(label);
    if (lab.size() > kUf8ValueLabelChars)
        lab = abbreviateTrackName_(lab, static_cast<int>(kUf8ValueLabelChars),
                                   TNM_SmartAbbrev, /*foldLatin1*/ false);
    std::string val(value);
    if (val.size() > kUf8ValueValueChars) {
        val.resize(kUf8ValueValueChars);
        while (!val.empty() && val.back() == ' ') val.pop_back();
    }
    return composeValueLine(lab, val);
}

std::string composeValueLine(std::string_view label, std::string_view value)
{
    constexpr size_t kWidth = 19;
    // ⛔ Every subtraction below is size_t. A `value` at least as long as the
    // whole line wrapped `kWidth - value.size() - 1` to ~2^64; substr(0, huge)
    // is legal and clamps, so the label came back at FULL length, and then
    // `padding` underflowed too and append() threw std::length_error
    // ("basic_string"). That escaped onTimer into REAPER's run loop, which
    // treats an uncaught C++ exception as fatal — the SIGABRT Frank hit all day
    // on 2026-08-14. `value` is a plug-in's FORMATTED parameter value, i.e.
    // arbitrary text: FabFilter runs past 19 characters, SSL's own strips never
    // did, which is why it read as "always FabFilter".
    //
    // Clamp BEFORE subtracting. Cap at kWidth-1 rather than kWidth so there is
    // always at least one pad space, which keeps the value right-aligned in its
    // zone the way the firmware expects.
    if (value.size() > kWidth - 1) value = value.substr(0, kWidth - 1);
    // Prefer to show the full value; trim the label to whatever is left.
    const size_t labelMax = kWidth - value.size() - 1;
    if (label.size() > labelMax) label = label.substr(0, labelMax);
    std::string out(label);
    out.append(kWidth - label.size() - value.size(), ' ');
    out.append(value);
    return out;
}

// dBFS → UF8 VU byte (0..31). -55 dBFS → 0, 0 dBFS → 31.
// The cutoff is intentionally above -60 dB: REAPER's track-peak ballistics
// have a slow decay tail that drifts through -60..-55 even on silent
// tracks, which would otherwise flicker the bottom LED. Snapping anything
// below -55 to 0 gives a clean noise-floor.
uint8_t dbToVuByte_(double dbfs)
{
    if (dbfs >= 0.0)   return 0x1F;
    if (dbfs <= -55.0) return 0x00;
    const double f = (dbfs + 55.0) / 55.0;
    const int byte = static_cast<int>(f * 31.0 + 0.5);
    return static_cast<uint8_t>(std::clamp(byte, 0, 0x1F));
}

std::string uf1SoftKeyText(std::string_view name)
{
    // 13 chars is the field; abbreviate past it (see kUf1SoftKeyChars).
    // Fold to Latin-1 FIRST, for two reasons: the UF1 panel is one byte per
    // glyph, and folding before the length check makes that check and the
    // byte-wise abbreviation character-safe instead of counting an umlaut as two
    // and possibly cutting one in half. abbreviateTrackName_ therefore keeps
    // foldLatin1=false — folding twice would re-decode the high bytes.
    std::string label = utf8ToLatin1(name);
    if (label.size() > kUf1SoftKeyChars)
        label = abbreviateTrackName_(label, static_cast<int>(kUf1SoftKeyChars),
                                     TNM_SmartAbbrev, /*foldLatin1*/ false);
    return label;
}
