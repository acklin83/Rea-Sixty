// Press / Toggle (Frank 2026-09-28: "wieso haben wir überhaupt mehr optionen als
// einfach press und toggle?", "mach keine fehler"). Runs the real bindings engine:
// what fires on press and release for each kind of action and each stored
// behaviour, the modifiers, the editor's two choices, and the load migration.

#include "Bindings.h"
#include "BindingsHost.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace bnd = uf8::bindings;
using bnd::Behavior;
using bnd::BuiltinKind;
using bnd::PressMode;

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

static std::string g_dir;

// Fake builtins, one per kind. Each counts the firings it gets, and the Switch
// one flips a state on every firing, the way the real toggles do.
static int  g_onceFires = 0, g_selFires = 0, g_swFires = 0, g_edgeCalls = 0, g_edgeFires = 0;
static bool g_swOn = false, g_selOn = false;
static int  g_reaperFires = 0;

static void registerFakes()
{
    bnd::registerBuiltin("t_once", bnd::BuiltinDescriptor{
        [](bool firing, bool, int) { if (firing) ++g_onceFires; }, nullptr, "once", false });
    bnd::registerBuiltin("t_switch", bnd::BuiltinDescriptor{
        [](bool firing, bool, int) { if (!firing) return; ++g_swFires; g_swOn = !g_swOn; },
        [](int) { return g_swOn; }, "switch", false });
    bnd::setBuiltinKind("t_switch", BuiltinKind::Switch);
    bnd::registerBuiltin("t_select", bnd::BuiltinDescriptor{
        [](bool firing, bool, int) { if (!firing) return; ++g_selFires; g_selOn = true; },
        [](int) { return g_selOn; }, "select", false });            // Auto -> Select
    bnd::registerBuiltin("t_edges", bnd::BuiltinDescriptor{
        [](bool firing, bool, int) { ++g_edgeCalls; if (firing) ++g_edgeFires; },
        nullptr, "edges", false });
    bnd::setBuiltinKind("t_edges", BuiltinKind::Edges);
    // What a REAPER step calls (Bindings.cpp, runStep_).
    bnd::registerBuiltin("__reaper_action__", bnd::BuiltinDescriptor{
        [](bool firing, bool, int) { if (firing) ++g_reaperFires; }, nullptr, "", false });
}

static bnd::Binding keyWith(bnd::ActionType type, const char* action, Behavior b, int param = 0)
{
    bnd::Binding bd = bnd::getBinding(bnd::getActiveLayer(), bnd::ButtonId::Fine);
    for (auto& s : bd.shortPress) s = bnd::ActionSlot{};
    bd.hasLongPress = false;
    bd.behavior = b;
    auto& p = bd.shortPress[static_cast<int>(bnd::Modifier::Plain)];
    p.type = type;
    p.action = action;
    p.param = param;
    return bd;
}

static void pressRelease(const bnd::Binding& bd)
{
    bnd::setBinding(bnd::getActiveLayer(), bnd::ButtonId::Fine, bd);
    bnd::dispatch(bnd::ButtonId::Fine, true);
    bnd::dispatch(bnd::ButtonId::Fine, false);
}

static bool shift() { return bnd::modifierHeld(bnd::Modifier::Shift); }

// Press, sample, release, sample, twice, beyond the double-click window.
static std::string modifierRun(const bnd::Binding& bd)
{
    bnd::setBinding(bnd::getActiveLayer(), bnd::ButtonId::Fine, bd);
    std::string seq;
    for (int k = 0; k < 2; ++k) {
        bnd::dispatch(bnd::ButtonId::Fine, true);  seq += shift() ? '1' : '0';
        usleep(450 * 1000);
        bnd::dispatch(bnd::ButtonId::Fine, false); seq += shift() ? '1' : '0';
        usleep(450 * 1000);
    }
    return seq;
}

int main()
{
    char tmpl[] = "/tmp/rs_pressmodeXXXXXX";
    g_dir = mkdtemp(tmpl);
    bnd::Host h;
    h.configDir  = [] { return g_dir; };
    h.configFile = [] { return std::string("bindings.json"); };
    bnd::setHost(std::move(h));
    registerFakes();
    bnd::registerModifierBuiltins(nullptr);
    bnd::load();

    const Behavior all[] = { Behavior::Momentary, Behavior::Toggle, Behavior::Hold };

    // ── kinds ────────────────────────────────────────────────────────────────
    EXPECT(bnd::builtinKind("t_once") == BuiltinKind::Once);
    EXPECT(bnd::builtinKind("t_select") == BuiltinKind::Select);   // state, unclassified
    EXPECT(bnd::builtinKind("t_switch") == BuiltinKind::Switch);
    EXPECT(bnd::builtinKind("mod_shift") == BuiltinKind::Edges);
    EXPECT(bnd::builtinKind("nobody_registered_this") == BuiltinKind::Once);

    // ── a one-shot fires once per press, whatever is stored ──────────────────
    // (Hold used to run it on the release too.)
    for (Behavior b : all) {
        g_onceFires = 0;
        pressRelease(keyWith(bnd::ActionType::Builtin, "t_once", b));
        EXPECT(g_onceFires == 1);
    }
    // ── a choice is made once per press, whatever is stored ──────────────────
    for (Behavior b : all) {
        g_selFires = 0;
        pressRelease(keyWith(bnd::ActionType::Builtin, "t_select", b));
        EXPECT(g_selFires == 1);
    }
    // ── a switch: Toggle flips per press, Press is on while held ─────────────
    for (Behavior b : { Behavior::Momentary, Behavior::Toggle }) {
        g_swOn = false;
        pressRelease(keyWith(bnd::ActionType::Builtin, "t_switch", b));
        EXPECT(g_swOn);                          // stays on after the release
        pressRelease(keyWith(bnd::ActionType::Builtin, "t_switch", b));
        EXPECT(!g_swOn);                         // the next press turns it off
    }
    {
        g_swOn = false;
        bnd::setBinding(bnd::getActiveLayer(), bnd::ButtonId::Fine,
                        keyWith(bnd::ActionType::Builtin, "t_switch", Behavior::Hold));
        bnd::dispatch(bnd::ButtonId::Fine, true);
        EXPECT(g_swOn);                          // on while held
        bnd::dispatch(bnd::ButtonId::Fine, false);
        EXPECT(!g_swOn);                         // off on the release
    }
    // ── an Edges builtin sees both edges; Hold also fires the release ────────
    for (Behavior b : all) {
        g_edgeCalls = g_edgeFires = 0;
        pressRelease(keyWith(bnd::ActionType::Builtin, "t_edges", b));
        EXPECT(g_edgeCalls == 2);
        EXPECT(g_edgeFires == (b == Behavior::Hold ? 2 : 1));
    }
    // ── a REAPER step keeps firing as before (the surface thread may not ask
    //    REAPER whether it is a toggle): Hold fires the release too ────────────
    for (Behavior b : all) {
        g_reaperFires = 0;
        pressRelease(keyWith(bnd::ActionType::Reaper, "40001", b));
        EXPECT(g_reaperFires == (b == Behavior::Hold ? 2 : 1));
    }

    // ── modifiers: the param is the choice, the behaviour field cannot hide it
    //    (Frank's FINE: Behavior Toggle with Mode Momentary held Shift) ────────
    for (Behavior b : all) {
        EXPECT(modifierRun(keyWith(bnd::ActionType::Builtin, "mod_shift", b, 0)) == "1010");
        EXPECT(modifierRun(keyWith(bnd::ActionType::Builtin, "mod_shift", b, 1)) == "1100");
    }

    // ── the editor's two choices ─────────────────────────────────────────────
    {
        auto bd = keyWith(bnd::ActionType::Builtin, "t_switch", Behavior::Momentary);
        EXPECT(bnd::pressModeOf(bd) == PressMode::Toggle);
        bnd::setPressMode(bd, PressMode::Toggle);            // Momentary stays
        EXPECT(bd.behavior == Behavior::Momentary);
        bnd::setPressMode(bd, PressMode::Press);
        EXPECT(bd.behavior == Behavior::Hold && bnd::pressModeOf(bd) == PressMode::Press);
        bnd::setPressMode(bd, PressMode::Toggle);            // Hold becomes Toggle
        EXPECT(bd.behavior == Behavior::Toggle && bnd::pressModeOf(bd) == PressMode::Toggle);

        auto md = keyWith(bnd::ActionType::Builtin, "mod_shift", Behavior::Toggle, 0);
        EXPECT(bnd::pressModeOf(md) == PressMode::Press);    // the param says held
        bnd::setPressMode(md, PressMode::Toggle);
        EXPECT(md.shortPress[0].param == 1 && md.behavior == Behavior::Toggle);
        bnd::setPressMode(md, PressMode::Press);
        EXPECT(md.shortPress[0].param == 0 && md.behavior == Behavior::Hold);
    }
    // ── where the choice is offered ──────────────────────────────────────────
    {
        const auto yes = [](const std::string&) { return true; };
        const auto no  = [](const std::string&) { return false; };
        EXPECT(!bnd::offersPressChoice(keyWith(bnd::ActionType::Builtin, "t_once", Behavior::Momentary), no));
        EXPECT(!bnd::offersPressChoice(keyWith(bnd::ActionType::Builtin, "t_select", Behavior::Toggle), no));
        EXPECT(bnd::offersPressChoice(keyWith(bnd::ActionType::Builtin, "t_switch", Behavior::Momentary), no));
        EXPECT(bnd::offersPressChoice(keyWith(bnd::ActionType::Builtin, "mod_ctrl", Behavior::Hold), no));
        EXPECT(bnd::offersPressChoice(keyWith(bnd::ActionType::Reaper, "40001", Behavior::Momentary), yes));
        EXPECT(!bnd::offersPressChoice(keyWith(bnd::ActionType::Reaper, "40001", Behavior::Momentary), no));
        // A Hold that is stored stays visible, so it can be switched back.
        EXPECT(bnd::offersPressChoice(keyWith(bnd::ActionType::Builtin, "t_once", Behavior::Hold), no));
        // Nothing bound, nothing to choose.
        EXPECT(!bnd::offersPressChoice(keyWith(bnd::ActionType::Noop, "", Behavior::Hold), no));
        // The Shift set counts too.
        auto sh = keyWith(bnd::ActionType::Builtin, "t_once", Behavior::Momentary);
        sh.shortPress[static_cast<int>(bnd::Modifier::Shift)].type = bnd::ActionType::Builtin;
        sh.shortPress[static_cast<int>(bnd::Modifier::Shift)].action = "t_switch";
        EXPECT(bnd::offersPressChoice(sh, no));
    }

    // ── load: a modifier's behaviour is made to say what its param does ──────
    {
        bnd::setBinding(bnd::getActiveLayer(), bnd::ButtonId::Fine,
                        keyWith(bnd::ActionType::Builtin, "mod_shift", Behavior::Toggle, 0));
        bnd::load();
        const auto got = bnd::getBinding(bnd::getActiveLayer(), bnd::ButtonId::Fine);
        EXPECT(got.shortPress[0].action == "mod_shift");
        EXPECT(got.behavior == Behavior::Hold && got.shortPress[0].param == 0);

        bnd::setBinding(bnd::getActiveLayer(), bnd::ButtonId::Fine,
                        keyWith(bnd::ActionType::Builtin, "mod_shift", Behavior::Momentary, 1));
        bnd::load();
        const auto got2 = bnd::getBinding(bnd::getActiveLayer(), bnd::ButtonId::Fine);
        EXPECT(got2.behavior == Behavior::Toggle && got2.shortPress[0].param == 1);
    }

    // ── SEL long press is a binding (Frank 2026-09-30) ───────────────────────
    {
        using bnd::ButtonId;
        using bnd::Modifier;
        const int P = static_cast<int>(Modifier::Plain);
        auto isSpillSeed = [&](ButtonId id) {
            const auto bd = bnd::getBinding(0, id);
            return bd.hasLongPress && bd.longPress[P].type == bnd::ActionType::Builtin
                && bd.longPress[P].action == "strip_spill";
        };
        // Factory (the first load ran on an empty dir; nothing above touched SEL).
        EXPECT(isSpillSeed(ButtonId::Uf8Select));
        EXPECT(isSpillSeed(ButtonId::Uf1Sel));

        // The UF8 strip timer fires the LONG slot; off or empty = nothing.
        auto sel = bnd::getBinding(0, ButtonId::Uf8Select);
        sel.longPress[P] = bnd::ActionSlot{};
        sel.longPress[P].type = bnd::ActionType::Builtin;
        sel.longPress[P].action = "t_once";
        bnd::setBinding(0, ButtonId::Uf8Select, sel);
        const int before = g_onceFires;
        EXPECT(bnd::fireLongPress(ButtonId::Uf8Select));
        EXPECT(g_onceFires == before + 1);
        sel.hasLongPress = false;
        bnd::setBinding(0, ButtonId::Uf8Select, sel);
        EXPECT(!bnd::fireLongPress(ButtonId::Uf8Select));
        EXPECT(g_onceFires == before + 1);

        // A pre-v50 file without a long press gets the seed on load ...
        sel.longPress[P] = bnd::ActionSlot{};
        bnd::setBinding(0, ButtonId::Uf8Select, sel);
        bnd::save();
        const std::string path = g_dir + "/bindings.json";
        auto setVersion = [&](int v) {
            std::ifstream in(path);
            std::stringstream ss; ss << in.rdbuf();
            std::string js = ss.str();
            const size_t k = js.find("\"version\"");
            EXPECT(k != std::string::npos);
            if (k == std::string::npos) return;
            size_t a = js.find(':', k) + 1;
            while (a < js.size() && js[a] == ' ') ++a;
            size_t b = a;
            while (b < js.size() && js[b] >= '0' && js[b] <= '9') ++b;
            js.replace(a, b - a, std::to_string(v));
            std::ofstream(path) << js;
        };
        setVersion(49);
        bnd::load();
        EXPECT(isSpillSeed(ButtonId::Uf8Select));

        // ... but a long press switched off in a v50 file stays off.
        sel = bnd::getBinding(0, ButtonId::Uf8Select);
        sel.hasLongPress = false;
        sel.longPress[P] = bnd::ActionSlot{};
        bnd::setBinding(0, ButtonId::Uf8Select, sel);
        bnd::save();
        bnd::load();
        EXPECT(!bnd::getBinding(0, ButtonId::Uf8Select).hasLongPress);
    }

    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("press mode ok\n");
    return 0;
}
