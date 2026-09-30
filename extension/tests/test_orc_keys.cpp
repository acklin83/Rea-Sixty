// ORC's transport keys: one decision for the press and the lamp (Frank
// 30.09.2026: Stop on Mono in ORC switched Mono in REAPER, and the lamp showed
// REAPER's transport). Runs the real bindings engine: orc.json with Stop on a
// TotalMix builtin in red and Play on a REAPER action, read as Rea-Sixty's
// side-car source. Stop is ORC's, press and lamp, in ORC's colours; Play stays
// the host's.

#include "Bindings.h"
#include "BindingsHost.h"
#include "Uf1SoftKeys.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

namespace bnd = uf8::bindings;

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

static std::string g_dir;
static bool g_mono = false;
static int  g_monoFires = 0;

int main()
{
    char tmpl[] = "/tmp/rs_orckeysXXXXXX";
    g_dir = mkdtemp(tmpl);
    bnd::Host h;
    h.configDir  = [] { return g_dir; };
    h.configFile = [] { return std::string("bindings.json"); };
    bnd::setHost(std::move(h));
    // A stand-in for rme_mono: the prefix is what makes a key ORC's.
    bnd::registerBuiltin("rme_test_mono", bnd::BuiltinDescriptor{
        [](bool firing, bool, int) { if (!firing) return; ++g_monoFires; g_mono = !g_mono; },
        [](int) { return g_mono; }, "mono", false });
    bnd::setBuiltinKind("rme_test_mono", bnd::BuiltinKind::Switch);
    bnd::registerModifierBuiltins(nullptr);
    bnd::load();

    // ── orc.json as ORC writes it ────────────────────────────────────────────
    const int layer = bnd::getActiveLayer();
    bnd::Binding stop = bnd::getBinding(layer, bnd::ButtonId::Uf1Stop);
    for (auto& s : stop.shortPress) s = bnd::ActionSlot{};
    stop.shortPress[0].type   = bnd::ActionType::Builtin;
    stop.shortPress[0].action = "rme_test_mono";
    stop.color[0] = 0xFF; stop.color[1] = 0x00; stop.color[2] = 0x00;
    stop.brightness = bnd::Brightness::Bright;
    stop.inactiveColor[0] = 0xFF; stop.inactiveColor[1] = 0x00; stop.inactiveColor[2] = 0x00;
    stop.inactiveBrightness = bnd::Brightness::Dim;
    bnd::setBinding(layer, bnd::ButtonId::Uf1Stop, stop);
    bnd::Binding play = bnd::getBinding(layer, bnd::ButtonId::Uf1Play);
    for (auto& s : play.shortPress) s = bnd::ActionSlot{};
    play.shortPress[0].type   = bnd::ActionType::Reaper;   // ORC's inherited factory
    play.shortPress[0].action = "1007";
    bnd::setBinding(layer, bnd::ButtonId::Uf1Play, play);
    const std::string orcJson = g_dir + "/orc.json";
    EXPECT(bnd::exportTo(orcJson));

    // Rea-Sixty's own Stop is REAPER's again: the side-car must read the FILE.
    bnd::Binding own = stop;
    for (auto& s : own.shortPress) s = bnd::ActionSlot{};
    bnd::setBinding(layer, bnd::ButtonId::Uf1Stop, own);
    bnd::setSideCarSource(orcJson);
    bnd::refreshSideCarSource(/*force*/ true);

    // ── the decision ─────────────────────────────────────────────────────────
    bnd::Binding bd;
    EXPECT(bnd::sideCarKeyBinding(bnd::ButtonId::Uf1Stop, bd));
    const bnd::ActionSlot* s = bnd::orcKeySlot(bd, 0);
    EXPECT(s && s->action == "rme_test_mono");
    EXPECT(bnd::orcKeySlot(bd, static_cast<int>(bnd::Modifier::Shift)) == nullptr);
    bnd::Binding pb;
    EXPECT(bnd::sideCarKeyBinding(bnd::ButtonId::Uf1Play, pb));
    EXPECT(bnd::orcKeySlot(pb, 0) == nullptr);                    // REAPER's
    bnd::Binding cb;
    EXPECT(!bnd::sideCarKeyBinding(bnd::ButtonId::Uf1Cycle, cb)); // not a side-car key

    // ── the press follows it ─────────────────────────────────────────────────
    EXPECT(bnd::dispatchSideCarKey(bnd::ButtonId::Uf1Stop, true));
    bnd::dispatchSideCarKey(bnd::ButtonId::Uf1Stop, false);
    EXPECT(g_monoFires == 1 && g_mono);
    EXPECT(!bnd::dispatchSideCarKey(bnd::ButtonId::Uf1Play, true));

    // ── and so does the lamp: on = red, off = red quartered ──────────────────
    EXPECT(bnd::sideCarKeyBinding(bnd::ButtonId::Uf1Stop, bd));
    s = bnd::orcKeySlot(bd, 0);
    EXPECT(s != nullptr);
    if (s) {
        EXPECT(bnd::bindingHasActiveSlotForSet(bd, 0));
        EXPECT(uf1sk::bindingLedColour(bd, *s, true) == 0xFF0000u);
        g_mono = false;
        EXPECT(!bnd::bindingHasActiveSlotForSet(bd, 0));
        EXPECT(uf1sk::bindingLedColour(bd, *s, false) == 0x3F0000u);
    }

    // ── a key nobody coloured stays ORC's white, as before 30.09. ────────────
    bnd::Binding plain;
    plain.shortPress[0].type   = bnd::ActionType::Builtin;
    plain.shortPress[0].action = "rme_test_mono";
    EXPECT(uf1sk::bindingLedColour(plain, plain.shortPress[0], true)  == 0xFFFFFFu);
    EXPECT(uf1sk::bindingLedColour(plain, plain.shortPress[0], false) == 0x3F3F3Fu);

    if (g_fail == 0) std::printf("orc_keys: all passed\n");
    return g_fail == 0 ? 0 : 1;
}
