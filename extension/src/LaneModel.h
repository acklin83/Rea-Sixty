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

// ---- loop around the cut ----------------------------------------------------------------
// SHIFT + ← (Frank 02.10.2026): `len` seconds centred on the cut nearest `t` (the one
// the cursor sits on after ← / →), from `edges` as ← / → use them. No edge at all:
// centred on `t`. The loop does not start before the project.
inline double nearestEdge(const std::vector<double>& edges, double t)
{
    double best = t, dist = -1.0;
    for (double e : edges) {
        const double d = e > t ? e - t : t - e;
        if (dist < 0.0 || d < dist) { best = e; dist = d; }
    }
    return best;
}
inline void loopAround(double cut, double len, double& s, double& e)
{
    if (!(len > 0.0)) len = 2.0;
    s = cut - len / 2.0;
    e = cut + len / 2.0;
    if (s < 0.0) s = 0.0;
}

// ⇨ SHIFT + ← AGAIN AT THE SAME CUT SWITCHES THE LOOP OFF (Frank 02.10.2026). "Same"
// = the loop is still exactly the one this set and the new one would be it again.
// At another cut it moves there. Off puts repeat back as it was before the first
// press; a loop changed by hand in between counts as none of ours.
struct LoopToggle {
    bool   active = false;
    double s = 0.0, e = 0.0;
    int    repeatBefore = 0;

    enum class Act { Set, Clear };
    // curS/curE: the loop REAPER has now, newS/newE: the one around the cut,
    // repeat: REAPER's repeat now. Updates the state; the caller applies Act
    // (Clear: no loop, repeat back to repeatBefore).
    Act press(double curS, double curE, double newS, double newE, int repeat)
    {
        auto same = [](double a, double b) { return a - b < 1e-6 && b - a < 1e-6; };
        const bool ours = active && same(curS, s) && same(curE, e);
        if (ours && same(newS, s) && same(newE, e)) { active = false; return Act::Clear; }
        if (!ours) repeatBefore = repeat;
        active = true;
        s = newS;
        e = newE;
        return Act::Set;
    }
};

// ---- no slivers at the cuts ------------------------------------------------------------
// REAPER lets neighbouring comp areas overlap by 10 ms. A stroke that starts where the
// last one ended cut the old area there and left its 10 ms head plus overlap behind,
// 20 ms of the old lane between the two strokes (Frank 02.10.2026, Test.RPP:
// 18.98-20.98 lane 5, 20.97-20.99 lane 8, 20.98-22.98 lane 6). So before writing
// [s, e] from `lane`: where an area of another lane would keep less than `sliver` on
// either side of the new stretch, the stretch takes that bit too.
inline void absorbSlivers(const std::vector<CompArea>& areas, int lane,
                          double& s, double& e, double sliver = 0.03)
{
    for (const CompArea& a : areas) {
        if (a.srcLane == lane) continue;
        if (a.start < s && s < a.end && s - a.start < sliver) s = a.start;
        if (a.start < e && e < a.end && a.end - e < sliver) e = a.end;
    }
}

// ---- painting and live comping (Baustein F) ----------------------------------------

// The stretch a stroke paints: from where the hold began to where the cursor got,
// either way round (turning back takes back what went too far), both cuts moved
// `leadIn` earlier so a cut lands just before a consonant rather than on it.
// False when nothing is left to paint.
inline bool strokeRange(double anchor, double at, double leadIn, double& s, double& e)
{
    s = std::min(anchor, at);
    e = std::max(anchor, at);
    if (e - s < 1e-4) return false;
    s -= leadIn;
    e -= leadIn;
    if (s < 0.0) s = 0.0;
    return e - s >= 1e-4;
}

// Live comping: while the transport runs, every change of the heard lane is a cut,
// and the section that ends there goes into the comp at once (one undo step per cut,
// plan decision 6). Nothing is written before the first change: the first one opens
// the first section.
struct LiveSection { int lane = -1; double start = 0.0, end = 0.0; };

struct LiveComp {
    bool   open  = false;
    int    lane  = -1;
    double start = 0.0;

    // The heard lane is now `newLane`, from `t` on (-1: several lanes or the comp
    // lane play, nothing to take a section from). True when a section ended here.
    //
    // A lane that played for less than `settle` was passed on the way: turning the
    // wheel from take 2 to take 5 walks over 3 and 4. It is never written; the lane
    // you stop on takes its start (or, for -1, it is dropped).
    bool change(int newLane, double t, LiveSection& out, double settle = 0.0)
    {
        if (open && t - start < settle) {
            if (newLane >= 0) lane = newLane;
            else { open = false; lane = -1; }
            return false;
        }
        const bool done = finish(t, out);
        open  = newLane >= 0;
        lane  = newLane;
        start = t;
        return done;
    }

    // Stop, the loop jumping back, live comping off, another track: the open section
    // ends at `t` and none opens. After a loop jump the next pass writes only where
    // it changes lanes, so a pass without a change leaves the comp alone (Frank
    // 02.10.2026).
    bool close(double t, LiveSection& out)
    {
        const bool done = finish(t, out);
        open = false;
        lane = -1;
        return done;
    }

    bool finish(double t, LiveSection& out) const
    {
        if (!open || t - start < 1e-4) return false;
        out = { lane, start, t };
        return true;
    }
};

// The play position went back since the last look: the loop jumped to its start, or
// someone seeked. `slack` swallows the jitter of the reported position.
inline bool jumpedBack(double last, double now, double slack = 0.05)
{
    return now < last - slack;
}

// ---- the Lanes bank (Baustein C) -----------------------------------------------------
// Key N of the dynamic bank "Lanes" is lane N of the lane track (UF8 eight a page,
// UF1 four). Frank 02.10.2026: takes white, the active comp lane green; no long
// press yet (making a lane the comp lane has no API and was not probed).
namespace bank {

struct Key {
    bool        present = false;
    std::string label;
    int         led     = 0;      // 0 dark, 1 dim (there, silent), 2 lit (plays)
    bool        comp    = false;  // the active comp lane: its own colour
};

// `name` as P_LANENAME gives it ("" or the lane's number when it has none of its
// own). A track without lanes shows one key, "Lanes on", on key 0.
// ⇨ COMPING (a comp lane, live comping off; Frank 02.10.2026): the comp plays all
// the time, so a take's lamp says something else: lit = the comp area under the
// cursor (`areaSrc`, -1 none) comes from this take.
inline Key key(int lane, int n, bool trackHasLanes, const std::string& name,
               bool plays, int compLane, bool comping = false, int areaSrc = -1)
{
    Key k;
    if (!trackHasLanes) {
        if (lane == 0) { k.present = true; k.label = "Lanes on"; k.led = 1; }
        return k;
    }
    if (lane < 0 || lane >= n) return k;
    k.present = true;
    k.comp    = (lane == compLane);
    if (k.comp)                                                   k.label = "Comp";
    else if (name.empty() || name == std::to_string(lane + 1))    k.label = "Lane " + std::to_string(lane + 1);
    else                                                          k.label = name;
    k.led = (comping && !k.comp) ? (lane == areaSrc ? 2 : 1) : (plays ? 2 : 1);
    return k;
}

// AreaTake: the comp area under the cursor takes this lane. Audition: hear this
// lane alone while SHIFT is held, the comp comes back when it is let go.
enum class Op { None, Solo, Toggle, CompHere, LanesOn, AreaTake, Audition };

// gesture as the dynamic banks post it: 0 push, 1 Shift, 2 Cmd, 3 Ctrl, 4 long.
// Push = this lane alone, Shift = into / out of the lanes that play, Cmd = Comp
// here from this lane (not from the comp lane itself). Ctrl and long are free.
inline Op op(int gesture, int lane, int n, bool trackHasLanes, int compLane,
              bool comping = false)
{
    if (!trackHasLanes) return (gesture == 0 && lane == 0) ? Op::LanesOn : Op::None;
    if (lane < 0 || lane >= n) return Op::None;
    if (comping && lane != compLane) {
        switch (gesture) {
            case 0: return Op::AreaTake;
            case 1: return Op::Audition;
            case 2: return Op::CompHere;
            default: return Op::None;
        }
    }
    switch (gesture) {
        case 0: return Op::Solo;
        case 1: return Op::Toggle;
        case 2: return lane == compLane ? Op::None : Op::CompHere;
        default: return Op::None;
    }
}

// Keys a bank shows for `n` lanes: one ("Lanes on") on a track without them.
inline int itemCount(int n, bool trackHasLanes) { return trackHasLanes ? (n > 0 ? n : 0) : 1; }

// The page that shows `lane`, `perPage` keys a page.
inline int pageOf(int lane, int perPage) { return (lane < 0 || perPage <= 0) ? 0 : lane / perPage; }

} // namespace bank

} // namespace lanes
