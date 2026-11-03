#pragma once
//
// Shift on the Mac's keyboard, read at the moment of a press. Only for a gesture
// that asks: ORC runs in the background, so a standing mirror as in Rea-Sixty
// would switch the soft keys' Shift lamps with every capital typed anywhere.

#include <CoreGraphics/CoreGraphics.h>

namespace orc {

inline bool keyboardShiftHeld()
{
    return (CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState)
            & kCGEventFlagMaskShift) != 0;
}

} // namespace orc
