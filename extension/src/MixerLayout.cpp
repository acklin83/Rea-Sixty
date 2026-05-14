#include "MixerLayout.h"
#include "ThemeAssets.h"

#include <cstdio>

#include "reaper_imgui_functions.h"

namespace uf8 {

namespace {

// Debug-pane labels paired with Slot enum order (must match exactly).
// Phase B3 verification UI. Replaced by the real strip layout in Phase C.
constexpr const char* kSlotLabel[(int)theme_assets::Slot::kCount] = {
    "mcp_vu",
    "tcp_vu",
    "mcp_fader",
    "mcp_fader_handle",
    "mcp_knob",
    "mcp_panknob",
    "mcp_recarm",
    "mcp_mute",
    "mcp_solo",
};

// Max preview width per asset — caps oversized sprite-sheets so the
// debug grid stays readable. Aspect ratio is preserved.
constexpr double kPreviewMaxW = 192.0;

} // namespace

void MixerLayout::draw(ImGui_Context* ctx)
{
    // Drive the theme asset cache. tick() detects theme switches +
    // context recreation. Cheap (~one stat() call) when nothing has
    // changed since the last frame.
    theme_assets::tick(ctx);

    // Header — surface what we know about the active theme so the user
    // can sanity-check the resolution path during Phase B3.
    char header[768];
    const char* path = theme_assets::activeThemePath();
    std::snprintf(header, sizeof(header),
                  "Theme: %s",
                  (path && *path) ? path : "(no theme detected)");
    ImGui_Text(ctx, header);

    ImGui_Separator(ctx);
    ImGui_Text(ctx,
        "Phase B3 — REAPER WALTER PNG preview. Switch themes in "
        "Options → Themes; this list should refresh on the next frame.");
    ImGui_Separator(ctx);

    ImGui_DrawList* dl = ImGui_GetWindowDrawList(ctx);

    for (int i = 0; i < (int)theme_assets::Slot::kCount; ++i) {
        const auto slot = (theme_assets::Slot)i;
        const bool loaded = theme_assets::slotLoaded(slot);
        ImGui_Image* img = theme_assets::get(slot);

        char line[160];
        if (loaded && img) {
            double w = 0, h = 0;
            ImGui_Image_GetSize(img, &w, &h);
            std::snprintf(line, sizeof(line),
                          "[%s]  %.0fx%.0f", kSlotLabel[i], w, h);
            ImGui_Text(ctx, line);

            // Cap preview width to keep the grid readable; preserve AR.
            double pw = w, ph = h;
            if (pw > kPreviewMaxW && pw > 0) {
                const double k = kPreviewMaxW / pw;
                pw *= k; ph *= k;
            }
            if (dl && pw > 0 && ph > 0) {
                double sx = 0, sy = 0;
                ImGui_GetCursorScreenPos(ctx, &sx, &sy);
                ImGui_DrawList_AddImage(dl, img, sx, sy, sx + pw, sy + ph,
                                        /*uv_min_x*/ nullptr,
                                        /*uv_min_y*/ nullptr,
                                        /*uv_max_x*/ nullptr,
                                        /*uv_max_y*/ nullptr,
                                        /*col_rgba*/ nullptr);
                // Reserve layout space so subsequent widgets don't overlap.
                ImGui_Dummy(ctx, pw, ph);
            }
        } else {
            std::snprintf(line, sizeof(line),
                          "[%s]  — not in active theme", kSlotLabel[i]);
            ImGui_Text(ctx, line);
        }
        ImGui_Separator(ctx);
    }
}

} // namespace uf8
