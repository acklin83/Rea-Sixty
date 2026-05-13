// macOS save-file dialog via NSSavePanel.
//
// Background: SWELL's BrowseForSaveFile (used by `reasixty_exportLayer-
// ViaDialog`) is not reliably reachable from a REAPER extension on
// macOS 15 — both `rec->GetFunc("BrowseForSaveFile")` and
// `dlsym(RTLD_DEFAULT, "BrowseForSaveFile")` return null because the
// symbol isn't exported from REAPER's main binary under the hardened
// runtime / shrunk dyld scope. The "Save layer X to file…" button
// silently no-op'd as a result.
//
// This file talks to AppKit directly, side-stepping the entire
// plugin_getapi / dlsym fragility. NSSavePanel must run on the main
// thread; REAPER's UI button click is on the main thread already so
// we can call it inline. ARC is off (the call is short, manual retain/
// release keeps the build simple and matches the rest of the
// extension which compiles without -fobjc-arc).

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <string>
#include <cstring>

namespace uf8 {

// Returns the user's chosen save path or "" on cancel / failure.
// `title` is the panel's window title; `defaultName` is the suggested
// filename pre-filled in the panel; `extension` is the allowed file
// extension without the dot (e.g. "json"). Pass nullptr / "" to skip
// the corresponding hint.
std::string macosSaveDialog(const char* title,
                            const char* defaultName,
                            const char* extension)
{
    @autoreleasepool {
        NSSavePanel* panel = [NSSavePanel savePanel];
        if (title && *title) {
            [panel setTitle:[NSString stringWithUTF8String:title]];
        }
        if (defaultName && *defaultName) {
            [panel setNameFieldStringValue:
                [NSString stringWithUTF8String:defaultName]];
        }
        if (extension && *extension) {
            // setAllowedContentTypes (UTType) is the macOS 12+ API but
            // requires UniformTypeIdentifiers framework + a deployment
            // target bump. setAllowedFileTypes is deprecated in 14 but
            // still works and keeps the build matrix narrow.
            NSString* ext = [NSString stringWithUTF8String:extension];
            [panel setAllowedFileTypes:@[ext]];
            [panel setAllowsOtherFileTypes:NO];
        }
        // Treat the suggested filename's extension as canonical so the
        // panel doesn't auto-strip it (NSSavePanel sometimes hides the
        // extension if it matches the allowed types and the user has
        // "Show all filename extensions" off in Finder prefs).
        [panel setExtensionHidden:NO];
        [panel setCanCreateDirectories:YES];

        NSModalResponse r = [panel runModal];
        if (r != NSModalResponseOK) return "";

        NSURL* url = [panel URL];
        if (!url) return "";
        const char* p = [[url path] UTF8String];
        if (!p) return "";
        return std::string(p);
    }
}

} // namespace uf8
