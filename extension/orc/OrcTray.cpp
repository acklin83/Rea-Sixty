#include "OrcUi.h"

#include "Surface.h"

#include <chrono>
#include <thread>

namespace orc {

// Stage 2 of docs/orc-windows-plan.md: no face yet. ORC runs in a console until
// Ctrl-C, and the log says what it does.
void runApp(Surface&, const std::function<bool()>& interrupted)
{
    while (!(interrupted && interrupted()))
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
}

} // namespace orc
