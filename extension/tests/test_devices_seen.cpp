// The device inventory (DevicesSeen.h): the REAPER start that lost the UF8 tab
// under Bindings when ORC held the UF8 (forum, 17.11.2026), replayed in main.cpp's
// order with ExtState as a plain int.

#include "DevicesSeen.h"

#include <cstdio>

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

constexpr int kUf8 = 0b001, kUc1 = 0b010, kUf1 = 0b100;

// main.cpp: markDeviceSeen_ and loadBrightness, against a fake ExtState.
struct Rig {
    int stored = 0;   // ExtState rea_sixty/devices_seen
    int known  = 0;   // g_devicesSeen
    int writes = 0;
    void mark(int bit) {
        const devseen::Mark m = devseen::mark(stored, known, bit);
        known |= m.mask;
        if (m.write) { stored = m.mask; ++writes; }
    }
    void load() { known = stored; }   // loadBrightness: store(), not or
};

int main()
{
    // ORC holds the UF8 at start: the UC1 opens in initDevices_, before the
    // load, then the UF8 arrives from ORC through the retry.
    {
        Rig r; r.stored = kUf8 | kUc1;
        r.mark(kUc1);                       // initDevices_, memory still 0
        EXPECT(r.stored == (kUf8 | kUc1));  // the UF8 stays in the file
        EXPECT(r.writes == 0);              // nothing new, nothing written
        r.load();
        EXPECT(r.known & kUf8);             // the tab shows
        r.mark(kUf8);                       // retry path
        EXPECT(r.known == (kUf8 | kUc1));
    }
    // First machine where the UF8 only ever lived in ORC: the late UF8 is
    // entered by the retry path's mark.
    {
        Rig r; r.stored = kUc1;
        r.mark(kUc1);
        r.load();
        EXPECT(!(r.known & kUf8));
        r.mark(kUf8);
        EXPECT(r.known & kUf8);
        EXPECT(r.stored == (kUf8 | kUc1));
        EXPECT(r.writes == 1);
        // Next start, ORC holds it again: still there.
        Rig n; n.stored = r.stored;
        n.mark(kUc1);
        n.load();
        EXPECT(n.known & kUf8);
    }
    // A new surface adds to the file, nothing falls out.
    {
        Rig r; r.stored = kUf8;
        r.mark(kUf1);
        EXPECT(r.stored == (kUf8 | kUf1));
        r.load();
        EXPECT(r.known == (kUf8 | kUf1));
    }
    // Garbage above the three bits is not carried.
    EXPECT(devseen::mark(0xFF, 0, kUf8).mask == devseen::kAll);
    EXPECT(!devseen::mark(0xFF, 0, kUf8).write);

    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("devices_seen: ok\n");
    return 0;
}
