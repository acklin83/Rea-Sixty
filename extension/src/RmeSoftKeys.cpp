#include "RmeSoftKeys.h"

#include "RmeManager.h"
#include "RmeNames.h"
#include "Uf1SoftKeys.h"

#include <algorithm>

namespace reasixty::rme::softkeys {

namespace bnd = uf8::bindings;
using DK = bnd::DynamicBankKind;

// ⇨ ONLY BANK ◄ ► PICK THE HALF (Frank 2026-09-28: "shift soll in beiden die
// hälfte gar nicht mehr umschalten, dann ist das konsistent"). Held SHIFT used
// to show the second half as well, from 25.09., when SHIFT and 5-8 were the
// only ways there. In the extension the computer keyboard's Shift did it too,
// and in ORC it did not, so the two programs answered differently. SHIFT stays
// a modifier (fine mode, the transport's Shift assignments).
int half(const input::State& s)
{
    return s.skHalf.load() != 0 ? 1 : 0;
}

bool hasSecondHalf(int bank)
{
    if (rmeKind(bank) != DK::None) return true;
    const auto cells = row(bank, 1);
    for (const auto& c : cells)
        if (!c.label.empty()) return true;
    return false;
}

void pickHalf(input::State& s, int bank, int half)
{
    s.skHalf.store((half != 0 && hasSecondHalf(bank)) ? 1 : 0);
}

bool stepKey(input::State& s, std::atomic<int>& bankIdx, int base, int count,
             const ::uf1::InputEvent& ev)
{
    const std::uint8_t id = ev.id;
    if (id == ::uf1::btn::kBankLeft || id == ::uf1::btn::kBankRight) {
        if (ev.pressed && !s.strip.load())
            pickHalf(s, base + bankIdx.load(), id == ::uf1::btn::kBankRight ? 1 : 0);
        return true;
    }
    if (id == ::uf1::btn::kArrowLeft || id == ::uf1::btn::kArrowRight) {
        if (ev.pressed) {
            const int nb  = std::max(1, count);
            const int dir = (id == ::uf1::btn::kArrowRight) ? 1 : -1;
            const int to  = std::clamp(bankIdx.load() + dir, 0, nb - 1);
            // A new bank starts on its first half.
            if (to != bankIdx.load()) s.skHalf.store(0);
            bankIdx.store(to);
        }
        return true;
    }
    return false;
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

KeyLamp dynKeyLamp(const bnd::Binding& slot, int half, int led)
{
    KeyLamp k;
    if (led <= 0 || half < 0 || half >= bnd::kModifierCount) return k;
    uint8_t c[3];
    bnd::Brightness bri;
    const auto& sp = slot.shortPress[half];
    if (led >= 2) bnd::effectiveLedActive  (slot, sp, c, bri);
    else          bnd::effectiveLedInactive(slot, sp, c, bri);
    if (bri == bnd::Brightness::Off) return k;
    k.rgb    = (std::uint32_t(c[0]) << 16) | (std::uint32_t(c[1]) << 8) | std::uint32_t(c[2]);
    k.bright = (bri == bnd::Brightness::Bright);
    return k;
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
        // Always through the colour path, because on the UF1 a key without a
        // colour knows only lit and dim, and "no link" has to be dark. The
        // colour is the key's own (dynKeyLamp), white when none is set.
        const DynSlot d = dynSlot(kind, half * 4 + i);
        const KeyLamp lamp = dynKeyLamp(bnd::getUf1SoftBankSlot(bank, i), half, d.led);
        c.label     = d.label;
        c.haveLabel = true;
        c.on        = (d.led >= 2);
        c.hasColour = true;
        c.colBright = lamp.bright;
        c.colRgb    = lamp.rgb;
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
