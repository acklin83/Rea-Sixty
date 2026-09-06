#pragma once
//
// obs-websocket v5 — the wire, and nothing else.
//
// Message builders and parsers. No sockets, no threads, no REAPER: ObsManager
// does the I/O through WsClient, this unit only turns intent into JSON and JSON
// back into structs. Unit-tested in tests/test_obs.cpp, which is the point of
// the split — HueClient.h has the same shape for the same reason.
//
// ---- What OBS speaks (from the official protocol document, not from memory) --
//
//   transport  plain WebSocket, ws://<host>:4455, no TLS. Bundled with OBS 28+.
//   envelope   {"op": <number>, "d": {…}}
//   opcodes    0 Hello (server) · 1 Identify (us) · 2 Identified (server)
//              5 Event (server) · 6 Request (us) · 7 RequestResponse (server)
//   rpcVersion 1
//   login      when Hello carries authentication{salt,challenge}:
//                base64(sha256(base64(sha256(password + salt)) + challenge))
//              No authentication object means the server wants no password.
//   events     subscribe with a bitmask; Scenes = 1<<2, Outputs = 1<<6.
//              We ask for exactly those two: scene changes and record state.
//
// ⛔ EVERY SCALAR COMES OUT OF THE WDL PARSER AS A STRING, numbers and booleans
// included, so "true" is compared as text. Same as HueClient.
//
// ⛔ The parser does not free its tree. Every parse in the .cpp holds a
// JsonTreeGuard (JsonTree.h) — the 171 GB lesson from 2026-08-27.

#include <string>
#include <vector>

namespace reasixty::obs {

// Scenes (1<<2) | Outputs (1<<6). Anything else would arrive and be dropped.
inline constexpr int kEventSubscriptions = (1 << 2) | (1 << 6);

enum class MsgKind { Unknown, Hello, Identified, Event, Response };

struct Message {
    MsgKind kind = MsgKind::Unknown;

    // Hello
    bool        authRequired = false;
    std::string salt, challenge;
    int         rpcVersion = 0;
    std::string obsVersion;

    // Event / RequestResponse
    std::string eventType;
    std::string requestType, requestId;
    bool        ok = false;              // requestStatus.result
    std::string comment;                 // requestStatus.comment, when it failed

    // The payloads we care about, already picked apart.
    bool        hasRecord   = false;     // RecordStateChanged or GetRecordStatus
    bool        recordActive = false;
    bool        recordPaused = false;
    std::string recordState;             // OBS_WEBSOCKET_OUTPUT_STARTED, …
    std::string recordTimecode;

    bool        hasScene = false;        // CurrentProgramSceneChanged
    std::string sceneName;

    bool                     hasScenes = false;   // GetSceneList
    std::vector<std::string> scenes;              // in OBS's own order
    std::string              currentScene;
};

// ---- outbound ------------------------------------------------------------

std::string jsonEscape(const std::string& s);

// base64(sha256(base64(sha256(password + salt)) + challenge))
std::string authToken(const std::string& password, const std::string& salt,
                      const std::string& challenge);

// op 1. Empty auth = the server asked for none.
std::string identify(const std::string& auth, int eventSubscriptions);

// op 6. `dataJson` is a complete object body ("{\"sceneName\":\"…\"}") or empty.
std::string request(const std::string& type, const std::string& id,
                    const std::string& dataJson = std::string());

// The bodies we send, so the request types live in one place.
std::string sceneSwitchData(const std::string& sceneName);
std::string chapterData(const std::string& chapterName);

// ---- inbound -------------------------------------------------------------

// false when the text is not a message we understand; `out` is then untouched
// apart from kind = Unknown.
bool parse(const std::string& json, Message& out);

}  // namespace reasixty::obs
