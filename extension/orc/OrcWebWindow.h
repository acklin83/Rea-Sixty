#pragma once
//
// ORC's window: the settings page (settings/settings.html, served by
// SettingsServer) in a web view. Studio Control is on its page MoreMe. Everything
// in it is the page; these files have what only the platform can do: the window,
// Open at login, the logo's file dialog. OrcWebWindow.mm with WKWebView on the Mac,
// OrcWebWindow.cpp with WebView2 under Windows.

#if defined(__OBJC__)
#import <Cocoa/Cocoa.h>

@interface OrcWebSettings : NSObject
+ (instancetype)shared;
- (void)present;
// On a page of the settings: "general", "moreme" ...
- (void)presentPage:(NSString*)page;
// 360 on the UF1: open the window, or close it when it is showing.
- (void)toggle;
// For the 360 lamps.
- (BOOL)isShowing;
@end
#else
#include <string>

namespace orc::webwindow {

// On a page of the settings ("general", "moreme" ...); "" for where it was last.
void present(const std::string& page);
// 360 on the UF1: open the window, or close it when it is showing.
void toggle();
// For the 360 lamps.
bool isShowing();
// ORC is quitting: no handing over of the open field, no second look.
void closeForQuit();

} // namespace orc::webwindow
#endif
