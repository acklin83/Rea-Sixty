#import "OrcDeskWindow.h"
#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>

#include "PhonesServer.h"

#include <string>

namespace ph = orc::phones;

// A link that opens a new window (Open, on the page) goes to the browser.
@interface OrcDeskDelegate : NSObject <WKUIDelegate>
@end
@implementation OrcDeskDelegate
- (WKWebView*)webView:(WKWebView*)webView createWebViewWithConfiguration:(WKWebViewConfiguration*)configuration
   forNavigationAction:(WKNavigationAction*)navigationAction windowFeatures:(WKWindowFeatures*)windowFeatures
{
    if (navigationAction.request.URL) [NSWorkspace.sharedWorkspace openURL:navigationAction.request.URL];
    return nil;
}
- (void)webView:(WKWebView*)webView runJavaScriptAlertPanelWithMessage:(NSString*)message
    initiatedByFrame:(WKFrameInfo*)frame completionHandler:(void (^)(void))completionHandler
{
    NSAlert* a = [[NSAlert alloc] init];
    a.messageText = message;
    [a runModal];
    completionHandler();
}
- (void)webView:(WKWebView*)webView runJavaScriptConfirmPanelWithMessage:(NSString*)message
    initiatedByFrame:(WKFrameInfo*)frame completionHandler:(void (^)(BOOL))completionHandler
{
    NSAlert* a = [[NSAlert alloc] init];
    a.messageText = message;
    [a addButtonWithTitle:@"OK"];
    [a addButtonWithTitle:@"Cancel"];
    completionHandler([a runModal] == NSAlertFirstButtonReturn);
}
- (void)webView:(WKWebView*)webView runJavaScriptTextInputPanelWithPrompt:(NSString*)prompt
    defaultText:(NSString*)defaultText initiatedByFrame:(WKFrameInfo*)frame
    completionHandler:(void (^)(NSString*))completionHandler
{
    NSAlert* a = [[NSAlert alloc] init];
    a.messageText = prompt;
    [a addButtonWithTitle:@"OK"];
    [a addButtonWithTitle:@"Cancel"];
    NSTextField* f = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 240, 24)];
    f.stringValue = defaultText ?: @"";
    a.accessoryView = f;
    completionHandler([a runModal] == NSAlertFirstButtonReturn ? f.stringValue : nil);
}
@end

namespace orc {

void openStudioControl()
{
    static NSWindow*        window   = nil;
    static WKWebView*       web      = nil;
    static OrcDeskDelegate* delegate = nil;

    const ph::Settings s = ph::server().settings();
    NSURL* url = [NSURL URLWithString:[NSString stringWithFormat:@"http://127.0.0.1:%d/desk", s.webPort]];
    if (!window) {
        window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1360, 860)
                                             styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                     | NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
                                               backing:NSBackingStoreBuffered defer:NO];
        window.title = @"Studio Control";
        window.releasedWhenClosed = NO;
        [window center];
        WKWebViewConfiguration* cfg = [[WKWebViewConfiguration alloc] init];
        web = [[WKWebView alloc] initWithFrame:window.contentView.bounds configuration:cfg];
        web.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        delegate = [[OrcDeskDelegate alloc] init];
        web.UIDelegate = delegate;
        [window.contentView addSubview:web];
    }
    // Loaded anew each time: the port may have changed.
    [web loadRequest:[NSURLRequest requestWithURL:url]];
    [NSApp activateIgnoringOtherApps:YES];
    [window makeKeyAndOrderFront:nil];
}

} // namespace orc
