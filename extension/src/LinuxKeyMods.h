#pragma once
//
// LinuxKeyMods: which keyboard modifiers an X11 modifier mask says are held.
// Pure; linux_keys.cpp asks the X server for the mask (XQueryPointer, the same
// question SWELL's GetAsyncKeyState asks GDK inside REAPER on Linux), and
// tests/test_linux_key_mods.cpp is the table.
//
// The bits are X11's own (xorgproto include/X11/X.h, read 18.11.2026):
//   ShiftMask (1<<0), LockMask (1<<1), ControlMask (1<<2), Mod1Mask (1<<3),
//   Mod4Mask (1<<6, Super on every common keymap), Mod5Mask (1<<7).
// Alt is Mod1. AltGr is ISO_Level3_Shift on Mod5, so it never reads as Alt or
// Ctrl here, and Linux needs none of Windows' AltGr carve-out. Caps Lock and
// Num Lock (Mod2) are latches, not modifiers you hold: they are ignored.
//
namespace linuxkeys {

constexpr unsigned kShiftMask   = 1u << 0;
constexpr unsigned kControlMask = 1u << 2;
constexpr unsigned kMod1Mask    = 1u << 3;   // Alt

struct Mods {
    bool shift = false;
    bool ctrl  = false;
    bool alt   = false;
};

inline Mods decode(unsigned mask)
{
    Mods m;
    m.shift = (mask & kShiftMask)   != 0;
    m.ctrl  = (mask & kControlMask) != 0;
    m.alt   = (mask & kMod1Mask)    != 0;
    return m;
}

// The held modifiers right now, from the X server. All false when there is no
// X server or libX11 (see linux_keys.cpp). Main thread only. Linux only: the
// other platforms read their own OS in main.cpp.
Mods held();

}  // namespace linuxkeys
