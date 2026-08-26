// Minimal async HTTPS client for talking to api.reasixty.com.
//
// WHY ASYNC. REAPER runs the extension's ImGui UI on the main thread via a
// timer. A synchronous network call there would freeze REAPER for the round
// trip. So this is fire-and-poll: begin() returns immediately with an id, the
// request runs off-thread, and the UI polls once per frame until it completes.
//
// WHY PLATFORM-NATIVE. The extension ships no bundled binary and must not — a
// new dylib drags in notarisation and ReaPack packaging. So each platform uses
// what the OS already provides, all behind this one interface:
//   macOS    macos_http.mm   NSURLSession
//   Windows  win_http.cpp    WinHTTP (winhttp.lib, ships with the OS)
//   Linux    linux_http.cpp  libcurl via dlopen, so a missing libcurl costs
//                            the exchange and not the whole extension
// All three are implemented and built. HttpClient.cpp is the fallback for a
// fourth platform: it is #if'd out on all three above and exists so an
// unported target fails with a readable message instead of hanging.
//
// Each backend runs one thread per request doing the synchronous call and
// drops its Response into a mutex-guarded map; poll() drains it on the main
// thread. The extension makes a handful of requests per session, so a thread
// each is the smaller thing to get right than an async state machine.

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

// Start a request. `method` is any verb the server understands ("GET", "POST",
// "PUT", …); all three backends pass it through and send `body` with it.
// `headers` are full lines ("Authorization: Bearer …"). Returns an opaque id
// (> 0) to poll on, or 0 if the request could not even be started.
//
// ⛔ `allowUntrustedCert` TURNS OFF TLS VERIFICATION for this one request.
// It exists for exactly one caller: the Philips Hue bridge, which serves CLIP
// API v2 over HTTPS with a Signify-signed certificate no system CA chains to,
// and which offers no plaintext path to v2 at all. Every other caller must
// leave it false — the mapping exchange talks to a public host with a public
// certificate and has no business here.
//
// Turning verification off makes the transport blind to WHO answered, so the
// Hue client re-establishes identity a layer up: it stores the bridge id at
// pairing time and compares it on every reconnect (HueManager). Do not copy
// this flag into a new caller without bringing an equivalent check with it.
uint64_t begin(const std::string& method,
               const std::string& url,
               const std::vector<std::string>& headers = {},
               const std::string& body = {},
               int timeoutSeconds = 20,
               bool allowUntrustedCert = false);

// If the request `id` has finished, fill `out` and return true (the result is
// then forgotten — poll once). If still in flight, return false. An unknown id
// returns true with a transport error, so a caller can't wait forever.
bool poll(uint64_t id, Response& out);

// Forget a pending request; its result (if any) is discarded. Safe on unknown ids.
void cancel(uint64_t id);

} // namespace reasixty::http
