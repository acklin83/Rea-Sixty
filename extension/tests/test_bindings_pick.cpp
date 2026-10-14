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
    EXPECT(pickActionFor(ButtonId::Uc1Btn360, "") == PickAction::Select);
    // Nothing to pick.
    EXPECT(pickActionFor(ButtonId::None, "") == PickAction::PassThrough);
    // Modifiers stay modifiers, by key and by what the key is bound to.
    EXPECT(pickActionFor(ButtonId::Fine, "") == PickAction::PassThrough);
    EXPECT(pickActionFor(ButtonId::Uf1Shift, "") == PickAction::PassThrough);
    EXPECT(pickActionFor(ButtonId::Uf1Play, "mod_shift") == PickAction::PassThrough);
    EXPECT(pickActionFor(ButtonId::Flip, "mod_ctrl") == PickAction::PassThrough);
    // The Settings key: the only way out of the window from a surface.
    EXPECT(pickActionFor(ButtonId::Uc1Btn360, "mixer_toggle") == PickAction::PassThrough);
    EXPECT(pickActionFor(ButtonId::Uf1Btn360, "mixer_toggle") == PickAction::PassThrough);
    // Drawn locked in the pane: keep working.
    EXPECT(pickActionFor(ButtonId::Uf1Solo, "") == PickAction::PassThrough);
    EXPECT(pickActionFor(ButtonId::Uf1Cut, "") == PickAction::PassThrough);
    EXPECT(pickActionFor(ButtonId::Uf1Scrub, "") == PickAction::PassThrough);
    // The keys that choose what the pane edits: picked AND run, as a click is.
    for (ButtonId b : { ButtonId::Layer1, ButtonId::Layer3, ButtonId::Quick2,
                        ButtonId::VPotBank, ButtonId::SoftKey1Bank, ButtonId::SoftKey5Bank,
                        ButtonId::Uf1ArrowLeft, ButtonId::Uf1ArrowRight })
        EXPECT(pickActionFor(b, "") == PickAction::SelectAndRun);

    // ⇨ WHERE A BINDING TAKES THE SURFACE (forum 2026-10-14). Every builtin
    // that switches layers, with the layer it lands on.
    auto slot = [](const char* action, int param = 0) {
        ActionSlot s;
        s.type = ActionType::Builtin;
        s.action = action;
        s.param = param;
        return s;
    };
    EXPECT(layerTheSlotLeadsTo(slot("softkey_bank_1")) == 0);
    EXPECT(layerTheSlotLeadsTo(slot("softkey_bank_3")) == 0);
    EXPECT(layerTheSlotLeadsTo(slot("softkey_bank_4")) == 1);
    EXPECT(layerTheSlotLeadsTo(slot("softkey_bank_9")) == 2);
    EXPECT(layerTheSlotLeadsTo(slot("softkey_bank_select", 2)) == -1);
    // Sets by number: 1 = L1 Q3, 2-4 = L2, 5-7 = L3, 8/9 = L1 Q1/Q2.
    EXPECT(layerTheSlotLeadsTo(slot("softkey_set_engage", 1)) == 0);
    EXPECT(layerTheSlotLeadsTo(slot("softkey_set_engage", 2)) == 1);
    EXPECT(layerTheSlotLeadsTo(slot("softkey_set_engage", 4)) == 1);
    EXPECT(layerTheSlotLeadsTo(slot("softkey_set_engage", 5)) == 2);
    EXPECT(layerTheSlotLeadsTo(slot("softkey_set_engage", 8)) == 0);
    EXPECT(layerTheSlotLeadsTo(slot("softkey_set_engage", 0)) == -1);
    EXPECT(layerTheSlotLeadsTo(slot("layer_select_2")) == 1);
    EXPECT(layerTheSlotLeadsTo(slot("layer_select", 2)) == 2);
    // Sets 8 and 9 by their own builtins stay on the layer.
    EXPECT(layerTheSlotLeadsTo(slot("domain_cs")) == -1);
    EXPECT(layerTheSlotLeadsTo(slot("domain_bc")) == -1);
    EXPECT(layerTheSlotLeadsTo(slot("ssl_softkey")) == -1);
    // A later step counts too.
    {
        ActionSlot s = slot("flip");
        ActionStep st;
        st.type = ActionType::Builtin;
        st.action = "softkey_bank_7";
        s.extraSteps.push_back(st);
        EXPECT(layerTheSlotLeadsTo(s) == 2);
    }
    // A Quick that leaves the layer is picked, not run; LAYER keys still run.
    EXPECT(pickActionFor(ButtonId::Quick2, "softkey_bank_4", true) == PickAction::Select);
    EXPECT(pickActionFor(ButtonId::SoftKey3Bank, "", true) == PickAction::Select);
    EXPECT(pickActionFor(ButtonId::Quick2, "softkey_bank_2", false) == PickAction::SelectAndRun);
    EXPECT(pickActionFor(ButtonId::Layer2, "layer_select_2", true) == PickAction::SelectAndRun);

    if (g_fail == 0) std::printf("test_bindings_pick: all passed\n");
    return g_fail == 0 ? 0 : 1;
}
