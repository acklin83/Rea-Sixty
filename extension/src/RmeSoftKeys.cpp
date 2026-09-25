#include "RmeSoftKeys.h"

#include "RmeManager.h"
#include "RmeNames.h"
#include "Uf1SoftKeys.h"

namespace reasixty::rme::softkeys {

namespace bnd = uf8::bindings;
using DK = bnd::DynamicBankKind;

int half(const input::State& s)
{
    if (s.skHalf.load() != 0) return 1;
    return bnd::bankModifierSnapshot() == bnd::Modifier::Shift ? 1 : 0;
}

DK rmeKind(int bank)
{
    // The bank's kind is Plain's: a TotalMix bank is one list of eight, split
    // over the two halves, not two lists.
    const DK k = bnd::getUf1SoftBankDynamic(bank, static_cast<int>(bnd::Modifier::Plain));
    return (k == DK::RmeSnapshots || k == DK::RmeLayouts) ? k : DK::None;
}

// Moved from the extension's dynamicBankSlot_ (main.cpp), 25.09.2026, word for
// word apart from the struct it fills.
// ⛔ TotalMix always has eight of each, so every key is present; the name comes
// from its state file (RmeNames.h). Without a link the names stay and the lamp
// goes dark, and the press does nothing (Frank 22.09.).
DynSlot dynSlot(DK kind, int slot)
{
    DynSlot d;
    if (slot < 0 || slot >= 8) return d;
    const auto& nm = namesFromDisk();
    const bool snap = (kind == DK::RmeSnapshots);
    d.label = snap ? snapshotName(nm, slot) : layoutName(nm, slot);
    auto& rm = manager();
    if (rm.link() != LinkState::Online) { d.led = 0; return d; }
    const auto sc = rm.scenes();
    if (snap) {
        // Aktiv und geaendert leuchten gleich und ruhig, wie ein Layout
        // (Frank 22.09.: "den aktiven nicht blinken lassen").
        d.led = (sc.snapshot[slot] == SnapshotState::Active
                 || sc.snapshot[slot] == SnapshotState::Changed) ? 2 : 1;
    } else {
        d.led = (sc.lastLayout == slot) ? 2 : 1;
    }
    return d;
}

// Moved from the extension's applyDynBankRmeOp_ (main.cpp), 25.09.2026. Saving
// a snapshot is not on these keys. Without a link nothing is sent
// (Manager::send drops it anyway when RME is switched off, but a link that is
// merely down would queue it).
bool loadDyn(DK kind, int slot)
{
    if (slot < 0 || slot >= 8) return false;
    if (kind != DK::RmeSnapshots && kind != DK::RmeLayouts) return false;
    auto& rm = manager();
    if (rm.link() != LinkState::Online) return false;
    // Snapshots count from 1 on the wire (protocol sheet); layouts are sent the
    // same way, which is not measured yet.
    const bool snap = (kind == DK::RmeSnapshots);
    rm.send((snap ? "/snapshot/load/" : "/layout/load/") + std::to_string(slot + 1), 1.0f);
    return true;
}

std::array<uf1spread::SkCell, 4> row(int bank, int half)
{
    std::array<uf1spread::SkCell, 4> cells{};
    const DK kind = rmeKind(bank);
    for (int i = 0; i < 4; ++i) {
        auto& c = cells[static_cast<std::size_t>(i)];
        if (kind == DK::None) {
            c = uf1sk::staticBankCell(bank, i, half);
            continue;
        }
        // The extension's uf1DynBankCell_ for a slot with a colour: white
        // through the colour path, because on the UF1 a key without a colour
        // knows only lit and dim, and "no link" has to be dark.
        const DynSlot d = dynSlot(kind, half * 4 + i);
        c.label     = d.label;
        c.haveLabel = true;
        c.on        = (d.led >= 2);
        c.hasColour = true;
        c.colBright = (d.led >= 2);
        c.colRgb    = (d.led == 0) ? 0u : 0xFFFFFFu;
    }
    return cells;
}

void press(int bank, int half, int slot, bool pressed, const DynLoad& load)
{
    if (slot < 0 || slot >= 4) return;
    const DK kind = rmeKind(bank);
    if (kind == DK::None) {
        bnd::dispatchUf1SoftBankSlot(bank, slot, pressed, half);
        return;
    }
    if (!pressed) return;
    if (load) load(kind, half * 4 + slot);
    else      loadDyn(kind, half * 4 + slot);
}

} // namespace reasixty::rme::softkeys
