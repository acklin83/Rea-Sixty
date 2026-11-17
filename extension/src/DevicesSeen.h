#pragma once
//
// DevicesSeen: the arithmetic of the device inventory (main.cpp, g_devicesSeen,
// ExtState rea_sixty/devices_seen). Pure; tests/test_devices_seen.cpp replays
// the start that lost the UF8 tab.
//
// ⛔ A MARK MERGES WITH WHAT IS STORED, not only with what is in memory. At
// REAPER start the devices open in initDevices_ BEFORE loadBrightness reads the
// stored inventory, so the memory mask is still 0 there. Marking against memory
// alone wrote "UC1 only" over a stored "UF8 + UC1" whenever ORC held the UF8 at
// start, and the UF8 that arrived later from ORC was never in the list: no UF8
// tab under Bindings (forum, 17.11.2026).
//
namespace devseen {

constexpr int kAll = 0b111;

struct Mark {
    int  mask;    // the inventory after the mark
    bool write;   // the stored copy must be rewritten
};

// `stored` = what ExtState has, `known` = the memory mask, `bit` = the surface
// that just opened.
inline Mark mark(int stored, int known, int bit)
{
    const int m = (stored | known | bit) & kAll;
    return { m, m != (stored & kAll) };
}

}  // namespace devseen
