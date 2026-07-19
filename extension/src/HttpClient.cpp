// Non-macOS stub for reasixty::http. macOS is implemented in macos_http.mm;
// Windows (WinHTTP) and Linux (libcurl via dlopen) are not built yet. The stub
// keeps the extension compiling and linking on those platforms, and returns a
// clear error so the Exchange UI can say "in-app browsing is macOS-only for
// now" rather than silently hang.

#include "HttpClient.h"

#if !defined(__APPLE__)

namespace reasixty::http {

uint64_t begin(const std::string&, const std::string&,
               const std::vector<std::string>&, const std::string&, int)
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

#endif // !__APPLE__
