#pragma once
//
// ⛔ THE SETTINGS PAGES HUG THEIR ROWS at just below Required, so each row keeps
// its own width. That also beats a view's default resistance to being squeezed,
// and a label or button with text cut off is what came out: the MoreMe page's
// address and status, "Cho..." (Frank 03.11.2026). What has to be read whole says
// so. Pop-ups are left out: their width is set by hand, and a long channel name
// would otherwise push the row off the page.

#import <AppKit/AppKit.h>

namespace orc {

inline void keepTextWhole(NSView* v)
{
    const bool label  = [v isKindOfClass:NSTextField.class] && !((NSTextField*)v).editable;
    const bool button = [v isKindOfClass:NSButton.class] && ![v isKindOfClass:NSPopUpButton.class];
    if (label || button)
        [v setContentCompressionResistancePriority:NSLayoutPriorityRequired
                                    forOrientation:NSLayoutConstraintOrientationHorizontal];
    for (NSView* sub in v.subviews) keepTextWhole(sub);
}

} // namespace orc
