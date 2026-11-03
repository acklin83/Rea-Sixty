#pragma once
//
// Shift on the computer's keyboard, read at the moment of a press. Only for a gesture
// that asks: ORC runs in the background, so a standing mirror as in Rea-Sixty
// would switch the soft keys' Shift lamps with every capital typed anywhere.

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <CoreGraphics/CoreGraphics.h>
#endif

namespace orc {

inline bool keyboardShiftHeld()
{
#if defined(_WIN32)
    // 0 while an elevated program has the foreground: then Shift does nothing.
    return (::GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
#else
    return (CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState)
            & kCGEventFlagMaskShift) != 0;
#endif
}

} // namespace orc
