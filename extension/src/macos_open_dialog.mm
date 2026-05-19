// macOS open-file dialog via NSOpenPanel — Import counterpart to
// macos_save_dialog.mm. Same rationale: SWELL's BrowseForOpenFile
// isn't reliably reachable from a REAPER extension on macOS 15.
// Talks to AppKit directly. Single-file selection only; no
// allows-multiple-selection / allows-directories etc. — bump only
// when a caller needs it.

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <string>
#include <cstring>

namespace uf8 {

std::string macosOpenDialog(const char* title,
                            const char* extension)
{
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        if (title && *title) {
            [panel setTitle:[NSString stringWithUTF8String:title]];
        }
        [panel setAllowsMultipleSelection:NO];
        [panel setCanChooseDirectories:NO];
        [panel setCanChooseFiles:YES];
        if (extension && *extension) {
            NSString* ext = [NSString stringWithUTF8String:extension];
            [panel setAllowedFileTypes:@[ext]];
            [panel setAllowsOtherFileTypes:NO];
        }

        NSModalResponse r = [panel runModal];
        if (r != NSModalResponseOK) return "";

        NSURL* url = [[panel URLs] firstObject];
        if (!url) return "";
        const char* p = [[url path] UTF8String];
        if (!p) return "";
        return std::string(p);
    }
}

} // namespace uf8
