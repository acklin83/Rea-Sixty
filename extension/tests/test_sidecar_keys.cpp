// Shift + Layer 2 / 3 enter the Hue / DynaMount UF8 side-car (Frank 29.10.2026,
// docs/orc-uf8-plan.md 2.2): in the factory set on every layer, and by the v56
// upgrade only where that Shift slot is still empty. Runs the real bindings
// engine on a temp directory.

#include "Bindings.h"
#include "BindingsHost.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace bnd = uf8::bindings;
using bnd::ButtonId;

static int g_fail = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static std::string g_dir;

static std::string fileText()
{
    std::ifstream f(g_dir + "/bindings.json");
    std::stringstream ss; ss << f.rdbuf(); return ss.str();
}
static void writeText(const std::string& t)
{
    std::ofstream f(g_dir + "/bindings.json", std::ios::trunc);
    f << t;
}

static const bnd::ActionSlot& shiftOf(const bnd::Binding& b)
{
    return b.shortPress[static_cast<int>(bnd::Modifier::Shift)];
}

int main()
{
    char tmpl[] = "/tmp/rs_sidecarXXXXXX";
    g_dir = mkdtemp(tmpl);
    bnd::Host h;
    h.configDir  = [] { return g_dir; };
    h.configFile = [] { return std::string("bindings.json"); };
    bnd::setHost(std::move(h));
    bnd::load();
    bnd::load();   // the second start runs the upgrade chain, as a real one does

    // Factory: on every layer, Plain stays the layer switch, Shift is the side-car.
    for (int l = 0; l < 3; ++l) {
        const auto l1 = bnd::getBinding(l, ButtonId::Layer1);
        const auto l2 = bnd::getBinding(l, ButtonId::Layer2);
        const auto l3 = bnd::getBinding(l, ButtonId::Layer3);
        EXPECT(l2.shortPress[0].action == "layer_select_2");
        EXPECT(shiftOf(l2).action == "selection_mode_hue");
        EXPECT(shiftOf(l3).action == "selection_mode_dynamount");
        EXPECT(bnd::slotIsEmpty(shiftOf(l1)));   // TotalMix comes later
    }

    // An install from before v56: the user put something of their own on
    // Shift + Layer 2 and nothing on Shift + Layer 3.
    {
        auto l2 = bnd::getBinding(0, ButtonId::Layer2);
        auto& sh = l2.shortPress[static_cast<int>(bnd::Modifier::Shift)];
        sh.type = bnd::ActionType::Builtin; sh.action = "layer_select_1"; sh.param = 0;
        bnd::setBinding(0, ButtonId::Layer2, l2);
        auto l3 = bnd::getBinding(0, ButtonId::Layer3);
        l3.shortPress[static_cast<int>(bnd::Modifier::Shift)] = bnd::ActionSlot{};
        bnd::setBinding(0, ButtonId::Layer3, l3);
    }
    std::string t = fileText();
    const auto at = t.find("\"version\": 56");
    EXPECT(at != std::string::npos);
    if (at != std::string::npos) t.replace(at, 13, "\"version\": 55");
    writeText(t);
    bnd::load();

    EXPECT(shiftOf(bnd::getBinding(0, ButtonId::Layer2)).action == "layer_select_1");   // kept
    EXPECT(shiftOf(bnd::getBinding(0, ButtonId::Layer3)).action == "selection_mode_dynamount");   // filled
    EXPECT(fileText().find("\"version\": 56") != std::string::npos);

    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("sidecar_keys: all passed\n");
    return 0;
}
