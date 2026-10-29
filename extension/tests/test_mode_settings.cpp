// ModeSettings: reading Rea-Sixty's values out of REAPER's reaper-extstate.ini
// for ORC's Import (docs/orc-uf8-plan.md section 6), and the keys an Import
// copies.

#include "ModeSettings.h"
#include "DynaMountManager.h"

#include <cstdio>
#include <string>

static int g_fail = 0;
#define EXPECT(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

int main()
{
    const std::string ini =
        "[ReaSixty]\n"
        "selectionMode=norm\n"
        "[rea_sixty]\r\n"
        "hue_config=v2;0\t1\tDesk\t0\tabc\t\t\t0\t0;\r\n"
        "hue_key=secret=with=equals\n"
        "dynamount_fill=R\n"
        "empty=\n"
        "=novalue\n"
        "[other]\n"
        "hue_config=not ours\n";
    const auto m = modesettings::parseIni(ini, "rea_sixty");
    EXPECT(m.count("hue_config") == 1);
    EXPECT(m.at("hue_config") == "v2;0\t1\tDesk\t0\tabc\t\t\t0\t0;");   // CR dropped, tabs kept
    EXPECT(m.at("hue_key") == "secret=with=equals");                    // the first '=' splits
    EXPECT(m.at("dynamount_fill") == "R");
    EXPECT(m.count("selectionMode") == 0);                              // another section
    EXPECT(m.at("empty").empty());
    EXPECT(m.size() == 4);
    EXPECT(modesettings::parseIni(ini, "nothing").empty());
    EXPECT(modesettings::parseIni("", "rea_sixty").empty());

    // The positions of the stands are not part of an Import.
    bool state = false;
    for (const char* k : modesettings::kImportKeys) state = state || std::string(k) == "dynamount_state";
    EXPECT(!state);

    // ⇨ A GETTER WITHOUT A FILL LEAVES THE DIRECTION ALONE (an import or a
    // handover that brings only some values, ModeSync.h).
    {
        auto& dm = uf8::dynamount::manager();
        modesettings::apply([](const char* k) { return std::string(k) == "dynamount_fill" ? std::string("R") : std::string(); });
        EXPECT(dm.fillDir() == uf8::dynamount::FillDir::Right);
        modesettings::apply([](const char*) { return std::string(); });
        EXPECT(dm.fillDir() == uf8::dynamount::FillDir::Right);
    }

    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("mode_settings: all passed\n");
    return 0;
}
