#include "Performers.h"

#include "JsonTree.h"
#include "Phones.h"
#include "RmeUf1.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace orc::phones {

namespace rmeu = reasixty::rme::uf1;

namespace {

constexpr std::size_t kMaxText       = 64;
constexpr std::size_t kMaxPerformers = 64;
constexpr std::size_t kMaxFaders     = 128;
constexpr std::size_t kMaxSnapshots  = 64;

std::string jstr(const std::string& s)
{
    std::string o = "\"";
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
        else o.push_back(ch);
    }
    return o + "\"";
}

const char* jbool(bool v) { return v ? "true" : "false"; }

std::string performerJson(const Performer& p)
{
    std::string f = "[";
    for (std::size_t i = 0; i < p.faders.size(); ++i) {
        const Fader& x = p.faders[i];
        if (i) f += ",";
        f += "{\"src\":" + jstr(x.src) + ",\"name\":" + jstr(x.name) + ",\"colour\":" + jstr(x.colour) + "}";
    }
    f += "]";
    return "{\"id\":" + jstr(p.id) + ",\"name\":" + jstr(p.name) + ",\"role\":" + jstr(p.role)
         + ",\"output\":" + jstr(p.output) + ",\"colour\":" + jstr(p.colour)
         + ",\"useLayout\":" + jbool(p.useLayout) + ",\"faders\":" + f
         + ",\"master\":" + jbool(p.master) + ",\"masterEq\":" + jbool(p.masterEq)
         + ",\"masterDyn\":" + jbool(p.masterDyn) + ",\"masterAuto\":" + jbool(p.masterAuto)
         + ",\"chanEq\":" + std::to_string(p.chanEq) + ",\"chanDyn\":" + std::to_string(p.chanDyn)
         + ",\"fxSend\":" + jbool(p.fxSend) + ",\"hide\":" + jbool(p.hide) + "}";
}

std::string performersJson(const std::vector<Performer>& v)
{
    std::string o = "[";
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) o += ",";
        o += performerJson(v[i]);
    }
    return o + "]";
}

std::string text(const wdl_json_element* e, const char* k)
{
    const char* v = e->get_string_by_name(k);
    std::string s = v ? v : "";
    if (s.size() > kMaxText) s.resize(kMaxText);
    return s;
}

bool flag(const wdl_json_element* e, const char* k, bool dflt)
{
    const char* v = e->get_string_by_name(k, true);
    return v ? std::strcmp(v, "true") == 0 : dflt;
}

int level(const wdl_json_element* e, const char* k)
{
    const char* v = e->get_string_by_name(k, true);
    return v ? std::clamp(std::atoi(v), 0, 2) : 0;
}

// "#rrggbb" or nothing: it ends up in a style attribute on the phones.
std::string colourOf(const std::string& c)
{
    if (c.size() != 7 || c[0] != '#') return {};
    for (std::size_t i = 1; i < 7; ++i)
        if (!std::isxdigit(static_cast<unsigned char>(c[i]))) return {};
    return c;
}

bool validSrc(const std::string& s)
{
    const std::size_t c = s.find(':');
    if (c == std::string::npos) return false;
    const std::string kind = s.substr(0, c);
    if (kind != "in" && kind != "pb") return false;
    if (c + 1 >= s.size() || s.size() - c - 1 > 6) return false;
    return std::all_of(s.begin() + static_cast<long>(c) + 1, s.end(),
                       [](char ch) { return ch >= '0' && ch <= '9'; });
}

bool validOutput(const std::string& s)
{
    if (s.empty() || s == "main" || s == "mainB") return true;
    if (s == "phones1" || s == "phones2" || s == "phones3" || s == "phones4") return true;
    if (s.rfind("output:", 0) != 0 || s.size() == 7 || s.size() > 13) return false;
    return std::all_of(s.begin() + 7, s.end(), [](char ch) { return ch >= '0' && ch <= '9'; });
}

// The fields a performer has, from a JSON object. `id` is the caller's.
void readFields(const wdl_json_element* e, Performer& p)
{
    p.name   = text(e, "name");
    p.role   = text(e, "role");
    const std::string out = text(e, "output");
    p.output = validOutput(out) ? out : std::string();
    p.colour = colourOf(text(e, "colour"));
    p.useLayout  = flag(e, "useLayout", true);
    p.master     = flag(e, "master", true);
    p.masterEq   = flag(e, "masterEq", false);
    p.masterDyn  = flag(e, "masterDyn", false);
    p.masterAuto = flag(e, "masterAuto", false);
    p.chanEq  = level(e, "chanEq");
    p.chanDyn = level(e, "chanDyn");
    p.fxSend  = flag(e, "fxSend", false);
    p.hide    = flag(e, "hide", false);
    p.faders.clear();
    if (const wdl_json_element* arr = e->get_item_by_name("faders"); arr && arr->is_array()) {
        for (int i = 0; p.faders.size() < kMaxFaders; ++i) {
            const wdl_json_element* x = arr->enum_item(i);
            if (!x) break;
            if (!x->is_object()) continue;
            Fader f;
            f.src = text(x, "src");
            if (!validSrc(f.src)) continue;
            // One channel once: two faders on one mix point would fight.
            if (std::any_of(p.faders.begin(), p.faders.end(),
                            [&](const Fader& o) { return o.src == f.src; })) continue;
            f.name   = text(x, "name");
            f.colour = colourOf(text(x, "colour"));
            p.faders.push_back(std::move(f));
        }
    }
}

std::vector<Performer> readPerformers(const wdl_json_element* arr)
{
    std::vector<Performer> out;
    if (!arr || !arr->is_array()) return out;
    for (int i = 0; out.size() < kMaxPerformers; ++i) {
        const wdl_json_element* e = arr->enum_item(i);
        if (!e) break;
        if (!e->is_object()) continue;
        Performer p;
        p.id = text(e, "id");
        if (!validToken(p.id)) continue;
        if (std::any_of(out.begin(), out.end(), [&](const Performer& o) { return o.id == p.id; }))
            continue;
        readFields(e, p);
        out.push_back(std::move(p));
    }
    return out;
}

} // namespace

bool validToken(const std::string& t)
{
    if (t.size() < 4 || t.size() > 16) return false;
    return std::all_of(t.begin(), t.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    });
}

std::string bookJson(const Book& b)
{
    std::string snaps = "{";
    bool first = true;
    for (const auto& [name, v] : b.snapshots) {
        if (!first) snaps += ",";
        first = false;
        snaps += jstr(name) + ":" + performersJson(v);
    }
    snaps += "}";
    return "{\"version\":1,\"current\":" + jstr(b.current)
         + ",\"performers\":" + performersJson(b.performers)
         + ",\"snapshots\":" + snaps + "}";
}

bool parseBook(const std::string& json, Book& out)
{
    wdl_json_parser p;
    const wdl_json_element* root = p.parse(json.c_str(), static_cast<int>(json.size()));
    JsonTreeGuard rootGuard{p, root};
    if (!root || !root->is_object()) return false;
    Book b;
    b.performers = readPerformers(root->get_item_by_name("performers"));
    b.current    = text(root, "current");
    if (const wdl_json_element* s = root->get_item_by_name("snapshots"); s && s->is_object()) {
        for (int i = 0; b.snapshots.size() < kMaxSnapshots; ++i) {
            const wdl_json_element* e = s->enum_item(i);
            const char* name = s->enum_item_name(i);
            if (!e || !name) break;
            std::string n = name;
            if (n.empty() || n.size() > kMaxText) continue;
            b.snapshots[n] = readPerformers(e);
        }
    }
    if (!b.snapshots.count(b.current)) b.current.clear();
    out = std::move(b);
    return true;
}

bool applyDesk(Book& b, const std::string& json, const std::function<std::string()>& token)
{
    wdl_json_parser jp;
    const wdl_json_element* root = jp.parse(json.c_str(), static_cast<int>(json.size()));
    JsonTreeGuard rootGuard{jp, root};
    if (!root || !root->is_object()) return false;
    const std::string t = text(root, "t");
    const std::string id = text(root, "id");
    auto find = [&]() {
        return std::find_if(b.performers.begin(), b.performers.end(),
                            [&](const Performer& p) { return p.id == id; });
    };

    if (t == "add") {
        if (b.performers.size() >= kMaxPerformers) return false;
        Performer p;
        p.id   = token();
        if (!validToken(p.id)) return false;
        p.name = "Performer " + std::to_string(b.performers.size() + 1);
        // The first headphone nobody has yet.
        for (const char* o : { "phones1", "phones2", "phones3", "phones4" }) {
            if (std::none_of(b.performers.begin(), b.performers.end(),
                             [&](const Performer& x) { return x.output == o; })) {
                p.output = o;
                break;
            }
        }
        b.performers.push_back(std::move(p));
        return true;
    }
    if (t == "update") {
        const auto it = find();
        const wdl_json_element* e = root->get_item_by_name("performer");
        if (it == b.performers.end() || !e || !e->is_object()) return false;
        readFields(e, *it);
        return true;
    }
    if (t == "remove") {
        const auto it = find();
        if (it == b.performers.end()) return false;
        b.performers.erase(it);
        return true;
    }
    const std::string name = text(root, "name");
    if (t == "snapNew") {
        b.performers.clear();
        b.current.clear();
        return true;
    }
    if (t == "snapSave") {
        // No name: the one in use. A new name is Save as.
        const std::string to = name.empty() ? b.current : name;
        if (to.empty() || (!b.snapshots.count(to) && b.snapshots.size() >= kMaxSnapshots)) return false;
        b.snapshots[to] = b.performers;
        b.current = to;
        return true;
    }
    if (t == "snapLoad") {
        const auto it = b.snapshots.find(name);
        if (it == b.snapshots.end()) return false;
        b.performers = it->second;
        b.current    = name;
        return true;
    }
    if (t == "snapDelete") {
        if (!b.snapshots.erase(name)) return false;
        if (b.current == name) b.current.clear();
        return true;
    }
    return false;
}

std::string outputsJson(const reasixty::rme::State& st)
{
    std::string o = "[";
    bool first = true;
    auto add = [&](const std::string& spec, const std::string& label) {
        if (!first) o += ",";
        first = false;
        o += "{\"spec\":" + jstr(spec) + ",\"label\":" + jstr(label) + "}";
    };
    const std::pair<const char*, const char*> roles[] = {
        { "phones1", "Phones 1" }, { "phones2", "Phones 2" }, { "phones3", "Phones 3" },
        { "phones4", "Phones 4" }, { "mainB", "Main B" },     { "main", "Main" } };
    for (const auto& [spec, name] : roles)
        if (rmeu::keyOutput(st, spec) >= 0) add(spec, name);
    for (const int ch : rmeu::outputsWithoutRole(st))
        add("output:" + std::to_string(ch), rmeu::displayName(st, rmeu::Row::Output, ch));
    return o + "]";
}

namespace {

// The host without its port, lower case.
std::string hostOnly(std::string h)
{
    if (const std::size_t scheme = h.find("://"); scheme != std::string::npos) h = h.substr(scheme + 3);
    if (const std::size_t slash = h.find('/'); slash != std::string::npos) h = h.substr(0, slash);
    if (const std::size_t colon = h.rfind(':'); colon != std::string::npos) h = h.substr(0, colon);
    for (char& c : h) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return h;
}

bool localName(const std::string& h)
{
    if (h == "localhost" || isPrivate(h)) return true;
    return h.size() > 6 && h.compare(h.size() - 6, 6, ".local") == 0;
}

} // namespace

bool deskAllowed(const std::string& peer, const std::string& xRealIp, const std::string& xForwardedFor,
                 const std::string& host, const std::string& origin)
{
    std::string p = peer;
    if (p.rfind("::ffff:", 0) == 0) p = p.substr(7);
    if (!isPrivate(p)) return false;
    if (!xRealIp.empty() || !xForwardedFor.empty()) return false;
    if (!localName(hostOnly(host))) return false;
    // A browser sends Origin with a WebSocket. A page elsewhere can't fake it.
    if (!origin.empty() && hostOnly(origin) != hostOnly(host)) return false;
    return true;
}

} // namespace orc::phones
