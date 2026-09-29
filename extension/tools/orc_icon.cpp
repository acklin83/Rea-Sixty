// orc_icon — renders ORC's app icon from the wordmark (orc/OrcLogo.cpp).
//
// Writes an .iconset folder with every size macOS asks for; CMake turns it
// into ORC.icns with iconutil. With a second argument it also writes two
// preview PNGs: the 1024 px icon and the menu-bar mark in black.
//
//   orc_icon <out.iconset> [<preview dir>]

#include "OrcLogo.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <cstdio>
#include <string>
#include <sys/stat.h>

namespace {

bool writePng(CGImageRef img, const std::string& path)
{
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        nullptr, reinterpret_cast<const UInt8*>(path.c_str()),
        static_cast<CFIndex>(path.size()), false);
    CGImageDestinationRef dst = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, nullptr);
    bool ok = false;
    if (dst) {
        CGImageDestinationAddImage(dst, img, nullptr);
        ok = CGImageDestinationFinalize(dst);
        CFRelease(dst);
    }
    CFRelease(url);
    return ok;
}

CGContextRef canvas(int w, int h)
{
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(nullptr, w, h, 8, 0, cs,
                                             kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(cs);
    CGContextSetShouldAntialias(ctx, true);
    CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
    return ctx;
}

CGColorRef rgb(double r, double g, double b, double a = 1.0)
{
    return CGColorCreateSRGB(r, g, b, a);
}

// ⇨ THE ICON: the wordmark in white on a dark rounded square, on Apple's icon
// grid (the square is 824 of 1024 with a 100 px margin, corner radius about
// 22.5 % of its side).
CGImageRef icon(int n)
{
    CGContextRef ctx = canvas(n, n);
    const double s = n / 1024.0;
    const CGRect sq = CGRectMake(100 * s, 100 * s, 824 * s, 824 * s);
    const double radius = 185.4 * s;
    CGPathRef shape = CGPathCreateWithRoundedRect(sq, radius, radius, nullptr);

    // A soft shadow under the square, as macOS icons have.
    CGColorRef shadow = rgb(0, 0, 0, 0.35);
    CGContextSaveGState(ctx);
    CGContextSetShadowWithColor(ctx, CGSizeMake(0, -10 * s), 20 * s, shadow);
    CGColorRef base = rgb(0.118, 0.122, 0.141);
    CGContextSetFillColorWithColor(ctx, base);
    CGContextAddPath(ctx, shape);
    CGContextFillPath(ctx);
    CGContextRestoreGState(ctx);

    // A gentle light from the top.
    CGContextSaveGState(ctx);
    CGContextAddPath(ctx, shape);
    CGContextClip(ctx);
    CGColorRef top = rgb(0.20, 0.205, 0.235);
    const void* cols[] = { top, base };
    CFArrayRef arr = CFArrayCreate(nullptr, cols, 2, &kCFTypeArrayCallBacks);
    CGGradientRef grad = CGGradientCreateWithColors(nullptr, arr, nullptr);
    CGContextDrawLinearGradient(ctx, grad, CGPointMake(0, sq.origin.y + sq.size.height),
                                CGPointMake(0, sq.origin.y), 0);
    CGGradientRelease(grad);
    CFRelease(arr);
    CGContextRestoreGState(ctx);

    // The wordmark, centred.
    const double h = 176 * s;
    CGColorRef white = rgb(1, 1, 1);
    orc::drawWordmark(ctx, CGRectMake(sq.origin.x, 512 * s - h / 2.0, sq.size.width, h), white);

    CGImageRef img = CGBitmapContextCreateImage(ctx);
    CGColorRelease(white); CGColorRelease(top); CGColorRelease(base); CGColorRelease(shadow);
    CGPathRelease(shape);
    CGContextRelease(ctx);
    return img;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: orc_icon <out.iconset> [<preview dir>]\n");
        return 2;
    }
    const std::string dir = argv[1];
    ::mkdir(dir.c_str(), 0755);
    const struct { int px; const char* name; } sizes[] = {
        { 16, "icon_16x16.png" },    { 32, "icon_16x16@2x.png" },
        { 32, "icon_32x32.png" },    { 64, "icon_32x32@2x.png" },
        { 128, "icon_128x128.png" }, { 256, "icon_128x128@2x.png" },
        { 256, "icon_256x256.png" }, { 512, "icon_256x256@2x.png" },
        { 512, "icon_512x512.png" }, { 1024, "icon_512x512@2x.png" },
    };
    for (const auto& sz : sizes) {
        CGImageRef img = icon(sz.px);
        const bool ok = writePng(img, dir + "/" + sz.name);
        CGImageRelease(img);
        if (!ok) { std::fprintf(stderr, "orc_icon: cannot write %s\n", sz.name); return 1; }
    }
    if (argc >= 3) {
        const std::string pv = argv[2];
        ::mkdir(pv.c_str(), 0755);
        CGImageRef big = icon(1024);
        writePng(big, pv + "/orc-icon-1024.png");
        CGImageRelease(big);
        // The menu-bar mark as macOS draws it on a light bar: black, 44 px high
        // (a 22 pt bar at 2x), 14 pt of it the letters.
        const int mh = 44;
        const int lh = 28;
        const int mw = static_cast<int>(orc::wordmarkAspect() * lh) + 12;
        CGContextRef ctx = canvas(mw, mh);
        CGColorRef black = rgb(0, 0, 0);
        orc::drawWordmark(ctx, CGRectMake(0, (mh - lh) / 2.0, mw, lh), black);
        CGImageRef mark = CGBitmapContextCreateImage(ctx);
        writePng(mark, pv + "/orc-menubar.png");
        CGImageRelease(mark);
        CGColorRelease(black);
        CGContextRelease(ctx);
    }
    return 0;
}
