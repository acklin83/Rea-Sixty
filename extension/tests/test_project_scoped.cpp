// ProjectScoped: project state follows the project tab (forum, timothys_monster,
// 29.09.2026). Replays the report: a set saved in A, a new empty tab B, back to
// A. Plus the save of A while B is active, a reload in the same tab, and a new
// project that gets the address of a closed one.

#include "ProjectScoped.h"

#include <cstdio>
#include <set>
#include <string>
#include <vector>

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

// Stand-ins for the globals a hook reads: a set's name and the active slot.
static std::string g_name;
static int g_active = 0;

struct State {
    std::string name;
    int active = 0;
};

static void swapLive(State& s)
{
    std::swap(g_name, s.name);
    std::swap(g_active, s.active);
}

using Store = reasixty::ProjectScoped<int, State>;

// The hook bodies as main.cpp has them: they only know the globals.
static void beginLoad() { g_name.clear(); g_active = 0; }
static void processLine(const std::string& name) { g_name = name; }
static std::vector<std::string> save()
{
    std::vector<std::string> lines;
    if (!g_name.empty()) lines.push_back("SELSET_1_DATA " + g_name);
    return lines;
}

int main()
{
    std::set<int> open;
    auto isOpen = [&](int p) { return open.count(p) != 0; };
    const int A = 1, B = 2, C = 3;

    // REAPER loads A at start, before the first timer tick.
    Store store(swapLive);
    open.insert(A);
    store.with(A, [] { beginLoad(); processLine("Drums"); });
    EXPECT(store.switchTo(A, isOpen));
    EXPECT(g_name == "Drums");
    g_active = 1;                       // the user recalls slot 1 in A

    // The report: a new tab with an empty project B. Its begin-load runs with B
    // as the project in load, then the timer sees the tab change.
    open.insert(B);
    store.with(B, [] { beginLoad(); });
    EXPECT(g_name == "Drums");          // B's load left A's live state alone
    EXPECT(store.switchTo(B, isOpen));
    EXPECT(g_name.empty());             // B has no set
    EXPECT(g_active == 0);              // and no active filter from A

    // Saving A while B is active writes A's set, not B's empty state.
    std::vector<std::string> linesA;
    store.with(A, [&] { linesA = save(); });
    EXPECT(linesA.size() == 1 && linesA[0] == "SELSET_1_DATA Drums");
    EXPECT(g_name.empty());             // B's live state untouched by the save

    // Back to A: the set and its recall are there again.
    EXPECT(store.switchTo(A, isOpen));
    EXPECT(g_name == "Drums");
    EXPECT(g_active == 1);
    EXPECT(!store.switchTo(A, isOpen)); // same tab, nothing to do

    // Opening another file in the same tab replaces A's state in place.
    store.with(A, [] { beginLoad(); processLine("Vox"); });
    EXPECT(g_name == "Vox");
    EXPECT(g_active == 0);

    // B is closed in the background; its copy goes. A new project that gets
    // B's address starts empty, even without a begin-load.
    open.erase(B);
    store.prune(isOpen);
    EXPECT(store.parkedCount() == 0);
    open.insert(B);
    EXPECT(store.switchTo(B, isOpen));
    EXPECT(g_name.empty());
    EXPECT(store.parkedCount() == 1);   // A, parked

    // The active tab is closed: its state is dropped, not parked.
    open.erase(B);
    open.insert(C);
    EXPECT(store.switchTo(C, isOpen));
    EXPECT(store.parkedCount() == 1);   // still only A
    EXPECT(store.switchTo(A, isOpen));
    EXPECT(g_name == "Vox");

    // Folder Mode's spilled folders (FOLDERSPILL, Frank 30.09.2026): a GUID set
    // per tab, and every swap asks the rebuild to re-resolve its pointers.
    {
        static std::set<std::string> spilled;
        static bool resolve = false;
        struct Spill { std::set<std::string> guids; };
        using SpillStore = reasixty::ProjectScoped<int, Spill>;
        SpillStore fs([](Spill& st) { std::swap(spilled, st.guids); resolve = true; });
        std::set<int> tabs{1, 2};
        auto tabOpen = [&](int p) { return tabs.count(p) != 0; };

        EXPECT(fs.switchTo(1, tabOpen));
        spilled = {"{DRUMS}", "{TOMS}"};             // two folders open in tab 1
        resolve = false;
        EXPECT(fs.switchTo(2, tabOpen));
        EXPECT(spilled.empty() && resolve);          // tab 2 starts with none
        spilled.insert("{VOX}");
        resolve = false;
        EXPECT(fs.switchTo(1, tabOpen));
        EXPECT((spilled == std::set<std::string>{"{DRUMS}", "{TOMS}"}) && resolve);
        EXPECT(fs.switchTo(2, tabOpen));
        EXPECT((spilled == std::set<std::string>{"{VOX}"}));
    }

    if (g_fail == 0) std::printf("project_scoped: all passed\n");
    return g_fail == 0 ? 0 : 1;
}
