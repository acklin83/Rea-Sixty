#pragma once

// Settings → Manual tab. Renders the embedded docs/user-manual.md (baked in
// at build time via cmake/embed_factory_bundle.cmake → generated/user_manual.h)
// with a focused Markdown renderer covering exactly the constructs the manual
// uses: H1/H2/H3 headings, paragraphs, bullet/numbered lists, GFM tables, and
// inline **bold** / `code` / [links](url). No images, no code fences — the
// manual has none. Two-level chapter → section navigation on the left, the
// selected chapter rendered (and anchor-scrolled) on the right.

struct ImGui_Context;

namespace uf8 {

class ManualView {
public:
    // Matches the RailEntry draw signature in MixerWindow.cpp.
    static void draw(ImGui_Context* ctx);
};

} // namespace uf8

// Font + base-size accessors, defined in MixerWindow.cpp and refreshed each
// frame. The manual renderer needs the bold and monospace faces (the manual
// leans heavily on both) plus the active UI font size to scale headings.
struct ImGui_Font;
ImGui_Font* reasixty_uiSansFont();
ImGui_Font* reasixty_uiBoldFont();
ImGui_Font* reasixty_uiMonoFont();
double      reasixty_uiFontPx();
