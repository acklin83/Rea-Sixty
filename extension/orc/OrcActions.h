#pragma once
//
// ⇨ ONE ANSWER FOR "WHICH ACTIONS ARE ORC's" (docs/orc-media-keys-plan.md). Before
// 15.11.2026 the settings page (rmeActions) and the manual's dump each spelled
// the category list out, and the UF1's transport lamp asked bnd::orcKeySlot,
// which only knows rme_: a transport key on Sleep fired and stayed dark.
//
// ⛔ bnd::orcKeySlot is NOT this. It answers Rea-Sixty's question, whether its
// side-car leaves a transport key to ORC, and stays at rme_, so the media keys
// are REAPER's again while REAPER has the surface.

#include "Bindings.h"

#include <string>

namespace orc {

// What a key can pick in ORC's window: the categories of the builtins ORC
// registers itself, besides the TotalMix ones (RmeBuiltins).
inline bool offersAction(const std::string& builtin)
{
    const std::string cat = uf8::bindings::builtinCategory(builtin);
    return cat == "RME" || cat == "Brightness" || cat == "Media";
}

// What ORC runs on a key: a builtin it has registered. Anything else on a key is
// the factory layer orc.json inherited from Rea-Sixty and does nothing here.
inline const uf8::bindings::ActionSlot* runsSlot(const uf8::bindings::Binding& bd, int mod)
{
    namespace bnd = uf8::bindings;
    if (mod < 0 || mod >= bnd::kModifierCount) return nullptr;
    const bnd::ActionSlot& sl = bd.shortPress[mod];
    if (sl.type != bnd::ActionType::Builtin || !bnd::builtinRegistered(sl.action)) return nullptr;
    return &sl;
}

} // namespace orc
