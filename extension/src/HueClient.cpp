#include "HueClient.h"

#include "WDL/jsonparse.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace uf8::hue {
namespace {

std::string jsonEscape(const std::string& in)
{
    std::string out;
    out.reserve(in.size() + 8);
    for (const char c : in) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof(b), "\\u%04x",
                                  static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += b;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// Fixed notation with enough digits for a chromaticity. %g would emit
// scientific notation for a small y and the bridge rejects that.
std::string num(double v, int decimals)
{
    char b[64];
    std::snprintf(b, sizeof(b), "%.*f", decimals, v);
    return b;
}

// Every scalar comes out of the WDL parser as a string, numbers included.
const char* str(const wdl_json_element* obj, const char* key)
{
    if (!obj) return nullptr;
    const wdl_json_element* v = obj->get_item_by_name(key);
    return v ? v->get_string_value(true) : nullptr;
}

double dbl(const wdl_json_element* obj, const char* key, double dflt = 0.0)
{
    const char* s = str(obj, key);
    return s ? std::atof(s) : dflt;
}

bool boolean(const wdl_json_element* obj, const char* key, bool dflt = false)
{
    const char* s = str(obj, key);
    if (!s) return dflt;
    return std::strcmp(s, "true") == 0;
}

std::string text(const wdl_json_element* obj, const char* key)
{
    const char* s = str(obj, key);
    return s ? std::string(s) : std::string();
}

const wdl_json_element* child(const wdl_json_element* obj, const char* key)
{
    return obj ? obj->get_item_by_name(key) : nullptr;
}

int arraySize(const wdl_json_element* e)
{
    return (e && e->m_array) ? e->m_array->GetSize() : 0;
}

// CLIP v2 wraps every collection: {"errors":[…], "data":[…]}. Returns the data
// array, or null when the shape is not what we expect.
const wdl_json_element* dataArray(const wdl_json_element* root)
{
    if (!root || !root->is_object()) return nullptr;
    const wdl_json_element* d = root->get_item_by_name("data");
    return (d && d->is_array()) ? d : nullptr;
}

// "xy": {"x": …, "y": …} → Xy, with a flag for "was it even there".
bool readXy(const wdl_json_element* obj, Xy* out)
{
    if (!obj || !out) return false;
    const wdl_json_element* xy = obj->get_item_by_name("xy");
    if (!xy) return false;
    out->x = dbl(xy, "x");
    out->y = dbl(xy, "y");
    return true;
}

} // namespace

// ---- kinds ------------------------------------------------------------------

const char* typeForKind(TargetKind k)
{
    return (k == TargetKind::Group) ? kTypeGroupedLight : kTypeLight;
}

int minGapMsForKind(TargetKind k)
{
    return (k == TargetKind::Group) ? kGroupMinGapMs : kLightMinGapMs;
}

// ---- URLs -------------------------------------------------------------------

std::string discoveryUrl()
{
    // The one call in Hue Mode that goes to the internet instead of the LAN, and
    // the one that does NOT need the certificate switch: meethue.com has a
    // perfectly ordinary public certificate.
    return "https://discovery.meethue.com/";
}

std::string pairUrl(const std::string& ip)
{
    // Pairing is still the v1 endpoint. v2 has no way to mint its own key.
    return "https://" + ip + "/api";
}

std::string resourceUrl(const std::string& ip, const char* type)
{
    return "https://" + ip + "/clip/v2/resource/" + (type ? type : "");
}

std::string resourceUrl(const std::string& ip, const char* type,
                        const std::string& id)
{
    return resourceUrl(ip, type) + "/" + id;
}

std::string eventStreamUrl(const std::string& ip)
{
    return "https://" + ip + "/eventstream/clip/v2";
}

std::string appKeyHeader(const std::string& appKey)
{
    return "hue-application-key: " + appKey;
}

// ---- request bodies ---------------------------------------------------------

std::string pairBody(const std::string& appName, const std::string& instance)
{
    // generateclientkey also returns the DTLS pre-shared key for the
    // Entertainment API. We do not stream (no DTLS in this tree), but asking for
    // it costs one field and pairing happens once — not asking would mean a
    // second link-button dance the day streaming ever lands.
    return "{\"devicetype\":\"" + jsonEscape(appName) + "#"
         + jsonEscape(instance) + "\",\"generateclientkey\":true}";
}

std::string lightBody(const LightWrite& w)
{
    std::string out = "{";
    bool first = true;
    auto comma = [&]() { if (!first) out += ","; first = false; };

    if (w.setOn) {
        comma();
        out += "\"on\":{\"on\":";
        out += w.on ? "true" : "false";
        out += "}";
    }

    // ⛔ 0 is not off (see the header). Anything at or below zero is lifted to
    // the smallest legal value; turning the lamp off is the caller's separate
    // decision via setOn.
    if (w.setBri) {
        double bri = w.briPercent;
        if (bri < 1.0)   bri = 1.0;
        if (bri > 100.0) bri = 100.0;
        comma();
        out += "\"dimming\":{\"brightness\":" + num(bri, 2) + "}";
    }

    // xy and mirek are the same setting seen from two sides — a light is in
    // colour mode or in white mode, never both. Sending both would let the
    // bridge pick, so xy wins and mirek is only written when xy is not.
    if (w.setXy) {
        comma();
        out += "\"color\":{\"xy\":{\"x\":" + num(w.xy.x, 4)
             + ",\"y\":" + num(w.xy.y, 4) + "}}";
    } else if (w.setMirek) {
        int m = w.mirek;
        if (m < kMirekMin) m = kMirekMin;
        if (m > kMirekMax) m = kMirekMax;
        comma();
        out += "\"color_temperature\":{\"mirek\":" + std::to_string(m) + "}";
    }

    if (w.durationMs >= 0) {
        comma();
        out += "\"dynamics\":{\"duration\":" + std::to_string(w.durationMs) + "}";
    }

    out += "}";
    return out;
}

std::string sceneRecallBody(bool dynamic, int durationMs, double briPercent)
{
    std::string out = "{\"recall\":{\"action\":";
    out += dynamic ? "\"dynamic_palette\"" : "\"active\"";
    if (durationMs >= 0)
        out += ",\"duration\":" + std::to_string(durationMs);
    if (briPercent >= 0.0) {
        double bri = briPercent;
        if (bri < 1.0)   bri = 1.0;
        if (bri > 100.0) bri = 100.0;
        out += ",\"dimming\":{\"brightness\":" + num(bri, 2) + "}";
    }
    out += "}}";
    return out;
}

// ---- response parsing -------------------------------------------------------

std::vector<DiscoveredBridge> parseDiscovery(const std::string& json)
{
    std::vector<DiscoveredBridge> out;
    wdl_json_parser p;
    const wdl_json_element* root = p.parse(json.c_str(),
                                           static_cast<int>(json.size()));
    if (!root || !root->is_array()) return out;

    const int n = arraySize(root);
    for (int i = 0; i < n; ++i) {
        const wdl_json_element* e = root->enum_item(i);
        if (!e || !e->is_object()) continue;
        DiscoveredBridge b;
        b.id = text(e, "id");
        b.ip = text(e, "internalipaddress");
        if (!b.ip.empty()) out.push_back(std::move(b));
    }
    return out;
}

PairResult parsePairResponse(const std::string& json)
{
    PairResult r;
    wdl_json_parser p;
    const wdl_json_element* root = p.parse(json.c_str(),
                                           static_cast<int>(json.size()));
    if (!root || !root->is_array() || arraySize(root) < 1) {
        r.error = "the bridge answered something that is not a pairing reply";
        return r;
    }

    const wdl_json_element* first = root->enum_item(0);
    if (!first || !first->is_object()) {
        r.error = "the bridge answered something that is not a pairing reply";
        return r;
    }

    if (const wdl_json_element* ok = first->get_item_by_name("success")) {
        r.appKey    = text(ok, "username");
        r.clientKey = text(ok, "clientkey");
        r.ok        = !r.appKey.empty();
        if (!r.ok) r.error = "the bridge accepted the pairing but sent no key";
        return r;
    }

    if (const wdl_json_element* err = first->get_item_by_name("error")) {
        // 101 is the whole point of the pairing loop: it means "go press the
        // button", not "this failed". Every other type is a real failure.
        const int type = static_cast<int>(dbl(err, "type", 0.0));
        if (type == 101) { r.waiting = true; return r; }
        r.error = text(err, "description");
        if (r.error.empty()) r.error = "the bridge refused the pairing";
        return r;
    }

    r.error = "the bridge answered something that is not a pairing reply";
    return r;
}

std::vector<Light> parseLights(const std::string& json)
{
    std::vector<Light> out;
    wdl_json_parser p;
    const wdl_json_element* root = p.parse(json.c_str(),
                                           static_cast<int>(json.size()));
    const wdl_json_element* data = dataArray(root);
    if (!data) return out;

    const int n = arraySize(data);
    for (int i = 0; i < n; ++i) {
        const wdl_json_element* e = data->enum_item(i);
        if (!e || !e->is_object()) continue;

        Light L;
        L.id   = text(e, "id");
        if (L.id.empty()) continue;
        L.name = text(child(e, "metadata"), "name");
        L.ownerRid = text(child(e, "owner"), "rid");
        L.on   = boolean(child(e, "on"), "on");

        if (const wdl_json_element* dim = child(e, "dimming")) {
            L.dimmable   = true;
            L.briPercent = dbl(dim, "brightness", 0.0);
        }

        if (const wdl_json_element* ct = child(e, "color_temperature")) {
            // mirek is null on a lamp that is currently in colour mode, and the
            // bridge tells us so in mirek_valid rather than by omitting it.
            if (boolean(ct, "mirek_valid", false)) {
                L.hasMirek = true;
                L.mirek    = static_cast<int>(dbl(ct, "mirek", kMirekMin));
            }
        }

        if (const wdl_json_element* col = child(e, "color")) {
            L.hasXy = readXy(col, &L.xy);
            if (const wdl_json_element* gm = col->get_item_by_name("gamut")) {
                const wdl_json_element* gr = gm->get_item_by_name("red");
                const wdl_json_element* gg = gm->get_item_by_name("green");
                const wdl_json_element* gb = gm->get_item_by_name("blue");
                if (gr && gg && gb) {
                    L.gamut.red   = Xy{ dbl(gr, "x"), dbl(gr, "y") };
                    L.gamut.green = Xy{ dbl(gg, "x"), dbl(gg, "y") };
                    L.gamut.blue  = Xy{ dbl(gb, "x"), dbl(gb, "y") };
                    L.gamut.valid = true;
                }
            }
        }
        out.push_back(std::move(L));
    }
    return out;
}

std::vector<Group> parseGroups(const std::string& json, bool zones)
{
    std::vector<Group> out;
    wdl_json_parser p;
    const wdl_json_element* root = p.parse(json.c_str(),
                                           static_cast<int>(json.size()));
    const wdl_json_element* data = dataArray(root);
    if (!data) return out;

    const int n = arraySize(data);
    for (int i = 0; i < n; ++i) {
        const wdl_json_element* e = data->enum_item(i);
        if (!e || !e->is_object()) continue;

        Group g;
        g.id     = text(e, "id");
        if (g.id.empty()) continue;
        g.name   = text(child(e, "metadata"), "name");
        g.isZone = zones;

        // Members: device rids for a room, light-service rids for a zone. Both
        // shapes go in the same list; the caller resolves whichever matches.
        if (const wdl_json_element* kids = e->get_item_by_name("children")) {
            const int m = arraySize(kids);
            for (int j = 0; j < m; ++j) {
                const wdl_json_element* c = kids->enum_item(j);
                if (!c || !c->is_object()) continue;
                std::string rid = text(c, "rid");
                if (!rid.empty()) g.childRids.push_back(std::move(rid));
            }
        }

        // ⇨ The writable id is NOT the room's. A room carries a `services` array
        //   and the grouped_light entry in it is the thing that dims. A room with
        //   no such service (no lights in it yet) stays unusable on purpose.
        if (const wdl_json_element* svc = e->get_item_by_name("services")) {
            const int m = arraySize(svc);
            for (int j = 0; j < m; ++j) {
                const wdl_json_element* s = svc->enum_item(j);
                if (!s || !s->is_object()) continue;
                if (text(s, "rtype") == kTypeGroupedLight) {
                    g.groupedLightId = text(s, "rid");
                    break;
                }
            }
        }
        out.push_back(std::move(g));
    }
    return out;
}

std::vector<GroupedLight> parseGroupedLights(const std::string& json)
{
    std::vector<GroupedLight> out;
    wdl_json_parser p;
    const wdl_json_element* root = p.parse(json.c_str(),
                                           static_cast<int>(json.size()));
    const wdl_json_element* data = dataArray(root);
    if (!data) return out;

    const int n = arraySize(data);
    for (int i = 0; i < n; ++i) {
        const wdl_json_element* e = data->enum_item(i);
        if (!e || !e->is_object()) continue;

        GroupedLight g;
        g.id = text(e, "id");
        if (g.id.empty()) continue;
        g.on = boolean(child(e, "on"), "on");
        if (const wdl_json_element* dim = child(e, "dimming")) {
            g.dimmable   = true;
            g.briPercent = dbl(dim, "brightness", 0.0);
        }
        out.push_back(std::move(g));
    }
    return out;
}

std::vector<Scene> parseScenes(const std::string& json)
{
    std::vector<Scene> out;
    wdl_json_parser p;
    const wdl_json_element* root = p.parse(json.c_str(),
                                           static_cast<int>(json.size()));
    const wdl_json_element* data = dataArray(root);
    if (!data) return out;

    const int n = arraySize(data);
    for (int i = 0; i < n; ++i) {
        const wdl_json_element* e = data->enum_item(i);
        if (!e || !e->is_object()) continue;

        Scene s;
        s.id = text(e, "id");
        if (s.id.empty()) continue;
        s.name     = text(child(e, "metadata"), "name");
        s.groupRid = text(child(e, "group"), "rid");

        // status.active is "inactive" | "static" | "dynamic_palette". Anything
        // that is not "inactive" means the room is showing this scene right now,
        // which is what the key LED wants to know.
        if (const wdl_json_element* st = child(e, "status")) {
            const std::string a = text(st, "active");
            s.active = !a.empty() && a != "inactive";
            // Undocumented but sent, and in practice the only signal there is.
            s.lastRecall = text(st, "last_recall");
        }
        out.push_back(std::move(s));
    }
    return out;
}

std::string parseBridgeId(const std::string& json)
{
    wdl_json_parser p;
    const wdl_json_element* root = p.parse(json.c_str(),
                                           static_cast<int>(json.size()));
    const wdl_json_element* data = dataArray(root);
    if (!data || arraySize(data) < 1) return std::string();
    return text(data->enum_item(0), "bridge_id");
}

bool hasApiError(const std::string& json, std::string* firstMessage)
{
    wdl_json_parser p;
    const wdl_json_element* root = p.parse(json.c_str(),
                                           static_cast<int>(json.size()));
    if (!root || !root->is_object()) return false;
    const wdl_json_element* errs = root->get_item_by_name("errors");
    if (!errs || !errs->is_array() || arraySize(errs) < 1) return false;
    if (firstMessage) *firstMessage = text(errs->enum_item(0), "description");
    return true;
}

} // namespace uf8::hue
