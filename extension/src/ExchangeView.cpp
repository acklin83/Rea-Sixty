#include "ExchangeView.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// ReaImGui bindings — declarations only; REAIMGUIAPI_IMPLEMENT lives in
// MixerWindow.cpp (one TU materialises the lazy-resolved function storage).
#include "reaper_imgui_functions.h"

// REAPER host API — GetExtState / SetExtState for the persisted settings.
#include "reaper_plugin_functions.h"

#include "HttpClient.h"
#include "UserPluginCatalog.h"
#include "WDL/jsonparse.h"

namespace uf8 {
namespace {

namespace up = uf8::user_plugins;
namespace http = reasixty::http;

constexpr const char* kExtSection  = "rea_sixty";
constexpr const char* kKeyApiUrl   = "exchange_api_url";
constexpr const char* kKeyApiToken = "exchange_token";
constexpr const char* kDefaultApiUrl = "http://127.0.0.1:8010";

char g_apiUrl[512] = {0};
char g_token[256]  = {0};
char g_search[128] = {0};
bool g_loaded      = false;
bool g_showSettings = false;

// ---- connection test ------------------------------------------------------
uint64_t    g_healthReq = 0;
std::string g_healthResult;
int         g_healthColor = 0;

// ---- browse ---------------------------------------------------------------
enum class View { List, Plugin, Map };
View g_view = View::List;

struct PluginRow {
    std::string slug, name, vendor, surfaces;
    int mapCount = 0, bestCoverage = 0;
};
std::vector<PluginRow> g_plugins;
uint64_t    g_listReq = 0;
std::string g_listError;
bool        g_listedOnce = false;

struct MapRow {
    int id = 0;
    std::string author, surfaces, domain, description;
    int covN = 0, covD = 0, covPct = 0, works = 0;
};
std::string g_pluginSlug, g_pluginName, g_pluginVendor;
std::vector<MapRow> g_maps;
uint64_t    g_pluginReq = 0;
std::string g_pluginError;

// ---- one map's detail (screen 3): the control -> parameter mappings --------
struct CtrlCell { std::string control, param; bool bound = false; };
struct Section  { std::string name; int total = 0, hit = 0; std::vector<CtrlCell> cells; };
struct Uf8Vpot  { int faderBank = 0, vpotBank = 0, strip = 0; std::string label, param; };
struct Uf8Strip { int faderBank = 0, strip = 0; std::string kind, param; };
struct ModBind  { std::string layer, control, param; };
struct MapDetail {
    int id = 0;
    std::string pluginName, vendor, surfaces, domain, author, description, licence;
    int covN = 0, covD = 0, covPct = 0;
    bool isUf8 = false;
    int uf8Vpots = 0, uf8VpotSlots = 0, uf8Strips = 0, uf8StripSlots = 0;
    std::vector<Section> sections;
    std::vector<Uf8Vpot>  uf8VpotBindings;
    std::vector<Uf8Strip> uf8StripBindings;
    std::vector<std::pair<int, std::string>> alsoMapped;              // linkIdx, param
    std::vector<ModBind> modLayers;
};
MapDetail   g_mapDetail;
uint64_t    g_mapReq = 0;
std::string g_mapError;

// ---- install --------------------------------------------------------------
uint64_t    g_dlReq = 0;
int         g_dlMapId = 0;
std::string g_installStatus;
int         g_installColor = 0;
up::MapShare g_pendingInstall;     // downloaded, awaiting a replace-confirm
bool        g_pendingClash = false;

void loadOnce() {
    if (g_loaded) return;
    g_loaded = true;
    const char* url = GetExtState(kExtSection, kKeyApiUrl);
    std::snprintf(g_apiUrl, sizeof(g_apiUrl), "%s", (url && *url) ? url : kDefaultApiUrl);
    const char* tok = GetExtState(kExtSection, kKeyApiToken);
    std::snprintf(g_token, sizeof(g_token), "%s", tok ? tok : "");
}

std::string baseUrl() {
    std::string s = g_apiUrl;
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

// --- tiny JSON readers over wdl_json ---------------------------------------
std::string jstr(wdl_json_element* o, const char* k) {
    if (!o) return "";
    auto* v = o->get_item_by_name(k);
    const char* s = v ? v->get_string_value(true) : nullptr;
    return s ? std::string(s) : std::string();
}
int jint(wdl_json_element* o, const char* k) {
    std::string s = jstr(o, k);
    return s.empty() ? 0 : std::atoi(s.c_str());
}
bool jbool(wdl_json_element* o, const char* k) {
    return jstr(o, k) == "true";
}
std::string jarrjoin(wdl_json_element* o, const char* k, const char* sep) {
    auto* a = o ? o->get_item_by_name(k) : nullptr;
    if (!a || !a->is_array() || !a->m_array) return "";
    std::string out;
    for (int i = 0; i < a->m_array->GetSize(); ++i) {
        auto* e = a->enum_item(i);
        const char* s = e ? e->get_string_value(true) : nullptr;
        if (!s) continue;
        if (!out.empty()) out += sep;
        out += s;
    }
    return out;
}

// URL-encode the few characters a plug-in search or slug can carry.
std::string urlEncode(const std::string& in) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : in) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 0xF]; }
    }
    return out;
}

// ---- fetch starters -------------------------------------------------------
void fetchList() {
    if (g_listReq) return;
    g_listError.clear();
    std::string url = baseUrl() + "/v1/plugins?pageSize=200";
    if (g_search[0]) url += "&search=" + urlEncode(g_search);
    g_listReq = http::begin("GET", url);
    if (!g_listReq) g_listError = "Bad server URL.";
    g_listedOnce = true;
}

void openPlugin(const std::string& slug) {
    if (g_pluginReq) return;
    g_pluginSlug = slug;
    g_pluginName.clear();
    g_maps.clear();
    g_pluginError.clear();
    g_installStatus.clear();
    g_view = View::Plugin;
    g_pluginReq = http::begin("GET", baseUrl() + "/v1/plugins/" + urlEncode(slug));
    if (!g_pluginReq) g_pluginError = "Bad server URL.";
}

void openMap(int mapId) {
    if (g_mapReq) return;
    g_mapDetail = MapDetail{};
    g_mapError.clear();
    g_installStatus.clear();
    g_pendingClash = false;
    g_view = View::Map;
    g_mapReq = http::begin("GET", baseUrl() + "/v1/maps/" + std::to_string(mapId));
    if (!g_mapReq) g_mapError = "Bad server URL.";
}

void startInstall(int mapId) {
    if (g_dlReq) return;
    g_installStatus.clear();
    g_installColor = 0;
    g_pendingClash = false;
    g_dlMapId = mapId;
    g_dlReq = http::begin("GET", baseUrl() + "/v1/maps/" + std::to_string(mapId) + "/download");
    if (!g_dlReq) { g_installStatus = "Bad server URL."; g_installColor = 0xCC4444FF; }
}

// Actually apply a downloaded map to the catalog.
void applyInstall(up::MapShare&& share) {
    const std::string match = share.map.match;
    if (up::collidesWithBuiltin(match)) {
        g_installStatus = "Can't install — \"" + match + "\" is a built-in mapping.";
        g_installColor = 0xCC4444FF;
        return;
    }
    up::upsert(std::move(share.map));
    switch (up::save()) {
        case up::SaveResult::Ok:
            g_installStatus = "Installed \"" + match + "\".";
            g_installColor = 0x44AA44FF;
            break;
        case up::SaveResult::Collision:
            g_installStatus = "Saved in memory, but a name clash blocked writing to disk.";
            g_installColor = 0xCC8844FF;
            break;
        case up::SaveResult::IoError:
            g_installStatus = "Installed in memory, but the catalog file could not be written.";
            g_installColor = 0xCC8844FF;
            break;
    }
}

// ---- per-frame pumps ------------------------------------------------------
void pumpHealth() {
    if (!g_healthReq) return;
    http::Response r;
    if (!http::poll(g_healthReq, r)) return;
    g_healthReq = 0;
    if (!r.error.empty())      { g_healthResult = "Could not reach the server: " + r.error; g_healthColor = 0xCC4444FF; }
    else if (r.status == 200)  { g_healthResult = "Connected — server is up.";               g_healthColor = 0x44AA44FF; }
    else                       { g_healthResult = "Server answered with HTTP " + std::to_string(r.status); g_healthColor = 0xCC8844FF; }
}

void pumpList() {
    if (!g_listReq) return;
    http::Response r;
    if (!http::poll(g_listReq, r)) return;
    g_listReq = 0;
    if (!r.error.empty()) { g_listError = "Could not reach the server: " + r.error; return; }
    if (r.status != 200)  { g_listError = "Server returned HTTP " + std::to_string(r.status); return; }
    wdl_json_parser p;
    wdl_json_element* root = p.parse(r.body.c_str(), (int)r.body.size());
    if (!root || !root->is_object()) { g_listError = "Bad response from server."; return; }
    auto* rows = root->get_item_by_name("rows");
    g_plugins.clear();
    if (rows && rows->is_array() && rows->m_array) {
        for (int i = 0; i < rows->m_array->GetSize(); ++i) {
            wdl_json_element* o = rows->enum_item(i);
            if (!o || !o->is_object()) continue;
            PluginRow pr;
            pr.slug     = jstr(o, "slug");
            pr.name     = jstr(o, "name");
            pr.vendor   = jstr(o, "vendor");
            pr.surfaces = jarrjoin(o, "surfaces", "+");
            pr.mapCount = jint(o, "mapCount");
            pr.bestCoverage = jint(o, "bestCoverage");
            g_plugins.push_back(std::move(pr));
        }
    }
}

void pumpPlugin() {
    if (!g_pluginReq) return;
    http::Response r;
    if (!http::poll(g_pluginReq, r)) return;
    g_pluginReq = 0;
    if (!r.error.empty()) { g_pluginError = "Could not reach the server: " + r.error; return; }
    if (r.status != 200)  { g_pluginError = "Server returned HTTP " + std::to_string(r.status); return; }
    wdl_json_parser p;
    wdl_json_element* root = p.parse(r.body.c_str(), (int)r.body.size());
    if (!root || !root->is_object()) { g_pluginError = "Bad response from server."; return; }
    g_pluginName   = jstr(root, "name");
    g_pluginVendor = jstr(root, "vendor");
    g_maps.clear();
    auto* maps = root->get_item_by_name("maps");
    if (maps && maps->is_array() && maps->m_array) {
        for (int i = 0; i < maps->m_array->GetSize(); ++i) {
            wdl_json_element* o = maps->enum_item(i);
            if (!o || !o->is_object()) continue;
            MapRow mr;
            mr.id          = jint(o, "id");
            mr.author      = jstr(o, "author");
            mr.surfaces    = jstr(o, "surfaces");
            mr.domain      = jstr(o, "domain");
            mr.description = jstr(o, "description");
            auto* cov = o->get_item_by_name("coverage");
            mr.covN   = jint(cov, "n");
            mr.covD   = jint(cov, "d");
            mr.covPct = jint(cov, "pct");
            mr.works  = jint(o, "works");
            g_maps.push_back(std::move(mr));
        }
    }
}

void pumpMap() {
    if (!g_mapReq) return;
    http::Response r;
    if (!http::poll(g_mapReq, r)) return;
    g_mapReq = 0;
    if (!r.error.empty()) { g_mapError = "Could not reach the server: " + r.error; return; }
    if (r.status != 200)  { g_mapError = "Server returned HTTP " + std::to_string(r.status); return; }
    wdl_json_parser p;
    wdl_json_element* root = p.parse(r.body.c_str(), (int)r.body.size());
    if (!root || !root->is_object()) { g_mapError = "Bad response from server."; return; }

    MapDetail d;
    d.id       = jint(root, "id");
    if (auto* pl = root->get_item_by_name("plugin")) d.pluginName = jstr(pl, "name");
    d.vendor   = jstr(root, "vendor");
    d.surfaces = jstr(root, "surfaces");
    d.domain   = jstr(root, "domain");
    d.author   = jstr(root, "author");
    d.description = jstr(root, "description");
    d.licence  = jstr(root, "licence");
    if (auto* cov = root->get_item_by_name("coverage")) {
        d.covN = jint(cov, "n"); d.covD = jint(cov, "d"); d.covPct = jint(cov, "pct");
    }
    if (auto* u = root->get_item_by_name("uf8"); u && u->is_object()) {
        d.isUf8 = true;
        d.uf8Vpots = jint(u, "vpots"); d.uf8VpotSlots = jint(u, "vpotSlots");
        d.uf8Strips = jint(u, "strips"); d.uf8StripSlots = jint(u, "stripSlots");
        if (auto* vb = u->get_item_by_name("vpotBindings"); vb && vb->is_array() && vb->m_array) {
            for (int i = 0; i < vb->m_array->GetSize(); ++i) {
                wdl_json_element* o = vb->enum_item(i);
                if (!o || !o->is_object()) continue;
                Uf8Vpot v;
                v.faderBank = jint(o, "faderBank"); v.vpotBank = jint(o, "vpotBank");
                v.strip = jint(o, "strip"); v.label = jstr(o, "label"); v.param = jstr(o, "param");
                d.uf8VpotBindings.push_back(std::move(v));
            }
        }
        if (auto* sb = u->get_item_by_name("stripBindings"); sb && sb->is_array() && sb->m_array) {
            for (int i = 0; i < sb->m_array->GetSize(); ++i) {
                wdl_json_element* o = sb->enum_item(i);
                if (!o || !o->is_object()) continue;
                Uf8Strip s;
                s.faderBank = jint(o, "faderBank"); s.strip = jint(o, "strip");
                s.kind = jstr(o, "kind"); s.param = jstr(o, "param");
                d.uf8StripBindings.push_back(std::move(s));
            }
        }
    }
    if (auto* secs = root->get_item_by_name("sections"); secs && secs->is_array() && secs->m_array) {
        for (int i = 0; i < secs->m_array->GetSize(); ++i) {
            wdl_json_element* so = secs->enum_item(i);
            if (!so || !so->is_object()) continue;
            Section s;
            s.name  = jstr(so, "name");
            s.total = jint(so, "total");
            s.hit   = jint(so, "hit");
            if (auto* cells = so->get_item_by_name("cells"); cells && cells->is_array() && cells->m_array) {
                for (int j = 0; j < cells->m_array->GetSize(); ++j) {
                    wdl_json_element* co = cells->enum_item(j);
                    if (!co || !co->is_object()) continue;
                    CtrlCell c;
                    c.control = jstr(co, "name");
                    c.param   = jstr(co, "param");
                    c.bound   = jbool(co, "bound");
                    s.cells.push_back(std::move(c));
                }
            }
            d.sections.push_back(std::move(s));
        }
    }
    if (auto* am = root->get_item_by_name("alsoMapped"); am && am->is_array() && am->m_array) {
        for (int i = 0; i < am->m_array->GetSize(); ++i) {
            wdl_json_element* o = am->enum_item(i);
            if (!o || !o->is_object()) continue;
            d.alsoMapped.emplace_back(jint(o, "linkIdx"), jstr(o, "param"));
        }
    }
    if (auto* ml = root->get_item_by_name("modifierLayers"); ml && ml->is_array() && ml->m_array) {
        for (int i = 0; i < ml->m_array->GetSize(); ++i) {
            wdl_json_element* o = ml->enum_item(i);
            if (!o || !o->is_object()) continue;
            ModBind mb;
            mb.layer = jstr(o, "layer");
            mb.control = jstr(o, "control");
            mb.param = jstr(o, "param");
            d.modLayers.push_back(std::move(mb));
        }
    }
    g_mapDetail = std::move(d);
}

void pumpDownload() {
    if (!g_dlReq) return;
    http::Response r;
    if (!http::poll(g_dlReq, r)) return;
    g_dlReq = 0;
    if (!r.error.empty()) { g_installStatus = "Download failed: " + r.error; g_installColor = 0xCC4444FF; return; }
    if (r.status != 200)  { g_installStatus = "Download failed: HTTP " + std::to_string(r.status); g_installColor = 0xCC4444FF; return; }

    up::MapShare share;
    std::string err;
    if (!up::importMapFromString(r.body, share, &err)) {
        g_installStatus = "Not a valid mapping: " + err;
        g_installColor = 0xCC4444FF;
        return;
    }
    // Replacing an existing user map wholesale is the user's call.
    bool exists = false;
    for (const auto& m : up::get().maps)
        if (m.match == share.map.match) { exists = true; break; }
    if (exists) {
        g_pendingInstall = std::move(share);
        g_pendingClash = true;
        g_installStatus = "You already have a mapping for \"" + g_pendingInstall.map.match + "\".";
        g_installColor = 0xCC8844FF;
    } else {
        applyInstall(std::move(share));
    }
}

// ---- render ---------------------------------------------------------------
void drawSettings(ImGui_Context* ctx) {
    ImGui_Text(ctx, "Server");
    ImGui_SetNextItemWidth(ctx, -1.0);
    if (ImGui_InputText(ctx, "##exch_url", g_apiUrl, sizeof(g_apiUrl), nullptr, nullptr))
        SetExtState(kExtSection, kKeyApiUrl, g_apiUrl, true);

    ImGui_Spacing(ctx);
    ImGui_Text(ctx, "Device token (for uploads)");
    int tokFlags = ImGui_InputTextFlags_Password;
    ImGui_SetNextItemWidth(ctx, -1.0);
    if (ImGui_InputText(ctx, "##exch_token", g_token, sizeof(g_token), &tokFlags, nullptr))
        SetExtState(kExtSection, kKeyApiToken, g_token, true);

    ImGui_Spacing(ctx);
    const bool busy = g_healthReq != 0;
    if (ImGui_Button(ctx, busy ? "Testing…##exch_test" : "Test connection##exch_test", nullptr, nullptr) && !busy) {
        g_healthResult.clear();
        g_healthReq = http::begin("GET", baseUrl() + "/health");
        if (!g_healthReq) { g_healthResult = "Bad server URL."; g_healthColor = 0xCC4444FF; }
    }
    if (!g_healthResult.empty()) {
        ImGui_SameLine(ctx, nullptr, nullptr);
        if (g_healthColor) ImGui_TextColored(ctx, g_healthColor, g_healthResult.c_str());
        else               ImGui_Text(ctx, g_healthResult.c_str());
    }
}

void drawList(ImGui_Context* ctx) {
    // Search + refresh.
    ImGui_SetNextItemWidth(ctx, 260.0);
    int sFlags = ImGui_InputTextFlags_EnterReturnsTrue;
    if (ImGui_InputText(ctx, "##exch_search", g_search, sizeof(g_search), &sFlags, nullptr))
        fetchList();
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (ImGui_Button(ctx, g_listReq ? "Loading…##exch_refresh" : "Search / Refresh##exch_refresh", nullptr, nullptr) && !g_listReq)
        fetchList();

    if (!g_listError.empty()) {
        ImGui_Spacing(ctx);
        ImGui_TextColored(ctx, 0xCC4444FF, g_listError.c_str());
        return;
    }
    ImGui_Spacing(ctx);
    ImGui_Separator(ctx);
    ImGui_Spacing(ctx);

    if (g_plugins.empty()) {
        ImGui_TextWrapped(ctx, g_listReq ? "Loading plug-ins…"
                                         : "No plug-ins found. Try Search / Refresh.");
        return;
    }

    // One row per plug-in. Click a row to open its maps. ScrollY + a fixed
    // outer height keeps the header row sticky while the body scrolls; columns
    // are resizable and hideable; the plug-in name stretches, the rest fixed.
    double availW = 0.0, availH = 0.0;
    ImGui_GetContentRegionAvail(ctx, &availW, &availH);
    if (availH < 160.0) availH = 160.0;   // never collapse in a short pane
    int tFlags = ImGui_TableFlags_RowBg | ImGui_TableFlags_Borders
               | ImGui_TableFlags_Resizable | ImGui_TableFlags_Hideable
               | ImGui_TableFlags_ScrollY;
    if (ImGui_BeginTable(ctx, "##exch_plugins", 5, &tFlags, nullptr, &availH, nullptr)) {
        int stretch = ImGui_TableColumnFlags_WidthStretch;
        int fixed   = ImGui_TableColumnFlags_WidthFixed;
        double wName = 3.0, wVendor = 2.0, wMaps = 46.0, wSurf = 100.0, wBest = 96.0;
        ImGui_TableSetupColumn(ctx, "Plug-in",  &stretch, &wName,   nullptr);
        ImGui_TableSetupColumn(ctx, "Vendor",   &stretch, &wVendor, nullptr);
        ImGui_TableSetupColumn(ctx, "Maps",     &fixed,   &wMaps,   nullptr);
        ImGui_TableSetupColumn(ctx, "Surfaces", &fixed,   &wSurf,   nullptr);
        // Best coverage any of this plug-in's maps reaches (the number is a
        // percentage of the controls that plug-in's surface can bind).
        ImGui_TableSetupColumn(ctx, "Best coverage", &fixed, &wBest, nullptr);
        ImGui_TableHeadersRow(ctx);

        char buf[512];
        for (const auto& p : g_plugins) {
            ImGui_TableNextRow(ctx, nullptr, nullptr);

            ImGui_TableSetColumnIndex(ctx, 0);
            std::snprintf(buf, sizeof(buf), "%s##exch_p_%s",
                          p.name.c_str(), p.slug.c_str());
            int selFlags = ImGui_SelectableFlags_SpanAllColumns;
            if (ImGui_Selectable(ctx, buf, nullptr, &selFlags, nullptr, nullptr))
                openPlugin(p.slug);

            ImGui_TableSetColumnIndex(ctx, 1);
            ImGui_Text(ctx, p.vendor.empty() ? "—" : p.vendor.c_str());

            ImGui_TableSetColumnIndex(ctx, 2);
            std::snprintf(buf, sizeof(buf), "%d", p.mapCount);
            ImGui_Text(ctx, buf);

            ImGui_TableSetColumnIndex(ctx, 3);
            ImGui_Text(ctx, p.surfaces.empty() ? "—" : p.surfaces.c_str());

            ImGui_TableSetColumnIndex(ctx, 4);
            std::snprintf(buf, sizeof(buf), "%d%%", p.bestCoverage);
            double frac = p.bestCoverage / 100.0;
            if (frac < 0.0) frac = 0.0; else if (frac > 1.0) frac = 1.0;
            ImGui_ProgressBar(ctx, frac, nullptr, nullptr, buf);
        }
        ImGui_EndTable(ctx);
    }
}

void drawPlugin(ImGui_Context* ctx) {
    if (ImGui_Button(ctx, "< Back##exch_back", nullptr, nullptr)) {
        g_view = View::List;
        return;
    }
    ImGui_SameLine(ctx, nullptr, nullptr);
    char hdr[384];
    std::snprintf(hdr, sizeof(hdr), "   %s%s%s",
                  g_pluginName.empty() ? g_pluginSlug.c_str() : g_pluginName.c_str(),
                  g_pluginVendor.empty() ? "" : "  —  ",
                  g_pluginVendor.c_str());
    ImGui_Text(ctx, hdr);

    if (!g_pluginError.empty()) {
        ImGui_Spacing(ctx);
        ImGui_TextColored(ctx, 0xCC4444FF, g_pluginError.c_str());
        return;
    }
    ImGui_Spacing(ctx);
    ImGui_Separator(ctx);
    ImGui_Spacing(ctx);

    if (g_maps.empty()) {
        ImGui_TextWrapped(ctx, g_pluginReq ? "Loading…" : "No maps for this plug-in.");
        return;
    }

    ImGui_TextWrapped(ctx, "Click a mapping to see its controls and install it.");
    ImGui_Spacing(ctx);

    // One row per map; click a row to open its detail (controls + install).
    // Sticky header via ScrollY + fixed height.
    double availW = 0.0, availH = 0.0;
    ImGui_GetContentRegionAvail(ctx, &availW, &availH);
    if (availH < 160.0) availH = 160.0;   // never collapse in a short pane
    int tFlags = ImGui_TableFlags_RowBg | ImGui_TableFlags_Borders
               | ImGui_TableFlags_Resizable | ImGui_TableFlags_ScrollY;
    if (ImGui_BeginTable(ctx, "##exch_maps", 4, &tFlags, nullptr, &availH, nullptr)) {
        int stretch = ImGui_TableColumnFlags_WidthStretch;
        int fixed   = ImGui_TableColumnFlags_WidthFixed;
        double wAuthor = 2.0, wSurf = 96.0, wCov = 150.0, wWorks = 64.0;
        ImGui_TableSetupColumn(ctx, "Author",   &stretch, &wAuthor, nullptr);
        ImGui_TableSetupColumn(ctx, "Surface",  &fixed,   &wSurf,   nullptr);
        ImGui_TableSetupColumn(ctx, "Coverage", &fixed,   &wCov,    nullptr);
        ImGui_TableSetupColumn(ctx, "Works",    &fixed,   &wWorks,  nullptr);
        ImGui_TableHeadersRow(ctx);

        char buf[256];
        for (const auto& m : g_maps) {
            ImGui_TableNextRow(ctx, nullptr, nullptr);

            ImGui_TableSetColumnIndex(ctx, 0);
            std::snprintf(buf, sizeof(buf), "%s##exch_m_%d",
                          m.author.empty() ? "—" : m.author.c_str(), m.id);
            int selFlags = ImGui_SelectableFlags_SpanAllColumns;
            if (ImGui_Selectable(ctx, buf, nullptr, &selFlags, nullptr, nullptr))
                openMap(m.id);
            if (!m.description.empty() && ImGui_IsItemHovered(ctx, nullptr))
                ImGui_SetTooltip(ctx, m.description.c_str());

            ImGui_TableSetColumnIndex(ctx, 1);
            ImGui_Text(ctx, m.surfaces.c_str());

            ImGui_TableSetColumnIndex(ctx, 2);
            std::snprintf(buf, sizeof(buf), "%d/%d (%d%%)", m.covN, m.covD, m.covPct);
            double frac = m.covD > 0 ? (double)m.covN / m.covD : 0.0;
            if (frac < 0.0) frac = 0.0; else if (frac > 1.0) frac = 1.0;
            ImGui_ProgressBar(ctx, frac, nullptr, nullptr, buf);

            ImGui_TableSetColumnIndex(ctx, 3);
            std::snprintf(buf, sizeof(buf), "%d", m.works);
            ImGui_Text(ctx, buf);
        }
        ImGui_EndTable(ctx);
    }
}

// The full UF8 picture: one 8×8 grid per fader bank (rows = V-Pot bank,
// columns = strip), every bank shown, each bound cell carrying its V-Pot label
// with the parameter on the tooltip. Then the fader/solo/cut/sel strip
// bindings. 2 fader banks × 8 V-Pot banks × 8 strips = 128 slots.
void drawUf8Grids(ImGui_Context* ctx, const MapDetail& d) {
    // O(1) cell lookup: index = faderBank*64 + vpotBank*8 + strip.
    const Uf8Vpot* grid[128] = { nullptr };
    for (const auto& v : d.uf8VpotBindings) {
        int idx = v.faderBank * 64 + v.vpotBank * 8 + v.strip;
        if (idx >= 0 && idx < 128) grid[idx] = &v;
    }

    for (int fb = 0; fb < 2; ++fb) {
        char h[96];
        std::snprintf(h, sizeof(h), "Fader bank %d   (strips %d–%d)",
                      fb + 1, fb * 8 + 1, fb * 8 + 8);
        ImGui_Text(ctx, h);

        char tid[32];
        std::snprintf(tid, sizeof(tid), "##uf8_grid_%d", fb);
        int tFlags = ImGui_TableFlags_RowBg | ImGui_TableFlags_Borders;
        if (ImGui_BeginTable(ctx, tid, 9, &tFlags, nullptr, nullptr, nullptr)) {
            int fixed = ImGui_TableColumnFlags_WidthFixed;
            int stretch = ImGui_TableColumnFlags_WidthStretch;
            double wBank = 56.0;
            ImGui_TableSetupColumn(ctx, "Bank\\Strip", &fixed, &wBank, nullptr);
            for (int s = 0; s < 8; ++s) {
                char cn[8]; std::snprintf(cn, sizeof(cn), "%d", s + 1);
                double w = 1.0;
                ImGui_TableSetupColumn(ctx, cn, &stretch, &w, nullptr);
            }
            ImGui_TableHeadersRow(ctx);

            for (int vb = 0; vb < 8; ++vb) {
                ImGui_TableNextRow(ctx, nullptr, nullptr);
                ImGui_TableSetColumnIndex(ctx, 0);
                char bl[8]; std::snprintf(bl, sizeof(bl), "%d", vb + 1);
                ImGui_Text(ctx, bl);
                for (int s = 0; s < 8; ++s) {
                    ImGui_TableSetColumnIndex(ctx, s + 1);
                    const Uf8Vpot* v = grid[fb * 64 + vb * 8 + s];
                    if (!v) continue;
                    const std::string& lbl = !v->label.empty() ? v->label : v->param;
                    ImGui_Text(ctx, lbl.empty() ? "●" : lbl.c_str());
                    if (ImGui_IsItemHovered(ctx, nullptr)) {
                        std::string tip = v->label;
                        if (!v->param.empty())
                            tip += (tip.empty() ? "" : "  ->  ") + v->param;
                        if (!tip.empty()) ImGui_SetTooltip(ctx, tip.c_str());
                    }
                }
            }
            ImGui_EndTable(ctx);
        }
        ImGui_Spacing(ctx);
    }

    if (!d.uf8StripBindings.empty()) {
        ImGui_Text(ctx, "Strip bindings (fader / solo / cut / sel):");
        int tFlags = ImGui_TableFlags_RowBg | ImGui_TableFlags_Borders
                   | ImGui_TableFlags_Resizable;
        if (ImGui_BeginTable(ctx, "##uf8_strips", 4, &tFlags, nullptr, nullptr, nullptr)) {
            int fixed = ImGui_TableColumnFlags_WidthFixed;
            int stretch = ImGui_TableColumnFlags_WidthStretch;
            double wStrip = 60.0, wKind = 70.0, wParam = 2.0, wBankc = 60.0;
            ImGui_TableSetupColumn(ctx, "Strip",     &fixed,   &wStrip, nullptr);
            ImGui_TableSetupColumn(ctx, "Control",   &fixed,   &wKind,  nullptr);
            ImGui_TableSetupColumn(ctx, "Parameter", &stretch, &wParam, nullptr);
            ImGui_TableSetupColumn(ctx, "Bank",      &fixed,   &wBankc, nullptr);
            ImGui_TableHeadersRow(ctx);
            char buf[64];
            for (const auto& s : d.uf8StripBindings) {
                ImGui_TableNextRow(ctx, nullptr, nullptr);
                ImGui_TableSetColumnIndex(ctx, 0);
                std::snprintf(buf, sizeof(buf), "%d", s.faderBank * 8 + s.strip + 1);
                ImGui_Text(ctx, buf);
                ImGui_TableSetColumnIndex(ctx, 1);
                ImGui_Text(ctx, s.kind.c_str());
                ImGui_TableSetColumnIndex(ctx, 2);
                ImGui_Text(ctx, s.param.empty() ? "(bound)" : s.param.c_str());
                ImGui_TableSetColumnIndex(ctx, 3);
                std::snprintf(buf, sizeof(buf), "%d", s.faderBank + 1);
                ImGui_Text(ctx, buf);
            }
            ImGui_EndTable(ctx);
        }
    }
}

// Screen 3 — one map: metadata, coverage, the control -> parameter table
// grouped by section, the off-face "also mapped" list, and Install.
void drawMap(ImGui_Context* ctx) {
    if (ImGui_Button(ctx, "< Back##exch_map_back", nullptr, nullptr)) {
        g_view = View::Plugin;
        return;
    }
    const MapDetail& d = g_mapDetail;
    ImGui_SameLine(ctx, nullptr, nullptr);
    char hdr[384];
    std::snprintf(hdr, sizeof(hdr), "   %s%s%s",
                  d.pluginName.c_str(),
                  d.vendor.empty() ? "" : "  —  ", d.vendor.c_str());
    ImGui_Text(ctx, hdr);

    if (!g_mapError.empty()) {
        ImGui_Spacing(ctx);
        ImGui_TextColored(ctx, 0xCC4444FF, g_mapError.c_str());
        return;
    }
    if (g_mapReq) { ImGui_Spacing(ctx); ImGui_Text(ctx, "Loading…"); return; }

    ImGui_Spacing(ctx);
    char meta[384];
    std::snprintf(meta, sizeof(meta), "by %s   ·   %s   ·   %s",
                  d.author.empty() ? "—" : d.author.c_str(),
                  d.surfaces.c_str(), d.licence.empty() ? "—" : d.licence.c_str());
    ImGui_Text(ctx, meta);
    if (!d.description.empty()) ImGui_TextWrapped(ctx, d.description.c_str());

    // Coverage headline.
    ImGui_Spacing(ctx);
    if (d.isUf8) {
        std::snprintf(meta, sizeof(meta), "%d of %d V-Pot slots   ·   %d of %d strips",
                      d.uf8Vpots, d.uf8VpotSlots, d.uf8Strips, d.uf8StripSlots);
        ImGui_Text(ctx, meta);
    } else {
        std::snprintf(meta, sizeof(meta), "%d of %d controls mapped", d.covN, d.covD);
        ImGui_Text(ctx, meta);
    }
    {
        double w = 240.0, h = 0.0;
        double frac = d.covD > 0 ? (double)d.covN / d.covD : 0.0;
        if (frac < 0.0) frac = 0.0; else if (frac > 1.0) frac = 1.0;
        char pct[16]; std::snprintf(pct, sizeof(pct), "%d%%", d.covPct);
        ImGui_ProgressBar(ctx, frac, &w, &h, pct);
    }

    // Install + the clash confirm live here now, on the detail page.
    ImGui_Spacing(ctx);
    const bool downloading = g_dlReq != 0;
    if (g_pendingClash) {
        ImGui_TextColored(ctx, g_installColor, g_installStatus.c_str());
        ImGui_TextWrapped(ctx, "You already have a mapping for this plug-in. "
                               "Replacing discards your version — there is no undo.");
        if (ImGui_Button(ctx, "Replace mine##exch_replace", nullptr, nullptr)) {
            g_pendingClash = false;
            applyInstall(std::move(g_pendingInstall));
            g_pendingInstall = {};
        }
        ImGui_SameLine(ctx, nullptr, nullptr);
        if (ImGui_Button(ctx, "Keep mine##exch_keep", nullptr, nullptr)) {
            g_pendingClash = false;
            g_pendingInstall = {};
            g_installStatus.clear();
        }
    } else {
        if (ImGui_Button(ctx, downloading ? "Installing…##exch_map_inst"
                                          : "Install this mapping##exch_map_inst",
                         nullptr, nullptr) && !downloading)
            startInstall(d.id);
        if (!g_installStatus.empty()) {
            ImGui_SameLine(ctx, nullptr, nullptr);
            ImGui_TextColored(ctx, g_installColor ? g_installColor : 0xCCCCCCFF,
                              g_installStatus.c_str());
        }
    }

    ImGui_Spacing(ctx);
    ImGui_Separator(ctx);
    ImGui_Spacing(ctx);

    if (d.isUf8) {
        drawUf8Grids(ctx, d);
    } else {
        // The control -> parameter table, grouped by section. Bound controls
        // show their parameter; unbound ones are dimmed with a dash.
        for (const auto& s : d.sections) {
            char sh[96];
            std::snprintf(sh, sizeof(sh), "%s   %d/%d", s.name.c_str(), s.hit, s.total);
            ImGui_Text(ctx, sh);
            char tid[64];
            std::snprintf(tid, sizeof(tid), "##exch_sec_%s", s.name.c_str());
            int tFlags = ImGui_TableFlags_RowBg | ImGui_TableFlags_BordersInnerH;
            if (ImGui_BeginTable(ctx, tid, 2, &tFlags, nullptr, nullptr, nullptr)) {
                int stretch = ImGui_TableColumnFlags_WidthStretch;
                double wA = 1.0, wB = 2.0;
                ImGui_TableSetupColumn(ctx, "Control",   &stretch, &wA, nullptr);
                ImGui_TableSetupColumn(ctx, "Parameter", &stretch, &wB, nullptr);
                for (const auto& c : s.cells) {
                    ImGui_TableNextRow(ctx, nullptr, nullptr);
                    ImGui_TableSetColumnIndex(ctx, 0);
                    if (c.bound) ImGui_Text(ctx, c.control.c_str());
                    else         ImGui_TextColored(ctx, 0x808890FF, c.control.c_str());
                    ImGui_TableSetColumnIndex(ctx, 1);
                    if (c.bound) ImGui_Text(ctx, c.param.empty() ? "(bound)" : c.param.c_str());
                    else         ImGui_TextColored(ctx, 0x808890FF, "—");
                }
                ImGui_EndTable(ctx);
            }
            ImGui_Spacing(ctx);
        }
    }

    // Off-face bindings that have no control on the face — never dropped.
    if (!d.alsoMapped.empty()) {
        ImGui_Spacing(ctx);
        ImGui_Text(ctx, "Also mapped (no fixed control):");
        char row[256];
        for (const auto& a : d.alsoMapped) {
            std::snprintf(row, sizeof(row), "    linkIdx %d  ->  %s",
                          a.first, a.second.empty() ? "(bound)" : a.second.c_str());
            ImGui_Text(ctx, row);
        }
    }
    // Modifier layers — one table per modifier (Option / Control /
    // Control+Option), in the surface's fixed layer order.
    if (!d.modLayers.empty()) {
        struct Layer { const char* key; const char* title; };
        static const Layer kLayers[] = {
            { "option",     "Option" },
            { "control",    "Control" },
            { "ctrlOption", "Control + Option" },
        };
        ImGui_Spacing(ctx);
        ImGui_Text(ctx, "Modifier layers");
        for (const auto& L : kLayers) {
            int count = 0;
            for (const auto& m : d.modLayers) if (m.layer == L.key) ++count;
            if (!count) continue;

            ImGui_Spacing(ctx);
            ImGui_Text(ctx, L.title);
            char tid[64];
            std::snprintf(tid, sizeof(tid), "##exch_mod_%s", L.key);
            int tFlags = ImGui_TableFlags_RowBg | ImGui_TableFlags_BordersInnerH;
            if (ImGui_BeginTable(ctx, tid, 2, &tFlags, nullptr, nullptr, nullptr)) {
                int stretch = ImGui_TableColumnFlags_WidthStretch;
                double wA = 1.0, wB = 2.0;
                ImGui_TableSetupColumn(ctx, "Control",   &stretch, &wA, nullptr);
                ImGui_TableSetupColumn(ctx, "Parameter", &stretch, &wB, nullptr);
                for (const auto& m : d.modLayers) {
                    if (m.layer != L.key) continue;
                    ImGui_TableNextRow(ctx, nullptr, nullptr);
                    ImGui_TableSetColumnIndex(ctx, 0);
                    ImGui_Text(ctx, m.control.c_str());
                    ImGui_TableSetColumnIndex(ctx, 1);
                    ImGui_Text(ctx, m.param.empty() ? "(bound)" : m.param.c_str());
                }
                ImGui_EndTable(ctx);
            }
        }
    }
}

} // namespace

void ExchangeView::draw(ImGui_Context* ctx) {
    loadOnce();
    pumpHealth();
    pumpList();
    pumpPlugin();
    pumpMap();
    pumpDownload();

    ImGui_Text(ctx, "Mapping Exchange");
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (ImGui_Button(ctx, g_showSettings ? "Hide server settings##exch_cfg"
                                         : "Server settings##exch_cfg", nullptr, nullptr))
        g_showSettings = !g_showSettings;

    if (g_showSettings) {
        ImGui_Spacing(ctx);
        drawSettings(ctx);
    }
    ImGui_Spacing(ctx);
    ImGui_Separator(ctx);
    ImGui_Spacing(ctx);

    // Auto-load the list the first time the tab is opened.
    if (!g_listedOnce && !g_listReq) fetchList();

    if      (g_view == View::List)   drawList(ctx);
    else if (g_view == View::Plugin) drawPlugin(ctx);
    else                             drawMap(ctx);
}

} // namespace uf8
