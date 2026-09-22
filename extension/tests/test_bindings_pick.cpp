// Press-to-pick in Settings → Bindings: which surface keys a press SELECTS,
// which it selects AND runs, and which it leaves alone (BindingsPick.h).
// Frank 2026-09-22.
#include "BindingsPick.h"

#include <cstdio>

static int g_fail = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

int main()
{
    using namespace uf8::bindings;
    // An ordinary bindable key: picked, not run.
    EXPECT(pickActionFor(ButtonId::Uf1Play, "") == PickAction::Select);
    EXPECT(pickActionFor(ButtonId::TopSoftKey3, "ssl_softkey") == PickAction::Select);
    EXPECT(pickActionFor(ButtonId::Uf1DisplaySoft2, "") == PickAction::Select);
    EXPECT(pickActionFor(ButtonId::Uc1Btn360, "mixer_toggle") == PickAction::Select);
    // Nothing to pick.
    EXPECT(pickActionFor(ButtonId::None, "") == PickAction::PassThrough);
    // Modifiers stay modifiers, by key and by what the key is bound to.
    EXPECT(pickActionFor(ButtonId::Fine, "") == PickAction::PassThrough);
    EXPECT(pickActionFor(ButtonId::Uf1Shift, "") == PickAction::PassThrough);
    EXPECT(pickActionFor(ButtonId::Uf1Play, "mod_shift") == PickAction::PassThrough);
    EXPECT(pickActionFor(ButtonId::Flip, "mod_ctrl") == PickAction::PassThrough);
    // Drawn locked in the pane: keep working.
    EXPECT(pickActionFor(ButtonId::Uf1Solo, "") == PickAction::PassThrough);
    EXPECT(pickActionFor(ButtonId::Uf1Cut, "") == PickAction::PassThrough);
    EXPECT(pickActionFor(ButtonId::Uf1Scrub, "") == PickAction::PassThrough);
    // The keys that choose what the pane edits: picked AND run, as a click is.
    for (ButtonId b : { ButtonId::Layer1, ButtonId::Layer3, ButtonId::Quick2,
                        ButtonId::VPotBank, ButtonId::SoftKey1Bank, ButtonId::SoftKey5Bank,
                        ButtonId::Uf1ArrowLeft, ButtonId::Uf1ArrowRight })
        EXPECT(pickActionFor(b, "") == PickAction::SelectAndRun);
    if (g_fail == 0) std::printf("test_bindings_pick: all passed\n");
    return g_fail == 0 ? 0 : 1;
}
