#import "OrcUi.h"
#import "OrcSettingsWindow.h"

#import <Cocoa/Cocoa.h>

#include "RmeManager.h"
#include "Surface.h"

namespace rme = reasixty::rme;

// The one surface, for the menu to ask about. Set by runApp before the loop
// starts and not touched again.
static orc::Surface* g_surface = nullptr;

@interface OrcAppDelegate : NSObject <NSApplicationDelegate, NSMenuDelegate>
@property (nonatomic, strong) NSStatusItem* item;
@property (nonatomic, strong) NSMenuItem*   uf1Line;
@property (nonatomic, strong) NSMenuItem*   mixerLine;
@end

@implementation OrcAppDelegate

- (instancetype)init
{
    if ((self = [super init])) {
        _item = [NSStatusBar.systemStatusBar
            statusItemWithLength:NSVariableStatusItemLength];
        NSImage* icon = [NSImage imageWithSystemSymbolName:@"slider.horizontal.3"
                                  accessibilityDescription:@"ORC"];
        if (icon) _item.button.image = icon;
        else      _item.button.title = @"ORC";

        NSMenu* menu = [[NSMenu alloc] init];
        menu.delegate = self;

        // ⇨ THE FIRST TWO LINES ARE THE WHOLE POINT OF THE MENU: who holds the
        // UF1, and whether TotalMix is answering. That is the question
        // Rea-Sixty cannot answer either, and the reason a surface "does
        // nothing" nine times out of ten.
        _uf1Line   = [[NSMenuItem alloc] initWithTitle:@"UF1" action:nil keyEquivalent:@""];
        _mixerLine = [[NSMenuItem alloc] initWithTitle:@"TotalMix" action:nil keyEquivalent:@""];
        _uf1Line.enabled = NO;
        _mixerLine.enabled = NO;
        [menu addItem:_uf1Line];
        [menu addItem:_mixerLine];
        [menu addItem:[NSMenuItem separatorItem]];
        [menu addItemWithTitle:@"Settings…"
                        action:@selector(openSettings:)
                 keyEquivalent:@","].target = self;
        [menu addItem:[NSMenuItem separatorItem]];
        [menu addItemWithTitle:@"Quit ORC"
                        action:@selector(quit:)
                 keyEquivalent:@"q"].target = self;
        _item.menu = menu;
    }
    return self;
}

- (void)menuNeedsUpdate:(NSMenu*)menu
{
    if (g_surface) {
        const orc::Status st = g_surface->status();
        // ⛔ The reason is repeated verbatim. libusb cannot tell an absent UF1
        // from one another program is holding, so ORC does not name a culprit
        // it cannot prove.
        self.uf1Line.title = st.open
            ? [NSString stringWithFormat:@"UF1   held by ORC, %s", st.serial.c_str()]
            : [NSString stringWithFormat:@"UF1   not open: %s", st.error.c_str()];
    }
    auto& mgr = rme::manager();
    const char* word = "off";
    switch (mgr.link()) {
        case rme::LinkState::Off:      word = "off";        break;
        case rme::LinkState::PortBusy: word = "port taken"; break;
        case rme::LinkState::Waiting:  word = "waiting";    break;
        case rme::LinkState::Online:   word = "online";     break;
        case rme::LinkState::Silent:   word = "gone quiet"; break;
    }
    self.mixerLine.title = [NSString stringWithFormat:@"TotalMix   %s, %s",
                                     word, mgr.status().c_str()];
}

- (void)openSettings:(id)sender { [[OrcSettingsWindowController shared] present]; }

// ⛔ stop:, not terminate:. terminate: ends the process where it stands and the
// surface thread would be cut off mid-frame with the UF1 still claimed. stop:
// makes [NSApp run] return, main() closes the device and the link, and the exit
// is the one we wrote.
- (void)quit:(id)sender
{
    [NSApp stop:nil];
    // stop: is only noticed when the next event is handled, so give it one.
    [NSApp postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                        location:NSZeroPoint
                                   modifierFlags:0
                                       timestamp:0
                                    windowNumber:0
                                         context:nil
                                         subtype:0
                                           data1:0
                                           data2:0]
             atStart:YES];
}

@end

namespace orc {

void runApp(Surface& surface, const std::function<bool()>& interrupted)
{
    g_surface = &surface;
    @autoreleasepool {
        [NSApplication sharedApplication];
        // Accessory: a menu-bar program has no Dock icon and no windows of its
        // own until somebody opens the settings. Info.plist carries LSUIElement
        // for the same reason; this line is what makes it true when ORC is run
        // straight out of the build directory, without the bundle.
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        OrcAppDelegate* delegate = [[OrcAppDelegate alloc] init];
        [NSApp setDelegate:delegate];

        NSTimer* watch = [NSTimer scheduledTimerWithTimeInterval:0.25
                                                         repeats:YES
                                                           block:^(NSTimer*) {
            if (interrupted && interrupted()) [delegate quit:nil];
        }];
        [NSApp run];
        [watch invalidate];
    }
    g_surface = nullptr;
}

} // namespace orc
