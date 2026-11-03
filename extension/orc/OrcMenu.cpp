#include "OrcMenu.h"

#include "OrcConfig.h"
#include "PhonesServer.h"
#include "Surface.h"

#if defined(_WIN32)
#include "WinUsbDriver.h"
#endif

namespace orc {

MenuLines menuLines(const Surface* surface)
{
    MenuLines m;
    m.uf1 = "UF1";
    m.uf8 = "UF8";
    if (surface) {
        const Status st = surface->status();
        // ⛔ The reason is repeated verbatim. libusb cannot tell an absent UF1
        // from one another program is holding, so ORC does not name a culprit
        // it cannot prove.
        m.uf1 = st.off ? std::string("UF1   off")
              : st.open ? "UF1   held by ORC, " + st.serial
                        : "UF1   not open: " + st.error;
        // The UF8, the same way. ⇨ A REAPER extension too old for the receipt
        // (a marker without "v2", OrcHandover.h) may never pick the UF8 up: it
        // tried once at its start, while ORC still held it. Say so rather than
        // leave the unit dark without a word (docs/orc-uf8-plan.md, section 5).
        const Uf8Status u8 = surface->uf8Status();
        if (u8.off)
            m.uf8 = "UF8   off";
        else if (u8.open)
            m.uf8 = "UF8   held by ORC, " + u8.serial;
        else if (reaperWantsUf1() && reaperMarkerVersion() == 1)
            m.uf8 = "UF8   REAPER cannot take it yet: update the REAPER extension";
        else
            m.uf8 = "UF8   not open: " + u8.error;
    }
#if defined(_WIN32)
    // ⇨ PLUGGED IN, BUT SSL'S DRIVER HAS IT: libusb cannot open it, and "not open"
    // would leave the user without a clue. The menu is the place that asks; ⛔ never
    // the USB thread (WinUsbDriver.h).
    if (surface) {
        const Status st = surface->status();
        const Uf8Status u8 = surface->uf8Status();
        if ((!st.off && !st.open) || (!u8.off && !u8.open)) {
            const auto now = winusb::status({ { "VID_31E9&PID_0025", "UF1" }, { "VID_31E9&PID_0021", "UF8" } });
            if (!st.off && !st.open && now[0].connected && !now[0].ready) m.uf1 = "UF1   needs the USB driver";
            if (!u8.off && !u8.open && now[1].connected && !now[1].ready) m.uf8 = "UF8   needs the USB driver";
        }
    }
#endif
    m.mixer = "TotalMix   " + linkSummary();

    namespace ph = phones;
    const ph::Status mm = ph::server().status();
    m.moreme    = "MoreMe   " + ph::summary(mm);
    m.deskShown = mm.enabled && mm.listening;
    if (m.deskShown) {
        m.address = ph::pageAddress(mm.port);
        if (!m.address.empty()) m.addressLine = m.address + "   PIN " + ph::server().pin();
    }
    return m;
}

} // namespace orc
