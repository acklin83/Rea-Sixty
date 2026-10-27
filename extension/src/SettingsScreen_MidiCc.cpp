// SettingsScreen_MidiCc.cpp: the MIDI CC layer in Settings > Bindings > UF8
// (docs/midi-cc-layer-plan.md, Baustein I, as in Frank's approved mockup of
// 27.10.2026): the layer type, the layer block (outputs, input, follow, default
// channel, presets), the five pages, the grid of controls and the editor of the
// one selected.
//
// ⛔ ONLY CALL SHAPES SettingsScreen.cpp ALREADY USES. ReaImGui tears the window
// down on a call shape it objects to, with no crash report, even when the
// signature is fine ([[reaimgui-new-call-shape-teardown]]).

#include "SettingsScreen.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "Bindings.h"
#include "MidiCcLayer.h"
#include "reaper_imgui_functions.h"
#include "reaper_plugin_functions.h"

namespace {

namespace bnd = uf8::bindings;
using midicc::Kind;

int  s_kind  = static_cast<int>(Kind::Fader);
int  s_index = 0;
int  s_presetSel = -1;
char s_presetName[64] = {0};

inline void tip_(ImGui_Context* ctx, const char* text)
{
    if (ImGui_IsItemHovered(ctx, nullptr)) ImGui_SetTooltip(ctx, text);
}

const char* kindName_(Kind k)
{
    switch (k) {
        case Kind::Fader:      return "Fader";
        case Kind::VPot:       return "V-Pot";
        case Kind::Push:       return "V-Pot push";
        case Kind::Soft:       return "Soft-key";
        case Kind::SendPlugin: return "Send/plug-in key";
        case Kind::Foot:       return "Foot switch";
    }
    return "";
}

int rgba_(uint32_t rgb, int alpha) { return static_cast<int>((rgb << 8) | uint32_t(alpha)); }

std::vector<std::string> outputNames_()
{
    std::vector<std::string> v;
    const int n = GetNumMIDIOutputs();
    for (int i = 0; i < n; ++i) {
        char nm[256] = {0};
        if (GetMIDIOutputName(i, nm, sizeof(nm)) && *nm) v.push_back(nm);
    }
    return v;
}

std::vector<std::string> inputNames_()
{
    std::vector<std::string> v;
    const int n = GetNumMIDIInputs();
    for (int i = 0; i < n; ++i) {
        char nm[256] = {0};
        // REAPER's own keyboard is not a device the layer can open (probe 27.10.).
        if (GetMIDIInputName(i, nm, sizeof(nm)) && *nm
            && std::string(nm) != "Virtual MIDI Keyboard")
            v.push_back(nm);
    }
    return v;
}

bool hasOutput_(const midicc::Layer& L, const std::string& name)
{
    return std::find(L.outputs.begin(), L.outputs.end(), name) != L.outputs.end();
}

void setOutput_(midicc::Layer& L, const std::string& name, bool on)
{
    auto it = std::find(L.outputs.begin(), L.outputs.end(), name);
    if (on && it == L.outputs.end()) L.outputs.push_back(name);
    if (!on && it != L.outputs.end()) L.outputs.erase(it);
}

// The layer block: where the MIDI goes, where it comes back from, presets.
bool drawLayerBlock_(ImGui_Context* ctx, int layer, midicc::Layer& L)
{
    bool dirty = false;

    ImGui_Text(ctx, "Output");
    ImGui_SameLine(ctx, nullptr, nullptr);
    {
        bool v = hasOutput_(L, "");
        if (ImGui_Checkbox(ctx, "REAPER (virtual MIDI keyboard)##cc_out_vkb", &v)) {
            setOutput_(L, "", v);
            dirty = true;
        }
        tip_(ctx, "Tracks whose input is the virtual MIDI keyboard receive and record it.");
    }
    const std::vector<std::string> outs = outputNames_();
    for (size_t i = 0; i < outs.size(); ++i) {
        ImGui_SameLine(ctx, nullptr, nullptr);
        char id[300];
        std::snprintf(id, sizeof(id), "%s##cc_out_%d", outs[i].c_str(), int(i));
        bool v = hasOutput_(L, outs[i]);
        if (ImGui_Checkbox(ctx, id, &v)) { setOutput_(L, outs[i], v); dirty = true; }
    }
    // A chosen device that is not plugged in stays chosen, and says so.
    for (size_t i = 0; i < L.outputs.size(); ++i) {
        const std::string& o = L.outputs[i];
        if (o.empty() || std::find(outs.begin(), outs.end(), o) != outs.end()) continue;
        ImGui_SameLine(ctx, nullptr, nullptr);
        char id[320];
        std::snprintf(id, sizeof(id), "%s (offline)##cc_out_off_%d", o.c_str(), int(i));
        bool v = true;
        if (ImGui_Checkbox(ctx, id, &v) && !v) { setOutput_(L, o, false); dirty = true; break; }
    }

    ImGui_Text(ctx, "Input");
    ImGui_SameLine(ctx, nullptr, nullptr);
    ImGui_SetNextItemWidth(ctx, 260.0);
    {
        const std::string cur = L.inputDevice.empty() ? std::string("(none)") : L.inputDevice;
        if (ImGui_BeginCombo(ctx, "##cc_input", cur.c_str(), nullptr)) {
            bool sel = L.inputDevice.empty();
            if (ImGui_Selectable(ctx, "(none)##cc_in_none", &sel, nullptr, nullptr, nullptr)) {
                L.inputDevice.clear();
                dirty = true;
            }
            const std::vector<std::string> ins = inputNames_();
            for (size_t i = 0; i < ins.size(); ++i) {
                char id[300];
                std::snprintf(id, sizeof(id), "%s##cc_in_%d", ins[i].c_str(), int(i));
                bool s = (L.inputDevice == ins[i]);
                if (ImGui_Selectable(ctx, id, &s, nullptr, nullptr, nullptr)) {
                    L.inputDevice = ins[i];
                    dirty = true;
                }
            }
            ImGui_EndCombo(ctx);
        }
        tip_(ctx, "Where the hardware reports its controls back. The faders follow it.");
    }

    {
        bool v = L.followItems;
        if (ImGui_Checkbox(ctx, "Follow CC in REAPER##cc_follow", &v)) {
            L.followItems = v;
            dirty = true;
        }
        tip_(ctx, "Faders follow the CC lanes of the selected track while REAPER plays.");
    }

    ImGui_Text(ctx, "Default channel");
    ImGui_SameLine(ctx, nullptr, nullptr);
    ImGui_SetNextItemWidth(ctx, 110.0);
    {
        int ch = L.defaultChannel;
        if (ImGui_InputInt(ctx, "##cc_defch", &ch, nullptr, nullptr, nullptr)) {
            L.defaultChannel = std::clamp(ch, 1, 16);
            dirty = true;
        }
    }

    // Presets.
    const std::vector<std::string> names = bnd::ccPresetNames();
    if (s_presetSel >= static_cast<int>(names.size())) s_presetSel = -1;
    ImGui_Text(ctx, "CC preset");
    ImGui_SameLine(ctx, nullptr, nullptr);
    ImGui_SetNextItemWidth(ctx, 200.0);
    if (ImGui_BeginCombo(ctx, "##cc_preset",
                         s_presetSel >= 0 ? names[s_presetSel].c_str() : "(choose)", nullptr)) {
        for (size_t i = 0; i < names.size(); ++i) {
            char id[96];
            std::snprintf(id, sizeof(id), "%s##cc_pre_%d", names[i].c_str(), int(i));
            bool s = (s_presetSel == int(i));
            if (ImGui_Selectable(ctx, id, &s, nullptr, nullptr, nullptr)) s_presetSel = int(i);
        }
        ImGui_EndCombo(ctx);
    }
    if (s_presetSel >= 0) {
        ImGui_SameLine(ctx, nullptr, nullptr);
        if (ImGui_Button(ctx, "Load##cc_pre_load", nullptr, nullptr))
            bnd::loadCcPreset(names[s_presetSel], layer);
        tip_(ctx, "Replaces this layer's CC set with the preset.");
        ImGui_SameLine(ctx, nullptr, nullptr);
        if (ImGui_Button(ctx, "Save##cc_pre_save", nullptr, nullptr))
            bnd::saveCcPreset(names[s_presetSel], layer);
        tip_(ctx, "Overwrites the preset with this layer's CC set.");
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (ImGui_Button(ctx, "Save as##cc_pre_saveas", nullptr, nullptr)) {
        s_presetName[0] = 0;
        ImGui_OpenPopup(ctx, "Save CC preset###cc_pre_modal", nullptr);
    }
    if (s_presetSel >= 0) {
        ImGui_SameLine(ctx, nullptr, nullptr);
        if (ImGui_Button(ctx, "Delete##cc_pre_del", nullptr, nullptr)) {
            bnd::deleteCcPreset(names[s_presetSel]);
            s_presetSel = -1;
        }
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (ImGui_Button(ctx, "Default##cc_pre_default", nullptr, nullptr)) {
        midicc::Layer f = midicc::factoryLayer();
        f.outputs = L.outputs;             // where the MIDI goes is not a CC set
        f.inputDevice = L.inputDevice;
        L = f;
        dirty = true;
    }
    tip_(ctx, "Back to the factory CC set. Output and input stay.");

    if (ImGui_BeginPopupModal(ctx, "Save CC preset###cc_pre_modal", nullptr, nullptr)) {
        ImGui_PushItemWidth(ctx, 280);
        ImGui_InputTextWithHint(ctx, "Name##cc_pre_name", "e.g. 'Strings'",
                                s_presetName, static_cast<int>(sizeof(s_presetName)),
                                nullptr, nullptr);
        ImGui_PopItemWidth(ctx);
        std::string nm = s_presetName;
        while (!nm.empty() && nm.back() == ' ') nm.pop_back();
        while (!nm.empty() && nm.front() == ' ') nm.erase(nm.begin());
        if (!nm.empty()) {
            if (ImGui_Button(ctx, "Save##cc_pre_modal_ok", nullptr, nullptr)) {
                if (dirty) bnd::setCcLayer(layer, L);   // save what is on screen
                dirty = false;
                bnd::saveCcPreset(nm, layer);
                ImGui_CloseCurrentPopup(ctx);
            }
        } else {
            ImGui_TextDisabled(ctx, "Save");
        }
        ImGui_SameLine(ctx, nullptr, nullptr);
        if (ImGui_Button(ctx, "Cancel##cc_pre_modal_cancel", nullptr, nullptr))
            ImGui_CloseCurrentPopup(ctx);
        ImGui_EndPopup(ctx);
    }
    return dirty;
}

// One grid cell: the control's name on its colour, outlined when selected.
void drawCell_(ImGui_Context* ctx, const midicc::Control& c, Kind k, int i, int page)
{
    const bool selected = (s_kind == static_cast<int>(k) && s_index == i);
    const bool on = midicc::assigned(c);
    std::string label = midicc::displayName(k, c);
    if (label.empty()) label = "-";
    char id[64];
    std::snprintf(id, sizeof(id), "%s##cc_cell_%d_%d_%d", label.c_str(), page, int(k), i);
    int pushed = 0;
    if (selected) {
        ImGui_PushStyleColor(ctx, ImGui_Col_Button, 0x4477CCFF);
        ++pushed;
    } else if (on) {
        ImGui_PushStyleColor(ctx, ImGui_Col_Button, rgba_(midicc::colourRgb(c), 0x55));
        ++pushed;
    }
    double bw = 74.0, bh = 22.0;
    if (ImGui_Button(ctx, id, &bw, &bh)) {
        s_kind = static_cast<int>(k);
        s_index = i;
    }
    if (pushed) ImGui_PopStyleColor(ctx, &pushed);
}

// The editor of the selected control.
bool drawControlEditor_(ImGui_Context* ctx, midicc::Layer& L, int page)
{
    bool dirty = false;
    const Kind k = static_cast<Kind>(s_kind);
    if (s_index >= midicc::countOf(k)) s_index = 0;
    midicc::Control& c = L.pages[page].at(k, s_index);

    char hdr[96];
    std::snprintf(hdr, sizeof(hdr), "%s %d, page %d", kindName_(k), s_index + 1, page + 1);
    ImGui_Text(ctx, hdr);

    ImGui_Text(ctx, "Channel");
    ImGui_SameLine(ctx, nullptr, nullptr);
    ImGui_SetNextItemWidth(ctx, 110.0);
    {
        int ch = c.channel;
        if (ImGui_InputInt(ctx, "##cc_ed_ch", &ch, nullptr, nullptr, nullptr)) {
            c.channel = std::clamp(ch, 0, 16);
            dirty = true;
        }
        tip_(ctx, "0 = the layer's default channel.");
    }

    ImGui_Text(ctx, k == Kind::SendPlugin ? "Program" : "CC number");
    ImGui_SameLine(ctx, nullptr, nullptr);
    ImGui_SetNextItemWidth(ctx, 110.0);
    {
        int n = c.number;
        if (ImGui_InputInt(ctx, "##cc_ed_num", &n, nullptr, nullptr, nullptr)) {
            c.number = std::clamp(n, -1, 127);
            if (c.number > 31) c.hiRes = false;
            dirty = true;
        }
        tip_(ctx, "0 to 127. -1 = this control does nothing.");
    }

    if (k == Kind::Fader) {
        if (c.number >= 0 && c.number <= 31) {
            bool hr = c.hiRes;
            char lbl[80];
            std::snprintf(lbl, sizeof(lbl), "14 bit (CC %d and CC %d)##cc_ed_hr",
                          c.number, c.number + 32);
            if (ImGui_Checkbox(ctx, lbl, &hr)) { c.hiRes = hr; dirty = true; }
            tip_(ctx, "16384 steps instead of 128. The receiver has to read the pair.");
        } else {
            ImGui_TextDisabled(ctx, "14 bit: only CC 0 to 31");
        }
    }

    if (midicc::isKey(k)) {
        ImGui_Text(ctx, "Behaviour");
        ImGui_SameLine(ctx, nullptr, nullptr);
        ImGui_SetNextItemWidth(ctx, 140.0);
        const bool hold = c.behaviour == midicc::Behaviour::Hold;
        if (ImGui_BeginCombo(ctx, "##cc_ed_beh", hold ? "Hold" : "Toggle", nullptr)) {
            bool sT = !hold, sH = hold;
            if (ImGui_Selectable(ctx, "Toggle##cc_beh_t", &sT, nullptr, nullptr, nullptr)) {
                c.behaviour = midicc::Behaviour::Toggle;
                dirty = true;
            }
            if (ImGui_Selectable(ctx, "Hold##cc_beh_h", &sH, nullptr, nullptr, nullptr)) {
                c.behaviour = midicc::Behaviour::Hold;
                dirty = true;
            }
            ImGui_EndCombo(ctx);
        }
        tip_(ctx, hold ? "127 while held, 0 on release." : "0 and 127 in turn.");
    }

    ImGui_Text(ctx, "Name");
    ImGui_SameLine(ctx, nullptr, nullptr);
    {
        char buf[16];
        std::snprintf(buf, 9, "%s", c.name.c_str());   // the field holds 8
        int flags = 0;
        ImGui_PushItemWidth(ctx, 120);
        if (ImGui_InputText(ctx, "##cc_ed_name", buf, 9, &flags, nullptr)) {
            c.name = buf;
            dirty = true;
        }
        ImGui_PopItemWidth(ctx);
        tip_(ctx, "8 characters, as the UF8 shows them. Empty = the CC number.");
    }

    ImGui_Text(ctx, "Colour");
    for (int i = 0; i < midicc::kColourCount; ++i) {
        ImGui_SameLine(ctx, nullptr, nullptr);
        char id[32];
        std::snprintf(id, sizeof(id), "##cc_col_%d", i);
        int flags = 0;
        double w = (i == c.colour) ? 30.0 : 22.0, h = w;
        if (ImGui_ColorButton(ctx, id, rgba_(midicc::kColours[i], 0xFF), &flags, &w, &h)) {
            c.colour = i;
            dirty = true;
        }
    }
    return dirty;
}

}  // namespace

// The row above the UF8 schematic: which layer is edited, and what it drives.
// Returns true when the layer is a MIDI CC layer (the caller then draws the CC
// editor instead of the schematic).
bool drawUf8LayerKindRow(ImGui_Context* ctx, int layer)
{
    char hdr[48];
    std::snprintf(hdr, sizeof(hdr), "Editing: Layer %d", layer + 1);
    ImGui_Text(ctx, hdr);
    ImGui_SameLine(ctx, nullptr, nullptr);
    ImGui_Text(ctx, "   Layer type");
    ImGui_SameLine(ctx, nullptr, nullptr);
    ImGui_SetNextItemWidth(ctx, 140.0);
    const bool cc = bnd::layerKind(layer) == bnd::LayerKind::MidiCc;
    if (ImGui_BeginCombo(ctx, "##uf8_layer_kind", cc ? "MIDI CC" : "REAPER", nullptr)) {
        bool sR = !cc, sC = cc;
        if (ImGui_Selectable(ctx, "REAPER##lk_reaper", &sR, nullptr, nullptr, nullptr))
            bnd::setLayerKind(layer, bnd::LayerKind::Reaper);
        if (ImGui_Selectable(ctx, "MIDI CC##lk_cc", &sC, nullptr, nullptr, nullptr))
            bnd::setLayerKind(layer, bnd::LayerKind::MidiCc);
        ImGui_EndCombo(ctx);
    }
    tip_(ctx, "MIDI CC: faders, V-Pots and keys send MIDI on this layer.");
    if (!cc) return false;
    // On a CC layer the schematic is not drawn, so the layer keys are here.
    for (int l = 0; l < 3; ++l) {
        ImGui_SameLine(ctx, nullptr, nullptr);
        char id[32];
        std::snprintf(id, sizeof(id), "Layer %d##lk_go_%d", l + 1, l);
        if (ImGui_Button(ctx, id, nullptr, nullptr)) {
            const bnd::ButtonId b = l == 0 ? bnd::ButtonId::Layer1
                                  : l == 1 ? bnd::ButtonId::Layer2 : bnd::ButtonId::Layer3;
            bnd::dispatch(b, /*pressed*/ true);
            bnd::dispatch(b, /*pressed*/ false);
        }
    }
    return bnd::layerKind(layer) == bnd::LayerKind::MidiCc;
}

// The CC layer's editor, in place of the schematic.
void drawUf8MidiCcLayer(ImGui_Context* ctx, int layer, int livePage)
{
    midicc::Layer L = bnd::ccLayer(layer);
    bool dirty = false;

    ImGui_Spacing(ctx);
    dirty |= drawLayerBlock_(ctx, layer, L);
    ImGui_Spacing(ctx);
    ImGui_Separator(ctx);
    ImGui_Spacing(ctx);

    // The page the UF8 is on, unless the user clicks another one here.
    static int s_page = 0;
    static int s_lastLive = -1;
    if (livePage != s_lastLive) { s_page = livePage; s_lastLive = livePage; }
    s_page = std::clamp(s_page, 0, midicc::kPages - 1);
    ImGui_Text(ctx, "Page");
    for (int p = 0; p < midicc::kPages; ++p) {
        ImGui_SameLine(ctx, nullptr, nullptr);
        char id[24];
        std::snprintf(id, sizeof(id), " %d ##cc_page_%d", p + 1, p);
        int pushed = 0;
        if (p == s_page) { ImGui_PushStyleColor(ctx, ImGui_Col_Button, 0x4477CCFF); ++pushed; }
        if (ImGui_Button(ctx, id, nullptr, nullptr)) s_page = p;
        if (pushed) ImGui_PopStyleColor(ctx, &pushed);
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    ImGui_TextDisabled(ctx, "   on the UF8: soft-key bank keys 1 to 5, PAGE < >");

    ImGui_Spacing(ctx);
    const midicc::Page& P = L.pages[s_page];
    struct Row { const char* name; Kind k; };
    const Row rows[] = {
        {"Soft-key",     Kind::Soft},
        {"V-Pot",        Kind::VPot},
        {"V-Pot push",   Kind::Push},
        {"Send/plug-in", Kind::SendPlugin},
        {"Fader",        Kind::Fader},
    };
    // Fixed column for the cells, so the rows line up whatever the label width.
    double cellX = 110.0;
    for (const Row& r : rows) {
        ImGui_Text(ctx, r.name);
        for (int i = 0; i < midicc::kStrips; ++i) {
            if (i == 0) ImGui_SameLine(ctx, &cellX, nullptr);
            else        ImGui_SameLine(ctx, nullptr, nullptr);
            drawCell_(ctx, P.at(r.k, i), r.k, i, s_page);
        }
    }
    ImGui_Text(ctx, "Foot");
    for (int i = 0; i < midicc::kFeet; ++i) {
        if (i == 0) ImGui_SameLine(ctx, &cellX, nullptr);
        else        ImGui_SameLine(ctx, nullptr, nullptr);
        drawCell_(ctx, P.foot[i], Kind::Foot, i, s_page);
    }
    ImGui_TextDisabled(ctx,
        "FINE, the channel encoder and its keys, the zoom pad, the automation row, FLIP,\n"
        "the layer keys and 360 work as on Layer 1. SOLO, CUT, SEL, BANK, Quick and the\n"
        "other keys have no function on this layer.");

    ImGui_Spacing(ctx);
    ImGui_Separator(ctx);
    ImGui_Spacing(ctx);
    dirty |= drawControlEditor_(ctx, L, s_page);

    if (dirty) bnd::setCcLayer(layer, L);
}
