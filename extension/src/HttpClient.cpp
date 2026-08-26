// Last-resort stub for reasixty::http on a platform with no native client.
// macOS is macos_http.mm (NSURLSession), Windows is win_http.cpp (WinHTTP),
// Linux is linux_http.cpp (libcurl via dlopen) — this file covers whatever is
// left, and returns a clear error so the Exchange UI says so rather than
// silently hanging. It compiles to nothing on the three shipped platforms.

#include "HttpClient.h"

#if !defined(__APPLE__) && !defined(_WIN32) && !defined(__linux__)

namespace reasixty::http {

uint64_t begin(const std::string&, const std::string&,
               const std::vector<std::string>&, const std::string&, int, bool)
{
    return 0;   // could not start — no client on this platform yet
}

bool poll(uint64_t, Response& out)
{
    out = Response{};
    out.error = "the in-app exchange client is not built on this platform yet";
    return true;
}

void cancel(uint64_t) {}

} // namespace reasixty::http

#endif // no native client on this platform
