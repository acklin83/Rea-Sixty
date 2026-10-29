#pragma once
//
// ⇨ THE FIVE BRIGHTNESS STEPS, the bytes each surface gets for them. Moved out
// of main.cpp on 29.10.2026 so ORC steps through the same five (Frank: the
// brightness actions in ORC too). Rea-Sixty's Brightness sliders and actions,
// ORC's actions, and both programs' wake from sleep read this one table.
// SSL 360° names the steps dark, dim, half, bright, full.

#include <cstdint>

namespace brightness {

enum Level : int { Dark = 0, Dim = 1, Half = 2, Bright = 3, Full = 4 };

inline int clampLevel(int level) { return level < Dark ? Dark : level > Full ? Full : level; }

struct Bytes {
    uint8_t uf8_led; uint8_t uf8_lcd;
    uint8_t uc1_led; uint8_t uc1_lcd; uint8_t uc1_status;
    // ⇨ THE UF1 IS ON THE SLIDERS TOO, since 2026-09-07. It never was: the
    // action list had brightness down as blocked for want of a decoded frame,
    // and pushBrightness only ever called the UF8 and UC1 paths. Sleep needed
    // those frames and found them, so the sliders get them as well (Frank asked
    // for it the moment sleep worked).
    //
    // The LED master shares the UF8's opcode AND its scale, so it reuses uf8_led.
    //
    // ⚠ THE UF1's BIG SCREEN NEEDS ITS OWN COLUMN. Driving it off uf8_lcd read a
    // full step darker than the other surfaces at every setting, and the bottom
    // step went out altogether where the UF8 and UC1 keep a visible glow (Frank,
    // at the device, 2026-09-07). uf1_lcd is uf8_lcd moved up one step with the
    // top opened toward full scale. Corrected by eye, not measured.
    //
    // ⛔ THE BOTTOM STEP IS NOT OFF. "Dark" is the dimmest a user can dial, and
    // it has to stay legible — going out at 0 makes the surface look broken and
    // takes the panel away for a setting that is meant to keep it. Only SLEEP
    // sends a real 0, and it does that outside this table.
    //
    // There is no column for the small channel LCD: the only frame that reaches
    // it (0x47) turned out to be a panel master over the LEDs as well, so it is
    // not a slider's to move. See buildMasterBrightness.
    uint8_t uf1_lcd;
};
inline constexpr Bytes kTable[5] = {
    {0x05, 0x18, 0x0A, 0x18, 0x08, 0x30},  // dark
    {0x0A, 0x30, 0x13, 0x30, 0x0F, 0x50},  // dim
    {0x10, 0x50, 0x20, 0x50, 0x19, 0x60},  // half
    {0x13, 0x60, 0x26, 0x60, 0x1E, 0xA0},  // bright
    {0x20, 0xA0, 0x40, 0xA0, 0x32, 0xE0},  // full
};

} // namespace brightness
