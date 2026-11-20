// REAPER actions outside Main (20.11.2026, forum: MIDI Editor actions on the
// user jog modes). A step remembers its action list section through a save and
// a reload, an old file without one stays Main, a section nobody can run falls
// back to Main, a press fires through the host's section hook and not through
// the Main sentinel, and the toggle state and the editor's press choice are
// asked in the step's own section. Runs the real bindings engine on a temp dir.

#include "Bindings.h"
#include "BindingsHost.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace bnd = uf8::bindings;

static int g_fail = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static std::string g_dir;
static int g_mainFires = 0, g_mainCmd = 0;
static int g_secFires = 0, g_secSection = -1, g_secCmd = 0;
static int g_toggleAskedSection = -1;

static std::string fileText()
{
    std::ifstream f(g_dir + "/bindings.json");
    std::stringstream ss; ss << f.rdbuf(); return ss.str();
}
static void writeFile(const std::string& t)
{
    std::ofstream f(g_dir + "/bindings.json"); f << t;
}
static size_t count(const std::string& hay, const std::string& needle)
{
    size_t n = 0;
    for (size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + 1)) ++n;
    return n;
}

static bnd::Binding keyWith(const char* action, int section)
{
    bnd::Binding bd = bnd::getBinding(0, bnd::ButtonId::Fine);
    for (auto& s : bd.shortPress) s = bnd::ActionSlot{};
    for (auto& s : bd.longPress)  s = bnd::ActionSlot{};
    bd.hasLongPress = false;
    bd.behavior = bnd::Behavior::Momentary;
    auto& p = bd.shortPress[static_cast<int>(bnd::Modifier::Plain)];
    p.type = bnd::ActionType::Reaper;
    p.action = action;
    p.section = section;
    return bd;
}

int main()
{
    char tmpl[] = "/tmp/rs_actsecXXXXXX";
    g_dir = mkdtemp(tmpl);
    bnd::Host h;
    h.configDir  = [] { return g_dir; };
    h.configFile = [] { return std::string("bindings.json"); };
    h.namedCommand = [](const char*) { return 0; };
    h.toggleState = [](int section, int) { g_toggleAskedSection = section; return 1; };
    h.runSectionAction = [](int section, int cmd) { ++g_secFires; g_secSection = section; g_secCmd = cmd; };
    bnd::setHost(std::move(h));
    bnd::registerBuiltin("__reaper_action__", bnd::BuiltinDescriptor{
        [](bool firing, bool, int cmd) { if (firing) { ++g_mainFires; g_mainCmd = cmd; } },
        nullptr, "", false });
    bnd::load();

    // ── the known sections ──────────────────────────────────────────────────
    EXPECT(bnd::actionSectionKnown(0) && bnd::actionSectionKnown(32060)
        && bnd::actionSectionKnown(32061) && bnd::actionSectionKnown(32065));
    EXPECT(!bnd::actionSectionKnown(32062) && !bnd::actionSectionKnown(32063)
        && !bnd::actionSectionKnown(100) && !bnd::actionSectionKnown(-1));

    // ── a Main step writes no section at all ────────────────────────────────
    bnd::setBinding(0, bnd::ButtonId::Fine, keyWith("40001", bnd::kActionSectionMain));
    bnd::save();
    EXPECT(fileText().find("\"section\"") == std::string::npos);

    // ── a MIDI Editor step survives save and reload ─────────────────────────
    bnd::setBinding(0, bnd::ButtonId::Fine, keyWith("40001", bnd::kActionSectionMidiEditor));
    bnd::save();
    EXPECT(count(fileText(), "\"section\": 32060") == 1);
    bnd::load();
    {
        const bnd::Binding r = bnd::getBinding(0, bnd::ButtonId::Fine);
        const auto& p = r.shortPress[static_cast<int>(bnd::Modifier::Plain)];
        EXPECT(p.type == bnd::ActionType::Reaper && p.action == "40001");
        EXPECT(p.section == bnd::kActionSectionMidiEditor);
    }

    // ── a section nobody can run reads back as Main ─────────────────────────
    {
        std::string t = fileText();
        const size_t at = t.find("\"section\": 32060");
        EXPECT(at != std::string::npos);
        if (at != std::string::npos) t.replace(at, 16, "\"section\": 32063");
        writeFile(t);
        bnd::load();
        const bnd::Binding r = bnd::getBinding(0, bnd::ButtonId::Fine);
        EXPECT(r.shortPress[static_cast<int>(bnd::Modifier::Plain)].section == bnd::kActionSectionMain);
    }

    // ── a press goes to the host's section hook, never to Main ──────────────
    {
        bnd::ActionSlot s = keyWith("40001", bnd::kActionSectionMidiEventList)
                                .shortPress[static_cast<int>(bnd::Modifier::Plain)];
        g_mainFires = g_secFires = 0;
        bnd::runPressSlot(s);
        EXPECT(g_secFires == 1 && g_secSection == bnd::kActionSectionMidiEventList && g_secCmd == 40001);
        EXPECT(g_mainFires == 0);

        s.section = bnd::kActionSectionMain;
        g_mainFires = g_secFires = 0;
        bnd::runPressSlot(s);
        EXPECT(g_mainFires == 1 && g_mainCmd == 40001 && g_secFires == 0);
    }

    // ── toggle state and the press choice are asked in the step's section ───
    {
        const bnd::Binding bd = keyWith("40001", bnd::kActionSectionCrossfade);
        g_toggleAskedSection = -1;
        EXPECT(bnd::bindingHasActiveSlot(bd));
        EXPECT(g_toggleAskedSection == bnd::kActionSectionCrossfade);

        int askedSection = -1;
        EXPECT(bnd::offersPressChoice(bd, [&](const std::string& a, int sec) {
            askedSection = sec; return a == "40001";
        }));
        EXPECT(askedSection == bnd::kActionSectionCrossfade);
    }

    if (g_fail == 0) std::printf("action_sections: all passed\n");
    return g_fail == 0 ? 0 : 1;
}
