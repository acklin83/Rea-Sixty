#include "ThemeBridge.h"

#include "reaper_imgui_functions.h"

namespace uf8 {

// kVanilla — the original Rea-Sixty palette. Mirrors the UF8-schematic
// colours in drawUf8Vector so the surrounding ImGui surface reads as
// one continuous mock-up rather than "REAPER widget on top of a custom
// canvas". Hardware-button tones come from drawHwBtn (kSelFill 0x4477CC,
// idle 0x252A33, hover 0x3A4253, border 0x4A5060, txt 0xD0D4DA).
const ThemePalette kVanillaPalette {
    /*windowBg*/      0x1A1E24FF,
    /*childBg*/       0x202530FF,
    /*popupBg*/       0x202530FF,
    /*border*/        0x4A5060FF,
    /*frameBg*/       0x252A33FF,
    /*frameBgHovered*/0x2E3440FF,
    /*frameBgActive*/ 0x3A4253FF,
    /*button*/        0x2A3140FF,
    /*buttonHovered*/ 0x3A4253FF,
    /*buttonActive*/  0x4477CCFF,
    /*header*/        0x2A3F66FF,
    /*headerHovered*/ 0x6699EEFF,
    /*headerActive*/  0x4477CCFF,
    /*checkMark*/     0x6699EEFF,
    /*separator*/     0x383C44FF,
    /*text*/          0xD0D4DAFF,
    /*textDisabled*/  0x70747CFF,
    /*plotLines*/     0x40C040FF,
};

// kDark — port of the RAPID project's MixnoteStyle theme. Neutral
// 4-level dark background (body → card → input → border) with an Indigo
// accent family. Original values: RAPID.lua lines 144–164.
const ThemePalette kDarkPalette {
    /*windowBg*/      0x0F0F0FFF,   // bg_body
    /*childBg*/       0x1A1A1AFF,   // bg_card
    /*popupBg*/       0x1A1A1AFF,   // bg_card
    /*border*/        0x3A3A3AFF,   // bg_border
    /*frameBg*/       0x2A2A2AFF,   // bg_input
    /*frameBgHovered*/0x3A3A3AFF,   // bg_border
    /*frameBgActive*/ 0x3A3A3AFF,   // bg_border
    /*button*/        0x6366F1FF,   // accent
    /*buttonHovered*/ 0x5558E8FF,   // accent_hover
    /*buttonActive*/  0x4F46E5FF,   // accent_active
    /*header*/        0x6366F140,   // accent_dim
    /*headerHovered*/ 0x5558E8FF,   // accent_hover
    /*headerActive*/  0x6366F1FF,   // accent
    /*checkMark*/     0x6366F1FF,   // accent
    /*separator*/     0x3A3A3AFF,   // bg_border
    /*text*/          0xE5E7EBFF,   // text
    /*textDisabled*/  0x6B7280FF,   // text_muted
    /*plotLines*/     0x4ADE80FF,   // green
};

// kLight — high-contrast light mode mirroring Dark's structure (same
// Indigo accent family, inverted neutrals). 4-level background hierarchy
// runs near-white → white → light-gray → mid-gray for separators / borders.
const ThemePalette kLightPalette {
    /*windowBg*/      0xF5F5F5FF,   // body
    /*childBg*/       0xFFFFFFFF,   // card
    /*popupBg*/       0xFFFFFFFF,   // card
    /*border*/        0xCCCCCCFF,   // border
    /*frameBg*/       0xEAEAEAFF,   // input
    /*frameBgHovered*/0xDADADAFF,
    /*frameBgActive*/ 0xCACACAFF,
    /*button*/        0x4F46E5FF,   // accent (indigo)
    /*buttonHovered*/ 0x4338CAFF,
    /*buttonActive*/  0x3730A3FF,
    /*header*/        0x4F46E540,   // accent_dim (alpha 25%)
    /*headerHovered*/ 0x4338CAFF,
    /*headerActive*/  0x4F46E5FF,
    /*checkMark*/     0x4F46E5FF,
    /*separator*/     0xCCCCCCFF,
    /*text*/          0x111111FF,
    /*textDisabled*/  0x808080FF,
    /*plotLines*/     0x059669FF,   // forest green (better on white)
};

const ThemePalette& paletteFor(Theme t)
{
    switch (t) {
        case Theme::Dark:    return kDarkPalette;
        case Theme::Light:   return kLightPalette;
        case Theme::Vanilla:
        default:             return kVanillaPalette;
    }
}

int ThemeBridge::pushAll(ImGui_Context* ctx, const ThemePalette& p)
{
    int n = 0;
    auto push = [&](int colIdx, unsigned int rgba) {
        ImGui_PushStyleColor(ctx, colIdx, static_cast<int>(rgba));
        ++n;
    };

    // Conservative set — sticks to enum names that have been stable
    // across ImGui 1.89..1.91+. Skips the Tab* family (renamed to
    // TabSelected* etc. in 1.91, ReaImGui follows DearImGui), which
    // crashes here as a NULL function pointer when the deployed
    // dylib doesn't export the old names. Same caution for any
    // enum I can't verify against the user's installed version —
    // safer to undertheme than to take down REAPER on every tick.
    push(ImGui_Col_WindowBg,         p.windowBg);
    push(ImGui_Col_ChildBg,          p.childBg);
    push(ImGui_Col_PopupBg,          p.popupBg);
    push(ImGui_Col_Border,           p.border);

    push(ImGui_Col_FrameBg,          p.frameBg);
    push(ImGui_Col_FrameBgHovered,   p.frameBgHovered);
    push(ImGui_Col_FrameBgActive,    p.frameBgActive);

    push(ImGui_Col_Button,           p.button);
    push(ImGui_Col_ButtonHovered,    p.buttonHovered);
    push(ImGui_Col_ButtonActive,     p.buttonActive);

    push(ImGui_Col_Header,           p.header);
    push(ImGui_Col_HeaderHovered,    p.headerHovered);
    push(ImGui_Col_HeaderActive,     p.headerActive);

    push(ImGui_Col_CheckMark,        p.checkMark);
    push(ImGui_Col_Separator,        p.separator);

    push(ImGui_Col_Text,             p.text);
    push(ImGui_Col_TextDisabled,     p.textDisabled);

    push(ImGui_Col_PlotLines,        p.plotLines);
    return n;
}

void ThemeBridge::popAll(ImGui_Context* ctx, int count)
{
    if (count > 0) ImGui_PopStyleColor(ctx, &count);
}

} // namespace uf8
