#pragma once
//
// McpFolderCollapse: children of a folder collapsed in the MIXER leave the surface
// while it mirrors the MCP. Pure logic; main.cpp reads the chunks and walks REAPER's
// parents, tests/test_mcp_folder_collapse.cpp a table.
//
// ⛔ IsTrackVisible(track, true) DOES NOT KNOW THIS STATE. Measured 30.09.2026 on
// REAPER 7.81 (Desktop probe rea_sixty_ordner_sonde.lua): folder collapsed in the
// mixer, TCP open, every child still answered "visible" while its strip had
// I_MCPW 0. CSI asks the same call and has the same gap. There is no API for the
// state either: it lives only in the folder's track chunk, second field of
//
//   BUSCOMP <tcp> <mixer> <wiring> <x> <y>
//
// tcp = I_FOLDERCOMPACT (0 normal, 1 small, 2 hidden), mixer = 0 open, 1 collapsed.
// The TCP side needs nothing from here: IsTrackVisible(track, false) already drops
// the children of a folder set to hidden, and a folder set to small keeps them on
// the surface because REAPER still draws them (Frank 30.09.: only hidden is hidden).
//
#include <cstdlib>
#include <cstring>

namespace mcpfold {

// Second BUSCOMP field of a track chunk == 1. The first BUSCOMP line is the track's
// own; FX blocks come after it and their base64 lines hold no spaced keyword.
inline bool mixerCollapsedFromChunk(const char* chunk)
{
    if (!chunk) return false;
    const char* p = chunk;
    while (*p) {
        const char* line = p;
        while (*line == ' ' || *line == '\t') ++line;
        if (std::strncmp(line, "BUSCOMP", 7) == 0 && (line[7] == ' ' || line[7] == '\t')) {
            char* end = nullptr;
            std::strtol(line + 7, &end, 10);                 // TCP state
            if (!end || end == line + 7) return false;
            const char* f2 = end;
            const long mixer = std::strtol(f2, &end, 10);
            if (end == f2) return false;
            return mixer == 1;
        }
        while (*p && *p != '\n') ++p;
        if (*p == '\n') ++p;
    }
    return false;
}

// Does any ancestor of `tr` sit in `collapsed`? parentOf(nullptr-like) ends the walk.
template <class Track, class ParentOf, class Set>
bool underCollapsedFolder(Track tr, ParentOf parentOf, const Set& collapsed)
{
    if (collapsed.empty()) return false;
    for (Track anc = parentOf(tr); anc; anc = parentOf(anc))
        if (collapsed.count(anc)) return true;
    return false;
}

} // namespace mcpfold
