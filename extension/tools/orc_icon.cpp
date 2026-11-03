// orc_icon — renders ORC's app icon from the wordmark (orc/OrcLogo.cpp).
//
// Writes an .iconset folder with every size macOS asks for; CMake turns it
// into ORC.icns with iconutil. With a second argument it also writes two
// preview PNGs: the 1024 px icon and the menu-bar mark in black.
//
//   orc_icon <out.iconset> [<preview dir>]
//
// ⇨ AND THE ICONS FOR WINDOWS, one drawing for both platforms (Frank 29.09.2026):
//   orc_icon --ico <folder>   writes ORC.ico, ORC-tray-white.ico, ORC-tray-black.ico
// CoreGraphics only runs here, so these three live in orc/win/ and
// tools/orc-release.sh refuses a release when they no longer match this drawing.

#include "OrcLogo.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <vector>

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

std::vector<std::uint8_t> pngBytes(CGImageRef img)
{
    CFMutableDataRef data = CFDataCreateMutable(nullptr, 0);
    CGImageDestinationRef dst = CGImageDestinationCreateWithData(data, CFSTR("public.png"), 1, nullptr);
    CGImageDestinationAddImage(dst, img, nullptr);
    CGImageDestinationFinalize(dst);
    CFRelease(dst);
    std::vector<std::uint8_t> out(CFDataGetBytePtr(data), CFDataGetBytePtr(data) + CFDataGetLength(data));
    CFRelease(data);
    return out;
}

// The file holds each size as a PNG, which Windows reads from Vista on.
bool writeIco(const std::string& path, const std::vector<int>& sizes, CGImageRef (*draw)(int, double),
              double shade)
{
    const auto le16 = [](std::vector<std::uint8_t>& v, unsigned x) {
        v.push_back(x & 0xFF); v.push_back((x >> 8) & 0xFF);
    };
    const auto le32 = [&](std::vector<std::uint8_t>& v, std::uint32_t x) {
        le16(v, x & 0xFFFF); le16(v, x >> 16);
    };
    std::vector<std::vector<std::uint8_t>> pngs;
    for (const int n : sizes) {
        CGImageRef img = draw(n, shade);
        pngs.push_back(pngBytes(img));
        CGImageRelease(img);
    }
    std::vector<std::uint8_t> out;
    le16(out, 0); le16(out, 1); le16(out, static_cast<unsigned>(sizes.size()));
    std::uint32_t offset = 6 + 16 * static_cast<std::uint32_t>(sizes.size());
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        out.push_back(sizes[i] >= 256 ? 0 : static_cast<std::uint8_t>(sizes[i]));
        out.push_back(sizes[i] >= 256 ? 0 : static_cast<std::uint8_t>(sizes[i]));
        out.push_back(0); out.push_back(0);
        le16(out, 1); le16(out, 32);
        le32(out, static_cast<std::uint32_t>(pngs[i].size()));
        le32(out, offset);
        offset += static_cast<std::uint32_t>(pngs[i].size());
    }
    for (const auto& p : pngs) out.insert(out.end(), p.begin(), p.end());
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

CGImageRef appIconFor(int n, double) { return icon(n); }

// ⇨ THE TASKBAR'S FIELD IS SQUARE, and the wordmark is three times as wide as high, so
// it fills the width and nothing else. Transparent, the letters in one shade.
CGImageRef trayMark(int n, double shade)
{
    CGContextRef ctx = canvas(n, n);
    CGColorRef c = rgb(shade, shade, shade);
    const double h = n / orc::wordmarkAspect();
    orc::drawWordmark(ctx, CGRectMake(0, (n - h) / 2.0, n, h), c);
    CGImageRef img = CGBitmapContextCreateImage(ctx);
    CGColorRelease(c);
    CGContextRelease(ctx);
    return img;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc == 3 && std::string(argv[1]) == "--ico") {
        const std::string d = argv[2];
        const std::vector<int> app{ 16, 20, 24, 32, 40, 48, 64, 128, 256 };
        const std::vector<int> tray{ 16, 20, 24, 32, 40, 48, 64 };
        const bool ok = writeIco(d + "/ORC.ico", app, appIconFor, 0)
                     && writeIco(d + "/ORC-tray-white.ico", tray, trayMark, 1.0)
                     && writeIco(d + "/ORC-tray-black.ico", tray, trayMark, 0.0);
        if (!ok) std::fprintf(stderr, "orc_icon: cannot write into %s\n", d.c_str());
        return ok ? 0 : 1;
    }
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
        // (a 22 pt bar at 2x), 8.5 pt of it the letters as in OrcApp.mm.
        const int mh = 44;
        const int lh = 17;
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
