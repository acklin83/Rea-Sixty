#include "ExchangeView.h"

#include <cstdint>
#include <cstring>
#include <string>

// ReaImGui bindings — declarations only; REAIMGUIAPI_IMPLEMENT lives in
// MixerWindow.cpp (one TU materialises the lazy-resolved function storage).
#include "reaper_imgui_functions.h"

// REAPER host API — GetExtState / SetExtState for the persisted settings.
#include "reaper_plugin_functions.h"

#include "HttpClient.h"

namespace uf8 {
namespace {

// Persisted settings. Section matches the rest of the extension's ExtState use.
constexpr const char* kExtSection   = "rea_sixty";
constexpr const char* kKeyApiUrl    = "exchange_api_url";
constexpr const char* kKeyApiToken  = "exchange_token";

// Default to loopback so the connection test works against a locally-run
// server before api.reasixty.com is deployed. Swap to the real host once DNS
// and Caddy are up.
constexpr const char* kDefaultApiUrl = "http://127.0.0.1:8010";

char g_apiUrl[512]  = {0};
char g_token[256]   = {0};
bool g_loaded       = false;

// Connection-test state machine. One in-flight request at a time is plenty for
// a manual "Test" button.
uint64_t    g_healthReq   = 0;      // 0 = idle
std::string g_healthResult;         // last outcome, shown under the button
int         g_healthColor = 0;      // 0 = default, else RGBA

void loadOnce() {
    if (g_loaded) return;
    g_loaded = true;
    const char* url = GetExtState(kExtSection, kKeyApiUrl);
    std::snprintf(g_apiUrl, sizeof(g_apiUrl), "%s",
                  (url && *url) ? url : kDefaultApiUrl);
    const char* tok = GetExtState(kExtSection, kKeyApiToken);
    std::snprintf(g_token, sizeof(g_token), "%s", tok ? tok : "");
}

std::string trimSlash(const char* url) {
    std::string s = url ? url : "";
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

// Poll the in-flight health request, if any, and stash the outcome.
void pumpHealth() {
    if (!g_healthReq) return;
    reasixty::http::Response r;
    if (!reasixty::http::poll(g_healthReq, r)) return;   // still in flight
    g_healthReq = 0;
    if (!r.error.empty()) {
        g_healthResult = "Could not reach the server: " + r.error;
        g_healthColor  = 0xCC4444FF;
    } else if (r.status == 200) {
        g_healthResult = "Connected — server is up.";
        g_healthColor  = 0x44AA44FF;
    } else {
        g_healthResult = "Server answered with HTTP " + std::to_string(r.status);
        g_healthColor  = 0xCC8844FF;
    }
}

} // namespace

void ExchangeView::draw(ImGui_Context* ctx) {
    loadOnce();
    pumpHealth();

    ImGui_Text(ctx, "Mapping Exchange");
    ImGui_Spacing(ctx);
    ImGui_TextWrapped(ctx,
        "Browse, download and share plug-in mappings without exporting files. "
        "Browsing and downloading need no account; uploading needs a device "
        "token you paste in below (get one from your account on the website).");
    ImGui_Spacing(ctx);
    ImGui_Separator(ctx);
    ImGui_Spacing(ctx);

    // --- Server URL -------------------------------------------------------
    ImGui_Text(ctx, "Server");
    ImGui_SetNextItemWidth(ctx, -1.0);
    if (ImGui_InputText(ctx, "##exch_url", g_apiUrl, sizeof(g_apiUrl),
                        nullptr, nullptr)) {
        SetExtState(kExtSection, kKeyApiUrl, g_apiUrl, true);
    }

    // --- Device token (masked) -------------------------------------------
    ImGui_Spacing(ctx);
    ImGui_Text(ctx, "Device token (for uploads)");
    int tokFlags = ImGui_InputTextFlags_Password;
    ImGui_SetNextItemWidth(ctx, -1.0);
    if (ImGui_InputText(ctx, "##exch_token", g_token, sizeof(g_token),
                        &tokFlags, nullptr)) {
        SetExtState(kExtSection, kKeyApiToken, g_token, true);
    }

    ImGui_Spacing(ctx);
    ImGui_Separator(ctx);
    ImGui_Spacing(ctx);

    // --- Connection test --------------------------------------------------
    const bool busy = g_healthReq != 0;
    if (ImGui_Button(ctx, busy ? "Testing…##exch_test" : "Test connection##exch_test",
                     nullptr, nullptr) && !busy) {
        const std::string url = trimSlash(g_apiUrl) + "/health";
        g_healthResult.clear();
        g_healthReq = reasixty::http::begin("GET", url);
        if (!g_healthReq) {
            g_healthResult = "Bad server URL.";
            g_healthColor  = 0xCC4444FF;
        }
    }

    if (!g_healthResult.empty()) {
        ImGui_Spacing(ctx);
        if (g_healthColor)
            ImGui_TextColored(ctx, g_healthColor, g_healthResult.c_str());
        else
            ImGui_Text(ctx, g_healthResult.c_str());
    }
}

} // namespace uf8
