#include "OrcWebWindow.h"

#include "OrcFiles.h"
#include "OrcWinUi.h"
#include "SettingsServer.h"

#include <algorithm>
#include <dwmapi.h>
#include <objbase.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <string>
#include <wrl.h>
#include <WebView2.h>

#pragma comment(lib, "dwmapi.lib")

namespace set = orc::settings;
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace orc::webwindow {

namespace {

constexpr wchar_t kWindowClass[] = L"ORC.Settings";
constexpr UINT     kChooseLogo   = WM_APP + 10;
constexpr UINT_PTR kLoginTimer   = 1;
constexpr UINT_PTR kCloseTimer   = 2;
constexpr int      kWantW = 1280, kWantH = 820, kMinW = 620, kMinH = 420;
constexpr wchar_t  kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

HWND                         g_wnd = nullptr;
ComPtr<ICoreWebView2Environment>  g_env;
ComPtr<ICoreWebView2Controller>   g_controller;
ComPtr<ICoreWebView2>             g_web;
std::string                  g_shownPage;
std::wstring                 g_pendingUrl;
bool                         g_closing = false;

// ── Open at login ────────────────────────────────────────────────────────────

// The Run value is what this reads. ⚠ A user who turns ORC off in Task Manager's
// startup list leaves the value standing; that list keeps its answer under a key
// Microsoft does not document, so the page cannot tell (handbook).
bool loginIsOn()
{
    return ::RegGetValueW(HKEY_CURRENT_USER, kRunKey, L"ORC", RRF_RT_REG_SZ, nullptr, nullptr, nullptr)
        == ERROR_SUCCESS;
}

void setLogin(bool on)
{
    if (!runningFromProgramFiles()) return;
    LONG rc = ERROR_SUCCESS;
    if (on) {
        wchar_t exe[32768];
        const DWORD n = ::GetModuleFileNameW(nullptr, exe, 32768);
        const std::wstring cmd = L"\"" + std::wstring(exe, n) + L"\"";
        rc = ::RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, L"ORC", REG_SZ, cmd.c_str(),
                               static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        rc = ::RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, L"ORC");
        if (rc == ERROR_FILE_NOT_FOUND) rc = ERROR_SUCCESS;
    }
    if (rc != ERROR_SUCCESS)
        set::server().post([rc](set::Context& c) { c.error = "Windows refused (" + std::to_string(rc) + ")"; });
}

void pushLogin()
{
    const bool available = runningFromProgramFiles();
    const bool on = loginIsOn();
    set::server().post([available, on](set::Context& c) {
        c.loginAvailable = available;
        c.loginOn = on;
    });
}

// ── the logo's file dialog ───────────────────────────────────────────────────

void chooseLogo()
{
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
    const COMDLG_FILTERSPEC types[] = { { L"Pictures", L"*.png;*.jpg;*.jpeg;*.svg" } };
    dlg->SetFileTypes(1, types);
    if (FAILED(dlg->Show(g_wnd))) return;   // also: the user cancelled
    ComPtr<IShellItem> item;
    if (FAILED(dlg->GetResult(&item))) return;
    PWSTR wide = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &wide))) return;
    const std::string path = orc_files::narrow_(wide);
    ::CoTaskMemFree(wide);
    set::server().post([path](set::Context& c) { set::logoChosen(path, c); });
}

// ── the web view ─────────────────────────────────────────────────────────────

void fitToWindow()
{
    if (!g_controller) return;
    RECT r{};
    ::GetClientRect(g_wnd, &r);
    g_controller->put_Bounds(r);
}

void createController();

void destroyWebView()
{
    if (g_controller) g_controller->Close();
    g_controller.Reset();
    g_web.Reset();
}

// The right-click menu: what a text field needs, not Back, Reload, Print and Inspect.
HRESULT filterContextMenu(ICoreWebView2ContextMenuRequestedEventArgs* args)
{
    ComPtr<ICoreWebView2ContextMenuItemCollection> items;
    if (FAILED(args->get_MenuItems(&items))) return S_OK;
    UINT32 count = 0;
    items->get_Count(&count);
    for (UINT32 i = count; i-- > 0;) {
        ComPtr<ICoreWebView2ContextMenuItem> item;
        if (FAILED(items->GetValueAtIndex(i, &item))) continue;
        LPWSTR name = nullptr;
        item->get_Name(&name);
        const std::wstring n = name ? name : L"";
        ::CoTaskMemFree(name);
        if (n != L"cut" && n != L"copy" && n != L"paste" && n != L"selectAll" && n != L"undo" && n != L"redo")
            items->RemoveValueAtIndex(i);
    }
    return S_OK;
}

void webViewReady(ICoreWebView2Controller* controller)
{
    g_controller = controller;
    g_controller->get_CoreWebView2(&g_web);
    // Shown when it has drawn.
    g_controller->put_IsVisible(FALSE);
    fitToWindow();

    ComPtr<ICoreWebView2Settings> st;
    if (SUCCEEDED(g_web->get_Settings(&st))) {
        st->put_AreDevToolsEnabled(FALSE);
        st->put_IsStatusBarEnabled(FALSE);
        st->put_IsZoomControlEnabled(FALSE);
        // F5, Ctrl-P and the like. Ctrl-C, V, X, A stay, Microsoft says so.
        ComPtr<ICoreWebView2Settings3> st3;
        if (SUCCEEDED(st.As(&st3))) st3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
    }

    EventRegistrationToken token{};
    g_web->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                if (g_controller) g_controller->put_IsVisible(TRUE);
                return S_OK;
            }).Get(), &token);
    // A link that opens a new window (Open, on Studio Control) goes to the browser.
    g_web->add_NewWindowRequested(
        Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                LPWSTR uri = nullptr;
                if (SUCCEEDED(args->get_Uri(&uri)) && uri)
                    ::ShellExecuteW(nullptr, L"open", uri, nullptr, nullptr, SW_SHOWNORMAL);
                ::CoTaskMemFree(uri);
                args->put_Handled(TRUE);
                return S_OK;
            }).Get(), &token);
    // The page's process gone: load it again, the page connects by itself. The whole
    // browser gone: a new web view on the same address.
    g_web->add_ProcessFailed(
        Callback<ICoreWebView2ProcessFailedEventHandler>(
            [](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs* args) -> HRESULT {
                COREWEBVIEW2_PROCESS_FAILED_KIND kind{};
                args->get_ProcessFailedKind(&kind);
                if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED
                    || kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE) {
                    if (g_web) g_web->Reload();
                } else if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED) {
                    LPWSTR uri = nullptr;
                    if (g_web && SUCCEEDED(g_web->get_Source(&uri)) && uri) g_pendingUrl = uri;
                    ::CoTaskMemFree(uri);
                    g_controller.Reset();
                    g_web.Reset();
                    g_env.Reset();
                    if (!g_pendingUrl.empty()) createController();
                }
                return S_OK;
            }).Get(), &token);
    ComPtr<ICoreWebView2_11> web11;
    if (SUCCEEDED(g_web.As(&web11)))
        web11->add_ContextMenuRequested(
            Callback<ICoreWebView2ContextMenuRequestedEventHandler>(
                [](ICoreWebView2*, ICoreWebView2ContextMenuRequestedEventArgs* args) -> HRESULT {
                    return filterContextMenu(args);
                }).Get(), &token);

    if (!g_pendingUrl.empty()) {
        g_web->Navigate(g_pendingUrl.c_str());
        g_pendingUrl.clear();
    }
    g_controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
}

void missingRuntime()
{
    // The installer takes care of it, so this is the rare machine: say what is missing.
    const int a = ::MessageBoxW(g_wnd,
        L"ORC's settings need the Microsoft Edge WebView2 Runtime. Open Microsoft's download page?",
        L"ORC", MB_ICONWARNING | MB_YESNO);
    if (a == IDYES)
        ::ShellExecuteW(nullptr, L"open", L"https://developer.microsoft.com/microsoft-edge/webview2/consumer/",
                        nullptr, nullptr, SW_SHOWNORMAL);
    ::PostMessageW(g_wnd, WM_CLOSE, 0, 0);
}

void createController()
{
    const auto withEnv = [] {
        g_env->CreateCoreWebView2Controller(g_wnd,
            Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [](HRESULT hr, ICoreWebView2Controller* c) -> HRESULT {
                    if (FAILED(hr) || !c) { missingRuntime(); return S_OK; }
                    webViewReady(c);
                    return S_OK;
                }).Get());
    };
    if (g_env) { withEnv(); return; }

    // ⛔ THE FOLDER IS NAMED. The default is beside the program, and creating it under
    // Program Files fails at startup (Microsoft, user data folder).
    wchar_t local[MAX_PATH];
    const DWORD n = ::GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    const std::wstring data = (n > 0 && n < MAX_PATH) ? std::wstring(local) + L"\\ORC\\WebView2" : std::wstring();
    // The page is dark: no white flash before it has drawn. Set before the environment
    // exists, which is the way without one (Microsoft).
    ::SetEnvironmentVariableW(L"WEBVIEW2_DEFAULT_BACKGROUND_COLOR", L"FF0D0D0D");
    const HRESULT hr = ::CreateCoreWebView2EnvironmentWithOptions(nullptr, data.empty() ? nullptr : data.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [withEnv](HRESULT r, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(r) || !env) { missingRuntime(); return S_OK; }
                g_env = env;
                withEnv();
                return S_OK;
            }).Get());
    if (FAILED(hr)) missingRuntime();
}

// ── the window ───────────────────────────────────────────────────────────────

void finishClosing()
{
    g_closing = false;
    // Nothing is polled, and nothing listens, while nobody is looking.
    ::KillTimer(g_wnd, kLoginTimer);
    destroyWebView();
    set::server().stop();
    ::ShowWindow(g_wnd, SW_HIDE);
}

// A field being typed in is handed over before the window goes, and the page we were
// on is where it opens next time.
void beginClosing()
{
    if (g_closing) return;
    g_closing = true;
    if (!g_web) { finishClosing(); return; }
    g_web->ExecuteScript(L"document.activeElement&&document.activeElement.blur();location.hash.slice(1)",
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [](HRESULT hr, LPCWSTR json) -> HRESULT {
                if (SUCCEEDED(hr) && json) {
                    std::wstring v = json;   // a JSON string, in quotes
                    if (v.size() > 2 && v.front() == L'"' && v.back() == L'"') {
                        v = v.substr(1, v.size() - 2);
                        g_shownPage = orc_files::narrow_(v.c_str());
                    }
                }
                ::SetTimer(g_wnd, kCloseTimer, 150, nullptr);
                return S_OK;
            }).Get());
}

LRESULT CALLBACK windowProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
        case WM_SIZE:
            fitToWindow();
            return 0;
        case WM_MOVE:
            if (g_controller) g_controller->NotifyParentWindowPositionChanged();
            return 0;
        case WM_SETFOCUS:
            if (g_controller) g_controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
            return 0;
        case WM_DPICHANGED: {
            const auto* r = reinterpret_cast<const RECT*>(lp);
            ::SetWindowPos(w, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                           SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            const int dpi = static_cast<int>(::GetDpiForWindow(w));
            RECT r{ 0, 0, ::MulDiv(kMinW, dpi, 96), ::MulDiv(kMinH, dpi, 96) };
            ::AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, static_cast<UINT>(dpi));
            auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
            mm->ptMinTrackSize = POINT{ r.right - r.left, r.bottom - r.top };
            return 0;
        }
        case WM_TIMER:
            if (wp == kLoginTimer) pushLogin();
            else if (wp == kCloseTimer) { ::KillTimer(w, kCloseTimer); finishClosing(); }
            return 0;
        case kChooseLogo:
            chooseLogo();
            return 0;
        case WM_CLOSE:
            beginClosing();
            return 0;
        default: break;
    }
    return ::DefWindowProcW(w, msg, wp, lp);
}

void buildWindow()
{
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = windowProc;
    wc.hInstance     = ::GetModuleHandleW(nullptr);
    wc.hCursor       = ::LoadCursorW(nullptr, MAKEINTRESOURCEW(32512))   /* IDC_ARROW */;
    wc.hIcon         = ::LoadIconW(wc.hInstance, MAKEINTRESOURCEW(kIconApp));
    wc.hbrBackground = ::CreateSolidBrush(RGB(0x0D, 0x0D, 0x0D));
    wc.lpszClassName = kWindowClass;
    ::RegisterClassExW(&wc);
    g_wnd = ::CreateWindowExW(0, kWindowClass, L"ORC Settings", WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                              nullptr, nullptr, wc.hInstance, nullptr);
    // A dark frame around a dark page. Windows 11; earlier versions ignore it.
    const BOOL dark = TRUE;
    ::DwmSetWindowAttribute(g_wnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    // The size wanted, not more than the screen, in the middle of it.
    const int dpi = static_cast<int>(::GetDpiForWindow(g_wnd));
    RECT want{ 0, 0, ::MulDiv(kWantW, dpi, 96), ::MulDiv(kWantH, dpi, 96) };
    ::AdjustWindowRectExForDpi(&want, WS_OVERLAPPEDWINDOW, FALSE, 0, static_cast<UINT>(dpi));
    RECT area{};
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
    const int cx = std::min<LONG>(want.right - want.left, area.right - area.left);
    const int cy = std::min<LONG>(want.bottom - want.top, area.bottom - area.top);
    ::SetWindowPos(g_wnd, nullptr, area.left + ((area.right - area.left) - cx) / 2,
                   area.top + ((area.bottom - area.top) - cy) / 2, cx, cy, SWP_NOZORDER | SWP_NOACTIVATE);
}

} // namespace

void present(const std::string& page)
{
    if (!g_wnd) buildWindow();
    if (!page.empty()) g_shownPage = page;
    if (::IsWindowVisible(g_wnd)) {
        if (g_closing) return;
        if (!page.empty() && g_web)
            g_web->ExecuteScript(widen("window.orcShow&&orcShow('" + page + "')").c_str(), nullptr);
    } else {
        set::Host host;
        host.setLogin   = [](bool on) { setLogin(on); };
        host.chooseLogo = [] { ::PostMessageW(g_wnd, kChooseLogo, 0, 0); };
        const std::string address = set::server().start(resourcesDir() + "/settings", host);
        pushLogin();
        if (address.empty()) return;
        std::string url = address;
        if (!g_shownPage.empty()) url += "#" + g_shownPage;
        g_pendingUrl = widen(url);
        ::SetTimer(g_wnd, kLoginTimer, 1000, nullptr);
        if (g_web) { g_web->Navigate(g_pendingUrl.c_str()); g_pendingUrl.clear(); }
        else createController();
    }
    ::ShowWindow(g_wnd, SW_SHOW);
    if (::IsIconic(g_wnd)) ::ShowWindow(g_wnd, SW_RESTORE);
    ::SetForegroundWindow(g_wnd);
}

void toggle()
{
    if (g_wnd && ::IsWindowVisible(g_wnd)) ::PostMessageW(g_wnd, WM_CLOSE, 0, 0);
    else present("");
}

void closeForQuit()
{
    if (!g_wnd) return;
    if (::IsWindowVisible(g_wnd)) finishClosing();
    g_env.Reset();
    ::DestroyWindow(g_wnd);
    g_wnd = nullptr;
}

} // namespace orc::webwindow
