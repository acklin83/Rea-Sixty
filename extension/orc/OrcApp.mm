#import "OrcUi.h"
#include "OrcLogo.h"
#include <cmath>
#import "OrcSettingsWindow.h"

#import <Cocoa/Cocoa.h>
#import <CoreImage/CoreImage.h>

#include "OrcConfig.h"
#include "PhonesServer.h"
#include "Surface.h"

namespace rme = reasixty::rme;

// The one surface, for the menu to ask about. Set by runApp before the loop
// starts and not touched again.
static orc::Surface* g_surface = nullptr;

@interface OrcAppDelegate : NSObject <NSApplicationDelegate, NSMenuDelegate>
@property (nonatomic, strong) NSStatusItem* item;
@property (nonatomic, strong) NSMenuItem*   uf1Line;
@property (nonatomic, strong) NSMenuItem*   uf8Line;
@property (nonatomic, strong) NSMenuItem*   mixerLine;
@property (nonatomic, strong) NSMenuItem*   moremeLine;
@property (nonatomic, strong) NSMenuItem*   moremeAddress;
@property (nonatomic, strong) NSMenuItem*   moremeQr;
@property (nonatomic, strong) NSImageView*  moremeQrView;
@property (nonatomic, copy)   NSString*     moremeQrFor;
@end

// ⇨ THE ADDRESS AS A QR CODE, for the musicians to scan (docs/orc-phones-plan.md).
// macOS draws it (CoreImage's QR generator), no library. ⛔ The address only,
// never the PIN (Frank 29.10.2026). Drawn pixel for pixel: a smoothed QR code
// does not scan.
static NSImage* qrImage(NSString* text, CGFloat side)
{
    CIFilter* f = [CIFilter filterWithName:@"CIQRCodeGenerator"];
    [f setValue:[text dataUsingEncoding:NSUTF8StringEncoding] forKey:@"inputMessage"];
    [f setValue:@"M" forKey:@"inputCorrectionLevel"];
    CIImage* code = f.outputImage;
    if (!code) return nil;
    CIContext* ctx = [CIContext contextWithOptions:nil];
    CGImageRef cg = [ctx createCGImage:code fromRect:code.extent];
    if (!cg) return nil;
    id held = CFBridgingRelease(cg);
    return [NSImage imageWithSize:NSMakeSize(side, side) flipped:NO drawingHandler:^BOOL(NSRect r) {
        [NSColor.whiteColor setFill];
        NSRectFill(r);
        CGContextRef c = NSGraphicsContext.currentContext.CGContext;
        CGContextSetInterpolationQuality(c, kCGInterpolationNone);
        CGContextSetShouldAntialias(c, false);
        const CGFloat quiet = std::floor(side * 0.08);
        CGContextDrawImage(c, CGRectInset(r, quiet, quiet), (__bridge CGImageRef)held);
        return YES;
    }];
}

@implementation OrcAppDelegate

- (instancetype)init
{
    if ((self = [super init])) {
        _item = [NSStatusBar.systemStatusBar
            statusItemWithLength:NSVariableStatusItemLength];
        // ⇨ THE WORDMARK, NOT A SYMBOL (Frank 29.09.2026: "einfach ORC als
        // Buchstaben, soll auch in der top-row so sein"). The same drawing as
        // the app icon (OrcLogo). A template image: macOS tints it for a light
        // or a dark menu bar and for the highlighted state.
        // Cap height of the menu bar's own text (Frank 29.09.: "gleich gross
        // wie UA", the input-source item): 12 pt measured 24 px against its
        // 17 px, so 12 * 17 / 24.
        const CGFloat letters = 8.5;    // points; the bar is 22
        const NSSize size = NSMakeSize(std::ceil(orc::wordmarkAspect() * letters) + 2.0, 18.0);
        NSImage* mark = [NSImage imageWithSize:size flipped:NO
                                drawingHandler:^BOOL(NSRect r) {
            CGContextRef ctx = NSGraphicsContext.currentContext.CGContext;
            orc::drawWordmark(ctx, CGRectMake(0, (r.size.height - letters) / 2.0,
                                              r.size.width, letters),
                              NSColor.blackColor.CGColor);
            return YES;
        }];
        [mark setTemplate:YES];   // `template` is a C++ keyword in this .mm
        mark.accessibilityDescription = @"ORC";
        _item.button.image = mark;

        NSMenu* menu = [[NSMenu alloc] init];
        menu.delegate = self;

        // ⇨ WHICH ORC IS THIS (Frank 30.09.2026: "woher soll ich wissen welche
        // version ich am laufen habe?"). The bundle's version, and the folder
        // when this is not the copy in Applications: a development build has
        // the same name, icon and bundle id, and macOS refuses it as a login
        // item ("Operation not permitted") where the installed one is allowed.
        NSString* ver = [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
        NSString* folder = NSBundle.mainBundle.bundlePath.stringByDeletingLastPathComponent;
        NSString* title = [NSString stringWithFormat:@"ORC %@", ver ?: @"?"];
        if (![folder isEqualToString:@"/Applications"]
            && ![folder isEqualToString:[@"~/Applications" stringByExpandingTildeInPath]])
            title = [title stringByAppendingFormat:@"   %@", folder.stringByAbbreviatingWithTildeInPath];
        NSMenuItem* versionLine = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
        versionLine.enabled = NO;
        [menu addItem:versionLine];
        [menu addItem:[NSMenuItem separatorItem]];

        // ⇨ THE FIRST TWO LINES ARE THE WHOLE POINT OF THE MENU: who holds the
        // UF1, and whether TotalMix is answering. That is the question
        // Rea-Sixty cannot answer either, and the reason a surface "does
        // nothing" nine times out of ten.
        _uf1Line   = [[NSMenuItem alloc] initWithTitle:@"UF1" action:nil keyEquivalent:@""];
        _uf8Line   = [[NSMenuItem alloc] initWithTitle:@"UF8" action:nil keyEquivalent:@""];
        _mixerLine = [[NSMenuItem alloc] initWithTitle:@"TotalMix" action:nil keyEquivalent:@""];
        _uf1Line.enabled = NO;
        _uf8Line.enabled = NO;
        _mixerLine.enabled = NO;
        [menu addItem:_uf1Line];
        [menu addItem:_uf8Line];
        [menu addItem:_mixerLine];
        // ⇨ ORC MOREME: its state, then the address and the PIN to give the
        // musicians, then the address to scan. The last two only while the page
        // is served.
        _moremeLine = [[NSMenuItem alloc] initWithTitle:@"MoreMe" action:nil keyEquivalent:@""];
        _moremeLine.enabled = NO;
        [menu addItem:_moremeLine];
        _moremeAddress = [[NSMenuItem alloc] initWithTitle:@"" action:@selector(copyMoremeAddress:)
                                             keyEquivalent:@""];
        _moremeAddress.target = self;
        _moremeAddress.toolTip = @"Copy the address";
        [menu addItem:_moremeAddress];
        _moremeQr = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
        {
            const CGFloat side = 168.0;
            NSView* box = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, side + 40.0, side + 12.0)];
            _moremeQrView = [[NSImageView alloc] initWithFrame:NSMakeRect(20.0, 6.0, side, side)];
            _moremeQrView.imageScaling = NSImageScaleNone;
            [box addSubview:_moremeQrView];
            _moremeQr.view = box;
        }
        [menu addItem:_moremeQr];
        [menu addItem:[NSMenuItem separatorItem]];
        [menu addItemWithTitle:@"Settings…"
                        action:@selector(openSettings:)
                 keyEquivalent:@","].target = self;
        // ⇨ THE MANUAL, NOT A SENTENCE IN THE WINDOW (Frank 27.09.2026: no
        // helper texts in ORC's window; 29.09.: "ein schlaues Handbuch").
        [menu addItemWithTitle:@"Manual"
                        action:@selector(openManual:)
                 keyEquivalent:@""].target = self;
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
        self.uf1Line.title = st.off ? @"UF1   off"
            : st.open
            ? [NSString stringWithFormat:@"UF1   held by ORC, %s", st.serial.c_str()]
            : [NSString stringWithFormat:@"UF1   not open: %s", st.error.c_str()];
        // The UF8, the same way. ⇨ A REAPER extension too old for the receipt
        // (a marker without "v2", OrcHandover.h) may never pick the UF8 up: it
        // tried once at its start, while ORC still held it. Say so rather than
        // leave the unit dark without a word (docs/orc-uf8-plan.md, section 5).
        const orc::Uf8Status u8 = g_surface->uf8Status();
        if (u8.off)
            self.uf8Line.title = @"UF8   off";
        else if (u8.open)
            self.uf8Line.title = [NSString stringWithFormat:@"UF8   held by ORC, %s", u8.serial.c_str()];
        else if (orc::reaperWantsUf1() && orc::reaperMarkerVersion() == 1)
            self.uf8Line.title = @"UF8   REAPER cannot take it yet: update the REAPER extension";
        else
            self.uf8Line.title = [NSString stringWithFormat:@"UF8   not open: %s", u8.error.c_str()];
    }
    self.mixerLine.title = [NSString stringWithFormat:@"TotalMix   %s",
                                     orc::linkSummary().c_str()];

    namespace ph = orc::phones;
    const ph::Status mm = ph::server().status();
    self.moremeLine.title = [NSString stringWithFormat:@"MoreMe   %s", ph::summary(mm).c_str()];
    const std::string addr = (mm.enabled && mm.listening) ? ph::pageAddress(mm.port) : std::string();
    self.moremeAddress.hidden = addr.empty();
    self.moremeQr.hidden = addr.empty();
    if (!addr.empty()) {
        NSString* a = [NSString stringWithUTF8String:addr.c_str()];
        self.moremeAddress.title = [NSString stringWithFormat:@"%@   PIN %s", a,
                                             ph::server().pin().c_str()];
        if (![a isEqualToString:self.moremeQrFor]) {
            self.moremeQrFor = a;
            self.moremeQrView.image = qrImage(a, self.moremeQrView.frame.size.width);
        }
    }
}

- (void)copyMoremeAddress:(id)sender
{
    if (!self.moremeQrFor) return;
    [NSPasteboard.generalPasteboard clearContents];
    [NSPasteboard.generalPasteboard setString:self.moremeQrFor forType:NSPasteboardTypeString];
}

- (void)openSettings:(id)sender { [[OrcSettingsWindowController shared] present]; }

// ⇨ THE MANUAL'S ADDRESS: GitHub Pages of the releases-only repo acklin83/ORC
// (docs/orc-mac-release-plan.md, step 9), built by tools/orc_manual.py. It goes
// live with that repo; until then the link leads nowhere.
- (void)openManual:(id)sender
{
    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"https://acklin83.github.io/ORC/"]];
}

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
            // 360 on the UF1 (Surface::onEvent_): opens the settings window and
            // closes it again (Frank 01.10.2026), here on the main thread where
            // AppKit wants it.
            if (g_surface && g_surface->takeSettingsRequest())
                [[OrcSettingsWindowController shared] toggle];
        }];
        [NSApp run];
        [watch invalidate];
    }
    g_surface = nullptr;
}

} // namespace orc
