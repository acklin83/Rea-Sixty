// OrcHandover.h: the files ORC and Rea-Sixty hand the surfaces over with
// (docs/orc-uf8-plan.md, section 5). Both programs read what the other wrote,
// so the format is pinned here, including what an older program wrote.

#include "OrcFiles.h"
#include "OrcHandover.h"
#include "TestScratch.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>

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
        const std::string dir = testscratch::makeDir("orc_handover_test");
        EXPECT(!dir.empty());
        if (!dir.empty()) {
            const std::string path = dir + "/handover";
            EXPECT(readFile(path).empty());              // no file, no marker
            EXPECT(parseMarker(readFile(path)).pid == 0);
            EXPECT(writeFile(path, formatMarker(777)));
            EXPECT(parseMarker(readFile(path)).pid == 777);
            EXPECT(parseMarker(readFile(path)).version == 2);
            std::remove(path.c_str());
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
        }
    }

#if defined(__APPLE__) || defined(_WIN32)
    // ORC's files under a name with an umlaut: an account can be called that, and
    // every path here goes to fopen narrow (docs/orc-windows-plan.md, section 4).
    {
        const std::string dir = testscratch::makeDir("orc_m\xC3\xBCller_");
        EXPECT(!dir.empty());
        if (!dir.empty()) {
            testscratch::useAsHome(dir);
            const std::string folder = orc_files::supportFolder();
            EXPECT(!folder.empty());
            EXPECT(folder.find("m\xC3\xBCller") != std::string::npos);
            EXPECT(orc_files::makeOwnerOnlyDir(folder));
            const std::string path = folder + "/rme.json";
            EXPECT(writeFile(path, "{}"));
            EXPECT(readFile(path) == "{}");
            std::error_code ec;
            EXPECT(std::filesystem::is_regular_file(path, ec));
            std::filesystem::remove_all(dir, ec);
        }
    }
#endif

    // A process that lives, and one that does not.
    EXPECT(orc_files::processAlive(orc_files::currentPid()));
    EXPECT(!orc_files::processAlive(0));
    EXPECT(!orc_files::processAlive(-1));
    EXPECT(!orc_files::processAlive(0x7FFFFFF0));

    // ⛔ Replace and remove with the other program reading: under Windows fopen
    // holds a file without FILE_SHARE_DELETE, and Rea-Sixty reads rme.json every
    // second. A reader must never see anything but a whole file, and no replace or
    // remove may fail (the marker would stay behind without a word).
    {
        const std::string dir = testscratch::makeDir("orc_replace_test");
        EXPECT(!dir.empty());
        if (!dir.empty()) {
            const std::string path = dir + "/rme.json";
            const std::string a(4000, 'a'), b(4000, 'b');
            EXPECT(writeFile(path, a));
            std::atomic<bool> run{true};
            std::atomic<int> reads{0}, torn{0}, missed{0};
            std::thread reader([&] {
                while (run.load()) {
                    const std::string got = readFile(path);
                    ++reads;
                    // Under Windows the moment of the swap can refuse the open. That
                    // is no reading at all, and the reader asks again; half a file
                    // would be a wrong one.
                    if (got.empty()) ++missed;
                    else if (got != a && got != b) ++torn;
                }
            });
            int failed = 0;
            for (int i = 0; i < 500; ++i) {
                const std::string tmp = path + ".tmp";
                if (!writeFile(tmp, i % 2 ? a : b) || !orc_files::replaceFile(tmp, path)) ++failed;
            }
            run.store(false);
            reader.join();
            EXPECT(failed == 0);
            EXPECT(torn == 0);
            EXPECT(reads > 0);
            std::printf("replace: %d failed, %d torn, %d not opened, of %d reads\n", failed, torn.load(), missed.load(), reads.load());

            run.store(true);
            std::thread holder([&] {
                while (run.load()) {
                    if (FILE* f = std::fopen((dir + "/handover").c_str(), "rb")) {
                        char c;
                        (void)std::fread(&c, 1, 1, f);
                        std::fclose(f);
                    }
                }
            });
            int notRemoved = 0;
            for (int i = 0; i < 300; ++i) {
                EXPECT(writeFile(dir + "/handover", "1\n"));
                if (!orc_files::removeFile(dir + "/handover")) ++notRemoved;
            }
            run.store(false);
            holder.join();
            EXPECT(notRemoved == 0);
            EXPECT(!orc_files::removeFile(dir + "/handover"));   // was not there

            // Same size, same second: still a change.
            long long m1 = 0, s1 = 0, m2 = 0, s2 = 0;
            EXPECT(writeFile(path, a));
            EXPECT(orc_files::fileStamp(path, m1, s1));
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            EXPECT(writeFile(path, b));
            EXPECT(orc_files::fileStamp(path, m2, s2));
            EXPECT(s1 == s2);
            EXPECT(m1 != m2);
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
        }
    }

    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("orc_handover: all passed\n");
    return 0;
}
