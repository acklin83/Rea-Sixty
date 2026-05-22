#include "SetupBundle.h"

#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "WDL/jsonparse.h"

#include "Bindings.h"
#include "ParameterGroups.h"
#include "UserPluginCatalog.h"

#include "factory_bundle.h"
#include "reaper_plugin_functions.h"

// Implemented in main.cpp — re-reads every rea_sixty / ReaSixty key
// into the runtime atomics + re-pushes device brightness so the
// imported bundle becomes active without a REAPER restart.
extern "C" void reasixty_reloadGlobalExtState();

namespace uf8 {
namespace setup_bundle {
namespace {

// Static keys we serialize / restore. Dynamic uc1 calibration keys are
// expanded at write/read time from the cal index helpers below.
struct ExtKey { const char* section; const char* key; };

constexpr ExtKey kKeys[] = {
    // rea_sixty — Settings preferences
    { "rea_sixty", "alt_drag_snap_back" },
    { "rea_sixty", "auto_fill_from_right" },
    { "rea_sixty", "auto_hide_read_trim" },
    { "rea_sixty", "ballistic_mode" },
    { "rea_sixty", "brightness" },
    { "rea_sixty", "fx_chain_pin_center" },
    { "rea_sixty", "fx_chain_pin_pos" },
    { "rea_sixty", "fx_chain_pin_x" },
    { "rea_sixty", "fx_chain_pin_y" },
    { "rea_sixty", "gr_any_fx" },
    { "rea_sixty", "visibility_follow" },
    { "rea_sixty", "modes_subtab" },
    { "rea_sixty", "nav_auto_follow" },
    { "rea_sixty", "nav_color_bar" },
    { "rea_sixty", "nav_default_view" },
    { "rea_sixty", "nav_lower_row" },
    { "rea_sixty", "nav_region_press" },
    { "rea_sixty", "nav_uc1_long_press_v2" },
    { "rea_sixty", "nav_uc1_push" },
    { "rea_sixty", "nav_uc1_push_shift_v2" },
    { "rea_sixty", "nav_uc1_takeover" },
    { "rea_sixty", "plugin_gui_follows_instance" },
    { "rea_sixty", "plugin_gui_pin_center" },
    { "rea_sixty", "plugin_gui_pin_pos" },
    { "rea_sixty", "plugin_gui_pin_x" },
    { "rea_sixty", "plugin_gui_pin_y" },
    { "rea_sixty", "rec_cut" },
    { "rea_sixty", "rec_rme_enabled" },
    { "rea_sixty", "rec_solo" },
    { "rea_sixty", "rec_vpot_push" },
    { "rea_sixty", "rec_vpot_rotate_gain" },
    { "rea_sixty", "rec_vpot_shift_inputch" },
    { "rea_sixty", "scribble_brightness" },
    { "rea_sixty", "sel_follows_color" },
    { "rea_sixty", "selset_auto_mode" },
    { "rea_sixty", "strip_follows_focused_fx" },
    { "rea_sixty", "track_sel_follows_param" },
    { "rea_sixty", "uc1_cal_factory_v" },
    // ReaSixty — runtime mode state
    { "ReaSixty",  "cycleControlMask" },
    { "ReaSixty",  "cycleEngagesUf8" },
    { "ReaSixty",  "cycleOpenMode" },
    { "ReaSixty",  "encoderMode" },
    { "ReaSixty",  "flip" },
    { "ReaSixty",  "folderMode" },
    { "ReaSixty",  "forcePan" },
    { "ReaSixty",  "fxLearnMockup" },
    { "ReaSixty",  "pluginFaderMode" },
    { "ReaSixty",  "selectionMode" },
    { "ReaSixty",  "showOnlySelected" },
    { "ReaSixty",  "softKeyBank" },
    { "ReaSixty",  "uf8PluginMode" },
};

bool readFile_(const std::string& path, std::string& out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n < 0) { std::fclose(f); return false; }
    out.assign(static_cast<size_t>(n), '\0');
    const size_t got = std::fread(out.data(), 1, static_cast<size_t>(n), f);
    std::fclose(f);
    out.resize(got);
    return true;
}

bool writeFile_(const std::string& path, const std::string& contents)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fwrite(contents.data(), 1, contents.size(), f);
    std::fclose(f);
    return true;
}

void appendEscaped_(std::ostringstream& os, const std::string& s)
{
    os << '"';
    for (char c : s) {
        switch (c) {
        case '"':  os << "\\\""; break;
        case '\\': os << "\\\\"; break;
        case '\n': os << "\\n";  break;
        case '\r': os << "\\r";  break;
        case '\t': os << "\\t";  break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x",
                              static_cast<unsigned>(c));
                os << buf;
            } else {
                os << c;
            }
            break;
        }
    }
    os << '"';
}

// All ExtState keys (static + dynamic uc1 cal) in a flat list.
std::vector<ExtKey> allExtKeys_()
{
    std::vector<ExtKey> v;
    v.reserve(sizeof(kKeys) / sizeof(kKeys[0]) + 11);
    for (const auto& k : kKeys) v.push_back(k);
    // Dynamic UC1 calibration delta keys — same layout as main.cpp's
    // startup loop. The strings have to outlive the vector entries,
    // hence the static buffers (lookup table sizes are fixed).
    static char bcKeys[6][24];
    static char csKeys[5][24];
    for (int i = 0; i < 6; ++i) {
        snprintf(bcKeys[i], sizeof(bcKeys[i]), "uc1_bc_vu_cal_%d", i);
        v.push_back({"rea_sixty", bcKeys[i]});
    }
    for (int i = 0; i < 5; ++i) {
        snprintf(csKeys[i], sizeof(csKeys[i]), "uc1_cs_leds_cal_%d", i);
        v.push_back({"rea_sixty", csKeys[i]});
    }
    // Selection-Set global-scope keys. Each slot has a scope flag
    // ("global" / "project") plus the content for global-scoped slots.
    // Project-scoped slots store their content in ProjExtState — that
    // travels with the .rpp and is out of scope for this bundle. Only
    // global slots round-trip through the Settings export (Frank
    // 2026-05-19: "Group-Selsets sollen global erhalten bleiben").
    static char selsetScopeKeys[8][24];
    static char selsetDataKeys[8][24];
    for (int s = 1; s <= 8; ++s) {
        snprintf(selsetScopeKeys[s - 1], 24, "selset_%d_scope", s);
        snprintf(selsetDataKeys[s - 1],  24, "selset_%d_data",  s);
        v.push_back({"rea_sixty", selsetScopeKeys[s - 1]});
        v.push_back({"rea_sixty", selsetDataKeys[s - 1]});
    }
    return v;
}

const char* readJsonString_(wdl_json_element* el)
{
    if (!el) return nullptr;
    return el->get_string_value(true);
}

} // namespace

bool exportToFile(const std::string& path, std::string* errOut)
{
    // Pull each module's serialised JSON straight from disk — the
    // in-memory state is what the user has staged, but the on-disk
    // file is always up-to-date because every mutation persists
    // immediately via save() / writeFile_(). Reading the file gives
    // us a known-valid JSON without re-running each module's
    // serializer.
    std::string bindingsJson, userPluginsJson, paramGroupsJson;
    readFile_(bindings::configPath(),     bindingsJson);
    readFile_(user_plugins::configPath(), userPluginsJson);
    readFile_(param_groups::configPath(), paramGroupsJson);

    std::ostringstream os;
    os << "{\n";
    os << "  \"format\": \"rea-sixty-setup\",\n";
    os << "  \"version\": 1,\n";
    os << "  \"bindings\": ";        appendEscaped_(os, bindingsJson);     os << ",\n";
    os << "  \"user_plugins\": ";    appendEscaped_(os, userPluginsJson);  os << ",\n";
    os << "  \"parameter_groups\": "; appendEscaped_(os, paramGroupsJson); os << ",\n";
    os << "  \"ext_state\": {\n";

    const auto keys = allExtKeys_();
    bool first = true;
    for (const auto& k : keys) {
        const char* v = GetExtState(k.section, k.key);
        if (!v || !*v) continue;   // missing -> omit (restore leaves default)
        if (!first) os << ",\n";
        first = false;
        os << "    \"" << k.section << "." << k.key << "\": ";
        appendEscaped_(os, v);
    }
    os << "\n  }\n";
    os << "}\n";

    if (!writeFile_(path, os.str())) {
        if (errOut) *errOut = "could not write " + path;
        return false;
    }
    return true;
}

bool importFromJson(const std::string& contents, std::string* errOut)
{
    wdl_json_parser p;
    wdl_json_element* root = p.parse(
        contents.c_str(), static_cast<int>(contents.size()));
    if (!root || !root->is_object()) {
        if (errOut) *errOut = "bundle is not valid JSON";
        return false;
    }

    if (auto* fmt = root->get_item_by_name("format")) {
        const char* s = fmt->get_string_value(true);
        if (!s || std::strcmp(s, "rea-sixty-setup") != 0) {
            if (errOut) *errOut = "not a rea-sixty-setup bundle";
            return false;
        }
    }

    // Section 1: write the three module JSONs to their canonical
    // on-disk paths, then call each module's load() to swap the
    // in-memory state.
    auto applyModule = [&](const char* name,
                           const std::string& outPath,
                           void(*loadFn)())
    {
        auto* el = root->get_item_by_name(name);
        const char* s = readJsonString_(el);
        if (!s) return;
        std::string body(s);
        if (body.empty()) return;
        if (!writeFile_(outPath, body)) {
            if (errOut) *errOut = std::string("could not write ") + outPath;
            // Continue anyway — the other modules might still apply.
            return;
        }
        loadFn();
    };
    applyModule("bindings",
                bindings::configPath(),
                +[]() { bindings::load(); });
    applyModule("user_plugins",
                user_plugins::configPath(),
                +[]() { user_plugins::load(); });
    applyModule("parameter_groups",
                param_groups::configPath(),
                +[]() { param_groups::load(); });

    // Section 2: ExtState. Bundle stores all values as flat
    // "<section>.<key>" -> string pairs under "ext_state".
    if (auto* es = root->get_item_by_name("ext_state");
        es && es->is_object())
    {
        for (int i = 0; ; ++i) {
            wdl_json_element* val = es->enum_item(i);
            if (!val) break;
            const char* nm = es->enum_item_name(i);
            if (!nm) continue;
            const char* dot = std::strchr(nm, '.');
            if (!dot) continue;
            std::string section(nm, dot - nm);
            std::string key(dot + 1);
            const char* v = val->get_string_value(true);
            if (!v) continue;
            SetExtState(section.c_str(), key.c_str(), v, true);
        }
    }

    // Section 3: re-read every ExtState key into runtime atomics +
    // re-push device brightness so the new settings take effect now.
    reasixty_reloadGlobalExtState();
    return true;
}

bool importFromFile(const std::string& path, std::string* errOut)
{
    std::string contents;
    if (!readFile_(path, contents)) {
        if (errOut) *errOut = "could not read " + path;
        return false;
    }
    return importFromJson(contents, errOut);
}

bool restoreFactoryDefaults(std::string* errOut)
{
    // kFactoryBundleBytes / Size are the embedded factory.rea60config
    // payload baked in at build time via cmake/embed_factory_bundle.cmake.
    // Stored as a byte array (not a raw-string literal) so MSVC's 16380-
    // char string-literal limit doesn't truncate the bundle.
    return importFromJson(
        std::string(reinterpret_cast<const char*>(kFactoryBundleBytes),
                    kFactoryBundleSize),
        errOut);
}

} // namespace setup_bundle
} // namespace uf8
