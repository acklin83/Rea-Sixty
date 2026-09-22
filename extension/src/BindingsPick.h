#pragma once
//
// BindingsPick — while Settings → Bindings is open, a press on a surface picks
// that button in the pane instead of running its binding (Frank 22.09.2026: "so
// dass der Druck nicht die gebundene Aktion ausfuehrt, sondern nur diesen Button
// auf dem Tab der Surface auswaehlt"). This file is only the DECISION, pure, so
// tests/test_bindings_pick.cpp can pin it. The three surfaces ask it at their
// earliest press point (onUf8Input, UC1Surface::handleButton_, onUf1Event).
//
#include "Bindings.h"

#include <string_view>

namespace uf8::bindings {

enum class PickAction : std::uint8_t {
    PassThrough,   // not a pick: the press does what it always does
    Select,        // pick it, swallow the press AND its release
    SelectAndRun,  // pick it, and let the press run as well
};

// `plainBuiltin` is the Plain short-press builtin bound to the key ("" when
// none or not a builtin), so a key the user made a modifier stays one.
inline PickAction pickActionFor(ButtonId id, std::string_view plainBuiltin)
{
    if (id == ButtonId::None) return PickAction::PassThrough;
    // ⇨ MODIFIERS STAY MODIFIERS. SHIFT/FINE are bindings that go through
    // dispatch (mod_shift), and the pane already follows a held SHIFT onto the
    // soft-keys' Shift set (trackBankModifierEdge_). Swallowing them would take
    // that away. Select them with the mouse.
    if (plainBuiltin == "mod_shift" || plainBuiltin == "mod_cmd"
        || plainBuiltin == "mod_ctrl" || plainBuiltin == "fine_modifier")
        return PickAction::PassThrough;
    // ⛔ THE KEY THAT OPENS AND CLOSES SETTINGS STAYS THAT KEY, or there is no
    // way out of the window from the surface while the pane is open (Frank
    // 22.09.). Wherever it is bound (UC1 360 by factory).
    if (plainBuiltin == "mixer_toggle")
        return PickAction::PassThrough;
    switch (id) {
        case ButtonId::Fine:
        case ButtonId::Uf1Shift:
        // Drawn locked in the pane: nothing to pick, so keep them working.
        case ButtonId::Uf1Solo:
        case ButtonId::Uf1Cut:
        case ButtonId::Uf1Scrub:
            return PickAction::PassThrough;
        // ⇨ THE KEYS THAT CHOOSE WHAT THE PANE EDITS. Clicking these tiles has
        // always run them too (drawUf8Vector: "hardware proxy"), because the
        // layer, Quick and bank ARE the pane's context; a hardware press does
        // the same, or the bank could no longer be changed at the surface.
        // UF1 < > page its soft-key banks in the DAW view.
        case ButtonId::Layer1: case ButtonId::Layer2: case ButtonId::Layer3:
        case ButtonId::Quick1: case ButtonId::Quick2: case ButtonId::Quick3:
        case ButtonId::VPotBank:
        case ButtonId::SoftKey1Bank: case ButtonId::SoftKey2Bank:
        case ButtonId::SoftKey3Bank: case ButtonId::SoftKey4Bank:
        case ButtonId::SoftKey5Bank:
        case ButtonId::Uf1ArrowLeft: case ButtonId::Uf1ArrowRight:
            return PickAction::SelectAndRun;
        default:
            return PickAction::Select;
    }
}

}  // namespace uf8::bindings
