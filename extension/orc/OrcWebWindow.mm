#import "OrcWebWindow.h"
#import <ServiceManagement/ServiceManagement.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <WebKit/WebKit.h>

#include "SettingsServer.h"

#include <string>

namespace set = orc::settings;

namespace {

// ⇨ OPEN AT LOGIN ONLY FROM APPLICATIONS (16.10.2026). SMAppService registers
// the copy that is running, by its path. Ticked once in a build folder, macOS
// started that build at every login from then on, whichever state it was in,
// while the released ORC sat unused in /Applications (Frank's Mac, 12. to 16.10.).
bool runningFromApplications()
{
    NSString* path = NSBundle.mainBundle.bundleURL.URLByResolvingSymlinksInPath.path;
    NSString* user = [NSHomeDirectory() stringByAppendingPathComponent:@"Applications/"];
    return [path hasPrefix:@"/Applications/"] || [path hasPrefix:user];
}

// macOS keeps the switch, and the user can also turn it off in System Settings,
// so the page shows what macOS says, not what was last clicked. Enabled, or
// waiting for the user's approval: both are "the user asked for it".
bool loginIsOn()
{
    const SMAppServiceStatus s = SMAppService.mainAppService.status;
    return s == SMAppServiceStatusEnabled || s == SMAppServiceStatusRequiresApproval;
}

void postError(const std::string& err)
{
    set::server().post([err](set::Context& c) { c.error = err; });
}

void setLoginOnMain(bool on)
{
    if (!runningFromApplications()) return;
    SMAppService* app = SMAppService.mainAppService;
    NSError* err = nil;
    const BOOL ok = on ? [app registerAndReturnError:&err] : [app unregisterAndReturnError:&err];
    // macOS may hold a new login item until the user allows it. Then the place
    // to do that opens, rather than a sentence in this window saying where it is.
    if (on && app.status == SMAppServiceStatusRequiresApproval)
        [SMAppService openSystemSettingsLoginItems];
    else if (!ok && err)
        postError(err.localizedDescription.UTF8String ?: "");
}

void chooseLogoOnMain()
{
    NSOpenPanel* p = [NSOpenPanel openPanel];
    p.allowedContentTypes = @[ UTTypePNG, UTTypeJPEG, UTTypeSVG ];
    p.allowsMultipleSelection = NO;
    if ([p runModal] != NSModalResponseOK || !p.URL) return;
    const std::string path = p.URL.path.UTF8String ?: "";
    set::server().post([path](set::Context& c) { set::logoChosen(path, c); });
}

// Cmd-C, V, X, A, Z in the page's fields. Without an Edit menu AppKit has nowhere
// to send those keys. ORC is an accessory program, so no menu bar ever shows it.
void installEditMenu()
{
    if (NSApp.mainMenu) return;
    NSMenu* bar = [[NSMenu alloc] init];
    NSMenuItem* holder = [[NSMenuItem alloc] init];
    NSMenu* edit = [[NSMenu alloc] initWithTitle:@"Edit"];
    [edit addItemWithTitle:@"Undo" action:NSSelectorFromString(@"undo:") keyEquivalent:@"z"];
    NSMenuItem* redo = [edit addItemWithTitle:@"Redo" action:NSSelectorFromString(@"redo:") keyEquivalent:@"z"];
    redo.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
    [edit addItemWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"];
    [edit addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"];
    [edit addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"];
    [edit addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];
    holder.submenu = edit;
    [bar addItem:holder];
    NSApp.mainMenu = bar;
}

} // namespace

@interface OrcWebSettings () <NSWindowDelegate, WKNavigationDelegate, WKUIDelegate>
@property (nonatomic, strong) NSWindow*  window;
@property (nonatomic, strong) WKWebView* web;
@property (nonatomic, strong) NSTimer*   tick;
@property (nonatomic, copy)   NSString*  shownPage;
@property (nonatomic, assign) BOOL       closing;
@end

@implementation OrcWebSettings

+ (instancetype)shared
{
    static OrcWebSettings* one = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ one = [[OrcWebSettings alloc] init]; });
    return one;
}

- (void)build
{
    NSRect want = NSMakeRect(0, 0, 1280, 820);
    const NSRect screen = NSScreen.mainScreen.visibleFrame;
    want.size.width  = MIN(want.size.width, screen.size.width);
    want.size.height = MIN(want.size.height, screen.size.height);
    self.window = [[NSWindow alloc] initWithContentRect:want
                                              styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                      | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    self.window.title = @"ORC Settings";
    self.window.contentMinSize = NSMakeSize(620, 420);
    self.window.releasedWhenClosed = NO;
    self.window.delegate = self;
    // The page is dark: no white flash before it has drawn.
    self.window.backgroundColor = [NSColor colorWithSRGBRed:0x0D / 255.0 green:0x0D / 255.0 blue:0x0D / 255.0 alpha:1];
    self.window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    [self.window center];
    self.web = [[WKWebView alloc] initWithFrame:self.window.contentView.bounds
                                  configuration:[[WKWebViewConfiguration alloc] init]];
    self.web.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    self.web.navigationDelegate = self;
    self.web.UIDelegate = self;
    [self.window.contentView addSubview:self.web];
    installEditMenu();
}

- (void)present { [self presentPage:nil]; }

- (void)presentPage:(NSString*)page
{
    if (!self.window) [self build];
    if (page) self.shownPage = page;
    if (self.window.isVisible) {
        if (page)
            [self.web evaluateJavaScript:[NSString stringWithFormat:@"window.orcShow&&orcShow('%@')", page]
                       completionHandler:nil];
    } else {
        set::Host host;
        host.setLogin   = [](bool on) { dispatch_async(dispatch_get_main_queue(), ^{ setLoginOnMain(on); }); };
        host.chooseLogo = [] { dispatch_async(dispatch_get_main_queue(), ^{ chooseLogoOnMain(); }); };
        NSString* res = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"settings"];
        const std::string address = set::server().start(res.UTF8String ?: "", host);
        [self pushLogin];
        if (address.empty()) return;
        NSString* url = @(address.c_str());
        if (self.shownPage) url = [url stringByAppendingFormat:@"#%@", self.shownPage];
        self.web.alphaValue = 0;   // shown when it has drawn
        [self.web loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:url]]];
        [self.tick invalidate];
        self.tick = [NSTimer scheduledTimerWithTimeInterval:1.0 repeats:YES block:^(NSTimer*) { [self pushLogin]; }];
    }
    [NSApp activateIgnoringOtherApps:YES];
    [self.window makeKeyAndOrderFront:nil];
}

- (void)toggle
{
    if (self.window.isVisible) [self.window performClose:nil];
    else                       [self present];
}

- (BOOL)isShowing { return self.window.isVisible; }

- (void)pushLogin
{
    const bool available = runningFromApplications();
    const bool on = loginIsOn();
    set::server().post([available, on](set::Context& c) {
        c.loginAvailable = available;
        c.loginOn = on;
    });
}

// A field being typed in is handed over before the window goes, and the page we
// were on is where it opens next time.
- (BOOL)windowShouldClose:(NSWindow*)sender
{
    if (self.closing) return YES;
    self.closing = YES;
    [self.web evaluateJavaScript:@"document.activeElement&&document.activeElement.blur();location.hash.slice(1)"
               completionHandler:^(id value, NSError*) {
        if ([value isKindOfClass:NSString.class] && [value length]) self.shownPage = value;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 150 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
            [self.window close];
        });
    }];
    return NO;
}

- (void)windowWillClose:(NSNotification*)note
{
    self.closing = NO;
    // Nothing is polled, and nothing listens, while nobody is looking.
    [self.tick invalidate];
    self.tick = nil;
    [self.web loadHTMLString:@"" baseURL:nil];
    set::server().stop();
}

- (void)webView:(WKWebView*)webView didFinishNavigation:(WKNavigation*)navigation { webView.alphaValue = 1; }

- (void)webViewWebContentProcessDidTerminate:(WKWebView*)webView { [webView reload]; }

// A link that opens a new window (Open, on Studio Control) goes to the browser.
- (WKWebView*)webView:(WKWebView*)webView createWebViewWithConfiguration:(WKWebViewConfiguration*)configuration
   forNavigationAction:(WKNavigationAction*)navigationAction windowFeatures:(WKWindowFeatures*)windowFeatures
{
    if (navigationAction.request.URL) [NSWorkspace.sharedWorkspace openURL:navigationAction.request.URL];
    return nil;
}

@end
