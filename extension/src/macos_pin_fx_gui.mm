// macOS native window-positioning for the "Pin plug-in GUI position"
// feature.
//
// Why this exists: SWELL's SetWindowPos / GetWindowRect / GetSystemMetrics
// are unreliable from a REAPER extension on macOS 15 — both
// rec->GetFunc(...) and dlsym(RTLD_DEFAULT, ...) hit the same hardened-
// runtime / dyld-scope problem we previously saw with BrowseForSaveFile
// (see macos_save_dialog.mm). Going to AppKit directly side-steps it.
//
// Coordinate convention used by callers (and inside main.cpp):
//   * Win32-style: origin (0, 0) at the TOP-LEFT of the primary screen,
//     y increases downward.
// macOS uses NSScreen / NSWindow with origin BOTTOM-LEFT, y up. The
// helpers below translate between the two.

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

namespace uf8 {

// Resolve a SWELL HWND (which may be either an NSWindow* or an NSView*
// pointer in disguise) to the NSWindow that owns it. Returns nil if
// the argument isn't a recognisable Cocoa object.
static NSWindow* windowFromHwnd_(void* hwnd)
{
    if (!hwnd) return nil;
    id obj = (__bridge id)hwnd;
    if ([obj isKindOfClass:[NSWindow class]]) return (NSWindow*)obj;
    if ([obj isKindOfClass:[NSView class]])   return [(NSView*)obj window];
    return nil;
}

// The screen we treat as canonical for Win32-style coordinate maths.
// [NSScreen screens][0] is the user's *primary* display (the one with
// the menu bar in standard macOS setups) regardless of which screen
// holds the active app. mainScreen would track the active window and
// shift mid-session.
static NSScreen* primaryScreen_()
{
    NSArray<NSScreen*>* screens = [NSScreen screens];
    if (screens.count == 0) return nil;
    return [screens objectAtIndex:0];
}

// Set the window's top-left to (x, y) in Win32-style coordinates. Size
// is left alone (mirrors SetWindowPos with SWP_NOSIZE). No-op if hwnd
// isn't a window we can recognise.
void macosPinWindow(void* hwnd, int x, int y)
{
    @autoreleasepool {
        NSWindow* w = windowFromHwnd_(hwnd);
        if (!w) return;
        NSScreen* scr = primaryScreen_();
        if (!scr) return;
        NSRect frame = [w frame];
        const CGFloat sh = NSHeight([scr frame]);
        // NSWindow origin.y measures from screen bottom up to the bottom
        // of the window. Win32 y measures from screen top down to the
        // top of the window. Convert.
        const CGFloat nsY = sh - (CGFloat)y - frame.size.height;
        frame.origin = NSMakePoint((CGFloat)x, nsY);
        [w setFrame:frame display:YES animate:NO];
    }
}

// Fill in (x, y, w, h) with the window's Win32-style top-left rect.
// Returns true on success. No-op + returns false if hwnd isn't a
// recognised window.
bool macosGetWindowRect(void* hwnd, int* x, int* y, int* w, int* h)
{
    @autoreleasepool {
        NSWindow* win = windowFromHwnd_(hwnd);
        if (!win) return false;
        NSScreen* scr = primaryScreen_();
        if (!scr) return false;
        NSRect frame = [win frame];
        const CGFloat sh = NSHeight([scr frame]);
        if (x) *x = (int)NSMinX(frame);
        // win32 y_top = screen_height - (nsY_origin + window_height)
        if (y) *y = (int)(sh - NSMaxY(frame));
        if (w) *w = (int)NSWidth(frame);
        if (h) *h = (int)NSHeight(frame);
        return true;
    }
}

// Primary screen pixel dimensions.
void macosGetScreenSize(int* w, int* h)
{
    @autoreleasepool {
        NSScreen* scr = primaryScreen_();
        if (!scr) {
            if (w) *w = 0;
            if (h) *h = 0;
            return;
        }
        NSRect frame = [scr frame];
        if (w) *w = (int)NSWidth(frame);
        if (h) *h = (int)NSHeight(frame);
    }
}

// Find a REAPER FX-chain window by track name. REAPER titles its
// chain windows "FX: <track-name>" on macOS. When trackName is non-empty
// we require the title to contain it (handles multiple open chains);
// otherwise the front-most "FX: …" window wins (covers the just-opened
// case after TrackFX_Show(.., 1) where the chain has just gained
// focus). Returns nil when nothing visible matches.
void* macosFindFxChainWindow(const char* trackName)
{
    @autoreleasepool {
        NSString* filter = nil;
        if (trackName && *trackName) {
            filter = [NSString stringWithUTF8String:trackName];
        }
        for (NSWindow* w in [NSApp orderedWindows]) {
            if (![w isVisible]) continue;
            NSString* title = [w title];
            if (!title || ![title hasPrefix:@"FX: "]) continue;
            if (filter && filter.length > 0
                && [title rangeOfString:filter].location == NSNotFound)
            {
                continue;
            }
            return (__bridge void*)w;
        }
        return nullptr;
    }
}

} // namespace uf8
