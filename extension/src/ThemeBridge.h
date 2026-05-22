#pragma once
//
// ThemeBridge — pushes a curated palette as ReaImGui style colors at frame
// entry. Two palettes currently shipped: `kVanilla` (the original dark-blue
// scheme that ships with Rea-Sixty) and `kMixnote` (Indigo accent on neutral
// dark, ported from the RAPID project). User selection lives in main.cpp
// (g_themeSelection, ExtState "theme"); ThemeBridge stays agnostic — call
// pushAll(ctx, palette) with whichever palette the caller wants.
//
// API model matches ReaImGui's Push/Pop convention: pushAll() at the start of
// each frame, popAll() at the end.
//

class ImGui_Context; // ReaImGui opaque context pointer

namespace uf8 {

// 16-slot palette. Order doesn't matter — palettes are dispatched by name.
struct ThemePalette {
    // Unsigned so brace-initialisers can hold 0xD0D4DAFF / 0xE5E7EBFF
    // (bit-31 set → narrowing error if the field were `int`). Cast to
    // int at the ImGui_PushStyleColor call site — the byte order is
    // unchanged, only the storage signedness.
    unsigned int windowBg;
    unsigned int childBg;
    unsigned int popupBg;
    unsigned int border;
    unsigned int frameBg;
    unsigned int frameBgHovered;
    unsigned int frameBgActive;
    unsigned int button;
    unsigned int buttonHovered;
    unsigned int buttonActive;
    unsigned int header;
    unsigned int headerHovered;
    unsigned int headerActive;
    unsigned int checkMark;
    unsigned int separator;
    unsigned int text;
    unsigned int textDisabled;
    unsigned int plotLines;
};

// Theme identifiers — kept stable in ExtState (numeric value persists).
enum class Theme : int {
    Vanilla = 0,
    Mixnote = 1,
};

// Built-in palettes.
extern const ThemePalette kVanillaPalette;
extern const ThemePalette kMixnotePalette;

// Resolve the active palette from a Theme enum. Falls back to Vanilla
// for unknown values so a corrupted ExtState can't render a blank window.
const ThemePalette& paletteFor(Theme t);

class ThemeBridge {
public:
    // Push the given palette's colours onto the ImGui style stack.
    // Returns the count for the matching popAll().
    static int pushAll(ImGui_Context* ctx, const ThemePalette& palette);

    // Pop the colours pushed by the matching pushAll() call.
    static void popAll(ImGui_Context* ctx, int count);
};

} // namespace uf8
