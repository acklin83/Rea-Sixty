#pragma once
//
// ⇨ WHAT A SOFT KEY SHOWS FOR THE ROOM: a Hue scene, a DynaMount preset, an OBS
// scene. One answer for every surface and both programs: Rea-Sixty's dynamic
// banks (main.cpp dynamicBankSlot_), the UF8 side-cars in Rea-Sixty and ORC
// (docs/orc-uf8-plan.md 2.3, Frank 29.10.2026: the soft keys belong to the
// mode), and the TotalMix banks' OBS kind (RmeSoftKeys). They had grown three
// copies of the same three loops.
//
// Three functions rather than one switch, so a file that needs one of them
// links only that manager.

#include "DynaMountManager.h"
#include "HueManager.h"
#include "ObsManager.h"

#include <cstdint>
#include <string>

namespace roomcells {

struct Cell {
    bool          present = false;   // false: the key is dark
    std::string   label;
    int           led     = 0;       // 0 off, 1 dim, 2 on
    bool          hasRgb  = false;   // a colour of its own (Hue: the slot's)
    std::uint32_t rgb     = 0xFFFFFFu;
};

// The scene slots from the Hue settings. Lit while the bridge reports it showing.
inline Cell hueScene(int slot)
{
    Cell c;
    auto& hm = uf8::hue::manager();
    if (!hm.sceneSlotFilled(slot)) return c;
    c.present = true;
    c.label   = hm.sceneSlotName(slot);
    if (c.label.empty()) c.label = "Scene";
    c.led     = hm.sceneSlotActive(slot) ? 2 : 1;
    c.hasRgb  = true;
    c.rgb     = hm.sceneSlot(slot).rgb;
    return c;
}

// A preset is the room's stands. Empty slots stay dark but take a long press
// (store); the last one recalled is lit.
inline Cell dynaPreset(int slot)
{
    Cell c;
    auto& dm = uf8::dynamount::manager();
    const auto p = dm.preset(slot);
    if (!p.defined()) return c;
    c.present = true;
    c.label   = p.name.empty() ? "Preset " + std::to_string(slot + 1) : p.name;
    c.led     = (dm.lastPreset() == slot) ? 2 : 1;
    return c;
}

// The scenes OBS reports, in its order; the one on air lit, a key past the
// last dark.
inline Cell obsScene(int slot)
{
    Cell c;
    auto& om = reasixty::obs::manager();
    const std::string nm = om.sceneAt(slot);
    if (nm.empty()) return c;
    c.present = true;
    c.label   = nm;
    c.led     = (nm == om.currentScene()) ? 2 : 1;
    return c;
}

} // namespace roomcells
