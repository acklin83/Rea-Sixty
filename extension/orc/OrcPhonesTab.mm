#import "OrcPhonesTab.h"
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "OrcConfig.h"
#include "PhonesServer.h"

#include <string>

namespace ph = orc::phones;

namespace {

NSString* str(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }
std::string cstr(NSString* s) { return s.UTF8String ? std::string(s.UTF8String) : std::string(); }
NSTextField* label(NSString* t) { return [NSTextField labelWithString:t]; }

// A PIN is digits, four to eight of them.
bool validPin(const std::string& p)
{
    if (p.size() < 4 || p.size() > 8) return false;
    for (const char c : p) if (c < '0' || c > '9') return false;
    return true;
}

int port(NSTextField* f)
{
    const int v = f.intValue;
    return (v > 0 && v <= 65535) ? v : 0;
}

} // namespace

@interface OrcPhonesFlippedClip : NSClipView
@end
@implementation OrcPhonesFlippedClip
- (BOOL)isFlipped { return YES; }
@end

@interface OrcPhonesTab ()
@property (nonatomic, strong) NSButton*    onBox;
@property (nonatomic, strong) NSTextField* webPort;
@property (nonatomic, strong) NSTextField* sendPort;
@property (nonatomic, strong) NSTextField* recvPort;
@property (nonatomic, strong) NSTextField* pinField;
@property (nonatomic, strong) NSTextField* studio;
@property (nonatomic, strong) NSArray<NSTextField*>* names;
@property (nonatomic, strong) NSImageView* logoView;
@property (nonatomic, strong) NSTextField* address;
@property (nonatomic, strong) NSTextField* status;
@property (nonatomic, strong) NSTextField* error;
@end

@implementation OrcPhonesTab {
    std::string _shownLogo;   // the logo the preview was last drawn from
}

- (NSTextField*)field:(SEL)action width:(CGFloat)width
{
    NSTextField* f = [NSTextField textFieldWithString:@""];
    f.target = self; f.action = action;
    f.translatesAutoresizingMaskIntoConstraints = NO;
    [f.widthAnchor constraintEqualToConstant:width].active = YES;
    return f;
}

- (NSTabViewItem*)tabItem
{
    NSTabViewItem* item = [[NSTabViewItem alloc] initWithIdentifier:@"moreme"];
    item.label = @"MoreMe";

    NSStackView* v = [[NSStackView alloc] init];
    self.onBox = [NSButton checkboxWithTitle:@"Headphone mixes in the browser"
                                      target:self action:@selector(onChanged:)];
    [v addArrangedSubview:self.onBox];

    NSGridView* g = [NSGridView gridViewWithNumberOfColumns:2 rows:0];
    g.columnSpacing = 10.0;
    g.rowSpacing = 8.0;
    self.address = label(@"");
    self.address.selectable = YES;
    [g addRowWithViews:@[ label(@"Address"), self.address ]];
    self.pinField = [self field:@selector(pinChanged:) width:90];
    [g addRowWithViews:@[ label(@"PIN"), self.pinField ]];
    self.webPort = [self field:@selector(portsChanged:) width:90];
    [g addRowWithViews:@[ label(@"Page port"), self.webPort ]];
    self.sendPort = [self field:@selector(portsChanged:) width:90];
    [g addRowWithViews:@[ label(@"TotalMix send to port"), self.sendPort ]];
    self.recvPort = [self field:@selector(portsChanged:) width:90];
    [g addRowWithViews:@[ label(@"TotalMix listen on port"), self.recvPort ]];
    self.status = label(@"");
    [g addRowWithViews:@[ label(@"Status"), self.status ]];
    [v addArrangedSubview:g];

    NSGridView* look = [NSGridView gridViewWithNumberOfColumns:2 rows:0];
    look.columnSpacing = 10.0;
    look.rowSpacing = 8.0;
    self.studio = [self field:@selector(studioChanged:) width:220];
    [look addRowWithViews:@[ label(@"Studio name"), self.studio ]];
    self.logoView = [[NSImageView alloc] init];
    self.logoView.imageScaling = NSImageScaleProportionallyUpOrDown;
    self.logoView.translatesAutoresizingMaskIntoConstraints = NO;
    [self.logoView.widthAnchor constraintEqualToConstant:56].active = YES;
    [self.logoView.heightAnchor constraintEqualToConstant:56].active = YES;
    NSStackView* logoRow = [NSStackView stackViewWithViews:@[
        self.logoView,
        [NSButton buttonWithTitle:@"Choose…" target:self action:@selector(chooseLogo:)],
        [NSButton buttonWithTitle:@"Default Logo" target:self action:@selector(defaultLogo:)] ]];
    logoRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    [look addRowWithViews:@[ label(@"Logo"), logoRow ]];
    // ⇨ THE STUDIO'S OWN NAMES FOR THE FOUR HEADPHONES (Frank 30.10.2026).
    // Empty: TotalMix' name of the output, else "Phones N".
    NSMutableArray<NSTextField*>* names = [NSMutableArray array];
    for (int i = 0; i < 4; ++i) {
        NSTextField* f = [self field:@selector(namesChanged:) width:220];
        f.placeholderString = [NSString stringWithFormat:@"Phones %d", i + 1];
        [names addObject:f];
        NSGridRow* row = [look addRowWithViews:@[
            label([NSString stringWithFormat:@"Phones %d", i + 1]), f ]];
        if (i == 0) row.topPadding = 10.0;
    }
    self.names = names;
    [v setCustomSpacing:20.0 afterView:g];
    [v addArrangedSubview:look];

    self.error = label(@"");
    self.error.textColor = NSColor.systemRedColor;
    [v addArrangedSubview:self.error];

    // The same page frame as the window's other tabs (OrcSettingsWindow).
    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 700, 560)];
    scroll.hasVerticalScroller = YES;
    scroll.drawsBackground = NO;
    scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    scroll.contentView = [[OrcPhonesFlippedClip alloc] init];
    v.orientation = NSUserInterfaceLayoutOrientationVertical;
    v.alignment = NSLayoutAttributeLeading;
    v.spacing = 10.0;
    v.edgeInsets = NSEdgeInsetsMake(16, 16, 16, 16);
    v.translatesAutoresizingMaskIntoConstraints = NO;
    for (NSView* sub in v.arrangedSubviews)
        [sub setContentHuggingPriority:NSLayoutPriorityRequired - 1
                        forOrientation:NSLayoutConstraintOrientationHorizontal];
    scroll.documentView = v;
    [NSLayoutConstraint activateConstraints:@[
        [v.topAnchor      constraintEqualToAnchor:scroll.contentView.topAnchor],
        [v.leadingAnchor  constraintEqualToAnchor:scroll.contentView.leadingAnchor],
        [v.trailingAnchor constraintEqualToAnchor:scroll.contentView.trailingAnchor],
    ]];
    item.view = scroll;
    return item;
}

// ── writing ──────────────────────────────────────────────────────────────────

- (void)store:(const ph::Settings&)s pin:(const std::string&)pin
{
    std::string err;
    if (!ph::saveSettings(s)) err = "Cannot write " + ph::settingsPath();
    else if (!pin.empty() && pin != ph::loadPin() && !ph::savePin(pin))
        err = "Cannot write the PIN to " + orc::supportDir();
    ph::server().apply(s, pin);
    self.error.stringValue = str(err);
}

- (void)onChanged:(id)sender
{
    ph::Settings s = ph::server().settings();
    s.enabled = self.onBox.state == NSControlStateValueOn;
    std::string pin = ph::server().pin();
    // ⛔ A PIN AS SOON AS IT IS ON (Frank 29.10.2026): the first switch-on
    // makes one, and the page shows it.
    if (s.enabled && !validPin(pin)) pin = ph::generatePin();
    [self store:s pin:pin];
    if (self.onBox.window) [self refreshInWindow:self.onBox.window];
}

- (void)pinChanged:(id)sender
{
    const std::string want = cstr(self.pinField.stringValue);
    if (!validPin(want)) {
        self.pinField.stringValue = str(ph::server().pin());
        self.error.stringValue = @"The PIN is 4 to 8 digits.";
        return;
    }
    [self store:ph::server().settings() pin:want];
}

- (void)portsChanged:(id)sender
{
    ph::Settings s = ph::server().settings();
    const int w = port(self.webPort), sp = port(self.sendPort), rp = port(self.recvPort);
    if (!w || !sp || !rp) {
        self.error.stringValue = @"A port is a number from 1 to 65535.";
        return;
    }
    s.webPort = w; s.sendPort = sp; s.recvPort = rp;
    [self store:s pin:ph::server().pin()];
}

- (void)studioChanged:(id)sender
{
    ph::Settings s = ph::server().settings();
    s.studio = cstr(self.studio.stringValue);
    [self store:s pin:ph::server().pin()];
}

- (void)namesChanged:(id)sender
{
    ph::Settings s = ph::server().settings();
    for (int i = 0; i < 4; ++i) {
        NSString* n = [self.names[i].stringValue stringByTrimmingCharactersInSet:
                       NSCharacterSet.whitespaceAndNewlineCharacterSet];
        s.names[static_cast<size_t>(i)] = cstr(n);
    }
    [self store:s pin:ph::server().pin()];
}

- (void)chooseLogo:(id)sender
{
    NSOpenPanel* p = [NSOpenPanel openPanel];
    p.allowedContentTypes = @[ UTTypePNG, UTTypeJPEG, UTTypeSVG ];
    p.allowsMultipleSelection = NO;
    if ([p runModal] != NSModalResponseOK || !p.URL) return;
    std::string err;
    const std::string name = ph::importLogo(cstr(p.URL.path), err);
    if (name.empty()) { self.error.stringValue = str(err); return; }
    ph::Settings s = ph::server().settings();
    s.logo = name;
    _shownLogo.clear();   // same name, new picture: draw it again
    [self store:s pin:ph::server().pin()];
    if (self.onBox.window) [self refreshInWindow:self.onBox.window];
}

- (void)defaultLogo:(id)sender
{
    ph::Settings s = ph::server().settings();
    s.logo.clear();
    [self store:s pin:ph::server().pin()];
    if (self.onBox.window) [self refreshInWindow:self.onBox.window];
}

// ── showing ──────────────────────────────────────────────────────────────────

- (void)refreshInWindow:(NSWindow*)window
{
    auto typing = [window](NSControl* c) { return window.firstResponder == c.currentEditor && c.currentEditor; };
    const ph::Settings s = ph::server().settings();
    const ph::Status   st = ph::server().status();

    self.onBox.state = s.enabled ? NSControlStateValueOn : NSControlStateValueOff;
    if (!typing(self.webPort))  self.webPort.stringValue  = @(s.webPort).stringValue;
    if (!typing(self.sendPort)) self.sendPort.stringValue = @(s.sendPort).stringValue;
    if (!typing(self.recvPort)) self.recvPort.stringValue = @(s.recvPort).stringValue;
    if (!typing(self.pinField)) self.pinField.stringValue = str(ph::server().pin());
    if (!typing(self.studio))   self.studio.stringValue   = str(s.studio);
    for (int i = 0; i < 4; ++i)
        if (!typing(self.names[i])) self.names[i].stringValue = str(s.names[static_cast<size_t>(i)]);
    self.status.stringValue = str(ph::summary(st));
    const std::string addr = (s.enabled && st.listening) ? ph::pageAddress(s.webPort) : std::string();
    self.address.stringValue = addr.empty() ? @"--" : str(addr);

    const std::string logoKey = s.logo.empty() ? std::string("(mark)") : s.logo;
    if (_shownLogo != logoKey) {
        _shownLogo = logoKey;
        NSImage* img = nil;
        if (!s.logo.empty())
            img = [[NSImage alloc] initWithContentsOfFile:str(orc::supportDir() + "/" + s.logo)];
        if (!img)
            img = [[NSImage alloc] initWithContentsOfFile:
                [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"moreme/moreme-logo.svg"]];
        self.logoView.image = img;
    }
}

@end
