#import "OrcSettingsWindow.h"

#include "OrcConfig.h"
#include "Palette.h"
#include "RmeManager.h"
#include "RmeState.h"
#include "RmeUf1.h"

#include <string>
#include <vector>

namespace rme  = reasixty::rme;
namespace rmeu = reasixty::rme::uf1;

namespace {

// Every target a pot or the jog wheel can carry: the control-room roles first,
// then whatever this remote can actually see, named as TotalMix names it.
// ⇨ Built from the live state, so the list is the mixer's answer rather than one
// we keep in step by hand.
struct Choice {
    std::string spec;
    std::string label;
    bool operator==(const Choice& o) const { return spec == o.spec && label == o.label; }
};

std::vector<Choice> targetChoices(const rme::State& st)
{
    std::vector<Choice> out;
    out.push_back({ "", "(nothing)" });
    const char* roles[] = { "main", "mainB", "phones1", "phones2",
                            "phones3", "phones4", "talk" };
    for (const char* r : roles) {
        const auto t = rmeu::resolveTarget(st, r);
        std::string label = r;
        if (!t.assigned)     label += "   not handed out";
        else if (!t.visible) label += "   hidden from this remote";
        else                 label += "   " + rmeu::displayName(st, t.row, t.ch);
        out.push_back({ r, label });
    }
    const char* kinds[] = { "input", "playback", "output" };
    for (int r = 0; r < rmeu::kRowCount; ++r) {
        const auto row = static_cast<rmeu::Row>(r);
        for (int ch : rmeu::visibleChannels(st, row)) {
            char spec[32];
            std::snprintf(spec, sizeof spec, "%s:%d", kinds[r], ch);
            out.push_back({ spec, std::string(spec) + "   "
                                  + rmeu::displayName(st, row, ch) });
        }
    }
    return out;
}

const char* linkWord(rme::LinkState st)
{
    switch (st) {
        case rme::LinkState::Off:      return "off";
        case rme::LinkState::PortBusy: return "port taken";
        case rme::LinkState::Waiting:  return "waiting";
        case rme::LinkState::Online:   return "online";
        case rme::LinkState::Silent:   return "gone quiet";
    }
    return "?";
}

NSString* str(const std::string& s) { return @(s.c_str()); }

NSTextField* label(NSString* text)
{
    NSTextField* f = [NSTextField labelWithString:text];
    return f;
}

NSTextField* dim(NSString* text)
{
    NSTextField* f = [NSTextField labelWithString:text];
    f.textColor = NSColor.secondaryLabelColor;
    f.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
    // A sentence in a settings page wraps; it does not push the window wider
    // than the screen.
    f.lineBreakMode = NSLineBreakByWordWrapping;
    f.preferredMaxLayoutWidth = 620.0;
    return f;
}

} // namespace

// ⛔ A SCROLL VIEW'S ORIGIN IS AT THE BOTTOM ON macOS. With the stock clip view,
// a page shorter than the window is pinned to the bottom edge and the top half
// looks empty, which is exactly how the first build came out. A flipped clip
// view puts the origin top-left, the way every other list on this platform
// behaves.
@interface OrcFlippedClipView : NSClipView
@end

@implementation OrcFlippedClipView
- (BOOL)isFlipped { return YES; }
@end

@interface OrcSettingsWindowController () <NSWindowDelegate>
@property (nonatomic, strong) NSButton*    enabledBox;
@property (nonatomic, strong) NSTextField* hostField;
@property (nonatomic, strong) NSTextField* sendField;
@property (nonatomic, strong) NSTextField* recvField;
@property (nonatomic, strong) NSTextField* linkLabel;
@property (nonatomic, strong) NSTextField* countsLabel;
@property (nonatomic, strong) NSTextField* errorLabel;
@property (nonatomic, strong) NSMutableArray<NSPopUpButton*>* potTargets;
@property (nonatomic, strong) NSMutableArray<NSPopUpButton*>* potTurns;
@property (nonatomic, strong) NSMutableArray<NSPopUpButton*>* potPushes;
@property (nonatomic, strong) NSPopUpButton* jogTarget;
@property (nonatomic, strong) NSTextField*   jogStep;
@property (nonatomic, strong) NSTextField*   knobStep;
@property (nonatomic, strong) NSMutableArray<NSPopUpButton*>* colourPops;
@property (nonatomic, strong) NSMutableArray<NSTextField*>*   wearerLabels;
@property (nonatomic, strong) NSTimer* tick;
@end

@implementation OrcSettingsWindowController {
    // ⇨ The last list we filled the target menus from. Rebuilding them on every
    // tick would move a menu under the hand while somebody is reading it, so
    // they are refilled only when the mixer's answer actually changed. Same
    // rule as the surface painter: compare what you draw, not what arrived.
    std::vector<Choice> _lastChoices;
}

+ (instancetype)shared
{
    static OrcSettingsWindowController* one = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ one = [[OrcSettingsWindowController alloc] initBuilding]; });
    return one;
}

- (instancetype)initBuilding
{
    NSWindow* w = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 760, 620)
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                            | NSWindowStyleMaskMiniaturizable
                            | NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    w.title = @"ORC Settings";
    w.contentMinSize = NSMakeSize(620, 420);
    [w center];
    if ((self = [super initWithWindow:w])) {
        w.delegate = self;
        self.potTargets   = [NSMutableArray array];
        self.potTurns     = [NSMutableArray array];
        self.potPushes    = [NSMutableArray array];
        self.colourPops   = [NSMutableArray array];
        self.wearerLabels = [NSMutableArray array];

        NSTabView* tabs = [[NSTabView alloc] initWithFrame:w.contentView.bounds];
        tabs.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        [tabs addTabViewItem:[self connectionTab]];
        [tabs addTabViewItem:[self controlsTab]];
        [tabs addTabViewItem:[self coloursTab]];
        [w.contentView addSubview:tabs];
        [self refresh];
    }
    return self;
}

// ── the three pages ──────────────────────────────────────────────────────────

// ⛔ A DOCUMENT VIEW NEEDS CONSTRAINTS, NOT A FRAME. The first build handed the
// scroll view a stack with autoresizing still on: nothing pinned it, so it
// collapsed to its origin and the whole page looked empty with one stray menu in
// the bottom corner. Pinning top, leading and trailing (and leaving the height
// free, which is what makes it scroll) is the whole fix.
- (NSView*)pageWithStack:(NSStackView*)stack
{
    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 700, 560)];
    scroll.hasVerticalScroller = YES;
    scroll.drawsBackground = NO;
    scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    scroll.contentView = [[OrcFlippedClipView alloc] init];

    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = NSLayoutAttributeLeading;
    stack.spacing = 10.0;
    stack.edgeInsets = NSEdgeInsetsMake(16, 16, 16, 16);
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.documentView = stack;
    [NSLayoutConstraint activateConstraints:@[
        [stack.topAnchor      constraintEqualToAnchor:scroll.contentView.topAnchor],
        [stack.leadingAnchor  constraintEqualToAnchor:scroll.contentView.leadingAnchor],
        [stack.trailingAnchor constraintEqualToAnchor:scroll.contentView.trailingAnchor],
    ]];
    return scroll;
}

- (NSTabViewItem*)connectionTab
{
    NSTabViewItem* item = [[NSTabViewItem alloc] initWithIdentifier:@"connection"];
    item.label = @"Connection";

    NSStackView* v = [[NSStackView alloc] init];

    self.enabledBox = [NSButton checkboxWithTitle:@"Talk to TotalMix"
                                           target:self
                                           action:@selector(enabledChanged:)];
    [v addArrangedSubview:self.enabledBox];
    // ⛔ NO ROMANS, AND NO ASSUMPTIONS ABOUT SOMEBODY ELSE'S RIG. The first
    // version told the user to take Remote 3 because 1 and 2 were "often taken
    // already (TotalReaper, stoerme)" — which is Frank's machine, not theirs.
    [v addArrangedSubview:dim(@"In TotalMix: Options, Settings, OSC. Take a free "
                              @"Remote Controller, tick In Use, and give it the "
                              @"ports below.")];

    NSGridView* grid = [NSGridView gridViewWithNumberOfColumns:2 rows:0];
    grid.columnSpacing = 10.0;
    grid.rowSpacing = 8.0;

    self.hostField = [self field:@selector(hostChanged:) width:180];
    [grid addRowWithViews:@[ label(@"Host"), self.hostField ]];

    self.sendField = [self field:@selector(portsChanged:) width:90];
    [grid addRowWithViews:@[ label(@"Send to port"), self.sendField ]];

    self.recvField = [self field:@selector(portsChanged:) width:90];
    [grid addRowWithViews:@[ label(@"Listen on port"), self.recvField ]];
    [v addArrangedSubview:grid];

    NSButton* ask = [NSButton buttonWithTitle:@"Ask TotalMix again"
                                       target:self
                                       action:@selector(askAgain:)];
    ask.toolTip = @"Missing channels are hidden in TotalMix: Options, Channel "
                  @"Layout.";
    [v addArrangedSubview:ask];

    self.linkLabel = label(@"");
    self.countsLabel = dim(@"");
    self.errorLabel = label(@"");
    self.errorLabel.textColor = NSColor.systemRedColor;
    [v addArrangedSubview:self.linkLabel];
    [v addArrangedSubview:self.countsLabel];
    [v addArrangedSubview:self.errorLabel];

    item.view = [self pageWithStack:v];
    return item;
}

- (NSTabViewItem*)controlsTab
{
    NSTabViewItem* item = [[NSTabViewItem alloc] initWithIdentifier:@"controls"];
    item.label = @"Controls";

    NSStackView* v = [[NSStackView alloc] init];
    NSGridView* grid = [NSGridView gridViewWithNumberOfColumns:4 rows:0];
    grid.columnSpacing = 10.0;
    grid.rowSpacing = 6.0;
    [grid addRowWithViews:@[ dim(@"pot"), dim(@"target"), dim(@"turn"), dim(@"push") ]];

    const char* turns[]  = { "volume", "none" };
    const char* pushes[] = { "submix", "select", "mute", "none" };

    for (int i = 0; i < rme::Config::kVpotSlots; ++i) {
        NSPopUpButton* target = [self popUp:@selector(potTargetChanged:) tag:i width:300];
        NSPopUpButton* turn   = [self popUp:@selector(potTurnChanged:) tag:i width:110];
        NSPopUpButton* push   = [self popUp:@selector(potPushChanged:) tag:i width:110];
        for (const char* t : turns)  [turn addItemWithTitle:@(t)];
        for (const char* p : pushes) [push addItemWithTitle:@(p)];
        [self.potTargets addObject:target];
        [self.potTurns addObject:turn];
        [self.potPushes addObject:push];
        [grid addRowWithViews:@[
            label([NSString stringWithFormat:@"bank %d, pot %d", i / 4 + 1, i % 4 + 1]),
            target, turn, push ]];
    }
    [v addArrangedSubview:grid];
    [v addArrangedSubview:dim(@"A role TotalMix has not handed out leaves its pot "
                              @"blank.")];

    NSGridView* jog = [NSGridView gridViewWithNumberOfColumns:2 rows:0];
    jog.columnSpacing = 10.0;
    jog.rowSpacing = 8.0;
    self.jogTarget = [self popUp:@selector(jogTargetChanged:) tag:0 width:300];
    [jog addRowWithViews:@[ label(@"Jog wheel"), self.jogTarget ]];
    self.jogStep = [self field:@selector(stepsChanged:) width:90];
    [jog addRowWithViews:@[ label(@"Jog step, dB"), self.jogStep ]];
    self.knobStep = [self field:@selector(stepsChanged:) width:90];
    [jog addRowWithViews:@[ label(@"Knob step, dB"), self.knobStep ]];
    [v addArrangedSubview:jog];

    item.view = [self pageWithStack:v];
    return item;
}

- (NSTabViewItem*)coloursTab
{
    NSTabViewItem* item = [[NSTabViewItem alloc] initWithIdentifier:@"colours"];
    item.label = @"Colours";

    NSStackView* v = [[NSStackView alloc] init];
    [v addArrangedSubview:dim(@"TotalMix numbers its channel colours. Pick the "
                              @"UF1 colour each number becomes.")];

    NSGridView* grid = [NSGridView gridViewWithNumberOfColumns:3 rows:0];
    grid.columnSpacing = 10.0;
    grid.rowSpacing = 6.0;
    [grid addRowWithViews:@[ dim(@"TotalMix"), dim(@"UF1 colour"), dim(@"worn by") ]];

    for (int i = 0; i < 9; ++i) {
        NSPopUpButton* pop = [self popUp:@selector(colourChanged:) tag:i width:260];
        // The word is the entry and a drawn swatch sits beside it: a name can be
        // searched, quoted and compared against SSL's own list. Palette.cpp is
        // the one place those names live.
        for (int p = 0; p < 16; ++p) {
            NSString* t = [NSString stringWithFormat:@"0x%02X  %s", p,
                           uf8::paletteName(static_cast<uint8_t>(p))];
            [pop addItemWithTitle:t];
            if (const auto rgb = uf8::paletteEntry(static_cast<uint8_t>(p))) {
                NSImage* sw = [NSImage imageWithSize:NSMakeSize(12, 12)
                                             flipped:NO
                                      drawingHandler:^BOOL(NSRect r) {
                    [[NSColor colorWithSRGBRed:rgb->r / 255.0
                                         green:rgb->g / 255.0
                                          blue:rgb->b / 255.0
                                         alpha:1.0] setFill];
                    NSRectFill(r);
                    return YES;
                }];
                [pop itemAtIndex:p].image = sw;
            }
        }
        [self.colourPops addObject:pop];

        NSTextField* worn = dim(@"");
        [self.wearerLabels addObject:worn];

        // ⇨ TotalMix' own word for the number, from RmeUf1::colourName. Read
        // off the mixer's colour menu, not derived from anything we paint.
        NSString* left = [NSString stringWithFormat:@"%d  %s", i, rmeu::colourName(i)];
        [grid addRowWithViews:@[ label(left), pop, worn ]];
    }
    [v addArrangedSubview:grid];
    [v addArrangedSubview:dim(@"The surface has no white, grey or yellow.")];

    item.view = [self pageWithStack:v];
    return item;
}

// A target menu carries entries like "output:12   Phones 3", and without a width
// the button grows to the longest one and shoves the columns off the page. The
// width is set here and the title truncates in the middle, which keeps both the
// address and the channel name readable.
- (NSPopUpButton*)popUp:(SEL)action tag:(NSInteger)tag width:(CGFloat)width
{
    NSPopUpButton* p = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, width, 24)
                                                  pullsDown:NO];
    p.target = self;
    p.action = action;
    p.tag = tag;
    p.translatesAutoresizingMaskIntoConstraints = NO;
    [p.widthAnchor constraintEqualToConstant:width].active = YES;
    p.cell.lineBreakMode = NSLineBreakByTruncatingMiddle;
    return p;
}

// Text fields have next to no intrinsic width when they are empty, so every one
// of them says how wide it is.
- (NSTextField*)field:(SEL)action width:(CGFloat)width
{
    NSTextField* f = [NSTextField textFieldWithString:@""];
    f.target = self;
    f.action = action;
    f.translatesAutoresizingMaskIntoConstraints = NO;
    [f.widthAnchor constraintEqualToConstant:width].active = YES;
    return f;
}

// ── showing and ticking ──────────────────────────────────────────────────────

- (void)present
{
    [self refresh];
    [NSApp activateIgnoringOtherApps:YES];
    [self showWindow:nil];
    [self.window makeKeyAndOrderFront:nil];
    if (!self.tick) {
        self.tick = [NSTimer scheduledTimerWithTimeInterval:1.0
                                                    repeats:YES
                                                      block:^(NSTimer*) { [self refresh]; }];
    }
}

- (void)windowWillClose:(NSNotification*)note
{
    // Nothing is polled while nobody is looking.
    [self.tick invalidate];
    self.tick = nil;
}

- (void)refresh
{
    auto& mgr = rme::manager();
    const auto cfg = mgr.config();
    const auto st  = mgr.snapshot();
    const auto link = mgr.link();

    self.enabledBox.state = cfg.enabled ? NSControlStateValueOn : NSControlStateValueOff;
    if (self.window.firstResponder != self.hostField.currentEditor)
        self.hostField.stringValue = str(cfg.host);
    if (self.window.firstResponder != self.sendField.currentEditor)
        self.sendField.stringValue = @(cfg.sendPort).stringValue;
    if (self.window.firstResponder != self.recvField.currentEditor)
        self.recvField.stringValue = @(cfg.recvPort).stringValue;

    self.linkLabel.stringValue =
        [NSString stringWithFormat:@"TotalMix: %s   %s", linkWord(link),
                                   mgr.status().c_str()];

    NSMutableString* counts = [NSMutableString string];
    for (auto row : { rmeu::Row::Input, rmeu::Row::Playback, rmeu::Row::Output }) {
        [counts appendFormat:@"%s %zu   ", rmeu::rowName(row),
                              rmeu::visibleChannels(st, row).size()];
    }
    self.countsLabel.stringValue = counts;

    // ⇨ Refill the target menus only when the mixer's answer changed. The model
    // arrives in pieces over about twenty seconds, so this fires a handful of
    // times at startup and then stops.
    const auto choices = targetChoices(st);
    if (choices != _lastChoices) {
        _lastChoices = choices;
        for (NSPopUpButton* p in self.potTargets) [self fill:p with:choices];
        [self fill:self.jogTarget with:choices];
    }

    for (int i = 0; i < rme::Config::kVpotSlots; ++i) {
        [self select:self.potTargets[i] spec:cfg.vpots[i].target among:choices];
        [self.potTurns[i] selectItemWithTitle:str(cfg.vpots[i].turn)];
        [self.potPushes[i] selectItemWithTitle:str(cfg.vpots[i].push)];
    }
    [self select:self.jogTarget spec:cfg.jogTarget among:choices];
    if (self.window.firstResponder != self.jogStep.currentEditor)
        self.jogStep.stringValue = [NSString stringWithFormat:@"%.2f", cfg.jogStepDb];
    if (self.window.firstResponder != self.knobStep.currentEditor)
        self.knobStep.stringValue = [NSString stringWithFormat:@"%.2f", cfg.vpotStepDb];

    std::string wearers[9];
    for (int r = 0; r < rmeu::kRowCount; ++r) {
        const auto row = static_cast<rmeu::Row>(r);
        for (int ch : rmeu::visibleChannels(st, row)) {
            const auto* c = rmeu::channelOf(st, row, ch);
            if (!c) continue;
            const int idx = c->colour < 0 ? 0 : (c->colour > 8 ? 8 : c->colour);
            if (wearers[idx].size() > 48) continue;
            if (!wearers[idx].empty()) wearers[idx] += ", ";
            wearers[idx] += rmeu::displayName(st, row, ch);
        }
    }
    for (int i = 0; i < 9; ++i) {
        const int cur = cfg.colourMap[i] < 0 ? 0 : (cfg.colourMap[i] > 15 ? 15 : cfg.colourMap[i]);
        [self.colourPops[i] selectItemAtIndex:cur];
        self.wearerLabels[i].stringValue = str(wearers[i]);
    }
}

- (void)fill:(NSPopUpButton*)pop with:(const std::vector<Choice>&)choices
{
    [pop removeAllItems];
    for (const auto& c : choices) [pop addItemWithTitle:str(c.label)];
}

- (void)select:(NSPopUpButton*)pop spec:(const std::string&)spec
         among:(const std::vector<Choice>&)choices
{
    for (std::size_t i = 0; i < choices.size(); ++i) {
        if (choices[i].spec == spec) { [pop selectItemAtIndex:(NSInteger)i]; return; }
    }
    // ⛔ A spec the live list cannot contain (the link is down, or the channel
    // is hidden) leaves the menu alone rather than rewriting the file to
    // whatever happens to sit at index 0.
    [pop selectItemAtIndex:-1];
}

// ── edits ────────────────────────────────────────────────────────────────────

- (void)apply:(void (^)(rme::Config&))mutate
{
    auto& mgr = rme::manager();
    auto cfg = mgr.config();
    mutate(cfg);
    mgr.setConfig(cfg);
    const std::string err = orc::saveRmeConfigIfDirty();
    self.errorLabel.stringValue = err.empty() ? @"" : str(err);
}

- (void)enabledChanged:(id)sender
{
    const bool on = self.enabledBox.state == NSControlStateValueOn;
    [self apply:^(rme::Config& c) { c.enabled = on; }];
}

- (void)hostChanged:(id)sender
{
    const std::string h = self.hostField.stringValue.UTF8String ?: "";
    [self apply:^(rme::Config& c) { c.host = h; }];
}

- (void)portsChanged:(id)sender
{
    const int sp = self.sendField.intValue;
    const int rp = self.recvField.intValue;
    [self apply:^(rme::Config& c) { c.sendPort = sp; c.recvPort = rp; }];
}

- (void)askAgain:(id)sender { rme::manager().refresh(); }

- (std::string)specAt:(NSInteger)index
{
    if (index < 0 || (std::size_t)index >= _lastChoices.size()) return {};
    return _lastChoices[(std::size_t)index].spec;
}

- (void)potTargetChanged:(NSPopUpButton*)sender
{
    const int i = (int)sender.tag;
    const std::string spec = [self specAt:sender.indexOfSelectedItem];
    [self apply:^(rme::Config& c) { c.vpots[i].target = spec; }];
}

- (void)potTurnChanged:(NSPopUpButton*)sender
{
    const int i = (int)sender.tag;
    const std::string v = sender.titleOfSelectedItem.UTF8String ?: "";
    [self apply:^(rme::Config& c) { c.vpots[i].turn = v; }];
}

- (void)potPushChanged:(NSPopUpButton*)sender
{
    const int i = (int)sender.tag;
    const std::string v = sender.titleOfSelectedItem.UTF8String ?: "";
    [self apply:^(rme::Config& c) { c.vpots[i].push = v; }];
}

- (void)jogTargetChanged:(NSPopUpButton*)sender
{
    const std::string spec = [self specAt:sender.indexOfSelectedItem];
    [self apply:^(rme::Config& c) { c.jogTarget = spec; }];
}

- (void)stepsChanged:(id)sender
{
    const double jog  = self.jogStep.doubleValue;
    const double knob = self.knobStep.doubleValue;
    [self apply:^(rme::Config& c) { c.jogStepDb = jog; c.vpotStepDb = knob; }];
}

- (void)colourChanged:(NSPopUpButton*)sender
{
    const int i = (int)sender.tag;
    const int v = (int)sender.indexOfSelectedItem;
    [self apply:^(rme::Config& c) { c.colourMap[i] = v; }];
}

@end
