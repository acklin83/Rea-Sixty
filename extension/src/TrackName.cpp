#include "TrackName.h"

#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

std::atomic<int> g_trackNameMode{TNM_Truncate};

std::string utf8ToLatin1(std::string_view in)
{
    std::string out;
    out.reserve(in.size());
    const size_t n = in.size();
    for (size_t i = 0; i < n; ) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        if (c < 0x80) { out.push_back(static_cast<char>(c)); ++i; continue; }
        const int len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 0;
        if (len == 0 || i + static_cast<size_t>(len) > n) {
            // Stray continuation / lead byte / truncated tail → pass through
            // as a Latin-1 byte so we never drop or mis-shift the rest.
            out.push_back(static_cast<char>(c)); ++i; continue;
        }
        uint32_t cp = (len == 2) ? (c & 0x1F) : (len == 3) ? (c & 0x0F) : (c & 0x07);
        bool ok = true;
        for (int k = 1; k < len; ++k) {
            const unsigned char cc = static_cast<unsigned char>(in[i + k]);
            if ((cc & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok) { out.push_back(static_cast<char>(c)); ++i; continue; }
        out.push_back(cp <= 0xFF ? static_cast<char>(static_cast<unsigned char>(cp))
                                 : '?');
        i += static_cast<size_t>(len);
    }
    return out;
}

// Shorten `src` to at most `maxLen` chars. In Truncate mode this is a
// straight resize. SmartAbbrev tries to keep every word visible:
//   1) split on space / dash / underscore / slash;
//   2) per token: keep first char, drop later vowels;
//   3) collapse runs of repeated consonants per token;
//   4) if combined length still exceeds maxLen, distribute the budget
//      proportionally to each token's shortened length, guaranteeing at
//      least one char per token so "Background Vocals" lands as
//      "BckgrV" or similar instead of "Bckgrnd" (which loses the V).
// All-uppercase short tokens (DI, FX, EQ, …) survive untouched.
std::string abbreviateTrackName_(const std::string& srcIn, int maxLen,
                                 int forceMode, bool foldLatin1)
{
    // Fold UTF-8 → Latin-1 ONLY for the UF8 hardware scribble (caller opts in).
    // After folding every char is one byte, so the byte-wise abbreviation below
    // stays character-safe. UC1 + ImGui companions pass false (raw UTF-8).
    const std::string src = foldLatin1 ? utf8ToLatin1(srcIn) : srcIn;
    if (maxLen <= 0) return src;
    if (static_cast<int>(src.size()) <= maxLen) return src;
    const int mode = (forceMode >= 0) ? forceMode : g_trackNameMode.load();
    if (mode != TNM_SmartAbbrev) {
        std::string out = src;
        out.resize(maxLen);
        return out;
    }
    auto isSep = [](char c) {
        return c == ' ' || c == '\t' || c == '-' || c == '_' || c == '/';
    };
    std::vector<std::string> tokens;
    {
        std::string cur;
        for (char c : src) {
            if (isSep(c)) {
                if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
            } else {
                cur.push_back(c);
            }
        }
        if (!cur.empty()) tokens.push_back(cur);
    }
    if (tokens.empty()) {
        std::string out = src;
        out.resize(maxLen);
        return out;
    }
    // Pass 1: just strip separators. Often enough on its own.
    {
        std::string joined;
        for (auto& t : tokens) joined += t;
        if (static_cast<int>(joined.size()) <= maxLen) return joined;
    }

    auto isVowel = [](char c) {
        const char l = static_cast<char>(
            std::tolower(static_cast<unsigned char>(c)));
        return l == 'a' || l == 'e' || l == 'i' || l == 'o' || l == 'u';
    };
    auto isAcronymToken = [](const std::string& t) {
        if (t.size() < 2 || t.size() > 4) return false;
        for (char c : t) {
            if (!std::isupper(static_cast<unsigned char>(c))) return false;
        }
        return true;
    };
    // Pass 2: per-token vowel drop (keep first char + every consonant).
    std::vector<std::string> abbr;
    abbr.reserve(tokens.size());
    for (auto& t : tokens) {
        if (isAcronymToken(t)) { abbr.push_back(t); continue; }
        std::string a;
        for (size_t i = 0; i < t.size(); ++i) {
            if (i == 0 || !isVowel(t[i])) a.push_back(t[i]);
        }
        if (a.empty()) a.push_back(t[0]);
        abbr.push_back(std::move(a));
    }
    {
        std::string joined;
        for (auto& a : abbr) joined += a;
        if (static_cast<int>(joined.size()) <= maxLen) return joined;
    }
    // Pass 3: collapse repeated consonants inside each token. Letters
    // only — digits stay verbatim so "M33" doesn't get squashed to "M3"
    // before Pass 4's trailing-digit-run check can protect it.
    int totalSize = 0;
    for (auto& a : abbr) {
        std::string c;
        for (char ch : a) {
            const bool isLetter =
                std::isalpha(static_cast<unsigned char>(ch));
            if (!c.empty() && c.back() == ch && isLetter && !isVowel(ch))
                continue;
            c.push_back(ch);
        }
        a = std::move(c);
        totalSize += static_cast<int>(a.size());
    }
    if (totalSize <= maxLen) {
        std::string joined;
        for (auto& a : abbr) joined += a;
        return joined;
    }
    // Pass 4: distribute budget across tokens. A trailing run of ≥2
    // digits per token is treated as identity-like (e.g. "M32", "Snare12")
    // and reserved whole — the letter portion competes with other tokens
    // for the remaining budget. Standalone single digits ("Mic 1") fall
    // through as normal letters. Without this, "MPEQ Main 2 M32" → 7
    // chars truncated to "MPEM2M3" (loses the 2 from M32); with it →
    // "MPM2M32" (digits stay).
    auto trailingDigitRun = [](const std::string& s) -> int {
        int d = 0;
        for (auto it = s.rbegin(); it != s.rend(); ++it) {
            if (std::isdigit(static_cast<unsigned char>(*it))) ++d;
            else break;
        }
        return d >= 2 ? d : 0;
    };

    struct Split { std::string letters; std::string digits; };
    const int n = static_cast<int>(abbr.size());
    std::vector<Split> splits;
    splits.reserve(static_cast<size_t>(n));
    int reservedDigits   = 0;
    int letterTotalSize  = 0;
    int letterTokenCount = 0;
    for (int i = 0; i < n; ++i) {
        const std::string& t = abbr[i];
        const int d = trailingDigitRun(t);
        Split sp{ t.substr(0, t.size() - d), t.substr(t.size() - d) };
        reservedDigits  += d;
        letterTotalSize += static_cast<int>(sp.letters.size());
        if (!sp.letters.empty()) ++letterTokenCount;
        splits.push_back(std::move(sp));
    }

    // If the reserved digit runs alone overflow the budget — or the
    // letter budget can't even give 1 char per letter token — fall back
    // to a hard truncate of the consonant-collapsed joined string.
    const int letterBudget = maxLen - reservedDigits;
    if (reservedDigits >= maxLen || letterBudget < letterTokenCount) {
        std::string joined;
        for (auto& a : abbr) joined += a;
        if (static_cast<int>(joined.size()) > maxLen) joined.resize(maxLen);
        return joined;
    }

    // Reserve 1 char per letter-bearing token (so no word disappears),
    // distribute the remaining flex proportionally to each token's
    // "growth capacity" (size - 1).
    const int flexBudget   = letterBudget - letterTokenCount;
    int growthDenom = 0;
    for (auto& sp : splits) {
        if (!sp.letters.empty()) {
            growthDenom += static_cast<int>(sp.letters.size()) - 1;
        }
    }
    if (growthDenom < 1) growthDenom = 1;  // guard /0 when all letter tokens size 1

    std::string out;
    int letterRemaining       = letterBudget;
    int processedLetterTokens = 0;
    for (int i = 0; i < n; ++i) {
        const auto& sp = splits[i];
        if (!sp.letters.empty()) {
            const bool isLastLetterToken =
                (processedLetterTokens == letterTokenCount - 1);
            int take;
            if (isLastLetterToken) {
                take = letterRemaining;
            } else {
                const int growth = static_cast<int>(sp.letters.size()) - 1;
                const double share = static_cast<double>(flexBudget)
                                   * static_cast<double>(growth)
                                   / static_cast<double>(growthDenom);
                take = 1 + static_cast<int>(share + 0.5);
            }
            if (take > static_cast<int>(sp.letters.size())) {
                take = static_cast<int>(sp.letters.size());
            }
            if (take < 0) take = 0;
            out.append(sp.letters, 0, static_cast<size_t>(take));
            letterRemaining -= take;
            ++processedLetterTokens;
        }
        out.append(sp.digits);
    }
    if (static_cast<int>(out.size()) > maxLen) out.resize(maxLen);
    return out;
}
