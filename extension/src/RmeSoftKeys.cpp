#include "RmeSoftKeys.h"

#include "RmeManager.h"
#include "ObsManager.h"
#include "RmeNames.h"
#include "RoomCells.h"
#include "Uf1SoftKeys.h"

#include <algorithm>

namespace reasixty::rme::softkeys {

namespace bnd = uf8::bindings;
using DK = bnd::DynamicBankKind;
namespace rmeu = reasixty::rme::uf1;

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
    return (k == DK::RmeSnapshots || k == DK::RmeLayouts || k == DK::ObsScenes
            || k == DK::RmeSubmixes) ? k : DK::None;
}

// Moved from the extension's dynamicBankSlot_ (main.cpp), 25.09.2026, word for
// word apart from the struct it fills.
// ⛔ TotalMix always has eight snapshots and six layouts (kLayoutSlots), so those
// keys are present and layout keys 7 and 8 are empty; the name comes from its
// state file (RmeNames.h). Without a link the names stay and the lamp goes dark,
// and the press does nothing (Frank 22.09.).
DynSlot dynSlot(DK kind, int slot)
{
    DynSlot d;
    if (slot < 0 || slot >= 8) return d;
    // ⇨ OBS SCENES ON A TOTALMIX BANK (Frank 29.10.2026: OBS is no mode of its
    // own, its scenes are a bank kind). The first eight OBS reports, in its
    // order; the one on air lit, a key past the last dark.
    if (kind == DK::ObsScenes) {
        const roomcells::Cell c = roomcells::obsScene(slot);
        d.label = c.label;
        d.led   = c.led;
        return d;
    }
    if (kind == DK::RmeLayouts && slot >= kLayoutSlots) return d;
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
    if (kind == DK::ObsScenes) {
        auto& om = reasixty::obs::manager();
        const std::string nm = om.sceneAt(slot);
        if (nm.empty()) return false;
        om.switchScene(nm);
        return true;
    }
    if (kind != DK::RmeSnapshots && kind != DK::RmeLayouts) return false;
    if (kind == DK::RmeLayouts && slot >= kLayoutSlots) return false;
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

std::string submixSpec(int bank, int item)
{
    if (item < 0 || item >= 8) return {};
    // Only our own: another action's text is its own business (an OBS scene's name).
    const auto& sp = bnd::getUf1SoftBankSlot(bank, item % 4).shortPress[item / 4];
    return sp.action == kSubmixSlotAction ? sp.textParam : std::string();
}

void setSubmixSpec(int bank, int item, const std::string& spec)
{
    if (item < 0 || item >= 8) return;
    bnd::Binding bd = bnd::getUf1SoftBankSlot(bank, item % 4);
    auto& sp = bd.shortPress[item / 4];
    sp.type      = spec.empty() ? bnd::ActionType::Noop : bnd::ActionType::Builtin;
    sp.action    = spec.empty() ? std::string() : std::string(kSubmixSlotAction);
    sp.textParam = spec;
    bnd::setUf1SoftBankSlot(bank, item % 4, bd);
}

// A submix bank's four cells. The name is the user's label, else TotalMix' own;
// a key without a visible output is empty and dark, as a key past OBS' last scene.
static std::array<uf1spread::SkCell, 4> submixRow(int bank, int half, const input::State* in)
{
    std::array<uf1spread::SkCell, 4> cells{};
    auto& rm = manager();
    const State st = rm.snapshot();
    const bool online = rm.link() == LinkState::Online;
    const int  sub = in ? rmeu::effectiveSubmix(st, in->submix.load()) : -1;
    for (int i = 0; i < 4; ++i) {
        auto& c = cells[static_cast<std::size_t>(i)];
        const bnd::Binding bd = bnd::getUf1SoftBankSlot(bank, i);
        const auto& sp = bd.shortPress[half];
        c.haveLabel = true;
        c.hasColour = true;
        const int out = rmeu::keyOutput(st, submixSpec(bank, half * 4 + i));
        if (out < 0) continue;
        const std::string& own = (half == 0 && sp.label.empty()) ? bd.label : sp.label;
        c.label = own.empty() ? rmeu::displayName(st, rmeu::Row::Output, out) : own;
        const int led = !online ? 0 : out == sub ? 2 : 1;
        const KeyLamp lamp = dynKeyLamp(bd, half, led);
        c.on        = led >= 2;
        c.colBright = lamp.bright;
        c.colRgb    = lamp.rgb;
        // Cue is yellow whatever colours the key carries: on stage it has to be
        // the one thing that means the same on every key (Frank 03.11.2026).
        if (led > 0 && st.cueChannel == out) {
            c.colRgb    = 0xFFFF00u;
            c.colBright = led >= 2;
        }
    }
    return cells;
}

std::array<uf1spread::SkCell, 4> row(int bank, int half, const input::State* in)
{
    const DK kind = rmeKind(bank);
    if (kind == DK::RmeSubmixes) return submixRow(bank, half, in);
    std::array<uf1spread::SkCell, 4> cells{};
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

// ⇨ THE UF8's EIGHT SOFT KEYS IN THE TOTALMIX SIDE-CAR (Frank 29.10.2026: the
// soft keys belong to the mode): bank `bankAbs`, both halves at once, keys 1-4
// the first, 5-8 the second. The same on the UF8 in ORC and in Rea-Sixty.
std::array<uf8side::SoftCell, 8> uf8Row(int bankAbs, const input::State* in)
{
    std::array<uf8side::SoftCell, 8> soft{};
    for (int h = 0; h < 2; ++h) {
        if (h == 1 && !hasSecondHalf(bankAbs)) break;
        const auto cells = row(bankAbs, h, in);
        for (int k = 0; k < 4; ++k) {
            const auto& c = cells[static_cast<size_t>(k)];
            uf8side::SoftCell& o = soft[static_cast<size_t>(h * 4 + k)];
            if (!c.haveLabel || c.label.empty()) continue;
            o.label = c.label;
            // A colour of 0 is "no link" (row): dark.
            o.lamp  = (c.hasColour && !c.colRgb) ? 0 : (c.on || c.colBright) ? 2 : 1;
            o.rgb   = c.hasColour ? c.colRgb : 0xFFFFFFu;
        }
    }
    return soft;
}

void press(int bank, int half, int slot, bool pressed, const DynLoad& load,
           input::State* in, bool shift)
{
    if (slot < 0 || slot >= 4) return;
    const DK kind = rmeKind(bank);
    if (kind == DK::RmeSubmixes) {
        if (!pressed || !in) return;
        auto& rm = manager();
        const State st = rm.snapshot();
        const int out = rmeu::keyOutput(st, submixSpec(bank, half * 4 + slot));
        if (out < 0) return;
        if (shift) {
            const auto m = rmeu::cueToggle(st, out);
            if (!m.first.empty()) rm.send(m.first, m.second);
        } else {
            input::setSubmix(*in, st, rm.config(), out);
        }
        return;
    }
    if (kind == DK::None) {
        bnd::dispatchUf1SoftBankSlot(bank, slot, pressed, half);
        return;
    }
    if (!pressed) return;
    if (load) load(kind, half * 4 + slot);
    else      loadDyn(kind, half * 4 + slot);
}

} // namespace reasixty::rme::softkeys
