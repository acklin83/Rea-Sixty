// Minimal async HTTPS client for talking to api.reasixty.com.
//
// WHY ASYNC. REAPER runs the extension's ImGui UI on the main thread via a
// timer. A synchronous network call there would freeze REAPER for the round
// trip. So this is fire-and-poll: begin() returns immediately with an id, the
// request runs off-thread, and the UI polls once per frame until it completes.
//
// WHY PLATFORM-NATIVE. The extension ships no bundled binary and must not — a
// new dylib drags in notarisation and ReaPack packaging. So each platform uses
// what the OS already provides: NSURLSession (macOS, built), WinHTTP (Windows),
// libcurl via dlopen (Linux). All behind this one interface. Only macOS is
// implemented today; the others return a clear error until built.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace reasixty::http {

struct Response {
    long        status = 0;   // HTTP status code; 0 means a transport failure
    std::string body;
    std::string error;        // non-empty only on transport failure (no TLS, DNS, timeout…)
};

// Start a request. `method` is "GET" or "POST". `headers` are full lines
// ("Authorization: Bearer …"). `body` is sent for POST. Returns an opaque id
// (> 0) to poll on, or 0 if the request could not even be started.
uint64_t begin(const std::string& method,
               const std::string& url,
               const std::vector<std::string>& headers = {},
               const std::string& body = {},
               int timeoutSeconds = 20);

// If the request `id` has finished, fill `out` and return true (the result is
// then forgotten — poll once). If still in flight, return false. An unknown id
// returns true with a transport error, so a caller can't wait forever.
bool poll(uint64_t id, Response& out);

// Forget a pending request; its result (if any) is discarded. Safe on unknown ids.
void cancel(uint64_t id);

} // namespace reasixty::http
