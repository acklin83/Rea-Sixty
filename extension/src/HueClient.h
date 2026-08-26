#pragma once
//
// Philips Hue CLIP API v2 — the wire, and nothing else.
//
// URL builders, request bodies, response parsers. No sockets, no threads, no
// REAPER: HueManager does the I/O through reasixty::http, this unit only turns
// intent into bytes and bytes back into structs. Unit-tested in
// tests/test_hue.cpp, which is the point of the split (DynaMountClient.h has
// the same shape and the same reason).
//
// ---- What the bridge speaks (verified against the openhue/openhue-api
//      OpenAPI spec and Home Assistant's aiohue, not from memory) ------------
//
//   base      https://<ip>/clip/v2/resource/<type>[/<id>]
//   auth      header  hue-application-key: <key>
//   pairing   POST https://<ip>/api  {"devicetype":"…","generateclientkey":true}
//             before the link button:  [{"error":{"type":101,…}}]
//             after:                   [{"success":{"username":…,"clientkey":…}}]
//             `username` IS the application key. There is no second name for it.
//   discovery GET https://discovery.meethue.com/  → [{"id":…,"internalipaddress":…}]
//
// ⛔ THREE FACTS THAT SHAPE EVERY CALLER:
//
// 1. HTTPS ONLY, AND THE CERTIFICATE IS SELF-SIGNED. v2 has no plaintext port.
//    Every request here goes out with reasixty::http's allowUntrustedCert, and
//    the identity check moves up to the stored bridge id (see HttpClient.h).
//
// 2. brightness 0 IS NOT OFF. The spec says so in as many words: "value cannot
//    be 0, writing 0 changes it to lowest possible brightness". Off is
//    {"on":{"on":false}} and nothing else. lightBody() enforces it.
//
// 3. THE RATE LIMITS ARE NOT ADVISORY. Philips says ~10 commands/s for a light
//    and ~1/s for a group, because a group goes out as a Zigbee broadcast and
//    Zigbee manages about 25 commands/s in practice. Past that the bridge drops
//    commands silently, which reads as a sticky fader rather than as an error.
//    kLightMinGapMs / kGroupMinGapMs below are those numbers.

#include "HueColor.h"

#include <string>
#include <vector>

namespace uf8::hue {

// ---- wire constants ---------------------------------------------------------

inline constexpr int kLightMinGapMs = 100;    // ~10 writes/s to one light
inline constexpr int kGroupMinGapMs = 1000;   // ~1 write/s to a grouped_light

// Resource type names as they appear in the path. Spelled out so a typo is a
// link error rather than a 404 at three in the morning.
inline constexpr const char* kTypeLight        = "light";
inline constexpr const char* kTypeGroupedLight = "grouped_light";
inline constexpr const char* kTypeRoom         = "room";
inline constexpr const char* kTypeZone         = "zone";
inline constexpr const char* kTypeScene        = "scene";
inline constexpr const char* kTypeBridge       = "bridge";

// A Hue target is a single light or the grouped_light service of a room/zone.
// The only behavioural difference is the rate limit, but that difference is big
// enough to be worth carrying in the type.
enum class TargetKind : uint8_t { Light = 0, Group = 1 };

const char* typeForKind(TargetKind k);
int         minGapMsForKind(TargetKind k);

// ---- URLs -------------------------------------------------------------------

std::string discoveryUrl();
std::string pairUrl(const std::string& ip);
std::string resourceUrl(const std::string& ip, const char* type);
std::string resourceUrl(const std::string& ip, const char* type,
                        const std::string& id);
std::string eventStreamUrl(const std::string& ip);

// Full header line for reasixty::http.
std::string appKeyHeader(const std::string& appKey);

// ---- request bodies ---------------------------------------------------------

// devicetype must look like "app#instance"; Hue shows the part after the # in
// its app so the user can see which machine paired.
std::string pairBody(const std::string& appName, const std::string& instance);

// What to write to one light or grouped_light. Everything is optional: a body
// with only `on` set is a legal and cheap on/off. `durationMs` < 0 leaves
// `dynamics` out entirely.
//
// ⇨ `on` and `brightness` are separate on purpose. Setting brightness while the
//   lamp is off turns nothing on, and setting on:true without a brightness
//   restores whatever it had. The fader path wants both, the CUT key wants one.
struct LightWrite {
    bool   setOn       = false;
    bool   on          = true;
    bool   setBri      = false;
    double briPercent  = 100.0;   // 1..100; 0 is not off, see the header
    bool   setXy       = false;
    Xy     xy;
    bool   setMirek    = false;
    int    mirek       = kMirekMin;
    int    durationMs  = -1;      // < 0 → no "dynamics"
};
std::string lightBody(const LightWrite& w);

// PUT on a scene. `dynamic` picks dynamic_palette over active — that is the
// difference between "put the room in this scene" and "start this scene
// cycling". briPercent < 0 leaves the scene's own levels alone.
std::string sceneRecallBody(bool dynamic, int durationMs, double briPercent = -1.0);

// ---- response parsing -------------------------------------------------------

struct DiscoveredBridge {
    std::string id;      // bridge id, lower case hex
    std::string ip;      // internalipaddress
};
std::vector<DiscoveredBridge> parseDiscovery(const std::string& json);

struct PairResult {
    bool        ok        = false;
    bool        waiting   = false;   // error type 101: link button not pressed
    std::string appKey;              // the "username" field
    std::string clientKey;           // only present with generateclientkey
    std::string error;               // human-readable, empty when ok or waiting
};
PairResult parsePairResponse(const std::string& json);

struct Light {
    std::string id;
    std::string name;
    std::string ownerRid;      // the device this light service belongs to
    bool        on        = false;
    bool        dimmable  = false;
    double      briPercent = 0.0;
    bool        hasXy     = false;
    Xy          xy;
    bool        hasMirek  = false;
    int         mirek     = 0;
    Gamut       gamut;
};
std::vector<Light> parseLights(const std::string& json);

// A room or a zone. `groupedLightId` is the id to WRITE to; the room's own id
// is not a thing you can dim. A room with no grouped_light service comes back
// with that field empty and must not be offered as a target.
struct Group {
    std::string id;
    std::string name;
    std::string groupedLightId;
    bool        isZone = false;
};
std::vector<Group> parseGroups(const std::string& json, bool zones);

struct Scene {
    std::string id;
    std::string name;
    std::string groupRid;    // the room or zone the scene belongs to
    bool        active = false;
};
std::vector<Scene> parseScenes(const std::string& json);

// The bridge id out of GET /clip/v2/resource/bridge. Empty on anything
// unexpected — and an empty answer must be treated as "not our bridge", because
// this is the only identity check left once the certificate is not verified.
std::string parseBridgeId(const std::string& json);

// True when a CLIP v2 response body carries a non-empty "errors" array. The
// bridge answers 200 with errors inside more often than it answers 4xx.
bool hasApiError(const std::string& json, std::string* firstMessage = nullptr);

} // namespace uf8::hue
