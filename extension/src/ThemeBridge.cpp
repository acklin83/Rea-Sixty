#include "ThemeBridge.h"

#include <cstdint>

#include "reaper_plugin_functions.h"
#include "reaper_imgui_functions.h"

namespace uf8 {

namespace {

// REAPER COLORREF (0x00BBGGRR, R at byte 0) → ReaImGui big-endian RGBA
// (0xRRGGBBAA, R at MSB). Confirmed against cfillion's Color::fromBigEndian
// in reaimgui/api/style.cpp.
inline int toRgba(intptr_t c, int alpha = 0xFF)
{
    const int r =  c        & 0xFF;
    const int g = (c >>  8) & 0xFF;
    const int b = (c >> 16) & 0xFF;
    return (r << 24) | (g << 16) | (b << 8) | (alpha & 0xFF);
}

// Pull theme color idx, fall back to `fallback` if REAPER returns 0
// (a black-on-black theme would be a degenerate case anyway).
inline int themeRgba(int idx, intptr_t fallback)
{
    const intptr_t c = GetColorTheme(idx, static_cast<int>(fallback));
    return toRgba(c == 0 ? fallback : c);
}

// Slight darken/lighten of an RGBA value. amount > 0 lightens, < 0 darkens.
// Used to derive Hovered/Active variants when REAPER's theme doesn't carry
// dedicated slots for them.
inline int shade(int rgba, int amount)
{
    auto adj = [&](int v) {
        v += amount;
        if (v < 0)   v = 0;
        if (v > 255) v = 255;
        return v;
    };
    const int r = adj((rgba >> 24) & 0xFF);
    const int g = adj((rgba >> 16) & 0xFF);
    const int b = adj((rgba >>  8) & 0xFF);
    const int a =      rgba        & 0xFF;
    return (r << 24) | (g << 16) | (b << 8) | a;
}

} // namespace

int ThemeBridge::pushAll(ImGui_Context* ctx)
{
    // ----------------------------------------------------------------------
    // Palette is sourced from REAPER's active theme via GetColorTheme(idx).
    // Indices below are the deprecated indexed API — kept because it still
    // works on every REAPER ≥ 6.x and we haven't vendored icontheme.h yet
    // (the structured GetColorThemeStruct path is the long-term home for
    // mixer-specific slots like mcp_track_bg / mcp_track_text). Hardcoded
    // SSL-schematic constants stay as fallbacks for slots a theme leaves
    // empty — that way the mixer is always readable, even on themes that
    // only define the bare minimum.
    //
    // Index meanings per reaper_plugin.h (REAPER_WANT_DEPRECATED_COLOR-
    // THEMESTUFF section). Inlined as ints so we don't have to define the
    // SDK macro and pull in its deprecation noise.
    // ----------------------------------------------------------------------
    constexpr int kIdxTimelineFG    = 0;   // primary text-on-timeline
    constexpr int kIdxItemText      = 1;   // item label text
    constexpr int kIdxItemBg        = 2;   // item / frame fill
    constexpr int kIdxTimelineBg    = 4;   // global background
    constexpr int kIdxTimelineSelBg = 5;   // selection accent
    constexpr int kIdxItemControls  = 6;   // button / control face
    constexpr int kIdxTrackBg1      = 24;  // primary track lane bg
    constexpr int kIdxTrackBg2      = 25;  // alt track lane bg / borders
    constexpr int kIdxPeaks1        = 28;  // meter foreground
    (void)kIdxItemText; (void)kIdxTimelineFG;  // reserved for plotting/labels

    // Fallback palette (mirrors the prior hardcoded SSL-schematic look).
    constexpr int kBg          = 0x1A1E24FF;
    constexpr int kChild       = 0x202530FF;
    constexpr int kFrame       = 0x252A33FF;
    constexpr int kFrameHover  = 0x2E3440FF;
    constexpr int kFrameActive = 0x3A4253FF;
    constexpr int kBorder      = 0x4A5060FF;
    constexpr int kButton      = 0x2A3140FF;
    constexpr int kButtonAct   = 0x4477CCFF;
    constexpr int kAccent      = 0x4477CCFF;
    constexpr int kAccentBri   = 0x6699EEFF;
    constexpr int kAccentDim   = 0x2A3F66FF;
    constexpr int kText        = 0xD0D4DAFF;
    constexpr int kTextDim     = 0x70747CFF;
    constexpr int kSeparator   = 0x383C44FF;

    // Pull theme slots once per frame.
    const int bg     = themeRgba(kIdxTrackBg1,      kBg);
    const int child  = themeRgba(kIdxTrackBg2,      kChild);
    const int frame  = themeRgba(kIdxItemBg,        kFrame);
    const int border = themeRgba(kIdxTrackBg2,      kBorder);
    const int btn    = themeRgba(kIdxItemControls,  kButton);
    const int accent = themeRgba(kIdxTimelineSelBg, kAccent);
    const int text   = themeRgba(kIdxItemText,      kText);
    const int peak   = themeRgba(kIdxPeaks1,        0x40C040FF);
    (void)kIdxTimelineBg;  // currently unused — picked TRACKBG1 over it

    // Derive Hovered/Active variants by shading the base colour: REAPER's
    // indexed theme doesn't carry dedicated hover/active slots.
    const int frameHover  = shade(frame, +12);
    const int frameActive = shade(frame, +24);
    const int btnHover    = shade(btn,   +18);
    const int accentBri   = shade(accent, +24);
    const int accentDim   = shade(accent, -32);
    const int textDim     = shade(text,  -64);
    const int separator   = shade(border, -16);
    (void)kFrameHover; (void)kFrameActive; (void)kAccentBri;
    (void)kAccentDim;  (void)kTextDim;     (void)kSeparator;

    int n = 0;
    auto push = [&](int colIdx, int rgba) {
        ImGui_PushStyleColor(ctx, colIdx, rgba);
        ++n;
    };

    // Conservative set — sticks to enum names that have been stable
    // across ImGui 1.89..1.91+. Skips the Tab* family which renamed in
    // 1.91 and crashes here as a NULL function pointer when the deployed
    // dylib doesn't export the old names.
    push(ImGui_Col_WindowBg,         bg);
    push(ImGui_Col_ChildBg,          child);
    push(ImGui_Col_PopupBg,          child);
    push(ImGui_Col_Border,           border);

    push(ImGui_Col_FrameBg,          frame);
    push(ImGui_Col_FrameBgHovered,   frameHover);
    push(ImGui_Col_FrameBgActive,    frameActive);

    push(ImGui_Col_Button,           btn);
    push(ImGui_Col_ButtonHovered,    btnHover);
    push(ImGui_Col_ButtonActive,     accent);

    push(ImGui_Col_Header,           accentDim);
    push(ImGui_Col_HeaderHovered,    accentBri);
    push(ImGui_Col_HeaderActive,     accent);

    push(ImGui_Col_CheckMark,        accentBri);
    push(ImGui_Col_Separator,        separator);

    push(ImGui_Col_Text,             text);
    push(ImGui_Col_TextDisabled,     textDim);

    push(ImGui_Col_PlotLines,        peak);

    (void)kButtonAct;  // future: a brighter "button pressed" variant
    return n;
}

void ThemeBridge::popAll(ImGui_Context* ctx, int count)
{
    if (count > 0) ImGui_PopStyleColor(ctx, &count);
}

} // namespace uf8
