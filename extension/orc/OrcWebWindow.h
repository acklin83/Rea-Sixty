#pragma once
//
// ORC's window: the settings page (settings/settings.html, served by
// SettingsServer) in a web view. Studio Control is on its page MoreMe. Everything
// in it is the page; this file has what only the Mac can do: the window, Open at
// login, the logo's file dialog.

#import <Cocoa/Cocoa.h>

@interface OrcWebSettings : NSObject
+ (instancetype)shared;
- (void)present;
// On a page of the settings: "general", "moreme" ...
- (void)presentPage:(NSString*)page;
// 360 on the UF1: open the window, or close it when it is showing.
- (void)toggle;
@end
