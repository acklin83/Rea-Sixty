// McpFolderCollapse: a folder collapsed in the mixer takes its children off the
// surface while it mirrors the MCP. Chunks shaped like REAPER 7.81's (probe
// 30.09.2026), a folder tree as a parent table.

#include "McpFolderCollapse.h"

#include <cstdio>
#include <map>
#include <set>

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

using mcpfold::mixerCollapsedFromChunk;
using mcpfold::underCollapsedFolder;

int main()
{
    // The three probe runs: BUSCOMP "2 0", "0 1", "1 1".
    EXPECT(!mixerCollapsedFromChunk("<TRACK\nNAME \"\"\nBUSCOMP 2 0 0 0 0\nSHOWINMIX 1 0.6667 0.5 1 0.5 0 0 0\n>"));
    EXPECT( mixerCollapsedFromChunk("<TRACK\nNAME \"\"\nBUSCOMP 0 1 0 0 0\n>"));
    EXPECT( mixerCollapsedFromChunk("<TRACK\nNAME \"\"\nBUSCOMP 1 1 0 0 0\n>"));
    // Indented like an .rpp, and CRLF.
    EXPECT( mixerCollapsedFromChunk("<TRACK\r\n  NAME x\r\n  BUSCOMP 0 1 0 0 0\r\n>"));
    // No BUSCOMP, empty, null, a name that merely contains the word.
    EXPECT(!mixerCollapsedFromChunk("<TRACK\nNAME \"BUSCOMP 0 1\"\n>"));
    EXPECT(!mixerCollapsedFromChunk(""));
    EXPECT(!mixerCollapsedFromChunk(nullptr));
    EXPECT(!mixerCollapsedFromChunk("<TRACK\nBUSCOMP\n>"));
    EXPECT(!mixerCollapsedFromChunk("<TRACK\nBUSCOMP 0\n>"));
    // The track's own line wins over anything further down.
    EXPECT(!mixerCollapsedFromChunk("<TRACK\nBUSCOMP 0 0 0 0 0\n<FXCHAIN\nBUSCOMP 0 1 0 0 0\n>\n>"));

    // Tree:  1 folder
    //          2
    //          3 folder
    //            4
    //          5
    //        6
    std::map<int, int> parent{ {2, 1}, {3, 1}, {4, 3}, {5, 1} };
    auto parentOf = [&](int t) { auto it = parent.find(t); return it == parent.end() ? 0 : it->second; };

    std::set<int> none;
    for (int t = 1; t <= 6; ++t) EXPECT(!underCollapsedFolder(t, parentOf, none));

    // Outer collapsed: everything below it goes, inner folder included; 1 and 6 stay.
    std::set<int> outer{ 1 };
    EXPECT(!underCollapsedFolder(1, parentOf, outer));
    EXPECT( underCollapsedFolder(2, parentOf, outer));
    EXPECT( underCollapsedFolder(3, parentOf, outer));
    EXPECT( underCollapsedFolder(4, parentOf, outer));
    EXPECT( underCollapsedFolder(5, parentOf, outer));
    EXPECT(!underCollapsedFolder(6, parentOf, outer));

    // Inner collapsed, outer open: only 4 goes, its folder 3 stays.
    std::set<int> inner{ 3 };
    EXPECT(!underCollapsedFolder(2, parentOf, inner));
    EXPECT(!underCollapsedFolder(3, parentOf, inner));
    EXPECT( underCollapsedFolder(4, parentOf, inner));
    EXPECT(!underCollapsedFolder(5, parentOf, inner));

    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("mcp_folder_collapse: all passed\n");
    return 0;
}
