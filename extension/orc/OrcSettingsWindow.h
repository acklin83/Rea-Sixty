#pragma once
//
// ORC's settings page. One window, one instance, opened from the menu-bar item.
// Objective-C++ because it is AppKit; everything it edits is the plain C++
// rme::Config that RmeManager already owns.
//
// ⛔ EVERY EDIT GOES THROUGH setConfig AND THEN saveRmeConfigIfDirty. The window
// never keeps its own copy of the settings between frames: it reads the manager
// when it draws and hands a whole Config back when something changes. A second
// copy of a value is how the V-Pot row and the EQ curve drifted apart.

#import <Cocoa/Cocoa.h>

@interface OrcSettingsWindowController : NSWindowController
+ (instancetype)shared;
- (void)present;
@end
