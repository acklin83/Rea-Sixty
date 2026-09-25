#include "Uf1Pacer.h"

#include <thread>

namespace uf1pace {

void emitCycle(uf1::UF1Device& dev, const Parts& p,
               std::chrono::steady_clock::time_point slot)
{
    // The three groups plus the silences between them are ONE unit to the
    // firmware. Hold everything else out of it; the painter's own sends are
    // released as a block once the trailer has closed it.
    dev.beginCycle();
    dev.sendBurst(std::vector<std::vector<std::uint8_t>>(p.img));
    std::this_thread::sleep_until(slot + kMetersOff);
    if (!p.meters.empty())
        dev.sendBurst(std::vector<std::vector<std::uint8_t>>(p.meters));
    std::this_thread::sleep_until(slot + kTailOff);
    if (!p.tail.empty())
        dev.sendBurst(std::vector<std::vector<std::uint8_t>>(p.tail));
    dev.endCycle();
}

} // namespace uf1pace
