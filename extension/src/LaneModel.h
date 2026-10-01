#pragma once
//
// LaneModel: fixed item lanes on the surfaces (plan docs/fixed-lanes-plan.md,
// Baustein A). Pure logic; main.cpp reads and writes REAPER, tests/test_lane_model.cpp
// a table.
//
// What REAPER gives (REAPER 7.12+, measured 01.10.2026 on 7.81 with the Desktop probe
// rea_sixty_lanes_sonde.lua):
//
//   I_NUMFIXEDLANES       lane count, lanes are 0-based top to bottom
//   C_LANEPLAYS:N         0 silent, 1 plays alone, 2 plays with others.
//                         ⛔ The play set lives HERE only. The chunk's LANESOLO line
//                         started as an inverted mask (4294967281) and became a plain
//                         bitmask later: never read it.
//   LANEREC  a b c d      track chunk; b = the ACTIVE COMP LANE (0-based), -1 = comping
//                         off. ⛔ The comp lane is not named "C1": in the probe it kept
//                         its name "1". Recognise it by LANEREC, never by name.
//                         d changed with every comp edit (0, 1, 2), meaning unknown.
//   LINKEDLANE s e src 0 -1 fin fout
//                         track chunk, one line per comp area: start, end, source lane
//                         (0-based), then three fields we do not use (the 0 matched the
//                         comp lane in the probe; with several comp lanes unmeasured)
//                         and the two fades (0.01). Neighbours overlap by 10 ms.
//   P_RAZOREDITS_EXT      "start end \"\" top bottom", y as a fraction of the track
//                         height; lane i of n is i/n .. (i+1)/n, the same as the
//                         items' F_FREEMODE_Y/H. 42475 takes exactly that lane as the
//                         comp source.
//
// Stepping writes C_LANEPLAYS WITHOUT an undo point: REAPER's own lane actions add
// one entry per click ("Change lane play state"), and a wheel would flood the undo
// history. A/B (LaneAb below) is the way back for "who plays".
//
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace lanes {

// ---- the play set --------------------------------------------------------------

// One entry per lane, 1 = plays. Lanes past the end count as silent everywhere.
using PlaySet = std::vector<char>;

inline PlaySet fromLanePlays(const std::vector<int>& lanePlays)
{
    PlaySet s(lanePlays.size(), 0);
    for (size_t i = 0; i < lanePlays.size(); ++i) s[i] = lanePlays[i] != 0;
    return s;
}

inline int countOn(const PlaySet& s)
{
    return (int)std::count(s.begin(), s.end(), (char)1);
}

inline bool plays(const PlaySet& s, int lane)
{
    return lane >= 0 && lane < (int)s.size() && s[lane];
}

// The values to write to C_LANEPLAYS: one lane on = 1, several = 2 each, off = 0.
inline std::vector<int> toLanePlays(const PlaySet& s)
{
    const int v = countOn(s) > 1 ? 2 : 1;
    std::vector<int> out(s.size(), 0);
    for (size_t i = 0; i < s.size(); ++i) if (s[i]) out[i] = v;
    return out;
}

inline PlaySet resized(PlaySet s, int n)
{
    s.resize(n < 0 ? 0 : n, 0);
    return s;
}

// Same lanes on, ignoring lanes past the shorter end (they are silent).
inline bool same(const PlaySet& a, const PlaySet& b)
{
    const size_t n = std::max(a.size(), b.size());
    for (size_t i = 0; i < n; ++i)
        if ((i < a.size() && a[i]) != (i < b.size() && b[i])) return false;
    return true;
}

inline PlaySet only(int n, int lane)
{
    PlaySet s(n < 0 ? 0 : n, 0);
    if (lane >= 0 && lane < n) s[lane] = 1;
    return s;
}

inline PlaySet all(int n)  { return PlaySet(n < 0 ? 0 : n, 1); }
inline PlaySet none(int n) { return PlaySet(n < 0 ? 0 : n, 0); }

// This lane into the set or out of it (REAPER's Ctrl-click). Taking all but one out
// is exclusive again; taking the last one out leaves nothing playing, like CUT.
inline PlaySet toggled(PlaySet s, int lane)
{
    if (lane < 0) return s;
    if (lane >= (int)s.size()) s.resize(lane + 1, 0);
    s[lane] = !s[lane];
    return s;
}

// The set copied onto a group mate with `n` lanes, lane numbers kept. Lanes the mate
// does not have drop out; when nothing is left the result is empty and the caller
// leaves that track alone (better than silencing it).
inline PlaySet pattern(const PlaySet& s, int n)
{
    PlaySet out = resized(s, n);
    return countOn(out) ? out : PlaySet{};
}

// ---- stepping --------------------------------------------------------------------

// The lane the wheel steps from: the one it last stepped to while that still plays,
// else the first playing lane, else -1.
inline int heardLane(const PlaySet& s, int remembered)
{
    if (plays(s, remembered)) return remembered;
    for (int i = 0; i < (int)s.size(); ++i) if (s[i]) return i;
    return -1;
}

// Next lane from `from` in `dir` (+1 down, -1 up), no wrap. `compLane` (LANEREC, -1
// = none) is passed over when `skipComp`. From -1 (nothing plays) +1 starts at the
// top and -1 at the bottom. -1 = nowhere to go (also what dims the ↑ / ↓ LED).
inline int stepLane(int n, int from, int dir, int compLane, bool skipComp)
{
    if (n <= 0 || dir == 0) return -1;
    dir = dir > 0 ? 1 : -1;
    int i = from < 0 || from >= n ? (dir > 0 ? 0 : n - 1) : from + dir;
    for (; i >= 0 && i < n; i += dir)
        if (!(skipComp && i == compLane)) return i;
    return -1;
}

// ---- A/B ---------------------------------------------------------------------------

// Per track: what played and what played before it. Fed with every play set seen,
// ours and REAPER's (a punch-in switches to the new lane, and A/B has to bring the
// set from before it back).
//
// A layered set waits while the wheel previews: stepping one single lane to the next
// keeps the layered set from before as the A/B partner, so "listen in, back to the
// layering" stays one press however many detents it took (plan, Baustein E).
struct LaneAb {
    PlaySet cur, prev;
    bool known = false;
    bool hasPrev = false;

    void observe(const PlaySet& now)
    {
        if (!known) { cur = now; known = true; return; }
        if (same(now, cur)) { cur = now; return; }
        const bool previewing = countOn(now) == 1 && countOn(cur) == 1;
        if (!(previewing && hasPrev && countOn(prev) > 1)) { prev = cur; hasPrev = true; }
        cur = now;
    }

    // The set to switch to, swapped in already so the observe() of what we write
    // changes nothing. Empty = nothing to go back to.
    PlaySet recall(int n)
    {
        if (!hasPrev) return {};
        const PlaySet target = pattern(prev, n);
        if (target.empty()) return {};
        prev = cur;
        cur = target;
        return target;
    }
};

// ---- razor on one lane -------------------------------------------------------------

struct Span { double top = 0.0, bottom = 1.0; };

inline Span laneSpan(int lane, int n)
{
    if (n <= 0 || lane < 0 || lane >= n) return {};
    return { (double)lane / n, (double)(lane + 1) / n };
}

// One P_RAZOREDITS_EXT entry for lane `lane` of `n`, media lane (no envelope GUID).
inline std::string razorEntry(double start, double end, int lane, int n)
{
    const Span y = laneSpan(lane, n);
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%.15g %.15g \"\" %.6f %.6f", start, end, y.top, y.bottom);
    return buf;
}

// ---- the comp map (track chunk) --------------------------------------------------

struct CompArea { double start = 0.0, end = 0.0; int srcLane = -1; };

struct CompInfo {
    int compLane = -1;                 // LANEREC field 2, -1 = comping off
    std::vector<CompArea> areas;       // sorted by start
};

// One LINKEDLANE line after the keyword: start, end, source lane.
inline bool parseLinkedLane(const char* q, CompArea& a)
{
    char* end = nullptr;
    a.start = std::strtod(q, &end);
    if (end == q) return false;
    q = end;
    a.end = std::strtod(q, &end);
    if (end == q) return false;
    q = end;
    a.srcLane = (int)std::strtol(q, &end, 10);
    return end != q;
}

inline bool isKey(const char* line, const char* key)
{
    const size_t n = std::strlen(key);
    return std::strncmp(line, key, n) == 0 && (line[n] == ' ' || line[n] == '\t');
}

// LANEREC and LINKEDLANE from the track's own level of the chunk: lines inside
// <ITEM>, <FXCHAIN> and the rest are skipped (base64 and item lines live there).
inline CompInfo compFromChunk(const char* chunk)
{
    CompInfo info;
    if (!chunk) return info;
    int depth = 0;
    for (const char* p = chunk; *p; ) {
        const char* line = p;
        while (*line == ' ' || *line == '\t') ++line;
        if (*line == '<') ++depth;
        else if (*line == '>') --depth;
        else if (depth == 1 && isKey(line, "LANEREC")) {
            char* end = nullptr;
            std::strtol(line + 7, &end, 10);
            const char* f2 = end;
            const long v = std::strtol(f2, &end, 10);
            if (end != f2) info.compLane = (int)v;
        } else if (depth == 1 && isKey(line, "LINKEDLANE")) {
            CompArea a;
            if (parseLinkedLane(line + 10, a)) info.areas.push_back(a);
        }
        while (*p && *p != '\n') ++p;
        if (*p == '\n') ++p;
    }
    std::stable_sort(info.areas.begin(), info.areas.end(),
                     [](const CompArea& a, const CompArea& b) { return a.start < b.start; });
    return info;
}

// The area that plays at `t`: inside the 10 ms overlap of two neighbours that is the
// later one, the one whose start the cut sits on. -1 = none.
inline int areaAt(const std::vector<CompArea>& areas, double t)
{
    int hit = -1;
    for (int i = 0; i < (int)areas.size(); ++i)
        if (areas[i].start <= t && t < areas[i].end) hit = i;
    return hit;
}

// The stops for ← / →: every area start and end plus `extra` (item edges of the heard
// lane), sorted, and edges closer than `merge` folded into the earliest of them, so a
// cut with its 10 ms overlap is ONE stop and sits on the later area's start.
inline std::vector<double> edges(const std::vector<CompArea>& areas,
                                 std::vector<double> extra, double merge = 0.02)
{
    for (const CompArea& a : areas) { extra.push_back(a.start); extra.push_back(a.end); }
    std::sort(extra.begin(), extra.end());
    std::vector<double> out;
    for (double e : extra)
        if (out.empty() || e - out.back() > merge) out.push_back(e);
    return out;
}

// First edge after / last edge before `t`, or `t` itself when there is none.
// `eps` keeps the cursor from sticking on the edge it already sits on.
inline double nextEdge(const std::vector<double>& es, double t, double eps = 1e-6)
{
    for (double e : es) if (e > t + eps) return e;
    return t;
}
inline double prevEdge(const std::vector<double>& es, double t, double eps = 1e-6)
{
    for (auto it = es.rbegin(); it != es.rend(); ++it) if (*it < t - eps) return *it;
    return t;
}

} // namespace lanes
