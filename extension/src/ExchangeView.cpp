#include "ExchangeView.h"

#include <algorithm>
#include <chrono>
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
#include "JsonTree.h"

// jsonparse.h drags in wdlcstring.h, which on MSVC does `#define snprintf
// WDL_snprintf` to paper over pre-2015 MSVC's non-terminating _snprintf. Every
// `std::snprintf` below that point then macro-expands to `std::WDL_snprintf`,
// which does not exist — the Windows build died on 21 of them. We build with
// MSVC 2022, whose std::snprintf is conforming, so drop the shim in this TU.
// No other file hits this: it is the only one that includes jsonparse.h.
#ifdef snprintf
#undef snprintf
#endif

// Defined in main.cpp. Declared here rather than in a header for the same
// reason ManualView.cpp and SettingsScreen.cpp do it: one free function, no
// header worth creating for it.
void reasixty_openUrl(const char* url);

namespace uf8 {
namespace {

namespace up = uf8::user_plugins;
namespace http = reasixty::http;

constexpr const char* kExtSection  = "rea_sixty";
constexpr const char* kKeyApiUrl   = "exchange_api_url";
constexpr const char* kKeyApiToken = "exchange_token";
// Production exchange. Shipped users never touch the URL field — it must point
// at the live server, not a dev loopback nobody is running. For local testing,
// set exchange_api_url to http://127.0.0.1:8010 in Settings → Exchange.
constexpr const char* kDefaultApiUrl = "https://api.reasixty.com";

char g_apiUrl[512] = {0};
char g_token[256]  = {0};
char g_search[128] = {0};
bool g_loaded      = false;
bool g_showSettings = false;

// Live search: refetch a short beat after the last keystroke, not on Enter.
bool g_searchDirty = false;
std::chrono::steady_clock::time_point g_searchDirtyAt;

// Sort, driven by the index table's clickable headers.
std::string g_sort = "name";
bool        g_sortAsc = true;
bool        g_sortPending = false;   // a header changed the sort; refetch when free

// ---- connection test ------------------------------------------------------
uint64_t    g_healthReq = 0;
std::string g_healthResult;
int         g_healthColor = 0;

// ---- browse ---------------------------------------------------------------
enum class View { List, Plugin, Map };
View g_view = View::List;

struct PluginRow {
    std::string slug, name, vendor, surfaces;
    int mapCount = 0, bestCoverage = 0, bestParamCoverage = -1;
};
std::vector<PluginRow> g_plugins;
uint64_t    g_listReq = 0;
std::string g_listError;
bool        g_listedOnce = false;

struct MapRow {
    int id = 0;
    std::string author, surfaces, domain, description;
    int covN = 0, covD = 0, covPct = 0, works = 0;
    int paramN = -1, paramD = -1, paramPct = 0;   // paramD < 0 = not available
};
std::string g_pluginSlug, g_pluginName, g_pluginVendor;
std::vector<MapRow> g_maps;
uint64_t    g_pluginReq = 0;
std::string g_pluginError;

// ---- one map's detail (screen 3): the control -> parameter mappings --------
struct CtrlCell { std::string control, param; bool bound = false; };
struct Section  { std::string name; int total = 0, hit = 0; std::vector<CtrlCell> cells; };
struct Uf8Vpot  { int faderBank = 0, vpotBank = 0, strip = 0; std::string label, param, mode; };
struct Uf8Strip { int faderBank = 0, strip = 0; std::string kind, param; };
struct ModBind  { std::string layer, control, param; };
struct ExtFunc  { std::string name, param; };   // UC1 EXT FUNCS (hidden BACK-menu)
// One bound UF1 plugin-mode position. kind is "vpot" or "softkey"; page/idx
// are derived server-side from the flat pos (4 per page, both streams).
struct Uf1Slot  { int pos = 0, page = 0, idx = 0; bool inverted = false;
                  std::string kind, label, param; };
struct MapDetail {
    int id = 0;
    std::string pluginName, vendor, surfaces, domain, author, description, licence;
    int covN = 0, covD = 0, covPct = 0;
    int paramN = -1, paramD = -1, paramPct = 0;   // paramD < 0 = not available
    bool isUf8 = false;
    int uf8Vpots = 0, uf8VpotSlots = 0, uf8Strips = 0, uf8StripSlots = 0;
    std::vector<Section> sections;
    std::vector<Uf8Vpot>  uf8VpotBindings;
    std::vector<Uf8Strip> uf8StripBindings;
    std::vector<std::pair<int, std::string>> alsoMapped;              // linkIdx, param
    std::vector<ModBind> modLayers;
    std::vector<ExtFunc> extFuncs;                                    // UC1 EXT FUNCS
    // The explicit UF1 layer. Carried for EVERY domain — a UC1+UF1 map keeps
    // its CS/BC domain, so gating this the way uf8 is gated would hide the
    // half the author meant to share. Empty = the map has no UF1 layer.
    std::vector<Uf1Slot> uf1Slots;
    // "Works for me" — the count everyone sees, and whether THIS account is
    // one of them. -1 means the server could not say because we sent no token,
    // which is NOT the same as "no": showing an un-pressed button to someone
    // with no credential would invite a click that can only fail.
    int works = 0;
    int worksMine = -1;
};
MapDetail   g_mapDetail;
uint64_t    g_mapReq = 0;
std::string g_mapError;
int         g_uf8FaderSel = 0;   // which fader bank the UF8 view shows

// ---- install --------------------------------------------------------------
uint64_t    g_dlReq = 0;
int         g_dlMapId = 0;
std::string g_installStatus;
int         g_installColor = 0;
up::MapShare g_pendingInstall;     // downloaded, awaiting a replace-confirm
bool        g_pendingClash = false;

// ---- "works for me" -------------------------------------------------------
// The only write the browse surface performs. It needs the upload token: the
// API accepts a session cookie OR a bearer token, and an ImGui window has no
// cookies.
uint64_t    g_worksReq = 0;
bool        g_worksUndo = false;          // which way the in-flight call goes
std::string g_worksStatus;
int         g_worksColor = 0;
char        g_worksVersion[64] = {0};     // plug-in version being confirmed

// ---- linking this machine -------------------------------------------------
// The extension cannot run a browser, so it cannot sign in. It asks the server
// for a grant, shows a short code, and collects the token itself once a human
// has approved it somewhere they ARE signed in. Nothing is copied by hand and
// no credential is ever mailed.
uint64_t    g_linkStartReq = 0;
uint64_t    g_linkPollReq  = 0;
std::string g_linkDeviceCode;             // secret; never shown on screen
std::string g_linkUserCode;               // short; the whole point of showing it
std::string g_linkUrl;
std::string g_linkStatus;
int         g_linkColor = 0;
std::chrono::steady_clock::time_point g_linkNextPoll;
std::chrono::steady_clock::time_point g_linkExpires;
bool        g_linking = false;

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
    if (!v) return "";
    const char* s = v->get_string_value(true);
    if (!s) return "";
    // A JSON null arrives as the UNQUOTED literal "null" (m_value_string=false),
    // NOT as an absent field or an empty string — wdl_json has no is_null(). Left
    // as "null" it defeats every `.empty()`/`jint` guard downstream: atoi("null")
    // is 0, so a null bestParamCoverage rendered as "0%" on the plug-in index
    // instead of "—" (Frank's UF8-only "delta 16" map, 2026-07-21). A genuine
    // quoted "null" string keeps m_value_string=true and is preserved.
    if (!v->m_value_string && std::strcmp(s, "null") == 0) return "";
    return std::string(s);
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
    url += "&sort=" + g_sort + "&dir=" + (g_sortAsc ? "asc" : "desc");
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
    g_worksStatus.clear();
    g_worksColor = 0;
    g_mapError.clear();
    g_installStatus.clear();
    g_pendingClash = false;
    g_uf8FaderSel = 0;               // start on fader bank 1 for each map
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
// Confirm (or withdraw) "this mapping works for me". `undo` sends DELETE.
//
// The version is optional and is the entire point of the feature: with a
// handful of votes a star average is noise, while "worked on 2.1.4" is what
// actually helps the next person. It is typed rather than detected — REAPER
// exposes no plug-in version string, and inventing one would be worse than
// leaving it blank.
// Ask the server for a grant. The reply carries a short code for the human and
// a long one for us; only the short one ever appears on screen.
void startLink() {
    if (g_linkStartReq || g_linkPollReq) return;
    g_linkStatus.clear();
    g_linkColor = 0;
    g_linkUserCode.clear();
    g_linkDeviceCode.clear();
    g_linkUrl.clear();

    const std::vector<std::string> headers = { "Content-Type: application/json" };
    g_linkStartReq = http::begin("POST", baseUrl() + "/auth/device/start",
                                 headers, "{\"label\":\"REAPER\"}");
    if (!g_linkStartReq) {
        g_linkStatus = "Bad server URL.";
        g_linkColor = 0xCC4444FF;
        return;
    }
    g_linking = true;
    g_linkStatus = "Asking the server…";
}

void stopLink(const std::string& msg, int color) {
    if (g_linkPollReq)  { http::cancel(g_linkPollReq);  g_linkPollReq = 0; }
    if (g_linkStartReq) { http::cancel(g_linkStartReq); g_linkStartReq = 0; }
    g_linking = false;
    g_linkDeviceCode.clear();
    g_linkUserCode.clear();
    g_linkUrl.clear();
    g_linkStatus = msg;
    g_linkColor = color;
}

void pumpLinkStart() {
    if (!g_linkStartReq) return;
    http::Response r;
    if (!http::poll(g_linkStartReq, r)) return;
    g_linkStartReq = 0;

    if (!r.error.empty()) { stopLink("Could not reach the server: " + r.error, 0xCC4444FF); return; }
    if (r.status != 200)  { stopLink("Server returned HTTP " + std::to_string(r.status), 0xCC4444FF); return; }

    wdl_json_parser p;
    wdl_json_element* root = p.parse(r.body.c_str(), (int)r.body.size());
    JsonTreeGuard rootGuard{p, root};
    if (!root || !root->is_object()) { stopLink("Bad response from server.", 0xCC4444FF); return; }

    g_linkDeviceCode = jstr(root, "deviceCode");
    g_linkUserCode   = jstr(root, "userCode");
    g_linkUrl        = jstr(root, "verificationUriComplete");
    const int ttl    = jint(root, "expiresIn");
    if (g_linkDeviceCode.empty() || g_linkUserCode.empty()) {
        stopLink("Bad response from server.", 0xCC4444FF);
        return;
    }

    const auto nowT = std::chrono::steady_clock::now();
    g_linkExpires  = nowT + std::chrono::seconds(ttl > 0 ? ttl : 600);
    g_linkNextPoll = nowT;                 // first poll immediately
    g_linkStatus   = "Waiting for you to approve it in the browser…";
    g_linkColor    = 0;

    if (!g_linkUrl.empty()) reasixty_openUrl(g_linkUrl.c_str());
}

// Poll on a timer rather than every frame: the UI runs at REAPER's timer rate,
// and hammering the endpoint would be both rude and rate-limited.
void pumpLink() {
    pumpLinkStart();
    if (!g_linking || g_linkDeviceCode.empty()) return;

    const auto nowT = std::chrono::steady_clock::now();
    if (nowT >= g_linkExpires) {
        stopLink("That code expired. Start again.", 0xCC4444FF);
        return;
    }

    if (!g_linkPollReq) {
        if (nowT < g_linkNextPoll) return;
        const std::vector<std::string> headers = { "Content-Type: application/json" };
        g_linkPollReq = http::begin("POST", baseUrl() + "/auth/device/poll", headers,
                                    "{\"deviceCode\":\"" + g_linkDeviceCode + "\"}");
        g_linkNextPoll = nowT + std::chrono::seconds(2);
        return;
    }

    http::Response r;
    if (!http::poll(g_linkPollReq, r)) return;
    g_linkPollReq = 0;

    if (!r.error.empty()) return;          // transient; the next tick retries

    wdl_json_parser p;
    wdl_json_element* root = p.parse(r.body.c_str(), (int)r.body.size());
    JsonTreeGuard rootGuard{p, root};
    const std::string state = (root && root->is_object()) ? jstr(root, "status") : "";

    if (state == "pending") return;
    if (state == "denied")  { stopLink("Refused in the browser.", 0xCC4444FF); return; }
    if (state == "expired") { stopLink("That code expired. Start again.", 0xCC4444FF); return; }
    if (state != "ok") {
        stopLink("Linking failed (HTTP " + std::to_string(r.status) + ").", 0xCC4444FF);
        return;
    }

    const std::string token = jstr(root, "token");
    if (token.empty()) { stopLink("The server sent no token.", 0xCC4444FF); return; }

    std::snprintf(g_token, sizeof(g_token), "%s", token.c_str());
    SetExtState(kExtSection, kKeyApiToken, g_token, true);

    const std::string who = jstr(root, "displayName");
    stopLink(who.empty() ? "Linked. You can upload now."
                         : "Linked as " + who + ". You can upload now.", 0);
}

void toggleWorks(int mapId, bool undo) {
    if (g_worksReq) return;
    g_worksStatus.clear();
    g_worksColor = 0;

    if (!g_token[0]) {
        g_worksStatus = "Paste your device token under \"Server settings\" first.";
        g_worksColor = 0xCC4444FF;
        return;
    }

    const std::vector<std::string> headers = {
        std::string("Authorization: Bearer ") + g_token,
        "Content-Type: application/json",
    };
    const std::string url = baseUrl() + "/v1/maps/" + std::to_string(mapId) + "/works";

    std::string body;
    if (!undo) {
        // Hand-built rather than pulled in a JSON writer for one field. The
        // version is user text, so the quotes and backslashes it may contain
        // have to be escaped or the request is malformed.
        std::string v;
        for (const char* c = g_worksVersion; *c; ++c) {
            if (*c == '"' || *c == '\\') v += '\\';
            if ((unsigned char)*c >= 0x20) v += *c;
        }
        body = "{\"pluginVersion\":\"" + v + "\"}";
    }

    g_worksUndo = undo;
    g_worksReq = http::begin(undo ? "DELETE" : "POST", url, headers, body);
    if (!g_worksReq) {
        g_worksStatus = "Bad server URL.";
        g_worksColor = 0xCC4444FF;
    }
}

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
    JsonTreeGuard rootGuard{p, root};
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
            // null when no map carries a functional param count (pre-v3).
            pr.bestParamCoverage = jstr(o, "bestParamCoverage").empty()
                                 ? -1 : jint(o, "bestParamCoverage");
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
    JsonTreeGuard rootGuard{p, root};
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
            if (auto* pc = o->get_item_by_name("paramCoverage"); pc && pc->is_object()) {
                mr.paramN = jint(pc, "n"); mr.paramD = jint(pc, "d"); mr.paramPct = jint(pc, "pct");
            }
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
    JsonTreeGuard rootGuard{p, root};
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
    if (auto* pc = root->get_item_by_name("paramCoverage"); pc && pc->is_object()) {
        d.paramN = jint(pc, "n"); d.paramD = jint(pc, "d"); d.paramPct = jint(pc, "pct");
    }
    d.works = jint(root, "works");
    // worksMine is a TRISTATE and the JSON carries it as true / false / null.
    // wdl_json has no is_null(): m_value holds the raw token, so a JSON null
    // reads back as the literal string "null". Matching on the three spellings
    // is the whole check — jbool() alone would fold null into false and offer
    // an un-pressed button to someone with no token, i.e. a guaranteed 401.
    const std::string wm = jstr(root, "worksMine");
    if      (wm == "true")  d.worksMine = 1;
    else if (wm == "false") d.worksMine = 0;
    else                    d.worksMine = -1;   // "null", or the field is absent
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
                v.mode = jstr(o, "mode");
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
    if (auto* u1 = root->get_item_by_name("uf1"); u1 && u1->is_array() && u1->m_array) {
        for (int i = 0; i < u1->m_array->GetSize(); ++i) {
            wdl_json_element* o = u1->enum_item(i);
            if (!o || !o->is_object()) continue;
            Uf1Slot u;
            u.kind  = jstr(o, "kind");
            u.pos   = jint(o, "pos");
            u.page  = jint(o, "page");
            u.idx   = jint(o, "idx");
            u.label = jstr(o, "label");
            u.param = jstr(o, "param");
            u.inverted = jstr(o, "inverted") == "true";
            d.uf1Slots.push_back(std::move(u));
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
    if (auto* ef = root->get_item_by_name("extFuncs"); ef && ef->is_array() && ef->m_array) {
        for (int i = 0; i < ef->m_array->GetSize(); ++i) {
            wdl_json_element* o = ef->enum_item(i);
            if (!o || !o->is_object()) continue;
            d.extFuncs.push_back({ jstr(o, "name"), jstr(o, "param") });
        }
    }
    g_mapDetail = std::move(d);
}

void pumpWorks() {
    if (!g_worksReq) return;
    http::Response r;
    if (!http::poll(g_worksReq, r)) return;
    g_worksReq = 0;

    if (!r.error.empty()) {
        g_worksStatus = "Could not reach the server: " + r.error;
        g_worksColor = 0xCC4444FF;
        return;
    }
    if (r.status == 401) {
        g_worksStatus = "That device token was refused. Check it under \"Server settings\".";
        g_worksColor = 0xCC4444FF;
        return;
    }
    if (r.status == 403) {
        g_worksStatus = "This account cannot post.";
        g_worksColor = 0xCC4444FF;
        return;
    }
    if (r.status != 200) {
        g_worksStatus = "Server returned HTTP " + std::to_string(r.status);
        g_worksColor = 0xCC4444FF;
        return;
    }

    // The reply carries the fresh count, so the screen updates without
    // refetching the whole map.
    wdl_json_parser p;
    wdl_json_element* root = p.parse(r.body.c_str(), (int)r.body.size());
    JsonTreeGuard rootGuard{p, root};
    if (root && root->is_object()) g_mapDetail.works = jint(root, "worksCount");
    g_mapDetail.worksMine = g_worksUndo ? 0 : 1;

    g_worksStatus = g_worksUndo ? "Withdrawn." : "Thanks — noted.";
    g_worksColor = 0;
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

    // ---- link this machine ------------------------------------------------
    // The whole reason the field above can stay empty. Pressing this opens the
    // browser, you confirm once, and the token arrives here by itself — it is
    // never copied, never in the clipboard and never in an email.
    ImGui_Spacing(ctx);
    if (!g_linking) {
        if (ImGui_Button(ctx, g_token[0] ? "Link this machine again…##exch_link"
                                         : "Link this machine…##exch_link",
                         nullptr, nullptr))
            startLink();
        ImGui_SameLine(ctx, nullptr, nullptr);
        ImGui_TextColored(ctx, 0x999999FF, "opens your browser — no copying");
    } else {
        char code[96];
        std::snprintf(code, sizeof(code), "Code:  %s", g_linkUserCode.c_str());
        ImGui_Text(ctx, g_linkUserCode.empty() ? "Starting…" : code);
        ImGui_SameLine(ctx, nullptr, nullptr);
        if (!g_linkUrl.empty()
            && ImGui_Button(ctx, "Open the page again##exch_link_open", nullptr, nullptr))
            reasixty_openUrl(g_linkUrl.c_str());
        ImGui_SameLine(ctx, nullptr, nullptr);
        if (ImGui_Button(ctx, "Cancel##exch_link_cancel", nullptr, nullptr))
            stopLink("Cancelled.", 0);
    }
    if (!g_linkStatus.empty()) {
        ImGui_TextColored(ctx, g_linkColor ? g_linkColor : 0xCCCCCCFF, g_linkStatus.c_str());
    }

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
    // Live search — filter as you type, debounced so a fast typist doesn't fire
    // a request per keystroke.
    ImGui_SetNextItemWidth(ctx, 260.0);
    if (ImGui_InputText(ctx, "##exch_search", g_search, sizeof(g_search), nullptr, nullptr)) {
        g_searchDirty = true;
        g_searchDirtyAt = std::chrono::steady_clock::now();
    }
    if (g_searchDirty && !g_listReq) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - g_searchDirtyAt).count();
        if (ms >= 180) { g_searchDirty = false; fetchList(); }
    }
    // A header click changed the sort — refetch as soon as no request is in flight.
    if (g_sortPending && !g_listReq) { g_sortPending = false; fetchList(); }
    ImGui_SameLine(ctx, nullptr, nullptr);
    if (ImGui_Button(ctx, g_listReq ? "Loading…##exch_refresh" : "Refresh##exch_refresh", nullptr, nullptr) && !g_listReq)
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
                                         : "No plug-ins found.");
        return;
    }

    // One row per plug-in. Click a row to open its maps; click a column header
    // to sort. ScrollY + a fixed outer height keeps the header sticky.
    double availW = 0.0, availH = 0.0;
    ImGui_GetContentRegionAvail(ctx, &availW, &availH);
    if (availH < 160.0) availH = 160.0;   // never collapse in a short pane
    int tFlags = ImGui_TableFlags_RowBg | ImGui_TableFlags_Borders
               | ImGui_TableFlags_Resizable | ImGui_TableFlags_Hideable
               | ImGui_TableFlags_ScrollY;
    if (ImGui_BeginTable(ctx, "##exch_plugins", 5, &tFlags, nullptr, &availH, nullptr)) {
        int stretch = ImGui_TableColumnFlags_WidthStretch;
        int fixed   = ImGui_TableColumnFlags_WidthFixed;
        double wName = 3.0, wVendor = 2.0, wMaps = 46.0, wSurf = 100.0, wCov = 100.0;
        ImGui_TableSetupColumn(ctx, "Plug-in",  &stretch, &wName,   nullptr);
        ImGui_TableSetupColumn(ctx, "Vendor",   &stretch, &wVendor, nullptr);
        ImGui_TableSetupColumn(ctx, "Maps",     &fixed,   &wMaps,   nullptr);
        ImGui_TableSetupColumn(ctx, "Surfaces", &fixed,   &wSurf,   nullptr);
        ImGui_TableSetupColumn(ctx, "Coverage", &fixed,   &wCov,    nullptr);

        // Custom clickable header row — ReaImGui's built-in Sortable never
        // drove our refetch, so we own the sort: click a column to sort by it,
        // click the same one again to flip direction. The active column shows
        // ^ (ascending) or v (descending).
        struct Hdr { int col; const char* label; const char* key; };
        static const Hdr kHdr[5] = {
            { 0, "Plug-in", "name" }, { 1, "Vendor", "vendor" }, { 2, "Maps", "maps" },
            { 3, "Surfaces", nullptr }, { 4, "Coverage", "coverage" },
        };
        int headerRow = ImGui_TableRowFlags_Headers;
        ImGui_TableNextRow(ctx, &headerRow, nullptr);
        for (const auto& h : kHdr) {
            ImGui_TableSetColumnIndex(ctx, h.col);
            if (!h.key) { ImGui_TableHeader(ctx, h.label); continue; }
            char lbl[64];
            const char* arrow = (g_sort == h.key) ? (g_sortAsc ? " ^" : " v") : "";
            std::snprintf(lbl, sizeof(lbl), "%s%s##hdr%d", h.label, arrow, h.col);
            if (ImGui_Selectable(ctx, lbl, nullptr, nullptr, nullptr, nullptr)) {
                if (g_sort == h.key) g_sortAsc = !g_sortAsc;
                else { g_sort = h.key; g_sortAsc = true; }
                g_sortPending = true;
            }
        }

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
            if (p.bestParamCoverage < 0) {
                ImGui_Text(ctx, "—");
            } else {
                // Plain number, no bar. The index row only carries a
                // percentage — n/d exists per MAP, and this row is one
                // PLUG-IN, which may have several with different totals.
                char lbl[16]; std::snprintf(lbl, sizeof(lbl), "%d%%", p.bestParamCoverage);
                ImGui_Text(ctx, lbl);
            }
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
            if (m.paramD < 0) {
                ImGui_Text(ctx, "—");
            } else {
                std::snprintf(buf, sizeof(buf), "%d/%d", m.paramN, m.paramD);
                ImGui_Text(ctx, buf);
            }

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
// Value / StepCycle / Toggle -> a short lower-case tag next to the V-Pot param.
const char* vpotModeTag(const std::string& m) {
    if (m == "Toggle")    return "toggle";
    if (m == "StepCycle") return "step";
    return "turn";
}

void drawUf8Grids(ImGui_Context* ctx, const MapDetail& d) {
    // The UF8 is 8 channel strips side by side. One fader bank shown at a time
    // (like the hardware's BANK button): strips are COLUMNS, strip controls
    // (Fader/Solo/Mute/Sel) are the first rows, then one row per V-Pot bank.
    // A selector switches to the second fader bank when it carries bindings.
    const char* kKinds[4] = { "fader", "solo", "cut", "sel" };
    const char* kKindLabel[4] = { "Fader", "Solo", "Mute", "Sel" };

    const Uf8Strip* strip[2][8][4] = {};
    for (const auto& s : d.uf8StripBindings) {
        int ki = -1;
        for (int k = 0; k < 4; ++k) if (s.kind == kKinds[k]) { ki = k; break; }
        if (ki >= 0 && s.faderBank < 2 && s.strip < 8) strip[s.faderBank][s.strip][ki] = &s;
    }
    const Uf8Vpot* vpot[2][8][8] = {};
    for (const auto& v : d.uf8VpotBindings)
        if (v.faderBank < 2 && v.vpotBank < 8 && v.strip < 8) vpot[v.faderBank][v.vpotBank][v.strip] = &v;

    bool has[2] = { false, false };
    for (int fb = 0; fb < 2; ++fb)
        for (int s = 0; s < 8; ++s) {
            for (int k = 0; k < 4; ++k) if (strip[fb][s][k]) has[fb] = true;
            for (int vb = 0; vb < 8; ++vb) if (vpot[fb][vb][s]) has[fb] = true;
        }
    if (!has[0] && !has[1]) return;

    // Keep the selection on a bank that exists.
    if (!has[g_uf8FaderSel]) g_uf8FaderSel = has[0] ? 0 : 1;

    // Selector — only when both fader banks carry bindings.
    if (has[0] && has[1]) {
        for (int fb = 0; fb < 2; ++fb) {
            char b[32];
            std::snprintf(b, sizeof(b), "%sFader bank %d##uf8fbsel%d",
                          fb == g_uf8FaderSel ? "▸ " : "", fb + 1, fb);
            if (ImGui_Button(ctx, b, nullptr, nullptr)) g_uf8FaderSel = fb;
            if (fb == 0) ImGui_SameLine(ctx, nullptr, nullptr);
        }
        ImGui_Spacing(ctx);
    }

    const int fb = g_uf8FaderSel;
    char h[96];
    std::snprintf(h, sizeof(h), "Fader bank %d   (strips %d–%d)",
                  fb + 1, fb * 8 + 1, fb * 8 + 8);
    ImGui_Text(ctx, h);

    int tFlags = ImGui_TableFlags_RowBg | ImGui_TableFlags_Borders
               | ImGui_TableFlags_ScrollX;
    double outW = 0.0, outH = 0.0;
    ImGui_GetContentRegionAvail(ctx, &outW, &outH);
    double th = 0.0;
    if (ImGui_BeginTable(ctx, "##uf8_fb", 9, &tFlags, &outW, &th, nullptr)) {
        int fixed = ImGui_TableColumnFlags_WidthFixed;
        double wLbl = 78.0, wCol = 116.0;
        ImGui_TableSetupColumn(ctx, "", &fixed, &wLbl, nullptr);
        for (int s = 0; s < 8; ++s) {
            char cn[12]; std::snprintf(cn, sizeof(cn), "Strip %d", fb * 8 + s + 1);
            ImGui_TableSetupColumn(ctx, cn, &fixed, &wCol, nullptr);
        }
        ImGui_TableHeadersRow(ctx);

        // Strip controls first — Fader / Solo / Mute / Sel.
        for (int k = 0; k < 4; ++k) {
            ImGui_TableNextRow(ctx, nullptr, nullptr);
            ImGui_TableSetColumnIndex(ctx, 0);
            ImGui_Text(ctx, kKindLabel[k]);
            for (int s = 0; s < 8; ++s) {
                ImGui_TableSetColumnIndex(ctx, s + 1);
                const Uf8Strip* sb = strip[fb][s][k];
                if (sb) ImGui_Text(ctx, sb->param.empty() ? "(bound)" : sb->param.c_str());
            }
        }

        // Then one row per V-Pot bank that has any binding on this fader bank.
        for (int vb = 0; vb < 8; ++vb) {
            bool anyVb = false;
            for (int s = 0; s < 8 && !anyVb; ++s) if (vpot[fb][vb][s]) anyVb = true;
            if (!anyVb) continue;
            ImGui_TableNextRow(ctx, nullptr, nullptr);
            ImGui_TableSetColumnIndex(ctx, 0);
            char rl[24]; std::snprintf(rl, sizeof(rl), "V-Pot B%d", vb + 1);
            ImGui_Text(ctx, rl);
            for (int s = 0; s < 8; ++s) {
                ImGui_TableSetColumnIndex(ctx, s + 1);
                const Uf8Vpot* v = vpot[fb][vb][s];
                if (!v) continue;
                ImGui_Text(ctx, v->param.empty() ? "(bound)" : v->param.c_str());
                ImGui_SameLine(ctx, nullptr, nullptr);
                ImGui_TextColored(ctx, 0x8892A0FF, vpotModeTag(v->mode));
                if (!v->label.empty() && ImGui_IsItemHovered(ctx, nullptr))
                    ImGui_SetTooltip(ctx, v->label.c_str());
            }
        }
        ImGui_EndTable(ctx);
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

    // Coverage: how much of the PLUG-IN the map controls (functional params).
    ImGui_Spacing(ctx);
    if (d.paramD >= 0) {
        char lbl[96]; std::snprintf(lbl, sizeof(lbl), "Coverage: %d of %d plug-in params",
                                    d.paramN, d.paramD);
        ImGui_Text(ctx, lbl);
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
            g_pendingInstall = up::MapShare();
        }
        ImGui_SameLine(ctx, nullptr, nullptr);
        if (ImGui_Button(ctx, "Keep mine##exch_keep", nullptr, nullptr)) {
            g_pendingClash = false;
            g_pendingInstall = up::MapShare();
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

    // ---- works for me -----------------------------------------------------
    // Deliberately on this screen and not the map list: confirming is a claim
    // about a mapping you have actually looked at.
    ImGui_Spacing(ctx);
    {
        char lbl[96];
        if (d.works > 0)
            std::snprintf(lbl, sizeof(lbl), "Works for %d %s",
                          d.works, d.works == 1 ? "person" : "people");
        else
            std::snprintf(lbl, sizeof(lbl), "Nobody has confirmed this yet");
        ImGui_Text(ctx, lbl);

        const bool busy = g_worksReq != 0;
        ImGui_SameLine(ctx, nullptr, nullptr);

        if (d.worksMine == 1) {
            if (ImGui_Button(ctx, busy ? "…##exch_works" : "Withdraw my confirmation##exch_works",
                             nullptr, nullptr) && !busy)
                toggleWorks(d.id, /*undo*/ true);
        } else {
            if (ImGui_Button(ctx, busy ? "…##exch_works" : "This works for me##exch_works",
                             nullptr, nullptr) && !busy)
                toggleWorks(d.id, /*undo*/ false);
            ImGui_SameLine(ctx, nullptr, nullptr);
            ImGui_SetNextItemWidth(ctx, 130.0);
            ImGui_InputTextWithHint(ctx, "##exch_works_ver", "plug-in version",
                                    g_worksVersion, sizeof(g_worksVersion), nullptr, nullptr);
        }

        if (!g_worksStatus.empty()) {
            ImGui_SameLine(ctx, nullptr, nullptr);
            ImGui_TextColored(ctx, g_worksColor ? g_worksColor : 0xCCCCCCFF,
                              g_worksStatus.c_str());
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
            ImGui_Text(ctx, s.name.c_str());
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

    // The UF1 layer, when the map carries one. Shown for every domain, next to
    // whatever the map does on the UC1 or UF8 — the point of a UF1 layer is
    // that it differs from the UC1 fill, so seeing it BEFORE taking the map is
    // the whole reason it is here. Grouped by page, the way the surface pages.
    if (!d.uf1Slots.empty()) {
        ImGui_Spacing(ctx);
        ImGui_Text(ctx, "UF1 layer");
        int lastPage = -1;
        bool tableOpen = false;
        auto closeTable = [&]() {
            if (tableOpen) { ImGui_EndTable(ctx); tableOpen = false; }
        };
        for (const auto& u : d.uf1Slots) {
            if (u.page != lastPage) {
                closeTable();
                lastPage = u.page;
                char hdr[32];
                std::snprintf(hdr, sizeof(hdr), "Page %d", u.page + 1);
                ImGui_Text(ctx, hdr);
                char tid[48];
                std::snprintf(tid, sizeof(tid), "##exch_uf1_p%d", u.page);
                int tFlags = ImGui_TableFlags_RowBg | ImGui_TableFlags_BordersInnerH;
                double wA = 1.0, wB = 2.0;
                int stretch = ImGui_TableColumnFlags_WidthStretch;
                tableOpen = ImGui_BeginTable(ctx, tid, 2, &tFlags,
                                             nullptr, nullptr, nullptr);
                if (tableOpen) {
                    ImGui_TableSetupColumn(ctx, "Control",   &stretch, &wA, nullptr);
                    ImGui_TableSetupColumn(ctx, "Parameter", &stretch, &wB, nullptr);
                }
            }
            if (!tableOpen) continue;
            ImGui_TableNextRow(ctx, nullptr, nullptr);
            ImGui_TableSetColumnIndex(ctx, 0);
            // "V-Pot 3" / "Soft-key 2" — idx is 0-based within the page.
            char ctrl[48];
            std::snprintf(ctrl, sizeof(ctrl), "%s %d",
                          u.kind == "softkey" ? "Soft-key" : "V-Pot",
                          u.idx + 1);
            ImGui_Text(ctx, ctrl);
            ImGui_TableSetColumnIndex(ctx, 1);
            // The author's own display name wins when they set one; otherwise
            // the plug-in's parameter name. Inverted is worth seeing here — it
            // changes which way the control travels.
            std::string cell = !u.label.empty() ? u.label
                             : (u.param.empty() ? std::string("(bound)") : u.param);
            if (u.inverted) cell += "  (inverted)";
            ImGui_Text(ctx, cell.c_str());
        }
        closeTable();
        ImGui_Spacing(ctx);
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
    // UC1 EXT FUNCS — the hidden BACK-menu (CS mode). Curated by the author,
    // decoupled from the face controls, so shown as its own name -> param table.
    if (!d.extFuncs.empty()) {
        ImGui_Spacing(ctx);
        ImGui_Text(ctx, "EXT FUNCS (UC1 hidden menu)");
        int tFlags = ImGui_TableFlags_RowBg | ImGui_TableFlags_BordersInnerH;
        if (ImGui_BeginTable(ctx, "##exch_extfuncs", 2, &tFlags, nullptr, nullptr, nullptr)) {
            int stretch = ImGui_TableColumnFlags_WidthStretch;
            double wA = 1.0, wB = 2.0;
            ImGui_TableSetupColumn(ctx, "Name",      &stretch, &wA, nullptr);
            ImGui_TableSetupColumn(ctx, "Parameter", &stretch, &wB, nullptr);
            for (const auto& e : d.extFuncs) {
                ImGui_TableNextRow(ctx, nullptr, nullptr);
                ImGui_TableSetColumnIndex(ctx, 0);
                ImGui_Text(ctx, e.name.empty() ? "(unnamed)" : e.name.c_str());
                ImGui_TableSetColumnIndex(ctx, 1);
                ImGui_Text(ctx, e.param.empty() ? "(bound)" : e.param.c_str());
            }
            ImGui_EndTable(ctx);
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
    pumpWorks();
    pumpLink();

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
