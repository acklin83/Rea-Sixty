#import "OrcModesTab.h"

#include "DynaMountManager.h"
#include "HueManager.h"
#include "ObsManager.h"
#include "OrcModes.h"
#include "Protocol.h"

#include <string>
#include <vector>

namespace dyna = uf8::dynamount;
namespace hue  = uf8::hue;

namespace {

NSString* str(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }
std::string cstr(NSString* s) { return s.UTF8String ? std::string(s.UTF8String) : std::string(); }

NSTextField* label(NSString* t) { return [NSTextField labelWithString:t]; }
NSTextField* dim(NSString* t)
{
    NSTextField* f = [NSTextField labelWithString:t];
    f.textColor = NSColor.secondaryLabelColor;
    f.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
    return f;
}
NSTextField* heading(NSString* t)
{
    NSTextField* f = [NSTextField labelWithString:t];
    f.font = [NSFont boldSystemFontOfSize:NSFont.systemFontSize];
    return f;
}

void saveDynaDevices()
{
    orc::modes::write("dynamount_devices", dyna::manager().serialize());
}

} // namespace

// The page scrolls from the top, as the window's other pages (their
// OrcFlippedClipView): an unflipped clip view would hang a short page at the
// bottom.
@interface OrcModesFlippedClip : NSClipView
@end
@implementation OrcModesFlippedClip
- (BOOL)isFlipped { return YES; }
@end

@interface OrcModesTab ()
@property (nonatomic, strong) NSTextField* importResult;
// OBS
@property (nonatomic, strong) NSButton*          obsOn;
@property (nonatomic, strong) NSTextField*       obsHost;
@property (nonatomic, strong) NSTextField*       obsPort;
@property (nonatomic, strong) NSSecureTextField* obsPassword;
@property (nonatomic, strong) NSTextField*       obsStatus;
// DynaMount
@property (nonatomic, strong) NSSegmentedControl* dynaFill;
@property (nonatomic, strong) NSMutableArray<NSButton*>*      dynaOn;
@property (nonatomic, strong) NSMutableArray<NSTextField*>*   dynaName;
@property (nonatomic, strong) NSMutableArray<NSTextField*>*   dynaIp;
@property (nonatomic, strong) NSMutableArray<NSPopUpButton*>* dynaColour;
@property (nonatomic, strong) NSMutableArray<NSTextField*>*   dynaStatus;
@property (nonatomic, strong) NSMutableArray<NSTextField*>*   presetName;
@property (nonatomic, strong) NSMutableArray<NSTextField*>*   presetStands;
// Hue
@property (nonatomic, strong) NSTextField*   hueStatus;
@property (nonatomic, strong) NSTextField*   hueIp;
@property (nonatomic, strong) NSSegmentedControl* hueFill;
@property (nonatomic, strong) NSMutableArray<NSButton*>*      hueOn;
@property (nonatomic, strong) NSMutableArray<NSTextField*>*   hueLabel;
@property (nonatomic, strong) NSMutableArray<NSPopUpButton*>* hueTarget;
@property (nonatomic, strong) NSMutableArray<NSPopUpButton*>* hueScene;
@property (nonatomic, strong) NSPopUpButton* huePot;
@property (nonatomic, strong) NSPopUpButton* huePotFlip;
@end

@implementation OrcModesTab {
    // The bridge's catalogue the target and scene menus were last filled from.
    // Refilled only when it changed: a menu rebuilt under the hand moves.
    std::vector<std::string> _lastTargets;
    std::vector<std::string> _lastScenes;
}

- (NSTextField*)field:(SEL)action width:(CGFloat)width tag:(NSInteger)tag
{
    NSTextField* f = [NSTextField textFieldWithString:@""];
    f.target = self; f.action = action; f.tag = tag;
    f.translatesAutoresizingMaskIntoConstraints = NO;
    [f.widthAnchor constraintEqualToConstant:width].active = YES;
    return f;
}

- (NSPopUpButton*)popUp:(SEL)action width:(CGFloat)width tag:(NSInteger)tag
{
    NSPopUpButton* p = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, width, 24) pullsDown:NO];
    p.target = self; p.action = action; p.tag = tag;
    p.translatesAutoresizingMaskIntoConstraints = NO;
    [p.widthAnchor constraintEqualToConstant:width].active = YES;
    p.cell.lineBreakMode = NSLineBreakByTruncatingTail;
    return p;
}

- (NSButton*)button:(NSString*)title action:(SEL)action tag:(NSInteger)tag
{
    NSButton* b = [NSButton buttonWithTitle:title target:self action:action];
    b.tag = tag;
    return b;
}

// ── the page ─────────────────────────────────────────────────────────────────

- (NSTabViewItem*)tabItem
{
    NSTabViewItem* item = [[NSTabViewItem alloc] initWithIdentifier:@"modes"];
    item.label = @"Modes";
    self.dynaOn = [NSMutableArray array];   self.dynaName = [NSMutableArray array];
    self.dynaIp = [NSMutableArray array];   self.dynaColour = [NSMutableArray array];
    self.dynaStatus = [NSMutableArray array];
    self.presetName = [NSMutableArray array]; self.presetStands = [NSMutableArray array];
    self.hueOn = [NSMutableArray array];    self.hueLabel = [NSMutableArray array];
    self.hueTarget = [NSMutableArray array]; self.hueScene = [NSMutableArray array];

    NSStackView* v = [[NSStackView alloc] init];

    // Import
    {
        NSStackView* row = [NSStackView stackViewWithViews:@[
            [self button:@"Import from REAPER" action:@selector(importReaper:) tag:0],
            (self.importResult = dim(@"")) ]];
        row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        [v addArrangedSubview:row];
    }

    // OBS
    [v addArrangedSubview:heading(@"OBS")];
    self.obsOn = [NSButton checkboxWithTitle:@"Talk to OBS" target:self action:@selector(obsChanged:)];
    [v addArrangedSubview:self.obsOn];
    {
        NSGridView* g = [NSGridView gridViewWithNumberOfColumns:2 rows:0];
        g.columnSpacing = 10; g.rowSpacing = 8;
        self.obsHost = [self field:@selector(obsChanged:) width:180 tag:0];
        self.obsPort = [self field:@selector(obsChanged:) width:80 tag:0];
        self.obsPassword = [NSSecureTextField textFieldWithString:@""];
        self.obsPassword.target = self; self.obsPassword.action = @selector(obsChanged:);
        self.obsPassword.translatesAutoresizingMaskIntoConstraints = NO;
        [self.obsPassword.widthAnchor constraintEqualToConstant:180].active = YES;
        self.obsStatus = label(@"");
        [g addRowWithViews:@[ label(@"Host"), self.obsHost ]];
        [g addRowWithViews:@[ label(@"Port"), self.obsPort ]];
        [g addRowWithViews:@[ label(@"Password"), self.obsPassword ]];
        [g addRowWithViews:@[ label(@"Status"), self.obsStatus ]];
        [v addArrangedSubview:g];
    }

    // DynaMount
    [v addArrangedSubview:heading(@"DynaMount")];
    self.dynaFill = [NSSegmentedControl segmentedControlWithLabels:@[ @"Fill from left", @"Fill from right" ]
                                                      trackingMode:NSSegmentSwitchTrackingSelectOne
                                                            target:self action:@selector(dynaFillChanged:)];
    [v addArrangedSubview:self.dynaFill];
    {
        NSGridView* g = [NSGridView gridViewWithNumberOfColumns:7 rows:0];
        g.columnSpacing = 8; g.rowSpacing = 6;
        [g addRowWithViews:@[ dim(@"On"), dim(@"Name"), dim(@"IP address"), dim(@"Colour"),
                              dim(@""), dim(@"Calibrate"), dim(@"Status") ]];
        int palCount = 0;
        const uf8::PaletteRgb* pal = uf8::selPaletteRgb(&palCount);
        for (int i = 0; i < dyna::kMaxMounts; ++i) {
            NSButton* on = [NSButton checkboxWithTitle:@"" target:self action:@selector(dynaChanged:)];
            on.tag = i;
            NSTextField* nm = [self field:@selector(dynaChanged:) width:120 tag:i];
            nm.placeholderString = @"mic name";
            NSTextField* ip = [self field:@selector(dynaChanged:) width:120 tag:i];
            ip.placeholderString = @"192.168.1.50";
            NSPopUpButton* col = [self popUp:@selector(dynaChanged:) width:70 tag:i];
            for (int k = 0; k < palCount; ++k) {
                [col addItemWithTitle:@""];
                const auto c = pal[k];
                col.lastItem.image = [NSImage imageWithSize:NSMakeSize(28, 12) flipped:NO
                                             drawingHandler:^BOOL(NSRect r) {
                    [[NSColor colorWithSRGBRed:c.r / 255.0 green:c.g / 255.0 blue:c.b / 255.0
                                         alpha:1.0] setFill];
                    NSRectFill(r);
                    return YES;
                }];
            }
            NSTextField* stl = label(@"");
            [g addRowWithViews:@[ on, nm, ip, col,
                                  [self button:@"Detect" action:@selector(dynaDetect:) tag:i],
                                  [self button:@"Home" action:@selector(dynaHome:) tag:i], stl ]];
            [self.dynaOn addObject:on]; [self.dynaName addObject:nm]; [self.dynaIp addObject:ip];
            [self.dynaColour addObject:col]; [self.dynaStatus addObject:stl];
        }
        [v addArrangedSubview:g];
    }
    [v addArrangedSubview:heading(@"Presets")];
    {
        NSGridView* g = [NSGridView gridViewWithNumberOfColumns:6 rows:0];
        g.columnSpacing = 8; g.rowSpacing = 6;
        [g addRowWithViews:@[ dim(@"#"), dim(@"Name"), dim(@"Stands"), dim(@""), dim(@""), dim(@"") ]];
        for (int k = 0; k < dyna::DynaMountManager::kMaxPresets; ++k) {
            NSTextField* nm = [self field:@selector(presetNameChanged:) width:150 tag:k];
            nm.placeholderString = [NSString stringWithFormat:@"Preset %d", k + 1];
            NSTextField* st = label(@"--");
            [g addRowWithViews:@[ label([NSString stringWithFormat:@"%d", k + 1]), nm, st,
                                  [self button:@"Go" action:@selector(presetGo:) tag:k],
                                  [self button:@"Store" action:@selector(presetStore:) tag:k],
                                  [self button:@"Clear" action:@selector(presetClear:) tag:k] ]];
            [self.presetName addObject:nm]; [self.presetStands addObject:st];
        }
        [v addArrangedSubview:g];
    }

    // Hue
    [v addArrangedSubview:heading(@"Hue")];
    {
        NSStackView* row = [NSStackView stackViewWithViews:@[
            [self button:@"Find bridge" action:@selector(hueFind:) tag:0],
            [self button:@"Pair" action:@selector(huePair:) tag:0],
            [self button:@"Cancel" action:@selector(hueCancel:) tag:0],
            [self button:@"Refresh lights + scenes" action:@selector(hueRefresh:) tag:0],
            [self button:@"Forget" action:@selector(hueForget:) tag:0] ]];
        row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        [v addArrangedSubview:row];
        NSGridView* g = [NSGridView gridViewWithNumberOfColumns:2 rows:0];
        g.columnSpacing = 10; g.rowSpacing = 8;
        self.hueIp = [self field:@selector(hueIpChanged:) width:160 tag:0];
        self.hueStatus = label(@"");
        [g addRowWithViews:@[ label(@"IP"), self.hueIp ]];
        [g addRowWithViews:@[ label(@"Status"), self.hueStatus ]];
        [v addArrangedSubview:g];
    }
    self.hueFill = [NSSegmentedControl segmentedControlWithLabels:@[ @"Fill from left", @"Fill from right" ]
                                                     trackingMode:NSSegmentSwitchTrackingSelectOne
                                                           target:self action:@selector(hueFillChanged:)];
    [v addArrangedSubview:self.hueFill];
    {
        NSGridView* g = [NSGridView gridViewWithNumberOfColumns:3 rows:0];
        g.columnSpacing = 8; g.rowSpacing = 6;
        [g addRowWithViews:@[ dim(@"On"), dim(@"Label"), dim(@"Target") ]];
        for (int i = 0; i < hue::kMaxSlots; ++i) {
            NSButton* on = [NSButton checkboxWithTitle:@"" target:self action:@selector(hueSlotChanged:)];
            on.tag = i;
            NSTextField* lb = [self field:@selector(hueSlotChanged:) width:110 tag:i];
            NSPopUpButton* tg = [self popUp:@selector(hueTargetChanged:) width:240 tag:i];
            [g addRowWithViews:@[ on, lb, tg ]];
            [self.hueOn addObject:on]; [self.hueLabel addObject:lb]; [self.hueTarget addObject:tg];
        }
        [v addArrangedSubview:g];
    }
    [v addArrangedSubview:heading(@"Scenes")];
    {
        NSGridView* g = [NSGridView gridViewWithNumberOfColumns:2 rows:0];
        g.columnSpacing = 8; g.rowSpacing = 6;
        for (int i = 0; i < hue::kMaxScenes; ++i) {
            NSPopUpButton* sc = [self popUp:@selector(hueSceneChanged:) width:240 tag:i];
            [g addRowWithViews:@[ label([NSString stringWithFormat:@"%d", i + 1]), sc ]];
            [self.hueScene addObject:sc];
        }
        [v addArrangedSubview:g];
    }
    {
        NSGridView* g = [NSGridView gridViewWithNumberOfColumns:2 rows:0];
        g.columnSpacing = 10; g.rowSpacing = 8;
        self.huePot     = [self popUp:@selector(hueControlsChanged:) width:140 tag:0];
        self.huePotFlip = [self popUp:@selector(hueControlsChanged:) width:140 tag:1];
        for (NSPopUpButton* p in @[ self.huePot, self.huePotFlip ])
            [p addItemsWithTitles:@[ @"Hue", @"Saturation", @"Warmth", @"Off" ]];
        [g addRowWithViews:@[ label(@"V-Pot"), self.huePot ]];
        [g addRowWithViews:@[ label(@"V-Pot + FLIP"), self.huePotFlip ]];
        [v addArrangedSubview:g];
    }

    // A scrolling page, as the window's others (pinned top, leading, trailing).
    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 700, 560)];
    scroll.hasVerticalScroller = YES;
    scroll.drawsBackground = NO;
    scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    NSClipView* clip = [[OrcModesFlippedClip alloc] init];
    scroll.contentView = clip;
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
        [v.topAnchor      constraintEqualToAnchor:clip.topAnchor],
        [v.leadingAnchor  constraintEqualToAnchor:clip.leadingAnchor],
        [v.trailingAnchor constraintEqualToAnchor:clip.trailingAnchor],
    ]];
    item.view = scroll;
    return item;
}

// ── refresh ──────────────────────────────────────────────────────────────────

- (void)refreshInWindow:(NSWindow*)window
{
    auto typing = [window](NSControl* c) { return window.firstResponder == c.currentEditor && c.currentEditor; };

    // OBS
    {
        auto& om = reasixty::obs::manager();
        const auto cfg = om.config();
        self.obsOn.state = cfg.enabled ? NSControlStateValueOn : NSControlStateValueOff;
        if (!typing(self.obsHost)) self.obsHost.stringValue = str(cfg.host);
        if (!typing(self.obsPort)) self.obsPort.stringValue = @(cfg.port).stringValue;
        if (!typing(self.obsPassword)) self.obsPassword.stringValue = str(om.password());
        std::string line = om.status();
        self.obsStatus.stringValue = str(line.empty() ? std::string("off") : line);
    }
    // DynaMount
    {
        auto& dm = dyna::manager();
        self.dynaFill.selectedSegment = dm.fillDir() == dyna::FillDir::Right ? 1 : 0;
        for (int i = 0; i < dyna::kMaxMounts; ++i) {
            const auto in = dm.info(i);
            self.dynaOn[i].state = in.enabled ? NSControlStateValueOn : NSControlStateValueOff;
            if (!typing(self.dynaName[i])) self.dynaName[i].stringValue = str(in.name);
            if (!typing(self.dynaIp[i]))   self.dynaIp[i].stringValue = str(in.ip);
            if (in.color >= 0 && in.color < self.dynaColour[i].numberOfItems)
                [self.dynaColour[i] selectItemAtIndex:in.color];
            self.dynaStatus[i].stringValue = in.online ? str(dyna::protoName(in.proto))
                                                       : (in.enabled ? @"offline" : @"--");
        }
        for (int k = 0; k < dyna::DynaMountManager::kMaxPresets; ++k) {
            const auto p = dm.preset(k);
            if (!typing(self.presetName[k])) self.presetName[k].stringValue = str(p.name);
            self.presetStands[k].stringValue = p.defined()
                ? [NSString stringWithFormat:@"%d", static_cast<int>(p.pos.size())] : @"--";
        }
    }
    // Hue
    {
        auto& hm = hue::manager();
        self.hueStatus.stringValue = str(hm.statusLine());
        if (!typing(self.hueIp)) self.hueIp.stringValue = str(hm.bridgeIp());
        self.hueFill.selectedSegment = hm.fillDir() == hue::HueManager::FillDir::Right ? 1 : 0;
        const auto lights = hm.lights();
        const auto groups = hm.groups();
        const auto scenes = hm.scenes();
        // The target menus: (none), every lamp, every room and zone with lamps.
        std::vector<std::string> targets;
        for (const auto& L : lights) targets.push_back("L" + L.id + "\t" + L.name);
        for (const auto& g : groups)
            if (!g.groupedLightId.empty())
                targets.push_back("G" + g.groupedLightId + "\t" + (g.isZone ? "Zone > " : "Room > ") + g.name);
        const bool refillTargets = targets != _lastTargets;
        _lastTargets = targets;
        std::vector<std::string> sceneKeys;
        for (const auto& sc : scenes) sceneKeys.push_back(sc.id + "\t" + sc.name);
        const bool refillScenes = sceneKeys != _lastScenes;
        _lastScenes = sceneKeys;

        for (int i = 0; i < hue::kMaxSlots; ++i) {
            const hue::SlotConfig c = hm.slot(i);
            self.hueOn[i].state = c.enabled ? NSControlStateValueOn : NSControlStateValueOff;
            if (!typing(self.hueLabel[i])) self.hueLabel[i].stringValue = str(c.label);
            NSPopUpButton* tg = self.hueTarget[i];
            if (refillTargets || tg.numberOfItems == 0) {
                [tg removeAllItems];
                [tg addItemWithTitle:@"(none)"];
                for (const auto& L : lights) [tg addItemWithTitle:str(L.name)];
                for (const auto& g : groups)
                    if (!g.groupedLightId.empty())
                        [tg addItemWithTitle:str(std::string(g.isZone ? "Zone > " : "Room > ") + g.name)];
            }
            // Select what the slot points at.
            NSInteger at = 0, k = 1;
            for (const auto& L : lights) {
                if (c.kind == hue::TargetKind::Light && c.rid == L.id) at = k;
                ++k;
            }
            for (const auto& g : groups) {
                if (g.groupedLightId.empty()) continue;
                if (c.kind == hue::TargetKind::Group && c.rid == g.groupedLightId) at = k;
                ++k;
            }
            [tg selectItemAtIndex:at];
        }
        for (int i = 0; i < hue::kMaxScenes; ++i) {
            const hue::SceneSlot s = hm.sceneSlot(i);
            NSPopUpButton* sp = self.hueScene[i];
            if (refillScenes || sp.numberOfItems == 0) {
                [sp removeAllItems];
                [sp addItemWithTitle:@"(none)"];
                for (const auto& sc : scenes) [sp addItemWithTitle:str(sc.name)];
            }
            NSInteger at = 0, k = 1;
            for (const auto& sc : scenes) { if (sc.id == s.id) at = k; ++k; }
            [sp selectItemAtIndex:at];
        }
        const hue::Controls ctl = hm.controls();
        [self.huePot selectItemAtIndex:static_cast<NSInteger>(ctl.pot)];
        [self.huePotFlip selectItemAtIndex:static_cast<NSInteger>(ctl.potFlip)];
    }
}

// ── actions ──────────────────────────────────────────────────────────────────

- (void)importReaper:(id)sender
{
    const int n = orc::modes::importFromReaper();
    self.importResult.stringValue = [NSString stringWithFormat:@"%d values imported", n];
}

- (void)obsChanged:(id)sender
{
    auto& om = reasixty::obs::manager();
    auto cfg = om.config();
    cfg.enabled = self.obsOn.state == NSControlStateValueOn;
    cfg.host    = cstr(self.obsHost.stringValue);
    const int port = self.obsPort.intValue;
    if (port > 0 && port < 65536) cfg.port = port;
    om.setConfig(cfg);
    if (sender == self.obsPassword) om.setPassword(cstr(self.obsPassword.stringValue));
}

- (void)dynaFillChanged:(id)sender
{
    const bool right = self.dynaFill.selectedSegment == 1;
    dyna::manager().setFillDir(right ? dyna::FillDir::Right : dyna::FillDir::Left);
    orc::modes::write("dynamount_fill", right ? "R" : "L");
}

- (void)dynaChanged:(NSControl*)sender
{
    const int i = static_cast<int>(sender.tag);
    if (i < 0 || i >= dyna::kMaxMounts) return;
    auto& dm = dyna::manager();
    dm.setMount(i, self.dynaOn[i].state == NSControlStateValueOn,
                cstr(self.dynaName[i].stringValue), cstr(self.dynaIp[i].stringValue),
                static_cast<int>(std::max<NSInteger>(0, self.dynaColour[i].indexOfSelectedItem)));
    saveDynaDevices();
}

- (void)dynaDetect:(NSButton*)sender { dyna::manager().requestDetect(static_cast<int>(sender.tag)); }
- (void)dynaHome:(NSButton*)sender   { dyna::manager().home(static_cast<int>(sender.tag)); }

- (void)presetNameChanged:(NSTextField*)sender
{
    dyna::manager().setPresetName(static_cast<int>(sender.tag), cstr(sender.stringValue));
}
- (void)presetGo:(NSButton*)sender    { dyna::manager().recallPreset(static_cast<int>(sender.tag)); }
- (void)presetStore:(NSButton*)sender { dyna::manager().storePreset(static_cast<int>(sender.tag)); }
- (void)presetClear:(NSButton*)sender { dyna::manager().clearPreset(static_cast<int>(sender.tag)); }

- (void)hueFind:(id)sender    { hue::manager().requestDiscovery(); }
- (void)huePair:(id)sender    { hue::manager().requestPairing(); }
- (void)hueCancel:(id)sender  { hue::manager().cancelPairing(); }
- (void)hueRefresh:(id)sender { hue::manager().requestRefresh(); }
- (void)hueForget:(id)sender  { hue::manager().forget(); }
- (void)hueIpChanged:(id)sender { hue::manager().setBridgeIp(cstr(self.hueIp.stringValue)); }

- (void)hueFillChanged:(id)sender
{
    hue::manager().setFillDir(self.hueFill.selectedSegment == 1 ? hue::HueManager::FillDir::Right
                                                                 : hue::HueManager::FillDir::Left);
}

- (void)hueSlotChanged:(NSControl*)sender
{
    const int i = static_cast<int>(sender.tag);
    if (i < 0 || i >= hue::kMaxSlots) return;
    auto& hm = hue::manager();
    hue::SlotConfig c = hm.slot(i);
    c.enabled = self.hueOn[i].state == NSControlStateValueOn;
    c.label   = cstr(self.hueLabel[i].stringValue).substr(0, hue::kLabelChars);
    hm.setSlot(i, c);
}

- (void)hueTargetChanged:(NSPopUpButton*)sender
{
    const int i = static_cast<int>(sender.tag);
    auto& hm = hue::manager();
    hue::SlotConfig c = hm.slot(i);
    const auto lights = hm.lights();
    const auto groups = hm.groups();
    NSInteger pick = sender.indexOfSelectedItem;
    if (pick <= 0) { hm.setSlot(i, hue::slotWithNothing(c)); return; }
    --pick;
    if (pick < static_cast<NSInteger>(lights.size())) {
        hm.setSlot(i, hue::slotWithLight(c, lights[static_cast<size_t>(pick)]));
        return;
    }
    pick -= static_cast<NSInteger>(lights.size());
    for (const auto& g : groups) {
        if (g.groupedLightId.empty()) continue;
        if (pick-- == 0) { hm.setSlot(i, hue::slotWithGroup(c, g)); return; }
    }
}

- (void)hueSceneChanged:(NSPopUpButton*)sender
{
    const int i = static_cast<int>(sender.tag);
    auto& hm = hue::manager();
    const auto scenes = hm.scenes();
    const NSInteger pick = sender.indexOfSelectedItem;
    if (pick <= 0) { hm.setSceneSlot(i, hue::SceneSlot{}); return; }
    if (pick - 1 < static_cast<NSInteger>(scenes.size()))
        hm.setSceneSlot(i, hue::sceneSlotWith(hm.sceneSlot(i), scenes[static_cast<size_t>(pick - 1)]));
}

- (void)hueControlsChanged:(id)sender
{
    auto& hm = hue::manager();
    hue::Controls ctl = hm.controls();
    ctl.pot     = static_cast<hue::PotRole>(std::max<NSInteger>(0, self.huePot.indexOfSelectedItem));
    ctl.potFlip = static_cast<hue::PotRole>(std::max<NSInteger>(0, self.huePotFlip.indexOfSelectedItem));
    hm.setControls(ctl);
}

@end
