#include "OrcUi.h"

#include "OrcFiles.h"
#include "OrcMenu.h"
#include "OrcWinUi.h"
#include "OrcWebWindow.h"
#include "Surface.h"

#include "qrcodegen.hpp"

#include <algorithm>
#include <cstring>
#include <objbase.h>
#include <shellapi.h>
#include <string>
#include <windowsx.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")

namespace orc {

std::wstring widen(const std::string& s)
{
    if (s.empty()) return {};
    const int need = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(need), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), need);
    return out;
}

std::string exeFolder() { return resourcesDir(); }

bool runningFromProgramFiles()
{
    wchar_t pf[MAX_PATH];
    const DWORD n = ::GetEnvironmentVariableW(L"ProgramFiles", pf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    return ::CompareStringOrdinal((std::wstring(pf) + L"\\ORC").c_str(), -1,
                                  widen(exeFolder()).c_str(), -1, TRUE) == CSTR_EQUAL;
}

namespace {

constexpr UINT    kTrayMessage  = WM_APP + 1;
constexpr UINT    kOpenSettings = WM_APP + 2;   // from a second start of ORC
constexpr UINT_PTR kWatchTimer  = 1;
constexpr wchar_t kWindowClass[] = L"ORC.Tray";
constexpr wchar_t kMutexName[]   = L"Local\\ch.stoersender.orc";   // the installer asks for it too

enum Item : UINT {
    kNoCommand = 1, kCopyAddress, kStudioControl, kSettings, kManual, kQuit, kQrImage,
};

Surface*              g_surface = nullptr;
std::function<bool()> g_interrupted;
std::function<void()> g_sessionEnding;
HWND                  g_wnd = nullptr;
UINT                  g_taskbarCreated = 0;
HANDLE                g_oneOrc = nullptr;
std::string           g_qrFor;
std::wstring          g_addressToCopy;

// ⇨ THE LETTERS HAVE TO SHOW ON THE BAR THEY ARE ON. Windows tints nothing, the Mac's
// template image has no counterpart. SystemUsesLightTheme is what Settings writes;
// Microsoft does not document it. When it is missing the bar is dark.
bool taskbarIsLight()
{
    DWORD v = 0, size = sizeof(v);
    const LONG rc = ::RegGetValueW(HKEY_CURRENT_USER,
                                   L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                   L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &size);
    return rc == ERROR_SUCCESS && v != 0;
}

void addTrayIcon()
{
    NOTIFYICONDATAW n{};
    n.cbSize = sizeof(n);
    n.hWnd   = g_wnd;
    n.uID    = 1;
    n.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    n.uCallbackMessage = kTrayMessage;
    const int px = ::GetSystemMetrics(SM_CXSMICON);
    n.hIcon = static_cast<HICON>(::LoadImageW(::GetModuleHandleW(nullptr),
                                              MAKEINTRESOURCEW(taskbarIsLight() ? kIconTrayBlack : kIconTrayWhite),
                                              IMAGE_ICON, px, px, LR_DEFAULTCOLOR));
    wcscpy_s(n.szTip, L"ORC");
    if (!::Shell_NotifyIconW(NIM_MODIFY, &n)) ::Shell_NotifyIconW(NIM_ADD, &n);
    // Not kept across a sign-out or NIM_ADD: every time.
    n.uVersion = NOTIFYICON_VERSION_4;
    ::Shell_NotifyIconW(NIM_SETVERSION, &n);
    if (n.hIcon) ::DestroyIcon(n.hIcon);
}

void removeTrayIcon()
{
    NOTIFYICONDATAW n{};
    n.cbSize = sizeof(n);
    n.hWnd   = g_wnd;
    n.uID    = 1;
    ::Shell_NotifyIconW(NIM_DELETE, &n);
}

// ⇨ THE ADDRESS AS A QR CODE, for the musicians to scan. ⛔ The address only, never
// the PIN (Frank 29.10.2026). Whole pixels: a smoothed QR code does not scan.
struct QrSize { int moduleSize, quiet, modules, side; };

QrSize qrLayout(HWND wnd)
{
    const int side = ::MulDiv(168, static_cast<int>(::GetDpiForWindow(wnd)), 96);
    const int modules = g_qrFor.empty() ? 1
        : qrcodegen::QrCode::encodeText(g_qrFor.c_str(), qrcodegen::QrCode::Ecc::MEDIUM).getSize();
    const int quiet = side * 8 / 100;
    const int mod = std::max(1, (side - 2 * quiet) / modules);
    return { mod, quiet, modules, mod * modules + 2 * quiet };
}

void drawQr(const DRAWITEMSTRUCT* d)
{
    const QrSize q = qrLayout(g_wnd);
    const qrcodegen::QrCode code = qrcodegen::QrCode::encodeText(g_qrFor.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
    const int margin = ::MulDiv(20, static_cast<int>(::GetDpiForWindow(g_wnd)), 96);
    const int x0 = d->rcItem.left + margin;
    const int y0 = d->rcItem.top + ::MulDiv(6, static_cast<int>(::GetDpiForWindow(g_wnd)), 96);
    HBRUSH white = static_cast<HBRUSH>(::GetStockObject(WHITE_BRUSH));
    HBRUSH black = static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH));
    ::FillRect(d->hDC, &d->rcItem, reinterpret_cast<HBRUSH>(COLOR_MENU + 1));
    RECT all{ x0, y0, x0 + q.side, y0 + q.side };
    ::FillRect(d->hDC, &all, white);
    for (int y = 0; y < q.modules; ++y) {
        for (int x = 0; x < q.modules; ++x) {
            if (!code.getModule(x, y)) continue;
            RECT r{ x0 + q.quiet + x * q.moduleSize, y0 + q.quiet + y * q.moduleSize,
                    x0 + q.quiet + (x + 1) * q.moduleSize, y0 + q.quiet + (y + 1) * q.moduleSize };
            ::FillRect(d->hDC, &r, black);
        }
    }
}

void addLine(HMENU m, const std::string& text)
{
    ::AppendMenuW(m, MF_STRING | MF_GRAYED, kNoCommand, widen(text).c_str());
}

void showMenu(POINT at)
{
    const MenuLines lines = menuLines(g_surface);
    HMENU m = ::CreatePopupMenu();

    // ⇨ WHICH ORC IS THIS (Frank 30.09.2026). The version, and the folder when this
    // is not the installed copy.
    std::string title = "ORC " + version();
    if (!runningFromProgramFiles()) title += "   " + exeFolder();
    addLine(m, title);
    ::AppendMenuW(m, MF_SEPARATOR, 0, nullptr);

    // ⇨ THE FIRST LINES ARE THE WHOLE POINT OF THE MENU: who holds the surfaces, and
    // whether TotalMix is answering.
    addLine(m, lines.uf1);
    addLine(m, lines.uf8);
    addLine(m, lines.mixer);
    addLine(m, lines.moreme);
    if (!lines.address.empty()) {
        g_qrFor = lines.address;
        g_addressToCopy = widen(lines.address);
        ::AppendMenuW(m, MF_STRING, kCopyAddress, widen(lines.addressLine).c_str());
        ::AppendMenuW(m, MF_OWNERDRAW, kQrImage, nullptr);
    }
    if (lines.deskShown) ::AppendMenuW(m, MF_STRING, kStudioControl, L"MoreMe…");
    ::AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(m, MF_STRING, kSettings, L"Settings…");
    ::AppendMenuW(m, MF_STRING, kManual, L"Manual");
    ::AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(m, MF_STRING, kQuit, L"Quit ORC");

    // Microsoft's own remark for this menu: without these two it does not close when
    // the user clicks elsewhere.
    ::SetForegroundWindow(g_wnd);
    const UINT align = ::GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
    const UINT cmd = static_cast<UINT>(::TrackPopupMenu(m, align | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
                                                        at.x, at.y, 0, g_wnd, nullptr));
    ::PostMessageW(g_wnd, WM_NULL, 0, 0);
    ::DestroyMenu(m);

    switch (cmd) {
        case kCopyAddress:
            if (::OpenClipboard(g_wnd)) {
                ::EmptyClipboard();
                const std::size_t bytes = (g_addressToCopy.size() + 1) * sizeof(wchar_t);
                if (HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, bytes)) {
                    std::memcpy(::GlobalLock(mem), g_addressToCopy.c_str(), bytes);
                    ::GlobalUnlock(mem);
                    if (!::SetClipboardData(CF_UNICODETEXT, mem)) ::GlobalFree(mem);
                }
                ::CloseClipboard();
            }
            break;
        case kStudioControl: webwindow::present("moreme"); break;
        case kSettings:      webwindow::present(""); break;
        // ⇨ THE MANUAL, NOT A SENTENCE IN THE WINDOW (Frank 27.09.2026).
        case kManual:
            ::ShellExecuteW(nullptr, L"open", L"https://acklin83.github.io/ORC/", nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case kQuit: ::PostQuitMessage(0); break;
        default: break;
    }
}

LRESULT CALLBACK trayProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (g_taskbarCreated && msg == g_taskbarCreated) {   // Explorer restarted
        addTrayIcon();
        return 0;
    }
    switch (msg) {
        case kTrayMessage:
            // Which of these a click sends Microsoft does not spell out: all of them
            // open the same menu, with the anchor in wParam.
            switch (LOWORD(lp)) {
                case WM_CONTEXTMENU:
                case NIN_SELECT:
                case NIN_KEYSELECT:
                    showMenu(POINT{ GET_X_LPARAM(wp), GET_Y_LPARAM(wp) });
                    break;
                default: break;
            }
            return 0;
        case kOpenSettings:
            webwindow::present("");
            return 0;
        case WM_TIMER:
            if (wp == kWatchTimer) {
                if (g_interrupted && g_interrupted()) ::PostQuitMessage(0);
                // 360 on the UF1: opens the settings window and closes it again.
                if (g_surface && g_surface->takeSettingsRequest()) webwindow::toggle();
            }
            return 0;
        case WM_MEASUREITEM: {
            auto* mi = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
            if (mi->CtlType == ODT_MENU && mi->itemID == kQrImage) {
                const QrSize q = qrLayout(w);
                const int dpi = static_cast<int>(::GetDpiForWindow(w));
                mi->itemWidth  = static_cast<UINT>(q.side + ::MulDiv(40, dpi, 96));
                mi->itemHeight = static_cast<UINT>(q.side + ::MulDiv(12, dpi, 96));
                return TRUE;
            }
            break;
        }
        case WM_DRAWITEM: {
            const auto* d = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
            if (d->CtlType == ODT_MENU && d->itemID == kQrImage) { drawQr(d); return TRUE; }
            break;
        }
        case WM_SETTINGCHANGE:
            // The theme changed: the other set of letters.
            if (lp && ::lstrcmpW(reinterpret_cast<LPCWSTR>(lp), L"ImmersiveColorSet") == 0) addTrayIcon();
            return 0;
        case WM_QUERYENDSESSION:
            return TRUE;
        case WM_ENDSESSION:
            // ⛔ The session may end any time after this returns (Microsoft), so the
            // surfaces, the link and orc.pid are dealt with here, not after the loop.
            if (wp) {
                removeTrayIcon();
                if (g_sessionEnding) g_sessionEnding();
            }
            return 0;
        default: break;
    }
    return ::DefWindowProcW(w, msg, wp, lp);
}

} // namespace

bool handedToRunningCopy()
{
    // ⇨ ONE ORC. Two would fight over the USB devices, the page port and orc.pid. A
    // second start opens the first one's settings and leaves.
    g_oneOrc = ::CreateMutexW(nullptr, FALSE, kMutexName);
    if (!g_oneOrc || ::GetLastError() != ERROR_ALREADY_EXISTS) return false;
    if (HWND first = ::FindWindowW(kWindowClass, nullptr)) {
        DWORD pid = 0;
        ::GetWindowThreadProcessId(first, &pid);
        ::AllowSetForegroundWindow(pid);
        ::PostMessageW(first, kOpenSettings, 0, 0);
    }
    return true;
}

void runApp(Surface& surface, const std::function<bool()>& interrupted, const std::function<void()>& sessionEnding)
{
    g_surface = &surface;
    g_interrupted = interrupted;
    g_sessionEnding = sessionEnding;
    // WebView2 wants a single-threaded apartment on the thread that pumps messages.
    const HRESULT co = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = trayProc;
    wc.hInstance     = ::GetModuleHandleW(nullptr);
    wc.lpszClassName = kWindowClass;
    ::RegisterClassExW(&wc);
    // A hidden window, not a message-only one: the menu needs one that can be in
    // front.
    g_wnd = ::CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"ORC", WS_POPUP, 0, 0, 0, 0,
                              nullptr, nullptr, wc.hInstance, nullptr);
    g_taskbarCreated = ::RegisterWindowMessageW(L"TaskbarCreated");
    addTrayIcon();
    ::SetTimer(g_wnd, kWatchTimer, 250, nullptr);

    MSG msg;
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    webwindow::closeForQuit();
    ::KillTimer(g_wnd, kWatchTimer);
    removeTrayIcon();
    ::DestroyWindow(g_wnd);
    g_wnd = nullptr;
    if (SUCCEEDED(co)) ::CoUninitialize();
    g_surface = nullptr;
}

} // namespace orc
