#pragma once
//
// OrcLogo — the ORC wordmark, drawn from lines rather than set in a font.
//
// ⇨ ONE DRAWING FOR TWO PLACES (Frank 29.09.2026: "einfach ORC als Buchstaben,
// soll auch in der top-row auf mac so sein"). The menu bar item draws it at
// run time as a template image, so macOS tints it for a light or dark bar; the
// app icon is rendered from the same function at build time (tools/orc_icon.mm).
// Geometric strokes and not a typeface: Apple's system fonts are licensed for
// user interfaces, not for baking into a logo, and this way the two places
// cannot drift apart.
//
// Plain CoreGraphics, so a command-line tool can use it without AppKit.
#include <CoreGraphics/CoreGraphics.h>

namespace orc {

// Width of the wordmark for a height of 1.
double wordmarkAspect();

// Draws "ORC" filling `box` vertically, centred horizontally, in `color`.
void drawWordmark(CGContextRef ctx, CGRect box, CGColorRef color);

}  // namespace orc
