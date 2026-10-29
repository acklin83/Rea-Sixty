#pragma once
//
// ⇨ BRIGHTNESS ON A KEY IN ORC (Frank 29.10.2026): Rea-Sixty's six actions under
// their names and descriptions (main.cpp registerBuiltin), LEDs, the displays,
// or both, one step each way. ORC's Surface registers them with the step that
// writes rme.json; tools/orc_manual_dump with none, so the manual lists exactly
// what the window offers.

#include "Bindings.h"

#include <functional>

namespace orc {

// `step(dLed, dLcd)`: move ORC's two Brightness.h steps by that much.
inline void registerBrightnessBuiltins(const std::function<void(int, int)>& step)
{
    const struct { const char* name; int led, lcd; const char* desc; } kLight[] = {
        { "brightness_leds_up",    1,  0, "Brightness LEDs +" },
        { "brightness_leds_down", -1,  0, "Brightness LEDs -" },
        { "brightness_lcds_up",    0,  1, "Brightness LCDs +" },
        { "brightness_lcds_down",  0, -1, "Brightness LCDs -" },
        { "brightness_both_up",    1,  1, "Brightness Both (LEDs+LCDs) +" },
        { "brightness_both_down", -1, -1, "Brightness Both (LEDs+LCDs) -" },
    };
    for (const auto& l : kLight) {
        const int dl = l.led, dc = l.lcd;
        uf8::bindings::registerBuiltin(l.name, uf8::bindings::BuiltinDescriptor{
            [step, dl, dc](bool firing, bool, int) { if (firing && step) step(dl, dc); },
            nullptr, l.desc, false });
    }
}

} // namespace orc
