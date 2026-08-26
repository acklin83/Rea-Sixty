// macOS HTTPS client — NSURLSession behind reasixty::http.
//
// NSURLSession is already async: dataTaskWithRequest completes on a background
// queue. We stash each completed Response in a mutex-guarded map keyed by the
// id begin() handed out, and poll() drains it from the main thread. No extra
// threads of our own, no bundled binary. TLS, DNS and redirects are the OS's
// job. Compiled with -fno-objc-arc (matching the other .mm files), so the
// blocks retain/release the session task explicitly via the shared state.

#import <Foundation/Foundation.h>

#include "HttpClient.h"

#include <atomic>
#include <map>
#include <mutex>

// Server-trust delegate for the untrusted-certificate session. A delegate-less
// NSURLSession never asks anybody about the certificate, it just fails the task,
// so the only way to accept the Hue bridge's Signify-signed certificate is a
// second session that HAS a delegate. Name carries the ReaSixty prefix because
// Objective-C classes live in one flat namespace shared with every other REAPER
// extension in the process.
@interface ReaSixtyTrustAnyServerDelegate : NSObject <NSURLSessionDelegate>
@end

@implementation ReaSixtyTrustAnyServerDelegate
- (void)URLSession:(NSURLSession*)session
    didReceiveChallenge:(NSURLAuthenticationChallenge*)challenge
      completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition,
                                  NSURLCredential*))completionHandler
{
    // Only the server-trust challenge is answered blind. A proxy or HTTP-auth
    // challenge still goes to the default handling — this session exists to
    // skip a certificate chain, not to hand credentials to anyone who asks.
    if ([challenge.protectionSpace.authenticationMethod
            isEqualToString:NSURLAuthenticationMethodServerTrust]
        && challenge.protectionSpace.serverTrust)
    {
        completionHandler(NSURLSessionAuthChallengeUseCredential,
                          [NSURLCredential credentialForTrust:
                              challenge.protectionSpace.serverTrust]);
        return;
    }
    completionHandler(NSURLSessionAuthChallengePerformDefaultHandling, nil);
}
@end

namespace reasixty::http {
namespace {

std::atomic<uint64_t> g_nextId{1};

struct Pending {
    bool                  done = false;
    Response              response;
    NSURLSessionDataTask* task = nil;   // retained in begin(); for cancel()
};

std::mutex                     g_mutex;
std::map<uint64_t, Pending>    g_requests;

NSURLSession* sharedSession() {
    // One ephemeral session for the whole extension — no cookies, no cache,
    // nothing persisted to disk. Created once.
    static NSURLSession* s_session = nil;
    static std::once_flag s_once;
    std::call_once(s_once, [] {
        NSURLSessionConfiguration* cfg =
            [NSURLSessionConfiguration ephemeralSessionConfiguration];
        cfg.HTTPShouldSetCookies = NO;
        cfg.URLCache = nil;
        s_session = [[NSURLSession sessionWithConfiguration:cfg] retain];
    });
    return s_session;
}

// The same thing with the trust delegate attached. Separate session on purpose:
// the delegate is per-session, so sharing one would make every request in the
// extension certificate-blind instead of just the bridge's. Both the session and
// its delegate are process-lifetime singletons and are never released.
NSURLSession* untrustedCertSession() {
    static NSURLSession* s_session = nil;
    static std::once_flag s_once;
    std::call_once(s_once, [] {
        NSURLSessionConfiguration* cfg =
            [NSURLSessionConfiguration ephemeralSessionConfiguration];
        cfg.HTTPShouldSetCookies = NO;
        cfg.URLCache = nil;
        ReaSixtyTrustAnyServerDelegate* delegate =
            [[ReaSixtyTrustAnyServerDelegate alloc] init];
        s_session = [[NSURLSession sessionWithConfiguration:cfg
                                                   delegate:delegate
                                              delegateQueue:nil] retain];
    });
    return s_session;
}

} // namespace

uint64_t begin(const std::string& method,
               const std::string& url,
               const std::vector<std::string>& headers,
               const std::string& body,
               int timeoutSeconds,
               bool allowUntrustedCert)
{
    @autoreleasepool {
        NSString* nsUrl = [NSString stringWithUTF8String:url.c_str()];
        NSURL* u = nsUrl ? [NSURL URLWithString:nsUrl] : nil;
        if (!u) return 0;

        NSMutableURLRequest* req = [NSMutableURLRequest requestWithURL:u];
        req.HTTPMethod = [NSString stringWithUTF8String:method.c_str()];
        req.timeoutInterval = timeoutSeconds > 0 ? (NSTimeInterval)timeoutSeconds : 20.0;
        for (const std::string& h : headers) {
            const auto colon = h.find(':');
            if (colon == std::string::npos) continue;
            std::string key = h.substr(0, colon);
            std::string val = h.substr(colon + 1);
            // Trim one leading space after the colon, the usual header form.
            if (!val.empty() && val.front() == ' ') val.erase(val.begin());
            [req setValue:[NSString stringWithUTF8String:val.c_str()]
                forHTTPHeaderField:[NSString stringWithUTF8String:key.c_str()]];
        }
        if (!body.empty()) {
            req.HTTPBody = [NSData dataWithBytes:body.data() length:body.size()];
        }

        const uint64_t id = g_nextId.fetch_add(1);
        {
            std::lock_guard<std::mutex> lk(g_mutex);
            g_requests[id] = Pending{};
        }

        NSURLSession* session =
            allowUntrustedCert ? untrustedCertSession() : sharedSession();
        NSURLSessionDataTask* task = [session
            dataTaskWithRequest:req
              completionHandler:^(NSData* data, NSURLResponse* resp, NSError* err) {
            Response r;
            if (err) {
                r.error = [[err localizedDescription] UTF8String] ?: "request failed";
            } else {
                if ([resp isKindOfClass:[NSHTTPURLResponse class]]) {
                    r.status = (long)[(NSHTTPURLResponse*)resp statusCode];
                }
                if (data && data.length) {
                    r.body.assign((const char*)data.bytes, data.length);
                }
            }
            std::lock_guard<std::mutex> lk(g_mutex);
            auto it = g_requests.find(id);
            if (it != g_requests.end()) {         // still wanted (not cancelled)
                it->second.done = true;
                it->second.response = std::move(r);
            }
        }];

        {
            std::lock_guard<std::mutex> lk(g_mutex);
            auto it = g_requests.find(id);
            if (it != g_requests.end()) it->second.task = [task retain];
        }
        [task resume];
        return id;
    }
}

bool poll(uint64_t id, Response& out)
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
    if (it->second.task) [it->second.task release];
    g_requests.erase(it);
    return true;
}

void cancel(uint64_t id)
{
    NSURLSessionDataTask* task = nil;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        auto it = g_requests.find(id);
        if (it == g_requests.end()) return;
        task = it->second.task;
        g_requests.erase(it);   // completion handler will find no entry and drop its result
    }
    [task cancel];
    [task release];
}

} // namespace reasixty::http
