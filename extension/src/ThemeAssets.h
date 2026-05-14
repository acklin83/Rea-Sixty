#pragma once
//
// ThemeAssets — loads the active REAPER theme's PNG sprite-sheets (mcp_vol*,
// gen_mute_*, gen_solo_*, mcp_recarm_*, gen_knob_bg_*, meter_bg_mcp, ...)
// and exposes them to the Mixer as ImGui image handles.
//
// Resolution path:
//   GetLastColorThemeFile() → either a `.ReaperTheme` file (PNG resources
//   in a sibling folder) OR a `.ReaperThemeZip` bundle (PNGs live inside
//   the zip). Both are handled transparently.
//
// Filename conventions vary across themes (Default 6/7, WT, Reapertips,
// Imperial etc.), so each Slot has an ordered candidate list — the loader
// walks it and picks the first hit. HiDPI subfolder priority is "200/"
// then "150/" then base, so Retina users get crisp sprites where the
// theme ships them.
//
// Lifetime:
//   - Raw PNG bytes cached in memory; theme switches invalidate + reload.
//   - ImGui image handles attached to the calling context. When the
//     MixerWindow recreates its context on toggle, tick() detects the
//     change and re-materialises the handles automatically.
//

class ImGui_Context;
class ImGui_Image;

namespace uf8::theme_assets {

// Logical asset slots. Multiple slots can resolve to the same file in
// some themes (e.g. mute_on / mute_off may live in one sprite-sheet on
// older themes); the loader still treats them independently.
enum class Slot : int {
    kMcpFaderBg = 0,     // vertical fader groove
    kMcpFaderHandle,     // fader thumb/cap

    kMcpMuteOff,         // mute button — inactive frame
    kMcpMuteOn,          // mute button — active frame
    kMcpSoloOff,         // solo button — inactive frame
    kMcpSoloOn,          // solo button — active frame
    kMcpRecArmOff,       // record-arm button — inactive
    kMcpRecArmOn,        // record-arm button — armed

    kMcpMeterBg,         // VU meter background panel
    kMcpKnob,            // small knob (pan / aux)
    kMcpKnobLarge,       // large knob (EQ / comp / gate)

    kCount,
};

// Call once per Mixer frame. Detects theme switches, reloads PNG bytes
// on change, materialises ImGui image handles attached to ctx.
void tick(ImGui_Context* ctx);

// Returns the image handle for the slot, or nullptr if no theme PNG
// satisfied the candidate list (caller falls back to primitive draw).
ImGui_Image* get(Slot s);

// Pixel size of the resolved sprite — used by layout to compute scale.
// Returns false (and writes 0,0) when the slot isn't loaded.
bool getSize(Slot s, double* w, double* h);

// Diagnostics for the debug pane: active theme path + per-slot status.
const char* activeThemePath();
bool slotLoaded(Slot s);

} // namespace uf8::theme_assets
