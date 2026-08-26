// Linux HTTPS client — libcurl, loaded at run time, behind reasixty::http.
//
// WHY dlopen AND NOT -lcurl. Linking libcurl would put a hard dependency into
// reaper_rea-sixty.so: the extension would fail to load on a machine without
// the matching soname, taking the surface support down with it over a feature
// (the mapping exchange) most users never touch. It would also make
// libcurl4-openssl-dev a build requirement for everyone who builds from
// source. Loading it lazily costs one dlopen and degrades to a readable error.
//
// WHY THE CONSTANTS ARE SPELLED OUT. Same reason: no curl headers at build
// time. The CURLOPT_* numbers below are the documented, ABI-stable option ids
// (curl never renumbers an option) and every one was read out of a real
// curl.h, not recalled.
//
// WHY A THREAD PER REQUEST. The interface is fire-and-poll and the extension
// makes a handful of requests per session. The worker runs a synchronous
// curl_easy_perform and drops its Response into a mutex-guarded map; poll()
// drains it from the main thread. Same shape as macos_http.mm.

#if defined(__linux__)

#include "HttpClient.h"

#include <dlfcn.h>

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace reasixty::http {
namespace {

// ---------------------------------------------------------------- libcurl ABI

// Option ids, verified against curl.h. The base offsets are CURLOPTTYPE_LONG 0,
// CURLOPTTYPE_OBJECTPOINT 10000, CURLOPTTYPE_FUNCTIONPOINT 20000.
enum : int {
    kOptWriteData      = 10001,   // CURLOPT_WRITEDATA
    kOptUrl            = 10002,   // CURLOPT_URL
    kOptWriteFunction  = 20011,   // CURLOPT_WRITEFUNCTION
    kOptTimeout        = 13,      // CURLOPT_TIMEOUT           (seconds, long)
    kOptPostFields     = 10015,   // CURLOPT_POSTFIELDS        (not copied)
    kOptUserAgent      = 10018,   // CURLOPT_USERAGENT
    kOptHttpHeader     = 10023,   // CURLOPT_HTTPHEADER
    kOptCustomRequest  = 10036,   // CURLOPT_CUSTOMREQUEST
    kOptFollowLocation = 52,      // CURLOPT_FOLLOWLOCATION    (long)
    kOptPostFieldSize  = 60,      // CURLOPT_POSTFIELDSIZE     (long)
    kOptMaxRedirs      = 68,      // CURLOPT_MAXREDIRS         (long)
    kOptNoSignal       = 99,      // CURLOPT_NOSIGNAL          (long)
    // Read out of curl.h on this machine, both CURLOPTTYPE_LONG:
    //   CURLOPT(CURLOPT_SSL_VERIFYPEER, CURLOPTTYPE_LONG, 64)
    //   CURLOPT(CURLOPT_SSL_VERIFYHOST, CURLOPTTYPE_LONG, 81)
    kOptSslVerifyPeer  = 64,      // CURLOPT_SSL_VERIFYPEER    (long)
    kOptSslVerifyHost  = 81,      // CURLOPT_SSL_VERIFYHOST    (long)
};

// CURLINFO_RESPONSE_CODE = CURLINFO_LONG (0x200000) + 2.
enum : int { kInfoResponseCode = 0x200000 + 2 };

// CURL_GLOBAL_DEFAULT = CURL_GLOBAL_ALL = CURL_GLOBAL_SSL | CURL_GLOBAL_WIN32.
enum : long { kGlobalDefault = (1L << 0) | (1L << 1) };

enum : int { kCurlOk = 0 };   // CURLE_OK

// setopt and getinfo are variadic in libcurl. Declaring the pointers variadic
// too keeps the call ABI correct rather than relying on a fixed-arity cast
// happening to work.
using GlobalInitFn   = int   (*)(long);
using EasyInitFn     = void* (*)(void);
using EasySetoptFn   = int   (*)(void*, int, ...);
using EasyPerformFn  = int   (*)(void*);
using EasyCleanupFn  = void  (*)(void*);
using EasyGetinfoFn  = int   (*)(void*, int, ...);
using EasyStrerrorFn = const char* (*)(int);
using SlistAppendFn  = void* (*)(void*, const char*);
using SlistFreeFn    = void  (*)(void*);

struct Curl {
    void*           handle = nullptr;
    GlobalInitFn    global_init   = nullptr;
    EasyInitFn      easy_init     = nullptr;
    EasySetoptFn    easy_setopt   = nullptr;
    EasyPerformFn   easy_perform  = nullptr;
    EasyCleanupFn   easy_cleanup  = nullptr;
    EasyGetinfoFn   easy_getinfo  = nullptr;
    EasyStrerrorFn  easy_strerror = nullptr;
    SlistAppendFn   slist_append  = nullptr;
    SlistFreeFn     slist_free    = nullptr;
    std::string     loadError;
};

template <typename Fn>
bool bindSymbol(void* lib, const char* name, Fn& out)
{
    out = reinterpret_cast<Fn>(::dlsym(lib, name));
    return out != nullptr;
}

// Resolved once. Distributions disagree on which libcurl is installed —
// Debian/Ubuntu ship the OpenSSL build as libcurl.so.4 but some packages pull
// the GnuTLS one instead, and only the -dev package provides the unversioned
// libcurl.so. Try the realistic names in order.
const Curl& curl()
{
    static Curl s_curl;
    static std::once_flag s_once;
    std::call_once(s_once, [] {
        static const char* kCandidates[] = {
            "libcurl.so.4",
            "libcurl.so",
            "libcurl-gnutls.so.4",
            "libcurl-nss.so.4",
        };
        for (const char* name : kCandidates) {
            void* lib = ::dlopen(name, RTLD_LAZY | RTLD_LOCAL);
            if (!lib) continue;
            Curl c;
            c.handle = lib;
            const bool complete =
                bindSymbol(lib, "curl_global_init",   c.global_init) &&
                bindSymbol(lib, "curl_easy_init",     c.easy_init) &&
                bindSymbol(lib, "curl_easy_setopt",   c.easy_setopt) &&
                bindSymbol(lib, "curl_easy_perform",  c.easy_perform) &&
                bindSymbol(lib, "curl_easy_cleanup",  c.easy_cleanup) &&
                bindSymbol(lib, "curl_easy_getinfo",  c.easy_getinfo) &&
                bindSymbol(lib, "curl_easy_strerror", c.easy_strerror) &&
                bindSymbol(lib, "curl_slist_append",  c.slist_append) &&
                bindSymbol(lib, "curl_slist_free_all", c.slist_free);
            if (!complete) {
                ::dlclose(lib);
                continue;
            }
            c.global_init(kGlobalDefault);
            s_curl = c;
            return;
        }
        s_curl.loadError =
            "libcurl is not installed — the in-app exchange needs it on Linux "
            "(Debian/Ubuntu: libcurl4, Fedora: libcurl)";
    });
    return s_curl;
}

// -------------------------------------------------------------------- request

std::atomic<uint64_t> g_nextId{1};

struct Pending {
    bool        done = false;
    Response    response;
    std::thread worker;
};

std::mutex                  g_mutex;
std::map<uint64_t, Pending> g_requests;

size_t writeToString(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    const size_t bytes = size * nmemb;
    static_cast<std::string*>(userdata)->append(ptr, bytes);
    return bytes;
}

// The whole synchronous round trip. Transport failures come back with status 0
// and a non-empty error, matching macOS.
Response perform(const std::string& method,
                 const std::string& url,
                 const std::vector<std::string>& headers,
                 const std::string& body,
                 int timeoutSeconds,
                 bool allowUntrustedCert)
{
    Response r;

    const Curl& c = curl();
    if (!c.handle) {
        r.error = c.loadError;
        return r;
    }

    void* easy = c.easy_init();
    if (!easy) {
        r.error = "could not create a libcurl handle";
        return r;
    }

    void* headerList = nullptr;
    for (const std::string& h : headers) {
        if (h.empty()) continue;
        void* next = c.slist_append(headerList, h.c_str());
        if (!next) break;
        headerList = next;
    }

    c.easy_setopt(easy, kOptUrl, url.c_str());
    c.easy_setopt(easy, kOptWriteFunction, &writeToString);
    c.easy_setopt(easy, kOptWriteData, &r.body);
    c.easy_setopt(easy, kOptUserAgent, "Rea-Sixty/1.0");
    c.easy_setopt(easy, kOptTimeout, (long)(timeoutSeconds > 0 ? timeoutSeconds : 20));
    c.easy_setopt(easy, kOptFollowLocation, 1L);
    c.easy_setopt(easy, kOptMaxRedirs, 5L);
    // Without NOSIGNAL libcurl arms SIGALRM for its DNS timeout, which in a
    // host process we do not own is a way to kill REAPER, not to time out.
    c.easy_setopt(easy, kOptNoSignal, 1L);
    // Certificate-blind mode for the Hue bridge (see HttpClient.h). VERIFYHOST
    // has to go too: the bridge's common name is its bridge id, never the IP we
    // dialled, so host verification would fail even with the chain accepted.
    if (allowUntrustedCert) {
        c.easy_setopt(easy, kOptSslVerifyPeer, 0L);
        c.easy_setopt(easy, kOptSslVerifyHost, 0L);
    }
    if (headerList) c.easy_setopt(easy, kOptHttpHeader, headerList);

    if (method == "POST") {
        // POSTFIELDS does not copy; `body` outlives the perform() call below.
        c.easy_setopt(easy, kOptPostFieldSize, (long)body.size());
        c.easy_setopt(easy, kOptPostFields, body.data());
    } else if (method != "GET" && !method.empty()) {
        c.easy_setopt(easy, kOptCustomRequest, method.c_str());
        if (!body.empty()) {
            c.easy_setopt(easy, kOptPostFieldSize, (long)body.size());
            c.easy_setopt(easy, kOptPostFields, body.data());
        }
    }

    const int rc = c.easy_perform(easy);
    if (rc != kCurlOk) {
        const char* msg = c.easy_strerror(rc);
        r.error = msg ? msg : "request failed";
        r.body.clear();
    } else {
        long status = 0;
        if (c.easy_getinfo(easy, kInfoResponseCode, &status) == kCurlOk) r.status = status;
    }

    if (headerList) c.slist_free(headerList);
    c.easy_cleanup(easy);
    return r;
}

} // namespace

uint64_t begin(const std::string& method,
               const std::string& url,
               const std::vector<std::string>& headers,
               const std::string& body,
               int timeoutSeconds,
               bool allowUntrustedCert)
{
    const uint64_t id = g_nextId.fetch_add(1);
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_requests[id];   // default-construct the slot before the worker can look for it
    }

    std::thread worker;
    try {
        worker = std::thread([id, method, url, headers, body, timeoutSeconds,
                              allowUntrustedCert] {
            Response r = perform(method, url, headers, body, timeoutSeconds,
                                 allowUntrustedCert);
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
    // curl_easy_perform cannot be interrupted from outside, so the request runs
    // to its timeout; detaching just means nobody waits for it.
    if (orphan.joinable()) orphan.detach();
}

} // namespace reasixty::http

#endif // __linux__
