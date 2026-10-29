#pragma once
//
// The settings page for ORC MoreMe, the headphone mixer in the browser
// (docs/orc-phones-plan.md, stage 4): on/off, the page's port, the second
// TotalMix remote's ports, the PIN, the studio's name and logo, and where it
// stands. Every change goes into moreme.json / moreme.pin and to the server at
// once (orc/PhonesServer); this page keeps no copy between ticks.

#import <Cocoa/Cocoa.h>

@interface OrcPhonesTab : NSObject
- (NSTabViewItem*)tabItem;
// Once a second from the window's timer: the status, and fields nobody is typing in.
- (void)refreshInWindow:(NSWindow*)window;
@end
