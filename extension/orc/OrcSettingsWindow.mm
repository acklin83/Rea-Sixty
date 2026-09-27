#import "OrcSettingsWindow.h"

#include "Bindings.h"
#include "OrcConfig.h"
#include "Palette.h"
#include "RmeManager.h"
#include "RmeState.h"
#include "RmeUf1.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <string>
#include <vector>

namespace bnd  = uf8::bindings;
namespace rme  = reasixty::rme;
namespace rmeu = reasixty::rme::uf1;

namespace {

// Every target a pot or the jog wheel can carry: the control-room roles first,
// then whatever this remote can actually see, named as TotalMix names it.
// ⇨ Built from the live state, so the list is the mixer's answer rather than one
// we keep in step by hand.
// ⇨ WORDS, NOT SPECS (Frank 27.09.2026: "liest sich wie code", "nicht
// wiederholen"). A role reads as its name, with the channel only where TotalMix
// calls it something else; a channel reads as its TotalMix name under a heading
// for its row. The spec stays behind the entry. A heading has no spec.
struct Choice {
    std::string spec;
    std::string label;
    bool header = false;
    bool operator==(const Choice& o) const
    {
        return spec == o.spec && label == o.label && header == o.header;
    }
};

std::vector<Choice> targetChoices(const rme::State& st)
{
    std::vector<Choice> out;
    out.push_back({ "", "None" });
    struct Role { const char* spec; const char* name; };
    const Role roles[] = { { "main", "Main" }, { "mainB", "Main B" },
                           { "phones1", "Phones 1" }, { "phones2", "Phones 2" },
                           { "phones3", "Phones 3" }, { "phones4", "Phones 4" },
                           { "talk", "Talkback" } };
    for (const auto& r : roles) {
        const auto t = rmeu::resolveTarget(st, r.spec);
        std::string label = r.name;
        if (!t.assigned)     label += " (not assigned)";
        else if (!t.visible) label += " (hidden)";
        else {
            const std::string ch = rmeu::displayName(st, t.row, t.ch);
            if (!ch.empty() && ch != r.name) label += " (" + ch + ")";
        }
        out.push_back({ r.spec, label });
    }
    const char* kinds[]   = { "input", "playback", "output" };
    const char* headers[] = { "Inputs", "Playback", "Outputs" };
    for (int r = 0; r < rmeu::kRowCount; ++r) {
        const auto row = static_cast<rmeu::Row>(r);
        const auto chans = rmeu::visibleChannels(st, row);
        if (chans.empty()) continue;
        out.push_back({ "", headers[r], true });
        for (int ch : chans) {
            char spec[32];
            std::snprintf(spec, sizeof spec, "%s:%d", kinds[r], ch);
            out.push_back({ spec, rmeu::displayName(st, row, ch) });
        }
    }
    return out;
}

// A pot's push, stored as the word on the left, shown as the one on the right.
struct PushChoice { const char* value; const char* title; };
const PushChoice kPushes[] = {
    { "submix", "Submix" }, { "select", "Select" }, { "mute", "Mute" }, { "none", "Nothing" },
};

// ⇨ WHAT A KEY CAN DO IN ORC: the builtins in the RME category, which is every
// action ORC can run without REAPER. Read from the engine's registry, so a new
// rme_* builtin turns up here without anyone touching this list.
std::vector<std::string> rmeActions()
{
    std::vector<std::string> out;
    for (const auto& n : bnd::builtinNames())
        if (std::string(bnd::builtinCategory(n)) == "RME") out.push_back(n);
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        return bnd::builtinDisplayName(a) < bnd::builtinDisplayName(b);
    });
    return out;
}

// The five transport keys the side-car hands to ORC's bindings (Bindings.cpp,
// dispatchSideCarKey), in the order they sit on the surface.
struct TransportKey { bnd::ButtonId id; const char* name; };
const TransportKey kTransport[5] = {
    { bnd::ButtonId::Uf1Rwd,  "Rewind" },
    { bnd::ButtonId::Uf1Ffw,  "Forward" },
    { bnd::ButtonId::Uf1Stop, "Stop" },
    { bnd::ButtonId::Uf1Play, "Play" },
    { bnd::ButtonId::Uf1Rec,  "Record" },
};

NSString* str(const std::string& s) { return @(s.c_str()); }

// Every action ORC offers is TotalMix', so the "RME: " in front of each would
// only repeat itself down the menu.
std::string actionTitle(const std::string& builtin)
{
    std::string d = bnd::builtinDisplayName(builtin);
    const std::string prefix = "RME: ";
    if (d.rfind(prefix, 0) == 0) d.erase(0, prefix.size());
    return d;
}

NSTextField* label(NSString* text)
{
    NSTextField* f = [NSTextField labelWithString:text];
    return f;
}

// A column heading. ⛔ No explaining sentences on these pages (Frank
// 27.09.2026: "sämtliche Helfer-Texte raus"): a heading names, it does not
// instruct.
NSTextField* dim(NSString* text)
{
    NSTextField* f = [NSTextField labelWithString:text];
    f.textColor = NSColor.secondaryLabelColor;
    f.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
    return f;
}

// A group title inside a page, e.g. "Bank 1".
NSTextField* heading(NSString* text)
{
    NSTextField* f = [NSTextField labelWithString:text];
    f.font = [NSFont boldSystemFontOfSize:NSFont.systemFontSize];
    return f;
}

// A number field followed by its unit.
NSStackView* withUnit(NSView* field, NSString* unit)
{
    NSStackView* h = [NSStackView stackViewWithViews:@[ field, [NSTextField labelWithString:unit] ]];
    h.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    h.spacing = 6.0;
    // A stack hugs loosely by default and would widen the grid it sits in.
    [h setHuggingPriority:NSLayoutPriorityRequired
           forOrientation:NSLayoutConstraintOrientationHorizontal];
    return h;
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
@property (nonatomic, strong) NSTextField* errorLabel;
@property (nonatomic, strong) NSMutableArray<NSPopUpButton*>* potTargets;
@property (nonatomic, strong) NSMutableArray<NSPopUpButton*>* potPushes;
@property (nonatomic, strong) NSPopUpButton* jogTarget;
@property (nonatomic, strong) NSTextField*   jogStep;
@property (nonatomic, strong) NSTextField*   knobStep;
@property (nonatomic, strong) NSMutableArray<NSPopUpButton*>* colourPops;
@property (nonatomic, strong) NSMutableArray<NSTextField*>*   wearerLabels;
@property (nonatomic, strong) NSPopUpButton*      skBank;
@property (nonatomic, strong) NSSegmentedControl* skHalf;
@property (nonatomic, strong) NSPopUpButton*      skKind;
@property (nonatomic, strong) NSTextField*        skName;
@property (nonatomic, strong) NSMutableArray<NSTextField*>*   skLabels;
@property (nonatomic, strong) NSMutableArray<NSPopUpButton*>* skActions;
@property (nonatomic, strong) NSMutableArray<NSTextField*>*   skKeyNumbers;
@property (nonatomic, strong) NSMutableArray<NSPopUpButton*>* trActions;
@property (nonatomic, strong) NSTimer* tick;
@end

@implementation OrcSettingsWindowController {
    // ⇨ The last list we filled the target menus from. Rebuilding them on every
    // tick would move a menu under the hand while somebody is reading it, so
    // they are refilled only when the mixer's answer actually changed. Same
    // rule as the surface painter: compare what you draw, not what arrived.
    std::vector<Choice> _lastChoices;
    // The action menus' entries after "(none)", filled once the surface has
    // registered its builtins (it starts after this window may be built).
    std::vector<std::string> _actions;
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
        self.potPushes    = [NSMutableArray array];
        self.colourPops   = [NSMutableArray array];
        self.wearerLabels = [NSMutableArray array];
        self.skLabels     = [NSMutableArray array];
        self.skActions    = [NSMutableArray array];
        self.skKeyNumbers = [NSMutableArray array];
        self.trActions    = [NSMutableArray array];

        NSTabView* tabs = [[NSTabView alloc] initWithFrame:w.contentView.bounds];
        tabs.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        // ⇨ The tab strip took keyboard focus when the window opened and drew a
        // focus ring around the selected tab, a second outline around the blue
        // one (Frank 27.09.2026: "Highlight nicht sauber").
        tabs.focusRingType = NSFocusRingTypeNone;
        [tabs addTabViewItem:[self connectionTab]];
        [tabs addTabViewItem:[self controlsTab]];
        [tabs addTabViewItem:[self softKeysTab]];
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
    // ⛔ A grid hugs loosely and the stack is as wide as the window, so every
    // grid grew to the page width and threw its spare room into the columns:
    // a wide gap between "key" and "label", Target and Push far apart. Each
    // row of the page keeps its own width.
    // Just below required: at required it tied with the fields' fixed widths
    // and the host field lost, squeezed to the width of a port.
    for (NSView* sub in stack.arrangedSubviews)
        [sub setContentHuggingPriority:NSLayoutPriorityRequired - 1
                        forOrientation:NSLayoutConstraintOrientationHorizontal];
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

    NSGridView* grid = [NSGridView gridViewWithNumberOfColumns:2 rows:0];
    grid.columnSpacing = 10.0;
    grid.rowSpacing = 8.0;

    self.hostField = [self field:@selector(hostChanged:) width:180];
    [grid addRowWithViews:@[ label(@"Host"), self.hostField ]];

    self.sendField = [self field:@selector(portsChanged:) width:90];
    [grid addRowWithViews:@[ label(@"Send to port"), self.sendField ]];

    self.recvField = [self field:@selector(portsChanged:) width:90];
    [grid addRowWithViews:@[ label(@"Listen on port"), self.recvField ]];

    self.linkLabel = label(@"");
    [grid addRowWithViews:@[ label(@"Status"), self.linkLabel ]];
    [v addArrangedSubview:grid];

    NSButton* ask = [NSButton buttonWithTitle:@"Ask TotalMix again"
                                       target:self
                                       action:@selector(askAgain:)];
    [v addArrangedSubview:ask];

    self.errorLabel = label(@"");
    self.errorLabel.textColor = NSColor.systemRedColor;
    [v addArrangedSubview:self.errorLabel];

    item.view = [self pageWithStack:v];
    return item;
}

- (NSTabViewItem*)controlsTab
{
    NSTabViewItem* item = [[NSTabViewItem alloc] initWithIdentifier:@"controls"];
    item.label = @"Controls";

    NSStackView* v = [[NSStackView alloc] init];
    // ⇨ No "turn" column: a pot turns the volume of its target, there is
    // nothing else to pick (Frank 27.09.2026). rme.json keeps its field.
    NSGridView* grid = [NSGridView gridViewWithNumberOfColumns:3 rows:0];
    grid.columnSpacing = 10.0;
    grid.rowSpacing = 6.0;
    [grid addRowWithViews:@[ dim(@"Pot"), dim(@"Target"), dim(@"Push") ]];

    for (int i = 0; i < rme::Config::kVpotSlots; ++i) {
        if (i % 4 == 0) {
            // ⛔ Not merged across the columns: a merged cell let the grid
            // stretch to the page width and pulled Target and Push apart.
            NSGridRow* row = [grid addRowWithViews:@[
                heading([NSString stringWithFormat:@"Bank %d", i / 4 + 1]),
                [NSGridCell emptyContentView], [NSGridCell emptyContentView] ]];
            row.topPadding = i == 0 ? 4.0 : 10.0;
        }
        NSPopUpButton* target = [self popUp:@selector(potTargetChanged:) tag:i width:300];
        NSPopUpButton* push   = [self popUp:@selector(potPushChanged:) tag:i width:130];
        for (const auto& p : kPushes) [push addItemWithTitle:@(p.title)];
        [self.potTargets addObject:target];
        [self.potPushes addObject:push];
        [grid addRowWithViews:@[
            label([NSString stringWithFormat:@"Pot %d", i % 4 + 1]), target, push ]];
    }
    [v addArrangedSubview:grid];

    NSGridView* jog = [NSGridView gridViewWithNumberOfColumns:2 rows:0];
    jog.columnSpacing = 10.0;
    jog.rowSpacing = 8.0;
    self.jogTarget = [self popUp:@selector(jogTargetChanged:) tag:0 width:300];
    [jog addRowWithViews:@[ label(@"Jog wheel"), self.jogTarget ]];
    self.jogStep = [self field:@selector(stepsChanged:) width:70];
    [jog addRowWithViews:@[ label(@"Jog step"), withUnit(self.jogStep, @"dB") ]];
    self.knobStep = [self field:@selector(stepsChanged:) width:70];
    [jog addRowWithViews:@[ label(@"Pot step"), withUnit(self.knobStep, @"dB") ]];
    [v setCustomSpacing:20.0 afterView:grid];
    [v addArrangedSubview:jog];

    item.view = [self pageWithStack:v];
    return item;
}

// ⇨ THE SOFT KEYS ARE BUILT HERE, NOT IN REA-SIXTY (Frank 25.09.2026: "die
// bänke müssen wir den user bauen lassen, mit einer werksbesetzung"). Ten banks
// of four keys, each with two halves (Bank ◄ ► or SHIFT on the surface), and the five
// transport keys. Written to orc.json through the bindings engine; Rea-Sixty's
// side-car reads the same file.
- (NSTabViewItem*)softKeysTab
{
    NSTabViewItem* item = [[NSTabViewItem alloc] initWithIdentifier:@"softkeys"];
    item.label = @"Soft Keys";

    NSStackView* v = [[NSStackView alloc] init];

    NSGridView* head = [NSGridView gridViewWithNumberOfColumns:2 rows:0];
    head.columnSpacing = 10.0;
    head.rowSpacing = 8.0;
    self.skBank = [self popUp:@selector(skBankChanged:) tag:0 width:200];
    for (int b = 0; b < bnd::kUf1RmeBankCount; ++b)
        [self.skBank addItemWithTitle:[NSString stringWithFormat:@"Bank %d", b + 1]];
    [head addRowWithViews:@[ label(@"Bank"), self.skBank ]];

    self.skHalf = [NSSegmentedControl segmentedControlWithLabels:@[ @"Keys 1-4", @"Keys 5-8" ]
                                                    trackingMode:NSSegmentSwitchTrackingSelectOne
                                                          target:self
                                                          action:@selector(skBankChanged:)];
    self.skHalf.selectedSegment = 0;
    [head addRowWithViews:@[ label(@"Half"), self.skHalf ]];

    self.skKind = [self popUp:@selector(skKindChanged:) tag:0 width:200];
    [self.skKind addItemWithTitle:@"Keys"];
    [self.skKind addItemWithTitle:@"TotalMix snapshots"];
    [self.skKind addItemWithTitle:@"TotalMix layouts"];
    [head addRowWithViews:@[ label(@"Shows"), self.skKind ]];

    self.skName = [self field:@selector(skNameChanged:) width:200];
    [head addRowWithViews:@[ label(@"Name"), self.skName ]];
    [v addArrangedSubview:head];

    NSGridView* keys = [NSGridView gridViewWithNumberOfColumns:3 rows:0];
    keys.columnSpacing = 10.0;
    keys.rowSpacing = 6.0;
    [keys addRowWithViews:@[ dim(@"Key"), dim(@"Label"), dim(@"Action") ]];
    for (int i = 0; i < 4; ++i) {
        NSTextField* num = label([NSString stringWithFormat:@"%d", i + 1]);
        NSTextField* lab = [self field:@selector(skLabelChanged:) width:140];
        lab.tag = i;
        NSPopUpButton* act = [self popUp:@selector(skActionChanged:) tag:i width:300];
        [self.skKeyNumbers addObject:num];
        [self.skLabels addObject:lab];
        [self.skActions addObject:act];
        [keys addRowWithViews:@[ num, lab, act ]];
    }
    [v addArrangedSubview:keys];

    NSButton* factory = [NSButton buttonWithTitle:@"Factory set for this bank"
                                           target:self
                                           action:@selector(skFactory:)];
    [v addArrangedSubview:factory];

    NSGridView* tr = [NSGridView gridViewWithNumberOfColumns:2 rows:0];
    tr.columnSpacing = 10.0;
    tr.rowSpacing = 6.0;
    [tr addRowWithViews:@[ dim(@"Transport"), dim(@"Action") ]];
    for (int k = 0; k < 5; ++k) {
        NSPopUpButton* act = [self popUp:@selector(trActionChanged:) tag:k width:300];
        [self.trActions addObject:act];
        [tr addRowWithViews:@[ label(@(kTransport[k].name)), act ]];
    }
    [v setCustomSpacing:20.0 afterView:factory];
    [v addArrangedSubview:tr];

    item.view = [self pageWithStack:v];
    return item;
}

- (NSTabViewItem*)coloursTab
{
    NSTabViewItem* item = [[NSTabViewItem alloc] initWithIdentifier:@"colours"];
    item.label = @"Colours";

    NSStackView* v = [[NSStackView alloc] init];

    NSGridView* grid = [NSGridView gridViewWithNumberOfColumns:3 rows:0];
    grid.columnSpacing = 10.0;
    grid.rowSpacing = 6.0;
    [grid addRowWithViews:@[ dim(@"TotalMix"), dim(@"UF1"), dim(@"Channels") ]];

    for (int i = 0; i < 9; ++i) {
        NSPopUpButton* pop = [self popUp:@selector(colourChanged:) tag:i width:200];
        // The word is the entry and a drawn swatch sits beside it: a name can be
        // searched, quoted and compared against SSL's own list. Palette.cpp is
        // the one place those names live, and they are SSL's (issue #8).
        // ⇨ Only what lights: 0x0D-0x0F show nothing on a panel, and offering
        // them gave three more entries called "off". The palette index rides on
        // the item's tag, so the menu position is free.
        for (int p = 0; p < 16; ++p) {
            const auto idx = static_cast<uint8_t>(p);
            const auto rgb = uf8::paletteSwatch(idx);
            if (p != 0 && !rgb) continue;
            NSString* t = p == 0 ? @"Off" : @(uf8::paletteName(idx));
            [pop addItemWithTitle:t];
            NSMenuItem* it = pop.lastItem;
            it.tag = p;
            if (rgb) {
                const auto c = *rgb;
                it.image = [NSImage imageWithSize:NSMakeSize(12, 12)
                                          flipped:NO
                                   drawingHandler:^BOOL(NSRect r) {
                    [[NSColor colorWithSRGBRed:c.r / 255.0
                                         green:c.g / 255.0
                                          blue:c.b / 255.0
                                         alpha:1.0] setFill];
                    NSRectFill(r);
                    [[NSColor.separatorColor colorWithAlphaComponent:0.6] setStroke];
                    NSFrameRect(r);
                    return YES;
                }];
            }
        }
        [self.colourPops addObject:pop];

        NSTextField* worn = [NSTextField labelWithString:@""];
        worn.textColor = NSColor.secondaryLabelColor;
        [self.wearerLabels addObject:worn];

        // ⇨ TotalMix' own word for the number, from RmeUf1::colourName. Read
        // off the mixer's colour menu, not derived from anything we paint. The
        // number itself is only OSC's.
        std::string name = rmeu::colourName(i);
        if (!name.empty()) name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
        [grid addRowWithViews:@[ label(str(name)), pop, worn ]];
    }
    [v addArrangedSubview:grid];

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

    std::string linkText = orc::linkSummary();
    if (!linkText.empty()) linkText[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(linkText[0])));
    self.linkLabel.stringValue = str(linkText);
    self.linkLabel.textColor = link == rme::LinkState::Online ? NSColor.labelColor
                                                              : NSColor.secondaryLabelColor;

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
        NSInteger push = -1;
        for (std::size_t p = 0; p < std::size(kPushes); ++p)
            if (cfg.vpots[i].push == kPushes[p].value) push = (NSInteger)p;
        [self.potPushes[i] selectItemAtIndex:push];
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
        if (![self.colourPops[i] selectItemWithTag:cur]) [self.colourPops[i] selectItemAtIndex:-1];
        self.wearerLabels[i].stringValue = str(wearers[i]);
    }
    [self refreshSoftKeys];
}

// ── the soft-key page ────────────────────────────────────────────────────────

- (int)skBankAbs { return bnd::kUf1RmeBankBase + (int)std::max<NSInteger>(0, self.skBank.indexOfSelectedItem); }
- (int)skHalfNow { return self.skHalf.selectedSegment == 1 ? 1 : 0; }

- (void)fillActions:(NSPopUpButton*)pop
{
    [pop removeAllItems];
    [pop addItemWithTitle:@"None"];
    for (const auto& n : _actions)
        [pop addItemWithTitle:str(actionTitle(n))];
}

- (void)selectAction:(NSPopUpButton*)pop slot:(const bnd::ActionSlot&)sp
{
    if (sp.type != bnd::ActionType::Builtin || sp.action.empty()) {
        [pop selectItemAtIndex:(sp.type == bnd::ActionType::Noop && sp.action.empty()) ? 0 : -1];
        return;
    }
    const auto it = std::find(_actions.begin(), _actions.end(), sp.action);
    // Something ORC cannot run (a REAPER action from the inherited factory)
    // shows no entry rather than pretending to be "None".
    [pop selectItemAtIndex:it == _actions.end() ? -1 : 1 + (it - _actions.begin())];
}

- (void)refreshSoftKeys
{
    if (_actions.empty()) {
        _actions = rmeActions();
        if (!_actions.empty()) {
            for (NSPopUpButton* p in self.skActions) [self fillActions:p];
            for (NSPopUpButton* p in self.trActions) [self fillActions:p];
        }
    }
    const int bank = [self skBankAbs];
    const int half = [self skHalfNow];
    const auto kind = bnd::getUf1SoftBankDynamic(bank, 0);
    const bool dyn = kind == bnd::DynamicBankKind::RmeSnapshots
                  || kind == bnd::DynamicBankKind::RmeLayouts;
    [self.skKind selectItemAtIndex:kind == bnd::DynamicBankKind::RmeSnapshots ? 1
                                 : kind == bnd::DynamicBankKind::RmeLayouts   ? 2
                                 : kind == bnd::DynamicBankKind::None         ? 0 : -1];
    if (self.window.firstResponder != self.skName.currentEditor)
        self.skName.stringValue = str(bnd::getUf1SoftBankName(bank, 0));

    for (int i = 0; i < 4; ++i) {
        self.skKeyNumbers[i].stringValue = [NSString stringWithFormat:@"%d", i + 1 + 4 * half];
        const bnd::Binding bd = bnd::getUf1SoftBankSlot(bank, i);
        const auto& sp = bd.shortPress[half];
        NSTextField* lab = self.skLabels[i];
        lab.enabled = !dyn;
        self.skActions[i].enabled = !dyn;
        if (self.window.firstResponder != lab.currentEditor) {
            const std::string own = (half == 0 && sp.label.empty()) ? bd.label : sp.label;
            lab.stringValue = dyn ? @"" : str(own);
            lab.placeholderString = dyn ? @"" : str(bnd::softKeyFallbackLabel(sp));
        }
        if (dyn) [self.skActions[i] selectItemAtIndex:-1];
        else     [self selectAction:self.skActions[i] slot:sp];
    }

    const int layer = bnd::getActiveLayer();
    for (int k = 0; k < 5; ++k) {
        const bnd::Binding bd = bnd::getBinding(layer, kTransport[k].id);
        const auto& sp = bd.shortPress[0];
        // A REAPER action there is ORC's inherited factory and does nothing in
        // ORC: it reads as "None", which is also what Rea-Sixty makes of it.
        if (sp.type == bnd::ActionType::Builtin && sp.action.rfind("rme_", 0) == 0)
            [self selectAction:self.trActions[k] slot:sp];
        else
            [self.trActions[k] selectItemAtIndex:0];
    }
}

- (std::string)actionAt:(NSInteger)index
{
    if (index <= 0 || (std::size_t)index > _actions.size()) return {};
    return _actions[(std::size_t)index - 1];
}

- (void)skBankChanged:(id)sender { [self refreshSoftKeys]; }

- (void)skKindChanged:(NSPopUpButton*)sender
{
    const int bank = [self skBankAbs];
    const NSInteger i = sender.indexOfSelectedItem;
    const auto kind = i == 1 ? bnd::DynamicBankKind::RmeSnapshots
                    : i == 2 ? bnd::DynamicBankKind::RmeLayouts
                             : bnd::DynamicBankKind::None;
    bnd::setUf1SoftBankDynamic(bank, 0, kind);
    // The kind belongs to the bank, both halves (RmeSoftKeys::rmeKind).
    bnd::setUf1SoftBankDynamic(bank, 1, bnd::DynamicBankKind::None);
    [self refreshSoftKeys];
}

- (void)skNameChanged:(id)sender
{
    const int bank = [self skBankAbs];
    const std::string n = self.skName.stringValue.UTF8String ?: "";
    // One name for the bank, whichever half is showing when it is announced.
    bnd::setUf1SoftBankName(bank, 0, n);
    bnd::setUf1SoftBankName(bank, 1, n);
}

- (void)skLabelChanged:(NSTextField*)sender
{
    const int bank = [self skBankAbs];
    const int half = [self skHalfNow];
    const int i = (int)sender.tag;
    const std::string t = sender.stringValue.UTF8String ?: "";
    bnd::Binding bd = bnd::getUf1SoftBankSlot(bank, i);
    // The first half's name is the key's own (Binding::label), the second
    // half's lives in its set (ActionSlot::label): uf1SoftBankKeyLabel.
    if (half == 0) {
        bd.label = t;
        bd.labelIsUserSet = !t.empty();
        bd.shortPress[0].label.clear();
    } else {
        bd.shortPress[1].label = t;
    }
    bnd::setUf1SoftBankSlot(bank, i, bd);
}

- (void)skActionChanged:(NSPopUpButton*)sender
{
    const int bank = [self skBankAbs];
    const int half = [self skHalfNow];
    const int i = (int)sender.tag;
    const std::string a = [self actionAt:sender.indexOfSelectedItem];
    bnd::Binding bd = bnd::getUf1SoftBankSlot(bank, i);
    auto& sp = bd.shortPress[half];
    const std::string keepLabel = sp.label;
    sp = bnd::ActionSlot{};
    if (!a.empty()) {
        sp.type   = bnd::ActionType::Builtin;
        sp.action = a;
    }
    if (half == 1) sp.label = a.empty() ? std::string() : keepLabel;
    // A label nobody typed follows the new action (the placeholder shows it).
    if (half == 0 && !bd.labelIsUserSet) bd.label.clear();
    bnd::setUf1SoftBankSlot(bank, i, bd);
    [self refreshSoftKeys];
}

- (void)skFactory:(id)sender
{
    bnd::restoreRmeSideCarBank((int)std::max<NSInteger>(0, self.skBank.indexOfSelectedItem));
    [self refreshSoftKeys];
}

- (void)trActionChanged:(NSPopUpButton*)sender
{
    const int k = (int)sender.tag;
    const std::string a = [self actionAt:sender.indexOfSelectedItem];
    const int layer = bnd::getActiveLayer();
    bnd::Binding bd = bnd::getBinding(layer, kTransport[k].id);
    bd.shortPress[0] = bnd::ActionSlot{};
    if (!a.empty()) {
        bd.shortPress[0].type   = bnd::ActionType::Builtin;
        bd.shortPress[0].action = a;
    }
    bd.label.clear();
    bnd::setBinding(layer, kTransport[k].id, bd);
    [self refreshSoftKeys];
}

// One menu item per choice, headings included, so an item's index is its
// choice's index (specAt, select:).
- (void)fill:(NSPopUpButton*)pop with:(const std::vector<Choice>&)choices
{
    [pop removeAllItems];
    NSMenu* menu = pop.menu;
    menu.autoenablesItems = NO;
    for (const auto& c : choices) {
        NSMenuItem* it;
        if (c.header) {
            if (@available(macOS 14.0, *)) {
                it = [NSMenuItem sectionHeaderWithTitle:str(c.label)];
            } else {
                it = [[NSMenuItem alloc] initWithTitle:str(c.label) action:nil keyEquivalent:@""];
            }
            it.enabled = NO;
        } else {
            it = [[NSMenuItem alloc] initWithTitle:str(c.label) action:nil keyEquivalent:@""];
        }
        [menu addItem:it];
    }
}

- (void)select:(NSPopUpButton*)pop spec:(const std::string&)spec
         among:(const std::vector<Choice>&)choices
{
    for (std::size_t i = 0; i < choices.size(); ++i) {
        if (!choices[i].header && choices[i].spec == spec) {
            [pop selectItemAtIndex:(NSInteger)i];
            return;
        }
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
    return _lastChoices[(std::size_t)index].spec;   // a heading's is empty
}

- (void)potTargetChanged:(NSPopUpButton*)sender
{
    const int i = (int)sender.tag;
    const std::string spec = [self specAt:sender.indexOfSelectedItem];
    [self apply:^(rme::Config& c) { c.vpots[i].target = spec; }];
}

- (void)potPushChanged:(NSPopUpButton*)sender
{
    const int i = (int)sender.tag;
    const NSInteger at = sender.indexOfSelectedItem;
    if (at < 0 || (std::size_t)at >= std::size(kPushes)) return;
    const std::string v = kPushes[at].value;
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
    const int v = (int)sender.selectedTag;
    [self apply:^(rme::Config& c) { c.colourMap[i] = v; }];
}

@end
