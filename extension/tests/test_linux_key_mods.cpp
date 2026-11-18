// LinuxKeyMods.h: X11 modifier mask to Shift / Ctrl / Alt. The bits are the
// ones in xorgproto's X.h; a wrong one here means a key on Linux that engages
// the wrong modifier slot on every surface.

#include "LinuxKeyMods.h"

#include <cstdio>

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

int main()
{
    using linuxkeys::decode;
    constexpr unsigned Shift = 1u << 0, Lock = 1u << 1, Control = 1u << 2,
                       Mod1 = 1u << 3, Mod2 = 1u << 4, Mod4 = 1u << 6,
                       Mod5 = 1u << 7, Button1 = 1u << 8;

    { auto m = decode(0);       EXPECT(!m.shift && !m.ctrl && !m.alt); }
    { auto m = decode(Shift);   EXPECT(m.shift && !m.ctrl && !m.alt); }
    { auto m = decode(Control); EXPECT(!m.shift && m.ctrl && !m.alt); }
    { auto m = decode(Mod1);    EXPECT(!m.shift && !m.ctrl && m.alt); }
    { auto m = decode(Control | Mod1 | Shift); EXPECT(m.shift && m.ctrl && m.alt); }
    // Latches and other keys are not held modifiers: Caps Lock, Num Lock
    // (Mod2), Super (Mod4), AltGr (Mod5), a mouse button.
    { auto m = decode(Lock | Mod2 | Mod4 | Mod5 | Button1);
      EXPECT(!m.shift && !m.ctrl && !m.alt); }
    // Num Lock on does not hide a held Ctrl.
    { auto m = decode(Mod2 | Control); EXPECT(m.ctrl && !m.alt); }

    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("linux_key_mods: ok\n");
    return 0;
}
