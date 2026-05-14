#pragma once
//
// ThemeAssets — loads the active REAPER theme's WALTER PNG sprite-sheets
// (mcp_vu.png, mcp_fader*.png, mcp_knob.png, mcp_button.png, etc.) and
// exposes them to the Mixer as ImGui image handles.
//
// Resolution path:
//   GetLastColorThemeFile() → either a `.ReaperTheme` file (PNG resources
//   in a sibling folder of the same basename) OR a `.ReaperThemeZip`
//   bundle (PNGs live inside the zip). Both are handled transparently.
//
// Lifetime:
//   - Raw PNG bytes are cached in memory; theme switches invalidate and
//     reload them (poll on mtime each tick).
//   - ImGui image handles are attached to the calling context. When the
//     MixerWindow recreates its context on toggle, tick() detects the
//     change and re-materialises the handles automatically.
//

class ImGui_Context;
class ImGui_Image;

namespace uf8::theme_assets {

enum class Slot : int {
    kMcpVU = 0,          // mcp_vu.png   — 4 vertical strips (W/4 each)
    kTcpVU,              // tcp_vu.png   — 8 horizontal slices (H/8 each)
    kMcpFader,           // mcp_fader.png — fader background/track
    kMcpFaderHandle,     // mcp_fader_handle.png — fader handle cap
    kMcpKnob,            // mcp_knob.png — knob rotation frames
    kMcpPanKnob,         // mcp_panknob.png — pan-knob rotation frames
    kMcpRecArm,          // mcp_recarm.png — rec-arm button
    kMcpMute,            // mcp_mute.png   — mute button
    kMcpSolo,            // mcp_solo.png   — solo button
    kCount,
};

// Call once per Mixer frame. Detects theme switches, reloads PNG bytes
// on change, materialises ImGui image handles attached to ctx. Cheap
// (one stat() syscall) when nothing has changed.
void tick(ImGui_Context* ctx);

// Returns the image handle for the slot, or nullptr if the theme does
// not provide that asset (caller falls back to ImGui primitive drawing).
ImGui_Image* get(Slot s);

// Computes UV coords for a particular frame within a sprite-sheet.
// Returns false (and fills uv=0..1) if the slot has no defined slicing —
// treat the image as a single frame in that case.
bool getUv(Slot s, int frame, double* u0, double* v0, double* u1, double* v1);

// Diagnostics for the debug pane (Phase B3): returns the active theme
// file path and a per-slot "loaded?" snapshot.
const char* activeThemePath();
bool slotLoaded(Slot s);

} // namespace uf8::theme_assets
