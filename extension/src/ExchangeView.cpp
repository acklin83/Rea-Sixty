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
enum class View { List, Plugin };
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

    char line[512];
    for (const auto& p : g_plugins) {
        std::snprintf(line, sizeof(line), "%s##exch_p_%s",
                      p.name.c_str(), p.slug.c_str());
        if (ImGui_Button(ctx, line, nullptr, nullptr))
            openPlugin(p.slug);
        ImGui_SameLine(ctx, nullptr, nullptr);
        char meta[384];
        std::snprintf(meta, sizeof(meta), "   %s · %d map%s · %s · best %d%%",
                      p.vendor.empty() ? "—" : p.vendor.c_str(),
                      p.mapCount, p.mapCount == 1 ? "" : "s",
                      p.surfaces.empty() ? "—" : p.surfaces.c_str(),
                      p.bestCoverage);
        ImGui_Text(ctx, meta);
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

    // Replace-confirm for a map that clashes with one already in the catalog.
    if (g_pendingClash) {
        ImGui_TextColored(ctx, g_installColor, g_installStatus.c_str());
        ImGui_TextWrapped(ctx, "Replacing discards your version — there is no undo.");
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
        ImGui_Spacing(ctx);
        ImGui_Separator(ctx);
        ImGui_Spacing(ctx);
    } else if (!g_installStatus.empty()) {
        ImGui_TextColored(ctx, g_installColor, g_installStatus.c_str());
        ImGui_Spacing(ctx);
    }

    char btn[64], meta[512];
    for (const auto& m : g_maps) {
        const bool downloading = g_dlReq && g_dlMapId == m.id;
        std::snprintf(btn, sizeof(btn), "%s##exch_dl_%d",
                      downloading ? "Installing…" : "Download & install", m.id);
        // Disable further clicks while any download is in flight or a clash
        // is awaiting the user's decision.
        const bool blocked = g_dlReq != 0 || g_pendingClash;
        if (ImGui_Button(ctx, btn, nullptr, nullptr) && !blocked)
            startInstall(m.id);
        ImGui_SameLine(ctx, nullptr, nullptr);
        std::snprintf(meta, sizeof(meta), "   by %s · %s · %d/%d (%d%%)%s",
                      m.author.empty() ? "—" : m.author.c_str(),
                      m.surfaces.c_str(), m.covN, m.covD, m.covPct,
                      m.works > 0 ? ("  · " + std::to_string(m.works) + " works-for-me").c_str() : "");
        ImGui_Text(ctx, meta);
        if (!m.description.empty()) {
            ImGui_Text(ctx, "      ");
            ImGui_SameLine(ctx, nullptr, nullptr);
            ImGui_TextWrapped(ctx, m.description.c_str());
        }
    }
}

} // namespace

void ExchangeView::draw(ImGui_Context* ctx) {
    loadOnce();
    pumpHealth();
    pumpList();
    pumpPlugin();
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

    if (g_view == View::List) drawList(ctx);
    else                      drawPlugin(ctx);
}

} // namespace uf8
