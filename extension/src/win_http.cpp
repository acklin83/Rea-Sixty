// Windows HTTPS client — WinHTTP behind reasixty::http.
//
// WinHTTP ships with Windows, so this adds no bundled binary and nothing for
// ReaPack to package: link winhttp.lib and the OS supplies TLS, DNS, the proxy
// configuration and redirects.
//
// WHY A THREAD PER REQUEST. WinHTTP's own async mode wants a status callback
// and a message pump; the interface here is fire-and-poll, and the extension
// makes a handful of requests per session, not thousands. One detached-on-
// cancel std::thread doing the synchronous WinHTTP calls is the smaller thing
// to get right, and it mirrors macos_http.mm's shape: the worker drops its
// Response into a mutex-guarded map, poll() drains it from the main thread.
//
// Buffers live on the heap. Windows gives REAPER's threads a modest stack and
// this codebase has already eaten one STATUS_STACK_OVERFLOW from a large
// stack buffer — see the Windows notes in the release runbook.

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "HttpClient.h"

#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace reasixty::http {
namespace {

std::atomic<uint64_t> g_nextId{1};

struct Pending {
    bool        done = false;
    Response    response;
    std::thread worker;
};

std::mutex                  g_mutex;
std::map<uint64_t, Pending> g_requests;

// WinHTTP is a wide-character API and everything above it here is UTF-8.
std::wstring widen(const std::string& s)
{
    if (s.empty()) return std::wstring();
    const int need = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (need <= 0) return std::wstring();
    std::wstring out((size_t)need, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], need);
    return out;
}

// WinHTTP's own errors (12000+) live in winhttp.dll's message table, not the
// system one, so FORMAT_MESSAGE_FROM_SYSTEM alone yields nothing for exactly
// the failures a user hits (no connection, bad certificate, timeout).
std::string errorText(DWORD code)
{
    char*  msg = nullptr;
    DWORD  len = 0;
    HMODULE winhttp = ::GetModuleHandleW(L"winhttp.dll");
    if (winhttp) {
        len = ::FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE |
                                   FORMAT_MESSAGE_IGNORE_INSERTS,
                               winhttp, code, 0, (char*)&msg, 0, nullptr);
    }
    if (!len) {
        len = ::FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                   FORMAT_MESSAGE_IGNORE_INSERTS,
                               nullptr, code, 0, (char*)&msg, 0, nullptr);
    }
    std::string out;
    if (len && msg) {
        out.assign(msg, len);
        while (!out.empty() && (out.back() == '\r' || out.back() == '\n' || out.back() == ' '))
            out.pop_back();
    }
    ::LocalFree(msg);
    if (out.empty()) out = "request failed";
    return out + " (WinHTTP " + std::to_string((unsigned long)code) + ")";
}

// The whole synchronous round trip. Returns a filled Response; transport
// failures come back with status 0 and a non-empty error, matching macOS.
Response perform(const std::string& method,
                 const std::string& url,
                 const std::vector<std::string>& headers,
                 const std::string& body,
                 int timeoutSeconds)
{
    Response r;

    const std::wstring wurl = widen(url);

    URL_COMPONENTS comp{};
    comp.dwStructSize = sizeof(comp);
    // Non-zero length with a null pointer = "point me at the substring in wurl
    // and tell me how long it is", which avoids allocating per component.
    comp.dwSchemeLength    = (DWORD)-1;
    comp.dwHostNameLength  = (DWORD)-1;
    comp.dwUrlPathLength   = (DWORD)-1;
    comp.dwExtraInfoLength = (DWORD)-1;

    if (!::WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &comp)) {
        r.error = errorText(::GetLastError());
        return r;
    }

    const std::wstring host(comp.lpszHostName, comp.dwHostNameLength);
    // Query string included: WinHttpOpenRequest takes path and query together.
    std::wstring target(comp.lpszUrlPath, comp.dwUrlPathLength);
    if (comp.dwExtraInfoLength) target.append(comp.lpszExtraInfo, comp.dwExtraInfoLength);
    if (target.empty()) target = L"/";

    const bool secure = (comp.nScheme == INTERNET_SCHEME_HTTPS);

    HINTERNET session = ::WinHttpOpen(L"Rea-Sixty/1.0",
                                      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME,
                                      WINHTTP_NO_PROXY_BYPASS,
                                      0);
    if (!session) {
        r.error = errorText(::GetLastError());
        return r;
    }

    const int ms = (timeoutSeconds > 0 ? timeoutSeconds : 20) * 1000;
    ::WinHttpSetTimeouts(session, ms, ms, ms, ms);

    HINTERNET connect = ::WinHttpConnect(session, host.c_str(), comp.nPort, 0);
    if (!connect) {
        r.error = errorText(::GetLastError());
        ::WinHttpCloseHandle(session);
        return r;
    }

    HINTERNET request = ::WinHttpOpenRequest(connect,
                                             widen(method).c_str(),
                                             target.c_str(),
                                             nullptr,
                                             WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             secure ? WINHTTP_FLAG_SECURE : 0);
    if (!request) {
        r.error = errorText(::GetLastError());
        ::WinHttpCloseHandle(connect);
        ::WinHttpCloseHandle(session);
        return r;
    }

    // Callers hand us full header lines; WinHTTP wants them CRLF-joined.
    std::wstring headerBlock;
    for (const std::string& h : headers) {
        if (h.empty()) continue;
        if (!headerBlock.empty()) headerBlock += L"\r\n";
        headerBlock += widen(h);
    }

    // Spelled out rather than folded into ternaries: WINHTTP_NO_ADDITIONAL_
    // HEADERS and WINHTTP_NO_REQUEST_DATA are NULL macros, and `cond ? NULL :
    // ptr` is not portably well-formed across compilers' NULL definitions.
    LPCWSTR headerPtr = nullptr;
    DWORD   headerLen = 0;
    if (!headerBlock.empty()) {
        headerPtr = headerBlock.c_str();
        headerLen = (DWORD)-1L;   // -1 = "null-terminated, work out the length"
    }
    LPVOID bodyPtr = nullptr;
    // `body` is a by-value capture in the worker lambda, so it outlives the
    // whole exchange — WinHTTP requires the buffer to stay valid that long.
    if (!body.empty()) bodyPtr = (LPVOID)body.data();

    BOOL ok = ::WinHttpSendRequest(request,
                                   headerPtr,
                                   headerLen,
                                   bodyPtr,
                                   (DWORD)body.size(),
                                   (DWORD)body.size(),
                                   0);
    if (ok) ok = ::WinHttpReceiveResponse(request, nullptr);

    if (!ok) {
        r.error = errorText(::GetLastError());
    } else {
        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        if (::WinHttpQueryHeaders(request,
                                  WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                  WINHTTP_HEADER_NAME_BY_INDEX,
                                  &status,
                                  &statusSize,
                                  WINHTTP_NO_HEADER_INDEX)) {
            r.status = (long)status;
        }

        // 8 KB is WinHTTP's own internal read buffer size, and its docs ask for
        // a read buffer at least that large.
        std::vector<char> chunk(8192);
        for (;;) {
            DWORD read = 0;
            if (!::WinHttpReadData(request, chunk.data(), (DWORD)chunk.size(), &read)) {
                r.error = errorText(::GetLastError());
                r.status = 0;
                r.body.clear();
                break;
            }
            if (!read) break;   // synchronous read of 0 bytes = end of response
            r.body.append(chunk.data(), read);
        }
    }

    ::WinHttpCloseHandle(request);
    ::WinHttpCloseHandle(connect);
    ::WinHttpCloseHandle(session);
    return r;
}

} // namespace

uint64_t begin(const std::string& method,
               const std::string& url,
               const std::vector<std::string>& headers,
               const std::string& body,
               int timeoutSeconds)
{
    const uint64_t id = g_nextId.fetch_add(1);
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_requests[id];   // default-construct the slot before the worker can look for it
    }

    std::thread worker;
    try {
        worker = std::thread([id, method, url, headers, body, timeoutSeconds] {
            Response r = perform(method, url, headers, body, timeoutSeconds);
            std::lock_guard<std::mutex> lk(g_mutex);
            auto it = g_requests.find(id);
            if (it != g_requests.end()) {   // still wanted (not cancelled)
                it->second.done = true;
                it->second.response = std::move(r);
            }
        });
    } catch (...) {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_requests.erase(id);
        return 0;
    }

    {
        std::lock_guard<std::mutex> lk(g_mutex);
        auto it = g_requests.find(id);
        if (it != g_requests.end()) {
            it->second.worker = std::move(worker);
            return id;
        }
    }
    // Cancelled before we could park the thread — nobody will ever join it.
    worker.detach();
    return id;
}

bool poll(uint64_t id, Response& out)
{
    std::thread finished;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        auto it = g_requests.find(id);
        if (it == g_requests.end()) {
            // Unknown id — treat as a finished transport error so nobody waits
            // forever on a request that was cancelled or never existed.
            out = Response{};
            out.error = "unknown request";
            return true;
        }
        if (!it->second.done) return false;
        out = std::move(it->second.response);
        finished = std::move(it->second.worker);
        g_requests.erase(it);
    }
    // The worker set done as its last act, so this join returns immediately.
    // Joining outside the lock: the worker takes g_mutex on its way out.
    if (finished.joinable()) finished.join();
    return true;
}

void cancel(uint64_t id)
{
    std::thread orphan;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        auto it = g_requests.find(id);
        if (it == g_requests.end()) return;
        orphan = std::move(it->second.worker);
        g_requests.erase(it);   // the worker will find no entry and drop its result
    }
    // WinHTTP's synchronous calls can't be interrupted from outside, so the
    // request runs to its timeout; detaching just means nobody waits for it.
    if (orphan.joinable()) orphan.detach();
}

} // namespace reasixty::http

#endif // _WIN32
