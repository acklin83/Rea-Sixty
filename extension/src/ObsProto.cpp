#include "ObsProto.h"

#include "JsonTree.h"
#include "Sha256.h"

#include <cstdlib>
#include <cstring>

namespace reasixty::obs {
namespace {

// Same three helpers HueClient has, for the same parser.
const char* str(const wdl_json_element* obj, const char* key)
{
    if (!obj) return nullptr;
    const wdl_json_element* v = obj->get_item_by_name(key);
    return v ? v->get_string_value(true) : nullptr;
}

std::string text(const wdl_json_element* obj, const char* key)
{
    const char* s = str(obj, key);
    return s ? std::string(s) : std::string();
}

bool boolean(const wdl_json_element* obj, const char* key, bool dflt = false)
{
    const char* s = str(obj, key);
    if (!s) return dflt;
    return std::strcmp(s, "true") == 0;
}

const wdl_json_element* child(const wdl_json_element* obj, const char* key)
{
    return obj ? obj->get_item_by_name(key) : nullptr;
}

// Both a RecordStateChanged event and a GetRecordStatus response carry the same
// three facts under different names, so one reader serves both.
void readRecord(const wdl_json_element* d, Message& m)
{
    if (!d) return;
    m.hasRecord      = true;
    m.recordActive   = boolean(d, "outputActive");
    m.recordPaused   = boolean(d, "outputPaused");
    m.recordState    = text(d, "outputState");
    m.recordTimecode = text(d, "outputTimecode");
}

}  // namespace

std::string jsonEscape(const std::string& s)
{
    std::string o;
    o.reserve(s.size() + 8);
    for (const char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                // Control characters have to go out as \u00XX; everything else,
                // UTF-8 included, travels as itself.
                if (static_cast<unsigned char>(c) < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof(b), "\\u%04x", c & 0xFF);
                    o += b;
                } else {
                    o += c;
                }
        }
    }
    return o;
}

std::string authToken(const std::string& password, const std::string& salt,
                      const std::string& challenge)
{
    const std::string secret = sha256Base64(password + salt);
    return sha256Base64(secret + challenge);
}

std::string identify(const std::string& auth, int eventSubscriptions)
{
    std::string s = "{\"op\":1,\"d\":{\"rpcVersion\":1";
    if (!auth.empty()) s += ",\"authentication\":\"" + jsonEscape(auth) + "\"";
    s += ",\"eventSubscriptions\":" + std::to_string(eventSubscriptions);
    s += "}}";
    return s;
}

std::string request(const std::string& type, const std::string& id,
                    const std::string& dataJson)
{
    std::string s = "{\"op\":6,\"d\":{\"requestType\":\"" + jsonEscape(type) + "\"";
    s += ",\"requestId\":\"" + jsonEscape(id) + "\"";
    if (!dataJson.empty()) s += ",\"requestData\":" + dataJson;
    s += "}}";
    return s;
}

std::string sceneSwitchData(const std::string& sceneName)
{
    return "{\"sceneName\":\"" + jsonEscape(sceneName) + "\"}";
}

std::string chapterData(const std::string& chapterName)
{
    if (chapterName.empty()) return std::string();
    return "{\"chapterName\":\"" + jsonEscape(chapterName) + "\"}";
}

bool parse(const std::string& json, Message& out)
{
    out = Message{};
    wdl_json_parser p;
    const wdl_json_element* root = p.parse(json.c_str(),
                                           static_cast<int>(json.size()));
    JsonTreeGuard rootGuard{p, root};
    if (!root || !root->is_object()) return false;

    const char* opStr = str(root, "op");
    if (!opStr) return false;
    const int op = std::atoi(opStr);
    const wdl_json_element* d = child(root, "d");

    switch (op) {
        case 0: {                                   // Hello
            out.kind       = MsgKind::Hello;
            out.rpcVersion = std::atoi(text(d, "rpcVersion").c_str());
            out.obsVersion = text(d, "obsStudioVersion");
            if (const wdl_json_element* a = child(d, "authentication")) {
                out.authRequired = true;
                out.salt         = text(a, "salt");
                out.challenge    = text(a, "challenge");
            }
            return true;
        }
        case 2:                                     // Identified
            out.kind       = MsgKind::Identified;
            out.rpcVersion = std::atoi(text(d, "negotiatedRpcVersion").c_str());
            return true;

        case 5: {                                   // Event
            out.kind      = MsgKind::Event;
            out.eventType = text(d, "eventType");
            const wdl_json_element* ed = child(d, "eventData");
            if (out.eventType == "RecordStateChanged") {
                readRecord(ed, out);
            } else if (out.eventType == "CurrentProgramSceneChanged") {
                out.hasScene  = true;
                out.sceneName = text(ed, "sceneName");
            }
            return true;
        }
        case 7: {                                   // RequestResponse
            out.kind        = MsgKind::Response;
            out.requestType = text(d, "requestType");
            out.requestId   = text(d, "requestId");
            const wdl_json_element* st = child(d, "requestStatus");
            out.ok      = boolean(st, "result");
            out.comment = text(st, "comment");
            const wdl_json_element* rd = child(d, "responseData");
            if (out.requestType == "GetRecordStatus") {
                readRecord(rd, out);
            } else if (out.requestType == "GetSceneList" && rd) {
                out.hasScenes    = true;
                out.currentScene = text(rd, "currentProgramSceneName");
                if (const wdl_json_element* arr = child(rd, "scenes")) {
                    // The order is OBS's own: the array is taken as it comes.
                    // The protocol document does not say how it relates to the
                    // order the scene list shows in the UI, and guessing would
                    // put the keys upside down half the time — so that is a
                    // question for the first run at the device, and a one-line
                    // flip here if it turns out reversed.
                    const int n = arr->m_array ? arr->m_array->GetSize() : 0;
                    out.scenes.reserve(static_cast<size_t>(n < 0 ? 0 : n));
                    for (int i = 0; i < n; ++i) {
                        const wdl_json_element* e = arr->enum_item(i);
                        const std::string nm = text(e, "sceneName");
                        if (!nm.empty()) out.scenes.push_back(nm);
                    }
                }
            }
            return true;
        }
        default:
            out.kind = MsgKind::Unknown;
            return true;          // a message we do not need is not an error
    }
}

}  // namespace reasixty::obs
