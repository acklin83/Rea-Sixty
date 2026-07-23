#pragma once

// Settings → Exchange tab. The in-app face of the mapping exchange
// (api.reasixty.com): browse plug-in maps, download+install, and upload your
// own — no file export/import. Talks to the API through reasixty::http (async,
// polled once per frame so REAPER never blocks on the network).
//
// This first slice is settings + a connection test: it proves the HTTPS
// transport works from inside REAPER before the browse/download/upload UI is
// layered on. Base URL and the device token persist to ExtState.

// See ManualView.h: must be `class` to match reaper_imgui_functions.h's
// elaborated-type-specifier on MSVC.
class ImGui_Context;

namespace uf8 {

class ExchangeView {
public:
    // Matches the RailEntry draw signature in MixerWindow.cpp.
    static void draw(ImGui_Context* ctx);
};

} // namespace uf8
