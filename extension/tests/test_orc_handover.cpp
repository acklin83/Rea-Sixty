// OrcHandover.h: the files ORC and Rea-Sixty hand the surfaces over with
// (docs/orc-uf8-plan.md, section 5). Both programs read what the other wrote,
// so the format is pinned here, including what an older program wrote.

#include "OrcHandover.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

using namespace orc_handover;

int main()
{
    // The marker this Rea-Sixty writes.
    EXPECT(formatMarker(4711) == "4711\nv2\n");
    {
        const Marker m = parseMarker(formatMarker(4711));
        EXPECT(m.pid == 4711);
        EXPECT(m.version == 2);
    }
    // What Rea-Sixty up to 0.7.x wrote: the pid alone. A new ORC must still
    // let go on it, and know that this REAPER will not wait for a receipt.
    {
        const Marker m = parseMarker("4711\n");
        EXPECT(m.pid == 4711);
        EXPECT(m.version == 1);
    }
    // ORC 1.2.x reads the marker with fscanf("%d"): the new marker must still
    // start with the pid and nothing before it.
    {
        int pid = 0;
        EXPECT(std::sscanf(formatMarker(99).c_str(), "%d", &pid) == 1 && pid == 99);
    }
    // Nothing usable.
    EXPECT(parseMarker("").pid == 0);
    EXPECT(parseMarker("").version == 0);
    EXPECT(parseMarker("abc").pid == 0);
    EXPECT(parseMarker("-5\nv2\n").pid == 0);
    EXPECT(parseMarker("0\n").version == 0);
    // An unknown second line is a pid-only marker, not a v2 one.
    EXPECT(parseMarker("12\nv3\n").version == 1);

    // orc.pid and handover.released.
    EXPECT(formatPid(321) == "321\n");
    EXPECT(parsePid(formatPid(321)) == 321);
    EXPECT(parsePid("") == 0);
    EXPECT(parsePid("x") == 0);
    EXPECT(parsePid("0\n") == 0);

    // The receipt carries the stands' positions on its second line; an older
    // reader still finds the pid first.
    {
        const std::string t = formatReleased(4711, "10.0.0.1,10,90,50;10.0.0.2,0,45,30;");
        EXPECT(parsePid(t) == 4711);
        const Released r = parseReleased(t);
        EXPECT(r.pid == 4711);
        EXPECT(r.positions == "10.0.0.1,10,90,50;10.0.0.2,0,45,30;");
        EXPECT(parseReleased("4711\n").positions.empty());   // ORC without stands
        EXPECT(parseReleased(formatPid(9)).pid == 9);
    }

    // Through the file system, as the two programs do it.
    {
        char tmpl[] = "/tmp/orc_handover_testXXXXXX";
        const char* dir = ::mkdtemp(tmpl);
        EXPECT(dir != nullptr);
        if (dir) {
            const std::string path = std::string(dir) + "/handover";
            EXPECT(readFile(path).empty());              // no file, no marker
            EXPECT(parseMarker(readFile(path)).pid == 0);
            EXPECT(writeFile(path, formatMarker(777)));
            EXPECT(parseMarker(readFile(path)).pid == 777);
            EXPECT(parseMarker(readFile(path)).version == 2);
            std::remove(path.c_str());
            ::rmdir(dir);
        }
    }

    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("orc_handover: all passed\n");
    return 0;
}
