#pragma once
//
// The settings page for the UF8's side-car modes in ORC (docs/orc-uf8-plan.md,
// sections 2 and 6): Hue, DynaMount, OBS, and Import from REAPER. Every value
// goes into the managers (the extension's own) and from there into ORC's files
// (OrcModes); this page keeps no copy between ticks. The field names are the
// ones Rea-Sixty's tabs use, so the two read alike.

#import <Cocoa/Cocoa.h>

@interface OrcModesTab : NSObject
- (NSTabViewItem*)tabItem;
// Once a second from the window's timer: statuses, and fields nobody is typing in.
- (void)refreshInWindow:(NSWindow*)window;
@end
